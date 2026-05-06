#include "m6_colregs_reasoner/rules/colregs/rule5_lookout.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule5_Lookout::evaluate(const TargetGeometricState& geo,
                                       OddDomain odd,
                                       const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.is_active = true;
  result.rule_id = 5;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";
  result.confidence = 0.25f;  // low confidence: baseline obligation, no specific action
  result.rationale = "Rule 5: Maintain proper look-out by sight and hearing. "
                     "Baseline obligation always active. Target " +
                     std::to_string(geo.target_id);
  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
