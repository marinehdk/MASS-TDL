#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

TEST(ColregsReasonerNodeSmoke, CreateNode) {
  rclcpp::init(0, nullptr);
  ASSERT_NO_THROW({
    auto node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();
  });
  rclcpp::shutdown();
}
