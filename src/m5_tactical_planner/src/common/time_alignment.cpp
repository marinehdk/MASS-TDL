#include "m5_tactical_planner/common/time_alignment.hpp"

#include <cassert>
#include <cstddef>

namespace mass_l3::m5 {

// ---------------------------------------------------------------------------
// update() — record latest message timestamp for a source
// ---------------------------------------------------------------------------
void TimeAlignment::update(SourceId src, std::int64_t ts_ns) noexcept {
  const auto idx = static_cast<std::size_t>(src);
  assert(idx < timestamps_.size());
  timestamps_[idx] = ts_ns;
}

// ---------------------------------------------------------------------------
// is_fresh() — check if source is within its staleness budget
// Returns false if source has never been received (timestamp == -1).
// ---------------------------------------------------------------------------
bool TimeAlignment::is_fresh(SourceId src,
                              std::int64_t now_ns) const noexcept {
  const auto idx = static_cast<std::size_t>(src);
  assert(idx < timestamps_.size());
  const std::int64_t ts = timestamps_[idx];
  if (ts < 0LL) { return false; }  // never received
  const std::int64_t age = now_ns - ts;
  return age <= max_stale_ns(src);
}

// ---------------------------------------------------------------------------
// all_fresh() — check ALL sources simultaneously
// ---------------------------------------------------------------------------
bool TimeAlignment::all_fresh(std::int64_t now_ns) const noexcept {
  const std::size_t kCount = static_cast<std::size_t>(SourceId::kCount);
  for (std::size_t i = 0u; i < kCount; ++i) {
    if (!is_fresh(static_cast<SourceId>(i), now_ns)) { return false; }
  }
  return true;
}

// ---------------------------------------------------------------------------
// last_ts() — accessor
// ---------------------------------------------------------------------------
std::int64_t TimeAlignment::last_ts(SourceId src) const noexcept {
  const auto idx = static_cast<std::size_t>(src);
  assert(idx < timestamps_.size());
  return timestamps_[idx];
}

// ---------------------------------------------------------------------------
// staleness_ns() — age of latest message
// ---------------------------------------------------------------------------
std::int64_t TimeAlignment::staleness_ns(SourceId src,
                                          std::int64_t now_ns) const noexcept {
  const std::int64_t ts = last_ts(src);
  if (ts < 0LL) { return std::int64_t{9'000'000'000'000LL}; }  // sentinel
  return now_ns - ts;
}

// ---------------------------------------------------------------------------
// max_stale_ns() — threshold lookup
// ---------------------------------------------------------------------------
std::int64_t TimeAlignment::max_stale_ns(SourceId src) noexcept {
  switch (src) {
    case SourceId::WorldState:        return kWorldStateMaxStale_ns;
    case SourceId::BehaviorPlan:      return kBehaviorMaxStale_ns;
    case SourceId::ColregsConstraint: return kColregsMaxStale_ns;
    case SourceId::OddState:          return kOddStateMaxStale_ns;
    case SourceId::PlannedRoute:      return kPlannedRouteMaxStale_ns;
    default:                          return kWorldStateMaxStale_ns;
  }
}

// ---------------------------------------------------------------------------
// source_name() — human-readable label
// ---------------------------------------------------------------------------
std::string_view TimeAlignment::source_name(SourceId src) noexcept {
  switch (src) {
    case SourceId::WorldState:        return "WorldState";
    case SourceId::BehaviorPlan:      return "BehaviorPlan";
    case SourceId::ColregsConstraint: return "ColregsConstraint";
    case SourceId::OddState:          return "OddState";
    case SourceId::PlannedRoute:      return "PlannedRoute";
    default:                          return "Unknown";
  }
}

}  // namespace mass_l3::m5
