#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"

#include <algorithm>
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

  // COLREGs Rule 13: the overtaking vessel comes up from more than 22.5 deg
  // abaft the other vessel's beam. That requires checking both perspectives:
  // target-in-own-stern means own is being overtaken; own-in-target-stern means
  // own is overtaking the target ahead.
  constexpr double kAbaftBeamMinDeg = 112.5;
  constexpr double kAbaftBeamMaxDeg = 247.5;
  constexpr double kSameCourseMaxDeg = 45.0;
  constexpr double kMinOvertakingSpeedDiffKn = 2.0;
  const auto in_abaft_beam_sector = [](double relative_bearing_deg_value) {
    return relative_bearing_deg_value >= kAbaftBeamMinDeg &&
        relative_bearing_deg_value <= kAbaftBeamMaxDeg;
  };

  const double kTargetRelBearingFromOwn =
      relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);
  const double kOwnBearingFromTarget = normalize_bearing_deg(geo.bearing_deg + 180.0);
  const double kOwnRelBearingFromTarget =
      relative_bearing_deg(geo.target_heading_deg, kOwnBearingFromTarget);
  const bool kSameCourse =
      angle_diff_deg(geo.ownship_heading_deg, geo.target_heading_deg) <= kSameCourseMaxDeg;
  const bool kOwnFaster =
      geo.ownship_speed_kn > geo.target_speed_kn + kMinOvertakingSpeedDiffKn;
  const bool kTargetFaster =
      geo.target_speed_kn > geo.ownship_speed_kn + kMinOvertakingSpeedDiffKn;
  const bool kOwnOvertakingTarget =
      kSameCourse && kOwnFaster && in_abaft_beam_sector(kOwnRelBearingFromTarget);
  const bool kTargetOvertakingOwn =
      kSameCourse && kTargetFaster && in_abaft_beam_sector(kTargetRelBearingFromOwn);

  if (!kOwnOvertakingTarget && !kTargetOvertakingOwn) {
    result.is_active = false;
    result.confidence = 0.6F;
    result.rationale = "Rule 13: Target bearing " +
                       std::to_string(kTargetRelBearingFromOwn) +
                       " deg / own bearing from target " +
                       std::to_string(kOwnRelBearingFromTarget) +
                       " deg not an overtaking geometry.";
    return result;
  }

  if (kOwnOvertakingTarget) {
    result.is_active = true;
    result.role = Role::GIVE_WAY;
    result.encounter_type = EncounterType::OVERTAKING;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = std::max(params.min_alteration_deg, 65.0);
    result.confidence = 0.75F;
    result.rationale = "Rule 13: Overtaking. Own vessel overtaking target " +
                       std::to_string(geo.target_id) +
                       " from astern. Give-way obligation.";
  } else {
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
