#pragma once
// M5 avoidance waypoint generation (Track A A3). Pure C++ geometry, unit-tested
// independently of the ROS node. Generates a waypoint string from M4's heading
// window + own-ship state that satisfies the GNC active_route_manager feasibility
// gate (see third_party/gnc_ws active_route_manager_node.cpp::evaluate_avoidance_plan):
//   - segment length >= emergency_min_segment_length_m (15 m)
//   - turn radius >= max(static_min[45m], v^2/max_lateral_accel, v/yaw_rate_limit)
//
// The straight-line projection used here has no interior turn vertices, so the
// turn-radius check is trivially satisfied (available_turn_radius = infinity).
// Segment lengths are >= 150 m (first point) and grow monotonically, both well
// above the 15 m floor. If a future iteration curves the corridor, the turn-
// radius assertion in the gtest will catch a violation.
#include <cmath>
#include <vector>

namespace mass_l3::m5 {

struct WaypointLatLon {
  double lat;
  double lon;
};

// meters-per-degree latitude on the WGS84 ellipsoid (equirectangular approx).
inline constexpr double kMetersPerDegLat = 111320.0;

// Generate a waypoint string along the avoidance heading (window midpoint,
// biased toward max for starboard preference) from own-ship position.
// Distances are projected along a single heading (no inter-waypoint turn),
// so all interior vertices are collinear and feasibility is automatic.
inline std::vector<WaypointLatLon> generate_avoidance_waypoints(
    double heading_min_deg, double heading_max_deg,
    double own_lat, double own_lon, double /*own_heading_deg*/,
    double /*target_speed_mps*/) {
  // Avoidance heading: window midpoint. Starboard preference is encoded by the
  // caller choosing heading_min/max such that the midpoint biases starboard;
  // the geometry here is heading-only so M4 remains the COLREG authority.
  const double avoid_heading_deg = (heading_min_deg + heading_max_deg) * 0.5;
  const double avoid_heading_rad = avoid_heading_deg * M_PI / 180.0;

  const double m_per_deg_lon = kMetersPerDegLat * std::cos(own_lat * M_PI / 180.0);

  // Distance ladder: first point 150 m ahead (>= emergency_min_segment_length),
  // then monotonically growing segments (>= 15 m) giving a smooth corridor.
  // Collinear points -> no turn-radius constraint at interior vertices.
  static const std::vector<double> kDistancesM = {150.0, 300.0, 500.0, 800.0, 1200.0};

  const double sin_h = std::sin(avoid_heading_rad);
  const double cos_h = std::cos(avoid_heading_rad);

  std::vector<WaypointLatLon> wps;
  wps.reserve(kDistancesM.size());
  for (double d : kDistancesM) {
    // heading measured clockwise from north: north component = d*cos, east = d*sin
    const double d_north = d * cos_h;
    const double d_east  = d * sin_h;
    wps.push_back({
      own_lat + d_north / kMetersPerDegLat,
      own_lon + d_east  / m_per_deg_lon,
    });
  }
  return wps;
}

inline std::vector<WaypointLatLon> generate_return_to_route_waypoints(
    double own_lat, double own_lon,
    double planned_route_bearing_rad,
    double route_xte_m) {
  const double m_per_deg_lon = kMetersPerDegLat * std::cos(own_lat * M_PI / 180.0);
  const double sin_b = std::sin(planned_route_bearing_rad);
  const double cos_b = std::cos(planned_route_bearing_rad);

  auto project = [&](double along_m, double xte_correction_m) {
    const double d_north = along_m * cos_b + xte_correction_m * sin_b;
    const double d_east = along_m * sin_b - xte_correction_m * cos_b;
    return WaypointLatLon{
      own_lat + d_north / kMetersPerDegLat,
      own_lon + d_east / m_per_deg_lon,
    };
  };

  return {
    {own_lat, own_lon},
    project(600.0, route_xte_m),
    project(1200.0, route_xte_m),
  };
}

}  // namespace mass_l3::m5
