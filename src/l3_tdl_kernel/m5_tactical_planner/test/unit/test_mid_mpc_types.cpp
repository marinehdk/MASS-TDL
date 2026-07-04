#include <gtest/gtest.h>

#include "m5_tactical_planner/common/types.hpp"

// Spec v2.2 §4.7 (D2): MidMpcInput.speed_gap_infeasible — dispatch-only flag,
// set when own_u/planned_u gap exceeds N·decel_max·dt. Does NOT alter NLP
// constraint expressions; feeds §13.1 BC-MPC take-over condition.

TEST(MidMpcInputV22Test, SpeedGapInfeasibleFlagDefaultsFalse) {
  mass_l3::m5::MidMpcInput input;
  EXPECT_FALSE(input.speed_gap_infeasible);
}
