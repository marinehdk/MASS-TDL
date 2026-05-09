#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/common/error.hpp"

#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

// Type note: ODDState::conformance_score is float32 (ROS2 IDL → float);
// TrackedTarget::cpa_m is float64 (ROS2 IDL → double). Both correct per ros2-idl-implementation-guide.md.

namespace mass_l3::m7::iec61508 {

// ---------------------------------------------------------------------------
// validate_odd_state
// Conformance score must lie in [0.0, 1.0]; outside this range is an M1 bug.
// ---------------------------------------------------------------------------

common::NoExceptionResult<bool>
FaultMonitor::validate_odd_state(l3_msgs::msg::ODDState const& msg) noexcept {
  bool const kValid = (msg.conformance_score >= 0.0F) && (msg.conformance_score <= 1.0F);
  return common::NoExceptionResult<bool>::ok(kValid);
}

// ---------------------------------------------------------------------------
// validate_cpa_consistency
// CPA is a distance (metres): strictly negative values indicate an M2 bug.
// M7 does not recompute CPA; it only checks the sign invariant.
// ---------------------------------------------------------------------------

common::NoExceptionResult<bool>
FaultMonitor::validate_cpa_consistency(l3_msgs::msg::WorldState const& world) noexcept {
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
    l3_msgs::msg::COLREGsConstraint const& colregs) noexcept
{
  bool const kHasRules = !colregs.active_rules.empty();
  bool const kHasTargets = !world.targets.empty();
  // Rules active with no known targets is suspicious — flag it
  if (kHasRules && !kHasTargets) {
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
  auto const kOddResult     = validate_odd_state(odd);
  auto const kCpaResult     = validate_cpa_consistency(world);
  auto const kColregsResult = validate_colregs_consistency(world, colregs);

  bool const kOddOk     = kOddResult.has_value() && kOddResult.value();
  bool const kCpaOk     = kCpaResult.has_value() && kCpaResult.value();
  bool const kColregsOk = kColregsResult.has_value() && kColregsResult.value();

  if (!kOddOk)     { ++fault_count_; }
  if (!kCpaOk)     { ++fault_count_; }
  if (!kColregsOk) { ++fault_count_; }

  DiagnosticResult result{};
  result.conformance_score_valid = kOddOk;
  result.cpa_internal_consistent = kCpaOk;
  result.colregs_target_id_match = kColregsOk;
  result.fault_count             = fault_count_;

  return result;
}

}  // namespace mass_l3::m7::iec61508
