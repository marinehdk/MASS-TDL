#include <gtest/gtest.h>

#include <cstdint>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::ColregsPreferredDirection;
using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::accept_tail_gate;

namespace {

constexpr std::uint8_t kRoleStandOn = 0U;

MidMpcSolution biased_offset_solution()
{
  MidMpcSolution solution;
  solution.status = MidMpcSolution::Status::Converged;
  for (int k = 0; k < 18; ++k) {
    TrajectoryPoint point;
    point.x_m = static_cast<double>(k) * 20.0;
    point.y_m = static_cast<double>(k) * 6.0;
    point.psi_rad = 0.12;
    point.u_mps = 5.0;
    point.t_s = static_cast<double>(k) * 5.0;
    solution.trajectory.push_back(point);
  }
  return solution;
}

MidMpcInput stand_on_fixture()
{
  MidMpcInput input;
  input.colregs_conflict_active = true;
  input.colregs_primary_role = kRoleStandOn;
  input.colregs_preferred_direction = ColregsPreferredDirection::Hold;
  input.planned_route_bearing_rad = 0.0;
  return input;
}

}  // namespace

TEST(StandOnReject, NlpProducingOffsetIsRejected)
{
  const auto result = accept_tail_gate(
      biased_offset_solution(),
      stand_on_fixture());

  EXPECT_FALSE(result.accepted);
  EXPECT_TRUE(result.nlp_tail_gate_failed);
  EXPECT_EQ(result.reason, "stand_on_heading_violation");
}

TEST(StandOnReject, DegradedPublishDoesNotMarkTerminalHoldForStandOnConflict)
{
  MidMpcInput input = stand_on_fixture();
  l3_msgs::msg::AvoidancePlan plan;
  plan.behavior_mode = "emergency_avoidance";
  plan.segment_source = {
      l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR,
      l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD};

  mass_l3::m5::apply_tail_gate_publish_contract(input, plan);

  EXPECT_TRUE(plan.latitude.empty());
  EXPECT_TRUE(plan.segment_source.empty());
  EXPECT_EQ(plan.status, "NORMAL");
  EXPECT_FALSE(plan.nlp_tail_gate_failed);
  EXPECT_EQ(plan.rationale, "M5 stand-on hold — no avoidance tail published");
}
