// include/m8_hmi_transparency_bridge/tor_request_generator.hpp
#ifndef MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
#define MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_

#include <string>

#include "l3_msgs/msg/to_r_request.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

namespace mass_l3::m8 {

/// Constructs ToR_RequestMsg: 60s deadline, SAT-1 context summary, recommended action.
class ToRRequestGenerator final {
 public:
  [[nodiscard]] l3_msgs::msg::ToRRequest generate(
      TorProtocol::Reason reason,
      const l3_msgs::msg::ODDState& odd,
      const std::string& sat1_context_summary,
      double deadline_s) const;

 private:
  [[nodiscard]] static uint8_t reason_to_msg_enum(TorProtocol::Reason r) noexcept;
  [[nodiscard]] static std::string recommended_action_for(
      TorProtocol::Reason r, const l3_msgs::msg::ODDState& odd);
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_TOR_REQUEST_GENERATOR_HPP_
