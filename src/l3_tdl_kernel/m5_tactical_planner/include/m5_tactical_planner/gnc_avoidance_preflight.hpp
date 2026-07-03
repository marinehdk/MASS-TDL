#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"

namespace mass_l3::m5 {

struct GncAvoidancePreflightConfig {
  // Emergency-avoidance profile (default). M5 publishes avoidance routes under
  // emergency semantics (COLREGs maneuver), so the preflight must use the
  // vessel's EMERGENCY maneuvering envelope, not the nominal cruise envelope.
  // The previous defaults (a_lat=0.25 m/s², yaw=2.0 deg/s) were nominal-like
  // and over-conservative for emergency: at v=8 m/s they demanded a 256 m
  // turn radius, rejecting valid avoidance geometries (Codex review 2026-07-03,
  // OVER-CONSERVATIVE). [TBD-HAZID] confirm against live GNC ODD / sea-trial
  // data; emergency capability is typically 2× nominal.
  double max_command_speed_mps{8.0};
  double emergency_min_segment_length_m{15.0};
  double emergency_min_turn_radius_m{45.0};
  double max_lateral_accel_mps2{0.5};       // was 0.25 (nominal); emergency envelope
  double emergency_max_yaw_rate_deg_s{3.5};  // was 2.0 (nominal); emergency envelope
  double max_decel_mps2{0.08};
  double emergency_guidance_speed_cap_mps{3.2};
  double emergency_wheel_over_distance_m{120.0};
  double high_speed_flyby_min_segment_m{360.0};
  double raw_route_rejoin_threshold_m{60.0};
};

// Nominal/cruise profile for L2 route suffix preflight (not avoidance).
// M5 uses this when appending the L2 nominal suffix to a committed route;
// the avoidance segments themselves use the emergency default above.
inline GncAvoidancePreflightConfig nominal_cruise_preflight_config() {
  GncAvoidancePreflightConfig cfg;
  cfg.max_lateral_accel_mps2 = 0.25;
  cfg.emergency_max_yaw_rate_deg_s = 2.0;
  return cfg;
}

struct GncAvoidancePreflightResult {
  bool feasible{false};
  std::string reason{"not_checked"};
  std::size_t index{0U};
  double required_m{0.0};
  double available_m{0.0};
};

inline double gnc_distance_m(
    const WaypointLatLon& a,
    const WaypointLatLon& b,
    double ref_lat_deg) {
  const double dlat = (b.lat - a.lat) * kMetersPerDegLat;
  const double dlon =
      (b.lon - a.lon) * kMetersPerDegLat * std::cos(ref_lat_deg * M_PI / 180.0);
  return std::hypot(dlat, dlon);
}

inline double gnc_available_turn_radius_m(
    const WaypointLatLon& a,
    const WaypointLatLon& b,
    const WaypointLatLon& c,
    double ref_lat_deg) {
  const double m_per_deg_lon =
      kMetersPerDegLat * std::cos(ref_lat_deg * M_PI / 180.0);
  struct LocalXY {
    double x{0.0};
    double y{0.0};
  };
  auto to_xy = [&](const WaypointLatLon& p) {
    return LocalXY{
        p.lat * kMetersPerDegLat,
        p.lon * m_per_deg_lon};
  };
  const auto a_xy = to_xy(a);
  const auto b_xy = to_xy(b);
  const auto c_xy = to_xy(c);
  const double v1x = b_xy.x - a_xy.x;
  const double v1y = b_xy.y - a_xy.y;
  const double v2x = c_xy.x - b_xy.x;
  const double v2y = c_xy.y - b_xy.y;
  const double len1 = std::hypot(v1x, v1y);
  const double len2 = std::hypot(v2x, v2y);
  if (len1 < 1.0e-6 || len2 < 1.0e-6) {
    return 0.0;
  }
  const double dot = std::clamp((v1x * v2x + v1y * v2y) / (len1 * len2), -1.0, 1.0);
  const double angle = std::acos(dot);
  if (angle < M_PI / 180.0) {
    return std::numeric_limits<double>::infinity();
  }
  return std::min(len1, len2) / std::tan(angle * 0.5);
}

inline double gnc_cross_track_to_segment_m(
    const WaypointLatLon& point,
    const WaypointLatLon& a,
    const WaypointLatLon& b,
    double ref_lat_deg) {
  const double m_per_deg_lon =
      kMetersPerDegLat * std::cos(ref_lat_deg * M_PI / 180.0);
  const double px = point.lat * kMetersPerDegLat;
  const double py = point.lon * m_per_deg_lon;
  const double ax = a.lat * kMetersPerDegLat;
  const double ay = a.lon * m_per_deg_lon;
  const double bx = b.lat * kMetersPerDegLat;
  const double by = b.lon * m_per_deg_lon;
  const double abx = bx - ax;
  const double aby = by - ay;
  const double len = std::hypot(abx, aby);
  if (len < 1.0e-6) {
    return std::numeric_limits<double>::infinity();
  }
  const double apx = px - ax;
  const double apy = py - ay;
  return std::abs(apx * aby - apy * abx) / len;
}

inline double required_turn_radius_m(
    double speed_mps,
    const GncAvoidancePreflightConfig& cfg = {}) {
  const double speed = std::min(std::max(0.0, speed_mps), cfg.max_command_speed_mps);
  const double yaw_rate_rad_s = cfg.emergency_max_yaw_rate_deg_s * M_PI / 180.0;
  const double dynamic_required =
      speed * speed / std::max(0.01, cfg.max_lateral_accel_mps2);
  const double yaw_required =
      yaw_rate_rad_s > 1.0e-6
          ? speed / yaw_rate_rad_s
          : std::numeric_limits<double>::infinity();
  return std::max({cfg.emergency_min_turn_radius_m, dynamic_required, yaw_required});
}

inline double required_decel_distance_m(
    double v0_mps,
    double v1_mps,
    const GncAvoidancePreflightConfig& cfg = {}) {
  if (v0_mps <= v1_mps) {
    return 0.0;
  }
  return ((v0_mps * v0_mps) - (v1_mps * v1_mps)) /
      (2.0 * std::max(0.01, cfg.max_decel_mps2));
}

inline double gnc_emergency_command_speed_mps(
    double desired_mps,
    const GncAvoidancePreflightConfig& cfg = {}) {
  if (!std::isfinite(desired_mps) || desired_mps <= 0.0) {
    return std::min(cfg.emergency_guidance_speed_cap_mps, cfg.max_command_speed_mps);
  }
  return std::min({desired_mps, cfg.emergency_guidance_speed_cap_mps, cfg.max_command_speed_mps});
}

inline bool colregs_rule_active(
    const std::vector<std::uint8_t>& rules,
    std::uint8_t rule_id) {
  return std::find(rules.begin(), rules.end(), rule_id) != rules.end();
}

inline bool requires_colregs_overtake_corridor(
    bool colregs_conflict_active,
    const std::vector<std::uint8_t>& rules) {
  return colregs_conflict_active && colregs_rule_active(rules, 13u);
}

inline double gnc_avoidance_command_speed_mps(
    double desired_mps,
    bool colregs_overtake_corridor,
    const GncAvoidancePreflightConfig& cfg = {}) {
  if (!colregs_overtake_corridor) {
    return gnc_emergency_command_speed_mps(desired_mps, cfg);
  }
  if (!std::isfinite(desired_mps) || desired_mps <= 0.0) {
    return cfg.max_command_speed_mps;
  }
  return std::min(desired_mps, cfg.max_command_speed_mps);
}

inline const char* gnc_avoidance_navigation_mode(bool colregs_overtake_corridor) {
  return colregs_overtake_corridor ? "colregs_overtake" : "emergency_avoidance";
}

inline double gnc_return_command_speed_mps(
    double desired_mps,
    bool colregs_overtake_rejoin,
    const GncAvoidancePreflightConfig& cfg = {}) {
  return gnc_avoidance_command_speed_mps(desired_mps, colregs_overtake_rejoin, cfg);
}

inline const char* gnc_return_navigation_mode(bool colregs_overtake_rejoin) {
  return gnc_avoidance_navigation_mode(colregs_overtake_rejoin);
}

inline double speed_at_or_default(
    const std::vector<double>& speeds,
    std::size_t index,
    const GncAvoidancePreflightConfig& cfg) {
  if (index < speeds.size() && std::isfinite(speeds[index]) && speeds[index] > 0.0) {
    return std::min(speeds[index], cfg.max_command_speed_mps);
  }
  return cfg.max_command_speed_mps;
}

inline GncAvoidancePreflightResult validate_gnc_avoidance_plan(
    const WaypointLatLon& origin,
    const std::vector<WaypointLatLon>& wps,
    const std::vector<double>& speeds,
    const GncAvoidancePreflightConfig& cfg = {}) {
  if (wps.size() < 2U) {
    return {false, "invalid_avoidance_route", 0U, 2.0, static_cast<double>(wps.size())};
  }
  if (!speeds.empty() && speeds.size() != wps.size()) {
    return {false, "speed_length_mismatch", 0U, static_cast<double>(wps.size()), static_cast<double>(speeds.size())};
  }

  const double first_speed = speed_at_or_default(speeds, 0U, cfg);
  const bool high_speed_flyby =
      first_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6;
  const double first_required = high_speed_flyby
      ? cfg.high_speed_flyby_min_segment_m
      : cfg.emergency_wheel_over_distance_m;
  const double first_distance = gnc_distance_m(origin, wps.front(), origin.lat);
  if (first_distance + 1.0e-6 < first_required) {
    return {
        false,
        "first_maneuver_point_too_close",
        0U,
        first_required,
        first_distance};
  }

  const double ref_lat = wps.front().lat;
  if (high_speed_flyby && wps.size() >= 2U) {
    const double initial_raw_xte =
        gnc_cross_track_to_segment_m(origin, wps[0U], wps[1U], ref_lat);
    if (initial_raw_xte > cfg.raw_route_rejoin_threshold_m + 1.0e-6) {
      return {
          false,
          "initial_raw_route_xte_too_large",
          0U,
          cfg.raw_route_rejoin_threshold_m,
          initial_raw_xte};
    }
  }

  for (std::size_t i = 0U; i + 1U < wps.size(); ++i) {
    const double segment = gnc_distance_m(wps[i], wps[i + 1U], ref_lat);
    if (segment + 1.0e-6 < cfg.emergency_min_segment_length_m) {
      return {false, "segment_too_short", i, cfg.emergency_min_segment_length_m, segment};
    }
    const double segment_speed = std::max(
        speed_at_or_default(speeds, i, cfg),
        speed_at_or_default(speeds, i + 1U, cfg));
    if (segment_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6 &&
        segment + 1.0e-6 < cfg.high_speed_flyby_min_segment_m) {
      return {false, "flyby_segment_too_short", i, cfg.high_speed_flyby_min_segment_m, segment};
    }
  }

  if (wps.size() >= 2U) {
    const double available =
        gnc_available_turn_radius_m(origin, wps.front(), wps[1U], ref_lat);
    const double required = required_turn_radius_m(first_speed, cfg);
    if (available + 1.0e-6 < required) {
      return {false, "first_turn_radius_too_small", 0U, required, available};
    }
  }

  for (std::size_t i = 1U; i + 1U < wps.size(); ++i) {
    const double available = gnc_available_turn_radius_m(wps[i - 1U], wps[i], wps[i + 1U], ref_lat);
    const double required = required_turn_radius_m(speed_at_or_default(speeds, i, cfg), cfg);
    if (available + 1.0e-6 < required) {
      return {false, "turn_radius_too_small", i, required, available};
    }
  }

  for (std::size_t i = 0U; i + 1U < wps.size(); ++i) {
    const double required = required_decel_distance_m(
        speed_at_or_default(speeds, i, cfg),
        speed_at_or_default(speeds, i + 1U, cfg),
        cfg);
    if (required <= 0.0) {
      continue;
    }
    const double available = gnc_distance_m(wps[i], wps[i + 1U], ref_lat);
    if (available + 1.0e-6 < required) {
      return {false, "decel_distance_not_enough", i, required, available};
    }
  }

  return {true, "feasible", 0U, 0.0, 0.0};
}

}  // namespace mass_l3::m5
