#include "m6_colregs_reasoner/rules/colregs/rule8_action_to_avoid.hpp"

#include <string>

#include "m6_colregs_reasoner/geometry_utils.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule8_ActionToAvoid::evaluate(const TargetGeometricState& geo,
                                              OddDomain odd,
                                              const RuleParameters& params) const {
  (void)odd;

  RuleEvaluation result{};
  result.rule_id = 8;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Active when CPA < safe threshold (risk exists)
  const bool kRiskExists = (geo.cpa_m < params.cpa_safe_m);

  if (!kRiskExists) {
    result.is_active = false;
    result.confidence = 0.5F;
    result.rationale = "Rule 8: No action needed. CPA threshold not triggered.";
    return result;
  }

  result.is_active = true;
  result.confidence = 0.8F;

  // COLREGs Rule 8(a): starboard turn preferred
  const double kRelBearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);

  if (kRelBearing >= 0.0 && kRelBearing < 180.0) {
    // Target on starboard side — turn starboard to pass astern
    result.preferred_direction = "STARBOARD";
    result.rationale = "Rule 8: Action to avoid collision. "
                       "Target on starboard side (bearing " +
                       std::to_string(kRelBearing) +
                       " deg). Starboard turn recommended per COLREGs Rule 8(a).";
  } else {
    // Target on port side — turn starboard to increase CPA
    result.preferred_direction = "STARBOARD";
    result.rationale = "Rule 8: Action to avoid collision. "
                       "Target on port side (bearing " +
                       std::to_string(kRelBearing) +
                       " deg). Starboard turn recommended per COLREGs Rule 8(a).";
  }

  // Use ODD-aware min alteration from parameters
  result.min_alteration_deg = params.min_alteration_deg;

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
