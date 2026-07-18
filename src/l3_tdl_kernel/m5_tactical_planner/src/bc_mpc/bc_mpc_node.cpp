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
      "/l3/m2/world_state", 10,
      [this](l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state_(std::move(msg));
      });

  sub_mid_plan_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", 10,
      [this](l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        on_mid_mpc_plan_(std::move(msg));
      });

  // v2.2 §13.1: BC-MPC Phase E2 wiring. Subscribe to Mid-MPC's consecutive
  // failures counter and cache atomically. assemble_input_ reads this instead
  // of the Phase E1 stub = 0. Activation logic (is_bc_active_ when failures
  // >= kThreshold) is γ3. Reliable QoS (Codex 🟡1): safety-relevant dispatch
  // signal — a dropped sample could leave BC-MPC stale at 0 and delay take-over.
  sub_mid_mpc_failures_ = create_subscription<std_msgs::msg::UInt64>(
      "/l3/m5/mid_mpc/consecutive_failures", rclcpp::QoS(10).reliable(),
      [this](const std_msgs::msg::UInt64::SharedPtr msg) {
        mid_mpc_consecutive_failures_atomic_.store(
            msg->data, std::memory_order_relaxed);
      });

  pub_override_ = create_publisher<l3_msgs::msg::ReactiveOverrideCmd>(
      "/l3/m5/reactive_override_cmd", 10);
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record", 10);
  // P6: health metrics — consumed by mid_mpc_node for Condition A + FinalDegrade
  pub_health_ = create_publisher<l3_msgs::msg::BcMpcHealth>(
      "/l3/m5/bc_mpc/health", rclcpp::QoS(10).reliable());

  validity_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
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

    // P6 Condition A: count consecutive overrides with no CPA improvement
    {
      const double improve = sol.worst_case_cpa_m - last_worst_case_cpa_m_;
      const double cpa_threshold = input.cpa_safe_m * formulation_.config().override_cpa_multiplier;
      if (sol.worst_case_cpa_m <= cpa_threshold && improve < kCpaImproveEpsilon_m) {
        ++consecutive_override_no_improve_;
      } else {
        consecutive_override_no_improve_ = 0U;
      }
      last_worst_case_cpa_m_ = sol.worst_case_cpa_m;
    }

    publish_override_(sol);
  } else if (is_bc_active_ && sol.status == BcMpcSolution::Status::Resolved) {
    is_bc_active_         = false;
    remaining_validity_s_ = 0.0;
    consecutive_override_no_improve_ = 0U;  // P6: reset on resolve
    spdlog::info("[M5][BcMPC] CPA resolved; handing back to Mid-MPC");
  }
  publish_health_(sol);  // P6: every tick
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
    // P6: do NOT silent-deactivate — keep is_bc_active_=true and wait for next
    // WorldState to re-evaluate. This avoids a dead period where BC-MPC is
    // inactive but Mid-MPC has not yet recovered, leaving the vessel without
    // override. The flag remains true; on_world_state_ will decide on the next solve.
    spdlog::warn("[M5][BcMPC] validity expired; will re-resolve on next WorldState");
    // Publish ASDR audit for the expiry event
    l3_msgs::msg::ASDRRecord record;
    record.stamp = this->get_clock()->now();
    record.source_module = "M5_BC_MPC";
    record.decision_type = "validity_expired_re_resolve";
    record.decision_json = "{\"remaining_validity_s\":0.0}";
    pub_asdr_->publish(record);
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
BcMpcInput BcMpcNode::assemble_input_()
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
  last_input_predicted_cpa_ = min_cpa;  // P6: cache for health metrics

  // v2.2 §13.1: BC-MPC Phase E2 — read the live consecutive_failures from the
  // atomic cache (subscribed from /l3/m5/mid_mpc/consecutive_failures). Replaces
  // the Phase E1 stub (=0). Relaxed load: a slightly stale value only delays
  // take-over by one cycle, which is acceptable (BC-MPC still gates on its own
  // CPA check before issuing an Override).
  inp.mid_mpc_consecutive_failures = static_cast<std::int32_t>(
      mid_mpc_consecutive_failures_atomic_.load(std::memory_order_relaxed));
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

// ===========================================================================
// publish_health_ — P6: emit BcMpcHealth on /l3/m5/bc_mpc/health
// ===========================================================================
void BcMpcNode::publish_health_(const BcMpcSolution& sol)
{
  l3_msgs::msg::BcMpcHealth health;
  health.stamp = this->get_clock()->now();
  health.override_active = is_bc_active_;
  health.worst_case_cpa_m = static_cast<float>(sol.worst_case_cpa_m);
  health.predicted_short_horizon_cpa_m = static_cast<float>(last_input_predicted_cpa_);
  health.override_no_improve_count = consecutive_override_no_improve_;
  health.consecutive_failures = static_cast<std::uint32_t>(solver_.consecutive_failures());
  health.confidence = static_cast<float>(sol.confidence);
  health.rationale = is_bc_active_
      ? "BCMPC_OVERRIDE_ACTIVE"
      : "BCMPC_RESOLVED";
  pub_health_->publish(health);
}

}  // namespace mass_l3::m5::bc_mpc
