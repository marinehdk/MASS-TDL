#pragma once
// M5 Mid-MPC route-frame geometry helpers (Slice R1, spec §4.1/§4.3).
//
// Pure C++ geometry extracted from MidMpcNode::assemble_input_ so the active-leg
// nearest-leg search, end-clamped fallback, and cross-leg corner guard can be
// unit-tested independently of the ROS node (R1 review round-2 Critical 3).
//
// All geometry is in the OWN-relative NED frame: own ship sits at (0,0); every
// L2 polyline waypoint has been projected to NED metres relative to own. NED
// convention: x=north (cos), y=east (sin), ψ=0 → north, positive clockwise.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace mass_l3::m5::mid_mpc {

// Result of projecting the own ship onto the L2 route polyline (own-relative NED).
struct ActiveLegProjection {
  std::size_t leg_index{0};         // index of the active (nearest) segment
  double leg_length_m{0.0};         // length of the active segment
  double station_s0_m{0.0};         // along-track distance of own's projection on the active leg
  double cross_track_l0_m{0.0};     // signed cross-track of own against the active leg
                                     //   (positive starboard, n=(-sinψ,cosψ))
  double route_bearing_rad{0.0};    // active-leg bearing (atan2 of segment east/north delta)
  bool valid{false};                // false if the polyline had no usable segment
};

// Result of the cross-leg corner guard (spec §4.3): does extrapolating the own
// heading straight ahead for `reach_m` cross past the active leg's end corner?
struct CrossLegGuardResult {
  bool crosses_corner{false};
  bool evaluated{false};   // false if there is no next leg to cross into
};

// Project the own ship (at own-relative origin (0,0)) onto a polyline of
// own-relative NED waypoints. Implements the spec §4.1 nearest-leg search:
//   - scan every adjacent segment (wp_n[i],wp_e[i]) → (wp_n[i+1],wp_e[i+1])
//   - project own onto the segment; own relative to segment start = (-wp_n[i], -wp_e[i])
//   - prefer the segment own projects INSIDE (station along ∈ [0, len]); among
//     those pick the smallest perpendicular distance = the active leg
//   - fallback (own before route start or past route end): among segments whose
//     projection falls outside, pick the one with the smallest END-CLAMPED
//     distance (distance to the nearest segment endpoint), NOT the infinite-line
//     distance (R1 review round-2 Critical 3B: infinite-line distance falsely
//     reports own as "on" a leg whose extension passes near own but whose actual
//     segment is far away).
//
// Returns the projection (active leg, station s0, cross-track l0, bearing).
inline ActiveLegProjection project_own_onto_polyline(
    const std::vector<double>& wp_n,
    const std::vector<double>& wp_e) {
  ActiveLegProjection proj;
  const std::size_t n_wp = wp_n.size();
  if (n_wp != wp_e.size() || n_wp < 2u) { return proj; }

  // Candidate "on-segment" leg (own projects inside the segment).
  std::size_t on_leg = 0u;
  double on_len = 0.0;
  double on_along = 0.0;
  double on_cross = std::numeric_limits<double>::max();
  bool found_on = false;
  for (std::size_t i = 0u; i + 1u < n_wp; ++i) {
    const double sx = wp_n[i + 1u] - wp_n[i];   // segment north delta
    const double sy = wp_e[i + 1u] - wp_e[i];   // segment east  delta
    const double seg_len = std::hypot(sx, sy);
    if (seg_len < 1.0) { continue; }  // degenerate segment
    const double ox_rel = -wp_n[i];
    const double oy_rel = -wp_e[i];
    const double along = (ox_rel * sx + oy_rel * sy) / seg_len;  // [0,seg_len] when on
    const double cross = std::fabs((sx * oy_rel - sy * ox_rel) / seg_len);
    const bool on_segment = (along >= 0.0 && along <= seg_len);
    if (on_segment && cross < on_cross) {
      on_cross = cross;
      on_leg = i;
      on_len = seg_len;
      on_along = along;
      found_on = true;
    }
  }

  if (found_on) {
    proj.leg_index = on_leg;
    proj.leg_length_m = on_len;
    proj.station_s0_m = on_along;
    proj.valid = true;
  } else {
    // Fallback (own before route start or past route end): pick the segment with
    // the smallest END-CLAMPED point distance (Critical 3B). The infinite-line
    // perpendicular distance used previously falsely accepts a segment whose
    // extension passes near own while the actual segment is far away.
    std::size_t cl_leg = 0u;
    double cl_len = 0.0;
    double cl_along = 0.0;
    double cl_dist = std::numeric_limits<double>::max();
    for (std::size_t i = 0u; i + 1u < n_wp; ++i) {
      const double sx = wp_n[i + 1u] - wp_n[i];
      const double sy = wp_e[i + 1u] - wp_e[i];
      const double seg_len = std::hypot(sx, sy);
      if (seg_len < 1.0) { continue; }
      const double ox_rel = -wp_n[i];
      const double oy_rel = -wp_e[i];
      double along = (ox_rel * sx + oy_rel * sy) / seg_len;
      along = std::clamp(along, 0.0, seg_len);   // END-CLAMPED station
      // Closest point on the segment to own, and its distance.
      const double px = wp_n[i] + along / seg_len * sx;
      const double py = wp_e[i] + along / seg_len * sy;
      const double dist = std::hypot(px, py);    // own is at (0,0)
      if (dist < cl_dist) {
        cl_dist = dist;
        cl_leg = i;
        cl_len = seg_len;
        cl_along = along;
      }
    }
    if (cl_dist == std::numeric_limits<double>::max()) { return proj; }  // all degenerate
    proj.leg_index = cl_leg;
    proj.leg_length_m = cl_len;
    proj.station_s0_m = cl_along;
    proj.valid = true;
  }

  // Active-leg bearing + signed cross-track (starboard positive).
  const double ax = wp_n[proj.leg_index + 1u] - wp_n[proj.leg_index];
  const double ay = wp_e[proj.leg_index + 1u] - wp_e[proj.leg_index];
  proj.route_bearing_rad = std::atan2(ay, ax);
  const double ox_rel = -wp_n[proj.leg_index];
  const double oy_rel = -wp_e[proj.leg_index];
  proj.cross_track_l0_m = ox_rel * (-std::sin(proj.route_bearing_rad))
                        + oy_rel * ( std::cos(proj.route_bearing_rad));
  return proj;
}

// Cross-leg corner guard (spec §4.3). Extrapolate the own heading straight ahead
// for `reach_m`; if the along-track progress on the active leg, scaled by reach,
// exceeds the REMAINING distance from own's current station s0 to the active leg
// end corner (active_len - s0), the trajectory would cross into the next L2 leg
// → null J_route (return crosses_corner=true). Only evaluated when a next leg
// exists (leg_index + 1 + 1 < n_wp).
//
// R1 review round-2 Critical 3A: the previous guard compared reach·along_proj
// against the FULL active_len, ignoring that own has already travelled s0 along
// the leg. Own in the back half of a long leg was falsely left enabled (it is
// actually close to the corner), and own near the leg start was over-guarded.
// The remaining distance to the corner is (active_len - s0).
inline CrossLegGuardResult evaluate_cross_leg_guard(
    const ActiveLegProjection& proj,
    std::size_t n_waypoints,
    double own_heading_rad,
    double reach_m) {
  CrossLegGuardResult res;
  // Active leg must exist and have a next leg to cross into.
  if (!proj.valid || proj.leg_length_m <= 1.0) { return res; }
  if (n_waypoints <= proj.leg_index + 2u) { return res; }
  // Along-track component of the own-heading unit ray on the active leg.
  const double ax_n = std::cos(own_heading_rad);
  const double ax_e = std::sin(own_heading_rad);
  const double bearing_n = std::cos(proj.route_bearing_rad);
  const double bearing_e = std::sin(proj.route_bearing_rad);
  const double along_proj = (ax_n * bearing_n + ax_e * bearing_e);
  if (along_proj <= 1.0e-6) { res.evaluated = true; return res; }  // heading away from corner
  res.evaluated = true;
  // Remaining along-track distance from own's station s0 to the leg end corner.
  const double remaining_to_corner = proj.leg_length_m - proj.station_s0_m;
  if (reach_m * along_proj > remaining_to_corner) {
    res.crosses_corner = true;
  }
  return res;
}

}  // namespace mass_l3::m5::mid_mpc
