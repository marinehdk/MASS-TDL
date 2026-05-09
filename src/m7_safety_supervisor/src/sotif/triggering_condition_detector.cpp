#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

namespace mass_l3::m7::sotif {

bool TriggeringConditionDetector::detect(AssumptionStatus const& assumption) noexcept {
  return assumption.total_violation_count >= kExtremeScenarioThreshold;
}

}  // namespace mass_l3::m7::sotif
