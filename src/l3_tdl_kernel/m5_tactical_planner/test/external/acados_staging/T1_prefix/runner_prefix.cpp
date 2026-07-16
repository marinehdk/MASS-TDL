// P1b-0 T1 -- prefix equality staging runner.
//
// Links the generated solver lib for the prefix-staging OCP and proves the
// staging maps to acados:
//   - k <  K : psi[k] == prefix_psi[k]   (hard equality via the parametric h-row)
//   - k >= K : psi[k] free, but in-box and CPA-feasible
//
// STRATEGY (a) parametric activation: a second h-row h_prefix = pact*(psi-ppsi)
// with stage-UNIFORM equality bounds lh[1]=uh[1]=0. The stage-dependence lives
// in the per-stage parameter p=[ppsi,pact]: k<K sets pact=1.0,ppsi=prefix;
// k>=K sets pact=0.0 (row becomes identically 0 -> imposes nothing on psi).
// This is the ONLY clean path because acados lh/uh/lbx/ubx/idxsh are all single
// stage-uniform arrays; `p` is the sole per-stage lever.
//
// Asserts (PASS criteria, F5 = acados status 4 tolerated):
//   1. solver status 0 OR 4 (both acceptable; actual reported).
//   2. for k<3: |psi[k] - prefix_psi[k]| < 1e-4.
//   3. for k>=3: PSI_LB-1e-6 <= psi[k] <= PSI_UB+1e-6 AND CPA feasible
//      ((px-tx)^2+(py-ty)^2 >= cpa_hard^2 - 1e-6).
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_prefix.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int NP = 2;           // p = [ppsi, pact]
constexpr int N = 10;
constexpr int K = 3;
constexpr double DT = 5.0;
constexpr double TARGET_X = 200.0;
constexpr double TARGET_Y = 0.0;
constexpr double CPA_HARD = 100.0;
constexpr double PSI_LB = -1.2;
constexpr double PSI_UB = 1.2;
constexpr double PSI_REF = 0.3;
// Prefix headings committed for stages k=0,1,2 (the "already sent to L4" tail).
const double PREFIX_PSI[K] = {0.1, 0.2, 0.3};
}  // namespace

int main() {
  int rc = 0;

  m5_staging_prefix_solver_capsule* capsule =
      m5_staging_prefix_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "PREFIX FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_prefix_acados_create(capsule) != 0) {
    std::fprintf(stderr, "PREFIX FAIL: acados_create failed\n");
    m5_staging_prefix_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_prefix_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_prefix_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_prefix_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_prefix_acados_get_nlp_in(capsule);

  // x0: origin. The committed prefix's FIRST heading (0.1) is the current
  // heading (production-faithful: the prefix is the tail already sent to L4, so
  // stage 0 == current state). This isolates the staging question from an
  // artificial initial-state conflict.
  double x0[NX] = {0.0, 0.0, PREFIX_PSI[0], 5.0};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage parameter p = [ppsi, pact]. This is the staging switch:
  //        k <  K -> pact=1.0, ppsi=prefix_psi[k] : enforce equality
  //        k >= K -> pact=0.0, ppsi=<don't care>  : row disabled, psi free
  //      Set for ALL stages 0..N (terminal stage carries p too).
  //
  //      NOTE: acados exposes params via the GENERATED per-stage setter
  //      <name>_acados_update_params(capsule, stage, vals, np), NOT via the
  //      generic ocp_nlp_in_set(..,"p",..) (that field is unavailable in this
  //      build). The generated wrapper is the C equivalent of the Python
  //      solver.set(k,"p",vals) used in the parametric-h reference example. ----
  for (int k = 0; k <= N; ++k) {
    double p_vals[NP] = {0.0, 0.0};
    if (k < K) {
      p_vals[0] = PREFIX_PSI[k];
      p_vals[1] = 1.0;            // enforce prefix equality
    } else {
      p_vals[0] = 0.0;            // don't-care (pact=0 zeros the row)
      p_vals[1] = 0.0;            // disable row -> psi free
    }
    m5_staging_prefix_acados_update_params(capsule, k, p_vals, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero). Tracks the prefix
  //      for k<3 then smoothly turns toward PSI_REF for the free suffix. A
  //      zero seed yields an ill-conditioned first QP. Mirrors
  //      subset_runner.cpp:64-78. ----
  double x_seed[NX] = {x0[0], x0[1], x0[2], x0[3]};
  for (int k = 0; k <= N; ++k) {
    ocp_nlp_out_set(cfg, dims, out, k, "x", x_seed);
    if (k < N) {
      double dpsi = 0.0;
      if (k < K - 1) {
        // ramp along the prefix: psi goes PREFIX_PSI[k] -> PREFIX_PSI[k+1]
        dpsi = (PREFIX_PSI[k + 1] - PREFIX_PSI[k]) / DT;
      } else {
        // free suffix: turn from PREFIX_PSI[K-1] toward PSI_REF
        double psi_now = (k == K - 1) ? PREFIX_PSI[K - 1] : x_seed[2];
        dpsi = (PSI_REF - psi_now) / DT;
      }
      // clamp to the control box (DPSI_MAX=0.2)
      if (dpsi > 0.2) dpsi = 0.2;
      if (dpsi < -0.2) dpsi = -0.2;
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

  int status = m5_staging_prefix_acados_solve(capsule);
  std::printf("PREFIX: solver_status=%d\n", status);

  bool prefix_ok = true;   // k<K: |psi - prefix_psi| < 1e-4
  bool suffix_box_ok = true;   // k>=K: in heading box
  bool cpa_ok = true;     // k<N: CPA disc respected
  double prefix_max_err = 0.0;

  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    double px = xk[0], py = xk[1], psi = xk[2];
    double cpa_dist2 = (px - TARGET_X) * (px - TARGET_X) +
                       (py - TARGET_Y) * (py - TARGET_Y);
    double cpa_floor = CPA_HARD * CPA_HARD;
    bool stage_cpa_ok = (cpa_dist2 + 1e-6) >= cpa_floor;
    // Stage CPA enforced on 0..N-1; terminal reported for visibility.
    if (k < N && !stage_cpa_ok) cpa_ok = false;

    const char* tag = "";
    if (k < K) {
      double err = std::fabs(psi - PREFIX_PSI[k]);
      if (err > prefix_max_err) prefix_max_err = err;
      if (err > 1e-4) {
        prefix_ok = false;
        tag = "  <-- PREFIX VIOL";
      }
    } else {
      if (psi < PSI_LB - 1e-6 || psi > PSI_UB + 1e-6) {
        suffix_box_ok = false;
        tag = "  <-- BOX VIOL";
      }
    }
    const char* kmark = (k < K) ? "(prefix)" : "(free)  ";
    std::printf("PREFIX: k=%2d%s px=%8.2f py=%8.2f psi=%+.4f cpa_dist=%.1f%s\n",
                k, kmark, px, py, psi, std::sqrt(cpa_dist2),
                tag);
  }

  // F5: acados status 4 (QP error during refinement) is TOLERATED -- PASS on
  // constraint satisfaction, just report the actual status.
  bool status_ok = (status == 0 || status == 4);
  if (!status_ok) {
    std::fprintf(stderr, "PREFIX FAIL: solver_status=%d (expected 0 or 4)\n",
                status);
    rc = 1;
  } else if (!prefix_ok) {
    std::fprintf(stderr,
                 "PREFIX FAIL: prefix equality violated at some k<%d "
                 "(max |psi-prefix|=%.3e, tol=1e-4)\n",
                 K, prefix_max_err);
    rc = 1;
  } else if (!suffix_box_ok) {
    std::fprintf(stderr, "PREFIX FAIL: suffix heading box violated at some k>=%d\n",
                K);
    rc = 1;
  } else if (!cpa_ok) {
    std::fprintf(stderr, "PREFIX FAIL: CPA constraint violated at some stage\n");
    rc = 1;
  }

  std::printf("PREFIX: max |psi-prefix|(k<%d) = %.3e (tol 1e-4)\n",
              K, prefix_max_err);
  std::printf("PREFIX: acados solver_status=%d (0=converged; 4=QP error during "
              "refinement, tolerated)\n", status);

  m5_staging_prefix_acados_free(capsule);
  m5_staging_prefix_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf("PREFIX PASS: strategy (a) parametric activation "
                "h=pact*(psi-ppsi), uniform equality bounds + per-stage p "
                "works (status=%d)\n", status);
  }
  return rc;
}
