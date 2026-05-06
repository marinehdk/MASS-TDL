#ifndef M7_SAFETY_SUPERVISOR_SOTIF_TRIGGERING_CONDITION_DETECTOR_HPP_
#define M7_SAFETY_SUPERVISOR_SOTIF_TRIGGERING_CONDITION_DETECTOR_HPP_

#include <cstdint>
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"

namespace mass_l3::m7::sotif {

// TriggeringConditionDetector: ISO 21448 SOTIF triggering condition detection.
// A scenario is "extreme" (triggering condition) when >= 3 SOTIF assumptions
// are simultaneously violated, indicating the system is operating outside its
// known safe envelope and MRC escalation is required.
//
// INVARIANT: Single-threaded (main_loop callback group only).
class TriggeringConditionDetector {
public:
  TriggeringConditionDetector() noexcept = default;

  // Returns true when assumption.total_violation_count >= kExtremeScenarioThreshold (3).
  // ISO 21448 §5.3: simultaneous multi-assumption failure constitutes a triggering condition.
  [[nodiscard]] bool detect(AssumptionStatus const& assumption) const noexcept;

  // RFC-003 LOCKED: 3 simultaneous SOTIF assumption violations = extreme scenario.
  static constexpr uint32_t kExtremeScenarioThreshold = 3u;
};

}  // namespace mass_l3::m7::sotif

#endif  // M7_SAFETY_SUPERVISOR_SOTIF_TRIGGERING_CONDITION_DETECTOR_HPP_
