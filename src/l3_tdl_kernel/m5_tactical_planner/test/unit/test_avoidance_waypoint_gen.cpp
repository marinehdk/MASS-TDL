// TDD unit tests for M5 avoidance waypoint generation (Track A A3).
// Verifies the generated waypoint string satisfies the GNC active_route_manager
// feasibility gate, mirroring its evaluate_avoidance_plan checks:
//   - segment length >= 15 m (emergency_min_segment_length_m)
//   - turn radius >= max(45 m, v^2/0.25, v/yaw_rate_limit) at interior vertices
//   - first point >= 150 m from own-ship (avoids projection artefacts)
#include <gtest/gtest.h>

#include <cmath>

#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"

using mass_l3::m5::generate_avoidance_waypoints;
using mass_l3::m5::kMetersPerDegLat;

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
  auto to_xy = [&](const mass_l3::m5::WaypointLatLon& p) {
    return std::make_pair(p.lat * kMetersPerDegLat, p.lon * m_per_deg_lon);
  };
  auto [ax, ay] = to_xy(a);
  auto [bx, by] = to_xy(b);
  auto [cx, cy] = to_xy(c);
  const double v1x = bx - ax, v1y = by - ay;
  const double v2x = cx - bx, v2y = cy - by;
  const double l1 = std::hypot(v1x, v1y);
  const double l2 = std::hypot(v2x, v2y);
  if (l1 < 1e-9 || l2 < 1e-9) return 0.0;
  double dot = (v1x * v2x + v1y * v2y) / (l1 * l2);
  dot = std::max(-1.0, std::min(1.0, dot));
  const double angle = std::acos(dot);
  if (angle < 1.0 * M_PI / 180.0) return 1.0e18;  // collinear -> effectively infinite
  return std::min(l1, l2) / std::tan(angle * 0.5);
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
