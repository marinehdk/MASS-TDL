#include "m6_colregs_reasoner/rules/colregs/rule19_restricted_visibility.hpp"

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule19_RestrictedVisibility::evaluate(const TargetGeometricState& geo,
                                                      OddDomain odd,
                                                      const RuleParameters& params) const {
  RuleEvaluation result{};
  result.rule_id = 19;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";

  // Rule 19 only applies in restricted visibility (ODD Domain D)
  if (odd != OddDomain::ODD_D) {
    result.is_active = false;
    result.confidence = 0.5F;
    result.rationale = "Rule 19: Restricted visibility not active. "
                       "Current ODD domain is not D.";
    return result;
  }

  result.is_active = true;
  result.encounter_type = EncounterType::RESTRICTED_VIS;

  // In restricted visibility:
  // 1. Safe speed shall be reduced
  // 2. CPA thresholds are tighter (more conservative)
  // 3. Rules 11-18 do not apply in restricted visibility (Rule 19(b))

  // Scale: in restricted visibility, CPA threshold is more stringent
  const double kCpaMargin = geo.cpa_m / (params.cpa_safe_m * 0.7);

  if (kCpaMargin < 1.0) {
    // Target within reduced CPA threshold — evasive action needed
    result.role = Role::GIVE_WAY;
    result.preferred_direction = "STARBOARD";
    result.min_alteration_deg = params.min_alteration_deg * 1.5;  // larger alteration in restricted vis
    result.confidence = 0.85F;
    result.rationale = "Rule 19: Restricted visibility. "
                       "Target " + std::to_string(geo.target_id) +
                       " within CPA threshold. "
                       "Reduce speed, proceed at safe speed. "
                       "Starboard turn recommended. "
                       "CPA=" + std::to_string(geo.cpa_m) +
                       " m, threshold=" +
                       std::to_string(params.cpa_safe_m * 0.7) + " m.";
  } else {
    // Target beyond reduced CPA threshold — monitor
    result.role = Role::FREE;
    result.preferred_direction = "REDUCE_SPEED";
    result.confidence = 0.7F;
    result.rationale = "Rule 19: Restricted visibility. "
                       "Target " + std::to_string(geo.target_id) +
                       " outside CPA threshold. "
                       "Maintain safe speed, continuous monitoring. "
                       "CPA=" + std::to_string(geo.cpa_m) + " m.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
