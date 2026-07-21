#ifndef MASS_L3_M5_BC_MPC_NODE_HPP_
#define MASS_L3_M5_BC_MPC_NODE_HPP_

// M5 Tactical Planner — BC-MPC ROS2 Node
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Event-driven (triggered by WorldState at 4 Hz).
// Publishes ReactiveOverrideCmd when BC-MPC detects imminent collision.
// validity_timer_ at 10 Hz republishes the active command with a decrementing
// validity_s so downstream L4 always has a fresh expiry timestamp.

#include <atomic>
#include <cstdint>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/bc_mpc_health.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "std_msgs/msg/u_int64.hpp"

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

  explicit BcMpcNode(const Config& cfg);

 private:
  BcMpcBranchFormulation formulation_;
  BcMpcSolver            solver_;
  BcMpcOverrideGenerator override_gen_;

  l3_msgs::msg::WorldState::SharedPtr      world_state_;
  l3_msgs::msg::AvoidancePlan::SharedPtr   last_mid_mpc_plan_;

  // v2.2 §13.1: BC-MPC Phase E2 wiring. Atomic cache of Mid-MPC's
  // consecutive_failures (subscribed from /l3/m5/mid_mpc/consecutive_failures).
  // Initialised to 0 → no take-over until Mid-MPC reports failures (replaces the
  // Phase E1 stub at the old assemble_input_ site).
  std::atomic<std::uint64_t> mid_mpc_consecutive_failures_atomic_{0U};

  bool   is_bc_active_{false};
  double remaining_validity_s_{0.0};
  l3_msgs::msg::ReactiveOverrideCmd active_cmd_;

  // P6 Condition A counter: consecutive Override ticks with no CPA improvement
  std::uint32_t consecutive_override_no_improve_{0U};
  double last_worst_case_cpa_m_{1.0e9};
  double last_input_predicted_cpa_{1.0e9};
  static constexpr double kCpaImproveEpsilon_m = 1.0;

  Config cfg_;

  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr     sub_world_;
  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr  sub_mid_plan_;
  rclcpp::Subscription<std_msgs::msg::UInt64>::SharedPtr        sub_mid_mpc_failures_;
  rclcpp::Publisher<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr pub_override_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr         pub_asdr_;
  // P6: BC-MPC health metrics published at WorldState frequency
  rclcpp::Publisher<l3_msgs::msg::BcMpcHealth>::SharedPtr        pub_health_;
  rclcpp::TimerBase::SharedPtr validity_timer_;

  void on_world_state_(l3_msgs::msg::WorldState::SharedPtr msg);
  void on_mid_mpc_plan_(l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_validity_tick_();
  [[nodiscard]] BcMpcInput assemble_input_();
  void publish_override_(const BcMpcSolution& sol);
  // P6: publish health metrics to /l3/m5/bc_mpc/health
  void publish_health_(const BcMpcSolution& sol);
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_NODE_HPP_
