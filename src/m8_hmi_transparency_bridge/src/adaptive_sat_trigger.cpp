// src/adaptive_sat_trigger.cpp
#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"

#include <cstdint>

namespace mass_l3::m8 {

// ---------------------------------------------------------------------------
// decide — main entry point
// ---------------------------------------------------------------------------

SatTriggerDecision AdaptiveSatTrigger::decide(
    const l3_msgs::msg::ODDState& odd,
    const SatAggregator& sat_cache,
    const std::optional<l3_msgs::msg::SafetyAlert>& latest_m7_alert,
    const std::optional<l3_msgs::msg::COLREGsConstraint>& latest_m6_constraint,
    bool operator_requested_sat2,
    SatAggregator::TimePoint now) const
{
  SatTriggerDecision result{};  // sat1_visible=true, sat3_visible=true by default

  // --- SAT-3 TDL promotion ---
  if (tdl_compressed(odd)) {
    result.sat3_priority_high = true;
    result.sat3_alert_color = "bold_red";
  }

  // --- SAT-2 trigger conditions (independent) ---
  if (has_colreg_conflict(latest_m6_constraint)) {
    result.sat2_visible = true;
    result.sat2_trigger_reasons.push_back("colreg_conflict");
  }
  if (m7_alert_triggers_sat2(latest_m7_alert)) {
    result.sat2_visible = true;
    result.sat2_trigger_reasons.push_back("m7_sotif_warning");
  }
  if (system_confidence_dropped(sat_cache, now)) {
    result.sat2_visible = true;
    result.sat2_trigger_reasons.push_back("system_confidence_drop");
  }
  if (operator_requested_sat2) {
    result.sat2_visible = true;
    result.sat2_trigger_reasons.push_back("operator_request");
  }

  return result;
}

// ---------------------------------------------------------------------------
// Test-access helpers
// ---------------------------------------------------------------------------

bool AdaptiveSatTrigger::has_colreg_conflict_for_test(
    const std::optional<l3_msgs::msg::COLREGsConstraint>& m6) const
{
  return has_colreg_conflict(m6);
}

bool AdaptiveSatTrigger::m7_alert_triggers_sat2_for_test(
    const std::optional<l3_msgs::msg::SafetyAlert>& alert) const
{
  return m7_alert_triggers_sat2(alert);
}

// ---------------------------------------------------------------------------
// Private — condition implementations
// ---------------------------------------------------------------------------

bool AdaptiveSatTrigger::has_colreg_conflict(
    const std::optional<l3_msgs::msg::COLREGsConstraint>& m6) const
{
  return m6.has_value() && m6->conflict_detected;
}

bool AdaptiveSatTrigger::m7_alert_triggers_sat2(
    const std::optional<l3_msgs::msg::SafetyAlert>& alert) const
{
  return alert.has_value()
         && alert->severity >= l3_msgs::msg::SafetyAlert::SEVERITY_WARNING;
}

bool AdaptiveSatTrigger::system_confidence_dropped(
    const SatAggregator& sat_cache,
    SatAggregator::TimePoint now) const
{
  // Suppress unused-parameter warning: now is passed for API consistency
  // (future: age-gate stale entries before checking confidence)
  (void)now;

  const auto threshold = static_cast<float>(thresholds_.sat2_system_confidence_threshold);

  for (uint8_t i = 0; i < static_cast<uint8_t>(SatAggregator::SourceModule::kCount); ++i) {
    const auto src = static_cast<SatAggregator::SourceModule>(i);
    auto maybe_sat2 = sat_cache.latest_sat2(src);
    if (maybe_sat2.has_value() && maybe_sat2->system_confidence < threshold) {
      return true;
    }
  }
  return false;
}

bool AdaptiveSatTrigger::tdl_compressed(const l3_msgs::msg::ODDState& odd) const
{
  return static_cast<double>(odd.tdl_s) < thresholds_.sat3_priority_high_tdl_s;
}

}  // namespace mass_l3::m8
