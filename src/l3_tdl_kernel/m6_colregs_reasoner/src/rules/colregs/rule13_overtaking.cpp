#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"

#include <string>

#include "m6_colregs_reasoner/geometry_utils.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
RuleEvaluation Rule13_Overtaking::evaluate(const TargetGeometricState& geo,
                                            OddDomain odd,
                                            const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 13;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // COLREGs Rule 13: overtaking sector = more than 22.5 deg abaft the beam.
  // Relative bearing 112.5 to 247.5 degrees indicates target is overtaking from astern.
  // (Beam is at 90 deg / 270 deg relative; 22.5 deg abaft = 112.5 / 247.5)
  const double kRelBearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);

  const bool kIsOvertakingSector = (kRelBearing >= 112.5 && kRelBearing <= 247.5);

  if (!kIsOvertakingSector) {
    result.is_active = false;
    result.confidence = 0.6F;
    result.rationale = "Rule 13: Target bearing " +
                       std::to_string(kRelBearing) +
                       " deg not in overtaking sector (112.5-247.5). Not overtaking.";
    return result;
  }

  // Determine if own vessel is overtaking or being overtaken.
  // Simple heuristic: if own speed significantly higher, own is overtaking.
  // For Phase E1, we use relative speed sign as a proxy.
  // Positive relative_speed_kn means target is approaching (faster or closing).
  // We conservatively assume: if target is in overtaking sector AND aspect indicates
  // the target's heading is roughly same direction, it's being overtaken (they are faster).
  // Otherwise, own ship is overtaking.
  const double kAspect = aspect_angle_deg(geo.ownship_heading_deg, geo.target_heading_deg,
                                          kRelBearing);
  const bool kTargetHeadingSameDir = (kAspect > 315.0 || kAspect < 45.0);

  if (kTargetHeadingSameDir && geo.relative_speed_kn > 0.0) {
    // Target is faster and heading same direction → being overtaken (we are overtaking)
    result.is_active = true;
    result.role = Role::GIVE_WAY;
    result.encounter_type = EncounterType::OVERTAKING;
    result.preferred_direction = "STARBOARD";
    result.confidence = 0.75F;
    result.rationale = "Rule 13: Overtaking. Own vessel overtaking target " +
                       std::to_string(geo.target_id) +
                       " from astern. Give-way obligation.";
  } else {
    // Being overtaken → stand-on
    result.is_active = true;
    result.role = Role::STAND_ON;
    result.encounter_type = EncounterType::OVERTAKING;
    result.preferred_direction = "HOLD";
    result.confidence = 0.7F;
    result.rationale = "Rule 13: Being overtaken. Target " +
                       std::to_string(geo.target_id) +
                       " approaching from astern. Stand-on obligation.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
