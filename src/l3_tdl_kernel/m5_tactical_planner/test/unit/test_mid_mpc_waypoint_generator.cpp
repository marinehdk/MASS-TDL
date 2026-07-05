#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

#include "geographic_msgs/msg/geo_pose_stamped.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
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
// 1. Converged plan → dense waypoints, status = "NORMAL"
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, ConvergedPlan_HasDenseWaypoints)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  // Phase 3: use realistic NLP horizon N=18 (450m @ 5m/s) so wheel-over prefix
  // (120m) leaves enough span for dense maneuver waypoints.
  const auto sol = make_converged_solution(0.0, 5.0, /*N=*/18);
  const auto plan = gen.generate(sol, 30.0, 122.0);

  EXPECT_EQ(plan.status, "NORMAL");
  EXPECT_GE(static_cast<int32_t>(plan.waypoints.size()), 4);
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
  // Phase 3: N=18 for sufficient horizon after wheel-over prefix.
  const auto sol = make_converged_solution(0.0, 5.0, /*N=*/18);  // psi=0 = north
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_GE(static_cast<int32_t>(plan.waypoints.size()), 4);
  EXPECT_GE(plan.waypoints[0].position.latitude, 30.0);
  EXPECT_GT(plan.waypoints[3].position.latitude, plan.waypoints[0].position.latitude);
}

// ---------------------------------------------------------------------------
// 4. Heading east (psi = pi/2) → waypoints progress eastward (longitude increases)
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, WaypointLatLonMonotonicallyEast)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  // Phase 3: N=18 for sufficient horizon after wheel-over prefix.
  const auto sol = make_converged_solution(M_PI / 2.0, 5.0, /*N=*/18);  // psi=pi/2 = east
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_GE(static_cast<int32_t>(plan.waypoints.size()), 4);
  EXPECT_GT(plan.waypoints[3].position.longitude, plan.waypoints[0].position.longitude);
}

// ---------------------------------------------------------------------------
// 5. Speed conversion: u_mps = 5.14444 → target_speed_kn ≈ 10.0 kn
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, SpeedConversion_CorrectKnots)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  // Phase 3: N=18 for sufficient horizon after wheel-over prefix.
  const auto sol = make_converged_solution(0.0, 5.14444, /*N=*/18);
  const auto plan = gen.generate(sol, 30.0, 122.0);

  ASSERT_GE(static_cast<int32_t>(plan.waypoints.size()), 4);
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


TEST(MidMpcWaypointGeneratorTest, CanonicalOptimizedRouteUsesSelectedSolverWaypoints)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};
  const auto sol = make_converged_solution(0.0, 5.0);
  auto plan = gen.generate(sol, 30.0, 122.0);

  mass_l3::m5::mid_mpc::populate_canonical_route_from_selected_plan(
      plan,
      sol,
      "m5-midmpc-test",
      "nominal",
      "emergency_avoidance");

  ASSERT_EQ(plan.status, "NORMAL");
  ASSERT_EQ(plan.latitude.size(), plan.waypoints.size());
  ASSERT_EQ(plan.segment_source.size(), plan.latitude.size());
  EXPECT_EQ(plan.plan_id, "m5-midmpc-test");
  EXPECT_EQ(plan.parent_route_id, "nominal");
  EXPECT_EQ(plan.behavior_mode, "emergency_avoidance");
  EXPECT_EQ(plan.nlp_solver_status, l3_msgs::msg::AvoidancePlan::NLP_CONVERGED);
  EXPECT_FALSE(plan.nlp_tail_gate_failed);
  for (std::size_t i = 0; i < plan.waypoints.size(); ++i) {
    EXPECT_DOUBLE_EQ(plan.latitude[i], plan.waypoints[i].position.latitude);
    EXPECT_DOUBLE_EQ(plan.longitude[i], plan.waypoints[i].position.longitude);
    EXPECT_EQ(plan.segment_source[i], l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
  }
}

TEST(MidMpcWaypointGeneratorTest, RejectsL2SuffixThatBreaksFullRoutePreflight)
{
  l3_msgs::msg::AvoidancePlan plan;
  plan.latitude = {30.0020, 30.0040};
  plan.longitude = {122.0, 122.0};
  plan.command_speed_mps = {3.0, 3.0};
  plan.navigation_mode = {"emergency_avoidance", "emergency_avoidance"};
  plan.segment_source = {
      l3_msgs::msg::AvoidancePlan::DEGRADED_CORRIDOR,
      l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD,
  };

  auto route = std::make_shared<l3_external_msgs::msg::PlannedRoute>();
  geographic_msgs::msg::GeoPoseStamped suffix;
  suffix.pose.position.latitude = 30.00405;
  suffix.pose.position.longitude = 122.0;
  route->route.poses.push_back(suffix);

  const bool accepted = mass_l3::m5::mid_mpc::append_l2_nominal_suffix_if_preflight_feasible(
      plan,
      route,
      mass_l3::m5::WaypointLatLon{30.0, 122.0},
      3.0);

  EXPECT_FALSE(accepted);
  ASSERT_EQ(plan.latitude.size(), 2u);
  EXPECT_EQ(plan.segment_source.back(), l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);
}

// ---------------------------------------------------------------------------
// Phase 3.10 (spec v2.3 §5.2): prepend_l2_history_prefix_if_preflight_feasible
// must insert L2 nominal poses from L2 start up to (excluding) the L2 pose
// closest to ownship. Without the prefix, coordinate_transform_node's
// first_geometry_change_index pairs the M5 plan (anchored at ownship) against
// last_feedback_path_ (L2 nominal in cold start) and rejects every revision.
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, PrependL2HistoryPrefixAddsPrefixWhenOwnshipBeyondStart)
{
  // Construct a plan whose first waypoint is the ownship anchor (typical
  // MID_MPC_OPTIMIZED shape produced by populate_canonical_route_from_selected_plan).
  l3_msgs::msg::AvoidancePlan plan;
  plan.latitude = {63.4600, 63.4610, 63.4620};
  plan.longitude = {10.3800, 10.3811, 10.3822};
  plan.command_speed_mps = {3.0, 3.0, 3.0};
  plan.navigation_mode = {"emergency_avoidance", "emergency_avoidance", "emergency_avoidance"};
  plan.segment_source = {
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
  };

  // L2 nominal route: ownship started at (63.44, 10.38) and has travelled north
  // to ~(63.46, 10.38). The first L2 pose is the L2 origin; the closest L2 pose
  // to ownship is the third one at (63.45, 10.38).
  auto route = std::make_shared<l3_external_msgs::msg::PlannedRoute>();
  auto add_pose = [&](double lat, double lon) {
    geographic_msgs::msg::GeoPoseStamped p;
    p.pose.position.latitude = lat;
    p.pose.position.longitude = lon;
    route->route.poses.push_back(p);
  };
  add_pose(63.4400, 10.3800);  // L2 start
  add_pose(63.4450, 10.3800);  // mid-history (5.5 m step from prev — will decimate)
  add_pose(63.4500, 10.3800);  // closest to ownship (63.46) → excluded from prefix
  add_pose(63.5000, 10.3800);  // future L2 tail

  const bool accepted = mass_l3::m5::mid_mpc::prepend_l2_history_prefix_if_preflight_feasible(
      plan,
      route,
      mass_l3::m5::WaypointLatLon{63.4600, 10.3800},
      3.0);

  EXPECT_TRUE(accepted);
  // Prefix = poses[0..closest_idx-1] = poses[0..1] (start + mid-history). Step
  // (63.44→63.445) is ~555 m, above kMinPrefixSpacingM=15, so both are kept.
  // Final plan = [L2 start, L2 mid, MID_MPC_OPTIMIZED anchor, MID_MPC, MID_MPC].
  ASSERT_EQ(plan.latitude.size(), 5u);
  EXPECT_NEAR(plan.latitude[0], 63.4400, 1e-6);
  EXPECT_EQ(plan.segment_source[0], l3_msgs::msg::AvoidancePlan::L2_HISTORICAL_PREFIX);
  EXPECT_EQ(plan.navigation_mode[0], "cruise");
  EXPECT_NEAR(plan.latitude[1], 63.4450, 1e-6);
  EXPECT_EQ(plan.segment_source[1], l3_msgs::msg::AvoidancePlan::L2_HISTORICAL_PREFIX);
  // Original MID_MPC_OPTIMIZED entries preserved after the prefix.
  EXPECT_EQ(plan.segment_source[2], l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
  EXPECT_NEAR(plan.latitude[2], 63.4600, 1e-6);
  EXPECT_EQ(plan.segment_source.back(), l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
}

TEST(MidMpcWaypointGeneratorTest, PrependL2HistoryPrefixNoOpWhenOwnshipAtStart)
{
  // Ownship is at the L2 start (cold-start edge case): no history to prepend.
  l3_msgs::msg::AvoidancePlan plan;
  plan.latitude = {63.4400, 63.4410, 63.4420};
  plan.longitude = {10.3800, 10.3811, 10.3822};
  plan.command_speed_mps = {3.0, 3.0, 3.0};
  plan.navigation_mode = {"emergency_avoidance", "emergency_avoidance", "emergency_avoidance"};
  plan.segment_source = {
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
      l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED,
  };

  auto route = std::make_shared<l3_external_msgs::msg::PlannedRoute>();
  auto add_pose = [&](double lat, double lon) {
    geographic_msgs::msg::GeoPoseStamped p;
    p.pose.position.latitude = lat;
    p.pose.position.longitude = lon;
    route->route.poses.push_back(p);
  };
  add_pose(63.4400, 10.3800);  // L2 start == ownship → closest_idx=0
  add_pose(63.5000, 10.3800);

  const bool accepted = mass_l3::m5::mid_mpc::prepend_l2_history_prefix_if_preflight_feasible(
      plan,
      route,
      mass_l3::m5::WaypointLatLon{63.4400, 10.3800},
      3.0);

  EXPECT_TRUE(accepted);
  // No prefix added — plan structure unchanged.
  ASSERT_EQ(plan.latitude.size(), 3u);
  EXPECT_EQ(plan.segment_source[0], l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
  EXPECT_NEAR(plan.latitude[0], 63.4400, 1e-6);
}

TEST(MidMpcWaypointGeneratorTest, AvoidancePlanTtlHasHeartbeatMargin)
{
  EXPECT_DOUBLE_EQ(mass_l3::m5::mid_mpc::kAvoidancePlanHeartbeat_s, 60.0);
  EXPECT_GT(mass_l3::m5::mid_mpc::kAvoidancePlanTtl_s,
            mass_l3::m5::mid_mpc::kAvoidancePlanHeartbeat_s);
  EXPECT_GE(mass_l3::m5::mid_mpc::kAvoidancePlanTtl_s, 70.0);
}

// ---------------------------------------------------------------------------
// Phase 3 (spec §3.6): wps[1] (first maneuver after anchor) must be at
// >= wheel_over_distance_m from origin so preflight emergency_wheel_over gate
// passes. Anchor (wps[0]) stays at own ship position.
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, SamplesFirstManeuverBeyondWheelOver)
{
  MidMpcWaypointGenerator::Config cfg;
  cfg.num_waypoints = 10;
  cfg.dt_s = 5.0;
  cfg.wheel_over_distance_m = 120.0;
  MidMpcWaypointGenerator gen(cfg);

  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  sol.trajectory.resize(18);
  double t = 0.0;
  for (auto& pt : sol.trajectory) {
    pt.t_s = t;
    pt.u_mps = 5.0;     // 25m/step
    pt.psi_rad = 0.0;   // straight north
    t += 5.0;
  }

  const auto plan = gen.generate(sol, /*own_ship_lat=*/63.44, /*own_ship_lon=*/10.38);
  ASSERT_EQ(plan.status, "NORMAL");
  ASSERT_GE(plan.waypoints.size(), 2u);
  const auto& wp0 = plan.waypoints[0].position;
  const auto& wp1 = plan.waypoints[1].position;
  // wps[0] = anchor within 1m of own ship
  EXPECT_LT(std::hypot((wp0.latitude - 63.44) * 111000.0,
                       (wp0.longitude - 10.38) * 111000.0), 1.0);
  // wps[1] >= wheel_over_distance_m (120m) from own ship
  const double d1_m = std::hypot((wp1.latitude - 63.44) * 111000.0,
                                 (wp1.longitude - 10.38) * 111000.0);
  EXPECT_GE(d1_m, 120.0 - 1.0) << "wps[1] distance: " << d1_m << "m";
}
