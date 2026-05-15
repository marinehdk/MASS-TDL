#include <gtest/gtest.h>

#include <memory>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

namespace mass_l3::m4::test {

// Node can be constructed
TEST(BehaviorArbiterNodeTest, Construct) {
  auto node = std::make_shared<BehaviorArbiterNode>();
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->get_name(), "behavior_arbiter");
}

}  // namespace mass_l3::m4::test
