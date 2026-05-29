#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

class HmiNodeTest : public ::testing::Test {
 protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }
};

// Smoke test 1: node constructs without crash
TEST_F(HmiNodeTest, NodeConstructsSuccessfully) {
  rclcpp::NodeOptions opts;
  EXPECT_NO_THROW({
    auto node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(opts);
    (void)node;
  });
}

// Smoke test 2: node has correct name
TEST_F(HmiNodeTest, NodeHasCorrectName) {
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(opts);
  EXPECT_EQ(node->get_name(), std::string("m8_hmi_transparency_bridge"));
}

// Smoke test 3: publishers created on expected topics
TEST_F(HmiNodeTest, PublishersCreated) {
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(opts);
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    exec.spin_some(std::chrono::milliseconds(10));
    if (node->count_publishers("/l3/m8/ui_state") >= 1U &&
        node->count_publishers("/l3/asdr/record") >= 1U &&
        node->count_publishers("/l3/m8/tor_request") >= 1U) {
      break;
    }
  }
  EXPECT_GE(node->count_publishers("/l3/m8/ui_state"), 1U);
  EXPECT_GE(node->count_publishers("/l3/asdr/record"), 1U);
  EXPECT_GE(node->count_publishers("/l3/m8/tor_request"), 1U);
}

TEST_F(HmiNodeTest, SubscribesToM7Heartbeat) {
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(opts);
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (std::chrono::steady_clock::now() < deadline) {
    exec.spin_some(std::chrono::milliseconds(10));
    if (node->count_subscribers("/l3/m7/heartbeat") >= 1U) {
      break;
    }
  }
  EXPECT_GE(node->count_subscribers("/l3/m7/heartbeat"), 1U);
}

