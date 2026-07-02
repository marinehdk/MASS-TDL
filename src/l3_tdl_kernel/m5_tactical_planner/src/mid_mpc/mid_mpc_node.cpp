#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include <geometry_msgs/msg/point.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_risk_model/risk_model.hpp"
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/avoidance_waypoint_policy.hpp"
#include "m5_tactical_planner/avoidance_route_hash.hpp"
#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/committed_route/committed_route.hpp"
#include "m5_tactical_planner/gnc_avoidance_preflight.hpp"
#include "m5_tactical_planner/mid_mpc/degraded_candidate_adapter.hpp"

namespace mass_l3::m5::mid_mpc {

namespace {
// [TBD-HAZID] Safe CPA distance [m] used when ODD state is unavailable.
// Calibrate via HAZID RUN-001 WP-03 (SOTIF CPA threshold).
constexpr double kCpaSafeFallback_m = 1852.0;

// [TBD-HAZID] Default planned speed [m/s] when speed profile is absent.
// Set to nominal cruise speed from scenario YAML (10 kn for FCB imazu tests).
constexpr double kDefaultPlannedSpeed_mps = 5.14;

// GNC coordinate_transform keeps a wall-time route-update guard. Probe runs use
// accelerated sim time, so repeat the M6-owned release intent long enough for
// the GNC guard to admit one return_to_route update.
constexpr double kReturnToRouteRepublishWindow_s = 30.0;

l3_msgs::msg::AvoidanceWaypoint waypoint_from_route_point(
    double latitude,
    double longitude,
    double speed_mps,
    float confidence,
    const std::string& rationale) {
  l3_msgs::msg::AvoidanceWaypoint wp;
  wp.schema_version = 112;
  wp.position.latitude = latitude;
  wp.position.longitude = longitude;
  wp.position.altitude = 0.0;
  wp.target_speed_kn = speed_mps / units::kMsPerKn;
  wp.confidence = confidence;
  wp.rationale = rationale;
  return wp;
}

l3_external_msgs::msg::AvoidanceWaypoints compatibility_shadow_from_plan(
    const l3_msgs::msg::AvoidancePlan& plan) {
  l3_external_msgs::msg::AvoidanceWaypoints wp;
  wp.stamp = plan.stamp;
  wp.schema_version = 1;
  wp.command_source = "collision_avoidance";
  wp.plan_id = plan.plan_id;
  wp.parent_route_id = plan.parent_route_id;
  wp.behavior_mode = plan.behavior_mode;
  wp.latitude = plan.latitude;
  wp.longitude = plan.longitude;
  wp.command_speed_mps = plan.command_speed_mps;
  wp.navigation_mode = plan.navigation_mode;
  wp.valid_until = plan.valid_until;
  wp.allow_degraded_execution = plan.allow_degraded_execution;
  wp.has_return_to_route_point = plan.has_return_to_route_point;
  wp.return_latitude = plan.return_latitude;
  wp.return_longitude = plan.return_longitude;
  wp.confidence = plan.confidence;
  wp.rationale = plan.rationale;
  return wp;
}

mass_l3::m5::committed_route::CommittedRouteCandidate committed_candidate_from_plan(
    const l3_msgs::msg::AvoidancePlan& plan,
    bool nlp_ok,
    double valid_until_s) {
  mass_l3::m5::committed_route::CommittedRouteCandidate candidate;
  candidate.plan_id = plan.plan_id;
  candidate.valid_until_s = valid_until_s;
  candidate.nlp_ok = nlp_ok;
  candidate.frozen_prefix_count = 0U;
  const std::size_t n = std::min(
      {plan.latitude.size(), plan.longitude.size(), plan.command_speed_mps.size(),
       plan.navigation_mode.size(), plan.segment_source.size()});
  candidate.geometry.reserve(n);
  for (std::size_t i = 0U; i < n; ++i) {
    std::string nav_mode = plan.navigation_mode[i];
    if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED) {
      nav_mode = "MID_MPC_OPTIMIZED";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD) {
      nav_mode = "MID_MPC_TERMINAL_HOLD";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2) {
      nav_mode = "REJOIN_TO_L2";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::L2_NOMINAL_SUFFIX) {
      nav_mode = "L2_NOMINAL_SUFFIX";
    } else if (plan.segment_source[i] == l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR) {
      nav_mode = "DEGRADED_CORRIDOR";
    }
    candidate.geometry.push_back(mass_l3::m5::committed_route::GeoWP{
        plan.latitude[i], plan.longitude[i], plan.command_speed_mps[i], nav_mode});
  }
  return candidate;
}


mass_l3::risk::ColregsDuty colregs_duty_from_role(std::uint8_t primary_role) {
  if (primary_role == 1U) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (primary_role == 2U) {
    return mass_l3::risk::ColregsDuty::BothGiveWay;
  }
  if (primary_role == 0U) {
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  return mass_l3::risk::ColregsDuty::Free;
}

mass_l3::risk::OwnShipInput ownship_risk_input(const TrajectoryPoint& own_ship) {
  return mass_l3::risk::OwnShipInput{
      own_ship.x_m,
      own_ship.y_m,
      own_ship.psi_rad,
      own_ship.u_mps,
      46.0,
      1.0,
      false};
}

mass_l3::risk::TargetInput target_risk_input(const TargetState& target) {
  return mass_l3::risk::TargetInput{
      std::to_string(target.id),
      target.x_m,
      target.y_m,
      target.cog_rad,
      target.sog_mps,
      target.cpa_m,
      target.tcpa_s,
      target.confidence};
}

std::vector<TargetRiskSnapshot> build_target_risk_snapshots(
    const MidMpcInput& input,
    std::uint8_t primary_role,
    mass_l3::risk::RankingState& ranking_state) {
  std::vector<mass_l3::risk::RiskVector> risks;
  risks.reserve(input.targets.size());
  const auto own = ownship_risk_input(input.own_ship);
  const auto duty = colregs_duty_from_role(primary_role);
  for (const auto& target : input.targets) {
    risks.push_back(mass_l3::risk::evaluate_target(
        own,
        target_risk_input(target),
        duty));
  }
  const auto primary = mass_l3::risk::select_primary(risks, &ranking_state);

  std::vector<TargetRiskSnapshot> snapshots;
  snapshots.reserve(risks.size());
  for (const auto& risk : risks) {
    snapshots.push_back(TargetRiskSnapshot{
        risk.target_id,
        risk.risk_score,
        risk.warning_margin_m,
        risk.danger_margin_m,
        risk.tcpa_s,
        risk.closing_speed_mps,
        risk.target_id == primary.target_id});
  }
  return snapshots;
}
}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================
MidMpcNlpFormulation::Config MidMpcNode::resolve_nlp_config_(
    const MidMpcNlpFormulation::Config& cfg)
{
  MidMpcNlpFormulation::Config resolved = cfg;
  const double horizon_s = declare_parameter<double>("mid_mpc.horizon_s",
      static_cast<double>(resolved.n_horizon) * resolved.dt_s);
  const int64_t n_steps = declare_parameter<int64_t>(
      "mid_mpc.n_steps", static_cast<int64_t>(resolved.n_horizon));
  resolved.dt_s = declare_parameter<double>("mid_mpc.dt_s", resolved.dt_s);
  return resolve_mid_mpc_horizon_config(resolved, horizon_s, n_steps, resolved.dt_s);
}

MidMpcWaypointGenerator::Config MidMpcNode::resolve_waypoint_config_(
    const MidMpcWaypointGenerator::Config& cfg,
    double dt_s,
    int32_t n_horizon)
{
  MidMpcWaypointGenerator::Config resolved = cfg;
  resolved.dt_s = dt_s;
  resolved.num_waypoints = std::max(resolved.num_waypoints, n_horizon);
  return resolved;
}

MidMpcNode::MidMpcNode(const Config& cfg)
    : rclcpp::Node("m5_mid_mpc_node"),
      manifest_(mass_l3::m5::shared::CapabilityManifest::load_from_yaml(
          ament_index_cpp::get_package_share_directory("m5_tactical_planner") +
          "/config/fcb_vessel_capability.yaml")),
      vessel_model_(manifest_),
      nomoto_fallback_(nomoto_cfg_, manifest_),
      formulation_(resolve_nlp_config_(cfg.nlp)),
      solver_(formulation_, cfg.ipopt),
      wp_gen_(resolve_waypoint_config_(
          cfg.waypoint, formulation_.config().dt_s, formulation_.config().n_horizon))
{
  formulation_.build_symbolic_graph();

  nominal_speed_kn_ = declare_parameter<double>("m5.nominal_speed_kn", 10.0);

  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        world_state_ = std::move(msg);
      });

  sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", 10,
      [this](l3_msgs::msg::BehaviorPlan::SharedPtr msg) {
        behavior_plan_ = std::move(msg);
      });

  sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", 10,
      [this](l3_msgs::msg::COLREGsConstraint::SharedPtr msg) {
        colregs_constraint_ = std::move(msg);
      });

  sub_route_ = create_subscription<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", 10,
      [this](l3_external_msgs::msg::PlannedRoute::SharedPtr msg) {
        planned_route_ = std::move(msg);
      });

  sub_speed_ = create_subscription<l3_external_msgs::msg::SpeedProfile>(
      "/l2/speed_profile", 10,
      [this](l3_external_msgs::msg::SpeedProfile::SharedPtr msg) {
        speed_profile_ = std::move(msg);
      });

  // Cross-run reset: clear MPC warm state on new scenario. TRANSIENT_LOCAL so
  // we receive the latched scenario_id even though M5 starts after configure.
  sub_scenario_loaded_ = create_subscription<std_msgs::msg::String>(
      "/sil/scenario_loaded",
      rclcpp::QoS(rclcpp::KeepLast(10)).transient_local(),
      [this](std_msgs::msg::String::SharedPtr msg) { on_scenario_loaded_(std::move(msg)); });

  // W2: subscribe the latched GNC execution-ODD contract (forwarded by gnc_bridge
  // from domain 50). transient_local so a late-starting M5 gets the last value.
  sub_gnc_odd_ = create_subscription<ship_interfaces::msg::GncExecutionOdd>(
      "/gnc/execution_odd",
      rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable(),
      [this](ship_interfaces::msg::GncExecutionOdd::SharedPtr msg) {
        std::lock_guard<std::mutex> lk(gnc_odd_mutex_);
        latest_gnc_odd_ = std::move(*msg);
      });

  pub_avoidance_plan_ = create_publisher<l3_msgs::msg::AvoidancePlan>("/l3/m5/avoidance_plan", 10);
  // Track A A3: L3-owned waypoint plan for the GNC bridge.
  pub_avoidance_waypoints_ =
      create_publisher<l3_external_msgs::msg::AvoidanceWaypoints>(
          "/l3/m5/avoidance_waypoints", 10);
  pub_asdr_record_    = create_publisher<l3_msgs::msg::ASDRRecord>("/l3/asdr/record", 10);
  pub_sat_data_       = create_publisher<l3_msgs::msg::SATData>("/l3/sat/data", 10);
  pub_sat3_data_      = create_publisher<l3_msgs::msg::SAT3Data>("/sil/sat3_data", 10);

  nomoto_cfg_.n_steps = 12;
  nomoto_cfg_.dt_s    = 5.0;

  solve_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { on_solve_cycle_(); });
}

// ===========================================================================
// has_required_inputs_
// ===========================================================================
bool MidMpcNode::has_required_inputs_() const noexcept
{
  return world_state_       != nullptr
      && behavior_plan_     != nullptr
      && colregs_constraint_ != nullptr;
}

// ===========================================================================
// assemble_input_ — precondition: has_required_inputs_() == true
// ===========================================================================
MidMpcInput MidMpcNode::assemble_input_()
{
  MidMpcInput inp;

  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  inp.own_ship.psi_rad = world_state_->own_ship.heading_deg * units::kRadPerDeg;
  
  const double u_water = world_state_->own_ship.u_water;
  inp.own_ship.u_mps   = (u_water > 0.1) ? u_water
                         : world_state_->own_ship.sog_kn * units::kMsPerKn;

  const double own_lat = world_state_->own_ship.position.latitude;
  const double own_lon = world_state_->own_ship.position.longitude;

  for (const auto& tgt : world_state_->targets) {
    TargetState ts;
    ts.id       = static_cast<int32_t>(tgt.target_id & 0x7FFFFFFFu);
    ts.x_m      = (tgt.position.latitude  - own_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    ts.y_m      = (tgt.position.longitude - own_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                  * std::cos(own_lat * units::kRadPerDeg);
    ts.sog_mps  = tgt.sog_kn * units::kMsPerKn;
    ts.cog_rad  = tgt.cog_deg * units::kRadPerDeg;
    ts.cpa_m    = tgt.cpa_m;
    ts.cpa_sigma_m = std::sqrt(std::max(tgt.cpa_covariance_m2, 0.0));
    ts.tcpa_s   = tgt.tcpa_s;
    inp.targets.push_back(ts);
    inp.tail_gate_targets.push_back(ts);
  }

  double h_min_raw = static_cast<double>(behavior_plan_->heading_min_deg) * units::kRadPerDeg;
  double h_max_raw = static_cast<double>(behavior_plan_->heading_max_deg) * units::kRadPerDeg;
  // Full-circle M4 window (transit) ⇒ unconstrained [-π, +π]; else unwrap near
  // own_ship psi. Guarantees lb <= ub (CasADi nlpsol asserts it). See
  // resolve_heading_box_bounds + test_heading_bounds.
  const std::pair<double, double> heading_bounds =
      mass_l3::m5::resolve_heading_box_bounds(h_min_raw, h_max_raw, inp.own_ship.psi_rad);
  inp.constraints.heading_min_rad = heading_bounds.first;
  inp.constraints.heading_max_rad = heading_bounds.second;

  inp.constraints.speed_min_mps   = static_cast<double>(behavior_plan_->speed_min_kn) * units::kMsPerKn;
  double speed_max_raw = static_cast<double>(behavior_plan_->speed_max_kn);

  // R3 fix: if M4 is in fallback mode, use nominal speed instead of current SOG
  // to break the cascade slowdown feedback loop.
  const std::string& m4_rationale = behavior_plan_->rationale;
  if (mass_l3::m5::is_m4_fallback_rationale(m4_rationale)) {
    spdlog::info("[M5][MidMPC] M4 fallback detected (rationale: '{}'); using nominal speed {:.1f} kn",
                 m4_rationale, nominal_speed_kn_);
    speed_max_raw = nominal_speed_kn_;
  }

  inp.constraints.speed_max_mps   = speed_max_raw * units::kMsPerKn;
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
  inp.colregs_conflict_active =
      colregs_constraint_ != nullptr && colregs_constraint_->conflict_detected;
  if (colregs_constraint_ != nullptr) {
    inp.colregs_primary_role = colregs_constraint_->primary_role;
    inp.colregs_preferred_direction = mass_l3::m5::parse_colregs_preferred_direction(
        colregs_constraint_->primary_preferred_direction);
    for (const auto& rule : colregs_constraint_->active_rules) {
      const auto rule_id = static_cast<std::uint8_t>(rule.rule_id);
      const bool planner_visible = rule_id == 13u || rule_id == 14u || rule_id == 15u
          || rule_id == 16u || rule_id == 17u;
      if (planner_visible
          && std::find(inp.constraints.applicable_rules.begin(),
                       inp.constraints.applicable_rules.end(),
                       rule_id) == inp.constraints.applicable_rules.end()) {
        inp.constraints.applicable_rules.push_back(rule_id);
      }
    }
    double min_alt_deg = 0.0;
    for (const auto& c : colregs_constraint_->constraints) {
      if (c.constraint_type == "colregs" && c.unit == "deg" && c.numeric_value > 0.0) {
        min_alt_deg = std::max(min_alt_deg, c.numeric_value);
      }
    }
    inp.colregs_min_alteration_rad = min_alt_deg * units::kRadPerDeg;
  }

  if (inp.colregs_conflict_active) {
    inp.target_risks = build_target_risk_snapshots(
        inp,
        colregs_constraint_->primary_role,
        risk_ranking_state_);
    if (behavior_plan_->rationale.find("speed_reduction_preferred=true") != std::string::npos) {
      inp.colregs_preferred_direction = ColregsPreferredDirection::ReduceSpeed;
    }
  }

  // Dynamically adjust CPA safe distance based on COLREGs constraint.
  // Only the SOFT colreg-barrier cpa_safe is bumped here; J_colreg's range-ramp
  // weight (mid_mpc_nlp_formulation build_colreg_cost_) is the correct dynamic
  // weighting mechanism. The earlier target.cpa_m/tcpa_s mutation was dead code
  // — the NLP parameter pack does not read those fields (J_colreg redesign,
  // spec §8.2). target cpa_m/tcpa_s now stay at their M2-sourced values.
  double cpa_safe = kCpaSafeFallback_m;
  if (inp.colregs_conflict_active) {
    cpa_safe = 2500.0; // increase CPA boundary during active encounter
  }
  inp.constraints.cpa_safe_m       = cpa_safe;
  // Hard CPA floor is the un-bumped ODD CPA safe (1852), NOT the cost-scaled
  // cpa_safe above. compile_cpa_distance reads cpa_hard_m; the 2500 bump is for
  // the SOFT colreg barrier only (Bug C deep, RC-C; spec §L84).
  inp.constraints.cpa_hard_m       = kCpaSafeFallback_m;

  const bool has_route = planned_route_ != nullptr
      && planned_route_->route.poses.size() >= 2u;
  if (has_route) {
    const double p0_lat = planned_route_->route.poses[0].pose.position.latitude;
    const double p0_lon = planned_route_->route.poses[0].pose.position.longitude;
    const double p1_lat = planned_route_->route.poses[1].pose.position.latitude;
    const double p1_lon = planned_route_->route.poses[1].pose.position.longitude;
    const double ddx = (p1_lat - p0_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    const double ddy = (p1_lon - p0_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                       * std::cos(p0_lat * units::kRadPerDeg);
    const double route_bearing = std::atan2(ddy, ddx);
    inp.planned_route_bearing_rad = route_bearing;
    const double own_dx = (own_lat - p0_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    const double own_dy = (own_lon - p0_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                          * std::cos(p0_lat * units::kRadPerDeg);
    const double route_len = std::hypot(ddx, ddy);
    if (route_len > 1.0) {
      inp.route_xte_m = ((ddx * own_dy) - (ddy * own_dx)) / route_len;
    }

    // Slice R1: route-frame projection (spec §4.1/§4.2).
    // Origin = own ship relative to the active-leg start (p0), NED.
    // Active-leg normal n = (-sinψ, cosψ) → starboard is positive (spec §3.1).
    inp.route_frame_origin_x_m = own_dx;  // own ship north relative to p0
    inp.route_frame_origin_y_m = own_dy;  // own ship east  relative to p0
    inp.route_frame_normal_x = -std::sin(route_bearing);
    inp.route_frame_normal_y =  std::cos(route_bearing);
    inp.route_frame_active_leg_bearing_rad = route_bearing;
    // l_scale = GncExecutionOdd.max_lateral_offset_m (spec §3.2/§4.3). The
    // execution-ODD ROS msg does not yet carry this field (it lives in the
    // TailBuilder/GNC-preflight local struct, default 400 m); use the spec
    // default here. [TBD-HAZID] wire to the ODD msg field once published.
    inp.lateral_scale_m = 400.0;

    // Cross-leg guard (spec §4.3): extrapolate own_psi straight ahead ~900 m
    // (90 s horizon × ~10 m/s); if that ray passes the active leg end (p1), the
    // NLP trajectory would cross into the next L2 leg → null J_route to avoid
    // pulling toward the wrong normal. Single-segment routes cannot cross a
    // corner, so the guard defaults to active (weight=1.0).
    bool crosses_corner = false;
    const std::size_t n_wp = planned_route_->route.poses.size();
    if (n_wp > 2u && route_len > 1.0) {
      const double reach_m = std::max(
          inp.own_ship.u_mps *
              formulation_.config().n_horizon * formulation_.config().dt_s,
          900.0);
      // Along-track progress of the own_psi ray projected onto the active leg.
      const double own_psi = inp.own_ship.psi_rad;
      const double ex_n = std::cos(own_psi);
      const double ex_e = std::sin(own_psi);
      const double along_proj = (ex_n * ddx + ex_e * ddy) / route_len;
      if (along_proj > 1.0e-6 && reach_m * along_proj > route_len) {
        crosses_corner = true;  // ray reaches beyond p1 → next leg
      }
    }
    inp.route_weight = crosses_corner ? 0.0 : 1.0;
  } else {
    inp.planned_route_bearing_rad = 0.0;
    // No L2 route: disable J_route (no leg to return to) so it does not
    // introduce a spurious lateral setpoint.
    inp.route_weight = 0.0;
  }

  const bool has_speed = speed_profile_ != nullptr
      && !speed_profile_->target_speeds_kn.empty();
  inp.planned_speed_mps = has_speed
      ? speed_profile_->target_speeds_kn[0] * units::kMsPerKn
      : kDefaultPlannedSpeed_mps;

  const double hs_m = 0.0;  // [TBD-HAZID] sea state from EnvironmentState
  inp.rot_max_rad_s = vessel_model_.rot_max_rad_s(inp.own_ship.u_mps, hs_m);
  inp.decel_max_mps2 = std::max(effective_gnc_odd_().max_decel_mps2, 1.0e-6);

  inp.stamp_ns = this->get_clock()->now().nanoseconds();
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
  return inp;
}

// ===========================================================================
// on_solve_cycle_
// ===========================================================================
void MidMpcNode::on_solve_cycle_()
{
  if (!has_required_inputs_()) {
    spdlog::warn("[M5][MidMPC] Skipping cycle: missing required inputs");
    return;
  }

  const MidMpcInput input = assemble_input_();
  publish_trajectory_candidates_(input);
  const bool is_transit =
      behavior_plan_->behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT;
  const bool is_recovery =
      behavior_plan_->behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_RECOVERY;
  const bool wrapped_heading_window = !is_transit && !is_recovery
      && mass_l3::m5::heading_window_is_wrapped(
          input.constraints.heading_min_rad, input.constraints.heading_max_rad);
  formulation_.set_constraint_inputs(input.constraints);
  formulation_.build_symbolic_graph();
  const MidMpcSolution* warm = last_solution_.has_value() ? &last_solution_.value() : nullptr;
  MidMpcSolution sol;
  if (wrapped_heading_window) {
    sol.status = MidMpcSolution::Status::Infeasible;
    sol.stamp_ns = input.stamp_ns;
  } else {
    sol = solver_.solve(input, warm);
    last_solution_ = sol;
  }

  const double lat = world_state_->own_ship.position.latitude;
  const double lon = world_state_->own_ship.position.longitude;

  // Use geometric fallback when NLP solver fails or M4 signals starboard requirement.
  // Solver is a D3.1 stub (Phase 3); geometric arc is the DEMO-1 bridge.
  const bool solver_failed = (sol.status != MidMpcSolution::Status::Converged)
      || sol.trajectory.empty();
  const bool m4_geometric =
      behavior_plan_->rationale.find("geometric starboard") != std::string::npos
      || behavior_plan_->rationale.find("fallback") != std::string::npos;
  bool nlp_misses_colregs_target = false;
  std::string nlp_reject_reason;
  if (!solver_failed && input.colregs_conflict_active) {
    const auto tail_gate = mass_l3::m5::accept_tail_gate(sol, input);
    nlp_misses_colregs_target = !tail_gate.accepted;
    nlp_reject_reason = tail_gate.reason;
    if (nlp_misses_colregs_target) {
      spdlog::warn("[M5][TailGate] reject optimized NLP candidate: {}", nlp_reject_reason);
    }
  }

  l3_msgs::msg::AvoidancePlan plan;
  if (is_recovery) {
    // Phase 4: M4 RECOVERY — gradual return-to-route. Bypass NLP solver and
    // emit a direct XTE-decay trajectory toward the route projection.
    plan = build_recovery_plan_(input, lat, lon);
  } else if (is_transit) {
    // D-DEMO1 spin fix: M4 is the COLREG authority on whether avoidance is
    // active. When M4 is in TRANSIT, emit an EMPTY plan (no waypoints) so the
    // execution bridge releases avoidance and resumes route-following. Without
    // this the geometric fallback below keeps a VALID plan alive forever (the
    // NLP solver is a Phase-3 stub that never converges → solver_failed always
    // true), trapping the bridge in avoidance → endless circling, no return.
    plan.schema_version = 112;
    plan.status     = "NORMAL";
    plan.rationale  = "M4 TRANSIT — no avoidance required";
    plan.confidence = 1.0F;
    // waypoints left empty → bridge has_valid_plan == False → avoidance released
  } else if (wrapped_heading_window || solver_failed || m4_geometric ||
             nlp_misses_colregs_target) {
    std::string reason;
    if (wrapped_heading_window) {
      reason = "wrapped_heading_window";
    } else if (solver_failed) {
      reason = "solver_status=" + std::to_string(static_cast<int>(sol.status));
    } else if (nlp_misses_colregs_target) {
      reason = nlp_reject_reason.empty() ? "nlp_tail_gate_failed" : nlp_reject_reason;
    } else {
      reason = "m4_geometric";
    }
    plan = build_geometric_fallback_plan_(input, lat, lon, reason);
  } else {
    plan = wp_gen_.generate(sol, lat, lon);
  }
  publish_outputs_(sol, plan);
  // Track A A3: mirror the intent on the L3-owned waypoint plan so the GNC
  // bridge can translate it. Release authority lives here (spec D4): while M6
  // reports conflict we keep a rolling avoidance plan; on conflict-clear we
  // keep publishing the same return intent briefly so GNC update guards cannot
  // drop the only release message.
  publish_avoidance_waypoints_(this->get_clock()->now(), input, lat, lon, plan, sol);
}

// ===========================================================================
// build_geometric_fallback_plan_
// ===========================================================================
l3_msgs::msg::AvoidancePlan MidMpcNode::build_geometric_fallback_plan_(
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg,
    const std::string& reason)
{
  // Use nominal speed if current speed is too low to be meaningful
  const double target_speed_kn = mass_l3::m5::geometric_fallback_target_speed_kn(
      input.planned_speed_mps, nominal_speed_kn_, input.constraints.speed_max_mps);
  const double u_mps = (input.own_ship.u_mps > 0.5)
      ? input.own_ship.u_mps
      : (target_speed_kn * units::kMsPerKn);

  const double own_psi = input.own_ship.psi_rad;

  // R12.B superseded: magnitude from M6 minimum alteration (route-relative), clamped to window.
  const double h_min = input.constraints.heading_min_rad;
  const double h_max = input.constraints.heading_max_rad;
  const double route_brg = input.planned_route_bearing_rad;
  double min_alt_rad = input.colregs_min_alteration_rad;
  min_alt_rad = mass_l3::m5::fallback_min_alteration_rad(
      route_brg, h_min, h_max, min_alt_rad);
  const auto fallback_direction = mass_l3::m5::risk_aware_fallback_direction(input);
  double target_psi = mass_l3::m5::risk_aware_fallback_target_heading(
      input, route_brg, h_min, h_max, min_alt_rad, fallback_direction);

  const double delta_psi = mass_l3::m5::geometric_fallback_delta_heading_rad(
      own_psi, target_psi);
  const double turn_radius_m = mass_l3::m5::geometric_fallback_turn_radius_m(
      u_mps, input.rot_max_rad_s);

  constexpr int kNWp = 10;

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 112;
  plan.stamp = this->get_clock()->now();
  plan.horizon_s = static_cast<float>(
      mass_l3::m5::geometric_fallback_waypoint_time_s(kNWp - 1));
  plan.status = "DEGRADED";
  plan.confidence = 0.6f;
  plan.rationale = "M5 geometric COLREG fallback (" + reason + ")"
      + " turn_r=" + std::to_string(static_cast<int>(turn_radius_m)) + "m"
      + " tgt=" + std::to_string(static_cast<int>(target_psi * units::kDegPerRad)) + "deg"
      + " risk_dir=" + std::to_string(static_cast<int>(fallback_direction));

  double prev_xN = 0.0;
  double prev_xE = 0.0;

  for (int i = 0; i < kNWp; ++i) {
    const double t = mass_l3::m5::geometric_fallback_waypoint_time_s(i);
    const auto point = mass_l3::m5::geometric_fallback_arc_point(
        own_psi, target_psi, u_mps, input.rot_max_rad_s, t);
    const double xN = point.x_m;
    const double xE = point.y_m;

    // Segment distance from previous waypoint
    const double ddN = xN - prev_xN;
    const double ddE = xE - prev_xE;
    const double seg_dist = std::sqrt(ddN * ddN + ddE * ddE);
    prev_xN = xN;
    prev_xE = xE;

    // Flat-earth NED → WGS84 (same approximation as WaypointGenerator::ned_to_geopoint_)
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.schema_version = 112;
    wp.position.latitude  = lat0_deg
        + (xN / units::kEarthRadiusMean_m) * units::kDegPerRad;
    wp.position.longitude = lon0_deg
        + (xE / (units::kEarthRadiusMean_m
                 * std::cos(lat0_deg * units::kRadPerDeg)))
        * units::kDegPerRad;
    wp.position.altitude  = 0.0;
    wp.wp_distance_m      = seg_dist;
    wp.safety_corridor_m  = 500.0;
    wp.target_speed_kn    = target_speed_kn;
    wp.turn_radius_m      = turn_radius_m;
    wp.confidence         = 0.6f;
    wp.rationale          = "M5 geometric COLREG fallback";

    plan.waypoints.push_back(wp);
  }

  spdlog::info("[M5][GeoFallback] {} wps: turn_r={:.0f}m tgt_psi={:.1f}deg "
               "own_psi={:.1f}deg delta={:.1f}deg aggression={:.4f} reason={}",
               kNWp, turn_radius_m,
               target_psi * units::kDegPerRad,
               own_psi * units::kDegPerRad,
               delta_psi * units::kDegPerRad,
               min_alt_rad * units::kDegPerRad,
               reason);

  return plan;
}

// ===========================================================================
// build_recovery_plan_ — Phase 4 gradual return-to-route (architecture §7.2)
// ===========================================================================
l3_msgs::msg::AvoidancePlan MidMpcNode::build_recovery_plan_(
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg)
{
  const double target_speed_kn = mass_l3::m5::geometric_fallback_target_speed_kn(
      input.planned_speed_mps, nominal_speed_kn_, input.constraints.speed_max_mps);
  const double speed_mps = target_speed_kn * units::kMsPerKn;

  constexpr int kNWp = 6;
  const double horizon_s =
      mass_l3::m5::geometric_fallback_waypoint_time_s(kNWp - 1);

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 112;
  plan.stamp = this->get_clock()->now();
  plan.horizon_s = static_cast<float>(horizon_s);
  plan.status = "RECOVERY";
  plan.confidence = 0.8f;
  plan.rationale = "M5 RECOVERY gradual return to planned route"
      " xte=" + std::to_string(static_cast<int>(input.route_xte_m)) + "m";

  double prev_xN = 0.0;
  double prev_xE = 0.0;
  for (int i = 0; i < kNWp; ++i) {
    const double t = mass_l3::m5::geometric_fallback_waypoint_time_s(i);
    const auto point = mass_l3::m5::recovery_route_point(
        input.planned_route_bearing_rad,
        input.route_xte_m,
        speed_mps,
        t,
        horizon_s);

    const double ddN = point.x_m - prev_xN;
    const double ddE = point.y_m - prev_xE;
    const double seg_dist = std::sqrt(ddN * ddN + ddE * ddE);
    prev_xN = point.x_m;
    prev_xE = point.y_m;

    // Flat-earth NED → WGS84 (same approximation as geometric fallback).
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.schema_version = 112;
    wp.position.latitude  = lat0_deg
        + (point.x_m / units::kEarthRadiusMean_m) * units::kDegPerRad;
    wp.position.longitude = lon0_deg
        + (point.y_m / (units::kEarthRadiusMean_m
                         * std::cos(lat0_deg * units::kRadPerDeg)))
        * units::kDegPerRad;
    wp.position.altitude  = 0.0;
    wp.wp_distance_m      = seg_dist;
    wp.safety_corridor_m  = 500.0;
    wp.target_speed_kn    = target_speed_kn;
    wp.turn_radius_m      = mass_l3::m5::geometric_fallback_turn_radius_m(
        speed_mps, input.rot_max_rad_s);
    wp.confidence         = 0.8f;
    wp.rationale          = "M5 RECOVERY return-to-route";
    plan.waypoints.push_back(wp);
  }

  spdlog::info("[M5][Recovery] {} wps: xte={:.0f}m route_brg={:.1f}deg speed={:.1f}kn",
               kNWp, input.route_xte_m,
               input.planned_route_bearing_rad * units::kDegPerRad,
               target_speed_kn);

  return plan;
}

// ===========================================================================
// publish_outputs_
// ===========================================================================
void MidMpcNode::publish_outputs_(const MidMpcSolution& sol,
                                   const l3_msgs::msg::AvoidancePlan& plan)
{
  const auto now = this->get_clock()->now();

  // Slice A: /l3/m5/avoidance_plan is now the event-driven committed-route
  // execution truth. publish_outputs_ keeps ASDR/SAT audit only; route snapshots
  // are emitted by publish_avoidance_plan_ when geometry changes or heartbeat expires.

  const std::string planner_health =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.status == "DEGRADED" ? "GEOMETRIC_FALLBACK" :
       (plan.waypoints.empty() ? "EMPTY_TRANSIT" : "SOLVER_CONVERGED"));
  const std::string semantic_mode =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.waypoints.empty() ? "TRANSIT" : "AVOIDANCE");
  std::string fallback_reason = "none";
  if (plan.status == "DEGRADED") {
    const std::string marker = "fallback (";
    const auto begin = plan.rationale.find(marker);
    if (begin != std::string::npos) {
      const auto reason_begin = begin + marker.size();
      const auto reason_end = plan.rationale.find(')', reason_begin);
      fallback_reason = plan.rationale.substr(
          reason_begin,
          reason_end == std::string::npos ? std::string::npos : reason_end - reason_begin);
    } else {
      fallback_reason = "unknown";
    }
  }

  const std::string json =
      std::string("{\"status\":\"") + plan.status
      + "\",\"planner_health\":\"" + planner_health
      + "\",\"semantic_mode\":\"" + semantic_mode
      + "\",\"fallback_reason\":\"" + fallback_reason
      + "\",\"waypoints\":"  + std::to_string(plan.waypoints.size())
      + ",\"solve_ms\":"     + std::to_string(sol.solve_duration_ms)
      + ",\"ipopt_iter\":"   + std::to_string(sol.ipopt_iterations)
      + ",\"solver_status\":" + std::to_string(static_cast<int>(sol.status))
      + "}";

  l3_msgs::msg::ASDRRecord record;
  record.stamp         = now;
  record.source_module = "M5_Tactical_Planner";
  record.decision_type = "avoid_wp";
  record.decision_json = json;
  const auto digest = mass_l3::m5::common::sha256(json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_record_->publish(record);

  l3_msgs::msg::SATData sat;
  sat.stamp                   = now;
  sat.source_module           = "M5_Tactical_Planner";
  sat.sat2.trigger_reason     = "mpc_cycle";
  sat.sat2.reasoning_chain    =
      plan.rationale + "; planner_health=" + planner_health
      + "; semantic_mode=" + semantic_mode
      + "; fallback_reason=" + fallback_reason;
  sat.sat2.system_confidence  = plan.confidence;
  pub_sat_data_->publish(sat);
}


// ===========================================================================
// publish_avoidance_plan_ — Slice A committed-route execution truth
// ===========================================================================
void MidMpcNode::publish_avoidance_plan_(
    const l3_msgs::msg::AvoidancePlan& plan,
    const std::string& reason)
{
  const auto now = this->get_clock()->now();
  l3_msgs::msg::AvoidancePlan out = plan;
  out.schema_version = 114;
  out.stamp = now;
  out.route_hash = mass_l3::m5::avoidance_route_hash(out);

  const bool route_changed = !last_published_route_hash_.has_value()
      || last_published_route_hash_.value() != out.route_hash;
  const bool heartbeat_due = !last_avoidance_plan_publish_time_.has_value()
      || ((now - last_avoidance_plan_publish_time_.value()).seconds() >= kAvoidancePlanHeartbeat_s);

  if (!route_changed && !heartbeat_due) {
    return;
  }

  // Heartbeat refreshes valid_until without forcing a new revision/hash.
  out.valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
  pub_avoidance_plan_->publish(out);
  last_published_route_hash_ = out.route_hash;
  last_avoidance_plan_publish_time_ = now;
  RCLCPP_INFO(get_logger(),
      "[M5][AvoidancePlan] publish reason=%s changed=%d heartbeat=%d points=%zu hash=%u",
      reason.c_str(), route_changed ? 1 : 0, heartbeat_due ? 1 : 0,
      out.latitude.size(), out.route_hash);
}

// ===========================================================================
// publish_trajectory_candidates_ — DEMO-2 P0: SAT3Data with Nomoto fallback
// ===========================================================================
void MidMpcNode::publish_trajectory_candidates_(const MidMpcInput& input)
{
  auto sol = nomoto_fallback_.solve(input);
  const auto now = this->get_clock()->now();

  l3_msgs::msg::SAT3Data sat3;
  sat3.stamp = now;
  sat3.schema_version = 112;

  const std::size_t n_candidates = sol.trajectories.size();
  for (std::size_t i = 0; i < n_candidates; ++i) {
    l3_msgs::msg::TrajectoryCandidate tc;
    tc.stamp = now;
    tc.confidence = 1.0F;
    tc.branch_index = static_cast<int32_t>(i);
    tc.heading_deg = sol.headings_rad[i] * 180.0 / M_PI;
    tc.speed_kn = input.own_ship.u_mps * 1.94384;
    tc.cpa_m = sol.cpa_vals[i];
    tc.rule_compliant = false;
    tc.is_primary = (i == static_cast<std::size_t>(sol.primary_branch_idx));

    for (const auto& pt : sol.trajectories[i]) {
      geometry_msgs::msg::Point p;
      p.x = pt.x();
      p.y = pt.y();
      p.z = 0.0;
      tc.waypoints.push_back(p);
    }

    sat3.trajectory_candidates[i] = tc;
  }
  sat3.primary_trajectory_idx = static_cast<uint8_t>(sol.primary_branch_idx);

  pub_sat3_data_->publish(sat3);
}

// ===========================================================================
// publish_avoidance_waypoints_ (Track A A3)
// Mirrors the avoidance intent on the L3-owned waypoint plan
// (/l3/m5/avoidance_waypoints) for the GNC bridge to translate to
// ship_interfaces/AvoidancePlan. The bridge is a pure field-mapper, so all
// waypoint geometry is generated here. Release authority (spec D4): while M6
// reports conflict we emit one encounter-anchored avoidance corridor; on the
// conflict->clear transition we emit a stable current-anchored rejoin corridor,
// then stop emitting.
// ===========================================================================
void MidMpcNode::publish_avoidance_waypoints_(
    rclcpp::Time now,
    const MidMpcInput& input,
    double lat0_deg,
    double lon0_deg,
    const l3_msgs::msg::AvoidancePlan& selected_plan,
    const MidMpcSolution& sol) {
  const bool collision_avoidance_authorized =
      behavior_plan_ != nullptr &&
      mass_l3::m5::should_emit_collision_avoidance_waypoints(
          input.colregs_conflict_active, behavior_plan_->behavior);
  const bool conflict_active = collision_avoidance_authorized;

  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 114;
  plan.stamp = now;
  plan.command_source = "m5_committed_route";
  plan.confidence = 0.8F;
  // Slice D owns keep-last stale transitions. Slice A emits fresh route snapshots,
  // so zero is the explicit non-stale/unset value.
  plan.stale_committed_at.sec = 0;
  plan.stale_committed_at.nanosec = 0;

  const bool return_republish_active = return_to_route_emit_until_.has_value() &&
      ((*return_to_route_emit_until_ - now).seconds() > 0.0);

  if (input.colregs_conflict_active && !collision_avoidance_authorized) {
    avoidance_corridor_anchor_.reset();
    return_route_anchor_.reset();
    return_to_route_emit_until_.reset();
    last_emitted_conflict_active_ = false;
    return;
  }

  if (conflict_active && selected_plan.status == "NORMAL" && !selected_plan.waypoints.empty()) {
    return_to_route_emit_until_.reset();
    return_route_anchor_.reset();
    avoidance_corridor_anchor_.reset();
    plan = selected_plan;
    populate_canonical_route_from_selected_plan(
        plan,
        sol,
        "m5-midmpc-" + std::to_string(now.nanoseconds()),
        "nominal",
        mass_l3::m5::gnc_avoidance_navigation_mode(/*colregs_overtake_corridor=*/false));
    plan.valid_until = (now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s));
    if (!append_l2_nominal_suffix_if_preflight_feasible(
            plan, planned_route_, {lat0_deg, lon0_deg}, input.planned_speed_mps)) {
      RCLCPP_WARN(get_logger(),
          "[M5][GNCPreflight] reject L2 nominal suffix for optimized plan_id=%s; publishing selected route without suffix",
          plan.plan_id.c_str());
    }
    const auto preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg});
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible optimized plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          plan.plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    if (!committed_route_manager_.try_revise(
            committed_candidate_from_plan(plan, true, (now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s)).seconds()),
            now.seconds())) {
      RCLCPP_WARN(get_logger(),
          "[M5][CommittedRoute] reject optimized candidate plan_id=%s event=%s",
          plan.plan_id.c_str(), committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
  } else if (conflict_active) {
    return_to_route_emit_until_.reset();
    return_route_anchor_.reset();
    const bool colregs_overtake_corridor =
        mass_l3::m5::requires_colregs_overtake_corridor(
            input.colregs_conflict_active, input.constraints.applicable_rules);
    const double heading_min_deg =
        input.constraints.heading_min_rad / units::kRadPerDeg;
    const double heading_max_deg =
        input.constraints.heading_max_rad / units::kRadPerDeg;
    const double command_speed_mps =
        mass_l3::m5::gnc_avoidance_command_speed_mps(
            input.planned_speed_mps, colregs_overtake_corridor);
    const std::string navigation_mode =
        mass_l3::m5::gnc_avoidance_navigation_mode(colregs_overtake_corridor);
    const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
        input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
    const bool need_new_anchor =
        !avoidance_corridor_anchor_.has_value()
        || avoidance_corridor_anchor_->direction != input.colregs_preferred_direction;
    if (need_new_anchor) {
      avoidance_corridor_anchor_ = AvoidanceCorridorAnchor{
          lat0_deg,
          lon0_deg,
          heading_min_deg,
          heading_max_deg,
          command_speed_mps,
          route_frame.bearing_rad,
          route_frame.reversed ? -1.0 : 1.0,
          input.colregs_preferred_direction,
          colregs_overtake_corridor,
          "m5-colregs-" + std::to_string(now.nanoseconds())};
    }

    const auto& anchor = avoidance_corridor_anchor_.value();
    // Keep one encounter-anchored avoidance corridor active until M6 clears.
    // GNC follows waypoint geometry; regenerating from current position each cycle
    // makes XTE look healthy while diluting the global COLREG maneuver.
    plan.behavior_mode = navigation_mode;
    plan.parent_route_id = "nominal";
    plan.plan_id = anchor.plan_id;
    // W4-B: convert live targets to local NED relative to the corridor anchor.
    // TargetState.x_m/y_m are NED relative to own; the corridor anchor is fixed
    // at conflict onset, so shift each target by own-relative-to-anchor.
    std::vector<mass_l3::m5::TargetTrackPoint> target_tracks;
    target_tracks.reserve(input.targets.size());
    const double m_per_deg_lon_anchor =
        mass_l3::m5::kMetersPerDegLat * std::cos(anchor.lat_deg * M_PI / 180.0);
    const double own_n_rel_anchor =
        (lat0_deg - anchor.lat_deg) * mass_l3::m5::kMetersPerDegLat;
    const double own_e_rel_anchor =
        (lon0_deg - anchor.lon_deg) * m_per_deg_lon_anchor;
    for (const auto& tgt : input.targets) {
      const double tgt_n = own_n_rel_anchor + tgt.x_m;
      const double tgt_e = own_e_rel_anchor + tgt.y_m;
      target_tracks.push_back({tgt_n, tgt_e, tgt.cog_rad, tgt.sog_mps});
    }

    constexpr double kRule13OvertakeTaperStartM = 7500.0;
    constexpr double kRule13OvertakeTaperEndM = 12000.0;
    const auto wps = colregs_overtake_corridor
        ? mass_l3::m5::generate_rule13_overtake_corridor_waypoints(
            anchor.heading_min_deg,
            anchor.heading_max_deg,
            anchor.lat_deg,
            anchor.lon_deg,
            anchor.route_bearing_rad,
            anchor.direction,
            kRule13OvertakeTaperStartM,
            kRule13OvertakeTaperEndM)
        : mass_l3::m5::generate_target_safe_corridor_waypoints(
            anchor.heading_min_deg,
            anchor.heading_max_deg,
            anchor.lat_deg,
            anchor.lon_deg,
            anchor.route_bearing_rad,
            anchor.direction,
            target_tracks,
            /*own_n=*/0.0,
            /*own_e=*/0.0);
    if (!colregs_overtake_corridor && !target_tracks.empty()) {
      double corridor_max_east = 0.0;
      const double m_per_deg_lon_wps =
          mass_l3::m5::kMetersPerDegLat * std::cos(anchor.lat_deg * M_PI / 180.0);
      for (const auto& w : wps) {
        corridor_max_east = std::max(
            corridor_max_east, (w.lon - anchor.lon_deg) * m_per_deg_lon_wps);
      }
      RCLCPP_INFO(get_logger(),
          "[M5][W4] target-safe corridor: %zu targets, corridor peak east=%.0fm (default cap 270m)",
          target_tracks.size(), corridor_max_east);
    }
    const std::vector<double> speeds(wps.size(), anchor.command_speed_mps);
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds);
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible avoidance plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          anchor.plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    DegradedCandidateRequest degraded_request;
    degraded_request.plan_id = anchor.plan_id;
    degraded_request.parent_route_id = "nominal";
    degraded_request.behavior_mode = navigation_mode;
    degraded_request.confidence = 0.8F;
    degraded_request.rationale = colregs_overtake_corridor
        ? "Rule13 overtake corridor candidate"
        : "encounter-anchored avoidance corridor candidate";
    degraded_request.nlp_unavailable = selected_plan.status == "DEGRADED" ||
        sol.status != MidMpcSolution::Status::Converged;
    degraded_request.committed_route_can_continue =
        !committed_route_manager_.current().active_geometry.empty() &&
        !committed_route_manager_.should_enter_degraded_hold(now.seconds());
    degraded_request.has_return_to_route_point = false;
    degraded_request.safety_concern_event = "m5_degraded_corridor_no_return_route";
    degraded_request.points.reserve(wps.size());
    for (std::size_t i = 0; i < wps.size(); ++i) {
      degraded_request.points.push_back(
          DegradedCandidatePoint{wps[i].lat, wps[i].lon, speeds[i], navigation_mode});
    }
    const auto valid_until = now + rclcpp::Duration::from_seconds(kAvoidancePlanTtl_s);
    const auto degraded_plan = build_committed_degraded_candidate_plan(
        degraded_request, committed_route_manager_, now.seconds(), valid_until.seconds());
    if (!degraded_plan.has_value()) {
      RCLCPP_WARN(get_logger(),
          "[M5][DegradedCandidate] drop avoidance plan_id=%s reason=committed_route_rejected event=%s",
          anchor.plan_id.c_str(), committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    plan = degraded_plan.value();
    plan.stamp = now;
    plan.valid_until = valid_until;
  } else if (last_emitted_conflict_active_ || return_republish_active) {
    if (last_emitted_conflict_active_) {
      return_to_route_emit_until_ =
          now + rclcpp::Duration::from_seconds(kReturnToRouteRepublishWindow_s);
      const bool colregs_overtake_rejoin =
          avoidance_corridor_anchor_.has_value() &&
          avoidance_corridor_anchor_->colregs_overtake_corridor;
      const double command_speed_mps =
          mass_l3::m5::gnc_return_command_speed_mps(
              input.planned_speed_mps, colregs_overtake_rejoin);
      const std::string navigation_mode =
          mass_l3::m5::gnc_return_navigation_mode(colregs_overtake_rejoin);
      double return_route_bearing_rad = input.planned_route_bearing_rad;
      double return_route_xte_m = input.route_xte_m;
      if (avoidance_corridor_anchor_.has_value()) {
        return_route_bearing_rad = avoidance_corridor_anchor_->route_bearing_rad;
        return_route_xte_m = avoidance_corridor_anchor_->route_xte_sign * input.route_xte_m;
      } else {
        const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
            input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
        return_route_bearing_rad = route_frame.bearing_rad;
        return_route_xte_m = route_frame.route_xte_m;
      }
      const auto return_waypoints = mass_l3::m5::generate_return_to_route_waypoints(
          lat0_deg, lon0_deg, return_route_bearing_rad, return_route_xte_m);
      return_route_anchor_ = ReturnRouteAnchor{
          return_waypoints,
          command_speed_mps,
          navigation_mode,
          "m5-return-" + std::to_string(now.nanoseconds())};
    }
    avoidance_corridor_anchor_.reset();
    if (!return_route_anchor_.has_value()) {
      const double command_speed_mps =
          mass_l3::m5::gnc_return_command_speed_mps(
              input.planned_speed_mps, /*colregs_overtake_rejoin=*/false);
      const auto route_frame = mass_l3::m5::align_route_frame_with_heading(
          input.planned_route_bearing_rad, input.route_xte_m, input.own_ship.psi_rad);
      return_route_anchor_ = ReturnRouteAnchor{
          mass_l3::m5::generate_return_to_route_waypoints(
              lat0_deg, lon0_deg, route_frame.bearing_rad, route_frame.route_xte_m),
          command_speed_mps,
          mass_l3::m5::gnc_return_navigation_mode(/*colregs_overtake_rejoin=*/false),
          "m5-return-" + std::to_string(now.nanoseconds())};
    }
    // conflict -> clear transition: repeat return_to_route briefly so the GNC
    // route-update guard cannot drop the only lifecycle-release message.
    plan.behavior_mode             = "return_to_route";
    plan.parent_route_id           = "nominal";
    plan.plan_id                   = return_route_anchor_->plan_id;
    plan.has_return_to_route_point = true;
    const auto& wps = return_route_anchor_->waypoints;
    const std::vector<double> speeds(wps.size(), return_route_anchor_->command_speed_mps);
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {lat0_deg, lon0_deg}, wps, speeds);
    if (!preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible return plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          return_route_anchor_->plan_id.c_str(), preflight.reason.c_str(), preflight.index,
          preflight.required_m, preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    DegradedCandidateRequest degraded_return_request;
    degraded_return_request.plan_id = return_route_anchor_->plan_id;
    degraded_return_request.parent_route_id = "nominal";
    degraded_return_request.behavior_mode = "return_to_route";
    degraded_return_request.confidence = 0.8F;
    degraded_return_request.rationale =
        return_route_anchor_->navigation_mode == "colregs_overtake"
            ? "Rule13 return_to_route candidate on M6 conflict-clear"
            : "return_to_route candidate on M6 conflict-clear";
    degraded_return_request.nlp_unavailable = selected_plan.status == "DEGRADED" ||
        sol.status != MidMpcSolution::Status::Converged;
    degraded_return_request.committed_route_can_continue =
        !committed_route_manager_.current().active_geometry.empty() &&
        !committed_route_manager_.should_enter_degraded_hold(now.seconds());
    degraded_return_request.has_return_to_route_point = true;
    degraded_return_request.return_latitude = wps.back().lat;
    degraded_return_request.return_longitude = wps.back().lon;
    degraded_return_request.points.reserve(wps.size());
    for (std::size_t i = 0; i < wps.size(); ++i) {
      degraded_return_request.points.push_back(DegradedCandidatePoint{
          wps[i].lat,
          wps[i].lon,
          speeds[i],
          return_route_anchor_->navigation_mode});
    }
    const auto valid_until = now + rclcpp::Duration::from_seconds(kReturnToRouteRepublishWindow_s);
    const auto degraded_return_plan = build_committed_degraded_candidate_plan(
        degraded_return_request, committed_route_manager_, now.seconds(), valid_until.seconds());
    if (!degraded_return_plan.has_value()) {
      RCLCPP_WARN(get_logger(),
          "[M5][DegradedCandidate] drop return plan_id=%s reason=committed_route_rejected event=%s",
          return_route_anchor_->plan_id.c_str(),
          committed_route_manager_.current().safety_concern_event.c_str());
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    plan = degraded_return_plan.value();
    plan.stamp = now;
    plan.valid_until = valid_until;
  } else {
    // No conflict and already returned: emit nothing this cycle.
    last_emitted_conflict_active_ = conflict_active;
    return;
  }

  if (plan.status != "NORMAL") {
    if (plan.status != "DEGRADED" &&
        !append_l2_nominal_suffix_if_preflight_feasible(
            plan, planned_route_, {lat0_deg, lon0_deg}, input.planned_speed_mps)) {
      RCLCPP_WARN(get_logger(),
          "[M5][GNCPreflight] reject L2 nominal suffix for plan_id=%s; publishing preflighted base route without suffix",
          plan.plan_id.c_str());
    }
    const auto full_preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg});
    if (!full_preflight.feasible) {
      RCLCPP_WARN(
          get_logger(),
          "[M5][GNCPreflight] drop infeasible full route plan_id=%s reason=%s idx=%zu required=%.1f available=%.1f",
          plan.plan_id.c_str(), full_preflight.reason.c_str(), full_preflight.index,
          full_preflight.required_m, full_preflight.available_m);
      last_emitted_conflict_active_ = conflict_active;
      return;
    }
    plan.status = (plan.behavior_mode == "return_to_route") ? "RECOVERY" : "DEGRADED";
    plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
    plan.nlp_kkt_residual = 0.0F;
    plan.nlp_tail_gate_failed = (plan.behavior_mode != "return_to_route");
    mass_l3::m5::apply_tail_gate_publish_contract(input, plan);
  }
  plan.waypoints.clear();
  plan.waypoints.reserve(plan.latitude.size());
  for (std::size_t i = 0; i < plan.latitude.size(); ++i) {
    const double speed = i < plan.command_speed_mps.size() ? plan.command_speed_mps[i] : 0.0;
    plan.waypoints.push_back(waypoint_from_route_point(
        plan.latitude[i], plan.longitude[i], speed, plan.confidence, plan.rationale));
  }

  // Slice A: /l3/m5/avoidance_plan is the canonical execution truth. The legacy
  // waypoint topic is derived from that complete route snapshot as a compatibility shadow.
  publish_avoidance_plan_(plan, plan.behavior_mode);
  last_emitted_conflict_active_ = conflict_active;
  pub_avoidance_waypoints_->publish(compatibility_shadow_from_plan(plan));
}

void MidMpcNode::on_scenario_loaded_(const std_msgs::msg::String::SharedPtr msg) {
  RCLCPP_INFO(get_logger(),
      "scenario_loaded '%s' — resetting MPC warm state", msg->data.c_str());
  reset_cross_run_state();
}

ship_interfaces::msg::GncExecutionOdd MidMpcNode::effective_gnc_odd_() const {
  std::lock_guard<std::mutex> lk(gnc_odd_mutex_);
  if (!latest_gnc_odd_.schema_version.empty()) {
    return latest_gnc_odd_;
  }
  // Fallback to hardcoded defaults matching gnc_avoidance_preflight.hpp when no
  // live ODD msg has arrived yet (e.g. unit-test build, or gnc_bridge not up).
  ship_interfaces::msg::GncExecutionOdd fallback;
  fallback.emergency_avoidance_speed_cap_mps = 3.2;
  fallback.cruise_min_speed_mps = 3.8;
  fallback.max_transit_speed_mps = 3.0;
  fallback.max_lateral_accel_mps2 = 0.25;
  fallback.max_decel_mps2 = 0.08;
  fallback.emergency_min_turn_radius_m = 45.0;
  fallback.emergency_max_yaw_rate_deg_s = 2.0;
  return fallback;
}

void MidMpcNode::reset_cross_run_state() {
  // Drop the warm-start solution so the next scenario cold-starts the solver
  // instead of inheriting the prior run's MPC trajectory. Also reset the
  // ranking history (accumulated risk-ranking state).
  last_solution_.reset();
  risk_ranking_state_ = mass_l3::risk::RankingState{};
  last_emitted_conflict_active_ = false;
  return_to_route_emit_until_.reset();
  avoidance_corridor_anchor_.reset();
  return_route_anchor_.reset();
  last_published_route_hash_.reset();
  last_avoidance_plan_publish_time_.reset();
}

}  // namespace mass_l3::m5::mid_mpc
