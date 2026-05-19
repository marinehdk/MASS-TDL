#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include <l3_msgs/msg/behavior_plan.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>
#include <l3_msgs/msg/mission_goal.hpp>
#include <l3_msgs/msg/mode_cmd.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_msgs/msg/world_state.hpp>

#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_arbiter.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

/**
 * @brief ROS2 node wrapper for BehaviorArbiter.
 *
 * Subscribes to M1/M2/M3/M6 inputs; publishes Behavior_PlanMsg to M5 at 2 Hz.
 * Freshness is tracked per-source; stale inputs cause confidence reduction.
 */
class BehaviorArbiterNode : public rclcpp::Node {
 public:
  explicit BehaviorArbiterNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());
  ~BehaviorArbiterNode() override = default;
  BehaviorArbiterNode(const BehaviorArbiterNode&) = delete;
  BehaviorArbiterNode& operator=(const BehaviorArbiterNode&) = delete;
  BehaviorArbiterNode(BehaviorArbiterNode&&) = delete;
  BehaviorArbiterNode& operator=(BehaviorArbiterNode&&) = delete;

 private:
  void on_odd_state(l3_msgs::msg::ODDState::ConstSharedPtr msg);
  void on_world_state(l3_msgs::msg::WorldState::ConstSharedPtr msg);
  void on_mission_goal(l3_msgs::msg::MissionGoal::ConstSharedPtr msg);
  void on_mode_cmd(l3_msgs::msg::ModeCmd::ConstSharedPtr msg);
  void on_colregs(l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg);
  void on_timer();

  BehaviorDictionary dict_;
  std::unique_ptr<BehaviorArbiter> arbiter_;

  ArbitrationInputs inputs_{};

  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr sub_odd_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr sub_world_;
  rclcpp::Subscription<l3_msgs::msg::MissionGoal>::SharedPtr sub_mission_;
  rclcpp::Subscription<l3_msgs::msg::ModeCmd>::SharedPtr sub_mode_;
  rclcpp::Subscription<l3_msgs::msg::COLREGsConstraint>::SharedPtr sub_colregs_;

  rclcpp::Publisher<l3_msgs::msg::BehaviorPlan>::SharedPtr pub_;
  rclcpp::TimerBase::SharedPtr timer_;
};

}  // namespace mass_l3::m4
