#include <vector>

#include <gtest/gtest.h>

#include <l3_msgs/msg/odd_state.hpp>

#include "m4_behavior_arbiter/behavior_priority.hpp"

namespace mass_l3::m4 {

// P1: MrcDrift in active set overrides all other behaviors
TEST(BehaviorPriorityTest, MrcDriftOverridesAll) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{
      BehaviorType::Transit, BehaviorType::ColregAvoid, BehaviorType::MrcDrift};
  const IvPSolution sol{};
  const ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcDrift);
}

// P2: MrcAnchor in active set overrides mission behaviors
TEST(BehaviorPriorityTest, MrcAnchorOverridesMission) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::Transit, BehaviorType::MrcAnchor};
  const IvPSolution sol{};
  const ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcAnchor);
}

// P3: ColregAvoid overrides Transit (no MRC present)
TEST(BehaviorPriorityTest, ColregAvoidOverridesTransit) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::Transit, BehaviorType::ColregAvoid};
  const IvPSolution sol{};
  const ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::ColregAvoid);
}

// P4: Transit alone → Transit
TEST(BehaviorPriorityTest, TransitWhenAlone) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::Transit};
  const IvPSolution sol{};
  const ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::Transit);
}

// P5: HEALTH_CRITICAL forces MrcDrift even when active set has no MRC
TEST(BehaviorPriorityTest, CriticalHealthForcesMrcDrift) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::Transit};
  const IvPSolution sol{};
  ArbitrationInputs in{};
  in.odd_state.health = static_cast<decltype(in.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_CRITICAL);
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcDrift);
}

// P6: has_mrc returns true when MRC type present
TEST(BehaviorPriorityTest, HasMrcTrueWhenPresent) {
  const std::vector<BehaviorType> with_mrc{BehaviorType::Transit, BehaviorType::MrcHeaveTo};
  EXPECT_TRUE(BehaviorPriority::has_mrc(with_mrc));
}

// P7: has_mrc returns false when no MRC type present
TEST(BehaviorPriorityTest, HasMrcFalseWhenAbsent) {
  const std::vector<BehaviorType> no_mrc{BehaviorType::Transit, BehaviorType::ColregAvoid};
  EXPECT_FALSE(BehaviorPriority::has_mrc(no_mrc));
}

// P8: has_mrc returns false for empty set
TEST(BehaviorPriorityTest, HasMrcFalseForEmpty) {
  EXPECT_FALSE(BehaviorPriority::has_mrc({}));
}

// P9: DpHold returned when alone (port scenario)
TEST(BehaviorPriorityTest, DpHoldReturnedWhenAlone) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::DpHold};
  const IvPSolution sol{};
  const ArbitrationInputs in{};
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::DpHold);
}

// P10: CRITICAL health overrides MRC in active set too (MrcAnchor → MrcDrift)
TEST(BehaviorPriorityTest, CriticalHealthAlwaysReturnsMrcDrift) {
  const BehaviorPriority pri;
  const std::vector<BehaviorType> active{BehaviorType::MrcAnchor};
  const IvPSolution sol{};
  ArbitrationInputs in{};
  in.odd_state.health = static_cast<decltype(in.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_CRITICAL);
  // CRITICAL rule fires before MRC rule → always MrcDrift
  EXPECT_EQ(pri.select_primary(active, sol, in), BehaviorType::MrcDrift);
}

}  // namespace mass_l3::m4
