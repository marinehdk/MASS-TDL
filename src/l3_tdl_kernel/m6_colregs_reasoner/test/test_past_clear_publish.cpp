#include <gtest/gtest.h>

#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"
#include "m6_colregs_reasoner/encounter_state_machine.hpp"

namespace mass_l3::m6_colregs {
namespace {

TEST(PastClearPublish, ReleasesWhenFsmEntersRelease) {
  l3_msgs::msg::COLREGsConstraint msg;

  ColregsReasonerNode::test_populate_colregs_semantics(
      msg, EncounterState::RELEASE, /*latch_released=*/true,
      /*release_predicted=*/true);

  EXPECT_TRUE(msg.past_clear);
  EXPECT_EQ(msg.encounter_state,
            l3_msgs::msg::COLREGsConstraint::ENCOUNTER_RELEASE);
  EXPECT_TRUE(msg.release_predicted);
}

TEST(PastClearPublish, FalseWhenActive) {
  l3_msgs::msg::COLREGsConstraint msg;

  ColregsReasonerNode::test_populate_colregs_semantics(
      msg, EncounterState::ACTIVE, /*latch_released=*/false,
      /*release_predicted=*/false);

  EXPECT_FALSE(msg.past_clear);
  EXPECT_EQ(msg.encounter_state,
            l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ACTIVE);
  EXPECT_FALSE(msg.release_predicted);
}

TEST(PastClearPublish, PredictionOrResolvedBookkeepingDoesNotForceReleaseWhileActive) {
  l3_msgs::msg::COLREGsConstraint msg;

  ColregsReasonerNode::test_populate_colregs_publish_semantics(
      msg, EncounterState::ACTIVE,
      /*actual_latch_released=*/false,
      /*release_predicted=*/true,
      /*resolved_bookkeeping=*/true);

  EXPECT_FALSE(msg.past_clear);
  EXPECT_EQ(msg.encounter_state,
            l3_msgs::msg::COLREGsConstraint::ENCOUNTER_ACTIVE);
  EXPECT_TRUE(msg.release_predicted);
}

}  // namespace
}  // namespace mass_l3::m6_colregs
