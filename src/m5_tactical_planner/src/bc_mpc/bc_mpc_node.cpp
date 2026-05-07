#include "m5_tactical_planner/bc_mpc/bc_mpc_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <spdlog/spdlog.h>

#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::bc_mpc {

// [TBD-HAZID] Validity tick interval [s]. Phase E1 fixed at 100 ms; Phase E2
// may expose as a parameter once HAZID WP-04 FM-3 timing data is available.
namespace {
constexpr double kTickInterval_s = 0.1;
// Sentinel for predicted_short_horizon_cpa_m when M2 has no CPA estimates.
// Distinct from the detector's 1e9 "no-targets" sentinel — this field enters
// compute_urgency_() and must be >> cpa_safe_m (≈1852 m) to yield urgency ≈ 0.
constexpr double kNoCpaEstimate_m = 1.0e6;
}

// ===========================================================================
// Constructor
// ===========================================================================
BcMpcNode::BcMpcNode(const Config& cfg)
    : rclcpp::Node("m5_bc_mpc_node"),
      formulation_(cfg.branch),
      solver_(formulation_),
      cfg_(cfg)
{
  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state_(std::move(msg));
      });

  sub_mid_plan_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      [this](l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        on_mid_mpc_plan_(std::move(msg));
      });

  pub_override_ = create_publisher<l3_msgs::msg::ReactiveOverrideCmd>(
      "/m5/reactive_override_cmd", 10);
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/m5/asdr_record_bc", 10);

  validity_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::duration<double>(kTickInterval_s)),
      [this]() { on_validity_tick_(); });
}

// ===========================================================================
// on_world_state_
// ===========================================================================
void BcMpcNode::on_world_state_(l3_msgs::msg::WorldState::SharedPtr msg)
{
  world_state_ = std::move(msg);
  if (world_state_ == nullptr) {
    return;
  }

  const BcMpcInput input = assemble_input_();
  const BcMpcSolution sol = solver_.solve(input);

  if (sol.status == BcMpcSolution::Status::Override) {
    is_bc_active_          = true;
    remaining_validity_s_  = sol.validity_s;
    publish_override_(sol);
  } else if (is_bc_active_ && sol.status == BcMpcSolution::Status::Resolved) {
    is_bc_active_         = false;
    remaining_validity_s_ = 0.0;
    spdlog::info("[M5][BcMPC] CPA resolved; handing back to Mid-MPC");
  }
}

// ===========================================================================
// on_mid_mpc_plan_ — Phase E1: cache only; Phase E2 reads for Condition A.
// ===========================================================================
void BcMpcNode::on_mid_mpc_plan_(l3_msgs::msg::AvoidancePlan::SharedPtr msg)
{
  last_mid_mpc_plan_ = std::move(msg);
}

// ===========================================================================
// on_validity_tick_
// ===========================================================================
void BcMpcNode::on_validity_tick_()
{
  if (!is_bc_active_) {
    return;
  }

  remaining_validity_s_ -= kTickInterval_s;
  if (remaining_validity_s_ <= 0.0) {
    is_bc_active_ = false;
    spdlog::warn("[M5][BcMPC] validity_s expired; BC-MPC deactivated");
    return;
  }

  // Republish active command with updated validity so L4 has a fresh expiry.
  active_cmd_.validity_s   = static_cast<float>(remaining_validity_s_);
  active_cmd_.trigger_time = this->get_clock()->now();
  pub_override_->publish(active_cmd_);
}

// ===========================================================================
// assemble_input_ — precondition: world_state_ != nullptr
// ===========================================================================
BcMpcInput BcMpcNode::assemble_input_() const
{
  BcMpcInput inp;

  inp.own_ship.psi_rad = world_state_->own_ship.heading_deg * units::kRadPerDeg;
  inp.own_ship.u_mps   = world_state_->own_ship.u_water;
  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;

  const double own_lat = world_state_->own_ship.position.latitude;
  const double own_lon = world_state_->own_ship.position.longitude;

  for (const auto& tgt : world_state_->targets) {
    TargetState ts;
    ts.id      = static_cast<int32_t>(tgt.target_id & 0x7FFFFFFFu);
    ts.x_m     = (tgt.position.latitude  - own_lat) * units::kRadPerDeg
                 * units::kEarthRadiusMean_m;
    ts.y_m     = (tgt.position.longitude - own_lon) * units::kRadPerDeg
                 * units::kEarthRadiusMean_m
                 * std::cos(own_lat * units::kRadPerDeg);
    ts.sog_mps = tgt.sog_kn * units::kMsPerKn;
    ts.cog_rad = tgt.cog_deg * units::kRadPerDeg;
    ts.cpa_m   = tgt.cpa_m;
    ts.tcpa_s  = tgt.tcpa_s;
    inp.targets.push_back(ts);
  }

  inp.cpa_safe_m = cfg_.cpa_safe_m;

  // predicted_short_horizon_cpa_m: min CPA across targets; far-safe sentinel if none.
  double min_cpa = kNoCpaEstimate_m;
  for (const auto& ts : inp.targets) {
    if (ts.cpa_m < min_cpa) {
      min_cpa = ts.cpa_m;
    }
  }
  inp.predicted_short_horizon_cpa_m = min_cpa;

  inp.mid_mpc_consecutive_failures = 0;  // Phase E1; Phase E2 reads shared state
  inp.stamp_ns = this->get_clock()->now().nanoseconds();

  return inp;
}

// ===========================================================================
// publish_override_
// ===========================================================================
void BcMpcNode::publish_override_(const BcMpcSolution& sol)
{
  const auto now = this->get_clock()->now();
  const auto cmd = override_gen_.generate(sol, now);
  active_cmd_ = cmd;
  pub_override_->publish(cmd);

  l3_msgs::msg::ASDRRecord record;
  record.stamp         = now;
  record.source_module = "M5_BC_MPC";
  record.decision_type = "reactive_override";
  record.decision_json =
      std::string("{\"heading_deg\":") + std::to_string(cmd.heading_cmd_deg)
      + ",\"validity_s\":"  + std::to_string(cmd.validity_s)
      + ",\"worst_cpa_m\":" + std::to_string(sol.worst_case_cpa_m) + "}";
  const auto digest = mass_l3::m5::common::sha256(record.decision_json);
  record.signature.assign(digest.begin(), digest.end());
  pub_asdr_->publish(record);
}

}  // namespace mass_l3::m5::bc_mpc
