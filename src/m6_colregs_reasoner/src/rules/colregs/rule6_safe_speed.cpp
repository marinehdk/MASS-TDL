#include "m6_colregs_reasoner/rules/colregs/rule6_safe_speed.hpp"

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

  // max_speed_kn is loaded from odd_aware_thresholds.yaml (one value per ODD zone)
  if (geo.ownship_speed_kn > params.max_speed_kn) {
    result.is_active = true;
    result.confidence = 0.7f;
    result.preferred_direction = "REDUCE_SPEED";
    result.rationale = "Rule 6: Safe speed exceeded. Max=" +
                       std::to_string(params.max_speed_kn) +
                       " kn; current=" + std::to_string(geo.ownship_speed_kn) + " kn.";
  } else {
    result.is_active = false;
    result.confidence = 0.6f;
    result.rationale = "Rule 6: Speed within ODD safe bound (" +
                       std::to_string(params.max_speed_kn) + " kn).";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
