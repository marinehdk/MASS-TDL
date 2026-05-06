#include "m7_safety_supervisor/sotif/performance_monitor.hpp"

#include <algorithm>
#include <cstddef>

namespace mass_l3::m7::sotif {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

PerformanceMonitor::PerformanceMonitor(PerformanceConfig const& cfg) noexcept
  : cfg_{cfg}
{
  cpa_history_.fill(0.0f);
}

// ---------------------------------------------------------------------------
// evaluate()
// ---------------------------------------------------------------------------

PerformanceStatus PerformanceMonitor::evaluate(
    l3_msgs::msg::WorldState const& world,
    std::chrono::steady_clock::time_point /*now*/) noexcept
{
  // Step 1: find min CPA over all targets (meters), sentinel if no targets
  constexpr float kSentinelNm = 9999.0f;
  float min_cpa_m = 9999.0f * 1852.0f;
  bool has_targets = false;
  for (auto const& target : world.targets) {
    float const cpa_m = static_cast<float>(target.cpa_m);
    if (!has_targets || cpa_m < min_cpa_m) {
      min_cpa_m = cpa_m;
      has_targets = true;
    }
  }

  // Step 2: convert to NM
  float const min_cpa_nm = has_targets ? (min_cpa_m / 1852.0f) : kSentinelNm;

  // Step 3: store in ring buffer
  cpa_history_[history_idx_ % 30u] = min_cpa_nm;
  ++history_idx_;
  if (history_count_ < 30u) {
    ++history_count_;
  }

  // Step 4: compute slope
  double slope = 0.0;
  if (history_count_ >= 2u) {
    // Most recent: written at (history_idx_ - 1) % 30
    std::uint32_t const newest_idx = (history_idx_ - 1u) % 30u;
    // Oldest: if ring not full, index 0; if full, current write cursor (history_idx_ % 30)
    std::uint32_t const oldest_idx = (history_count_ < 30u) ? 0u : (history_idx_ % 30u);
    float const newest_val = cpa_history_[newest_idx];
    float const oldest_val = cpa_history_[oldest_idx];
    slope = static_cast<double>(newest_val - oldest_val) /
            static_cast<double>(history_count_ - 1u);
  }

  // Step 5: degrading flag
  bool const cpa_trend_degrading = (slope < cfg_.cpa_trend_slope_threshold_nm_s);

  // Step 6: max CPA in window
  double max_cpa_nm = 0.0;
  for (std::uint32_t i = 0u; i < history_count_; ++i) {
    double const val = static_cast<double>(cpa_history_[i]);
    if (val > max_cpa_nm) {
      max_cpa_nm = val;
    }
  }

  // Step 7: multiple targets nearby
  double const cpa_threshold_m = cfg_.multiple_targets_cpa_threshold_nm * 1852.0;
  std::uint32_t close_count = 0u;
  for (auto const& target : world.targets) {
    if (target.cpa_m < cpa_threshold_m) {
      ++close_count;
    }
  }
  bool const multiple_targets_nearby =
      (close_count >= cfg_.multiple_targets_count_threshold);

  PerformanceStatus status{};
  status.cpa_trend_degrading = cpa_trend_degrading;
  status.cpa_trend_slope_nm_s = slope;
  status.max_cpa_in_window_nm = max_cpa_nm;
  status.multiple_targets_nearby = multiple_targets_nearby;
  status.critical_target_count = close_count;
  return status;
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void PerformanceMonitor::reset() noexcept {
  cpa_history_.fill(0.0f);
  history_idx_ = 0u;
  history_count_ = 0u;
}

}  // namespace mass_l3::m7::sotif
