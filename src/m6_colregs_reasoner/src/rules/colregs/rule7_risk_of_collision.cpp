#include "m6_colregs_reasoner/rules/colregs/rule7_risk_of_collision.hpp"

#include <algorithm>
#include <cmath>

#include "m6_colregs_reasoner/geometry_utils.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

RuleEvaluation Rule7_RiskOfCollision::evaluate(const TargetGeometricState& geo,
                                                OddDomain odd,
                                                const RuleParameters& params) const {
  (void)odd;

  RuleEvaluation result{};
  result.rule_id = 7;
  result.encounter_type = EncounterType::NONE;
  result.role = Role::FREE;
  result.phase = TimingPhase::PRESERVE_COURSE;
  result.min_alteration_deg = 0.0;
  result.preferred_direction = "HOLD";
  result.confidence = 0.5f;

  // Primary check: CPA below safe threshold
  const bool cpa_risk = (geo.cpa_m < params.cpa_safe_m);

  // Secondary check: constant bearing, decreasing range.
  // With single-snapshot data, approximate via bearing near own heading
  // and close TCPA.
  const double rel_bearing = relative_bearing_deg(geo.ownship_heading_deg, geo.bearing_deg);
  const bool bearing_risk = (rel_bearing < 30.0 || rel_bearing > 330.0) &&
                            geo.tcpa_s < 180.0 &&
                            geo.tcpa_s > 0.0;

  const bool risk_detected = cpa_risk || bearing_risk;

  if (risk_detected) {
    result.is_active = true;
    result.encounter_type = EncounterType::AMBIGUOUS;

    // Confidence proportional to CPA margin
    double margin_ratio = 1.0;
    if (geo.cpa_m > 0.0) {
      margin_ratio = std::min(1.0, (params.cpa_safe_m - geo.cpa_m) / params.cpa_safe_m);
    }
    result.confidence = static_cast<float>(0.5 + 0.5 * margin_ratio);
    result.confidence = std::min(1.0f, result.confidence);

    result.rationale = "Rule 7: Risk of collision detected. "
                       "CPA=" + std::to_string(geo.cpa_m) +
                       " m (threshold=" + std::to_string(params.cpa_safe_m) +
                       " m), TCPA=" + std::to_string(geo.tcpa_s) +
                       " s, bearing=" + std::to_string(geo.bearing_deg) +
                       " deg, target " + std::to_string(geo.target_id);
  } else {
    result.is_active = false;
    result.rationale = "Rule 7: No risk of collision. "
                       "CPA=" + std::to_string(geo.cpa_m) +
                       " m exceeds safe threshold.";
  }

  return result;
}

}  // namespace mass_l3::m6_colregs::rules::colregs
