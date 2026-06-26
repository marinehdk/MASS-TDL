// test/test_cross_run_reset.cpp
// Verifies the M6 cross-run reset is wired and callable. M6 already self-heals
// via sim-time rewind; this confirms the new scenario_loaded trigger path
// (reset_cross_run_state, public) is callable and idempotent, and shares the
// same latch-clearing as the rewind path. The no-restart behavioral probe
// (Task 9) verifies the end-to-end reset correctness.
#include <gtest/gtest.h>

#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

namespace mass_l3::m6_colregs {

class M6CrossRunResetTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }
};

TEST_F(M6CrossRunResetTest, ResetIsCallableAndIdempotent) {
  // Node construction mirrors test_node_lifecycle (needs config/ via the
  // WORKING_DIRECTORY set in CMakeLists for this test).
  auto node = std::make_shared<ColregsReasonerNode>();
  // reset_cross_run_state is public. Idempotent + safe at any time.
  EXPECT_NO_THROW(node->reset_cross_run_state());
  EXPECT_NO_THROW(node->reset_cross_run_state());
}

TEST_F(M6CrossRunResetTest, ClearsEncounterStateMachines) {
  // Regression guard: encounter_fsms_ (per-(target,rule) EncounterStateMachine)
  // is populated during run_reasoning and used for onset classification. If it
  // is not cleared on cross-run reset, a prior scenario's FSM state biases the
  // next scenario's onset (observed: M6 onset 7s earlier and conflict 4.6x
  // longer in batch run 2 vs the clean single run of rule14-ho). Seed the map
  // as run_reasoning would, then verify reset clears it.
  auto node = std::make_shared<ColregsReasonerNode>();
  EXPECT_EQ(node->test_encounter_fsm_count(), 0u);

  // Seed two FSMs as a prior run of rule14 (head-on) + rule15 (crossing) would.
  node->test_seed_encounter_fsm(/*mmsi=*/123456, /*rule_id=*/14);
  node->test_seed_encounter_fsm(/*mmsi=*/123456, /*rule_id=*/15);
  EXPECT_EQ(node->test_encounter_fsm_count(), 2u);

  node->reset_cross_run_state();
  EXPECT_EQ(node->test_encounter_fsm_count(), 0u);
}

}  // namespace mass_l3::m6_colregs
