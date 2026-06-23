#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include <geometry_msgs/msg/point.hpp>
#include <ament_index_cpp/get_package_share_directory.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_risk_model/risk_model.hpp"
#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::mid_mpc {

namespace {
// [TBD-HAZID] Safe CPA distance [m] used when ODD state is unavailable.
// Calibrate via HAZID RUN-001 WP-03 (SOTIF CPA threshold).
constexpr double kCpaSafeFallback_m = 1852.0;

// [TBD-HAZID] Default planned speed [m/s] when speed profile is absent.
// Set to nominal cruise speed from scenario YAML (10 kn for FCB imazu tests).
constexpr double kDefaultPlannedSpeed_mps = 5.14;

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
MidMpcNode::MidMpcNode(const Config& cfg)
    : rclcpp::Node("m5_mid_mpc_node"),
      manifest_(mass_l3::m5::shared::CapabilityManifest::load_from_yaml(
          ament_index_cpp::get_package_share_directory("m5_tactical_planner") +
          "/config/fcb_vessel_capability.yaml")),
      vessel_model_(manifest_),
      nomoto_fallback_(nomoto_cfg_, manifest_),
      formulation_(cfg.nlp),
      solver_(formulation_, cfg.ipopt),
      wp_gen_(cfg.waypoint)
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

  pub_avoidance_plan_ = create_publisher<l3_msgs::msg::AvoidancePlan>("/m5/avoidance_plan", 10);
  pub_asdr_record_    = create_publisher<l3_msgs::msg::ASDRRecord>("/m5/asdr_record", 10);
  pub_sat_data_       = create_publisher<l3_msgs::msg::SATData>("/m5/sat_data", 10);
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
    ts.tcpa_s   = tgt.tcpa_s;
    inp.targets.push_back(ts);
  }

  // Normalize heading bounds relative to own ship psi_rad to prevent wrap-around infeasibility
  auto normalize_angle = [](double angle, double ref) {
    const double kPi = 3.14159265358979323846;
    double diff = angle - ref;
    diff = fmod(diff + kPi, 2.0 * kPi);
    if (diff < 0) diff += 2.0 * kPi;
    diff -= kPi;
    return ref + diff;
  };

  double h_min_raw = static_cast<double>(behavior_plan_->heading_min_deg) * units::kRadPerDeg;
  double h_max_raw = static_cast<double>(behavior_plan_->heading_max_deg) * units::kRadPerDeg;
  inp.constraints.heading_min_rad = normalize_angle(h_min_raw, inp.own_ship.psi_rad);
  inp.constraints.heading_max_rad = normalize_angle(h_max_raw, inp.own_ship.psi_rad);

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
    inp.colregs_preferred_direction = mass_l3::m5::parse_colregs_preferred_direction(
        colregs_constraint_->primary_preferred_direction);
    for (const auto& rule : colregs_constraint_->active_rules) {
      const auto rule_id = static_cast<std::uint8_t>(rule.rule_id);
      const bool compiler_supported = rule_id == 14u || rule_id == 15u
          || rule_id == 16u || rule_id == 17u;
      if (compiler_supported
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

  // Dynamically adjust CPA safe distance and target weights based on COLREGs constraint
  double cpa_safe = kCpaSafeFallback_m;
  if (inp.colregs_conflict_active) {
    cpa_safe = 2500.0; // increase CPA boundary during active encounter
    
    // Scale up weights for the primary target causing the collision conflict
    std::string target_id = colregs_constraint_->colregs_chain_target_id;
    for (auto& tgt : inp.targets) {
      if (std::to_string(tgt.id) == target_id) {
        tgt.cpa_m = std::max(tgt.cpa_m * 0.2, 50.0);   // reduce CPA by 80% to boost its MPC cost weight
        tgt.tcpa_s = std::max(tgt.tcpa_s * 0.2, 10.0); // reduce TCPA by 80% to boost its MPC cost weight
      }
    }
  }
  inp.constraints.cpa_safe_m       = cpa_safe;

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
    inp.planned_route_bearing_rad = std::atan2(ddy, ddx);
    const double own_dx = (own_lat - p0_lat) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    const double own_dy = (own_lon - p0_lon) * units::kRadPerDeg * units::kEarthRadiusMean_m
                          * std::cos(p0_lat * units::kRadPerDeg);
    const double route_len = std::hypot(ddx, ddy);
    if (route_len > 1.0) {
      inp.route_xte_m = ((ddx * own_dy) - (ddy * own_dx)) / route_len;
    }
  } else {
    inp.planned_route_bearing_rad = 0.0;
  }

  const bool has_speed = speed_profile_ != nullptr
      && !speed_profile_->target_speeds_kn.empty();
  inp.planned_speed_mps = has_speed
      ? speed_profile_->target_speeds_kn[0] * units::kMsPerKn
      : kDefaultPlannedSpeed_mps;

  const double hs_m = 0.0;  // [TBD-HAZID] sea state from EnvironmentState
  inp.rot_max_rad_s = vessel_model_.rot_max_rad_s(inp.own_ship.u_mps, hs_m);

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
  if (!solver_failed && input.colregs_conflict_active &&
      input.colregs_preferred_direction != ColregsPreferredDirection::Hold &&
      input.colregs_preferred_direction != ColregsPreferredDirection::ReduceSpeed) {
    constexpr double kTargetToleranceRad = 5.0 * units::kRadPerDeg;
    nlp_misses_colregs_target = !mass_l3::m5::trajectory_reaches_colregs_target(
        sol.trajectory,
        input.planned_route_bearing_rad,
        input.constraints.heading_min_rad,
        input.constraints.heading_max_rad,
        input.colregs_min_alteration_rad,
        input.colregs_preferred_direction,
        kTargetToleranceRad);
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
      reason = "nlp_misses_colregs_target";
    } else {
      reason = "m4_geometric";
    }
    plan = build_geometric_fallback_plan_(input, lat, lon, reason);
  } else {
    plan = wp_gen_.generate(sol, lat, lon);
  }
  publish_outputs_(sol, plan);
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

  l3_msgs::msg::AvoidancePlan out_plan = plan;
  out_plan.stamp = now;
  pub_avoidance_plan_->publish(out_plan);

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

}  // namespace mass_l3::m5::mid_mpc
