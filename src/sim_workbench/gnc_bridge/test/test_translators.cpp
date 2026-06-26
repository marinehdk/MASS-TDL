// TDD unit tests for gnc_bridge translators (Track A A4).
// Verifies each L3<->GNC field map is faithful and that command_heading_deg is
// left empty (GNC follows geometry, per spec D3 + docking doc §9.2).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "gnc_bridge/translators.hpp"
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
#include "l3_external_msgs/msg/gnc_execution_status.hpp"
#include "ship_interfaces/msg/avoidance_plan.hpp"
#include "ship_interfaces/msg/geo_position.hpp"
#include "ship_interfaces/msg/route_execution_status.hpp"
#include "sil_msgs/msg/own_ship_state.hpp"

using gnc_bridge::to_gnc_avoidance_plan;
using gnc_bridge::to_gnc_route_plan;
using gnc_bridge::to_sil_own_ship_state;
using gnc_bridge::to_l3_gnc_execution_status;
using gnc_bridge::rebase_avoidance_plan_timebase;

namespace {
builtin_interfaces::msg::Time make_stamp(int32_t sec, uint32_t nanosec = 0) {
  builtin_interfaces::msg::Time t;
  t.sec = sec;
  t.nanosec = nanosec;
  return t;
}

constexpr double kPi = 3.14159265358979323846;
}  // namespace

TEST(Translators, AvoidanceWaypointsToGncPlanPreservesWaypoints) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.plan_id = "test-1";
  src.behavior_mode = "emergency_avoidance";
  src.command_source = "collision_avoidance";
  src.latitude  = {63.44, 63.45, 63.46};
  src.longitude = {10.38, 10.39, 10.40};
  src.command_speed_mps = {3.0, 3.0, 3.0};
  src.navigation_mode = {"emergency_avoidance", "emergency_avoidance", "emergency_avoidance"};
  src.allow_degraded_execution = true;
  const auto stamp = make_stamp(100);
  auto gnc = to_gnc_avoidance_plan(src, stamp);
  EXPECT_EQ(gnc.header.stamp.sec, 100);
  EXPECT_EQ(gnc.plan_id, "test-1");
  EXPECT_EQ(gnc.behavior_mode, "emergency_avoidance");
  ASSERT_EQ(gnc.latitude.size(), 3u);
  EXPECT_DOUBLE_EQ(gnc.latitude[0], 63.44);
  EXPECT_DOUBLE_EQ(gnc.longitude[2], 10.40);
  ASSERT_EQ(gnc.command_speed_mps.size(), 3u);
  EXPECT_DOUBLE_EQ(gnc.command_speed_mps[0], 3.0);
  EXPECT_TRUE(gnc.allow_degraded_execution);
}

TEST(Translators, AvoidancePlanLeavesCommandHeadingEmptyForGeometryFollowing) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.latitude = {1.0};
  src.longitude = {2.0};
  auto gnc = to_gnc_avoidance_plan(src, make_stamp(0));
  EXPECT_TRUE(gnc.command_heading_deg.empty())
      << "GNC follows waypoint geometry; command_heading_deg must stay empty";
}

TEST(Translators, AvoidancePlanCarriesReturnToRouteHint) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.has_return_to_route_point = true;
  src.return_latitude  = 63.5;
  src.return_longitude = 10.5;
  auto gnc = to_gnc_avoidance_plan(src, make_stamp(0));
  EXPECT_TRUE(gnc.has_return_to_route_point);
  EXPECT_DOUBLE_EQ(gnc.return_latitude, 63.5);
  EXPECT_DOUBLE_EQ(gnc.return_longitude, 10.5);
}

TEST(Translators, RebaseAvoidancePlanPreservesValidUntilTtlOnTargetClock) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.stamp = make_stamp(100, 200000000);
  src.valid_until = make_stamp(130, 500000000);
  auto gnc = to_gnc_avoidance_plan(src, src.stamp);

  rebase_avoidance_plan_timebase(gnc, make_stamp(2000, 100000000));

  EXPECT_EQ(gnc.header.stamp.sec, 2000);
  EXPECT_EQ(gnc.header.stamp.nanosec, 100000000u);
  EXPECT_EQ(gnc.valid_until.sec, 2030);
  EXPECT_EQ(gnc.valid_until.nanosec, 400000000u);
}

TEST(Translators, RebaseAvoidancePlanLeavesZeroDeadlineForDefaultHold) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.stamp = make_stamp(100);
  auto gnc = to_gnc_avoidance_plan(src, src.stamp);

  rebase_avoidance_plan_timebase(gnc, make_stamp(2000));

  EXPECT_EQ(gnc.header.stamp.sec, 2000);
  EXPECT_EQ(gnc.valid_until.sec, 0);
  EXPECT_EQ(gnc.valid_until.nanosec, 0u);
}

TEST(Translators, RebaseAvoidancePlanKeepsExpiredSourcePlanExpired) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.stamp = make_stamp(100);
  src.valid_until = make_stamp(99);
  auto gnc = to_gnc_avoidance_plan(src, src.stamp);

  rebase_avoidance_plan_timebase(gnc, make_stamp(2000));

  EXPECT_EQ(gnc.header.stamp.sec, 2000);
  EXPECT_EQ(gnc.valid_until.sec, 2000);
  EXPECT_EQ(gnc.valid_until.nanosec, 0u);
}

TEST(Translators, GeoPositionToOwnShipStateMapsCoreFields) {
  ship_interfaces::msg::GeoPosition geo;
  geo.latitude   = 63.44;
  geo.longitude  = 10.38;
  geo.heading_deg = 45.0;
  geo.course_deg  = 47.0;
  geo.speed_mps   = 4.0;
  geo.surge_mps   = 3.9;
  geo.sway_mps    = 0.3;
  geo.yaw_rate_deg_s = 1.2;
  geo.yaw_rate_rads  = 0.021;
  const auto stamp = make_stamp(200);
  auto oss = to_sil_own_ship_state(geo, stamp);
  EXPECT_EQ(oss.stamp.sec, 200);
  EXPECT_DOUBLE_EQ(oss.lat, 63.44);
  EXPECT_DOUBLE_EQ(oss.lon, 10.38);
  EXPECT_NEAR(oss.heading, kPi / 4.0, 1e-12);
  EXPECT_DOUBLE_EQ(oss.sog, 4.0);
  EXPECT_NEAR(oss.cog, 47.0 * kPi / 180.0, 1e-12);
  EXPECT_DOUBLE_EQ(oss.u, 3.9);
  EXPECT_DOUBLE_EQ(oss.v, 0.3);
  EXPECT_NEAR(oss.rot, 1.2 * kPi / 180.0, 1e-12);
  EXPECT_DOUBLE_EQ(oss.r, 0.021);
}

TEST(Translators, RouteExecutionStatusToGncExecutionStatusMapsVerdict) {
  ship_interfaces::msg::RouteExecutionStatus res;
  res.plan_id = "p-7";
  res.accepted = false;
  res.rejected = true;
  res.reason = "turn_radius_too_small";
  res.suggested_action = "slow_down_or_enlarge_turn_radius";
  res.current_latitude  = 63.4;
  res.current_longitude = 10.4;
  res.current_heading_deg = 12.0;
  res.current_speed_mps   = 2.5;
  res.cross_track_error_m = 3.3;
  res.suggested_max_speed_mps = 2.0;
  const auto stamp = make_stamp(300);
  auto l3 = to_l3_gnc_execution_status(res, stamp);
  EXPECT_EQ(l3.stamp.sec, 300);
  EXPECT_EQ(l3.plan_id, "p-7");
  EXPECT_FALSE(l3.accepted);
  EXPECT_TRUE(l3.rejected);
  EXPECT_EQ(l3.reason, "turn_radius_too_small");
  EXPECT_EQ(l3.suggested_action, "slow_down_or_enlarge_turn_radius");
  EXPECT_DOUBLE_EQ(l3.current_latitude, 63.4);
  EXPECT_DOUBLE_EQ(l3.cross_track_error_m, 3.3);
  EXPECT_DOUBLE_EQ(l3.suggested_max_speed_mps, 2.0);
}

TEST(Translators, RouteExecutionStatusDegradedMaps) {
  ship_interfaces::msg::RouteExecutionStatus res;
  res.executing = true;
  res.degraded  = true;
  res.execution_state = "EXECUTING_WITH_LIMIT";
  res.reason = "yaw_rate_limited";
  auto l3 = to_l3_gnc_execution_status(res, make_stamp(0));
  EXPECT_TRUE(l3.executing);
  EXPECT_TRUE(l3.degraded);
  EXPECT_EQ(l3.execution_state, "EXECUTING_WITH_LIMIT");
}
