#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

#include <spdlog/spdlog.h>

#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"

namespace mass_l3::m4 {

BehaviorArbiterNode::BehaviorArbiterNode(const rclcpp::NodeOptions& options)
    : rclcpp::Node("m4_behavior_arbiter_node", options) {
  // Declare parameters
  const auto behavior_defs_path = declare_parameter<std::string>(
      "behavior_definitions_path",
      "install/m4_behavior_arbiter/share/m4_behavior_arbiter/config/behavior_definitions.yaml");

  // Load behavior dictionary
  ErrorCode err = dict_.load(behavior_defs_path);
  if (err != ErrorCode::Ok) {
    RCLCPP_ERROR(get_logger(), "[M4] Failed to load behavior dictionary: %d",
                 static_cast<int>(err));
    throw std::runtime_error("Failed to load behavior dictionary");
  }

  // Create IvP domains (Phase E1: stub parameters)
  IvPHeadingDomain h_domain{1.0};  // 1 degree resolution
  IvPSpeedDomain s_domain{0.0, 20.0, 0.5};  // [0, 20] kn, 0.5 kn resolution

  // Create arbiter with 100ms timeout
  arbiter_ = std::make_unique<BehaviorArbiter>(
      dict_, h_domain, s_domain, std::chrono::milliseconds{100});

  // Create subscriptions with QoS=10
  rclcpp::QoS qos{10};

  sub_odd_ = create_subscription<l3_msgs::msg::ODDState>(
      "/m1/odd_state", qos,
      [this](l3_msgs::msg::ODDState::ConstSharedPtr msg) { on_odd_state(msg); });

  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/m2/world_state", qos,
      [this](l3_msgs::msg::WorldState::ConstSharedPtr msg) { on_world_state(msg); });

  sub_mission_ = create_subscription<l3_msgs::msg::MissionGoal>(
      "/m3/mission_goal", qos,
      [this](l3_msgs::msg::MissionGoal::ConstSharedPtr msg) { on_mission_goal(msg); });

  sub_mode_ = create_subscription<l3_msgs::msg::ModeCmd>(
      "/m1/mode_cmd", qos,
      [this](l3_msgs::msg::ModeCmd::ConstSharedPtr msg) { on_mode_cmd(msg); });

  sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", qos,
      [this](l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg) { on_colregs(msg); });

  // Create publisher
  pub_ = create_publisher<l3_msgs::msg::BehaviorPlan>("/m4/behavior_plan", qos);

  // Create 2 Hz timer (500 ms)
  timer_ = create_wall_timer(std::chrono::milliseconds{500},
                             [this]() { on_timer(); });

  RCLCPP_INFO(get_logger(), "[M4] BehaviorArbiterNode initialized");
}

void BehaviorArbiterNode::on_odd_state(l3_msgs::msg::ODDState::ConstSharedPtr msg) {
  inputs_.odd_state = *msg;
  inputs_.m1_fresh = true;
}

void BehaviorArbiterNode::on_world_state(l3_msgs::msg::WorldState::ConstSharedPtr msg) {
  inputs_.world_state = *msg;
  inputs_.m2_fresh = true;
}

void BehaviorArbiterNode::on_mission_goal(l3_msgs::msg::MissionGoal::ConstSharedPtr msg) {
  inputs_.mission_goal = *msg;
  inputs_.m3_fresh = true;
}

void BehaviorArbiterNode::on_mode_cmd(l3_msgs::msg::ModeCmd::ConstSharedPtr msg) {
  inputs_.mode_cmd = *msg;
  inputs_.m1_fresh = true;
}

void BehaviorArbiterNode::on_colregs(l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg) {
  inputs_.colregs_constraint = *msg;
  inputs_.m6_fresh = true;
}

void BehaviorArbiterNode::on_timer() {
  // Run one arbitration cycle
  auto result = arbiter_->run(inputs_);

  // Create and publish BehaviorPlan
  l3_msgs::msg::BehaviorPlan plan_msg;
  plan_msg.stamp = now();
  plan_msg.behavior = static_cast<uint8_t>(result.primary);
  plan_msg.heading_min_deg = static_cast<float>(result.heading_min_deg);
  plan_msg.heading_max_deg = static_cast<float>(result.heading_max_deg);
  plan_msg.speed_min_kn = static_cast<float>(result.speed_min_kn);
  plan_msg.speed_max_kn = static_cast<float>(result.speed_max_kn);
  plan_msg.confidence = static_cast<float>(result.confidence);
  plan_msg.rationale = result.rationale;

  pub_->publish(plan_msg);

  spdlog::debug("[M4] BehaviorArbiterNode: cycle primary={} confidence={:.2f}",
                static_cast<int>(result.primary), result.confidence);

  // Reset freshness flags for next cycle
  inputs_.m1_fresh = false;
  inputs_.m2_fresh = false;
  inputs_.m3_fresh = false;
  inputs_.m6_fresh = false;
}

}  // namespace mass_l3::m4
