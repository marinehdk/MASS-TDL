#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include <geometry_msgs/msg/point.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::mid_mpc {

namespace {
// [TBD-HAZID] Safe CPA distance [m] used when ODD state is unavailable.
// Calibrate via HAZID RUN-001 WP-03 (SOTIF CPA threshold).
constexpr double kCpaSafeFallback_m = 1852.0;

// [TBD-HAZID] Default planned speed [m/s] when speed profile is absent.
// Calibrate per FCB service speed profile.
constexpr double kDefaultPlannedSpeed_mps = 5.0;
}  // namespace

// ===========================================================================
// Constructor
// ===========================================================================
MidMpcNode::MidMpcNode(const Config& cfg)
    : rclcpp::Node("m5_mid_mpc_node"),
      manifest_(mass_l3::m5::shared::CapabilityManifest::load_from_yaml(
          "/workspace/src/l3_tdl_kernel/m5_tactical_planner/config/fcb_vessel_capability.yaml")),
      vessel_model_(manifest_),
      nomoto_fallback_(nomoto_cfg_, manifest_),
      formulation_(cfg.nlp),
      solver_(formulation_, cfg.ipopt),
      wp_gen_(cfg.waypoint)
{
  formulation_.build_symbolic_graph();

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

  solve_timer_ = create_wall_timer(
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

  inp.own_ship.psi_rad = world_state_->own_ship.heading_deg * units::kRadPerDeg;
  inp.own_ship.u_mps   = world_state_->own_ship.u_water;

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

  if (inp.constraints.heading_min_rad > inp.constraints.heading_max_rad) {
    std::swap(inp.constraints.heading_min_rad, inp.constraints.heading_max_rad);
  }

  inp.constraints.speed_min_mps   = static_cast<double>(behavior_plan_->speed_min_kn) * units::kMsPerKn;
  inp.constraints.speed_max_mps   = static_cast<double>(behavior_plan_->speed_max_kn) * units::kMsPerKn;
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;

  // Dynamically adjust CPA safe distance and target weights based on COLREGs constraint
  double cpa_safe = kCpaSafeFallback_m;
  if (colregs_constraint_ != nullptr && !colregs_constraint_->active_rules.empty()) {
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
  const MidMpcSolution* warm = last_solution_.has_value() ? &last_solution_.value() : nullptr;
  const MidMpcSolution sol = solver_.solve(input, warm);
  last_solution_ = sol;

  const double lat = world_state_->own_ship.position.latitude;
  const double lon = world_state_->own_ship.position.longitude;
  const auto plan = wp_gen_.generate(sol, lat, lon);
  publish_outputs_(sol, plan);
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

  const std::string json =
      std::string("{\"status\":\"") + plan.status
      + "\",\"waypoints\":"  + std::to_string(plan.waypoints.size())
      + ",\"solve_ms\":"     + std::to_string(sol.solve_duration_ms)
      + ",\"ipopt_iter\":"   + std::to_string(sol.ipopt_iterations) + "}";

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
  sat.sat2.reasoning_chain    = plan.rationale;
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
