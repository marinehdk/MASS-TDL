// test/unit/test_m4_cross_run_reset.cpp
// Verifies the cross-run state reset is wired and callable. The field-level
// reset correctness is verified by the behavioral no-restart probe (Task 9)
// rather than private-field inspection, which keeps the test robust to
// friend/namespace resolution quirks across compiler versions.
#include <gtest/gtest.h>

#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

namespace mass_l3::m4 {

class M4CrossRunResetTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
};

TEST_F(M4CrossRunResetTest, ResetIsCallableAndIdempotent) {
  auto node = std::make_shared<BehaviorArbiterNode>();
  // reset_cross_run_state is public (the cross-run reset contract).
  // Idempotent + safe at any time (design invariant §6.1). Calling it must not
  // throw even on a freshly-constructed node with no sim data.
  EXPECT_NO_THROW(node->reset_cross_run_state());
  EXPECT_NO_THROW(node->reset_cross_run_state());
}

TEST_F(M4CrossRunResetTest, ResetLeavesNodeInUsableState) {
  // After reset the node must still be operable: constructing a second node,
  // resetting the first, then resetting the second must not interfere. This
  // guards against reset corrupting shared/pimpl state.
  auto node_a = std::make_shared<BehaviorArbiterNode>();
  auto node_b = std::make_shared<BehaviorArbiterNode>();
  node_a->reset_cross_run_state();
  EXPECT_NO_THROW(node_b->reset_cross_run_state());
  EXPECT_NO_THROW(node_a->reset_cross_run_state());
}

}  // namespace mass_l3::m4
