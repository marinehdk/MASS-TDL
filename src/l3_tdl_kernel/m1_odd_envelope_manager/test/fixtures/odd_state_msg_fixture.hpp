#pragma once

#include <string>

#include "l3_msgs/msg/odd_state.hpp"

namespace mass_l3::m1::test {

/// Build an ODDState message for test use.
inline l3_msgs::msg::ODDState make_odd_state_msg(
    uint8_t zone = 0,
    uint8_t level = 3,
    float conf = 1.0f,
    const std::string& rationale = "test") {
  l3_msgs::msg::ODDState msg{};
  msg.stamp = builtin_interfaces::msg::Time{};
  msg.current_zone = zone;
  msg.auto_level = level;
  msg.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  msg.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  msg.conformance_score = conf;
  msg.tmr_s = 60.0f;
  msg.tdl_s = 30.0f;
  msg.zone_reason = "test";
  msg.confidence = conf;
  msg.rationale = rationale;
  return msg;
}

}  // namespace mass_l3::m1::test
