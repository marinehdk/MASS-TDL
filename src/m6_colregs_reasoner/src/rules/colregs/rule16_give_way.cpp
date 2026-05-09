#include "m6_colregs_reasoner/rules/colregs/rule16_give_way.hpp"

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule16_GiveWay::evaluate(const TargetGeometricState& geo,
                                         OddDomain odd,
                                         const RuleParameters& params) const {
  (void)geo;
  (void)odd;

  RuleEvaluation result{};
  result.rule_id = 16;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Rule 16 cannot determine role from single-target geometry alone.
  // It validates obligations when role was determined by Rules 13-15.
  // For a single-target evaluation, check if CPA indicates give-way is needed.
  if (geo.cpa_m < params.cpa_safe_m * 1.5) {
    // Proximity suggests give-way action may be required
    result.is_active = true;
    result.role = Role::GIVE_WAY;
    result.phase = TimingPhase::SOUND_WARNING;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = params.min_alteration_deg;
    result.confidence = 0.7F;
    result.rationale = "Rule 16: Give-way obligations apply. "
                       "Early and substantial action required per COLREGs. "
                       "CPA=" + std::to_string(geo.cpa_m) +
                       " m indicates proximity. Starboard turn preferred.";
  } else {
    result.is_active = false;
    result.confidence = 0.5F;
    result.rationale = "Rule 16: No give-way obligation triggered at this range. "
                       "CPA=" + std::to_string(geo.cpa_m) + " m.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
