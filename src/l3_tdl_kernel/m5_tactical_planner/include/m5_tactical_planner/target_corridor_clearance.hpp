#pragma once
// M5 target-track vs avoidance-corridor clearance gate (W4-A).
// Pure C++ geometry, no ROS, unit-tested independently.
//
// Root cause (data 2026-06-29, trace 20260629_000517_cs_edge_single):
//   The fixed 270m starboard corridor is crossed by the target predicted track
//   for near-head-on crossing geometries (brg~25, aspect~0). own reaches 92%
//   of the corridor peak, so reachability is NOT the limiter; the corridor
//   itself is on the target's path. This gate checks whether a candidate
//   corridor segment keeps own-target separation >= floor at every sampled
//   target-track point.
//
// Coordinate frame: local NED metres. x = north, y = east.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace mass_l3::m5 {

struct TargetTrackPoint {
  double x_m{0.0};       // north [m]
  double y_m{0.0};       // east  [m]
  double cog_rad{0.0};   // course over ground [rad], 0 = north, +cw
  double sog_mps{0.0};   // speed over ground [m/s]
};

struct TargetClearanceVerdict {
  bool clear{false};
  double min_separation_m{std::numeric_limits<double>::infinity()};
  double crossing_t_s{-1.0};
};

inline std::vector<TargetTrackPoint> sample_target_track(
    const TargetTrackPoint& t0,
    double step_s,
    double horizon_s) {
  std::vector<TargetTrackPoint> track;
  if (step_s <= 0.0 || horizon_s < 0.0) {
    track.push_back(t0);
    return track;
  }
  const double sin_c = std::sin(t0.cog_rad);
  const double cos_c = std::cos(t0.cog_rad);
  const std::int64_t n = static_cast<std::int64_t>(horizon_s / step_s) + 1;
  track.reserve(static_cast<std::size_t>(n));
  for (std::int64_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i) * step_s;
    TargetTrackPoint p = t0;
    p.x_m = t0.x_m + t0.sog_mps * t * cos_c;
    p.y_m = t0.y_m + t0.sog_mps * t * sin_c;
    track.push_back(p);
  }
  return track;
}

inline double closest_target_to_segment_m(
    double px, double py,
    double ax, double ay,
    double bx, double by) {
  const double abx = bx - ax;
  const double aby = by - ay;
  const double apx = px - ax;
  const double apy = py - ay;
  const double ab2 = abx * abx + aby * aby;
  double t = (ab2 > 1.0e-9) ? ((apx * abx + apy * aby) / ab2) : 0.0;
  t = std::clamp(t, 0.0, 1.0);
  const double cx = ax + t * abx;
  const double cy = ay + t * aby;
  return std::hypot(px - cx, py - cy);
}

inline TargetClearanceVerdict evaluate_target_corridor_clearance(
    const TargetTrackPoint& target0,
    double seg_a_n, double seg_a_e,
    double seg_b_n, double seg_b_e,
    double floor_m,
    double horizon_s,
    double step_s) {
  const auto track = sample_target_track(target0, step_s, horizon_s);
  TargetClearanceVerdict v;
  v.clear = true;
  for (std::size_t i = 0; i < track.size(); ++i) {
    const double d = closest_target_to_segment_m(
        track[i].x_m, track[i].y_m, seg_a_n, seg_a_e, seg_b_n, seg_b_e);
    if (d < v.min_separation_m) {
      v.min_separation_m = d;
    }
    if (d < floor_m) {
      v.clear = false;
      if (v.crossing_t_s < 0.0) {
        v.crossing_t_s = static_cast<double>(i) * step_s;
      }
    }
  }
  return v;
}

}  // namespace mass_l3::m5
