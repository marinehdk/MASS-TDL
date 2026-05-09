#include "m6_colregs_reasoner/rules/colregs/rule5_lookout.hpp"

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule5_Lookout::evaluate(const TargetGeometricState& geo,
                                       OddDomain odd,
                                       const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 5;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Rule 5 (proper look-out) is a continuous, unconditional obligation under
  // COLREGs. is_active = true for every evaluation so that callers can audit
  // compliance; the preferred_direction = "HOLD" signals no corrective action.
  result.is_active = true;
  result.confidence = 0.25F;
  result.rationale = "Rule 5: Maintain proper look-out — system-level obligation "
                     "(not actionable per-target). Target " +
                     std::to_string(geo.target_id);
  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
