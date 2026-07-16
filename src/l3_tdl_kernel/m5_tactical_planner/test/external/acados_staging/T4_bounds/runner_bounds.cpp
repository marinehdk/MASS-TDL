// P1b-0 T4 -- bound schedule per-stage lb/ub + OR-composition runner.
//
// Links the generated solver lib for the bound-schedule OCP (m5_staging_bounds)
// and proves the production per-cycle bound schedule maps to acatos via the
// per-stage activation-factor technique (T1 lesson) on TWO independent h-rows:
//
//   Row 0 (CPA suffix-hard):  h_cpa   = cpa_act * g_cpa
//   Row 1 (prefix equality):  h_pref  = pact_pre * (psi - ppsi_pre)
//
//   con_h_expr = vertcat(h_cpa, h_pref)   (nh = 2)
//   Stage-UNIFORM bounds: lh = [0, 0], uh = [UH_INF, 0]
//     (row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0<=h<=0).
//   Per-stage p = [cpa_act, ppsi_pre, pact_pre] (np=3):
//     k <  3 : cpa_act=0 (CPA SOFT/disabled), pact_pre=1, ppsi_pre=prefix_psi[k]
//     k >= 3 : cpa_act=1 (CPA HARD), pact_pre=0, ppsi_pre=0 (prefix FREE)
//
// This is the OR-composition: prefix-soften(k<3) UNION cpa-suffix-hard(k>=3).
// The two schedules are INDEPENDENT per-stage switches on two different rows.
//
// ====================  why per-stage activation (not idxsh slack)  =================
// acatos lh/uh/lbx/ubx/idxsh/Zl/zl are ALL stage-UNIFORM. The ONLY per-stage
// lever is the parameter vector p, set per stage via the GENERATED
// m5_staging_bounds_acados_update_params(capsule, stage, vals, np). T3's idxsh
// slack softens the SAME row at EVERY stage (stage-uniform), so it CANNOT give
// a k<K-soft / k>=K-hard split. The activation factor (cpa_act*g_cpa) zeros the
// row value AND its Jacobian/Hessian at k<K -> cleanly disables it there. This
// is the verified-clean path (T1 technique, applied to the CPA row).
//
// ====================  schedule reproduction assertions  =========================
// After solve (F5: acatos status 0 OR 4 tolerated -- the schedule assertions
// are what matter):
//   1. k < 3 (prefix-soften + CPA SOFT):
//        - CPA is SOFTENED (disabled) -> g_cpa is ALLOWED to be < 0 (the hard
//          constraint is off). Do NOT assert g_cpa>=0 here.
//        - prefix EQUALITY holds: |psi - prefix_psi[k]| < 1e-4 (T1 binding).
//   2. k >= 3 (prefix FREE + CPA HARD):
//        - CPA HARD: g_cpa >= -tol (tol=1e-6).
//        - prefix FREE: psi in box (no equality pin -- pact_pre=0 there).
//   3. Print per-stage g_cpa, psi, and the activated schedule (SOFT/HARD for
//      CPA, EQ/FREE for prefix).
//
// Scenario design: target (200,0), cpa_hard=100, box psi in [-1.2, 1.2]. Seed
// turns north fast enough to clear the disc by stage 3 (box-respecting, so the
// first QP starts box-feasible). The softened stages k<3 may dip into the disc
// (allowed); the hard stages k>=3 must be clear.
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_bounds.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int NP = 3;           // p = [cpa_act, ppsi_pre, pact_pre]
constexpr int N = 10;
constexpr int K_PREFIX = 3;         // prefix equality stages: k < K_PREFIX
constexpr int CPA_HARD_FROM_K = 3;  // CPA hard from this stage onward
constexpr double DT = 5.0;
constexpr double TARGET_X = 200.0;
constexpr double TARGET_Y = 0.0;
constexpr double CPA_HARD = 100.0;
constexpr double PSI_LB = -1.2;
constexpr double PSI_UB = 1.2;
constexpr double PSI_REF = 0.3;
constexpr double UH_INF = 1.0e10;
// Schedule tolerances (do NOT widen to hide a violation).
constexpr double PREFIX_EQ_TOL = 1.0e-4;  // |psi - prefix_psi[k]| < tol at k<3
constexpr double CPA_HARD_TOL = 1.0e-6;   // g_cpa >= -tol at k>=3
constexpr double BOX_TOL = 1.0e-6;        // psi in [lb,ub] + tol
// Prefix headings committed for stages k=0,1,2 (the "already sent to L4" tail).
const double PREFIX_PSI[K_PREFIX] = {0.1, 0.2, 0.3};
}  // namespace

// g_cpa = (px-tx)^2 + (py-ty)^2 - cpa_hard^2 (matches gen_bounds.py h_cpa form
// with cpa_act=1). Used to verify the HARD stages respect the CPA disc.
static inline double g_cpa_eval(double px, double py) {
  const double dx = px - TARGET_X;
  const double dy = py - TARGET_Y;
  return dx * dx + dy * dy - CPA_HARD * CPA_HARD;
}

int main() {
  int rc = 0;

  m5_staging_bounds_solver_capsule* capsule =
      m5_staging_bounds_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "BOUNDS FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_bounds_acados_create(capsule) != 0) {
    std::fprintf(stderr, "BOUNDS FAIL: acados_create failed\n");
    m5_staging_bounds_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_bounds_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_bounds_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_bounds_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_bounds_acados_get_nlp_in(capsule);

  // x0: origin. The committed prefix's FIRST heading (0.1) is the current
  // heading (production-faithful: the prefix is the tail already sent to L4, so
  // stage 0 == current state). This isolates the staging question from an
  // artificial initial-state conflict.
  double x0[NX] = {0.0, 0.0, PREFIX_PSI[0], 5.0};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage parameter p = [cpa_act, ppsi_pre, pact_pre]. This is the
  //      OR-composition schedule switch (the ONLY per-stage lever acatos
  //      gives; lh/uh are stage-uniform):
  //        k <  3 -> cpa_act=0.0 (CPA SOFT), ppsi_pre=prefix_psi[k],
  //                  pact_pre=1.0 (prefix EQ binds)
  //        k >= 3 -> cpa_act=1.0 (CPA HARD), ppsi_pre=0.0 (don't-care),
  //                  pact_pre=0.0 (prefix row disabled -> psi FREE)
  //      Set for ALL stages 0..N (terminal stage carries p too).
  //
  //      NOTE: acatos exposes params via the GENERATED per-stage setter
  //      <name>_acados_update_params(capsule, stage, vals, np), NOT via the
  //      generic ocp_nlp_in_set(..,"p",..) (that field is unavailable in this
  //      build). The generated wrapper is the C equivalent of the Python
  //      solver.set(k,"p",vals) (T1 lesson). ----
  for (int k = 0; k <= N; ++k) {
    double p_vals[NP] = {0.0, 0.0, 0.0};
    if (k < CPA_HARD_FROM_K) {
      p_vals[0] = 0.0;                 // cpa_act: CPA SOFT (row disabled)
      p_vals[1] = PREFIX_PSI[k];       // ppsi_pre: prefix heading target
      p_vals[2] = 1.0;                 // pact_pre: prefix EQ binds
    } else {
      p_vals[0] = 1.0;                 // cpa_act: CPA HARD (row binds)
      p_vals[1] = 0.0;                 // ppsi_pre: don't-care (pact_pre=0)
      p_vals[2] = 0.0;                 // pact_pre: prefix FREE (row disabled)
    }
    m5_staging_bounds_acados_update_params(capsule, k, p_vals, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero). Box-respecting
  //      (psi stays in [-1.2,1.2]) AND CPA-feasible at the HARD stages (clears
  //      the disc by stage 3): ramp the prefix for k=0,1, then a fast north
  //      turn at k=2 (psi 0.3 -> 1.2, dpsi=0.18 <= DPSI_MAX=0.2), hold psi=1.2
  //      for the suffix. By stage 3 the vessel is at py~15, dist~128 > 100 ->
  //      g_cpa ~ +6300 (feasible). A zero seed yields an ill-conditioned first
  //      QP. Mirrors subset_runner.cpp:64-78. ----
  double DPSI_MAX = 0.2;
  double x_seed[NX] = {x0[0], x0[1], x0[2], x0[3]};
  for (int k = 0; k <= N; ++k) {
    ocp_nlp_out_set(cfg, dims, out, k, "x", x_seed);
    if (k < N) {
      double dpsi = 0.0;
      if (k < K_PREFIX - 1) {
        // ramp along the prefix: psi goes PREFIX_PSI[k] -> PREFIX_PSI[k+1]
        dpsi = (PREFIX_PSI[k + 1] - PREFIX_PSI[k]) / DT;
      } else if (k == K_PREFIX - 1) {
        // fast north turn at k=2: psi 0.3 -> 1.2 (clear the disc by stage 3)
        dpsi = (1.2 - PREFIX_PSI[K_PREFIX - 1]) / DT;   // = 0.18
      } else {
        // hold psi=1.2 (north) for the suffix -- keeps py growing, CPA clear.
        dpsi = 0.0;
      }
      // clamp to the control box (DPSI_MAX=0.2)
      if (dpsi > DPSI_MAX) dpsi = DPSI_MAX;
      if (dpsi < -DPSI_MAX) dpsi = -DPSI_MAX;
      double u_seed[NU] = {dpsi};
      ocp_nlp_out_set(cfg, dims, out, k, "u", u_seed);
      double px = x_seed[0], py = x_seed[1], psi = x_seed[2], u = x_seed[3];
      double psi_new = psi + dpsi * DT;
      x_seed[0] = px + u * DT * std::cos(psi);
      x_seed[1] = py + u * DT * std::sin(psi);
      x_seed[2] = psi_new;
      // u unchanged (constant surge)
    }
  }

  int status = m5_staging_bounds_acados_solve(capsule);
  std::printf("BOUNDS: solver_status=%d\n", status);

  // ---- Schedule assertions. ----
  bool prefix_ok = true;       // k<3: |psi - prefix_psi[k]| < 1e-4
  bool cpa_hard_ok = true;     // k>=3 (stages 3..N-1 enforced): g_cpa >= -tol
  bool suffix_box_ok = true;   // k>=3: psi in heading box (prefix FREE)
  double prefix_max_err = 0.0;
  double cpa_hard_min_g = 1e9;  // tightest (smallest) g_cpa over HARD stages

  std::printf("BOUNDS: per-stage (k, px, py, psi, g_cpa, dist, schedule):\n");
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    double px = xk[0], py = xk[1], psi = xk[2];
    double g_cpa = g_cpa_eval(px, py);
    double dist = std::sqrt((px - TARGET_X) * (px - TARGET_X) +
                            (py - TARGET_Y) * (py - TARGET_Y));

    const char* cpa_tag = (k < CPA_HARD_FROM_K) ? "SOFT" : "HARD";
    const char* pre_tag = (k < K_PREFIX) ? "EQ " : "FREE";
    const char* viol = "";

    if (k < K_PREFIX) {
      // prefix-soften + CPA SOFT: prefix equality MUST hold; CPA allowed soft.
      double err = std::fabs(psi - PREFIX_PSI[k]);
      if (err > prefix_max_err) prefix_max_err = err;
      if (err > PREFIX_EQ_TOL) {
        prefix_ok = false;
        viol = "  <-- PREFIX VIOL";
      }
      // NO g_cpa>=0 assertion here (CPA is intentionally softened at k<3).
    } else {
      // prefix FREE + CPA HARD: g_cpa >= -tol AND psi in box.
      if (k < N && g_cpa < cpa_hard_min_g) cpa_hard_min_g = g_cpa;
      if (k < N && g_cpa < -CPA_HARD_TOL) {
        cpa_hard_ok = false;
        viol = "  <-- CPA HARD VIOL";
      }
      if (psi < PSI_LB - BOX_TOL || psi > PSI_UB + BOX_TOL) {
        suffix_box_ok = false;
        viol = "  <-- BOX VIOL";
      }
    }
    std::printf("BOUNDS: k=%2d px=%8.2f py=%8.2f psi=%+.4f g_cpa=%+10.1f "
                "dist=%7.1f CPA=%s prefix=%s%s\n",
                k, px, py, psi, g_cpa, dist, cpa_tag, pre_tag, viol);
  }

  // F5: acatos status 4 (QP error during refinement) is TOLERATED -- PASS on
  // schedule satisfaction, just report the actual status.
  bool status_ok = (status == 0 || status == 4);
  if (!status_ok) {
    std::fprintf(stderr, "BOUNDS FAIL: solver_status=%d (expected 0 or 4)\n",
                 status);
    rc = 1;
  } else if (!prefix_ok) {
    std::fprintf(stderr,
                 "BOUNDS FAIL: prefix equality violated at some k<%d "
                 "(max |psi-prefix|=%.3e, tol=1e-4)\n",
                 K_PREFIX, prefix_max_err);
    rc = 1;
  } else if (!cpa_hard_ok) {
    std::fprintf(stderr,
                 "BOUNDS FAIL: CPA HARD violated at some k>=%d "
                 "(min g_cpa=%.3e, tol=-1e-6)\n",
                 CPA_HARD_FROM_K, cpa_hard_min_g);
    rc = 1;
  } else if (!suffix_box_ok) {
    std::fprintf(stderr,
                 "BOUNDS FAIL: suffix heading box violated at some k>=%d\n",
                 K_PREFIX);
    rc = 1;
  }

  std::printf("BOUNDS: max |psi-prefix|(k<%d) = %.3e (tol 1e-4)\n",
              K_PREFIX, prefix_max_err);
  std::printf("BOUNDS: min g_cpa over HARD stages (3..%d) = %.3e (tol -1e-6)\n",
              N - 1, cpa_hard_min_g);
  std::printf("BOUNDS: acados solver_status=%d (0=converged; 4=QP error during "
              "refinement, tolerated)\n", status);

  m5_staging_bounds_acados_free(capsule);
  m5_staging_bounds_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf("BOUNDS PASS: per-stage schedule reproduces "
                "prefix-soften(k<3) union cpa-suffix-hard(k>=3) "
                "(status=%d)\n", status);
  }
  return rc;
}
