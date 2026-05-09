#include "m6_colregs_reasoner/rules/colregs/rule6_safe_speed.hpp"

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule6_SafeSpeed::evaluate(const TargetGeometricState& geo,
                                         OddDomain odd,
                                         const RuleParameters& params) const {
  (void)odd;  // ODD-aware speed ceiling is already encoded in params.max_speed_kn

  RuleEvaluation result{};
  result.rule_id = 6;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // max_speed_kn is loaded from odd_aware_thresholds.yaml (one value per ODD zone).
  // Fall back to ODD-specific defaults when the parameter is unset (== 0).
  double effective_max = params.max_speed_kn;
  if (effective_max <= 0.0) {
    switch (odd) {
      case OddDomain::ODD_A: effective_max = 20.0; break;
      case OddDomain::ODD_B: effective_max = 15.0; break;
      case OddDomain::ODD_C: effective_max = 12.0; break;
      case OddDomain::ODD_D: effective_max = 10.0; break;
      default:               effective_max = 16.0; break;  // conservative for UNKNOWN
    }
  }

  if (geo.ownship_speed_kn > effective_max) {
    result.is_active = true;
    result.confidence = 0.7F;
    result.preferred_direction = "REDUCE_SPEED";
    result.rationale = "Rule 6: Safe speed exceeded. Max=" +
                       std::to_string(effective_max) +
                       " kn; current=" + std::to_string(geo.ownship_speed_kn) + " kn.";
  } else {
    result.is_active = false;
    result.confidence = 0.6F;
    result.rationale = "Rule 6: Speed within ODD safe bound (" +
                       std::to_string(effective_max) + " kn).";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
