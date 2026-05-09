// SPDX-License-Identifier: Proprietary
#include <gtest/gtest.h>
#include "fcb_simulator/scenario_runner.hpp"

TEST(ScenarioRunner, StraightDecelReturnsPositiveStopDistance) {
  ship_sim::ShipState init;
  init.u = 9.26; init.psi = 1.5708;
  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::STRAIGHT_DECEL, init);
  EXPECT_GT(result.stop_distance_m, 10.0);
  EXPECT_FALSE(result.time_s.empty());
  EXPECT_EQ(result.time_s.size(), result.x_m.size());
}

TEST(ScenarioRunner, StandardTurnReturnsTrajectory) {
  ship_sim::ShipState init;
  init.u = 9.26; init.psi = 1.5708;
  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::STANDARD_TURN_35DEG, init);
  EXPECT_GT(result.tactical_diameter_m, 0.0);
  EXPECT_FALSE(result.time_s.empty());
}

TEST(ScenarioRunner, ZigZagReturnsTrajectory) {
  ship_sim::ShipState init;
  init.u = 9.26; init.psi = 1.5708;
  auto result = fcb_sim::run_scenario(fcb_sim::ManeuverType::ZIG_ZAG_10_10, init);
  EXPECT_FALSE(result.time_s.empty());
}
