#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"

namespace mass_l3::m7::arbitrator {

// ---------------------------------------------------------------------------
// build_safety_alert
// ---------------------------------------------------------------------------

l3_msgs::msg::SafetyAlert AlertGenerator::build_safety_alert(
    builtin_interfaces::msg::Time const& stamp,
    uint8_t alert_type,
    uint8_t severity,
    std::string_view recommended_mrm,
    float confidence,
    std::string_view rationale,
    std::string_view description) noexcept
{
  l3_msgs::msg::SafetyAlert msg{};
  msg.stamp           = stamp;
  msg.alert_type      = alert_type;
  msg.severity        = severity;
  msg.description     = std::string{description};
  msg.recommended_mrm = std::string{recommended_mrm};
  msg.confidence      = confidence;
  msg.rationale       = std::string{rationale};
  return msg;
}

// ---------------------------------------------------------------------------
// build_asdr_record
// decision_json carries key=value text summary per spec (no JSON serialization).
// signature is left empty — SHA-256 signing is Task 7.
// ---------------------------------------------------------------------------

l3_msgs::msg::ASDRRecord AlertGenerator::build_asdr_record(
    builtin_interfaces::msg::Time const& stamp,
    std::string_view source_module,
    std::string_view decision_type,
    std::string_view decision_summary) noexcept
{
  l3_msgs::msg::ASDRRecord msg{};
  msg.stamp         = stamp;
  msg.source_module = std::string{source_module};
  msg.decision_type = std::string{decision_type};
  msg.decision_json = std::string{decision_summary};
  // msg.signature left empty (Task 7)
  return msg;
}

// ---------------------------------------------------------------------------
// build_sat_data
// sat1.state_summary is set from state_summary arg.
// sat2.trigger_reason and sat2.system_confidence are set from args.
// sat3 fields are left at default (no forecast in periodic health check).
// ---------------------------------------------------------------------------

l3_msgs::msg::SATData AlertGenerator::build_sat_data(
    builtin_interfaces::msg::Time const& stamp,
    std::string_view state_summary,
    float system_confidence,
    std::string_view trigger_reason) noexcept
{
  l3_msgs::msg::SATData msg{};
  msg.stamp         = stamp;
  msg.source_module = "M7_Safety_Supervisor";

  msg.sat1.state_summary = std::string{state_summary};
  // sat1.active_alerts left empty (populated by SafetyArbitrator when needed)

  msg.sat2.trigger_reason    = std::string{trigger_reason};
  msg.sat2.system_confidence = system_confidence;
  // sat2.reasoning_chain left empty for periodic ticks

  // sat3 left at default (no forecast in periodic health checks)

  return msg;
}

}  // namespace mass_l3::m7::arbitrator
