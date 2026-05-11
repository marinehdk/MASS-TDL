// SPDX-License-Identifier: Proprietary
// Straight-line stability: zero rudder, constant prop → surge converges to a
// positive equilibrium and sway stays near zero.
#include <gtest/gtest.h>

#include <cmath>

#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

TEST(Rk4Stability, StraightLineConverges) {
  fcb_sim::MmgParams p;
  fcb_sim::FcbState s;
  s.u = 5.0;                        // start below equilibrium

  const double delta = 0.0;
  const double n = 8.0;             // moderate rps
  const double dt = 0.05;
  const int total_steps = static_cast<int>(30.0 / dt);

  for (int i = 0; i < total_steps; ++i) {
    s = fcb_sim::rk4_step(s, delta, n, p, dt);
    ASSERT_TRUE(std::isfinite(s.u) && std::isfinite(s.v) && std::isfinite(s.r))
        << "non-finite at step " << i;
  }

  EXPECT_GT(s.u, 1.0) << "surge did not converge to positive equilibrium";
  EXPECT_LT(std::abs(s.v), 0.5) << "sway departed from straight-line";
  EXPECT_LT(std::abs(s.r), 0.05) << "yaw rate departed from zero";
}
