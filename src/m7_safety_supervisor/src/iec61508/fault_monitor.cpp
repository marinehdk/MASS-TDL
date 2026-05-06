#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"

namespace mass_l3::m7::iec61508 {

// ---------------------------------------------------------------------------
// validate_odd_state
// Conformance score must lie in [0.0, 1.0]; outside this range is an M1 bug.
// ---------------------------------------------------------------------------

common::NoExceptionResult<bool>
FaultMonitor::validate_odd_state(l3_msgs::msg::ODDState const& msg) const noexcept {
  bool const valid = (msg.conformance_score >= 0.0f) && (msg.conformance_score <= 1.0f);
  return common::NoExceptionResult<bool>::ok(valid);
}

// ---------------------------------------------------------------------------
// validate_cpa_consistency
// CPA is a distance (metres): strictly negative values indicate an M2 bug.
// M7 does not recompute CPA; it only checks the sign invariant.
// ---------------------------------------------------------------------------

common::NoExceptionResult<bool>
FaultMonitor::validate_cpa_consistency(l3_msgs::msg::WorldState const& world) const noexcept {
  for (auto const& target : world.targets) {
    // CPA cannot be negative (it is a closest-approach distance)
    if (target.cpa_m < 0.0) {
      return common::NoExceptionResult<bool>::ok(false);
    }
  }
  return common::NoExceptionResult<bool>::ok(true);
}

// ---------------------------------------------------------------------------
// validate_colregs_consistency
// Active rules without any tracked targets are implausible:
// COLREGs rules require at least one interacting vessel.
// ---------------------------------------------------------------------------

common::NoExceptionResult<bool>
FaultMonitor::validate_colregs_consistency(
    l3_msgs::msg::WorldState const& world,
    l3_msgs::msg::COLREGsConstraint const& colregs) const noexcept
{
  bool const has_rules = !colregs.active_rules.empty();
  bool const has_targets = !world.targets.empty();
  // Rules active with no known targets is suspicious — flag it
  if (has_rules && !has_targets) {
    return common::NoExceptionResult<bool>::ok(false);
  }
  return common::NoExceptionResult<bool>::ok(true);
}

// ---------------------------------------------------------------------------
// run — aggregate all three checks; accumulate fault_count_
// ---------------------------------------------------------------------------

DiagnosticResult FaultMonitor::run(
    l3_msgs::msg::ODDState const& odd,
    l3_msgs::msg::WorldState const& world,
    l3_msgs::msg::COLREGsConstraint const& colregs) noexcept
{
  auto const odd_result = validate_odd_state(odd);
  auto const cpa_result = validate_cpa_consistency(world);
  auto const colregs_result = validate_colregs_consistency(world, colregs);

  bool const odd_ok     = odd_result.has_value() && odd_result.value();
  bool const cpa_ok     = cpa_result.has_value() && cpa_result.value();
  bool const colregs_ok = colregs_result.has_value() && colregs_result.value();

  if (!odd_ok)     { ++fault_count_; }
  if (!cpa_ok)     { ++fault_count_; }
  if (!colregs_ok) { ++fault_count_; }

  DiagnosticResult result{};
  result.conformance_score_valid = odd_ok;
  result.cpa_internal_consistent = cpa_ok;
  result.colregs_target_id_match = colregs_ok;
  result.fault_count             = fault_count_;

  return result;
}

}  // namespace mass_l3::m7::iec61508
