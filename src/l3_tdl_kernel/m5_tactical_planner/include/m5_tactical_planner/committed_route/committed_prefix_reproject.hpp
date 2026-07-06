#ifndef MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_PREFIX_REPROJECT_HPP_
#define MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_PREFIX_REPROJECT_HPP_
// M5 committed-prefix reprojection + K computation (Slice C1, spec §6.2 / §6.3).
//
// Pure C++ extracted from the anonymous-namespace reproject_committed_prefix in
// mid_mpc_node.cpp so the §6.2 Critical reprojection contract (frozen WGS84
// geometry → per-cycle ownship-relative NED psi/u, geometry stays continuous as
// the origin moves) can be unit-tested independently of the ROS node. Same
// extraction pattern as committed_candidate_geometry.hpp (R1 review pattern, cf.
// mid_mpc_route_frame.hpp).
//
// The pure function takes the committed prefix as parallel WGS84 lat/lon degree
// vectors (not GeoWP) so it is testable with raw geometry arrays and has no ROS
// dependency; the node converts its GeoWP vector to lat/lon vectors before
// calling (spec §3.7 coordinate contract).
//
// All geometry is in the OWN-relative NED frame built from WGS84 lat/lon degrees
// (flat-earth, matching the node's tail_ned_to_latlon).
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/committed_route/committed_candidate_geometry.hpp"

namespace mass_l3::m5::committed_route {

// Spec §6.3: minimum suffix length (default 8 = 40 s at dt=5). Guarantees the
// suffix retains ample avoidance room regardless of K.
inline constexpr int32_t kSuffixMinSteps = 8;

// Result of reprojection: the NLP prefix-equality targets (psi/u, K steps).
struct PrefixReprojectResult {
  int32_t K{0};                  // pinned prefix length [steps]
  std::vector<double> psi_rad;   // per-step heading in the current NED frame [rad]
  std::vector<double> u_mps;     // per-step speed [m/s]
};

// Reproject the FROZEN committed-prefix WGS84 geometry (spec §6.2) into the
// CURRENT cycle's ownship-relative NED frame and back-infer the per-step NLP
// psi/u the equality rows pin (spec §6.2 step 3), plus the K derived from the
// GNC guard distance (spec §6.3).
//
// CONTRACT (spec §6.2 Critical): the prefix is frozen in WGS84 (the committed
// route geometry), NOT in psi/u (ownship-relative control quantities whose
// implied WGS84 geometry shifts each cycle as the ownship origin moves). Each
// cycle reprojects the frozen WGS84 waypoints to the CURRENT NED origin and
// back-infers the per-step psi/u, so the published geometry stays continuous.
//
// K computation (spec §6.3): K = ceil(guard_distance / (own_u · dt)), clamped to
//   [0, K_max] where K_max = N - K_suffix_min (K_suffix_min = 8 → 40 s suffix,
//   ample avoidance room). own_u is floored at 0.5 m/s so a near-stationary ship
//   does not inflate K to the full horizon (see spec §6.3 footnote; K_max clamp
//   is the hard backstop). K=0 on first commit (empty prefix) or when the suffix
//   would shrink below K_suffix_min.
//
// Back-infer (spec §6.2 step 3):
//   - Convert each prefix waypoint to own-relative NED metres (flat-earth).
//   - NLP psi[k]/u[k] is the heading/speed of control step k, which advances own
//     from pos[k] to pos[k+1]. To make the prefix reach the frozen waypoints, the
//     segment for step k goes from waypoint k-1 (or the own origin if k==0) to
//     waypoint k. So u[k] = |wp[k] - start|/dt, psi[k] = atan2(Δe, Δn).
//   - When the prefix has <2 points, psi/u default to own_psi/own_u.
//
// `guard_distance_m` defaults to kMinFirstChangedDistance_m (100 m).
inline PrefixReprojectResult reproject_prefix_psi_u(
    const std::vector<double>& prefix_lat_deg,
    const std::vector<double>& prefix_lon_deg,
    double own_lat_deg, double own_lon_deg,
    double own_psi_rad, double own_u_mps,
    double dt_s, int32_t N,
    double guard_distance_m = kMinFirstChangedDistance_m) {
  PrefixReprojectResult out;
  const std::size_t np = std::min(prefix_lat_deg.size(), prefix_lon_deg.size());

  // K from GNC guard distance (spec §6.3). own_u clamped to a 0.5 m/s floor so a
  // near-stationary ship does not inflate K to the full horizon (K_max clamp is
  // the hard backstop).
  const double u_eff = std::max(own_u_mps, 0.5);
  const double step_m = u_eff * dt_s;
  int32_t K = (step_m > 1.0e-6)
      ? static_cast<int32_t>(std::ceil(guard_distance_m / step_m))
      : 0;
  const int32_t K_max = std::max(0, N - kSuffixMinSteps);
  if (K > K_max) { K = K_max; }
  if (K < 0) { K = 0; }
  out.K = K;
  if (K == 0 || np == 0u) { return out; }

  // Reproject prefix waypoints to the current ownship-relative NED frame.
  const double cos_lat = std::cos(own_lat_deg * units::kRadPerDeg);
  std::vector<double> wn(np), we(np);
  for (std::size_t i = 0u; i < np; ++i) {
    wn[i] = (prefix_lat_deg[i] - own_lat_deg) * units::kRadPerDeg
            * units::kEarthRadiusMean_m;
    we[i] = (prefix_lon_deg[i] - own_lon_deg) * units::kRadPerDeg
            * units::kEarthRadiusMean_m * cos_lat;
  }

  // Back-infer psi/u from adjacent-point displacement over dt (spec §6.2 step 3).
  out.psi_rad.resize(static_cast<std::size_t>(K));
  out.u_mps.resize(static_cast<std::size_t>(K));
  for (int32_t k = 0; k < K; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    if (np < 2u) {
      out.psi_rad[kk] = own_psi_rad;
      out.u_mps[kk]   = own_u_mps;
      continue;
    }
    // End waypoint = wp[min(k, np-1)]; start = wp[k-1] (own origin if k==0).
    const std::size_t end_idx = std::min(kk, np - 1u);
    const double start_n = (kk == 0u) ? 0.0 : wn[std::min(kk - 1u, np - 1u)];
    const double start_e = (kk == 0u) ? 0.0 : we[std::min(kk - 1u, np - 1u)];
    const double dn = wn[end_idx] - start_n;
    const double de = we[end_idx] - start_e;
    const double dist = std::hypot(dn, de);
    out.psi_rad[kk] = std::atan2(de, dn);
    out.u_mps[kk]   = (dt_s > 1.0e-6) ? (dist / dt_s) : own_u_mps;
    // Guard against a degenerate near-zero displacement (coincident waypoints):
    // keep own_psi/own_u rather than atan2(0,0)=0.
    if (dist < 0.5) {
      out.psi_rad[kk] = own_psi_rad;
      out.u_mps[kk]   = own_u_mps;
    }
  }
  return out;
}

}  // namespace mass_l3::m5::committed_route

#endif  // MASS_L3_M5_COMMITTED_ROUTE_COMMITTED_PREFIX_REPROJECT_HPP_
