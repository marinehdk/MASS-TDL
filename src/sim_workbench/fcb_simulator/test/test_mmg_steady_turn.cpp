// SPDX-License-Identifier: Proprietary
// Steady turning test: 35° rudder, 5 rps prop → ship must turn (r > 0.05 rad/s)
// and surge speed must stay positive throughout.
#include <gtest/gtest.h>

#include <cmath>

#include "fcb_simulator/rk4_integrator.hpp"
#include "fcb_simulator/types.hpp"

namespace {
constexpr double kPi = 3.14159265358979323846;
}

TEST(MmgSteadyTurn, Turns35DegRudder) {
  fcb_sim::MmgParams p;          // defaults = FCB params
  fcb_sim::FcbState s;
  s.u = 9.26;                    // ~18 kn

  const double delta = 35.0 * kPi / 180.0;
  const double n = 5.0;          // rev/s
  const double dt = 0.05;
  const int total_steps = static_cast<int>(60.0 / dt);
  const int check_after = static_cast<int>(10.0 / dt);

  bool reached_turn_rate = false;
  for (int i = 0; i < total_steps; ++i) {
    s = fcb_sim::rk4_step(s, delta, n, p, dt);
    ASSERT_GT(s.u, 0.0) << "surge speed went non-positive at step " << i;
    ASSERT_TRUE(std::isfinite(s.u) && std::isfinite(s.v) && std::isfinite(s.r))
        << "non-finite state at step " << i;
    if (i > check_after && std::abs(s.r) > 0.05) {
      reached_turn_rate = true;
    }
  }
  EXPECT_TRUE(reached_turn_rate) << "yaw rate never exceeded 0.05 rad/s";
}
