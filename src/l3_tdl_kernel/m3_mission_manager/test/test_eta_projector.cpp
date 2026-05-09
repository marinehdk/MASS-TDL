#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>

#include "geographic_msgs/msg/geo_pose_stamped.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m3_mission_manager/eta_projector.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {
namespace {

constexpr double kDefaultSamplingIntervalS = 60.0;
constexpr int64_t kDefaultForecastHorizonS = 3600;
constexpr double kDefaultUncertaintyKn = 0.3;
constexpr double kDefaultAgeThresholdS = 0.5;
constexpr double kDefaultInfeasibleMarginS = 600.0;

constexpr double kTestLat1 = 38.0;
constexpr double kTestLon1 = -122.0;
constexpr double kTestLat2 = 37.9;
constexpr double kTestLon2 = -121.9;
constexpr double kTestLat3 = 37.8;
constexpr double kTestLon3 = -121.8;

constexpr int64_t kStampSec = 1000;
constexpr int64_t kStampNsec = 0;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

EtaProjectorConfig make_default_eta_config() {
  return EtaProjectorConfig{
      static_cast<int32_t>(kDefaultSamplingIntervalS),
      kDefaultForecastHorizonS,
      kDefaultUncertaintyKn,
      kDefaultAgeThresholdS,
      kDefaultInfeasibleMarginS};
}

l3_external_msgs::msg::PlannedRoute make_test_route(
    uint64_t route_id = 1,
    double total_distance_nm = 50.0,
    double estimated_duration_s = 3600.0) {
  l3_external_msgs::msg::PlannedRoute msg;
  msg.stamp.sec = kStampSec;
  msg.stamp.nanosec = kStampNsec;
  msg.route_id = route_id;
  msg.total_distance_nm = total_distance_nm;
  msg.estimated_duration_s = estimated_duration_s;
  msg.confidence = 0.95f;
  msg.rationale = "test route";

  // Build GeoPath with 3 waypoints
  geographic_msgs::msg::GeoPoseStamped pose1;
  pose1.pose.position.latitude = kTestLat1;
  pose1.pose.position.longitude = kTestLon1;
  pose1.pose.position.altitude = 0.0;

  geographic_msgs::msg::GeoPoseStamped pose2;
  pose2.pose.position.latitude = kTestLat2;
  pose2.pose.position.longitude = kTestLon2;
  pose2.pose.position.altitude = 0.0;

  geographic_msgs::msg::GeoPoseStamped pose3;
  pose3.pose.position.latitude = kTestLat3;
  pose3.pose.position.longitude = kTestLon3;
  pose3.pose.position.altitude = 0.0;

  msg.route.poses.push_back(pose1);
  msg.route.poses.push_back(pose2);
  msg.route.poses.push_back(pose3);

  return msg;
}

l3_external_msgs::msg::SpeedProfile make_test_speed_profile(
    uint64_t profile_id = 1) {
  l3_external_msgs::msg::SpeedProfile msg;
  msg.stamp.sec = kStampSec;
  msg.stamp.nanosec = 0;
  msg.profile_id = profile_id;
  msg.confidence = 0.95f;
  msg.rationale = "test speed profile";

  // 2 segments: 0-50000m at 18kn, 50000-92600m at 14kn
  msg.segment_start_distances_m = {0.0, 50000.0};
  msg.segment_end_distances_m = {50000.0, 92600.0};
  msg.target_speeds_kn = {18.0, 14.0};
  msg.max_speeds_kn = {20.0, 16.0};
  msg.min_speeds_kn = {12.0, 10.0};
  msg.segment_types = {"transit", "approach"};

  return msg;
}

l3_msgs::msg::WorldState make_test_world_state(
    double sog_kn = 18.0,
    double lat = kTestLat1,
    double lon = kTestLon1,
    int64_t stamp_sec = kStampSec,
    int64_t stamp_nsec = kStampNsec) {
  l3_msgs::msg::WorldState ws;
  ws.stamp.sec = static_cast<int32_t>(stamp_sec);
  ws.stamp.nanosec = static_cast<uint32_t>(stamp_nsec);
  ws.own_ship.sog_kn = sog_kn;
  ws.own_ship.cog_deg = 0.0;
  ws.own_ship.heading_deg = 0.0;
  ws.own_ship.position.latitude = lat;
  ws.own_ship.position.longitude = lon;
  ws.own_ship.position.altitude = 0.0;
  ws.confidence = 0.9f;
  ws.rationale = "test world state";
  return ws;
}

std::chrono::steady_clock::time_point make_now(
    int64_t stamp_sec = kStampSec,
    int64_t offset_ns = 50'000'000) {  // 50ms after stamp
  const int64_t total_ns = stamp_sec * 1'000'000'000LL + offset_ns;
  return std::chrono::steady_clock::time_point(
      std::chrono::nanoseconds(total_ns));
}

// ---------------------------------------------------------------------------
// 1. ProjectWithLoadedData — basic projection returns EtaProjection
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, ProjectWithLoadedData) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  projector.update_route(make_test_route());
  projector.update_speed_profile(make_test_speed_profile());

  const auto ws = make_test_world_state();
  const auto now = make_now();

  const auto result = projector.project(ws, now);
  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->eta_s, 0.0);
  EXPECT_GT(result->distance_remaining_nm, 0.0);
  EXPECT_EQ(result->computed_at, now);
}

// ---------------------------------------------------------------------------
// 2. ReturnsNulloptWithoutRoute — no route loaded -> nullopt
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, ReturnsNulloptWithoutRoute) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  // Only set profile, no route
  projector.update_speed_profile(make_test_speed_profile());

  const auto ws = make_test_world_state();
  const auto now = make_now();

  const auto result = projector.project(ws, now);
  EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 3. ReturnsNulloptWithoutProfile — no profile -> nullopt
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, ReturnsNulloptWithoutProfile) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  // Only set route, no profile
  projector.update_route(make_test_route());

  const auto ws = make_test_world_state();
  const auto now = make_now();

  const auto result = projector.project(ws, now);
  EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 4. ProjectedEtaPositive — ETA > 0 for forward route
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, ProjectedEtaPositive) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  projector.update_route(make_test_route());
  projector.update_speed_profile(make_test_speed_profile());

  const auto ws = make_test_world_state();
  const auto now = make_now();

  const auto result = projector.project(ws, now);
  ASSERT_TRUE(result.has_value());
  EXPECT_GT(result->eta_s, 0.0);
  EXPECT_GT(result->distance_remaining_nm, 0.0);
}

// ---------------------------------------------------------------------------
// 5. UncertaintyNonZero — uncertainty > 0 when config set
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, UncertaintyNonZero) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  projector.update_route(make_test_route());
  projector.update_speed_profile(make_test_speed_profile());

  const auto ws = make_test_world_state(18.0);
  const auto now = make_now();

  const auto result = projector.project(ws, now);
  ASSERT_TRUE(result.has_value());
  // Uncertainty should be > 0 when SOG > 0 and config has sea_current_uncertainty_kn
  EXPECT_GT(result->uncertainty_s, 0.0);
}

// ---------------------------------------------------------------------------
// 6. UpdateRouteChangesProjection — new route changes ETA
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, UpdateRouteChangesProjection) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  // First route: 50 nm
  projector.update_route(make_test_route(1, 50.0, 3600.0));
  projector.update_speed_profile(make_test_speed_profile());

  const auto ws = make_test_world_state();
  const auto now = make_now();
  const auto first_result = projector.project(ws, now);
  ASSERT_TRUE(first_result.has_value());

  // Second route: 100 nm (longer -> larger ETA)
  projector.update_route(make_test_route(2, 100.0, 7200.0));

  const auto second_result = projector.project(ws, now);
  ASSERT_TRUE(second_result.has_value());

  // Longer route should have larger ETA and distance remaining
  EXPECT_GT(second_result->eta_s, first_result->eta_s);
  EXPECT_GT(second_result->distance_remaining_nm,
            first_result->distance_remaining_nm);
}

// ---------------------------------------------------------------------------
// 7. SogFromWorldState — current_sog_kn works
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, SogFromWorldState) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);

  const auto ws = make_test_world_state(15.5);
  EXPECT_DOUBLE_EQ(projector.current_sog_kn(ws), 15.5);

  const auto ws_zero = make_test_world_state(0.0);
  EXPECT_DOUBLE_EQ(projector.current_sog_kn(ws_zero), 0.0);

  const auto ws_neg = make_test_world_state(-1.0);
  EXPECT_DOUBLE_EQ(projector.current_sog_kn(ws_neg), -1.0);
}

// ---------------------------------------------------------------------------
// 8. StaleWorldStateDetected — old world state handled
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, StaleWorldStateDetected) {
  auto config = make_default_eta_config();
  config.world_state_age_threshold_s = 0.5;  // 500ms
  EtaProjector projector(config);

  projector.update_route(make_test_route());
  projector.update_speed_profile(make_test_speed_profile());

  // World state stamp is at 1000s, but now is 1002s (2 seconds later)
  // That's older than 0.5s threshold -> should be rejected
  const auto ws = make_test_world_state(18.0, kTestLat1, kTestLon1,
                                        1000, 0);  // stamp at t=1000s
  const auto far_now = make_now(1002, 0);  // now at t=1002s, age = 2s

  const auto result = projector.project(ws, far_now);
  EXPECT_FALSE(result.has_value());
}

// ---------------------------------------------------------------------------
// 9. MoveConstructionWorks — move constructor and assignment function
// ---------------------------------------------------------------------------
TEST(EtaProjectorTest, MoveConstructionWorks) {
  const auto config = make_default_eta_config();
  EtaProjector projector(config);
  projector.update_route(make_test_route());
  projector.update_speed_profile(make_test_speed_profile());

  // Move construction
  EtaProjector p2(std::move(projector));
  EXPECT_TRUE(p2.has_route());
  EXPECT_TRUE(p2.has_profile());

  // Move assignment
  EtaProjector p3(make_default_eta_config());
  p3 = std::move(p2);
  EXPECT_TRUE(p3.has_route());
  EXPECT_TRUE(p3.has_profile());
}

}  // namespace
}  // namespace mass_l3::m3
