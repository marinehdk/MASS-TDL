// M5 Tactical Planner — LX-T3: continuous/swept CPA witness unit tests.
//
// Tests the compute_continuous_cpa() free function (pure geometry, no acados
// dependency). Constructs trajectories where the node-level CPA check passes
// (every discrete sample point is outside the hard CPA floor) but the swept
// line-segment passes within the floor (interval crossing).

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/continuous_cpa_check.hpp"

namespace m5 = mass_l3::m5;
namespace m5s = mass_l3::m5::shared;

namespace {

// Arbitrary CPA floor used across tests; mirrors default cpa_hard_m = 1852 m.
constexpr double kCpaHard = 1852.0;

// ---------------------------------------------------------------------------
// Test 1: no trajectory (empty) -> infinity, no violation.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, EmptyTrajectoryReturnsInfinity) {
  std::vector<m5::TrajectoryPoint> traj;
  std::vector<m5::TargetState> targets;
  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);
  EXPECT_FALSE(result.interval_crossing_detected);
  EXPECT_EQ(result.violating_target_id, 0);
  EXPECT_EQ(result.violating_segment_k, -1);
  // Infinity when no trajectory data is available.
  EXPECT_TRUE(std::isinf(result.min_swept_cpa_m));
}

// ---------------------------------------------------------------------------
// Test 2: single point trajectory (no segments) -> infinity, no violation.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, SinglePointReturnsInfinity) {
  std::vector<m5::TrajectoryPoint> traj(1);
  traj[0].x_m = 0.0;
  traj[0].y_m = 0.0;
  traj[0].t_s = 0.0;
  std::vector<m5::TargetState> targets;
  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);
  EXPECT_FALSE(result.interval_crossing_detected);
  EXPECT_TRUE(std::isinf(result.min_swept_cpa_m));
}

// ---------------------------------------------------------------------------
// Test 3: no targets -> infinity, no violation.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, NoTargetsReturnsInfinity) {
  std::vector<m5::TrajectoryPoint> traj(2);
  traj[0].x_m = 0.0;   traj[0].y_m = 0.0;   traj[0].t_s = 0.0;
  traj[1].x_m = 100.0; traj[1].y_m = 100.0; traj[1].t_s = 15.0;
  std::vector<m5::TargetState> targets;  // empty
  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);
  EXPECT_FALSE(result.interval_crossing_detected);
  EXPECT_TRUE(std::isinf(result.min_swept_cpa_m));
}

// ---------------------------------------------------------------------------
// Test 4: zero CPA floor -> infinity, no violation (function returns early).
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, ZeroCpaFloorReturnsInfinity) {
  std::vector<m5::TrajectoryPoint> traj(2);
  traj[0].x_m = 0.0;   traj[0].y_m = 0.0;   traj[0].t_s = 0.0;
  traj[1].x_m = 100.0; traj[1].y_m = 100.0; traj[1].t_s = 15.0;
  std::vector<m5::TargetState> targets(1);
  targets[0].id = 1;
  targets[0].x_m = 50.0;
  targets[0].y_m = 50.0;
  const auto result = m5s::compute_continuous_cpa(traj, targets, 0.0);
  EXPECT_FALSE(result.interval_crossing_detected);
  EXPECT_TRUE(std::isinf(result.min_swept_cpa_m));
}

// ---------------------------------------------------------------------------
// Test 5: straight-line trajectory, all nodes and segments far from target.
// All distances > 3000 m, well above kCpaHard. No violation expected.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, AllNodesFarNoViolation) {
  // Own-ship moves east along y=0 from x=3000 to x=4000.
  std::vector<m5::TrajectoryPoint> traj(3);
  traj[0].x_m = 3000.0; traj[0].y_m = 0.0;    traj[0].t_s = 0.0;
  traj[1].x_m = 3500.0; traj[1].y_m = 0.0;    traj[1].t_s = 15.0;
  traj[2].x_m = 4000.0; traj[2].y_m = 0.0;    traj[2].t_s = 30.0;

  // Stationary target far north (distance ~3500 m).
  std::vector<m5::TargetState> targets(1);
  targets[0].id = 42;
  targets[0].x_m = 3500.0;
  targets[0].y_m = 3500.0;  // distance = sqrt((3500-3500)^2 + (0-3500)^2) = 3500 > 1852
  targets[0].cog_rad = 0.0;
  targets[0].sog_mps = 0.0;

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);
  EXPECT_FALSE(result.interval_crossing_detected);
  EXPECT_GT(result.min_swept_cpa_m, kCpaHard);
  // The minimum should be ~3500 m (the distance to the target at its closest).
  EXPECT_NEAR(result.min_swept_cpa_m, 3500.0, 10.0);
}

// ---------------------------------------------------------------------------
// Test 6 (core): Interval crossing — node CPA >= kCpaHard but segment CPA
// drops below. Own-ship trajectory passes near target between samples.
//
// Geometry:
//   Own trajectory: (1900, 0) at k=0, (-1900, 0) at k=1.
//   Stationary target: (0, 1700).
//   Node distances: |(1900, 0) - (0, 1700)| = sqrt(1900^2 + 1700^2) = 2549 > 1852 OK
//                   |(-1900, 0) - (0, 1700)| = same = 2549 > 1852 OK
//   Segment midpoint: (0, 0). Distance to target: |(0, 0) - (0, 1700)| = 1700 < 1852.
//   Interval crossing detected.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, IntervalCrossingDetectedStationaryTarget) {
  std::vector<m5::TrajectoryPoint> traj(2);
  traj[0].x_m = 1900.0;  traj[0].y_m = 0.0;   traj[0].t_s = 0.0;
  traj[1].x_m = -1900.0; traj[1].y_m = 0.0;   traj[1].t_s = 15.0;

  std::vector<m5::TargetState> targets(1);
  targets[0].id = 99;
  targets[0].x_m = 0.0;
  targets[0].y_m = 1700.0;  // stationary target
  targets[0].cog_rad = 0.0;
  targets[0].sog_mps = 0.0;

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);

  // Must detect the interval crossing.
  EXPECT_TRUE(result.interval_crossing_detected);
  EXPECT_EQ(result.violating_target_id, 99);
  EXPECT_EQ(result.violating_segment_k, 0);  // segment 0 (k=0 -> k=1)
  // Swept CPA should be ~1700 m (perpendicular distance at midpoint).
  EXPECT_NEAR(result.min_swept_cpa_m, 1700.0, 10.0);
  EXPECT_LT(result.min_swept_cpa_m, kCpaHard);
}

// ---------------------------------------------------------------------------
// Test 7: Interval crossing with moving target.
//
// Own trajectory: from (5000, 5000) to (5000, 3000) in one step (moves south).
// Target: starts at (3000, 5000), moves north at 2000/15 m/s.
//
// At k=0: own=(5000,5000), target=(3000,5000) -> R=(2000,0), |R|=2000 > 1852 OK
// At k=1: own=(5000,3000), target=(5000,5000)  -> R=(0,-2000), |R|=2000 > 1852 OK
// Swept segment: (2000,0) -> (0,-2000). Midpoint (1000,-1000), dist=1414 < 1852.
// Interval crossing detected.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, IntervalCrossingDetectedMovingTarget) {
  std::vector<m5::TrajectoryPoint> traj(2);
  // Own moves south (decreasing x).
  traj[0].x_m = 5000.0; traj[0].y_m = 5000.0; traj[0].t_s = 0.0;
  traj[1].x_m = 5000.0; traj[1].y_m = 3000.0; traj[1].t_s = 15.0;

  std::vector<m5::TargetState> targets(1);
  targets[0].id = 7;
  targets[0].x_m = 3000.0;
  targets[0].y_m = 5000.0;          // starts at (3000, 5000)
  targets[0].cog_rad = 0.0;         // heading north (increasing x)
  targets[0].sog_mps = 2000.0 / 15.0;  // reaches (5000, 5000) at t=15

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);

  EXPECT_TRUE(result.interval_crossing_detected);
  EXPECT_EQ(result.violating_target_id, 7);
  EXPECT_EQ(result.violating_segment_k, 0);
  // Swept CPA at midpoint = sqrt(1000^2 + 1000^2) = 1414.21 m.
  EXPECT_NEAR(result.min_swept_cpa_m, 1414.2, 1.0);
  EXPECT_LT(result.min_swept_cpa_m, kCpaHard);
}

// ---------------------------------------------------------------------------
// Test 8: Multiple targets — first violating target reported.
// Same geometry as Test 6 but with a second, far-away target.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, MultipleTargetsFirstViolationReported) {
  std::vector<m5::TrajectoryPoint> traj(2);
  traj[0].x_m = 1900.0;  traj[0].y_m = 0.0;   traj[0].t_s = 0.0;
  traj[1].x_m = -1900.0; traj[1].y_m = 0.0;   traj[1].t_s = 15.0;

  std::vector<m5::TargetState> targets(2);
  // Far target (no violation expected from this one).
  targets[0].id = 1;
  targets[0].x_m = 5000.0;
  targets[0].y_m = 5000.0;
  targets[0].cog_rad = 0.0;
  targets[0].sog_mps = 0.0;

  // Interval-crossing target.
  targets[1].id = 2;
  targets[1].x_m = 0.0;
  targets[1].y_m = 1700.0;
  targets[1].cog_rad = 0.0;
  targets[1].sog_mps = 0.0;

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);

  EXPECT_TRUE(result.interval_crossing_detected);
  // Violation should reference the crossing target (id=2), NOT the far target.
  EXPECT_EQ(result.violating_target_id, 2);
  EXPECT_EQ(result.violating_segment_k, 0);
  // Overall min CPA is still dominated by the crossing target (~1700m).
  EXPECT_NEAR(result.min_swept_cpa_m, 1700.0, 10.0);
}

// ---------------------------------------------------------------------------
// Test 9: Multi-segment trajectory -- violation in later segment.
//
// Own moves: k=0:(3000,2000) -> k=1:(3000,1000) -> k=2:(-3000,1000) -> k=3:(-3000,0)
// Stationary target at (0, 0).
//
// Node distances:
//   k=0: |(3000,2000)| = 3606 > 1852 OK
//   k=1: |(3000,1000)| = 3162 > 1852 OK
//   k=2: |(-3000,1000)| = 3162 > 1852 OK
//   k=3: |(-3000,0)|    = 3000 > 1852 OK
// Segment distances:
//   k=0->1: vertical line at x=3000, min dist = 3000 > 1852 OK
//   k=1->2: horizontal from (3000,1000) to (-3000,1000), midpoint (0,1000), dist=1000 < 1852 VIOLATION
//   k=2->3: vertical line at x=-3000, min dist = 3000 > 1852 OK
//
// Violation should be reported in segment 1 (k=1 -> k=2).
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, MultiSegmentViolationInLaterSegment) {
  std::vector<m5::TrajectoryPoint> traj(4);
  traj[0].x_m = 3000.0;  traj[0].y_m = 2000.0; traj[0].t_s = 0.0;
  traj[1].x_m = 3000.0;  traj[1].y_m = 1000.0; traj[1].t_s = 15.0;
  traj[2].x_m = -3000.0; traj[2].y_m = 1000.0; traj[2].t_s = 30.0;
  traj[3].x_m = -3000.0; traj[3].y_m = 0.0;    traj[3].t_s = 45.0;

  std::vector<m5::TargetState> targets(1);
  targets[0].id = 11;
  targets[0].x_m = 0.0;
  targets[0].y_m = 0.0;
  targets[0].cog_rad = 0.0;
  targets[0].sog_mps = 0.0;

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);

  EXPECT_TRUE(result.interval_crossing_detected);
  EXPECT_EQ(result.violating_target_id, 11);
  // The violation occurs in segment 1 (k=1 -> k=2).
  EXPECT_EQ(result.violating_segment_k, 1);
  EXPECT_NEAR(result.min_swept_cpa_m, 1000.0, 10.0);
}

// ---------------------------------------------------------------------------
// Test 10: The swept CPA reports the correct minimum even when the segment
// endpoint dip (node-level) is the true minimum (no interval crossing bonus).
// The function should still report that minimum.
// ---------------------------------------------------------------------------
TEST(ContinuousCpaCheck, MinAtNodeReportsCorrectValue) {
  // Own moves straight toward target, minimum at the closest node.
  std::vector<m5::TrajectoryPoint> traj(3);
  traj[0].x_m = 0.0;     traj[0].y_m = 5000.0; traj[0].t_s = 0.0;
  traj[1].x_m = 0.0;     traj[1].y_m = 3000.0; traj[1].t_s = 15.0;
  traj[2].x_m = 0.0;     traj[2].y_m = 1000.0; traj[2].t_s = 30.0;

  std::vector<m5::TargetState> targets(1);
  targets[0].id = 3;
  targets[0].x_m = 0.0;
  targets[0].y_m = 1000.0;   // own reaches target at k=2
  targets[0].cog_rad = 0.0;
  targets[0].sog_mps = 0.0;

  const auto result = m5s::compute_continuous_cpa(traj, targets, kCpaHard);

  // min_swept_cpa should be ~0 (at k=2, own and target coincide).
  EXPECT_NEAR(result.min_swept_cpa_m, 0.0, 10.0);
  // This is NOT an interval crossing in the strict sense (the node also detects
  // it), but the swept function correctly reports the minimum distance.
  // interval_crossing_detected is true because min < cpa_hard.
  EXPECT_TRUE(result.interval_crossing_detected);
}

}  // namespace
