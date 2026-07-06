#ifndef MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_CANDIDATE_GEOMETRY_HPP_
#define MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_CANDIDATE_GEOMETRY_HPP_
// M5 committed-route candidate geometry helpers (M1 review, spec §6.6).
//
// Pure C++ extracted from the anonymous-namespace committed_candidate_from_plan
// in mid_mpc_node.cpp so the §6.6.2 frozen-prefix count can be unit-tested
// independently of the ROS node (R1 review pattern, cf. mid_mpc_route_frame.hpp).
//
// All geometry is in the OWN-relative NED frame built from WGS84 lat/lon degrees.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::committed_route {

// Spec §6.6.2: minimum first-changed distance [m]. Plan waypoints whose
// along-track distance from the own-ship is inside this window are already
// being executed by GNC and must be frozen (frozen_prefix_count) so a new
// revision cannot alter geometry the vessel is committed to.
inline constexpr double kMinFirstChangedDistance_m = 100.0;

// Result of computing the frozen prefix count from a plan's WGS84 geometry and
// the own-ship position (spec §6.6.2).
struct FrozenPrefixResult {
  std::size_t frozen_prefix_count{0U};  // leading in-guard waypoints
  double own_station_m{0.0};            // own's along-track station on the polyline
  bool valid{false};                    // false if the polyline had no usable segment
};

// Compute the along-track frozen prefix count (spec §6.6.2) for a plan whose
// waypoints are given as WGS84 lat/lon degrees, relative to the own-ship
// position (own_lat_deg, own_lon_deg).
//
// Algorithm (signed along-track projection, NOT Euclidean distance — Critical
// High-4 review fix):
//   1. Convert every plan waypoint to own-relative NED metres.
//   2. Accumulate each waypoint's cumulative along-track station from the
//      polyline START (monotonic).
//   3. Project the own-ship (at the NED origin (0,0)) onto the plan polyline to
//      get own_station = along-track distance of own's projection, measured from
//      the polyline START (own before the route start → negative station).
//   4. frozen_prefix_count = leading run of waypoints whose station is strictly
//      AHEAD of own AND within the in-guard window:
//        own_station < station[i] <= own_station + guard_distance.
//      Waypoints own has already overrun (station <= own_station) are PRUNED
//      (spec §6.6 "prune crossed points"): they are executed geometry, not a
//      frozen prefix. The run ends at the first waypoint beyond the forward
//      bound (station > own_station + guard_distance).
//
// `guard_distance_m` defaults to kMinFirstChangedDistance_m (100 m).
//
// The signed along-track projection is required because the legacy Euclidean
// (own↔waypoint) distance stays < guard_distance for a stretch even after own
// has passed the waypoint, so it could not bound the in-guard run correctly
// (spec §6.6). The along-track station is monotonic along the polyline and goes
// negative behind own, so the (own_station + guard_distance) bound advances
// monotonically as own progresses, shrinking the frozen run.
//
// PRUNE (spec §6.6 "prune crossed points", round-2 review fix): a waypoint is
// counted ONLY if it is strictly AHEAD of own (station > own_station) AND inside
// the forward guard (station <= own_station + guard_distance). Waypoints own has
// overrun (station <= own_station) are NOT counted — they are geometry the vessel
// is already executing, so freezing them would be wrong and, worse, would make
// the count GROW as own advances (each newly-overrun waypoint was still in the
// station<=guard bound). With the strict-ahead guard the count is non-increasing
// as own advances: once a waypoint is overrun it leaves the counted set.
//
// NOTE (M1 simplification — geometry-head prune is future work): the committed
// prefix the manager consumes is the leading slice [0, count). When a HEAD
// waypoint is overrun (own has passed wp0), this count drops it, but the slice
// still begins at wp0 — fully excising an overrun head waypoint would require
// the manager to advance its prefix start index, which is out of scope here
// (spec §6.6 head-prune). The strict-ahead count is the correct upper bound on
// how many leading waypoints remain frozen; downstream geometry-head rotation
// is tracked separately.
inline FrozenPrefixResult compute_frozen_prefix_count(
    const std::vector<double>& lat_deg,
    const std::vector<double>& lon_deg,
    double own_lat_deg,
    double own_lon_deg,
    double guard_distance_m = kMinFirstChangedDistance_m) {
  FrozenPrefixResult result;
  const std::size_t n = std::min(lat_deg.size(), lon_deg.size());
  if (n == 0u) { return result; }

  const double cos_lat = std::cos(own_lat_deg * units::kRadPerDeg);
  std::vector<double> wp_n(n), wp_e(n);
  for (std::size_t i = 0u; i < n; ++i) {
    wp_n[i] = (lat_deg[i] - own_lat_deg) * units::kRadPerDeg
              * units::kEarthRadiusMean_m;
    wp_e[i] = (lon_deg[i] - own_lon_deg) * units::kRadPerDeg
              * units::kEarthRadiusMean_m * cos_lat;
  }

  // Cumulative along-track station of each waypoint from the polyline start.
  std::vector<double> station(n);
  station[0] = 0.0;
  for (std::size_t i = 1u; i < n; ++i) {
    station[i] = station[i - 1u] + std::hypot(wp_n[i] - wp_n[i - 1u],
                                             wp_e[i] - wp_e[i - 1u]);
  }

  // Single waypoint: no segment to project onto. The lone waypoint's station is
  // 0 by definition, so the (own_station + guard) bound is meaningless here.
  // Freeze iff the waypoint lies within the guard radius of own (Euclidean — the
  // only distance defined for a single point).
  if (n == 1u) {
    const double dist = std::hypot(wp_n[0], wp_e[0]);
    result.own_station_m = -dist;  // own is `dist` before the lone waypoint
    result.frozen_prefix_count = (dist <= guard_distance_m) ? 1u : 0u;
    result.valid = true;
    return result;
  }

  // Project own (at origin) onto the polyline: nearest on-segment leg, else
  // end-clamped fallback (mirrors mid_mpc_route_frame project_own_onto_polyline,
  // kept self-contained here to avoid a cross-module dependency).
  std::size_t best_leg = 0u;
  double best_along = 0.0;
  double best_cross = std::numeric_limits<double>::max();
  bool found_on = false;
  for (std::size_t i = 0u; i + 1u < n; ++i) {
    const double sx = wp_n[i + 1u] - wp_n[i];
    const double sy = wp_e[i + 1u] - wp_e[i];
    const double seg_len = std::hypot(sx, sy);
    if (seg_len < 1.0) { continue; }
    const double ox_rel = -wp_n[i];
    const double oy_rel = -wp_e[i];
    const double along = (ox_rel * sx + oy_rel * sy) / seg_len;
    const double cross = std::fabs((sx * oy_rel - sy * ox_rel) / seg_len);
    const bool on_segment = (along >= 0.0 && along <= seg_len);
    if (on_segment && cross < best_cross) {
      best_cross = cross;
      best_leg = i;
      best_along = along;
      found_on = true;
    }
  }

  if (found_on) {
    // own's station from polyline start = sum of prior full segments + along.
    double s = 0.0;
    for (std::size_t i = 0u; i < best_leg; ++i) {
      s += std::hypot(wp_n[i + 1u] - wp_n[i], wp_e[i + 1u] - wp_e[i]);
    }
    result.own_station_m = s + best_along;
    result.valid = true;
  } else {
    // Own before the route start or past the route end. End-clamped fallback.
    // The signed along-track projection of own onto each segment is kept BEFORE
    // clamping so that "own before the route start" can be signalled with a
    // NEGATIVE own_station (own is `dist` behind the route start), which the
    // prune contract (station > own_station) needs to keep the first waypoint
    // frozen when own is ahead-of but short-of the route start. End-clamping to
    // the segment start would otherwise report own_station == 0 (== wp0's
    // station) and wrongly prune wp0 as overrun.
    std::size_t cl_leg = 0u;
    double cl_along = 0.0;        // clamped along ∈ [0, seg_len]
    double cl_raw_along = 0.0;    // unclamped along (sign of own's offset on leg)
    double cl_dist = std::numeric_limits<double>::max();
    for (std::size_t i = 0u; i + 1u < n; ++i) {
      const double sx = wp_n[i + 1u] - wp_n[i];
      const double sy = wp_e[i + 1u] - wp_e[i];
      const double seg_len = std::hypot(sx, sy);
      if (seg_len < 1.0) { continue; }
      const double ox_rel = -wp_n[i];
      const double oy_rel = -wp_e[i];
      const double raw_along = (ox_rel * sx + oy_rel * sy) / seg_len;
      const double along = std::clamp(raw_along, 0.0, seg_len);
      const double px = wp_n[i] + along / seg_len * sx;
      const double py = wp_e[i] + along / seg_len * sy;
      const double dist = std::hypot(px, py);
      if (dist < cl_dist) {
        cl_dist = dist;
        cl_leg = i;
        cl_along = along;
        cl_raw_along = raw_along;
      }
    }
    if (cl_dist == std::numeric_limits<double>::max()) { return result; }
    double s = 0.0;
    for (std::size_t i = 0u; i < cl_leg; ++i) {
      s += std::hypot(wp_n[i + 1u] - wp_n[i], wp_e[i + 1u] - wp_e[i]);
    }
    const double clamped_station = s + cl_along;
    // If the nearest projection's own is BEFORE the first segment's start
    // (raw along < 0 on the leading leg), own precedes the route start: report a
    // negative own_station so the strict-ahead prune keeps the head waypoints
    // frozen. Otherwise (own past the route end) own_station stays the clamped
    // route-end station (>= total length), which correctly prunes all waypoints.
    if (cl_leg == 0u && cl_raw_along < 0.0) {
      result.own_station_m = -cl_dist;  // own is `cl_dist` behind the route start
    } else {
      result.own_station_m = clamped_station;
    }
    result.valid = true;
  }

  // frozen_prefix_count: leading run of waypoints that are strictly AHEAD of own
  // AND inside the forward in-guard window:
  //   own_station < station[i] <= own_station + guard_distance.
  // Waypoints own has overrun (station <= own_station) are pruned (spec §6.6
  // "prune crossed points") — they are executed geometry, not a frozen prefix.
  // The run ends at the first waypoint beyond the forward bound.
  const double guard_ahead = result.own_station_m + guard_distance_m;
  std::size_t count = 0u;
  for (std::size_t i = 0u; i < n; ++i) {
    if (station[i] > result.own_station_m && station[i] <= guard_ahead) {
      ++count;
    } else if (station[i] > guard_ahead) {
      break;  // first waypoint beyond the forward guard ends the leading run
    }
    // else: station[i] <= own_station → overrun, pruned (skip, keep scanning).
  }
  result.frozen_prefix_count = count;
  return result;
}

}  // namespace mass_l3::m5::committed_route

#endif  // MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_CANDIDATE_GEOMETRY_HPP_
