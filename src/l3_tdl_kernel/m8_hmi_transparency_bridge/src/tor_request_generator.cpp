// src/tor_request_generator.cpp
#include "m8_hmi_transparency_bridge/tor_request_generator.hpp"

namespace mass_l3::m8 {

// ---------------------------------------------------------------------------
// generate — construct the ToR request message
// ---------------------------------------------------------------------------

l3_msgs::msg::ToRRequest ToRRequestGenerator::generate(
    TorProtocol::Reason reason,
    const l3_msgs::msg::ODDState& odd,
    const std::string& sat1_context_summary,
    double deadline_s) const
{
  l3_msgs::msg::ToRRequest msg{};
  // stamp left at zero — node will stamp before publishing

  msg.reason = reason_to_msg_enum(reason);
  msg.deadline_s = static_cast<float>(deadline_s);
  msg.target_level = l3_msgs::msg::ToRRequest::TARGET_LEVEL_D2;  // default: D2
  msg.confidence = 0.9F;  // ToR is a deterministic protocol action
  msg.context_summary = sat1_context_summary;
  msg.recommended_action = recommended_action_for(reason, odd);

  return msg;
}

// ---------------------------------------------------------------------------
// reason_to_msg_enum
// ---------------------------------------------------------------------------

uint8_t ToRRequestGenerator::reason_to_msg_enum(TorProtocol::Reason r) noexcept
{
  switch (r) {
    case TorProtocol::Reason::kOddExit:
      return l3_msgs::msg::ToRRequest::REASON_ODD_EXIT;
    case TorProtocol::Reason::kManualRequest:
      return l3_msgs::msg::ToRRequest::REASON_MANUAL_REQUEST;
    case TorProtocol::Reason::kSafetyAlert:
      return l3_msgs::msg::ToRRequest::REASON_SAFETY_ALERT;
    default:
      return l3_msgs::msg::ToRRequest::REASON_ODD_EXIT;
  }
}

// ---------------------------------------------------------------------------
// recommended_action_for
// ---------------------------------------------------------------------------

std::string ToRRequestGenerator::recommended_action_for(
    TorProtocol::Reason r, const l3_msgs::msg::ODDState& odd)
{
  switch (r) {
    case TorProtocol::Reason::kOddExit:
      return "Take manual helm control: vessel approaching ODD boundary ["
             + odd.zone_reason + "]";
    case TorProtocol::Reason::kManualRequest:
      return "Operator-initiated takeover: review SAT-1 before proceeding";
    case TorProtocol::Reason::kSafetyAlert:
      return "Safety alert detected: immediate helm takeover recommended";
    default:
      return "Operator takeover required";
  }
}

}  // namespace mass_l3::m8
