#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::ColregsPreferredDirection;
using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetRiskSnapshot;
using mass_l3::m5::TargetState;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::accept_tail_gate;

namespace {

constexpr std::uint8_t kRoleGiveWay = 1U;

MidMpcSolution starboard_offset_solution()
{
  MidMpcSolution solution;
  solution.status = MidMpcSolution::Status::Converged;
  for (int k = 0; k < 18; ++k) {
    TrajectoryPoint point;
    point.x_m = static_cast<double>(k) * 25.0;
    point.y_m = static_cast<double>(k) * 8.0;
    point.psi_rad = static_cast<double>(k) * 0.01;
    point.u_mps = 5.0;
    point.t_s = static_cast<double>(k) * 5.0;
    solution.trajectory.push_back(point);
  }
  return solution;
}

MidMpcInput give_way_starboard_fixture(double closing_speed_mps)
{
  MidMpcInput input;
  input.colregs_conflict_active = true;
  input.colregs_primary_role = kRoleGiveWay;
  input.colregs_preferred_direction = ColregsPreferredDirection::Starboard;
  input.own_ship.psi_rad = 0.0;
  input.own_ship.u_mps = 5.0;
  input.planned_route_bearing_rad = 0.0;
  input.constraints.cpa_safe_m = 1852.0;
  input.rot_max_rad_s = 0.2094;
  input.decel_max_mps2 = 0.08;

  TargetState target;
  target.id = 7;
  target.x_m = 1200.0;
  target.y_m = 500.0;
  target.cpa_m = 2250.0;
  target.cpa_sigma_m = 100.0;
  target.tcpa_s = 180.0;
  input.targets.push_back(target);
  input.tail_gate_targets.push_back(target);

  TargetRiskSnapshot risk;
  risk.target_id = "7";
  risk.risk_score = 1.0;
  risk.warning_margin_m = 400.0;
  risk.danger_margin_m = 700.0;
  risk.tcpa_s = 180.0;
  risk.closing_speed_mps = closing_speed_mps;
  risk.primary = true;
  input.target_risks.push_back(risk);
  return input;
}

}  // namespace

TEST(TailGate, AcceptsTerminalStateOnM6SideWithOpeningCpa)
{
  const auto result = accept_tail_gate(
      starboard_offset_solution(),
      give_way_starboard_fixture(-0.5));

  EXPECT_TRUE(result.accepted);
  EXPECT_FALSE(result.nlp_tail_gate_failed);
  EXPECT_EQ(result.reason, "accepted");
}

TEST(TailGate, RejectsWhenCpaIsWorsening)
{
  const auto result = accept_tail_gate(
      starboard_offset_solution(),
      give_way_starboard_fixture(0.4));

  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(result.nlp_tail_gate_failed);
  EXPECT_EQ(result.reason, "cpa_worsening");
}

TEST(TailGate, UsesRawM2CpaForReleaseInsteadOfOptimizerWeightedCpa)
{
  auto input = give_way_starboard_fixture(-0.5);
  input.constraints.cpa_safe_m = 2500.0;
  input.targets.front().cpa_m = 600.0;
  input.targets.front().cpa_sigma_m = 100.0;
  input.tail_gate_targets.front().cpa_m = 3000.0;
  input.tail_gate_targets.front().cpa_sigma_m = 100.0;

  const auto result = accept_tail_gate(starboard_offset_solution(), input);

  EXPECT_TRUE(result.accepted);
  EXPECT_EQ(result.reason, "accepted");
}

TEST(TailGate, RejectsTrajectoryThatCrossesAheadOfPrimaryTarget)
{
  auto input = give_way_starboard_fixture(-0.5);
  input.tail_gate_targets.front().x_m = 200.0;
  input.tail_gate_targets.front().y_m = 100.0;
  input.tail_gate_targets.front().sog_mps = 0.0;
  input.tail_gate_targets.front().cog_rad = 0.0;

  const auto result = accept_tail_gate(starboard_offset_solution(), input);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "crossing_ahead");
}

TEST(TailGate, RejectsDecelInfeasibleTrajectory)
{
  auto solution = starboard_offset_solution();
  solution.trajectory[0].u_mps = 5.0;
  solution.trajectory[1].u_mps = 1.0;
  auto input = give_way_starboard_fixture(-0.5);
  input.decel_max_mps2 = 0.08;

  const auto result = accept_tail_gate(solution, input);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "decel_infeasible");
}

TEST(TailGate, RejectsInstantaneousFirstStepHeadingJump)
{
  auto solution = starboard_offset_solution();
  solution.trajectory.front().psi_rad = 1.0;
  solution.trajectory.front().t_s = 0.0;
  auto input = give_way_starboard_fixture(-0.5);
  input.own_ship.psi_rad = 0.0;

  const auto result = accept_tail_gate(solution, input);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "turn_radius_infeasible");
}
