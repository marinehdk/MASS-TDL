// SPDX-License-Identifier: Proprietary
// 1-hour simulation stability test: 180,000 steps at dt=0.02s, steady cruise.
// Verifies no NaN/Inf and surge stays in physical bounds.
#include <gtest/gtest.h>
#include <cmath>
#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

TEST(Stability1h, NoNaNOrInfOver180kSteps) {
  fcb_sim::MmgParams p;
  fcb_sim::FcbState s;
  s.u   = 9.26;
  s.psi = 1.5708;

  const double dt      = 0.02;
  const int    steps   = 180000;
  const double u0      = s.u;
  const double n_cruise = 5.0;
  const double delta   = 0.0;

  for (int i = 0; i < steps; ++i) {
    s = fcb_sim::rk4_step(s, delta, n_cruise, p, dt);

    ASSERT_TRUE(std::isfinite(s.u) && std::isfinite(s.v) &&
                std::isfinite(s.r) && std::isfinite(s.phi) &&
                std::isfinite(s.x) && std::isfinite(s.y))
        << "Non-finite state at step " << i << " (t=" << i * dt << "s)";

    ASSERT_GE(s.u, 0.0) << "Surge went negative at step " << i;
    ASSERT_LE(s.u, u0 * 3.0) << "Surge exceeded 3x initial at step " << i;
  }
}
