#ifndef MASS_L3_M5_BC_MPC_NODE_HPP_
#define MASS_L3_M5_BC_MPC_NODE_HPP_

// M5 Tactical Planner — BC-MPC ROS2 Node
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Event-driven (triggered by WorldState at 4 Hz).
// Publishes ReactiveOverrideCmd when BC-MPC detects imminent collision.
// validity_timer_ at 10 Hz republishes the active command with a decrementing
// validity_s so downstream L4 always has a fresh expiry timestamp.

#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/world_state.hpp"

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_override_generator.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_solver.hpp"
#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcNode : public rclcpp::Node {
 public:
  struct Config {
    BcMpcBranchFormulation::Config branch;
    // [TBD-HAZID] cpa_safe_m: calibrate per ODD domain (coastal vs. open sea).
    double cpa_safe_m{1852.0};
  };

  explicit BcMpcNode(const Config& cfg = Config{});

 private:
  BcMpcBranchFormulation formulation_;
  BcMpcSolver            solver_;
  BcMpcOverrideGenerator override_gen_;

  l3_msgs::msg::WorldState::SharedPtr      world_state_;
  l3_msgs::msg::AvoidancePlan::SharedPtr   last_mid_mpc_plan_;

  bool   is_bc_active_{false};
  double remaining_validity_s_{0.0};
  l3_msgs::msg::ReactiveOverrideCmd active_cmd_;

  Config cfg_;

  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr     sub_world_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr  sub_mid_plan_;
  rclcpp::Publisher<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr pub_override_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr         pub_asdr_;
  rclcpp::TimerBase::SharedPtr validity_timer_;

  void on_world_state_(l3_msgs::msg::WorldState::SharedPtr msg);
  void on_mid_mpc_plan_(l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_validity_tick_();
  [[nodiscard]] BcMpcInput assemble_input_() const;
  void publish_override_(const BcMpcSolution& sol);
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_NODE_HPP_
