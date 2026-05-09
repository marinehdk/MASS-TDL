#ifndef M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_

#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"

namespace mass_l3::m7::iec61508 {

struct DiagnosticResult {
  bool conformance_score_valid;  // ODD.conformance_score ∈ [0, 1]
  bool cpa_internal_consistent;  // M2 CPA passes sanity check (no negative values)
  bool colregs_target_id_match;  // active_rules non-empty only if targets present
  std::uint32_t fault_count;     // cumulative fault count at time of this result
};

// FaultMonitor: SIL 2 diagnostic coverage
// Pure functions (no mutable state except cumulative fault_count_)
// Independence: does NOT reuse M6's COLREGs logic; performs simple sanity checks only
class FaultMonitor {
public:
  FaultMonitor() noexcept = default;

  // Validate ODD conformance_score is in [0, 1]
  [[nodiscard]] static common::NoExceptionResult<bool>
  validate_odd_state(l3_msgs::msg::ODDState const& msg) noexcept;

  // CPA sanity check: if any target has cpa_m < 0 (strictly negative), that is
  // a clear M2 computation bug — CPA is a distance and cannot be negative.
  // M7 does NOT recompute CPA exactly — only catches gross sign errors.
  [[nodiscard]] static common::NoExceptionResult<bool>
  validate_cpa_consistency(l3_msgs::msg::WorldState const& world) noexcept;

  // COLREGs active_rules should be non-empty only if targets are present.
  // Simple check: active_rules non-empty and world.targets empty → suspicious.
  // NOTE: this is a simplified plausibility check; M7 does not re-run COLREGs logic.
  [[nodiscard]] static common::NoExceptionResult<bool>
  validate_colregs_consistency(l3_msgs::msg::WorldState const& world,
                               l3_msgs::msg::COLREGsConstraint const& colregs) noexcept;

  // Main diagnostic entry point — runs all three checks
  [[nodiscard]] DiagnosticResult run(
      l3_msgs::msg::ODDState const& odd,
      l3_msgs::msg::WorldState const& world,
      l3_msgs::msg::COLREGsConstraint const& colregs) noexcept;

  [[nodiscard]] std::uint32_t fault_count() const noexcept { return fault_count_; }
  void reset_count() noexcept { fault_count_ = 0U; }

private:
  std::uint32_t fault_count_{0U};
};

}  // namespace mass_l3::m7::iec61508

#endif  // M7_SAFETY_SUPERVISOR_IEC61508_FAULT_MONITOR_HPP_
