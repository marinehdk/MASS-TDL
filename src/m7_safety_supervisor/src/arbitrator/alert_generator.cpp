#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "m7_safety_supervisor/common/sha256.hpp"

#include <string_view>

#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/safety_alert.hpp"

namespace mass_l3::m7::arbitrator {

// ---------------------------------------------------------------------------
// build_safety_alert
// ---------------------------------------------------------------------------

l3_msgs::msg::SafetyAlert AlertGenerator::build_safety_alert(
    builtin_interfaces::msg::Time const& stamp,
    SafetyAlertParams const& params) noexcept
{
  l3_msgs::msg::SafetyAlert msg{};
  msg.stamp           = stamp;
  msg.alert_type      = params.alert_type;
  msg.severity        = params.severity;
  msg.description     = std::string{params.description};
  msg.recommended_mrm = std::string{params.recommended_mrm};
  msg.confidence      = params.confidence;
  msg.rationale       = std::string{params.rationale};
  return msg;
}

// ---------------------------------------------------------------------------
// build_asdr_record
// decision_json carries key=value text summary per spec (no JSON serialization).
// SHA-256 digest of decision_json is written to signature (32 bytes).
// ---------------------------------------------------------------------------

l3_msgs::msg::ASDRRecord AlertGenerator::build_asdr_record(
    builtin_interfaces::msg::Time const& stamp,
    AsdrRecordParams const& params) noexcept
{
  l3_msgs::msg::ASDRRecord msg{};
  msg.stamp         = stamp;
  msg.source_module = std::string{params.source_module};
  msg.decision_type = std::string{params.decision_type};
  msg.decision_json = std::string{params.decision_summary};
  auto const kDigest = common::sha256(msg.decision_json);
  msg.signature.assign(kDigest.begin(), kDigest.end());
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
