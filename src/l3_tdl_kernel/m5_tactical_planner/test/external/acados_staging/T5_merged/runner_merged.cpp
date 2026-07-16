// P1b-0 T5 -- merged 4-point coexistence runner (the FINAL P1b-0 staging gate).
//
// Links the generated solver lib for the merged OCP (m5_staging_merged) and
// proves ALL FOUR verified complexity points (P1 prefix equality, P2 J_colreg
// per-stage EXTERNAL cost, P3 sigma slack, P4 CPA bound schedule) COEXIST in one
// acatos OCP and SOLVE together. This is the gate for P1b-1: if the four points
// compose without infeasibility / numerical interaction, staging is scalable and
// the full migration spec can be written.
//
// ====================  the merged OCP (one acatos solver)  ====================
//   con_h_expr = vertcat( cpa_act * g_cpa ,  pact_pre * (psi - ppsi_pre) )
//                                                                     (nh = 2)
//   Stage-UNIFORM bounds: lh = [0, 0], uh = [UH_INF, 0]
//     (row 0 CPA one-sided >= 0; row 1 prefix EQUALITY 0 <= h <= 0).
//   idxsh = [0]  -> soften row 0 (CPA), per-stage lower-slack xi_k.
//   cost_type = EXTERNAL  -> per-stage J_colreg form, cost_scaling=ones(N+1).
//
// ====================  CRITICAL composition: CPA row carries BOTH slack (P3)
//                          AND activation (P4)  ==============================
// Both P3 and P4 act on row 0. h_cpa = cpa_act*g_cpa; idxsh=[0] softens it; the
// solver sees h_cpa + xi_k >= 0 (xi_k >= 0).
//   k <  3 : cpa_act=0 -> h_cpa identically 0 -> `0 + xi_k >= 0` holds for any
//            xi_k>=0; the linear penalty zl*xi_k (zl=1e3) drives xi_k -> 0
//            (schedule-softened; slack trivially 0 -- activation + slack do NOT
//            conflict: the activation zeros the row, the slack has nothing to
//            relax, the penalty pushes it to 0).
//   k >= 3 : cpa_act=1 -> h_cpa = g_cpa enforced >= 0. This scenario is CPA-
//            feasible (seed clears the disc by stage 3), so xi_k -> 0 (nothing
//            to relax -> slack 0, exact-penalty). The activation controls WHETHER
//            the row binds; the slack controls HOW MUCH it may relax. ORTHOGONAL.
//
// ====================  per-stage parameter layout (np = 10)  =================
//   p = [ cpa_act, ppsi_pre, pact_pre,            // P1+P4 (indices 0,1,2)
//         disc_k,                                 // P2  (index 3)
//         txdrift_A, tydrift_A, tw_A,             // P2  (indices 4,5,6)
//         txdrift_B, tydrift_B, tw_B ]            // P2  (indices 7,8,9)
//   The ONLY per-stage lever acatos gives; set per stage via the GENERATED
//   m5_staging_merged_acados_update_params(capsule, stage, vals, np).
//     k <  3 : cpa_act=0 (SOFT), ppsi_pre=prefix_psi[k], pact_pre=1 (EQ binds)
//     k >= 3 : cpa_act=1 (HARD), ppsi_pre=0 (don't-care), pact_pre=0 (FREE)
//     disc_k = exp(-(k*DT)/T_DISCOUNT_S)
//     txdrift_tk = tx + ts*cos(tc)*(k*DT); tydrift_tk = ty + ts*sin(tc)*(k*DT)
//
// ====================  coexistence assertions (all 4 points)  ================
// F5: acatos status 0 OR 4 tolerated, BUT only if the solver MOVED
// (traj_delta>1e-6 -- T3 lesson: reject a status-4 that returns the seed
// unchanged). The 4-point assertions are what matter:
//   P1 prefix  : k<3 |psi - prefix_psi[k]| < 1e-4.
//   P2 colreg  : acatos_cost finite/reasonable AND (strong) |acatos_cost -
//                hand_lumped| < 1e-6 over the solved trajectory.
//   P3 slack   : exact-penalty -- max |xi_k| over HARD stages (k>=3) < 1e-3
//                (scenario is CPA-feasible -> slack ~0).
//   P4 schedule: k>=3 g_cpa >= -1e-6 (CPA hard); k<3 CPA softened (no assert).
//   + solution sensible (no NaN, suffix in box).
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_merged.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int NP = 10;          // p = [cpa_act, ppsi_pre, pact_pre, disc,
                                //      txdrift_A, tydrift_A, tw_A,
                                //      txdrift_B, tydrift_B, tw_B]
constexpr int N = 10;
constexpr int K_PREFIX = 3;         // prefix equality stages: k < K_PREFIX (P1)
constexpr int CPA_HARD_FROM_K = 3;  // CPA hard from this stage onward   (P4)
constexpr double DT = 5.0;

// ---- Constants (must match gen_merged.py EXACTLY) ----
constexpr double T_DISCOUNT_S = 100.0;
constexpr double ZETA = 5.0e-3;
constexpr double CPA_SAFE = 100.0;
constexpr double K_SQRT_GUARD = 1.0;
constexpr int NT = 2;
constexpr int NP_PER_TARGET = 3;
// scale_denom = max(1, Nt*N) = max(1, 2*10) = 20
constexpr double SCALE_DENOM = (NT * N > 1) ? static_cast<double>(NT * N) : 1.0;

// CPA h-constraint target/radius (baked into gen_merged.py).
constexpr double TARGET_X = 200.0;   // target A x (CPA disc center)
constexpr double TARGET_Y = 0.0;     // target A y
constexpr double CPA_HARD = 100.0;

constexpr double PSI_LB = -1.2;
constexpr double PSI_UB = 1.2;
constexpr double UH_INF = 1.0e10;

// Prefix headings committed for stages k=0,1,2 (the "already sent to L4" tail).
const double PREFIX_PSI[K_PREFIX] = {0.1, 0.2, 0.3};

// Param indices (must match gen_merged.py layout).
constexpr int I_CPA_ACT = 0;
constexpr int I_PPSI_PRE = 1;
constexpr int I_PACT_PRE = 2;
constexpr int I_DISC = 3;
constexpr int I_TGT0 = 4;

// ---- Two DISTINCT targets (T2's spec). raw: tx, ty, tc (rad), ts (m/s), tw. ----
// Target A: (200,0), heading WEST (pi) toward ownship, ts=2, tw=0.8.
// Target B: (50,150), heading SOUTH (-pi/2), ts=1.5, tw=0.5.
struct Target { double tx, ty, tc, ts, tw; };
constexpr Target TARGETS[NT] = {
    {200.0,   0.0,  M_PI,        2.0, 0.8},  // A
    { 50.0, 150.0, -M_PI / 2.0,  1.5, 0.5},  // B
};

// ---- Tolerances (do NOT widen to hide a violation). ----
constexpr double PREFIX_EQ_TOL = 1.0e-4;   // P1: |psi - prefix_psi[k]| at k<3
constexpr double CPA_HARD_TOL = 1.0e-6;    // P4: g_cpa >= -tol at k>=3
constexpr double BOX_TOL = 1.0e-6;         // psi in [lb,ub] + tol
constexpr double COST_EQUIV_TOL = 1.0e-6;  // P2: |acatos_cost - hand_lumped|
constexpr double HARD_SLACK_TOL = 1.0e-3;  // P3: max|xi_k| over HARD stages
constexpr double TRAJ_DELTA_TOL = 1.0e-6;  // F5 solver-moved guard
}  // namespace

// g_cpa = (px-tx)^2 + (py-ty)^2 - cpa_hard^2 (matches gen_merged.py h_cpa form
// with cpa_act=1).
static inline double g_cpa_eval(double px, double py) {
  const double dx = px - TARGET_X;
  const double dy = py - TARGET_Y;
  return dx * dx + dy * dy - CPA_HARD * CPA_HARD;
}

// Build the per-stage parameter vector p_k (np=10) for stage k. Layout:
//   [cpa_act, ppsi_pre, pact_pre, disc_k, {txdrift,tydrift,tw} x NT]
static void build_stage_params(int k, double p_out[NP]) {
  // P1+P4: schedule switches.
  if (k < CPA_HARD_FROM_K) {
    p_out[I_CPA_ACT] = 0.0;                          // CPA SOFT (row disabled)
    p_out[I_PPSI_PRE] = (k < K_PREFIX) ? PREFIX_PSI[k] : 0.0;  // prefix target
    p_out[I_PACT_PRE] = (k < K_PREFIX) ? 1.0 : 0.0;            // EQ binds k<K
  } else {
    p_out[I_CPA_ACT] = 1.0;                          // CPA HARD (row binds)
    p_out[I_PPSI_PRE] = 0.0;                         // don't-care (pact=0)
    p_out[I_PACT_PRE] = 0.0;                         // prefix FREE
  }
  // P2: TCPA discount + per-target drift/weight.
  const double k_dt = k * DT;
  p_out[I_DISC] = std::exp(-k_dt / T_DISCOUNT_S);
  for (int t = 0; t < NT; ++t) {
    const Target& tgt = TARGETS[t];
    const double tdx = tgt.ts * std::cos(tgt.tc);
    const double tdy = tgt.ts * std::sin(tgt.tc);
    const int base = I_TGT0 + NP_PER_TARGET * t;
    p_out[base + 0] = tgt.tx + tdx * k_dt;           // txdrift
    p_out[base + 1] = tgt.ty + tdy * k_dt;           // tydrift
    p_out[base + 2] = tgt.tw;                        // tw
  }
}

// Hand-compute the lumped J_colreg over the solved trajectory (stages 0..N-1),
// using the EXACT production formula and the SAME per-stage params passed to the
// solver. Reference the acatos staged cost must match.
//   J_colreg = (1/scale_denom) * sum_{k=0}^{N-1} sum_t tw_t*disc_k*barrier_{t,k}
static double hand_compute_lumped(const double px_traj[N + 1],
                                  const double py_traj[N + 1]) {
  double cost = 0.0;
  for (int k = 0; k < N; ++k) {           // production sums k=0..N-1
    double p_k[NP];
    build_stage_params(k, p_k);
    const double disc_k = p_k[I_DISC];
    for (int t = 0; t < NT; ++t) {
      const int base = I_TGT0 + NP_PER_TARGET * t;
      const double txdrift = p_k[base + 0];
      const double tydrift = p_k[base + 1];
      const double tw = p_k[base + 2];
      const double dx = px_traj[k] - txdrift;
      const double dy = py_traj[k] - tydrift;
      const double d = std::sqrt(dx * dx + dy * dy + K_SQRT_GUARD);
      const double barrier = std::exp(-ZETA * (d - CPA_SAFE));
      cost += tw * disc_k * barrier;
    }
  }
  return cost / SCALE_DENOM;
}

int main() {
  int rc = 0;

  m5_staging_merged_solver_capsule* capsule =
      m5_staging_merged_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "MERGED FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_merged_acados_create(capsule) != 0) {
    std::fprintf(stderr, "MERGED FAIL: acados_create failed\n");
    m5_staging_merged_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_merged_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_merged_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_merged_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_merged_acados_get_nlp_in(capsule);
  ocp_nlp_solver* solver = m5_staging_merged_acados_get_nlp_solver(capsule);

  // x0: origin. The committed prefix's FIRST heading (0.1) is the current
  // heading (production-faithful: the prefix is the tail already sent to L4, so
  // stage 0 == current state). Isolates the staging from an initial-state
  // conflict (T1/T4 pattern).
  double x0[NX] = {0.0, 0.0, PREFIX_PSI[0], 5.0};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage parameter p (np=10). The merged schedule switch (ONLY per-
  //      stage lever acatos gives): P1+P4 first 3, P2 last 7. Set for ALL
  //      stages 0..N (terminal carries p too). Uses the GENERATED per-stage
  //      setter (T1 lesson -- generic ocp_nlp_in_set "p" does NOT work). ----
  for (int k = 0; k <= N; ++k) {
    double p_k[NP];
    build_stage_params(k, p_k);
    m5_staging_merged_acados_update_params(capsule, k, p_k, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero, box-respecting AND
  //      CPA-feasible at the HARD stages -- clears the disc by stage 3 so the
  //      hard stages k>=3 are feasible and sigma ~0). T4's verified seed:
  //      ramp the prefix for k=0,1 (psi 0.1->0.2->0.3), then a fast north turn
  //      at k=2 (psi 0.3->1.2, dpsi=0.18 <= DPSI_MAX=0.2), hold psi=1.2 for the
  //      suffix. By stage 3 the vessel is at py~15, dist~128 > 100 -> g_cpa
  //      ~+6300 (feasible). A zero seed yields an ill-conditioned first QP
  //      (P1a finding). Mirrors subset_runner.cpp:64-78. ----
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

  // Snapshot the seed trajectory BEFORE solve to detect a non-converged solve
  // that returns the seed unchanged (status 4 on the first QP). F5 honest-
  // movement guard (T3 lesson).
  double px_seed_snap[N + 1] = {0}, py_seed_snap[N + 1] = {0};
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_seed_snap[k] = xk[0];
    py_seed_snap[k] = xk[1];
  }

  int status = m5_staging_merged_acados_solve(capsule);
  std::printf("MERGED: solver_status=%d\n", status);

  // ---- Extract solved trajectory + per-stage CPA slack xi_k (P3). ----
  double px_traj[N + 1] = {0}, py_traj[N + 1] = {0}, psi_traj[N + 1] = {0};
  double xi_traj[N + 1] = {0};   // CPA lower-slack per stage (NSH=1)
  double g_cpa_traj[N + 1] = {0};
  bool any_nan = false;
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_traj[k] = xk[0];
    py_traj[k] = xk[1];
    psi_traj[k] = xk[2];
    g_cpa_traj[k] = g_cpa_eval(xk[0], xk[1]);
    if (!std::isfinite(px_traj[k]) || !std::isfinite(py_traj[k]) ||
        !std::isfinite(psi_traj[k])) {
      any_nan = true;
    }
    // Slack: ocp_nlp_out_get(stage, "sl", sl_vec), length ns = 1 (path stages).
    double sl_vec[1] = {0.0};
    if (k < N) {
      ocp_nlp_out_get(cfg, dims, out, k, "sl", sl_vec);
    }
    xi_traj[k] = sl_vec[0];
    if (!std::isfinite(xi_traj[k])) any_nan = true;
  }

  // Did the solver actually move from the seed? (F5 honest-movement guard.)
  double traj_delta = 0.0;
  for (int k = 0; k <= N; ++k) {
    traj_delta += std::fabs(px_traj[k] - px_seed_snap[k]) +
                  std::fabs(py_traj[k] - py_seed_snap[k]);
  }
  const bool solver_moved = (status == 0) || (traj_delta > TRAJ_DELTA_TOL);

  // ---- P2: acatos total cost + hand-computed lumped equivalence. ----
  ocp_nlp_eval_cost(solver, in, out);
  double acados_cost = 0.0;
  ocp_nlp_get(solver, "cost_value", &acados_cost);
  const double hand_lumped = hand_compute_lumped(px_traj, py_traj);
  const double cost_diff = std::fabs(acados_cost - hand_lumped);
  const bool cost_finite = std::isfinite(acados_cost) &&
                           std::isfinite(hand_lumped);

  // ---- Per-stage printout (k, px, py, psi, g_cpa, xi_k, schedule). ----
  std::printf("MERGED: per-stage (k, px, py, psi, g_cpa, xi_k, schedule):\n");
  double prefix_max_err = 0.0;
  double cpa_hard_min_g = 1e9;     // tightest g_cpa over HARD stages
  double hard_max_slack = 0.0;     // max |xi_k| over HARD stages (P3)
  for (int k = 0; k <= N; ++k) {
    const char* cpa_tag = (k < CPA_HARD_FROM_K) ? "SOFT" : "HARD";
    const char* pre_tag = (k < K_PREFIX) ? "EQ " : "FREE";
    std::printf(
        "MERGED:   k=%2d px=%8.2f py=%8.2f psi=%+.4f g_cpa=%+11.1f "
        "xi_k=%+.3e CPA=%s prefix=%s\n",
        k, px_traj[k], py_traj[k], psi_traj[k], g_cpa_traj[k], xi_traj[k],
        cpa_tag, pre_tag);
    if (k < K_PREFIX) {
      const double err = std::fabs(psi_traj[k] - PREFIX_PSI[k]);
      if (err > prefix_max_err) prefix_max_err = err;
    }
    if (k >= CPA_HARD_FROM_K && k < N) {
      if (g_cpa_traj[k] < cpa_hard_min_g) cpa_hard_min_g = g_cpa_traj[k];
      if (std::fabs(xi_traj[k]) > hard_max_slack) {
        hard_max_slack = std::fabs(xi_traj[k]);
      }
    }
  }
  std::printf("MERGED: traj_delta=%.3e solver_moved=%d\n", traj_delta,
              solver_moved ? 1 : 0);
  std::printf("MERGED: acados_cost=%.12e  hand_lumped=%.12e  "
              "|diff|=%.3e (tol %.0e)\n",
              acados_cost, hand_lumped, cost_diff, COST_EQUIV_TOL);
  std::printf("MERGED: P1 prefix max|psi-prefix|(k<%d) = %.3e (tol %.0e)\n",
              K_PREFIX, prefix_max_err, PREFIX_EQ_TOL);
  std::printf("MERGED: P4 CPA HARD min g_cpa over stages %d..%d = %.3e "
              "(tol -%.0e)\n",
              CPA_HARD_FROM_K, N - 1, cpa_hard_min_g, CPA_HARD_TOL);
  std::printf("MERGED: P3 sigma slack max|xi_k| over HARD stages = %.3e "
              "(tol %.0e)\n",
              hard_max_slack, HARD_SLACK_TOL);

  // ---- Coexistence assertions (all 4 points). ----
  bool status_ok = (status == 0 || status == 4);
  bool prefix_ok = (prefix_max_err < PREFIX_EQ_TOL);
  bool cpa_hard_ok = (cpa_hard_min_g >= -CPA_HARD_TOL);
  bool slack_ok = (hard_max_slack < HARD_SLACK_TOL);
  bool cost_ok = cost_finite && (cost_diff < COST_EQUIV_TOL);
  // Suffix box check (k>=K_PREFIX).
  bool suffix_box_ok = true;
  for (int k = K_PREFIX; k <= N; ++k) {
    if (psi_traj[k] < PSI_LB - BOX_TOL || psi_traj[k] > PSI_UB + BOX_TOL) {
      suffix_box_ok = false;
      break;
    }
  }

  if (any_nan) {
    std::fprintf(stderr, "MERGED FAIL: trajectory contains NaN/inf\n");
    rc = 1;
  } else if (!status_ok) {
    std::fprintf(stderr, "MERGED FAIL: solver_status=%d (expected 0 or 4)\n",
                 status);
    rc = 1;
  } else if (status != 0 && !solver_moved) {
    // F5 guard: status 4 tolerated ONLY if the solver MOVED (T3 lesson).
    std::fprintf(stderr,
                 "MERGED FAIL: status=%d but solver did NOT move "
                 "(traj_delta=%.3e < %.0e) -- seed read-back rejected\n",
                 status, traj_delta, TRAJ_DELTA_TOL);
    rc = 1;
  } else if (!prefix_ok) {
    std::fprintf(stderr,
                 "MERGED FAIL (P1 prefix): max|psi-prefix|=%.3e >= tol %.0e "
                 "at some k<%d\n",
                 prefix_max_err, PREFIX_EQ_TOL, K_PREFIX);
    rc = 1;
  } else if (!cost_ok) {
    std::fprintf(stderr,
                 "MERGED FAIL (P2 colreg): cost not equivalent -- "
                 "acados=%.12e hand=%.12e |diff|=%.3e (tol %.0e) finite=%d\n",
                 acados_cost, hand_lumped, cost_diff, COST_EQUIV_TOL,
                 cost_finite);
    rc = 1;
  } else if (!slack_ok) {
    std::fprintf(stderr,
                 "MERGED FAIL (P3 slack): max|xi_k| over HARD stages=%.3e >= "
                 "tol %.0e (expected ~0 in this CPA-feasible scenario)\n",
                 hard_max_slack, HARD_SLACK_TOL);
    rc = 1;
  } else if (!cpa_hard_ok) {
    std::fprintf(stderr,
                 "MERGED FAIL (P4 schedule): CPA HARD min g_cpa=%.3e < -%.0e "
                 "at some k>=%d\n",
                 cpa_hard_min_g, CPA_HARD_TOL, CPA_HARD_FROM_K);
    rc = 1;
  } else if (!suffix_box_ok) {
    std::fprintf(stderr,
                 "MERGED FAIL: suffix heading box violated at some k>=%d\n",
                 K_PREFIX);
    rc = 1;
  }

  m5_staging_merged_acados_free(capsule);
  m5_staging_merged_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf(
        "MERGED PASS: 4 complexity points coexist, staging scalable -> "
        "P1b-1 全量 spec 可写 "
        "(prefix-EQ + J_colreg EXTERNAL + sigma slack + CPA bound schedule; "
        "status=%d, acados_cost=%.6e, |acatos-hand|=%.2e, "
        "max|xi_k|(hard)=%.2e, min g_cpa(hard)=%.3e)\n",
        status, acados_cost, cost_diff, hard_max_slack, cpa_hard_min_g);
  }
  return rc;
}
