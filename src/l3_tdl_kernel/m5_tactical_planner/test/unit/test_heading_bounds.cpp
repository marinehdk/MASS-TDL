#include <gtest/gtest.h>

#include <cmath>
#include <utility>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::propagate_trajectory_positions;
using mass_l3::m5::resolve_heading_box_bounds;
using mass_l3::m5::TrajectoryPoint;

namespace {
constexpr double kDeg2Rad = M_PI / 180.0;
}  // namespace

// Bug A regression guard (Slice J M5 NLP throw):
// During TRANSIT, M4 emits a full-circle heading window ([0, 360 deg] or the
// quantised [0, 359 deg]). Such a window means "no heading constraint" and MUST
// resolve to an unconstrained [-pi, +pi] box. A plain normalize_angle maps
// [0, 359 deg] to an inverted psi-relative window (min > max), which reaches
// MidMpcSolver as lbx > ubx and trips CasADi nlpsol's "lb <= ub" assertion
// every transit cycle (12606 throws observed). These tests pin the contract.

TEST(HeadingBoxBounds, FullCircleExactReturnsUnconstrained) {
  const auto b = resolve_heading_box_bounds(0.0, 2.0 * M_PI, 0.05);
  EXPECT_NEAR(b.first, -M_PI, 1e-6);
  EXPECT_NEAR(b.second, M_PI, 1e-6);
}

TEST(HeadingBoxBounds, FullCircleNearReturnsUnconstrained) {
  // [0, 359 deg] is the exact window observed 729x in the rule14-ho trace.
  const auto b = resolve_heading_box_bounds(0.0, 359.0 * kDeg2Rad, 5.0 * kDeg2Rad);
  EXPECT_NEAR(b.first, -M_PI, 1e-6);
  EXPECT_NEAR(b.second, M_PI, 1e-6);
}

TEST(HeadingBoxBounds, NarrowWindowStaysContiguousAndValid) {
  const auto b = resolve_heading_box_bounds(15.0 * kDeg2Rad, 45.0 * kDeg2Rad,
                                             30.0 * kDeg2Rad);
  EXPECT_LE(b.first, b.second);
  EXPECT_NEAR(b.first, 15.0 * kDeg2Rad, 1e-6);
  EXPECT_NEAR(b.second, 45.0 * kDeg2Rad, 1e-6);
}

TEST(HeadingBoxBounds, NarrowWindowNeverInvertsAcrossRefs) {
  // A legitimate avoidance corridor must never resolve to lb > ub for any
  // own-ship heading — otherwise the CasADi throw returns for non-transit too.
  const double h_min = 15.0 * kDeg2Rad;
  const double h_max = 45.0 * kDeg2Rad;
  for (double ref_deg : {0.0, 30.0, 90.0, 180.0, 270.0, 359.0}) {
    const auto b = resolve_heading_box_bounds(h_min, h_max, ref_deg * kDeg2Rad);
    EXPECT_LE(b.first, b.second) << "ref_deg=" << ref_deg;
  }
}

// Bug B regression guard (Slice J M5 wrong-side rejection):
// unpack_solution fills psi_rad/u_mps from the NLP x=[psi;u] but must also
// dead-reckon x_m/y_m, otherwise the trajectory has zero position everywhere
// and the tail-gate lateral-offset gate (>=25 m starboard) can NEVER pass —
// every converged NLP solution is rejected as wrong_m6_side. These tests pin
// the dead-reckon contract for propagate_trajectory_positions.

TEST(TrajectoryPositions, DeadReckonFromHeadingSpeed) {
  std::vector<TrajectoryPoint> traj(3);
  traj[0].psi_rad = 0.0;      traj[0].u_mps = 5.0;   // north
  traj[1].psi_rad = M_PI_2;   traj[1].u_mps = 5.0;   // east
  traj[2].psi_rad = M_PI_2;   traj[2].u_mps = 5.0;   // east
  propagate_trajectory_positions(traj, /*dt_s=*/1.0);
  // point[0] = start (0,0)
  EXPECT_NEAR(traj[0].x_m, 0.0, 1e-9);
  EXPECT_NEAR(traj[0].y_m, 0.0, 1e-9);
  // point[1] = advance by step-0 velocity: 5*cos(0)=5 north, 5*sin(0)=0 east
  EXPECT_NEAR(traj[1].x_m, 5.0, 1e-9);
  EXPECT_NEAR(traj[1].y_m, 0.0, 1e-9);
  // point[2] = + step-1 velocity: 5*cos(pi/2)=0 north, 5*sin(pi/2)=5 east
  EXPECT_NEAR(traj[2].x_m, 5.0, 1e-9);
  EXPECT_NEAR(traj[2].y_m, 5.0, 1e-9);
}

TEST(TrajectoryPositions, StarboardTurnProducesPositiveLateralOffset) {
  // A 60-deg starboard hold (typical give-way box_min) over a 12-step horizon
  // must produce a large positive lateral displacement — enough to clear the
  // 25 m tail-gate gate. Uses route bearing = 0 (north) so lateral = y_m (east).
  constexpr int kN = 12;
  constexpr double kDt = 5.0;            // 60 s horizon
  constexpr double kSpeed = 5.0;         // m/s
  constexpr double kPsi = 60.0 * kDeg2Rad;
  std::vector<TrajectoryPoint> traj(kN);
  for (auto& p : traj) { p.psi_rad = kPsi; p.u_mps = kSpeed; }
  propagate_trajectory_positions(traj, kDt);
  const double lateral_m = -std::sin(0.0) * traj.back().x_m
                         + std::cos(0.0) * traj.back().y_m;
  EXPECT_GT(lateral_m, 25.0);            // clears starboard gate
}

TEST(TrajectoryPositions, EmptyTrajectorySafe) {
  std::vector<TrajectoryPoint> traj;
  propagate_trajectory_positions(traj, 1.0);
  SUCCEED();
}

// ---------------------------------------------------------------------------
// Review High-3 (spec §5.3): the NLP terminal position pN must equal
// sol.trajectory.back()'s position, NOT a re-accumulation of all N intervals.
// propagate_trajectory_positions assigns point[k].pos = Σ_{j<k} (BEFORE advancing),
// so back() = point[N-1] = the position after N-1 intervals — the true terminal.
// Re-summing every point's u·dt from 0 yields Σ_{j<N} = one extra interval (the
// N-th step), which is off-by-one beyond the NLP terminal. This test pins that
// invariant so append_tail_waypoints_ can safely use back().x_m/y_m directly.
// ---------------------------------------------------------------------------
TEST(TrajectoryPositions, TerminalPositionIsBackNotReaccumulated) {
  // 3 points, dt=1, constant u=5 heading north.
  constexpr int kN = 3;
  constexpr double kDt = 1.0;
  constexpr double kSpeed = 5.0;
  std::vector<TrajectoryPoint> traj(kN);
  for (auto& p : traj) { p.psi_rad = 0.0; p.u_mps = kSpeed; }
  propagate_trajectory_positions(traj, kDt);

  // back() = point[N-1] = 2 intervals advanced = 10 m north (terminal).
  ASSERT_FALSE(traj.empty());
  EXPECT_NEAR(traj.back().x_m, static_cast<double>(kN - 1) * kSpeed * kDt, 1e-9);
  EXPECT_NEAR(traj.back().y_m, 0.0, 1e-9);

  // The off-by-one re-accumulation (sum ALL N steps) would give 15 m — wrong.
  double reaccumulated_n = 0.0;
  for (const auto& p : traj) {
    reaccumulated_n += p.u_mps * std::cos(p.psi_rad) * kDt;
  }
  EXPECT_NE(reaccumulated_n, traj.back().x_m);
  EXPECT_NEAR(reaccumulated_n, static_cast<double>(kN) * kSpeed * kDt, 1e-9);
}
