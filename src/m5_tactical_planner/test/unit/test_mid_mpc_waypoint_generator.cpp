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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
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
  MidMpcWaypointGenerator gen;
  const auto sol = make_converged_solution(0.0, 0.0);  // u=0 → no movement
  const auto plan = gen.generate(sol, 0.0, 0.0);

  ASSERT_FALSE(plan.waypoints.empty());
  EXPECT_NEAR(plan.waypoints[0].position.latitude,  0.0, 1e-6);
  EXPECT_NEAR(plan.waypoints[0].position.longitude, 0.0, 1e-6);
}
