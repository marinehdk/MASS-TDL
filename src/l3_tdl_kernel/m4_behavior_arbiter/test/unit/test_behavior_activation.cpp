#include <gtest/gtest.h>

#include "m4_behavior_arbiter/behavior_activation.hpp"

namespace mass_l3::m4::test {

// Default activation condition has false booleans
TEST(BehaviorActivationTest, DefaultConstruction) {
  BehaviorActivationCondition cond;
  EXPECT_DOUBLE_EQ(cond.min_conformance_score, 0.0);
  EXPECT_FALSE(cond.requires_colregs_violation);
  EXPECT_FALSE(cond.requires_dp_mode);
  EXPECT_FALSE(cond.requires_berthing);
  EXPECT_FALSE(cond.is_mrc);
}

}  // namespace mass_l3::m4::test
