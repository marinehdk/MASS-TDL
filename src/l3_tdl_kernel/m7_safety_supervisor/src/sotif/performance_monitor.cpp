#include "m7_safety_supervisor/sotif/performance_monitor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>

#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::sotif {

namespace {

// ---------------------------------------------------------------------------
// compute_min_cpa — extracted helper
// ---------------------------------------------------------------------------

float compute_min_cpa(l3_msgs::msg::WorldState const& world) noexcept {
  constexpr float kSentinelM = 9999.0F * 1852.0F;
  float min_cpa_m = kSentinelM;
  bool has_targets = false;
  for (auto const& target : world.targets) {
    auto const kCpaM = static_cast<float>(target.cpa_m);
    if (!has_targets || kCpaM < min_cpa_m) {
      min_cpa_m = kCpaM;
      has_targets = true;
    }
  }
  constexpr float kSentinelNm = 9999.0F;
  return has_targets ? (min_cpa_m / 1852.0F) : kSentinelNm;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

PerformanceMonitor::PerformanceMonitor(PerformanceConfig const& cfg) noexcept
  : cfg_{cfg}
{
  cpa_history_.fill(0.0F);
}

// ---------------------------------------------------------------------------
// compute_slope — extracted helper
// ---------------------------------------------------------------------------

double PerformanceMonitor::compute_slope() const noexcept {
  if (history_count_ < 2U) {
    return 0.0;
  }
  // Most recent: written at (history_idx_ - 1) % 30
  std::uint32_t const kNewestIdx = (history_idx_ - 1U) % 30U;
  // Oldest: if ring not full, index 0; if full, current write cursor (history_idx_ % 30)
  std::uint32_t const kOldestIdx = (history_count_ < 30U) ? 0U : (history_idx_ % 30U);
  float const kNewestVal = cpa_history_[kNewestIdx];
  float const kOldestVal = cpa_history_[kOldestIdx];
  return static_cast<double>(kNewestVal - kOldestVal) /
         static_cast<double>(history_count_ - 1U);
}

// ---------------------------------------------------------------------------
// compute_max_cpa_in_window — extracted helper
// ---------------------------------------------------------------------------

double PerformanceMonitor::compute_max_cpa_in_window() const noexcept {
  double max_cpa_nm = 0.0;
  for (std::uint32_t i = 0U; i < history_count_; ++i) {
    auto const kVal = static_cast<double>(cpa_history_[i]);
    max_cpa_nm = std::max(max_cpa_nm, kVal);
  }
  return max_cpa_nm;
}

// ---------------------------------------------------------------------------
// evaluate()
// ---------------------------------------------------------------------------

PerformanceStatus PerformanceMonitor::evaluate(
    l3_msgs::msg::WorldState const& world,
    std::chrono::steady_clock::time_point /*now*/) noexcept
{
  // Step 1-2: find min CPA over all targets (meters), convert to NM
  float const kMinCpaNm = compute_min_cpa(world);

  // Step 3: store in ring buffer
  cpa_history_[history_idx_ % 30U] = kMinCpaNm;
  ++history_idx_;
  if (history_count_ < 30U) {
    ++history_count_;
  }

  // Step 4: compute slope
  double const kSlope = compute_slope();

  // Step 5: degrading flag
  bool const kCpaTrendDegrading = (kSlope < cfg_.cpa_trend_slope_threshold_nm_s);

  // Step 6: max CPA in window
  double const kMaxCpaNm = compute_max_cpa_in_window();

  // Step 7: multiple targets nearby
  double const kCpaThresholdM = cfg_.multiple_targets_cpa_threshold_nm * 1852.0;
  std::uint32_t close_count = 0U;
  for (auto const& target : world.targets) {
    if (target.cpa_m < kCpaThresholdM) {
      ++close_count;
    }
  }
  bool const kMultipleTargetsNearby =
      (close_count >= cfg_.multiple_targets_count_threshold);

  PerformanceStatus status{};
  status.cpa_trend_degrading = kCpaTrendDegrading;
  status.cpa_trend_slope_nm_s = kSlope;
  status.max_cpa_in_window_nm = kMaxCpaNm;
  status.multiple_targets_nearby = kMultipleTargetsNearby;
  status.critical_target_count = close_count;
  return status;
}

// ---------------------------------------------------------------------------
// reset()
// ---------------------------------------------------------------------------

void PerformanceMonitor::reset() noexcept {
  cpa_history_.fill(0.0F);
  history_idx_ = 0U;
  history_count_ = 0U;
}

}  // namespace mass_l3::m7::sotif
