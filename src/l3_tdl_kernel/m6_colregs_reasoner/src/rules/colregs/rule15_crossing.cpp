#include "m6_colregs_reasoner/rules/colregs/rule15_crossing.hpp"

#include <string>

#include "m6_colregs_reasoner/geometry_utils.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule15_Crossing::evaluate(const TargetGeometricState& geo,
                                          OddDomain odd,
                                          const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 15;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // COLREGs Rule 15: Crossing.
  // Target relative bearing determines crossing side:
  //   Starboard side: 22.5 - 112.5 deg → give-way (own vessel must avoid)
  //   Port side: 247.5 - 337.5 deg → stand-on (own vessel holds course)
  const double kRelBearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);

  const bool kCrossingStarboard = (kRelBearing >= 22.5 && kRelBearing < 112.5);
  const bool kCrossingPort = (kRelBearing >= 247.5 && kRelBearing < 337.5);

  if (!kCrossingStarboard && !kCrossingPort) {
    result.is_active = false;
    result.confidence = 0.6F;
    result.rationale = "Rule 15: Not a crossing situation. "
                       "Relative bearing " + std::to_string(kRelBearing) +
                       " deg outside crossing sectors (22.5-112.5 or 247.5-337.5).";
    return result;
  }

  if (kCrossingStarboard) {
    // Target on starboard side → give-way (COLREGs Rule 15(a))
    result.is_active = true;
    result.role = Role::GIVE_WAY;
    result.encounter_type = EncounterType::CROSSING;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = params.min_alteration_deg;
    result.confidence = 0.85F;
    result.rationale = "Rule 15: Crossing situation. Target on starboard side "
                       "(bearing " + std::to_string(kRelBearing) +
                       " deg). Give-way obligation — alter course to starboard.";
  } else {
    // Target on port side → stand-on
    result.is_active = true;
    result.role = Role::STAND_ON;
    result.encounter_type = EncounterType::CROSSING;
    result.preferred_direction = "HOLD";
    result.confidence = 0.8F;
    result.rationale = "Rule 15: Crossing situation. Target on port side "
                       "(bearing " + std::to_string(kRelBearing) +
                       " deg). Stand-on obligation — hold course and speed.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
