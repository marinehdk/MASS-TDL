// SPDX-License-Identifier: Proprietary
#ifndef FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_
#define FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_

#include <memory>
#include <mutex>
#include <string>

#include <rclcpp/rclcpp.hpp>
#include <pluginlib/class_loader.hpp>

#include "ship_sim_interfaces/ship_motion_simulator.hpp"
#include "fcb_simulator/types.hpp"   // SimConfig only
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"

namespace fcb_sim {

class FcbSimulatorNode : public rclcpp::Node {
 public:
  FcbSimulatorNode();

 private:
  void load_params();
  void on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg);
  void on_reactive_override(const l3_msgs::msg::ReactiveOverrideCmd::SharedPtr msg);

  void step_dynamics();
  void publish_own_ship_state();
  void publish_tracked_targets();
  void compute_control(double& delta_rad, double& n_rps);

  std::unique_ptr<pluginlib::ClassLoader<ship_sim::ShipMotionSimulator>> class_loader_;
  std::shared_ptr<ship_sim::ShipMotionSimulator> plugin_;

  SimConfig cfg_;
  ship_sim::ShipState state_{};

  double psi_target_rad_{0.0};
  double u_target_mps_{0.0};

  bool override_active_{false};
  rclcpp::Time override_expires_;
  double override_psi_rad_{0.0};
  double override_u_mps_{0.0};

  mutable std::mutex cmd_mutex_;

  rclcpp::Subscription<l3_msgs::msg::AvoidancePlan>::SharedPtr sub_plan_;
  rclcpp::Subscription<l3_msgs::msg::ReactiveOverrideCmd>::SharedPtr sub_override_;
  rclcpp::Publisher<l3_external_msgs::msg::FilteredOwnShipState>::SharedPtr pub_state_;
  rclcpp::Publisher<l3_external_msgs::msg::TrackedTargetArray>::SharedPtr pub_targets_;

  rclcpp::TimerBase::SharedPtr timer_dynamics_;
  rclcpp::TimerBase::SharedPtr timer_targets_;
};

}  // namespace fcb_sim

#endif  // FCB_SIMULATOR_FCB_SIMULATOR_NODE_HPP_
