#include "m6_colregs_reasoner/rules/colregs/rule14_head_on.hpp"

#include <cmath>
#include <string>

#include "m6_colregs_reasoner/geometry_utils.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule14_HeadOn::evaluate(const TargetGeometricState& geo,
                                        OddDomain odd,
                                        const RuleParameters& params) const {
  (void)odd;
  (void)params;

  RuleEvaluation result{};
  result.rule_id = 14;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // COLREGs Rule 14: reciprocal or nearly reciprocal courses.
  // Both vessels' headings differ by ~180° AND target is on the bow.

  // Check 1: headings are nearly reciprocal (courses differ ~180°)
  const double kCourseDiff = angle_diff_deg(geo.ownship_heading_deg, geo.target_heading_deg);
  const bool kReciprocalCourses = std::abs(kCourseDiff - 180.0) < 6.0;

  // Check 2: target is near dead ahead (relative bearing ~0°)
  const double kRelBearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);
  const bool kHeadOnBearing = (kRelBearing < 6.0 || kRelBearing > 354.0);

  // Check 3: target's bow is pointed toward us (aspect ~0°)
  const double kAspect = aspect_angle_deg(geo.ownship_heading_deg, geo.target_heading_deg,
                                         kRelBearing);
  const bool kReciprocalAspect = kAspect < 10.0 || kAspect > 350.0;

  if (!(kReciprocalCourses || (kHeadOnBearing && kReciprocalAspect))) {
    result.is_active = false;
    result.confidence = 0.6F;
    result.rationale = "Rule 14: Not head-on. Course diff from reciprocal=" +
                       std::to_string(std::abs(kCourseDiff - 180.0)) +
                       " deg, rel_bearing=" + std::to_string(kRelBearing) + " deg.";
    return result;
  }

  // Both give-way in head-on situation (Rule 14(a))
  result.is_active = true;
  result.role = Role::BOTH_GIVE_WAY;
  result.encounter_type = EncounterType::HEAD_ON;
  result.preferred_direction = "STARBOARD";
  result.min_alteration_deg = params.min_alteration_deg;
  result.confidence = 0.85F;
  result.rationale = "Rule 14: Head-on situation detected. "
                     "Both vessels shall alter course to starboard. "
                     "Course diff=" + std::to_string(kCourseDiff) +
                     " deg, rel_bearing=" + std::to_string(kRelBearing) + " deg.";

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
