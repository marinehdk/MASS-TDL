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

  // Target positioned abeam (far east) so the starboard-offset trajectory's
  // terminal state has a safe CPA — the gate now checks the trajectory's
  // achieved terminal CPA, not M2's pre-maneuver CPA (Bug C). The
  // crossing-ahead test overrides this target.
  TargetState target;
  target.id = 7;
  target.x_m = 0.0;
  target.y_m = 4000.0;
  target.cpa_m = 4000.0;
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
  // trajectory[0].t_s == 0 (NLP initial label); the first-step turn rate is
  // measured over one control step (dt_s = 5 s here), not zero time. A jump
  // exceeding rot_max*dt_s must still be rejected: 1.5 rad / 5 s = 0.30 rad/s
  // > rot_max 0.2094 rad/s.
  auto solution = starboard_offset_solution();
  solution.trajectory.front().psi_rad = 1.5;
  solution.trajectory.front().t_s = 0.0;
  auto input = give_way_starboard_fixture(-0.5);
  input.own_ship.psi_rad = 0.0;

  const auto result = accept_tail_gate(solution, input);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "turn_radius_infeasible");
}

// Bug C deep (RC-A): the NLP returns trajectory[0].t_s == 0 (it is the first
// command over interval [0, dt_s], not a zero-duration step). The feasibility
// checks divided the own→traj[0] rate by ~0 (clamped 1e-6), rejecting every
// converged NLP whose u[0] != own_u exactly (decel_infeasible=512/512 in the
// rule14-ho trace). The rate must be measured over the step cadence.
TEST(TailGate, DecelCheckAcceptsGentleFirstStepWhenOwnSpeedDiffersFromU0)
{
  std::vector<TrajectoryPoint> traj;
  for (int k = 0; k < 4; ++k) {
    TrajectoryPoint p;
    p.t_s = static_cast<double>(k) * 5.0;  // dt_s = 5 s, traj[0].t_s = 0
    p.u_mps = 4.9 - 0.1 * static_cast<double>(k);  // 4.9, 4.8, 4.7, 4.6
    p.psi_rad = 0.0;
    traj.push_back(p);
  }
  // own_u = 5.0; first-step decel = (5.0 - 4.9)/5 = 0.02 m/s² < 0.08.
  EXPECT_TRUE(mass_l3::m5::tail_gate_decel_is_feasible(traj, 5.0, 0.08));
}

TEST(TailGate, TurnCheckAcceptsGentleFirstStepWhenOwnHeadingDiffersFromPsi0)
{
  std::vector<TrajectoryPoint> traj;
  for (int k = 0; k < 4; ++k) {
    TrajectoryPoint p;
    p.t_s = static_cast<double>(k) * 5.0;  // dt_s = 5 s, traj[0].t_s = 0
    p.psi_rad = 0.3 + 0.05 * static_cast<double>(k);  // 0.30, 0.35, 0.40, 0.45
    p.u_mps = 5.0;
    traj.push_back(p);
  }
  // own_psi = 0; first-step rate = 0.3/5 = 0.06 rad/s < rot_max 0.2094.
  EXPECT_TRUE(mass_l3::m5::tail_gate_turns_are_feasible(traj, 0.0, 0.2094));
}

// Bug C: the CPA-floor gate must accept an active-avoidance NLP route whose
// TERMINAL state achieves a safe CPA, even when the current target CPA is
// small (target still approaching). The old gate checked M2's current/do-nothing
// CPA (target.cpa_m), which is by definition small during active avoidance, so
// it rejected every converged NLP route -> geometric fallback drove the whole
// encounter. Spec committed-route-design-v2 §9.5: the CPA check is on the
// terminal hold state, not the pre-maneuver encounter. Also gate 5
// (cpa_worsening / current closing_speed) is removed: it is the same
// release-direction mistake and is subsumed by the terminal-CPA check.
TEST(TailGate, AcceptsActiveAvoidanceByTerminalCpaEvenIfCurrentCpaSmall)
{
  // Target approaching head-on: at (3000 m north, 0), moving south at 5 m/s.
  // Current CPA ~0 (collision course if neither vessel turns), target closing.
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

  TargetState target{};
  target.id = 7;
  target.x_m = 3000.0;
  target.y_m = 0.0;
  target.cog_rad = M_PI;        // south
  target.sog_mps = 5.0;
  target.cpa_m = 0.0;           // current CPA ~0 (active head-on approach)
  target.cpa_sigma_m = 50.0;
  target.tcpa_s = 300.0;
  input.targets.push_back(target);
  input.tail_gate_targets.push_back(target);

  TargetRiskSnapshot risk{};
  risk.target_id = "7";
  risk.primary = true;
  risk.closing_speed_mps = 10.0;  // target closing (active approach)
  input.target_risks.push_back(risk);

  // NLP trajectory: own turns starboard, terminal offset ~2 km to starboard
  // (east), heading east. The maneuver opens the terminal-state CPA well above
  // the floor (the gate must verify the trajectory, not the pre-maneuver CPA).
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  term.x_m = 100.0;
  term.y_m = 2000.0;            // own offset 2 km to starboard
  term.psi_rad = M_PI / 2.0;    // heading east
  term.u_mps = 5.0;
  term.t_s = 100.0;
  sol.trajectory.push_back(term);

  const auto result = accept_tail_gate(sol, input);

  EXPECT_TRUE(result.accepted)
      << "active-avoidance NLP route with safe terminal CPA must be accepted "
         "(current CPA being small is expected mid-maneuver, not a reject reason)";
  EXPECT_EQ(result.reason, "accepted");
}

// Counter-test: a trajectory that does NOT open the terminal CPA must still be
// rejected (the terminal-CPA gate is a real safety check, not a rubber stamp).
TEST(TailGate, RejectsTrajectoryWithUnsafeTerminalCpa)
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

  TargetState target{};
  target.id = 7;
  target.x_m = 600.0;           // close, dead ahead
  target.y_m = 0.0;
  target.cog_rad = M_PI;        // south, closing
  target.sog_mps = 5.0;
  target.cpa_m = 0.0;
  target.cpa_sigma_m = 50.0;
  target.tcpa_s = 60.0;
  input.targets.push_back(target);
  input.tail_gate_targets.push_back(target);

  TargetRiskSnapshot risk{};
  risk.target_id = "7";
  risk.primary = true;
  risk.closing_speed_mps = -1.0;   // target OPENING (release phase) -> gate applies
  input.target_risks.push_back(risk);

  // Trajectory barely offsets (50 m starboard) — terminal CPA stays small.
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  term.x_m = 50.0;
  term.y_m = 50.0;             // negligible starboard offset
  term.psi_rad = 0.0;
  term.u_mps = 5.0;
  term.t_s = 60.0;
  sol.trajectory.push_back(term);

  const auto result = accept_tail_gate(sol, input);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.reason, "cpa_release_floor");
}

// Bug C phase-aware gate: during ACTIVE approach (target closing) the CPA-floor
// is SKIPPED — the NLP maneuver is the CPA-opening action, so requiring the CPA
// already safe would reject every active-avoidance route. The same trajectory
// that is rejected above (when opening) must be ACCEPTED while the target is
// still closing.
TEST(TailGate, AcceptsActiveApproachBySkippingCpaFloorWhileTargetClosing)
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

  TargetState target{};
  target.id = 7;
  target.x_m = 600.0;           // close, dead ahead
  target.y_m = 0.0;
  target.cog_rad = M_PI;
  target.sog_mps = 5.0;
  target.cpa_m = 0.0;
  target.cpa_sigma_m = 50.0;
  target.tcpa_s = 60.0;
  input.targets.push_back(target);
  input.tail_gate_targets.push_back(target);

  TargetRiskSnapshot risk{};
  risk.target_id = "7";
  risk.primary = true;
  risk.closing_speed_mps = 10.0;  // target CLOSING (active approach) -> floor skipped
  input.target_risks.push_back(risk);

  // Same barely-offset trajectory as above (terminal CPA small), but the target
  // is closing -> the CPA-floor is skipped -> accepted (validated only by the
  // remaining gates: direction, no-crossing-ahead, feasibility).
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  TrajectoryPoint term{};
  term.x_m = 50.0;
  term.y_m = 50.0;
  term.psi_rad = 0.0;
  term.u_mps = 5.0;
  term.t_s = 60.0;
  sol.trajectory.push_back(term);

  const auto result = accept_tail_gate(sol, input);

  EXPECT_TRUE(result.accepted)
      << "active-approach NLP route must skip the release CPA-floor";
  EXPECT_EQ(result.reason, "accepted");
}
