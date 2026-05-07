#ifndef MASS_L3_M5_COMMON_TIME_ALIGNMENT_HPP_
#define MASS_L3_M5_COMMON_TIME_ALIGNMENT_HPP_

// M5 Tactical Planner — Multi-source timestamp alignment helper
// PATH-D (MISRA C++:2023): [[nodiscard]], <cstdint>, no bare new/delete.
//
// M5 assembles inputs from 5 upstream sources with different update rates:
//   World_StateMsg      4 Hz  → max staleness 200 ms (spec §2.2)
//   Behavior_PlanMsg    2 Hz  → max staleness 500 ms
//   COLREGs_ConstraintMsg 2 Hz → max staleness 500 ms
//   ODD_StateMsg        1 Hz  → max staleness 1000 ms
//   PlannedRoute       ~0.5 Hz → max staleness 5000 ms
//
// TimeAlignment stores the latest timestamp per source and checks whether
// each is within its declared staleness budget. This is the gatekeeper before
// assembling MidMpcInput or triggering BC-MPC fallback.

#include <array>
#include <cstdint>
#include <string_view>

namespace mass_l3::m5 {

// ---------------------------------------------------------------------------
// SourceId — enumeration of M5 upstream message sources
// ---------------------------------------------------------------------------
enum class SourceId : std::uint8_t {
  WorldState       = 0u,
  BehaviorPlan     = 1u,
  ColregsConstraint = 2u,
  OddState         = 3u,
  PlannedRoute     = 4u,
  kCount           = 5u,  // sentinel for array sizing
};

// ---------------------------------------------------------------------------
// TimeAlignment — stateful tracker for per-source message freshness
// Thread safety: NOT thread-safe; intended for single-threaded MPC cycle.
// ---------------------------------------------------------------------------
class TimeAlignment {
 public:
  // Max staleness thresholds per spec §2.2 [nanoseconds]
  // [TBD-HAZID]: These thresholds must be validated during FCB trials.
  // Tighter margins may be required if sensor latency is higher than simulated.
  static constexpr std::int64_t kWorldStateMaxStale_ns  = 200'000'000LL;  // 200 ms
  static constexpr std::int64_t kBehaviorMaxStale_ns    = 500'000'000LL;  // 500 ms
  static constexpr std::int64_t kColregsMaxStale_ns     = 500'000'000LL;  // 500 ms
  static constexpr std::int64_t kOddStateMaxStale_ns    = 1'000'000'000LL; // 1 s
  static constexpr std::int64_t kPlannedRouteMaxStale_ns = 5'000'000'000LL; // 5 s

  // ---------------------------------------------------------------------------
  // update() — record the latest timestamp for a given source
  // @param src    Source identifier
  // @param ts_ns  Message timestamp [nanoseconds since epoch]
  // ---------------------------------------------------------------------------
  void update(SourceId src, std::int64_t ts_ns) noexcept;

  // ---------------------------------------------------------------------------
  // is_fresh() — check whether a source is within its staleness budget
  // @param src       Source identifier
  // @param now_ns    Current wall-clock time [nanoseconds since epoch]
  // @returns true if (now_ns - last_ts[src]) ≤ max_stale[src]
  // ---------------------------------------------------------------------------
  [[nodiscard]] bool is_fresh(SourceId src, std::int64_t now_ns) const noexcept;

  // ---------------------------------------------------------------------------
  // all_fresh() — check whether ALL sources are within budget simultaneously
  // Used as pre-condition for Mid-MPC solve cycle.
  // ---------------------------------------------------------------------------
  [[nodiscard]] bool all_fresh(std::int64_t now_ns) const noexcept;

  // ---------------------------------------------------------------------------
  // last_ts() — accessor for auditing / diagnostics
  // ---------------------------------------------------------------------------
  [[nodiscard]] std::int64_t last_ts(SourceId src) const noexcept;

  // ---------------------------------------------------------------------------
  // staleness_ns() — age of the most recent message for src
  // ---------------------------------------------------------------------------
  [[nodiscard]] std::int64_t staleness_ns(SourceId src,
                                          std::int64_t now_ns) const noexcept;

  // ---------------------------------------------------------------------------
  // max_stale_ns() — threshold for a given source (for diagnostics / logging)
  // ---------------------------------------------------------------------------
  [[nodiscard]] static std::int64_t max_stale_ns(SourceId src) noexcept;

  // ---------------------------------------------------------------------------
  // source_name() — human-readable label for logging
  // ---------------------------------------------------------------------------
  [[nodiscard]] static std::string_view source_name(SourceId src) noexcept;

 private:
  // Initialised to -1 (never received)
  std::array<std::int64_t, static_cast<std::size_t>(SourceId::kCount)>
      timestamps_{-1LL, -1LL, -1LL, -1LL, -1LL};
};

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_TIME_ALIGNMENT_HPP_
