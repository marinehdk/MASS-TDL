#ifndef MASS_L3_M5_BC_MPC_OVERRIDE_GENERATOR_HPP_
#define MASS_L3_M5_BC_MPC_OVERRIDE_GENERATOR_HPP_

// M5 Tactical Planner — BC-MPC Override Generator
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Converts BcMpcSolution to ReactiveOverrideCmd. Stateless — all state lives
// in BcMpcNode. Heading normalisation to [0, 360) is done here so callers
// receive a compliant nav-display-ready value.

#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "rclcpp/time.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcOverrideGenerator {
 public:
  BcMpcOverrideGenerator() = default;

  // Convert BcMpcSolution to ReactiveOverrideCmd.
  // Pre-condition: solution.status == BcMpcSolution::Status::Override.
  // If solution has Resolved status, still generates the message (caller decides whether to send).
  [[nodiscard]] l3_msgs::msg::ReactiveOverrideCmd generate(
      const BcMpcSolution& solution,
      const rclcpp::Time& trigger_time) const;
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_OVERRIDE_GENERATOR_HPP_
