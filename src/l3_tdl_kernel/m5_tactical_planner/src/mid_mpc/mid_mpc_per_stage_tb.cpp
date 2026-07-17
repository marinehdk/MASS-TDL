// M5 Tactical Planner — per-stage t_b implementation (P2 T4, VR-07b).
//
// Calls the T1 project_to_segment pure function per stage on the F1
// forward-propagated own-ship seed positions. Pure arithmetic, no CasADi, no
// acatos, no dynamic allocation beyond the result vectors.
// PATH-D clean: -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion.

#include "m5_tactical_planner/mid_mpc/mid_mpc_per_stage_tb.hpp"

#include <cmath>
#include <cstddef>

#include "m5_tactical_planner/shared/relative_track.hpp"  // T1 project_to_segment

namespace mass_l3::m5::mid_mpc {

namespace {
// A seed/leg is degenerate when the own position is non-finite OR the leg
// extent is non-positive (zero-length segment). In either case the projection
// is undefined; the honest fallback is the absolute route origin A (the leg
// start), so the lateral deviation l is measured relative to the leg start
// (the original absolute-frame behavior).
bool seed_or_leg_degenerate(double px, double py,
                            double ax, double ay,
                            double bearing_rad,
                            double leg_extent_m) {
  if (!std::isfinite(px) || !std::isfinite(py)) return true;
  if (!std::isfinite(ax) || !std::isfinite(ay)) return true;
  if (!std::isfinite(bearing_rad)) return true;
  if (!(leg_extent_m > 0.0) || !std::isfinite(leg_extent_m)) return true;
  return false;
}
}  // namespace

PerStageTb compute_per_stage_tb(const std::vector<double>& px_seed,
                                const std::vector<double>& py_seed,
                                double ax, double ay,
                                double bearing_rad,
                                double nx, double ny,
                                double leg_extent_m,
                                int N) {
  PerStageTb out;
  if (N < 0) {
    return out;  // degenerate horizon: empty result.
  }
  const std::size_t n_stages = static_cast<std::size_t>(N) + 1u;
  out.tb_x.resize(n_stages, ax);  // default-init to ABSOLUTE route origin A
  out.tb_y.resize(n_stages, ay);  // (the honest fallback per stage).

  // Far endpoint B: A + bearing_dir * extent. The leg is effectively a ray
  // from A along the leg bearing; B is a far endpoint so projection onto
  // segment [A, B] equals projection onto the ray for any realistic own
  // position over the horizon (clamp-to-B is unreachable for sane extents).
  const double dir_x = std::cos(bearing_rad);
  const double dir_y = std::sin(bearing_rad);
  const double bx = ax + dir_x * leg_extent_m;
  const double by = ay + dir_y * leg_extent_m;

  // Per-stage projection on the seed positions. Stages 0..N-1 use the seed
  // position directly; stage N (the terminal) repeats stage N-1 (the last real
  // projection) — mirrors how pack_parameters already repeats stage N-1 for
  // the terminal per-stage row (acatos requires a param at every stage 0..N).
  for (int k = 0; k < N; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    if (kk >= px_seed.size() || kk >= py_seed.size()) {
      // Seed vector shorter than N+1: leave the fallback (ax, ay) for this and
      // all higher stages (do not silently read out of bounds).
      break;
    }
    const double px = px_seed[kk];
    const double py = py_seed[kk];
    if (seed_or_leg_degenerate(px, py, ax, ay, bearing_rad, leg_extent_m)) {
      // Fallback already in place (ax, ay) from the resize default — keep it.
      continue;
    }
    const shared::relative_track::Projection proj =
        shared::relative_track::project_to_segment(px, py, ax, ay,
                                                   bx, by, nx, ny);
    out.tb_x[kk] = proj.closest_x;
    out.tb_y[kk] = proj.closest_y;
  }
  // Terminal stage N: repeat stage N-1 (the last real projection), matching
  // pack_parameters' terminal-row repeat. If N==0 (no shooting intervals) the
  // single stage 0 already holds the absolute-origin fallback from the resize.
  if (N >= 1) {
    const std::size_t last = static_cast<std::size_t>(N - 1);
    out.tb_x[n_stages - 1u] = out.tb_x[last];
    out.tb_y[n_stages - 1u] = out.tb_y[last];
  }
  return out;
}

}  // namespace mass_l3::m5::mid_mpc
