#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
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
      formulation_(cfg.nlp),
      solver_(formulation_, cfg.ipopt),
      wp_gen_(cfg.waypoint)
{
  formulation_.build_symbolic_graph();

  sub_own_ship_ = create_subscription<l3_msgs::msg::OwnShipState>(
      "/m2/own_ship_state", 10,
      [this](l3_msgs::msg::OwnShipState::SharedPtr msg) {
        own_ship_state_ = std::move(msg);
      });

  sub_targets_ = create_subscription<l3_external_msgs::msg::TrackedTargetArray>(
      "/m2/tracked_targets", 10,
      [this](l3_external_msgs::msg::TrackedTargetArray::SharedPtr msg) {
        tracked_targets_ = std::move(msg);
      });

  sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/m4/behavior_plan", 10,
      [this](l3_msgs::msg::BehaviorPlan::SharedPtr msg) {
        behavior_plan_ = std::move(msg);
      });

  sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", 10,
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

  solve_timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { on_solve_cycle_(); });
}

// ===========================================================================
// has_required_inputs_
// ===========================================================================
bool MidMpcNode::has_required_inputs_() const noexcept
{
  return own_ship_state_    != nullptr
      && tracked_targets_   != nullptr
      && behavior_plan_     != nullptr
      && colregs_constraint_ != nullptr;
}

// ===========================================================================
// assemble_input_ — precondition: has_required_inputs_() == true
// ===========================================================================
MidMpcInput MidMpcNode::assemble_input_() const
{
  MidMpcInput inp;

  inp.own_ship.psi_rad = own_ship_state_->heading_deg * units::kRadPerDeg;
  inp.own_ship.u_mps   = own_ship_state_->u_water;

  const double own_lat = own_ship_state_->position.latitude;
  const double own_lon = own_ship_state_->position.longitude;

  for (const auto& tgt : tracked_targets_->targets) {
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

  inp.constraints.heading_min_rad = static_cast<double>(behavior_plan_->heading_min_deg) * units::kRadPerDeg;
  inp.constraints.heading_max_rad = static_cast<double>(behavior_plan_->heading_max_deg) * units::kRadPerDeg;
  inp.constraints.speed_min_mps   = static_cast<double>(behavior_plan_->speed_min_kn) * units::kMsPerKn;
  inp.constraints.speed_max_mps   = static_cast<double>(behavior_plan_->speed_max_kn) * units::kMsPerKn;
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
  inp.constraints.cpa_safe_m       = kCpaSafeFallback_m;  // [TBD-HAZID] from ODD state

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
  const MidMpcSolution* warm = last_solution_.has_value() ? &last_solution_.value() : nullptr;
  const MidMpcSolution sol = solver_.solve(input, warm);
  last_solution_ = sol;

  const double lat = own_ship_state_->position.latitude;
  const double lon = own_ship_state_->position.longitude;
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
  pub_asdr_record_->publish(record);

  l3_msgs::msg::SATData sat;
  sat.stamp                   = now;
  sat.source_module           = "M5_Tactical_Planner";
  sat.sat2.trigger_reason     = "mpc_cycle";
  sat.sat2.reasoning_chain    = plan.rationale;
  sat.sat2.system_confidence  = plan.confidence;
  pub_sat_data_->publish(sat);
}

}  // namespace mass_l3::m5::mid_mpc
