#include "m6_colregs_reasoner/rules/colregs/rule17_stand_on.hpp"

#include <string>

#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
RuleEvaluation Rule17_StandOn::evaluate(const TargetGeometricState& geo,
                                         OddDomain odd,
                                         const RuleParameters& params) const {
  (void)odd;

  RuleEvaluation result{};
  result.rule_id = 17;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Rule 17 applies when own vessel is stand-on.
  // For single-target evaluation, check CPA proximity to determine if stand-on duty exists.
  if (geo.cpa_m > params.cpa_safe_m * 2.0) {
    // Comfortable CPA, no stand-on concern
    result.is_active = false;
    result.confidence = 0.5F;
    result.rationale = "Rule 17: Stand-on not required. "
                       "CPA=" + std::to_string(geo.cpa_m) + " m is comfortable.";
    return result;
  }

  result.is_active = true;
  result.role = Role::STAND_ON;
  result.encounter_type = EncounterType::AMBIGUOUS;

  // Use PhaseClassifier to determine timing phase based on TCPA
  const PhaseClassifier kClassifier;
  result.phase = kClassifier.classify(geo.tcpa_s, params);

  switch (result.phase) {
    case TimingPhase::PRESERVE_COURSE:
      result.preferred_direction = "HOLD";
      result.confidence = 0.7F;
      result.rationale = "Rule 17 Phase 1: Stand-on vessel. Preserve course and speed. "
                         "TCPA=" + std::to_string(geo.tcpa_s) + " s.";
      break;

    case TimingPhase::SOUND_WARNING:
      result.preferred_direction = "HOLD";
      result.confidence = 0.75F;
      result.rationale = "Rule 17 Phase 2: Stand-on vessel. Sound warning signal. "
                         "Give-way vessel not taking appropriate action. "
                         "TCPA=" + std::to_string(geo.tcpa_s) + " s.";
      break;

    case TimingPhase::INDEPENDENT_ACTION:
      result.preferred_direction = "STARBOARD";
      result.min_alteration_deg = params.min_alteration_deg;
      result.confidence = 0.8F;
      result.rationale = "Rule 17 Phase 3: Stand-on may take independent action. "
                         "Give-way vessel has failed to act. TCPA=" +
                         std::to_string(geo.tcpa_s) + " s.";
      break;

    case TimingPhase::CRITICAL_ACTION:
      result.preferred_direction = "STARBOARD";
      result.min_alteration_deg = params.min_alteration_deg * 1.5;
      result.confidence = 0.9F;
      result.rationale = "Rule 17 Phase 4: CRITICAL. Immediate evasive action required. "
                         "TCPA=" + std::to_string(geo.tcpa_s) + " s.";
      break;
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
