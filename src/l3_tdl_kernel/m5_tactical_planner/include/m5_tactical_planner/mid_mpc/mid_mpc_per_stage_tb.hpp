// M5 Tactical Planner — per-stage t_b closest-point computation (P2 T4,
// VR-07b). Pure C++ utility with NO CasADi/acados dependency.
//
// Computes the per-stage t_b[k] = closest point on the nominal route leg to the
// own-ship PREDICTED position (the F1 forward-propagated seed position), using
// the T1 project_to_segment pure function. The result fills the per-stage
// tb_x/tb_y slots (kAcadosPerStageTbXOff / kAcadosPerStageTbYOff) that the
// acados route COST (build_route_cost_, T3) and terminal COST (build_terminal
// _cost_, T4) read as the lateral-deviation origin.
//
// The route leg is treated as an effectively-infinite ray from the active-leg
// origin A along the leg bearing (the leg extent is set large enough that the
// projection onto segment [A, B] never clamps to B for any realistic own
// position over the horizon). This matches the spec's "t_b = projection of own
// predicted position onto the nominal route leg" semantics.
//
// FALLBACK: on a degenerate seed (NaN own position, or a degenerate leg), the
// tb slots are filled with the ABSOLUTE route origin A (the leg start) — never
// silently left at (0,0). This makes the lateral deviation l relative to the
// leg start, the original absolute-frame behavior (a well-defined cost when the
// projection is unavailable).
//
// Extracted as a free function (rather than a private method on
// MidMpcAcadosSolver) so the unit test can call it DIRECTLY — without linking
// the generated acatos solver .so (which is stale NP=141 until T5). The solver
// .cpp solve() path calls this same function, so the test exercises the real
// computation. Mirrors the T1 project_to_segment / T2 huber_cost pattern.
//
// PATH-D (MISRA C++:2023): noexcept-where-possible, branch-light, no CasADi.
#pragma once

#include <vector>

namespace mass_l3::m5::mid_mpc {

// Per-stage t_b result: one (x, y) closest-point per stage 0..N (N+1 entries).
// The terminal stage N entry repeats stage N-1 (acatos requires a per-stage
// param at every stage 0..N; the terminal value is the last real projection —
// matches how pack_parameters already repeats stage N-1 for the terminal row).
struct PerStageTb {
  std::vector<double> tb_x;  // length N+1
  std::vector<double> tb_y;  // length N+1
};

// Compute per-stage t_b[k] = closest point on the nominal route leg [A, B] to
// the own-ship predicted position (px_seed[k], py_seed[k]), for k=0..N.
//
// @param px_seed, py_seed  Own-ship PREDICTED positions per stage (length MUST
//                          be >= N+1; the F1 forward-propagated seed). Only
//                          read up to index N.
// @param ax, ay            Active-leg origin A (the route-frame origin).
// @param bearing_rad       Active-leg bearing (leg direction unit vector is
//                          (cos, sin) of this; B = A + bearing_dir * extent).
// @param nx, ny            Route-frame NORMAL unit vector (passed through to
//                          project_to_segment for the signed-lateral field;
//                          not stored in the result, but used by T1).
// @param leg_extent_m      Distance from A to the far endpoint B. Large enough
//                          that projection onto [A, B] never clamps to B for
//                          any realistic own position (the leg is effectively
//                          a ray from A). Caller picks a generous value.
// @param N                 Horizon (number of shooting intervals); the result
//                          has N+1 entries (stages 0..N).
// @return PerStageTb with tb_x/tb_y of length N+1. On degenerate seed/leg the
//         entries fall back to (ax, ay) (the absolute route origin).
PerStageTb compute_per_stage_tb(const std::vector<double>& px_seed,
                                const std::vector<double>& py_seed,
                                double ax, double ay,
                                double bearing_rad,
                                double nx, double ny,
                                double leg_extent_m,
                                int N);

}  // namespace mass_l3::m5::mid_mpc
