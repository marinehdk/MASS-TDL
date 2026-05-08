// SPDX-License-Identifier: Proprietary
// Stopping distance numerical error test.
// Compares dt=0.02s RK4 against dt=0.001s reference solution.
// DoD gate: error <= 10%.
#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace {
double simulate_stopping(double dt) {
  fcb_sim::MmgParams p;
  fcb_sim::FcbState s;
  s.u   = 9.26;
  s.psi = 1.5708;
  const double x0 = s.x, y0 = s.y;
  const int max_steps = static_cast<int>(600.0 / dt);
  for (int i = 0; i < max_steps; ++i) {
    s = fcb_sim::rk4_step(s, 0.0, 0.0, p, dt);
    if (!std::isfinite(s.u)) break;
    if (s.u < 0.1) break;
  }
  return std::hypot(s.x - x0, s.y - y0);
}
}  // namespace

TEST(StoppingError, DtErrorWithin10Percent) {
  const double d_ref = simulate_stopping(0.001);
  const double d_sim = simulate_stopping(0.02);

  ASSERT_GT(d_ref, 1.0) << "Reference stopping distance suspiciously small";
  ASSERT_TRUE(std::isfinite(d_ref)) << "Reference solution diverged";
  ASSERT_TRUE(std::isfinite(d_sim)) << "Simulation diverged at dt=0.02s";

  const double err_pct = std::abs(d_sim - d_ref) / d_ref * 100.0;
  std::cout << "[D1.3a-audit] d_ref=" << d_ref << " m  d_sim=" << d_sim
            << " m  err=" << err_pct << "%" << std::endl;

  EXPECT_LE(err_pct, 10.0)
      << "Stopping distance error " << err_pct << "% exceeds 10% gate.\n"
      << "d_ref=" << d_ref << "m (dt=0.001s)  d_sim=" << d_sim << "m (dt=0.02s)\n"
      << "Per spec R3.1: if still failing, relax to 15% with HAZID-UNVERIFIED note.";
}
