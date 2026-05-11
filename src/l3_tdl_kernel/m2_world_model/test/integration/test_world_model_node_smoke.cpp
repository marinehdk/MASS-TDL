#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include "m2_world_model/world_model_node.hpp"

TEST(WorldModelNodeSmoke, CreateNode) {
  rclcpp::init(0, nullptr);
  ASSERT_NO_THROW({
    auto node = std::make_shared<mass_l3::m2::WorldModelNode>();
  });
  rclcpp::shutdown();
}

TEST(WorldModelNodeSmoke, CreateAndDestroy) {
  rclcpp::init(0, nullptr);
  mass_l3::m2::WorldModelNode* raw = nullptr;
  {
    auto node = std::make_shared<mass_l3::m2::WorldModelNode>();
    raw = node.get();
    ASSERT_NE(raw, nullptr);
  }
  rclcpp::shutdown();
}
