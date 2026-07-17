#include "m5_tactical_planner/shared/huber_cost.hpp"
#include <gtest/gtest.h>
#include <cmath>

using mass_l3::m5::shared::huber_cost;

TEST(HuberCost, QuadraticRegion_NearZero) {
  // |l| < delta_h: 0.5*l^2
  EXPECT_NEAR(huber_cost(0.5, 1.0), 0.125, 1e-9);   // 0.5*0.25
  EXPECT_NEAR(huber_cost(-0.5, 1.0), 0.125, 1e-9);
  EXPECT_NEAR(huber_cost(0.0, 1.0), 0.0, 1e-9);
}

TEST(HuberCost, LinearRegion_Far) {
  // |l| > delta_h: delta_h*(|l| - delta_h/2)
  EXPECT_NEAR(huber_cost(2.0, 1.0), 1.5, 1e-9);   // 1*(2-0.5)
  EXPECT_NEAR(huber_cost(-2.0, 1.0), 1.5, 1e-9);
}

TEST(HuberCost, ContinuousAtDelta) {
  // |l|=delta_h: both formulas agree
  const double d = 1.0;
  EXPECT_NEAR(huber_cost(d, d), 0.5*d*d, 1e-9);          // quadratic: 0.5
  EXPECT_NEAR(huber_cost(d, d), d*(d-0.5*d), 1e-9);      // linear: 0.5
}

TEST(HuberCost, DerivativeContinuous_Numerical) {
  // First derivative continuous at delta_h (numerical verification):
  // huber'(delta_h+eps) ~= huber'(delta_h-eps), and slope == delta_h in linear region.
  const double d = 1.0, eps = 1e-6;
  const double dp_pos = (huber_cost(d+eps, d) - huber_cost(d, d)) / eps;
  const double dp_neg = (huber_cost(d, d) - huber_cost(d-eps, d)) / eps;
  EXPECT_NEAR(dp_pos, dp_neg, 1e-4);   // C1 continuous at delta_h
  EXPECT_NEAR(dp_pos, d, 1e-4);        // linear-region slope == delta_h
}
