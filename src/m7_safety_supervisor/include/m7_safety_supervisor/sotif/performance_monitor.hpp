#ifndef M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::sotif {

struct PerformanceConfig {
  std::chrono::seconds cpa_window{30};
  double cpa_trend_slope_threshold_nm_s{-0.01};      // [TBD-HAZID] degrading when slope < threshold
  double multiple_targets_cpa_threshold_nm{1.0};      // [TBD-HAZID] NM
  std::uint32_t multiple_targets_count_threshold{2};  // [TBD-HAZID]
};

struct PerformanceStatus {
  bool cpa_trend_degrading;
  // Slope in NM/sample (≈ NM/s when evaluate() is called at 1 Hz per design assumption).
  // [TBD-HAZID]: Validate actual call rate against this assumption during FCB calibration.
  double cpa_trend_slope_nm_s;
  double max_cpa_in_window_nm;
  bool multiple_targets_nearby;
  std::uint32_t critical_target_count;
};

// INVARIANT: Designed for 1 Hz call rate (30 samples ≈ 30 s window).
// cpa_trend_slope_nm_s is NM/sample; at 1 Hz this equals NM/s.
// [TBD-HAZID]: If call rate deviates from 1 Hz, threshold must be rescaled.
// INVARIANT: Single-threaded (main_loop callback group only).
class PerformanceMonitor {
public:
  explicit PerformanceMonitor(PerformanceConfig const& cfg) noexcept;

  [[nodiscard]] PerformanceStatus
  evaluate(l3_msgs::msg::WorldState const& world,
           std::chrono::steady_clock::time_point now) noexcept;

  void reset() noexcept;

private:
  PerformanceConfig cfg_;
  std::array<float, 30> cpa_history_{};   // 30-sample ring buffer, CPA in NM
  std::uint32_t history_idx_{0};
  std::uint32_t history_count_{0};        // samples filled so far (saturates at 30)
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_PERFORMANCE_MONITOR_HPP_
