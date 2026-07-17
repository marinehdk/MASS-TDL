// M5 Tactical Planner — relative_track implementation (P2 T1, VR-07b).
//
// Standard point-to-segment projection. Pure arithmetic, no CasADi, no alloc.
// PATH-D clean: -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion.

#include "m5_tactical_planner/shared/relative_track.hpp"

namespace mass_l3::m5::shared::relative_track {

namespace {
// Squared-length threshold below which the segment is treated as degenerate
// (zero-length). 1e-12 m^2 -> leg shorter than 1e-6 m, well below any real
// route-leg length but tight enough to reject pure floating-point dust.
constexpr double kDegenerateAb2 = 1e-12;

inline double clamp_unit(double x) noexcept {
  if (x < 0.0) {
    return 0.0;
  }
  if (x > 1.0) {
    return 1.0;
  }
  return x;
}
}  // namespace

Projection project_to_segment(double px, double py,
                              double ax, double ay, double bx, double by,
                              double nx, double ny) noexcept {
  Projection r{0.0, ax, ay, 0.0};

  const double abx = bx - ax;
  const double aby = by - ay;
  const double ab2 = abx * abx + aby * aby;

  double t = 0.0;
  if (ab2 >= kDegenerateAb2) {
    const double apx = px - ax;
    const double apy = py - ay;
    t = clamp_unit((apx * abx + apy * aby) / ab2);
  }
  // Degenerate fallback: t stays 0 -> closest = A.

  const double cx = ax + t * abx;
  const double cy = ay + t * aby;

  r.t = t;
  r.closest_x = cx;
  r.closest_y = cy;
  r.signed_lateral = (px - cx) * nx + (py - cy) * ny;
  return r;
}

}  // namespace mass_l3::m5::shared::relative_track
