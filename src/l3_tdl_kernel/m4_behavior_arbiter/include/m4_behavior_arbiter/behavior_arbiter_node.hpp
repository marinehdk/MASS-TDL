#pragma once

#include <memory>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/asdr_record.hpp"

#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/behavior_priority.hpp"
#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

class BehaviorArbiterNode : public rclcpp::Node {
public:
  explicit BehaviorArbiterNode(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

private:
  void on_odd_state(const ODDStateMsg::SharedPtr msg);
  void on_world_state(const WorldStateMsg::SharedPtr msg);
  void on_mode_cmd(const ModeCmdMsg::SharedPtr msg);
  void on_mission_goal(const MissionGoalMsg::SharedPtr msg);
  void on_colregs_constraint(const COLREGsConstraintMsg::SharedPtr msg);
  void arbitration_timer_callback();

  ODDStateMsg::SharedPtr            latest_odd_;
  WorldStateMsg::SharedPtr          latest_world_;
  ModeCmdMsg::SharedPtr             latest_mode_;
  MissionGoalMsg::SharedPtr         latest_mission_;
  COLREGsConstraintMsg::SharedPtr   latest_colregs_;

  bool odd_received_{false};
  bool world_received_{false};
  bool mode_received_{false};
  bool mission_received_{false};
  bool colregs_received_{false};

  rclcpp::Subscription<ODDStateMsg>::SharedPtr          sub_odd_;
  rclcpp::Subscription<WorldStateMsg>::SharedPtr        sub_world_;
  rclcpp::Subscription<ModeCmdMsg>::SharedPtr           sub_mode_;
  rclcpp::Subscription<MissionGoalMsg>::SharedPtr       sub_mission_;
  rclcpp::Subscription<COLREGsConstraintMsg>::SharedPtr sub_colregs_;

  rclcpp::Publisher<BehaviorPlanMsg>::SharedPtr             pub_plan_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr    pub_asdr_;

  rclcpp::TimerBase::SharedPtr timer_;

  BehaviorDictionary  dictionary_;
  IvPHeadingDomain    heading_domain_;
  IvPSpeedDomain      speed_domain_;
  WeightedSumCombination combiner_;
};

}  // namespace mass_l3::m4
