// INT-009: M1+M6→M4 chain — verify M4 publishes BehaviorPlan when ODD+COLREGs present
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/rule_active.hpp"

TEST(INT009_M4, PublishesBehaviorPlanWhenInputsPresent) {
  rclcpp::init(0, nullptr);
  auto node = rclcpp::Node::make_shared("test_int_009");
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);

  bool received = false;
  auto sub = node->create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", 4,
      [&](l3_msgs::msg::BehaviorPlan::ConstSharedPtr) { received = true; });

  auto pub_odd = node->create_publisher<l3_msgs::msg::ODDState>("/l3/m1/odd_state", 4);
  auto pub_world = node->create_publisher<l3_msgs::msg::WorldState>("/l3/m2/world_state", 4);
  auto pub_colregs = node->create_publisher<l3_msgs::msg::COLREGsConstraint>("/l3/m6/colregs_constraint", 4);

  l3_msgs::msg::ODDState odd;
  odd.schema_version = "v1.1.2";
  odd.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN;
  odd.auto_level = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
  odd.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  pub_odd->publish(odd);

  l3_msgs::msg::WorldState world;
  world.schema_version = "v1.1.2";
  pub_world->publish(world);

  l3_msgs::msg::COLREGsConstraint colregs;
  colregs.schema_version = "v1.1.2";
  colregs.conflict_detected = true;
  l3_msgs::msg::RuleActive rule;
  rule.rule_id = 14;
  colregs.active_rules.push_back(rule);
  pub_colregs->publish(colregs);

  // Spin to allow M4 to receive and process
  for (int i = 0; i < 50; ++i) {
    exec.spin_some(std::chrono::milliseconds(100));
    if (received) break;
  }
  EXPECT_TRUE(received) << "M4 should publish BehaviorPlan within 5s";
  rclcpp::shutdown();
}
