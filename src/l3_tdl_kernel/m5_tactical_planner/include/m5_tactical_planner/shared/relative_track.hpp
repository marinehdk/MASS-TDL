// M5 Tactical Planner — relative_track: point-to-segment projection.
//
// P2 T1 (Eriksen relative-track t_b + Huber, VR-07b). Pure C++ utility with NO
// CasADi/acados dependency. The acados solver pack (T4) calls project_to_segment
// per-stage to compute t_b[k] = the parameter of the closest point on the
// nominal route leg (A->B) to the own-ship predicted position P.
//
// PATH-D (MISRA C++:2023): noexcept, branch-light, no dynamic allocation.
#pragma once

namespace mass_l3::m5::shared::relative_track {

struct Projection {
  double t;              // parameter t in [0,1] (clamped)
  double closest_x;      // closest point x
  double closest_y;      // closest point y
  double signed_lateral; // signed lateral deviation (P - closest) . n_hat
};

// Closest-point projection of (px,py) onto segment (ax,ay)->(bx,by).
// n_hat=(nx,ny) is the route normal unit vector (used for signed lateral).
// Degenerate (zero-length segment) fallback: closest=A, lateral=(P-A).n_hat.
Projection project_to_segment(double px, double py,
                              double ax, double ay, double bx, double by,
                              double nx, double ny) noexcept;

}  // namespace mass_l3::m5::shared::relative_track
