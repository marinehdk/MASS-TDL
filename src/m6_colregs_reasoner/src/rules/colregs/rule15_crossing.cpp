#include "m6_colregs_reasoner/rules/colregs/rule15_crossing.hpp"

#include "m6_colregs_reasoner/geometry_utils.hpp"

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
  const double rel_bearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);

  const bool crossing_starboard = (rel_bearing >= 22.5 && rel_bearing < 112.5);
  const bool crossing_port = (rel_bearing >= 247.5 && rel_bearing < 337.5);

  if (!crossing_starboard && !crossing_port) {
    result.is_active = false;
    result.confidence = 0.6f;
    result.rationale = "Rule 15: Not a crossing situation. "
                       "Relative bearing " + std::to_string(rel_bearing) +
                       " deg outside crossing sectors (22.5-112.5 or 247.5-337.5).";
    return result;
  }

  if (crossing_starboard) {
    // Target on starboard side → give-way (COLREGs Rule 15(a))
    result.is_active = true;
    result.role = Role::GIVE_WAY;
    result.encounter_type = EncounterType::CROSSING;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = params.min_alteration_deg;
    result.confidence = 0.85f;
    result.rationale = "Rule 15: Crossing situation. Target on starboard side "
                       "(bearing " + std::to_string(rel_bearing) +
                       " deg). Give-way obligation — alter course to starboard.";
  } else {
    // Target on port side → stand-on
    result.is_active = true;
    result.role = Role::STAND_ON;
    result.encounter_type = EncounterType::CROSSING;
    result.preferred_direction = "HOLD";
    result.confidence = 0.8f;
    result.rationale = "Rule 15: Crossing situation. Target on port side "
                       "(bearing " + std::to_string(rel_bearing) +
                       " deg). Stand-on obligation — hold course and speed.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
