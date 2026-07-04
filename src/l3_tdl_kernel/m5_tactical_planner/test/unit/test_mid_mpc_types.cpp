#include <gtest/gtest.h>

#include "m5_tactical_planner/common/types.hpp"

// Spec v2.2 §4.7 (D2): MidMpcInput.speed_gap_infeasible — dispatch-only flag,
// set when own_u/planned_u gap exceeds N·decel_max·dt. Does NOT alter NLP
// constraint expressions; feeds §13.1 BC-MPC take-over condition.

TEST(MidMpcInputV22Test, SpeedGapInfeasibleFlagDefaultsFalse) {
  mass_l3::m5::MidMpcInput input;
  EXPECT_FALSE(input.speed_gap_infeasible);
}

// Spec v2.2 §4.7 (D2): compute_speed_gap_infeasible() — free function so the
// gap rule is unit-testable without exercising the private assemble_input_.
// Threshold = decel_max·dt·N; flag fires when |own_u - planned_u| exceeds it.

TEST(SpeedContractV22, GapWithinHorizonNoFlag) {
  // decel 0.20 m/s² × dt 5.0 s × N 18 = 18.0 m/s reachable delta.
  // gap = |7.58 - 3.087| = 4.493 < 18.0 → no flag.
  EXPECT_FALSE(mass_l3::m5::compute_speed_gap_infeasible(7.58, 3.087, 0.20, 18, 5.0));
}

TEST(SpeedContractV22, GapExceedsHorizonFlag) {
  // gap = |20.0 - 1.0| = 19.0 > 18.0 → flag.
  EXPECT_TRUE(mass_l3::m5::compute_speed_gap_infeasible(20.0, 1.0, 0.20, 18, 5.0));
}
