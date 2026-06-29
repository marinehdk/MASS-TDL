// TDD unit tests for M5 avoidance waypoint generation (Track A A3).
// Verifies the generated waypoint string satisfies the GNC active_route_manager
// feasibility gate, mirroring its evaluate_avoidance_plan checks:
//   - segment length >= 15 m (emergency_min_segment_length_m)
//   - turn radius >= max(45 m, v^2/0.25, v/yaw_rate_limit) at interior vertices
//   - first point >= 150 m from own-ship (avoids projection artefacts)
//   - high-speed fly-by segments leave at least 3x wheel-over distance
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/avoidance_waypoint_policy.hpp"
#include "m5_tactical_planner/gnc_avoidance_preflight.hpp"
#include "m5_tactical_planner/target_corridor_clearance.hpp"

using mass_l3::m5::generate_avoidance_waypoints;
using mass_l3::m5::generate_return_to_route_waypoints;
using mass_l3::m5::generate_rule13_overtake_corridor_waypoints;
using mass_l3::m5::generate_stable_avoidance_corridor_waypoints;
using mass_l3::m5::generate_target_safe_corridor_waypoints;
using mass_l3::m5::gnc_avoidance_command_speed_mps;
using mass_l3::m5::gnc_avoidance_navigation_mode;
using mass_l3::m5::gnc_emergency_command_speed_mps;
using mass_l3::m5::gnc_return_command_speed_mps;
using mass_l3::m5::gnc_return_navigation_mode;
using mass_l3::m5::requires_colregs_overtake_corridor;
using mass_l3::m5::required_decel_distance_m;
using mass_l3::m5::required_turn_radius_m;
using mass_l3::m5::should_emit_collision_avoidance_waypoints;
using mass_l3::m5::validate_gnc_avoidance_plan;
using mass_l3::m5::align_route_frame_with_heading;
using mass_l3::m5::kDefaultStableCorridorPeakOffsetM;
using mass_l3::m5::kGncEmergencyWaypointSwitchGateM;
using mass_l3::m5::kMetersPerDegLat;
using mass_l3::m5::kRule13OvertakeInitialDoglegAngleRad;
using mass_l3::m5::kRule13OvertakeCorridorPeakOffsetM;
using mass_l3::m5::ColregsPreferredDirection;

namespace {

double distance_m(const mass_l3::m5::WaypointLatLon& a,
                  const mass_l3::m5::WaypointLatLon& b,
                  double ref_lat) {
  const double dlat = (a.lat - b.lat) * kMetersPerDegLat;
  const double dlon = (a.lon - b.lon) * kMetersPerDegLat * std::cos(ref_lat * M_PI / 180.0);
  return std::hypot(dlat, dlon);
}

// Mirror of active_route_manager_node available_turn_radius (equirectangular).
double available_turn_radius(const mass_l3::m5::WaypointLatLon& a,
                             const mass_l3::m5::WaypointLatLon& b,
                             const mass_l3::m5::WaypointLatLon& c,
                             double ref_lat) {
  const double m_per_deg_lon = kMetersPerDegLat * std::cos(ref_lat * M_PI / 180.0);
  struct LocalXY {
    double x;
    double y;
  };
  auto to_xy = [&](const mass_l3::m5::WaypointLatLon& p) {
    return LocalXY{p.lat * kMetersPerDegLat, p.lon * m_per_deg_lon};
  };
  const auto a_xy = to_xy(a);
  const auto b_xy = to_xy(b);
  const auto c_xy = to_xy(c);
  const double v1x = b_xy.x - a_xy.x, v1y = b_xy.y - a_xy.y;
  const double v2x = c_xy.x - b_xy.x, v2y = c_xy.y - b_xy.y;
  const double l1 = std::hypot(v1x, v1y);
  const double l2 = std::hypot(v2x, v2y);
  if (l1 < 1e-9 || l2 < 1e-9) return 0.0;
  double dot = (v1x * v2x + v1y * v2y) / (l1 * l2);
  dot = std::max(-1.0, std::min(1.0, dot));
  const double angle = std::acos(dot);
  if (angle < 1.0 * M_PI / 180.0) return 1.0e18;  // collinear -> effectively infinite
  return std::min(l1, l2) / std::tan(angle * 0.5);
}

double route_lateral_offset_m(const mass_l3::m5::WaypointLatLon& origin,
                              const mass_l3::m5::WaypointLatLon& point,
                              double route_bearing_rad) {
  const double north = (point.lat - origin.lat) * kMetersPerDegLat;
  const double east = (point.lon - origin.lon) * kMetersPerDegLat *
      std::cos(origin.lat * M_PI / 180.0);
  const double right_n = -std::sin(route_bearing_rad);
  const double right_e = std::cos(route_bearing_rad);
  return north * right_n + east * right_e;
}

double route_along_offset_m(const mass_l3::m5::WaypointLatLon& origin,
                            const mass_l3::m5::WaypointLatLon& point,
                            double route_bearing_rad) {
  const double north = (point.lat - origin.lat) * kMetersPerDegLat;
  const double east = (point.lon - origin.lon) * kMetersPerDegLat *
      std::cos(origin.lat * M_PI / 180.0);
  const double route_n = std::cos(route_bearing_rad);
  const double route_e = std::sin(route_bearing_rad);
  return north * route_n + east * route_e;
}

double route_relative_path_angle_deg(const mass_l3::m5::WaypointLatLon& origin,
                                     const mass_l3::m5::WaypointLatLon& point,
                                     double route_bearing_rad) {
  return std::atan2(route_lateral_offset_m(origin, point, route_bearing_rad),
                    route_along_offset_m(origin, point, route_bearing_rad)) *
      180.0 / M_PI;
}

}  // namespace

TEST(AvoidanceWaypointGen, ProducesAtLeastFourPoints) {
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  ASSERT_GE(wps.size(), 4u);
}

TEST(AvoidanceWaypointGen, FirstWaypointAtLeast150mAhead) {
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  ASSERT_FALSE(wps.empty());
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  EXPECT_GE(distance_m(origin, wps[0], 0.0), 150.0);
}

TEST(AvoidanceWaypointGen, EverySegmentAtLeast15m) {
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  for (size_t i = 0; i + 1 < wps.size(); ++i) {
    const double seg = distance_m(wps[i], wps[i + 1], 0.0);
    EXPECT_GE(seg, 15.0) << "segment " << i << " only " << seg << " m";
  }
}

TEST(AvoidanceWaypointGen, TurnRadiusFeasibleAtAllInteriorVertices) {
  // GNC: required_radius = max(static_min[45 m], v^2/0.25, v/yaw_rate_limit).
  // At 3 m/s: v^2/0.25 = 36 m, so 45 m dominates. Straight projection has no
  // turn -> available radius is infinite; this guards a future curved corridor.
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  constexpr double kMinRequired = 45.0;
  for (size_t i = 1; i + 1 < wps.size(); ++i) {
    const double avail = available_turn_radius(wps[i - 1], wps[i], wps[i + 1], 0.0);
    EXPECT_GE(avail, kMinRequired) << "vertex " << i << " radius " << avail << " < 45 m";
  }
}

TEST(AvoidanceWaypointGen, StarboardHeadingGivesEastwardProjection) {
  // Midpoint of [45, 90] = 67.5 deg -> east component positive (starboard of north).
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  ASSERT_FALSE(wps.empty());
  EXPECT_GT(wps[0].lon, 0.0);
}

TEST(AvoidanceWaypointGen, PointsCollinearForStraightProjection) {
  // Documents the design invariant: straight projection -> no interior turn.
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  for (size_t i = 1; i + 1 < wps.size(); ++i) {
    const double avail = available_turn_radius(wps[i - 1], wps[i], wps[i + 1], 0.0);
    EXPECT_GT(avail, 1.0e9) << "vertex " << i << " is not collinear";
  }
}

TEST(AvoidanceWaypointGen, EmergencySpeedCapMatchesGuidanceEnvelope) {
  EXPECT_NEAR(gnc_emergency_command_speed_mps(6.0), 3.2, 1e-9);
  EXPECT_NEAR(gnc_emergency_command_speed_mps(2.4), 2.4, 1e-9);
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorKeepsPlannedSpeed) {
  const std::vector<std::uint8_t> rules{13u, 16u, 18u};
  EXPECT_TRUE(requires_colregs_overtake_corridor(true, rules));
  EXPECT_NEAR(gnc_avoidance_command_speed_mps(7.2, true), 7.2, 1e-9);
  EXPECT_STREQ(gnc_avoidance_navigation_mode(true), "colregs_overtake");
}

TEST(AvoidanceWaypointGen, Rule13RejoinKeepsProtectedPlannedSpeed) {
  EXPECT_NEAR(gnc_return_command_speed_mps(7.2, true), 7.2, 1e-9);
  EXPECT_STREQ(gnc_return_navigation_mode(true), "colregs_overtake");
  EXPECT_NEAR(gnc_return_command_speed_mps(7.2, false), 3.2, 1e-9);
  EXPECT_STREQ(gnc_return_navigation_mode(false), "emergency_avoidance");
}

TEST(AvoidanceWaypointGen, Rule13RejoinRoutePassesGncPreflightAtPlannedSpeed) {
  const mass_l3::m5::WaypointLatLon origin{63.4953, 10.3860};
  const auto wps = generate_return_to_route_waypoints(
      origin.lat, origin.lon, /*planned_route_bearing_rad=*/0.0, /*route_xte_m=*/300.0);
  const std::vector<double> speeds(wps.size(), gnc_return_command_speed_mps(7.2, true));
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason
      << " required=" << result.required_m
      << " available=" << result.available_m;
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorPassesGncPreflightAtPlannedSpeed) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto speed = gnc_avoidance_command_speed_mps(7.2, true);
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, 0.0,
      ColregsPreferredDirection::Starboard, 7500.0, 12000.0);
  const std::vector<double> speeds(wps.size(), speed);
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason
      << " required=" << result.required_m
      << " available=" << result.available_m;
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorUsesDefaultEnvelopeAndInitialKick) {
  EXPECT_DOUBLE_EQ(kRule13OvertakeCorridorPeakOffsetM, kDefaultStableCorridorPeakOffsetM);
  EXPECT_LT(kRule13OvertakeInitialDoglegAngleRad * 180.0 / M_PI, 6.0);
  EXPECT_GE(kRule13OvertakeCorridorPeakOffsetM, 3.0 * kGncEmergencyWaypointSwitchGateM);
  EXPECT_LE(kRule13OvertakeCorridorPeakOffsetM, 500.0);
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorHoldsLateralClearanceThroughPass) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, 0.0,
      ColregsPreferredDirection::Starboard, 7500.0, 12000.0);
  ASSERT_GT(wps.size(), 9u);
  for (const auto& wp : wps) {
    if (route_along_offset_m(origin, wp, 0.0) >= 3000.0 &&
        route_along_offset_m(origin, wp, 0.0) <= 7500.0) {
      EXPECT_GT(route_lateral_offset_m(origin, wp, 0.0), 180.0);
    }
  }
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorEncodesInitialApparentActionForGnc) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, 0.0,
      ColregsPreferredDirection::Starboard, 7500.0, 12000.0);
  ASSERT_GE(wps.size(), 10u);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.front(), 0.0), 60.0, 0.5);
  EXPECT_GT(route_relative_path_angle_deg(origin, wps.front(), 0.0), 5.0);
  EXPECT_LT(route_relative_path_angle_deg(origin, wps.front(), 0.0), 6.0);
  EXPECT_LT(route_relative_path_angle_deg(origin, wps[1], 0.0), 6.0);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps[3], 0.0),
              kRule13OvertakeCorridorPeakOffsetM, 0.5);
  EXPECT_LT(route_lateral_offset_m(origin, wps.back(), 0.0),
            kRule13OvertakeCorridorPeakOffsetM);
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorAvoidsInitialRawRouteRejoin) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, 0.0,
      ColregsPreferredDirection::Starboard, 7500.0, 12000.0);
  ASSERT_GE(wps.size(), 2u);
  mass_l3::m5::GncAvoidancePreflightConfig cfg{};
  const double initial_raw_xte =
      mass_l3::m5::gnc_cross_track_to_segment_m(origin, wps[0], wps[1], origin.lat);
  EXPECT_LE(initial_raw_xte, cfg.raw_route_rejoin_threshold_m);
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorKeepsHighSpeedFlyBySegmentsLongEnough) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto speed = gnc_avoidance_command_speed_mps(7.2, true);
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, 0.0,
      ColregsPreferredDirection::Starboard, 7500.0, 12000.0);
  ASSERT_GT(wps.size(), 2u);
  mass_l3::m5::GncAvoidancePreflightConfig cfg{};
  EXPECT_GE(distance_m(origin, wps.front(), origin.lat),
            cfg.high_speed_flyby_min_segment_m);
  for (std::size_t i = 0; i + 1 < wps.size(); ++i) {
    EXPECT_GE(distance_m(wps[i], wps[i + 1], origin.lat),
              cfg.high_speed_flyby_min_segment_m) << "segment " << i;
  }
  const std::vector<double> speeds(wps.size(), speed);
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds, cfg);
  EXPECT_TRUE(result.feasible) << result.reason
      << " required=" << result.required_m
      << " available=" << result.available_m;
}

TEST(AvoidanceWaypointGen, PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {500.0 / kMetersPerDegLat, 0.0},
      {800.0 / kMetersPerDegLat, 0.0},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {7.2, 7.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "flyby_segment_too_short");
}

TEST(AvoidanceWaypointGen, PreflightRejectsHighSpeedInitialRawRouteXte) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const double lon_270m = 270.0 / kMetersPerDegLat;
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {600.0 / kMetersPerDegLat, lon_270m},
      {1200.0 / kMetersPerDegLat, lon_270m},
      {2000.0 / kMetersPerDegLat, lon_270m},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {7.2, 7.2, 7.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "initial_raw_route_xte_too_large");
}

TEST(AvoidanceWaypointGen, NonRule13AvoidanceKeepsEmergencySpeedEnvelope) {
  const std::vector<std::uint8_t> rules{14u, 16u, 18u};
  EXPECT_FALSE(requires_colregs_overtake_corridor(true, rules));
  EXPECT_NEAR(gnc_avoidance_command_speed_mps(7.2, false), 3.2, 1e-9);
  EXPECT_STREQ(gnc_avoidance_navigation_mode(false), "emergency_avoidance");
}

TEST(AvoidanceWaypointGen, RequiredTurnRadiusMirrorsGncYawRateGate) {
  EXPECT_NEAR(required_turn_radius_m(3.2), 91.673, 0.01);
  EXPECT_NEAR(required_turn_radius_m(8.0), 256.0, 1e-9);
}

TEST(AvoidanceWaypointGen, RequiredDecelDistanceMirrorsGncGate) {
  EXPECT_NEAR(required_decel_distance_m(8.0, 3.2), 336.0, 1e-9);
  EXPECT_DOUBLE_EQ(required_decel_distance_m(3.2, 8.0), 0.0);
}

TEST(AvoidanceWaypointGen, GeneratedAvoidanceCorridorPassesGncPreflight) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto speed = gnc_emergency_command_speed_mps(6.0);
  const auto wps = generate_avoidance_waypoints(
      45.0, 90.0, origin.lat, origin.lon, 0.0, speed);
  const std::vector<double> speeds(wps.size(), speed);
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason;
}

TEST(AvoidanceWaypointGen, StableCorridorPassesGncPreflight) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto speed = gnc_emergency_command_speed_mps(6.0);
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      45.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  const std::vector<double> speeds(wps.size(), speed);
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason;
}

TEST(AvoidanceWaypointGen, CollisionAvoidanceWaypointsWaitForM4AvoidanceAuthority) {
  EXPECT_FALSE(should_emit_collision_avoidance_waypoints(
      true, l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT));
  EXPECT_FALSE(should_emit_collision_avoidance_waypoints(
      true, l3_msgs::msg::BehaviorPlan::BEHAVIOR_RECOVERY));
  EXPECT_TRUE(should_emit_collision_avoidance_waypoints(
      true, l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID));
  EXPECT_FALSE(should_emit_collision_avoidance_waypoints(
      false, l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID));
}

TEST(AvoidanceWaypointGen, StableCorridorStaysInsideRouteCorridorContract) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      45.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_FALSE(wps.empty());
  for (const auto& wp : wps) {
    EXPECT_LE(std::abs(route_lateral_offset_m(origin, wp, route_bearing_rad)), 500.1);
  }
  EXPECT_GT(route_along_offset_m(origin, wps.back(), route_bearing_rad), 3000.0);
}

TEST(AvoidanceWaypointGen, StableCorridorKeepsStarboardLateralIntent) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      45.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_FALSE(wps.empty());
  EXPECT_GT(route_lateral_offset_m(origin, wps.front(), route_bearing_rad), 0.0);
  std::vector<double> lateral_offsets;
  lateral_offsets.reserve(wps.size());
  for (const auto& wp : wps) {
    lateral_offsets.push_back(route_lateral_offset_m(origin, wp, route_bearing_rad));
  }
  const auto peak = *std::max_element(lateral_offsets.begin(), lateral_offsets.end());
  EXPECT_NEAR(peak, kDefaultStableCorridorPeakOffsetM, 0.2);
  EXPECT_NEAR(lateral_offsets.back(), kDefaultStableCorridorPeakOffsetM, 0.2);
}

TEST(AvoidanceWaypointGen, DefaultStableCorridorKeepsThreeGncSwitchGateReserve) {
  EXPECT_GE(kDefaultStableCorridorPeakOffsetM, 3.0 * kGncEmergencyWaypointSwitchGateM);
  EXPECT_LT(kDefaultStableCorridorPeakOffsetM, 4.0 * kGncEmergencyWaypointSwitchGateM);
  EXPECT_LE(kDefaultStableCorridorPeakOffsetM, 500.0);
}

TEST(AvoidanceWaypointGen, StableCorridorHonorsStarboardIntentWhenHeadingWindowNearRoute) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      -1.0, 1.0, origin.lat, origin.lon, route_bearing_rad,
      ColregsPreferredDirection::Starboard);
  ASSERT_FALSE(wps.empty());
  std::vector<double> lateral_offsets;
  lateral_offsets.reserve(wps.size());
  for (const auto& wp : wps) {
    lateral_offsets.push_back(route_lateral_offset_m(origin, wp, route_bearing_rad));
  }
  const auto peak = *std::max_element(lateral_offsets.begin(), lateral_offsets.end());
  EXPECT_GT(lateral_offsets.front(), 0.0);
  EXPECT_NEAR(peak, 2.0 * kGncEmergencyWaypointSwitchGateM, 0.6);
}

TEST(AvoidanceWaypointGen, StableCorridorHonorsPortIntentWhenHeadingWindowNearRoute) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      -1.0, 1.0, origin.lat, origin.lon, route_bearing_rad,
      ColregsPreferredDirection::Port);
  ASSERT_FALSE(wps.empty());
  std::vector<double> lateral_offsets;
  lateral_offsets.reserve(wps.size());
  for (const auto& wp : wps) {
    lateral_offsets.push_back(route_lateral_offset_m(origin, wp, route_bearing_rad));
  }
  const auto peak = *std::min_element(lateral_offsets.begin(), lateral_offsets.end());
  EXPECT_LT(lateral_offsets.front(), 0.0);
  EXPECT_NEAR(peak, -2.0 * kGncEmergencyWaypointSwitchGateM, 0.6);
}

TEST(AvoidanceWaypointGen, StableCorridorKeepsLongitudinalLadderForGncFlyBy) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      60.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_GE(wps.size(), 3u);
  EXPECT_NEAR(route_along_offset_m(origin, wps[0], route_bearing_rad), 150.0, 0.2);
  EXPECT_NEAR(route_along_offset_m(origin, wps[1], route_bearing_rad), 300.0, 0.2);
  EXPECT_NEAR(route_along_offset_m(origin, wps[2], route_bearing_rad), 600.0, 0.2);
}

TEST(AvoidanceWaypointGen, StableCorridorFirstPointClearsGncSwitchGateAndCorridorCap) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      60.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_GE(wps.size(), 3u);
  EXPECT_GT(distance_m(origin, wps.front(), origin.lat), kGncEmergencyWaypointSwitchGateM);
  EXPECT_LE(
      std::abs(route_lateral_offset_m(origin, wps[0], route_bearing_rad)),
      kDefaultStableCorridorPeakOffsetM);
  EXPECT_LE(
      std::abs(route_lateral_offset_m(origin, wps[1], route_bearing_rad)),
      kDefaultStableCorridorPeakOffsetM);
}

TEST(AvoidanceWaypointGen, StableCorridorUsesBoundedGncVisibleDogleg) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      49.0, 79.0, origin.lat, origin.lon, route_bearing_rad,
      ColregsPreferredDirection::Starboard);
  ASSERT_FALSE(wps.empty());
  const double first_leg_deg =
      route_relative_path_angle_deg(origin, wps.front(), route_bearing_rad);
  EXPECT_GT(first_leg_deg, 15.0);
  EXPECT_LE(first_leg_deg, 20.1);
  const std::vector<double> speeds(wps.size(), gnc_emergency_command_speed_mps(6.0));
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason;
}

TEST(AvoidanceWaypointGen, DefaultStableCorridorHoldsLateralClearanceUntilRelease) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      60.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_GE(wps.size(), 10u);
  EXPECT_GT(route_lateral_offset_m(origin, wps[5], route_bearing_rad),
            2.0 * kGncEmergencyWaypointSwitchGateM);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps[8], route_bearing_rad),
              kDefaultStableCorridorPeakOffsetM, 1.0);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.back(), route_bearing_rad),
              kDefaultStableCorridorPeakOffsetM, 0.2);
}

TEST(AvoidanceWaypointGen, StableCorridorApproachesLateralClearanceSmoothly) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      60.0, 90.0, origin.lat, origin.lon, route_bearing_rad);
  ASSERT_GE(wps.size(), 7u);
  std::vector<double> lateral_offsets;
  lateral_offsets.reserve(wps.size());
  for (const auto& wp : wps) {
    lateral_offsets.push_back(route_lateral_offset_m(origin, wp, route_bearing_rad));
  }
  EXPECT_GT(lateral_offsets[0], 0.0);
  for (std::size_t i = 1; i <= 6; ++i) {
    EXPECT_GT(lateral_offsets[i], lateral_offsets[i - 1]) << "index " << i;
  }
  EXPECT_LT(lateral_offsets[3], kDefaultStableCorridorPeakOffsetM - 50.0);
  EXPECT_NEAR(lateral_offsets.back(), kDefaultStableCorridorPeakOffsetM, 0.2);
}

TEST(AvoidanceWaypointGen, AlignsReversedRouteFrameWithOwnHeading) {
  const auto frame = align_route_frame_with_heading(M_PI, 420.0, 0.0);
  EXPECT_TRUE(frame.reversed);
  EXPECT_NEAR(std::cos(frame.bearing_rad), 1.0, 1e-12);
  EXPECT_NEAR(std::sin(frame.bearing_rad), 0.0, 1e-12);
  EXPECT_NEAR(frame.route_xte_m, -420.0, 1e-12);
}

TEST(AvoidanceWaypointGen, ReversedRouteFrameStillKeepsStarboardCorridorEastward) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto frame = align_route_frame_with_heading(M_PI, 0.0, 0.0);
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      60.0, 90.0, origin.lat, origin.lon, frame.bearing_rad);
  ASSERT_FALSE(wps.empty());
  EXPECT_GT(route_lateral_offset_m(origin, wps.front(), frame.bearing_rad), 0.0);
  EXPECT_GT(wps.front().lon, origin.lon);
}

TEST(AvoidanceWaypointGen, ReversedRouteFrameKeepsReturnCorrectionInSameFrame) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto frame = align_route_frame_with_heading(M_PI, 500.0, 0.0);
  const auto wps = generate_return_to_route_waypoints(
      origin.lat, origin.lon, frame.bearing_rad, frame.route_xte_m);
  ASSERT_EQ(wps.size(), 4u);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.back(), frame.bearing_rad), 500.0, 0.2);
  EXPECT_GT(route_along_offset_m(origin, wps.front(), frame.bearing_rad), 120.0);
}

TEST(AvoidanceWaypointGen, PreflightRejectsFirstManeuverPointInsideWheelOverDistance) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const double lon_50m = 50.0 / kMetersPerDegLat;
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {0.0, lon_50m},
      {0.0, lon_50m * 2.0},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {3.2, 3.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "first_maneuver_point_too_close");
}

TEST(AvoidanceWaypointGen, AvoidanceCorridorExtendsBeyondShortRollingSegment) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const auto wps = generate_avoidance_waypoints(45.0, 90.0, origin.lat, origin.lon, 0.0, 3.2);
  ASSERT_FALSE(wps.empty());
  EXPECT_GE(distance_m(origin, wps.back(), origin.lat), 6000.0);
}

TEST(AvoidanceWaypointGen, ReturnRouteStartsAheadOfOwnShipAndTargetsRouteCenterline) {
  const auto wps = generate_return_to_route_waypoints(
      0.0, 0.0, 0.0, 500.0);
  ASSERT_EQ(wps.size(), 4u);
  EXPECT_NEAR(wps[0].lat * kMetersPerDegLat, 500.0, 1e-6);
  EXPECT_NEAR(wps[0].lon * kMetersPerDegLat, 0.0, 1e-6);
  EXPECT_NEAR(wps[1].lat * kMetersPerDegLat, 1200.0, 1e-6);
  EXPECT_NEAR(wps[1].lon * kMetersPerDegLat, -75.0, 1e-6);
  EXPECT_NEAR(wps[2].lat * kMetersPerDegLat, 2200.0, 1e-6);
  EXPECT_NEAR(wps[2].lon * kMetersPerDegLat, -275.0, 1e-6);
  EXPECT_NEAR(wps[3].lat * kMetersPerDegLat, 3500.0, 1e-6);
  EXPECT_NEAR(wps[3].lon * kMetersPerDegLat, -500.0, 1e-6);
}

TEST(AvoidanceWaypointGen, ReturnRouteFirstPointSatisfiesGncUpdateFutureGuard) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto wps = generate_return_to_route_waypoints(
      origin.lat, origin.lon, route_bearing_rad, 500.0);
  ASSERT_FALSE(wps.empty());
  EXPECT_GT(route_along_offset_m(origin, wps.front(), route_bearing_rad), 120.0);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.front(), route_bearing_rad), 0.0, 0.2);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.back(), route_bearing_rad), -500.0, 0.2);
}

TEST(AvoidanceWaypointGen, ShortReturnRoutePassesGncPreflightAtCorridorLimit) {
  const mass_l3::m5::WaypointLatLon origin{63.44, 10.38};
  const double route_bearing_rad = 0.0;
  const auto speed = gnc_emergency_command_speed_mps(6.0);
  const auto wps = generate_return_to_route_waypoints(
      origin.lat, origin.lon, route_bearing_rad, 500.0);
  ASSERT_EQ(wps.size(), 4U);
  const std::vector<double> speeds(wps.size(), speed);
  const auto result = validate_gnc_avoidance_plan(origin, wps, speeds);
  EXPECT_TRUE(result.feasible) << result.reason;
  EXPECT_GT(route_along_offset_m(origin, wps.front(), route_bearing_rad), 120.0);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.front(), route_bearing_rad), 0.0, 0.2);
  EXPECT_NEAR(route_lateral_offset_m(origin, wps.back(), route_bearing_rad), -500.0, 0.2);
}

TEST(AvoidanceWaypointGen, ReturnRouteSegmentsAndTurnAreFeasible) {
  const auto wps = generate_return_to_route_waypoints(
      63.44, 10.38, 0.0, 500.0);
  for (size_t i = 0; i + 1 < wps.size(); ++i) {
    EXPECT_GE(distance_m(wps[i], wps[i + 1], 63.44), 30.0);
  }
  for (size_t i = 1; i + 1 < wps.size(); ++i) {
    const double avail = available_turn_radius(wps[i - 1], wps[i], wps[i + 1], 63.44);
    EXPECT_GE(avail, required_turn_radius_m(3.2)) << "vertex " << i;
  }
}

TEST(GenerateTargetSafeCorridor, KeepsDefaultCapWhenNoTargets) {
  std::vector<mass_l3::m5::TargetTrackPoint> no_targets;
  const auto wps = mass_l3::m5::generate_target_safe_corridor_waypoints(
      40.0, 80.0, 63.44, 10.38, 0.0,
      mass_l3::m5::ColregsPreferredDirection::Starboard,
      no_targets, 0.0, 0.0);
  ASSERT_FALSE(wps.empty());
  double max_east = 0.0;
  const double m_per_deg_lon = mass_l3::m5::kMetersPerDegLat * std::cos(63.44 * M_PI / 180.0);
  for (const auto& w : wps) {
    max_east = std::max(max_east, (w.lon - 10.38) * m_per_deg_lon);
  }
  EXPECT_LT(max_east, 320.0);
}

TEST(GenerateTargetSafeCorridor, GrowsCapWhenTargetCrossesCorridor) {
  std::vector<mass_l3::m5::TargetTrackPoint> targets;
  targets.push_back({0.0, 100.0, 45.0 * M_PI / 180.0, 8.0});
  const auto wps = mass_l3::m5::generate_target_safe_corridor_waypoints(
      40.0, 80.0, 63.44, 10.38, 0.0,
      mass_l3::m5::ColregsPreferredDirection::Starboard,
      targets, 0.0, 0.0);
  double max_east = 0.0;
  const double m_per_deg_lon = mass_l3::m5::kMetersPerDegLat * std::cos(63.44 * M_PI / 180.0);
  for (const auto& w : wps) {
    max_east = std::max(max_east, (w.lon - 10.38) * m_per_deg_lon);
  }
  EXPECT_GT(max_east, 320.0);
}

TEST(GenerateTargetSafeCorridor, HonorsCapMax) {
  std::vector<mass_l3::m5::TargetTrackPoint> targets;
  targets.push_back({0.0, 0.0, 90.0 * M_PI / 180.0, 20.0});
  const auto wps = mass_l3::m5::generate_target_safe_corridor_waypoints(
      40.0, 80.0, 63.44, 10.38, 0.0,
      mass_l3::m5::ColregsPreferredDirection::Starboard,
      targets, 0.0, 0.0);
  double max_east = 0.0;
  const double m_per_deg_lon = mass_l3::m5::kMetersPerDegLat * std::cos(63.44 * M_PI / 180.0);
  for (const auto& w : wps) {
    max_east = std::max(max_east, (w.lon - 10.38) * m_per_deg_lon);
  }
  EXPECT_LT(max_east, 850.0);
}
