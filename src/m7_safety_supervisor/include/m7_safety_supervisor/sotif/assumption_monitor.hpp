#ifndef M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"
#include "m7_safety_supervisor/common/safety_constants.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"

namespace mass_l3::m7::sotif {

enum class AssumptionId : std::uint8_t {
  kAisRadarConsistency = 0,
  kMotionPredictability,
  kPerceptionCoverage,
  kColregsSolvability,
  kCommLink,
  kCheckerVetoRate,
  kCount
};

struct AssumptionConfig {
  double fusion_confidence_low{common::kDefaultFusionConfidenceLow};
  std::chrono::seconds ais_radar_duration_threshold{30};
  double motion_rmse_threshold_m{common::kDefaultMotionRmseThresholdM};
  std::chrono::seconds motion_window{30};
  double max_blind_zone_fraction{common::kDefaultMaxBlindZoneFraction};
  std::uint32_t colregs_consecutive_failure_count{common::kDefaultColregsFailureCount};
  double rtt_threshold_s{common::kDefaultRttThresholdS};
  double packet_loss_pct_threshold{common::kDefaultPacketLossPctThreshold};
  double checker_veto_rate_threshold{common::kCheckerVetoRateThreshold};
};

struct AssumptionStatus {
  std::array<bool, 6> violation_active{};
  std::array<float, 6> violation_metric{};
  std::uint32_t total_violation_count{0};
};

class AssumptionMonitor {
public:
  explicit AssumptionMonitor(AssumptionConfig const& cfg) noexcept;

  // Evaluate all 6 SOTIF assumptions. No dynamic allocation; all state is pre-allocated.
  [[nodiscard]] AssumptionStatus
  evaluate(l3_msgs::msg::ODDState const& odd,
           l3_msgs::msg::WorldState const& world,
           l3_msgs::msg::COLREGsConstraint const& colregs,
           double current_checker_veto_rate,
           double current_rtt_s,
           double current_packet_loss_pct,
           std::chrono::steady_clock::time_point now) noexcept;

  // Individual predicates — public for unit test entry points (§5.4).
  // Returns true when the corresponding assumption is VIOLATED.

  // Assumption 1: AIS/Radar fusion confidence sustained below threshold for >= duration.
  [[nodiscard]] bool check_ais_radar(l3_msgs::msg::WorldState const& world,
                                     std::chrono::steady_clock::time_point now) noexcept;

  // Assumption 2: World-model confidence proxy for motion predictability sustained low.
  [[nodiscard]] bool check_motion_predictability(l3_msgs::msg::WorldState const& world,
                                                 std::chrono::steady_clock::time_point now) noexcept;

  // Assumption 3: Fraction of unknown/unclassified targets exceeds max_blind_zone_fraction.
  // [TBD-HAZID-SOTIF-003]: Replace with dedicated blind-zone-fraction field when M2 exposes it.
  [[nodiscard]] bool check_perception_coverage(l3_msgs::msg::WorldState const& world) const noexcept;

  // Assumption 4: COLREGs solver confidence below threshold for N consecutive messages.
  [[nodiscard]] bool check_colregs_solvability(l3_msgs::msg::COLREGsConstraint const& colregs) noexcept;

  // Assumption 5: Shore-link RTT or packet loss exceeds thresholds.
  [[nodiscard]] bool check_comm_link(double rtt_s, double packet_loss_pct) const noexcept;

  // Assumption 6: M7 self-veto rate exceeds RFC-003 locked threshold (0.20).
  [[nodiscard]] bool check_checker_veto_rate(double current_rate) const noexcept;

  // Reset all internal state (used after MRC activation or ODD re-entry).
  void reset() noexcept;

private:
  AssumptionConfig cfg_;

  // Assumption 1 state — sustained low-confidence tracking
  std::chrono::steady_clock::time_point ais_radar_low_since_{};
  bool ais_radar_tracking_{false};

  // Assumption 2 state — sustained low world-confidence proxy
  std::chrono::steady_clock::time_point motion_low_since_{};
  bool motion_tracking_{false};

  // Assumption 2 history — 30-sample circular buffer of per-update confidence proxy
  std::array<float, 30> motion_rmse_history_{};
  std::uint32_t motion_history_idx_{0};
  bool motion_history_filled_{false};

  // Assumption 4 state — consecutive COLREGs failure counter
  std::uint32_t colregs_failure_count_{0};
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_ASSUMPTION_MONITOR_HPP_
