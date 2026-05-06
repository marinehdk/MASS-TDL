#include "m6_colregs_reasoner/rules/colregs/rule6_safe_speed.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

namespace {

/// Compute speed factor based on ODD domain.
/// ODD-A (open water): highest safe speed
/// ODD-D (restricted visibility): lowest safe speed
double odd_speed_factor(OddDomain odd) {
  switch (odd) {
    case OddDomain::ODD_A:
      return 1.0;   // open water, unrestricted
    case OddDomain::ODD_B:
      return 0.9;   // moderate traffic
    case OddDomain::ODD_C:
      return 0.75;  // congested
    case OddDomain::ODD_D:
      return 0.5;   // restricted visibility
    default:
      return 0.8;   // unknown — conservative
  }
}

}  // anonymous namespace

RuleEvaluation Rule6_SafeSpeed::evaluate(const TargetGeometricState& geo,
                                         OddDomain odd,
                                         const RuleParameters& params) const {
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 6;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  const double factor = odd_speed_factor(odd);
  // Base reference: own speed — check if it's appropriate for conditions
  const double max_safe_kn = factor * 20.0;  // baseline max 20 kn, scaled by ODD

  if (geo.ownship_speed_kn > max_safe_kn) {
    result.is_active = true;
    result.confidence = 0.7f;
    result.preferred_direction = "REDUCE_SPEED";
    result.rationale = "Rule 6: Safe speed recommendation. ODD factor " +
                       std::to_string(factor) +
                       " yields max safe " + std::to_string(max_safe_kn) +
                       " kn; current " + std::to_string(geo.ownship_speed_kn) + " kn exceeds limit.";
  } else {
    result.is_active = false;
    result.confidence = 0.6f;
    result.rationale = "Rule 6: Speed within safe bounds for current ODD. "
                       "Max safe = " + std::to_string(max_safe_kn) + " kn.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
