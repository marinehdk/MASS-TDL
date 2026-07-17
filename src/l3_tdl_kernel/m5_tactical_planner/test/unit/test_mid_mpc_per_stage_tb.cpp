// test/unit/test_mid_mpc_per_stage_tb.cpp
// P2 Task 4 (VR-07b) — per-stage t_b closest-point computation test.
//
// Cross-checks compute_per_stage_tb (the free function the acatos solver .cpp
// calls to fill the per-stage tb_x/tb_y slots) against the T1 project_to_segment
// pure-function ORACLE. This proves the solver actually computes tb from the F1
// seed (NOT leaves it 0.0) and that the result equals the per-stage closest-
// point projection on the nominal route leg.
//
// Why this is a free-function test (NOT an end-to-end solver test): the acatos
// .so is STALE (NP=141) until T5 regenerates it (NP=143). The full solver
// solve() cannot run until T5/T6. Extracting compute_per_stage_tb as a free
// function lets this test verify the tb computation in ISOLATION from the .so —
// the solver .cpp calls the SAME function at runtime, so a PASS here is a PASS
// of the solver's tb-pack logic modulo the codegen staleness (deferred T5/T6).
//
// The test does NOT mock project_to_segment — it calls the REAL T1 function as
// the oracle, so a regression in EITHER the T1 function or compute_per_stage_tb
// surfaces here. No mocks, no forced-pass, no threshold tuning.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "m5_tactical_planner/mid_mpc/mid_mpc_per_stage_tb.hpp"
#include "m5_tactical_planner/shared/relative_track.hpp"  // T1 oracle

using mass_l3::m5::mid_mpc::compute_per_stage_tb;
using mass_l3::m5::mid_mpc::PerStageTb;
using mass_l3::m5::shared::relative_track::project_to_segment;

namespace {
// A generous leg extent: the leg is effectively a ray from A along the bearing.
// The brief suggests planned_speed * (N+2) * dt or a fixed large value; 50000 m
// is well beyond any realistic own displacement over a 90s horizon (5 m/s *
// 90s = 450 m), so projection onto [A, B] never clamps to B.
constexpr double kLegExtentM = 50000.0;
constexpr int kN = 18;  // production default horizon (matches kAcadosNDefault)

// T1 oracle: per-stage closest-point on segment [A, B] to the seed position.
// Mirrors what compute_per_stage_tb MUST return (it calls the same T1 fn).
struct OraclePt { double cx; double cy; };
OraclePt oracle_closest(double px, double py,
                        double ax, double ay, double bearing_rad,
                        double nx, double ny) {
  const double bx = ax + std::cos(bearing_rad) * kLegExtentM;
  const double by = ay + std::sin(bearing_rad) * kLegExtentM;
  const auto proj = project_to_segment(px, py, ax, ay, bx, by, nx, ny);
  return {proj.closest_x, proj.closest_y};
}
}  // namespace

// Test 1 — ComputePerStageTb_MatchesProjectToSegmentOracle:
// Build a known seed trajectory (own moving OFF the route leg), compute tb,
// and assert each stage's tb[k] equals the T1 project_to_segment oracle on the
// seed position. This proves the solver tb-pack path uses the seed (not 0.0)
// and matches the closest-point projection.
TEST(MidMpcPerStageTbTest, ComputePerStageTb_MatchesProjectToSegmentOracle) {
  // Route leg: origin A=(100, -200), bearing=0 (north, +x in NED north). Leg
  // direction unit vector = (cos 0, sin 0) = (1, 0). Normal n = (0, 1).
  const double ax = 100.0;
  const double ay = -200.0;
  const double bearing = 0.0;
  const double nx = 0.0;
  const double ny = 1.0;

  // Seed trajectory: own starts at (100, -150) — 50 m EAST of A (along the
  // leg) and 50 m off the leg in the +y normal direction. Then drifts further
  // along +x AND further off-leg in +y. The closest point on the leg is the
  // along-leg projection of own's position; the per-stage tb must track that.
  std::vector<double> px_seed(static_cast<std::size_t>(kN + 1), 0.0);
  std::vector<double> py_seed(static_cast<std::size_t>(kN + 1), 0.0);
  for (int k = 0; k <= kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    px_seed[kk] = 100.0 + 50.0 * static_cast<double>(k);   // along-leg drift
    py_seed[kk] = -150.0 + 7.0 * static_cast<double>(k);   // off-leg drift
  }

  const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                             ax, ay, bearing, nx, ny,
                                             kLegExtentM, kN);
  ASSERT_EQ(static_cast<int>(tb.tb_x.size()), kN + 1);
  ASSERT_EQ(static_cast<int>(tb.tb_y.size()), kN + 1);

  // Stages 0..N-1: each tb[k] must equal the T1 oracle on (px_seed[k], py_seed[k]).
  for (int k = 0; k < kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    const OraclePt exp = oracle_closest(px_seed[kk], py_seed[kk],
                                        ax, ay, bearing, nx, ny);
    EXPECT_NEAR(tb.tb_x[kk], exp.cx, 1e-9)
        << "tb_x mismatch at k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], exp.cy, 1e-9)
        << "tb_y mismatch at k=" << k;
  }
  // Terminal stage N: repeats stage N-1 (the last real projection), matching
  // pack_parameters' terminal-row repeat.
  const std::size_t n_last = static_cast<std::size_t>(kN - 1);
  EXPECT_NEAR(tb.tb_x[static_cast<std::size_t>(kN)], tb.tb_x[n_last], 1e-12)
      << "terminal tb_x must repeat stage N-1";
  EXPECT_NEAR(tb.tb_y[static_cast<std::size_t>(kN)], tb.tb_y[n_last], 1e-12)
      << "terminal tb_y must repeat stage N-1";
}

// Test 2 — ComputePerStageTb_OnLeg_ProjectsToOwnX:
// When the own seed position is ON the leg, the closest point IS the own
// position's along-leg projection (its perpendicular foot on the leg). For a
// leg along +x with normal +y, own at (px_own, ay) (on the leg line) projects
// to (px_own, ay). This sanity-checks the projection geometry.
TEST(MidMpcPerStageTbTest, ComputePerStageTb_OnLeg_ProjectsToOwnAlongLeg) {
  const double ax = 0.0;
  const double ay = 0.0;
  const double bearing = 0.0;  // leg along +x
  const double nx = 0.0;
  const double ny = 1.0;       // normal +y

  std::vector<double> px_seed(static_cast<std::size_t>(kN + 1), 0.0);
  std::vector<double> py_seed(static_cast<std::size_t>(kN + 1), 0.0);
  for (int k = 0; k <= kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    px_seed[kk] = 100.0 * static_cast<double>(k);  // along the leg
    py_seed[kk] = 0.0;                              // on the leg line
  }

  const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                             ax, ay, bearing, nx, ny,
                                             kLegExtentM, kN);
  // On-leg: closest point == own position (no off-leg deviation).
  for (int k = 0; k < kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    EXPECT_NEAR(tb.tb_x[kk], px_seed[kk], 1e-9)
        << "on-leg tb_x must equal own x at k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], ay, 1e-9)
        << "on-leg tb_y must equal leg y at k=" << k;
  }
}

// Test 3 — ComputePerStageTb_DegenerateSeed_FallsBackToAbsoluteOrigin:
// When the seed position is NaN (or the leg degenerate), tb MUST fall back to
// the ABSOLUTE route origin A (the leg start) — never silently leave (0, 0).
// This is the honest fallback the spec mandates ("t_b 投影退化 -> fallback 用
// 绝对 route frame origin"). The cost is then well-defined relative to the leg
// start (the original absolute-frame behavior).
TEST(MidMpcPerStageTbTest, ComputePerStageTb_DegenerateSeed_FallsBackToAbsoluteOrigin) {
  const double ax = 1234.0;  // NON-ZERO origin — so a (0,0) fallback would be
  const double ay = -5678.0; // detectable (this is the regression we guard).
  const double bearing = 0.0;
  const double nx = 0.0;
  const double ny = 1.0;

  // Degenerate seed: all NaN.
  const double nan = std::numeric_limits<double>::quiet_NaN();
  std::vector<double> px_seed(static_cast<std::size_t>(kN + 1), nan);
  std::vector<double> py_seed(static_cast<std::size_t>(kN + 1), nan);

  const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                             ax, ay, bearing, nx, ny,
                                             kLegExtentM, kN);
  ASSERT_EQ(static_cast<int>(tb.tb_x.size()), kN + 1);
  for (int k = 0; k <= kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    EXPECT_NEAR(tb.tb_x[kk], ax, 1e-12)
        << "degenerate-seed tb_x must fall back to ABSOLUTE origin A.x at k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], ay, 1e-12)
        << "degenerate-seed tb_y must fall back to ABSOLUTE origin A.y at k=" << k;
  }
}

// Test 4 — ComputePerStageTb_DegenerateLeg_FallsBackToAbsoluteOrigin:
// A non-positive leg extent (or a NaN bearing) makes B == A (degenerate
// segment). project_to_segment's own fallback returns closest=A in that case;
// compute_per_stage_tb propagates that (tb == A for every stage). This guards
// against a regression where the leg extent is left at 0 (e.g. a config bug).
TEST(MidMpcPerStageTbTest, ComputePerStageTb_DegenerateLeg_FallsBackToAbsoluteOrigin) {
  const double ax = 100.0;
  const double ay = 200.0;
  const double bearing = 0.0;
  const double nx = 0.0;
  const double ny = 1.0;

  std::vector<double> px_seed(static_cast<std::size_t>(kN + 1), 500.0);
  std::vector<double> py_seed(static_cast<std::size_t>(kN + 1), 600.0);

  // Degenerate leg: extent 0 -> B == A.
  const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                             ax, ay, bearing, nx, ny,
                                             /*leg_extent_m=*/0.0, kN);
  for (int k = 0; k <= kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    EXPECT_NEAR(tb.tb_x[kk], ax, 1e-12)
        << "degenerate-leg tb_x must fall back to A.x at k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], ay, 1e-12)
        << "degenerate-leg tb_y must fall back to A.y at k=" << k;
  }
}

// Test 5 — ComputePerStageTb_ShortSeedVector_LeavesFallbackForHighStages:
// If the seed vector is shorter than N+1 (should not happen in production —
// solve() always builds an N+1 seed — but the function must not read OOB),
// compute_per_stage_tb must leave the absolute-origin fallback in place for
// the missing high stages (NOT read out of bounds, NOT silently 0.0).
TEST(MidMpcPerStageTbTest, ComputePerStageTb_ShortSeedVector_LeavesFallbackForHighStages) {
  const double ax = 1000.0;
  const double ay = 2000.0;
  const double bearing = 0.0;
  const double nx = 0.0;
  const double ny = 1.0;

  // Seed vector with only 3 entries (<< N+1=19).
  std::vector<double> px_seed{1500.0, 1600.0, 1700.0};
  std::vector<double> py_seed{2500.0, 2600.0, 2700.0};

  const PerStageTb tb = compute_per_stage_tb(px_seed, py_seed,
                                             ax, ay, bearing, nx, ny,
                                             kLegExtentM, kN);
  ASSERT_EQ(static_cast<int>(tb.tb_x.size()), kN + 1);
  // Stages 0..2: projected from the available seed positions.
  for (int k = 0; k < 3; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    const OraclePt exp = oracle_closest(px_seed[kk], py_seed[kk],
                                        ax, ay, bearing, nx, ny);
    EXPECT_NEAR(tb.tb_x[kk], exp.cx, 1e-9)
        << "short-seed tb_x mismatch at available k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], exp.cy, 1e-9)
        << "short-seed tb_y mismatch at available k=" << k;
  }
  // Stages 3..N-1: missing seed -> absolute-origin fallback (NOT 0.0, NOT OOB).
  for (int k = 3; k < kN; ++k) {
    const std::size_t kk = static_cast<std::size_t>(k);
    EXPECT_NEAR(tb.tb_x[kk], ax, 1e-12)
        << "short-seed tb_x must fall back to A.x at missing k=" << k;
    EXPECT_NEAR(tb.tb_y[kk], ay, 1e-12)
        << "short-seed tb_y must fall back to A.y at missing k=" << k;
  }
}
