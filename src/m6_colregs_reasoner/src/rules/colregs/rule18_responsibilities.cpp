#include "m6_colregs_reasoner/rules/colregs/rule18_responsibilities.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

namespace {

/// Priority ordering per COLREGs Rule 18 (higher = more privileged/maneuver restricted).
/// NUC > RAM > CBD > fishing > sail > power-driven.
/// Lower priority vessel must give way to higher priority vessel.
int32_t effective_priority(int32_t ship_type_priority) {
  // Input priority: lower number = more restricted
  // Map to consistent scale where higher = more restricted
  // Expected mapping based on typical AIS ship type encoding:
  //   NUC (not under command): 0
  //   RAM (restricted ability to maneuver): 1
  //   CBD (constrained by draft): 2
  //   fishing: 3
  //   sail: 4
  //   power-driven: 5
  return ship_type_priority;
}

}  // anonymous namespace

RuleEvaluation Rule18_Responsibilities::evaluate(const TargetGeometricState& geo,
                                                   OddDomain odd,
                                                   const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 18;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Rule 18 Responsibilities between vessels.
  // Higher priority (more maneuver-restricted) stands on.
  // Lower priority gives way.
  // Compare ownship vs target type priority.
  // The TargetGeometricState only has target_ship_type_priority,
  // not own priority. For single-target evaluation, we use the target priority
  // to determine the relationship.

  // Own vessel is assumed power-driven (priority 5) when not otherwise specified.
  constexpr int32_t kOwnDefaultPriority = 5;
  const int32_t own_priority = kOwnDefaultPriority;
  const int32_t target_effective = effective_priority(geo.target_ship_type_priority);

  if (own_priority == target_effective) {
    // Same priority class — Rule 18 doesn't apply; other rules (13-15) determine role
    result.is_active = false;
    result.confidence = 0.5f;
    result.rationale = "Rule 18: Same vessel type priority (" +
                       std::to_string(target_effective) +
                       "). Rule 18 does not assign role.";
    return result;
  }

  result.is_active = true;
  result.encounter_type = EncounterType::AMBIGUOUS;

  if (own_priority < target_effective) {
    // Own vessel is more restricted (higher priority) → stand-on
    result.role = Role::STAND_ON;
    result.preferred_direction = "HOLD";
    result.confidence = 0.8f;
    result.rationale = "Rule 18: Own vessel has higher priority (type=" +
                       std::to_string(own_priority) +
                       ") than target (type=" +
                       std::to_string(target_effective) +
                       "). Stand-on obligation.";
  } else {
    // Target is more restricted → give-way
    result.role = Role::GIVE_WAY;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = params.min_alteration_deg;
    result.confidence = 0.8f;
    result.rationale = "Rule 18: Target has higher priority (type=" +
                       std::to_string(target_effective) +
                       ") than own vessel (type=" +
                       std::to_string(own_priority) +
                       "). Give-way obligation.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
