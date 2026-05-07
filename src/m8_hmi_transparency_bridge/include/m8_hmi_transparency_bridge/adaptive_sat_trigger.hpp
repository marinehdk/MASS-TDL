// include/m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp
#ifndef MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
#define MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_

#include <optional>
#include <string>
#include <vector>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/colregs_constraint.hpp"
#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

/// v1.1.2 §12.2 adaptive SAT trigger decision
struct SatTriggerDecision {
  bool sat1_visible{true};                    // always true
  bool sat2_visible{false};
  bool sat3_visible{true};                    // always baseline
  bool sat3_priority_high{false};             // true when TDL < threshold
  std::vector<std::string> sat2_trigger_reasons{};
  std::string sat3_alert_color{"normal"};     // "normal" | "bold_red"
};

/// v1.1.2 §12.2 adaptive SAT display trigger
///
/// SAT-1: always visible.
/// SAT-2: shown only when ≥1 of 4 conditions triggers (transparency paradox mitigation).
/// SAT-3: always baseline visible; promoted to bold_red when TDL < sat3_priority_high_tdl_s.
class AdaptiveSatTrigger final {
 public:
  struct Thresholds {
    double sat3_priority_high_tdl_s{30.0};         // [TBD-HAZID]
    double sat2_system_confidence_threshold{0.6};  // [TBD-HAZID]
    float threat_confidence_threshold{0.7F};       // [TBD-HAZID]
    float rule_confidence_threshold{0.8F};         // [TBD-HAZID]
    double stale_threshold_s{2.0};                 // skip confidence check for sources silent > N s
  };

  explicit AdaptiveSatTrigger(Thresholds t) noexcept : thresholds_(t) {}

  /// Main entry: evaluate all 4 SAT-2 trigger conditions.
  [[nodiscard]] SatTriggerDecision decide(
      const l3_msgs::msg::ODDState& odd,
      const SatAggregator& sat_cache,
      const std::optional<l3_msgs::msg::SafetyAlert>& latest_m7_alert,
      const std::optional<l3_msgs::msg::COLREGsConstraint>& latest_m6_constraint,
      bool operator_requested_sat2,
      SatAggregator::TimePoint now) const;

  /// Test-access helpers (expose private logic for unit testing)
  [[nodiscard]] bool has_colreg_conflict_for_test(
      const std::optional<l3_msgs::msg::COLREGsConstraint>& m6) const;
  [[nodiscard]] bool m7_alert_triggers_sat2_for_test(
      const std::optional<l3_msgs::msg::SafetyAlert>& alert) const;

 private:
  Thresholds thresholds_;

  /// Condition 1: COLREGs conflict detected (m6.conflict_detected == true)
  [[nodiscard]] bool has_colreg_conflict(
      const std::optional<l3_msgs::msg::COLREGsConstraint>& m6) const;

  /// Condition 2: M7 SOTIF/safety alert severity ≥ WARNING
  [[nodiscard]] bool m7_alert_triggers_sat2(
      const std::optional<l3_msgs::msg::SafetyAlert>& alert) const;

  /// Condition 3: system confidence drop — any source's sat2.system_confidence < threshold
  [[nodiscard]] bool system_confidence_dropped(
      const SatAggregator& sat_cache,
      SatAggregator::TimePoint now) const;

  /// Condition 4: TDL compression — TDL < sat3_priority_high_tdl_s (same threshold)
  [[nodiscard]] bool tdl_compressed(const l3_msgs::msg::ODDState& odd) const;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_ADAPTIVE_SAT_TRIGGER_HPP_
