#include <gtest/gtest.h>

#include <vector>

#include "m4_behavior_arbiter/behavior_priority.hpp"

namespace mass_l3::m4::test {

TEST(BehaviorPriorityTest, MrcDriftOverridesAllOtherBehaviors) {
  BehaviorPriority priority;
  std::vector<BehaviorType> active = {
      BehaviorType::TRANSIT,
      BehaviorType::COLREG_AVOID,
      BehaviorType::MRC_DRIFT,
  };
  IvPSolution ivp{};
  ArbitrationInputs inputs;
  auto primary = priority.select_primary(active, ivp, inputs);
  EXPECT_EQ(primary, BehaviorType::MRC_DRIFT);
}

TEST(BehaviorPriorityTest, HasMrcDetectsMrcInSet) {
  std::vector<BehaviorType> active = {
      BehaviorType::TRANSIT,
      BehaviorType::MRC_DRIFT,
  };
  EXPECT_TRUE(BehaviorPriority::has_mrc(active));
}

TEST(BehaviorPriorityTest, HasMrcReturnsFalseWhenNoMrc) {
  std::vector<BehaviorType> active = {
      BehaviorType::TRANSIT,
      BehaviorType::COLREG_AVOID,
  };
  EXPECT_FALSE(BehaviorPriority::has_mrc(active));
}

}  // namespace mass_l3::m4::test
