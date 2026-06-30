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
    point.psi_rad = 0.15;
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
  input.planned_route_bearing_rad = 0.0;
  input.constraints.cpa_safe_m = 1852.0;
  input.rot_max_rad_s = 0.2094;

  TargetState target;
  target.id = 7;
  target.x_m = 1200.0;
  target.y_m = 500.0;
  target.cpa_m = 2250.0;
  target.cpa_sigma_m = 100.0;
  target.tcpa_s = 180.0;
  input.targets.push_back(target);

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
