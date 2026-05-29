#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"

using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::mid_mpc::MidMpcWaypointGenerator;

namespace {

MidMpcSolution make_converged_solution(double psi_rad, double u_mps,
                                        int32_t N = 8, double dt_s = 5.0)
{
  MidMpcSolution sol;
  sol.status            = MidMpcSolution::Status::Converged;
  sol.solve_duration_ms = 42;
  sol.ipopt_iterations  = 10;
  sol.cost_colreg       = 0.0;
  sol.cost_dist         = 1.5;
  sol.cost_vel          = 0.3;
  sol.cost_total        = 1.8;

  for (int32_t k = 0; k < N; ++k) {
    TrajectoryPoint pt;
    pt.psi_rad = psi_rad;
    pt.u_mps   = u_mps;
    pt.t_s     = static_cast<double>(k) * dt_s;
    sol.trajectory.push_back(pt);
  }
  return sol;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. Converged plan → 4 waypoints, status = "NORMAL"
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, ConvergedPlan_Has4Waypoints)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(0.0, 5.0);
  const auto plan = gen.generate(sol, 30.0, 122.0);

  EXPECT_EQ(plan.status, "NORMAL");
  EXPECT_EQ(static_cast<int32_t>(plan.waypoints.size()), 4);
  EXPECT_FLOAT_EQ(plan.confidence, 1.0F);
}

// ---------------------------------------------------------------------------
// 2. Non-converged solution → DEGRADED, empty waypoints
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, DegradedPlan_OnNonConverged)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::NotInitialized;

  const auto plan = gen.generate(sol, 30.0, 122.0);

  EXPECT_EQ(plan.status, "DEGRADED");
  EXPECT_TRUE(plan.waypoints.empty());
  EXPECT_FLOAT_EQ(plan.confidence, 0.0F);
  EXPECT_FALSE(plan.rationale.empty());
}

// ---------------------------------------------------------------------------
// 3. Heading north → waypoints progress northward (latitude increases)
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, WaypointLatLonMonotonicallyNorth)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(0.0, 5.0);  // psi=0 = north
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_EQ(static_cast<int32_t>(plan.waypoints.size()), 4);
  EXPECT_GE(plan.waypoints[0].position.latitude, 30.0);
  EXPECT_GT(plan.waypoints[3].position.latitude, plan.waypoints[0].position.latitude);
}

// ---------------------------------------------------------------------------
// 4. Heading east (psi = pi/2) → waypoints progress eastward (longitude increases)
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, WaypointLatLonMonotonicallyEast)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(M_PI / 2.0, 5.0);  // psi=pi/2 = east
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_EQ(static_cast<int32_t>(plan.waypoints.size()), 4);
  EXPECT_GT(plan.waypoints[3].position.longitude, plan.waypoints[0].position.longitude);
}

// ---------------------------------------------------------------------------
// 5. Speed conversion: u_mps = 5.14444 → target_speed_kn ≈ 10.0 kn
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, SpeedConversion_CorrectKnots)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(0.0, 5.14444);
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_EQ(static_cast<int32_t>(plan.waypoints.size()), 4);
  for (const auto& wp : plan.waypoints) {
    EXPECT_NEAR(wp.target_speed_kn, 10.0, 0.05);
  }
}

// ---------------------------------------------------------------------------
// 6. Converged + empty trajectory → DEGRADED (safety guard)
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, ConvergedEmptyTrajectory_IsDegraded)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;  // converged but no points

  const auto plan = gen.generate(sol, 30.0, 122.0);

  EXPECT_EQ(plan.status, "DEGRADED");
  EXPECT_TRUE(plan.waypoints.empty());
  EXPECT_FLOAT_EQ(plan.confidence, 0.0F);
  EXPECT_FALSE(plan.rationale.empty());
}

// ---------------------------------------------------------------------------
// 8. Zero-movement trajectory at origin → first waypoint near (0, 0)
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, NedToGeopoint_ZeroOffset)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(0.0, 0.0);  // u=0 → no movement
  const auto plan = gen.generate(sol, 0.0, 0.0);

  ASSERT_FALSE(plan.waypoints.empty());
  EXPECT_NEAR(plan.waypoints[0].position.latitude,  0.0, 1e-6);
  EXPECT_NEAR(plan.waypoints[0].position.longitude, 0.0, 1e-6);
}

// ---------------------------------------------------------------------------
// 9. NLP converged with heading change → turn_radius_m > 0 (BUG-1 regression)
//    Bridge gate: abs(waypoints[0].turn_radius_m) > 1e-6 must be true.
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, NlpConverged_TurnRadiusIsPositive)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};

  // Build a solution where heading changes from 0 → 0.28 rad over 8 steps at 5 s each.
  // This produces a ROT of 0.28/8 = 0.035 rad/s ≈ 2°/s, turn_radius ≈ 5/0.035 ≈ 143 m.
  MidMpcSolution sol;
  sol.status            = MidMpcSolution::Status::Converged;
  sol.solve_duration_ms = 30;
  sol.ipopt_iterations  = 12;
  constexpr int32_t N = 8;
  constexpr double dt_s = 5.0;
  constexpr double u_mps = 5.0;
  constexpr double psi_start = 0.0;
  constexpr double psi_end   = 0.28;  // ~16 deg — typical NLP minimum starboard turn
  for (int32_t k = 0; k < N; ++k) {
    TrajectoryPoint pt;
    pt.psi_rad = psi_start + (psi_end - psi_start) * static_cast<double>(k) / (N - 1);
    pt.u_mps   = u_mps;
    pt.t_s     = static_cast<double>(k) * dt_s;
    sol.trajectory.push_back(pt);
  }

  const auto plan = gen.generate(sol, 63.44, 10.38);

  ASSERT_EQ(plan.status, "NORMAL");
  ASSERT_FALSE(plan.waypoints.empty());
  // KEY ASSERTION: turn_radius_m must be > 0 so bridge gate passes
  EXPECT_GT(plan.waypoints[0].turn_radius_m, 1e-6)
      << "Bridge gate abs(turn_radius_m) > 1e-6 must pass for avoidance to activate";
}
