#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"

namespace mass_l3::m7::sotif {

bool TriggeringConditionDetector::detect(AssumptionStatus const& assumption) const noexcept {
  return assumption.total_violation_count >= kExtremeScenarioThreshold;
}

}  // namespace mass_l3::m7::sotif
