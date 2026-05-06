#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"
#include "m7_safety_supervisor/common/time_utils.hpp"

#include <cmath>
#include <cstring>

namespace mass_l3::m7::sotif {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AssumptionMonitor::AssumptionMonitor(AssumptionConfig const& cfg) noexcept
  : cfg_{cfg} {
  motion_rmse_history_.fill(0.0f);
}

// ---------------------------------------------------------------------------
// Assumption 1: AIS/Radar consistency — sustained low fusion confidence
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_ais_radar(
    l3_msgs::msg::WorldState const& world,
    std::chrono::steady_clock::time_point now) noexcept {
  bool const low = static_cast<double>(world.confidence) < cfg_.fusion_confidence_low;
  if (low) {
    if (!ais_radar_tracking_) {
      ais_radar_low_since_ = now;
      ais_radar_tracking_ = true;
    }
    auto const elapsed = common::elapsed_seconds(ais_radar_low_since_, now);
    return elapsed >= cfg_.ais_radar_duration_threshold;
  }
  // Confidence recovered: reset tracking state
  ais_radar_tracking_ = false;
  ais_radar_low_since_ = {};
  return false;
}

// ---------------------------------------------------------------------------
// Assumption 2: Motion predictability — world confidence proxy sustained low
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_motion_predictability(
    l3_msgs::msg::WorldState const& world,
    std::chrono::steady_clock::time_point now) noexcept {
  // Proxy metric: world confidence below 0.4 is used as surrogate for poor
  // motion prediction quality until M2 exposes a dedicated RMSE field.
  // [TBD-HAZID-SOTIF-002]: Replace 0.4 proxy with M2 prediction RMSE field.
  constexpr float kMotionProxyThreshold{0.4f};
  bool const poor = world.confidence < kMotionProxyThreshold;

  // Update circular history buffer
  motion_rmse_history_[motion_history_idx_] = world.confidence;
  motion_history_idx_ = (motion_history_idx_ + 1u) % 30u;
  if (motion_history_idx_ == 0u) {
    motion_history_filled_ = true;
  }

  if (poor) {
    if (!motion_tracking_) {
      motion_low_since_ = now;
      motion_tracking_ = true;
    }
    auto const elapsed = common::elapsed_seconds(motion_low_since_, now);
    return elapsed >= cfg_.motion_window;
  }
  // Confidence recovered: reset tracking
  motion_tracking_ = false;
  motion_low_since_ = {};
  return false;
}

// ---------------------------------------------------------------------------
// Assumption 3: Perception coverage — fraction of unknown targets
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_perception_coverage(
    l3_msgs::msg::WorldState const& world) const noexcept {
  // [TBD-HAZID-SOTIF-003]: Replace with dedicated blind-zone-fraction field
  // when M2 WorldState exposes it (planned for v1.1.3 post-HAZID calibration).
  std::size_t const total = world.targets.size();
  if (total == 0u) {
    return false;  // No targets present; no blind zone measurable
  }
  std::size_t unknown_count = 0u;
  for (auto const& t : world.targets) {
    bool const unknown_class =
        (t.classification == "unknown") ||
        (static_cast<double>(t.classification_confidence) < 0.3);
    if (unknown_class) {
      ++unknown_count;
    }
  }
  double const fraction =
      static_cast<double>(unknown_count) / static_cast<double>(total);
  return fraction > cfg_.max_blind_zone_fraction;
}

// ---------------------------------------------------------------------------
// Assumption 4: COLREGs solvability — consecutive failure counter
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_colregs_solvability(
    l3_msgs::msg::COLREGsConstraint const& colregs) noexcept {
  bool const failed =
      static_cast<double>(colregs.confidence) < common::kColregsConfidenceFailThreshold;
  if (failed) {
    ++colregs_failure_count_;
    return colregs_failure_count_ >= cfg_.colregs_consecutive_failure_count;
  }
  colregs_failure_count_ = 0u;
  return false;
}

// ---------------------------------------------------------------------------
// Assumption 5: Communication link — RTT and packet loss
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_comm_link(
    double rtt_s, double packet_loss_pct) const noexcept {
  return (rtt_s > cfg_.rtt_threshold_s) ||
         (packet_loss_pct > cfg_.packet_loss_pct_threshold);
}

// ---------------------------------------------------------------------------
// Assumption 6: Checker self-veto rate — RFC-003 locked threshold
// ---------------------------------------------------------------------------

bool AssumptionMonitor::check_checker_veto_rate(double current_rate) const noexcept {
  return current_rate >= cfg_.checker_veto_rate_threshold;
}

// ---------------------------------------------------------------------------
// evaluate() — aggregate all 6 assumptions
// ---------------------------------------------------------------------------

namespace {
  // Index aliases — kept here to avoid magic numbers in evaluate()
  constexpr std::size_t kIdxAis    = static_cast<std::size_t>(AssumptionId::kAisRadarConsistency);
  constexpr std::size_t kIdxMotion = static_cast<std::size_t>(AssumptionId::kMotionPredictability);
  constexpr std::size_t kIdxCovg   = static_cast<std::size_t>(AssumptionId::kPerceptionCoverage);
  constexpr std::size_t kIdxColreg = static_cast<std::size_t>(AssumptionId::kColregsSolvability);
  constexpr std::size_t kIdxComm   = static_cast<std::size_t>(AssumptionId::kCommLink);
  constexpr std::size_t kIdxVeto   = static_cast<std::size_t>(AssumptionId::kCheckerVetoRate);
}  // namespace

AssumptionStatus AssumptionMonitor::evaluate(
    l3_msgs::msg::ODDState const& /*odd*/,
    l3_msgs::msg::WorldState const& world,
    l3_msgs::msg::COLREGsConstraint const& colregs,
    double current_checker_veto_rate,
    double current_rtt_s,
    double current_packet_loss_pct,
    std::chrono::steady_clock::time_point now) noexcept {
  AssumptionStatus status{};
  // Populate violations and metrics for each assumption
  status.violation_active[kIdxAis]    = check_ais_radar(world, now);
  status.violation_metric[kIdxAis]    = world.confidence;
  status.violation_active[kIdxMotion] = check_motion_predictability(world, now);
  status.violation_metric[kIdxMotion] = world.confidence;
  status.violation_active[kIdxCovg]   = check_perception_coverage(world);
  status.violation_metric[kIdxCovg]   = world.confidence;
  status.violation_active[kIdxColreg] = check_colregs_solvability(colregs);
  status.violation_metric[kIdxColreg] = colregs.confidence;
  status.violation_active[kIdxComm]   = check_comm_link(current_rtt_s, current_packet_loss_pct);
  status.violation_metric[kIdxComm]   = static_cast<float>(current_rtt_s);
  status.violation_active[kIdxVeto]   = check_checker_veto_rate(current_checker_veto_rate);
  status.violation_metric[kIdxVeto]   = static_cast<float>(current_checker_veto_rate);
  // Aggregate violation count
  std::uint32_t count = 0u;
  for (bool const v : status.violation_active) { if (v) { ++count; } }
  status.total_violation_count = count;
  return status;
}

// ---------------------------------------------------------------------------
// reset() — zero all state (MRC activation or ODD re-entry)
// ---------------------------------------------------------------------------

void AssumptionMonitor::reset() noexcept {
  ais_radar_low_since_ = {};
  ais_radar_tracking_ = false;
  motion_low_since_ = {};
  motion_tracking_ = false;
  motion_rmse_history_.fill(0.0f);
  motion_history_idx_ = 0u;
  motion_history_filled_ = false;
  colregs_failure_count_ = 0u;
}

}  // namespace mass_l3::m7::sotif
