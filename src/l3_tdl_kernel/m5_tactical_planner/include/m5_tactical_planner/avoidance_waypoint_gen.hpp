#pragma once
// M5 avoidance waypoint generation (Track A A3). Pure C++ geometry, unit-tested
// independently of the ROS node. Generates a waypoint string from M4's heading
// window + own-ship state that satisfies the GNC active_route_manager feasibility
// gate (see third_party/gnc_ws active_route_manager_node.cpp::evaluate_avoidance_plan):
//   - segment length >= emergency_min_segment_length_m (15 m), with a larger
//     high-speed fly-by margin for routes that run above emergency guidance cap
//   - turn radius >= max(static_min[45m], v^2/max_lateral_accel, v/yaw_rate_limit)
//
// The straight-line projection used here has no interior turn vertices, so the
// turn-radius check is trivially satisfied (available_turn_radius = infinity).
// Segment lengths are >= 150 m (first point) and grow monotonically, both well
// above the 15 m floor. If a future iteration curves the corridor, the turn-
// radius and fly-by segment assertions in the gtest will catch a violation.
#include <algorithm>
#include <cmath>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/target_corridor_clearance.hpp"

namespace mass_l3::m5 {

struct WaypointLatLon {
  double lat;
  double lon;
};

struct AlignedRouteFrame {
  double bearing_rad;
  double route_xte_m;
  bool reversed;
};

// meters-per-degree latitude on the WGS84 ellipsoid (equirectangular approx).
inline constexpr double kMetersPerDegLat = 111320.0;
inline constexpr double kGncEmergencyWaypointSwitchGateM = 90.0;
inline constexpr double kDefaultStableCorridorPeakOffsetM =
    3.0 * kGncEmergencyWaypointSwitchGateM;
inline constexpr double kRule13OvertakeCorridorPeakOffsetM =
    kDefaultStableCorridorPeakOffsetM;
inline constexpr double kRule13OvertakeInitialDoglegAngleRad =
    0.09966865249116202737;  // atan(0.10), stays under GNC turn speed gate.
inline constexpr double kDefaultNoRejoinTaperDistanceM = 1.0e12;

// W4-A target-aware corridor sizing constants.
// [Data-derived 2026-06-29, trace 20260629_000517_cs_edge_single]
inline constexpr double kCorridorLateralCapMinM = 270.0;
inline constexpr double kCorridorLateralCapMaxM = 800.0;
inline constexpr double kCorridorLateralCapIncrementM = 130.0;
inline constexpr double kTargetClearanceFloorM = 200.0;
inline constexpr double kTargetTrackHorizonS = 600.0;
inline constexpr double kTargetTrackStepS = 30.0;

inline AlignedRouteFrame align_route_frame_with_heading(
    double route_bearing_rad,
    double route_xte_m,
    double own_heading_rad) {
  const double dot = std::cos(route_bearing_rad) * std::cos(own_heading_rad)
      + std::sin(route_bearing_rad) * std::sin(own_heading_rad);
  const bool reversed = std::isfinite(dot) && dot < 0.0;
  return {
      route_bearing_rad + (reversed ? M_PI : 0.0),
      reversed ? -route_xte_m : route_xte_m,
      reversed};
}

inline double wrap_angle_pi(double angle_rad) {
  double wrapped = std::fmod(angle_rad + M_PI, 2.0 * M_PI);
  if (wrapped < 0.0) {
    wrapped += 2.0 * M_PI;
  }
  return wrapped - M_PI;
}

inline double path_dogleg_slope_from_m4_window(
    double heading_min_deg,
    double heading_max_deg,
    double route_bearing_rad,
    ColregsPreferredDirection preferred_direction) {
  const double avoid_heading_deg = (heading_min_deg + heading_max_deg) * 0.5;
  const double avoid_rel_rad = wrap_angle_pi(
      avoid_heading_deg * M_PI / 180.0 - route_bearing_rad);
  constexpr double kMaxStableGncDoglegAngleRad = 20.0 * M_PI / 180.0;
  constexpr double kMinApparentDoglegAngleRad = 30.0 * M_PI / 180.0;
  const bool directed =
      preferred_direction == ColregsPreferredDirection::Starboard ||
      preferred_direction == ColregsPreferredDirection::Port;
  double dogleg_angle = std::abs(avoid_rel_rad);
  if (directed && dogleg_angle >= kMinApparentDoglegAngleRad) {
    dogleg_angle = std::max(dogleg_angle, kMinApparentDoglegAngleRad);
  }
  dogleg_angle = std::min(dogleg_angle, kMaxStableGncDoglegAngleRad);
  return std::tan(dogleg_angle);
}

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

  // Distance ladder: first point 150 m ahead (>= emergency wheel-over distance),
  // then monotonically growing segments (>= 15 m) giving a stable long corridor.
  // Collinear points -> no turn-radius constraint at interior vertices.
  static const std::vector<double> kDistancesM = {
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};

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

inline std::vector<WaypointLatLon> generate_stable_avoidance_corridor_waypoints(
    double heading_min_deg, double heading_max_deg,
    double anchor_lat, double anchor_lon,
    double planned_route_bearing_rad,
    ColregsPreferredDirection preferred_direction = ColregsPreferredDirection::Hold,
    double max_lateral_offset_m = kDefaultStableCorridorPeakOffsetM,
    double rejoin_taper_start_m = kDefaultNoRejoinTaperDistanceM,
    double rejoin_taper_end_m = kDefaultNoRejoinTaperDistanceM + 1.0) {
  const double avoid_heading_deg = (heading_min_deg + heading_max_deg) * 0.5;
  const double avoid_heading_rad = avoid_heading_deg * M_PI / 180.0;
  const double m_per_deg_lon = kMetersPerDegLat * std::cos(anchor_lat * M_PI / 180.0);

  const double route_n = std::cos(planned_route_bearing_rad);
  const double route_e = std::sin(planned_route_bearing_rad);
  const double right_n = -std::sin(planned_route_bearing_rad);
  const double right_e = std::cos(planned_route_bearing_rad);
  const double avoid_n = std::cos(avoid_heading_rad);
  const double avoid_e = std::sin(avoid_heading_rad);
  const double avoid_lateral = avoid_n * right_n + avoid_e * right_e;
  const double default_lateral_cap = std::max(
      2.0 * kGncEmergencyWaypointSwitchGateM, std::abs(max_lateral_offset_m));
  constexpr double kDirectionVisibleDistanceM = 1500.0;
  const double min_direction_slope =
      (2.0 * kGncEmergencyWaypointSwitchGateM) / kDirectionVisibleDistanceM;
  double lateral_cap = default_lateral_cap;
  double lateral_sign = (avoid_lateral < 0.0) ? -1.0 : 1.0;
  double lateral_slope = path_dogleg_slope_from_m4_window(
      heading_min_deg, heading_max_deg, planned_route_bearing_rad, preferred_direction);
  if (preferred_direction == ColregsPreferredDirection::Starboard) {
    lateral_sign = 1.0;
    if (lateral_slope < min_direction_slope) {
      lateral_cap = 2.0 * kGncEmergencyWaypointSwitchGateM;
    }
    lateral_slope = std::max(lateral_slope, min_direction_slope);
  } else if (preferred_direction == ColregsPreferredDirection::Port) {
    lateral_sign = -1.0;
    if (lateral_slope < min_direction_slope) {
      lateral_cap = 2.0 * kGncEmergencyWaypointSwitchGateM;
    }
    lateral_slope = std::max(lateral_slope, min_direction_slope);
  }
  const double taper_start_m = std::max(0.0, rejoin_taper_start_m);
  const double taper_end_m = std::max(taper_start_m + 1.0, rejoin_taper_end_m);

  // Phase 2 anchor contract (spec §3.2): kDistancesM[0]=0.0 → wps[0]=anchor
  // (own ship position at plan generation). Preflight skips wps[0] via
  // has_anchor=true. Old first maneuver at 150m becomes wps[1].
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};

  std::vector<WaypointLatLon> wps;
  wps.reserve(kDistancesM.size());
  for (double d : kDistancesM) {
    const double along = d;
    const double cap_abs = std::abs(lateral_cap);
    const double approach_distance_m = cap_abs / std::max(lateral_slope, 1.0e-6);
    double lateral_abs = cap_abs *
        (1.0 - std::exp(-d / std::max(1.0, approach_distance_m)));
    double lateral = lateral_sign * lateral_abs;
    if (d > taper_start_m) {
      const double taper = std::clamp(
          (taper_end_m - d) / (taper_end_m - taper_start_m),
          0.0, 1.0);
      lateral *= taper;
    }
    const double d_north = along * route_n + lateral * right_n;
    const double d_east = along * route_e + lateral * right_e;
    wps.push_back({
      anchor_lat + d_north / kMetersPerDegLat,
      anchor_lon + d_east / m_per_deg_lon,
    });
  }
  return wps;
}

// W4-A: target-safe corridor. Grows the lateral cap until the target predicted
// track stays >= clearance floor from the corridor, then delegates geometry to
// generate_stable_avoidance_corridor_waypoints. If no cap in [cap_min, cap_max]
// clears all targets, returns the cap_max corridor (best effort); the M5
// preflight is authoritative for drop vs hold.
inline std::vector<WaypointLatLon> generate_target_safe_corridor_waypoints(
    double heading_min_deg, double heading_max_deg,
    double anchor_lat, double anchor_lon,
    double planned_route_bearing_rad,
    ColregsPreferredDirection preferred_direction,
    const std::vector<TargetTrackPoint>& targets,
    double own_n, double own_e,
    double cap_min_m = kCorridorLateralCapMinM,
    double cap_max_m = kCorridorLateralCapMaxM,
    double cap_increment_m = kCorridorLateralCapIncrementM,
    double clearance_floor_m = kTargetClearanceFloorM,
    double track_horizon_s = kTargetTrackHorizonS,
    double track_step_s = kTargetTrackStepS) {
  (void)own_n; (void)own_e;

  if (targets.empty()) {
    return generate_stable_avoidance_corridor_waypoints(
        heading_min_deg, heading_max_deg, anchor_lat, anchor_lon,
        planned_route_bearing_rad, preferred_direction, cap_min_m);
  }

  constexpr double kCorridorFarAlongM = 9000.0;
  const double route_n = std::cos(planned_route_bearing_rad);
  const double route_e = std::sin(planned_route_bearing_rad);
  const double right_n = -std::sin(planned_route_bearing_rad);
  const double right_e = std::cos(planned_route_bearing_rad);

  for (double cap = cap_min_m; cap <= cap_max_m + 1.0e-6; cap += cap_increment_m) {
    const double far_n = kCorridorFarAlongM * route_n + cap * right_n;
    const double far_e = kCorridorFarAlongM * route_e + cap * right_e;
    bool all_clear = true;
    for (const auto& tgt : targets) {
      const TargetClearanceVerdict v = evaluate_target_corridor_clearance(
          tgt, 0.0, 0.0, far_n, far_e,
          clearance_floor_m, track_horizon_s, track_step_s);
      if (!v.clear) {
        all_clear = false;
        break;
      }
    }
    if (all_clear) {
      return generate_stable_avoidance_corridor_waypoints(
          heading_min_deg, heading_max_deg, anchor_lat, anchor_lon,
          planned_route_bearing_rad, preferred_direction, cap);
    }
  }
  return generate_stable_avoidance_corridor_waypoints(
      heading_min_deg, heading_max_deg, anchor_lat, anchor_lon,
      planned_route_bearing_rad, preferred_direction, cap_max_m);
}

inline std::vector<WaypointLatLon> generate_rule13_overtake_corridor_waypoints(
    double /*heading_min_deg*/, double /*heading_max_deg*/,
    double anchor_lat, double anchor_lon,
    double planned_route_bearing_rad,
    ColregsPreferredDirection preferred_direction = ColregsPreferredDirection::Starboard,
    double rejoin_taper_start_m = kDefaultNoRejoinTaperDistanceM,
    double rejoin_taper_end_m = kDefaultNoRejoinTaperDistanceM + 1.0) {
  const double m_per_deg_lon = kMetersPerDegLat * std::cos(anchor_lat * M_PI / 180.0);
  const double route_n = std::cos(planned_route_bearing_rad);
  const double route_e = std::sin(planned_route_bearing_rad);
  const double right_n = -std::sin(planned_route_bearing_rad);
  const double right_e = std::cos(planned_route_bearing_rad);
  double lateral_sign = 1.0;
  if (preferred_direction == ColregsPreferredDirection::Starboard) {
    lateral_sign = 1.0;
  } else if (preferred_direction == ColregsPreferredDirection::Port) {
    lateral_sign = -1.0;
  }

  const double taper_start_m = std::max(0.0, rejoin_taper_start_m);
  const double taper_end_m = std::max(taper_start_m + 1.0, rejoin_taper_end_m);
  // Phase 2 anchor contract (spec §3.2): kDistancesM[0]=0.0 → wps[0]=anchor.
  // Preflight skips wps[0] via has_anchor=true.
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor
      600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
      8400.0, 10000.0, 12000.0};

  std::vector<WaypointLatLon> wps;
  wps.reserve(kDistancesM.size());
  for (double d : kDistancesM) {
    double lateral_abs = std::min(kRule13OvertakeCorridorPeakOffsetM, d * 0.10);
    if (d > taper_start_m) {
      const double taper = std::clamp(
          (taper_end_m - d) / (taper_end_m - taper_start_m),
          0.0, 1.0);
      lateral_abs *= taper;
    }
    const double lateral = lateral_sign * lateral_abs;
    const double d_north = d * route_n + lateral * right_n;
    const double d_east = d * route_e + lateral * right_e;
    wps.push_back({
      anchor_lat + d_north / kMetersPerDegLat,
      anchor_lon + d_east / m_per_deg_lon,
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
    project(500.0, 0.0),
    project(1200.0, route_xte_m * 0.15),
    project(2200.0, route_xte_m * 0.55),
    project(3500.0, route_xte_m),
  };
}

}  // namespace mass_l3::m5
