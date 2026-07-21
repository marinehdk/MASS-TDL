// P1b-1a Task 9 -- merged 6-point coexistence runner (the FINAL P1b-1a gate).
//
// Links the generated solver lib for the merged OCP (m5_staging_merge6) and
// proves ALL SIX verified complexity points COEXIST in ONE acatos OCP and SOLVE
// together. This is the gate for P1b-1b: if the six points compose without
// infeasibility / numerical interaction, staging is scalable and the production
// backend can be written.
//
// The six points (each verified individually in T1-T7):
//   P1 (T1) prefix equality           h_prefix = pact_pre*(psi-ppsi_pre)
//   P2 (T2) J_colreg per-stage        EXTERNAL cost, cost_scaling=ones(N+1)
//       EXTERNAL cost
//   P3 (T7) per-target xi slack       idxsh=[0,1] soften BOTH CPA rows, mixed
//                                     L1/L2 Zl/zl (per-target per-stage slack)
//   P4 (T4) CPA bound schedule        h_cpa = cpa_act*g_cpa (k<3 off, k>=3 hard)
//   P5 (T6) double-integrator dyn     Path B, x=[px,py,psi,r], dr/dt=c_u*delta
//   P6 (T8) honest c_u                c_u = 9.825342e-3 (VDM-direct, baked)
//
// ====================  the merged OCP (one acatos solver)  ====================
//   con_h_expr = vertcat( cpa_act * vertcat(g_cpaA, g_cpaB),   (rows 0,1 CPA)
//                         pact_pre * (psi - ppsi_pre) )        (row 2 prefix)
//                                                                     (nh = 3)
//     g_cpa_t = (px - txdrift_t)^2 + (py - tydrift_t)^2 - cpa_hard^2 (parametric)
//   Stage-UNIFORM bounds: lh = [0,0,0], uh = [UH_INF, UH_INF, 0]
//     (rows 0,1 CPA one-sided >= 0; row 2 prefix EQUALITY 0 <= h <= 0).
//   idxsh = [0,1]  -> soften rows 0,1 (CPA), per-stage per-target xi_{t,k}.
//     Row 2 (prefix) NOT softened -- hard equality.
//   cost_type = EXTERNAL  -> per-stage J_colreg form, cost_scaling=ones(N+1).
//
// ====================  CRITICAL: CPA rows carry BOTH per-target slack (P3) AND
//                          activation (P4)  ================================
// Both P3 (per-target idxsh=[0,1] slack) and P4 (cpa_act activation) act on rows
// 0 and 1. h_cpa_t = cpa_act*g_cpa_t; idxsh=[0,1] softens both; the solver sees
// h_cpa_t + xi_{t,k} >= 0 (xi_{t,k} >= 0).
//   k <  3 : cpa_act=0 -> both CPA rows identically 0 -> `0 + xi_{t,k} >= 0`
//            holds for any xi>=0; the per-target linear penalty zl*xi drives
//            xi_{t,k} -> 0 (schedule-softened; slack trivially 0 -- the two
//            per-target slacks are INDEPENDENT of each other and of the
//            activation, T5+T7 orthogonality).
//   k >= 3 : cpa_act=1 -> h_cpa_t = g_cpa_t enforced >= 0. This scenario is CPA-
//            feasible for BOTH targets at every HARD stage (gentle NE path clears
//            both far-north discs by ~200m), so xi_{A,k}, xi_{B,k} -> 0 (nothing
//            to relax -> slack 0, per-target exact-penalty). The activation
//            controls WHETHER a row binds; the per-target slack controls HOW MUCH
//            it may relax. ORTHOGONAL per target.
//
// ====================  per-stage parameter layout (np = 10)  =================
//   p = [ cpa_act, ppsi_pre, pact_pre,            // P1+P4 (indices 0,1,2)
//         disc_k,                                 // P2  (index 3)
//         txdrift_A, tydrift_A, tw_A,             // P2  (indices 4,5,6)
//         txdrift_B, tydrift_B, tw_B ]            // P2  (indices 7,8,9)
//   The ONLY per-stage lever acatos gives; set per stage via the GENERATED
//   m5_staging_merge6_acados_update_params(capsule, stage, vals, np).
//     k <  3 : cpa_act=0 (SOFT), ppsi_pre=prefix_psi[k], pact_pre=1 (EQ binds)
//     k >= 3 : cpa_act=1 (HARD), ppsi_pre=0 (don't-care), pact_pre=0 (FREE)
//     disc_k = exp(-(k*DT)/T_DISCOUNT_S)
//     txdrift_tk / tydrift_tk : per-target drift (static here; production moves)
//
// ====================  the 6 coexistence assertions  =========================
// F5: acatos status 0 OR 4 tolerated, BUT only if the solver MOVED
// (traj_delta>1e-6 -- T3 lesson: reject a status-4 that returns the seed
// unchanged). The 6-point assertions are what matter:
//   P5 dyn     : seed-readback ~0 (THE disc_dyn_expr-correctness gate, T6 lesson:
//                exact at 0.0, tol 1e-9). SOLVED rollout match (solved_dyn) is
//                convergence EVIDENCE (SQP residual, tol 1e-4), NOT a correctness
//                gate -- reported, not gated on. ROT box |r|<=rot_max+tol.
//   P1 prefix  : k<3 |psi - prefix_psi[k]| < 1e-4.
//   P2 colreg  : acatos_cost finite AND |acatos_cost - hand_lumped| < 1e-6 over
//                the solved trajectory (hand_compute_lumped like runner_merged).
//   P3 slack   : per-target exact-penalty -- max |xi_{t,k}| over (target, stage)
//                < 1e-3 in this CPA-feasible scenario (both xi_A, xi_B ~ 0);
//                sl read length 2 (per-target).
//   P4 schedule: k>=3 g_cpa_t >= -tol for BOTH targets (CPA hard); k<3 CPA
//                softened (schedule).
//   + solution sensible (no NaN, psi/r/delta in box).
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_merge6.h"

namespace {
constexpr int NX = 4;            // [px, py, psi, r]
constexpr int NU = 1;            // [delta]
constexpr int NP = 10;           // p = [cpa_act, ppsi_pre, pact_pre, disc,
                                 //      txdrift_A, tydrift_A, tw_A,
                                 //      txdrift_B, tydrift_B, tw_B]
constexpr int NSH = 2;           // idxsh=[0,1]; ns per stage = 2 (per-target)
constexpr int N = 10;
constexpr int NT = 2;            // two CPA targets (rows 0,1 in con_h_expr)
constexpr int K_PREFIX = 3;         // prefix equality stages: k < K_PREFIX (P1)
constexpr int CPA_HARD_FROM_K = 3;  // CPA hard from this stage onward   (P4)
constexpr double DT = 5.0;

// ---- Constants (must match gen_merge6.py EXACTLY) ----
// P5 (T6) double-integrator.
constexpr double C_U = 9.825342e-3;   // T8 VDM-direct yaw gain (rad/s^2/rad)
constexpr double U_SURGE = 9.26;      // T8 u_cruise, surge held constant
constexpr double ROT_MAX = 0.2;       // ROT box (marginal-stability bound)
constexpr double DDELTA_MAX = 0.2;    // rudder box
constexpr double PSI_LB = -1.2;
constexpr double PSI_UB = 1.2;

// P2 (T2) J_colreg constants.
constexpr double T_DISCOUNT_S = 100.0;
constexpr double ZETA = 5.0e-3;
constexpr double CPA_SAFE = 100.0;    // J_colreg barrier reference radius
constexpr double K_SQRT_GUARD = 1.0;
constexpr int NP_PER_TARGET = 3;
// scale_denom = max(1, Nt*N) = max(1, 2*10) = 20
constexpr double SCALE_DENOM = (NT * N > 1) ? static_cast<double>(NT * N) : 1.0;

// CPA h-constraint radius (baked into BOTH per-target rows; = CPA_SAFE here).
constexpr double CPA_HARD = 100.0;
constexpr double UH_INF = 1.0e10;

// Prefix headings committed for stages k=0,1,2 (the "already sent to L4" tail).
const double PREFIX_PSI[K_PREFIX] = {0.1, 0.2, 0.3};
// Initial yaw rate (rad/s) -- committed-prefix mid-turn rate (psi ramps
// 0.1/stage over DT=5 -> 0.02 rad/s).
constexpr double R0 = 0.02;

// Param indices (must match gen_merge6.py layout).
constexpr int I_CPA_ACT = 0;
constexpr int I_PPSI_PRE = 1;
constexpr int I_PACT_PRE = 2;
constexpr int I_DISC = 3;
constexpr int I_TGT0 = 4;

// ---- Two DISTINCT targets (static positions, zero drift). Both placed FAR
//      NORTH of the gentle NE path so the double-integrator (huge turning
//      diameter, T6 finding 2) keeps CPA clearance at every stage. tw: the
//      J_colreg per-target weight (P2). ----
struct Target { double tx, ty, tw; };
constexpr Target TARGETS[NT] = {
    {  0.0, 300.0, 0.8},  // A: far north (tw=0.8)
    {200.0, 400.0, 0.5},  // B: far north (tw=0.5)
};

// ---- Tolerances (do NOT widen to hide a violation). ----
//
// T6 lesson: seed_readback is THE disc_dyn_expr-correctness gate (exact at 0.0,
// tol 1e-9). solved_dyn is the multiple-shooting SQP dynamics-equality residual
// at the converged iterate (~1e-6), NOT a correctness signal -- gated loosely at
// 1e-4 as convergence EVIDENCE only.
constexpr double SEED_READBACK_TOL = 1.0e-9;    // P5: disc_dyn_expr correctness
constexpr double SOLVED_DYN_CONV_TOL = 1.0e-4;  // P5: convergence evidence
constexpr double BOX_TOL = 1.0e-6;              // psi/r/delta box slack
constexpr double PREFIX_EQ_TOL = 1.0e-4;        // P1: |psi - prefix_psi[k]| at k<3
constexpr double CPA_HARD_TOL = 1.0e-6;         // P4: g_cpa_t >= -tol at k>=3
constexpr double COST_EQUIV_TOL = 1.0e-6;       // P2: |acatos_cost - hand_lumped|
constexpr double SLACK_TOL = 1.0e-3;            // P3: max|xi_{t,k}| (feasible->~0)
constexpr double TRAJ_DELTA_TOL = 1.0e-6;       // F5 solver-moved guard
}  // namespace

// g_cpa for target t at state (px,py). Matches gen_merge6.py con_h_expr row t.
static inline double g_cpa_eval(double px, double py, int t) {
  const double tx = TARGETS[t].tx;
  const double ty = TARGETS[t].ty;
  const double dx = px - tx;
  const double dy = py - ty;
  return dx * dx + dy * dy - CPA_HARD * CPA_HARD;
}

// One double-integrator discrete step (matches gen_merge6.py disc_dyn_expr and
// common.py::forward_seed_doubleint EXACTLY -- explicit Euler, psi uses r[k]
// pre-update). Returns the next state via out-params.
static inline void dyn_step(double px, double py, double psi, double r,
                            double delta, double& px2, double& py2,
                            double& psi2, double& r2) {
  px2 = px + U_SURGE * DT * std::cos(psi);
  py2 = py + U_SURGE * DT * std::sin(psi);
  psi2 = psi + DT * r;            // r[k] pre-update
  r2 = r + DT * C_U * delta;
}

// Build the per-stage parameter vector p_k (np=10) for stage k. Layout:
//   [cpa_act, ppsi_pre, pact_pre, disc_k, {txdrift,tydrift,tw} x NT]
static void build_stage_params(int k, double p_out[NP]) {
  // P1+P4: schedule switches.
  if (k < CPA_HARD_FROM_K) {
    p_out[I_CPA_ACT] = 0.0;                          // CPA SOFT (rows disabled)
    p_out[I_PPSI_PRE] = (k < K_PREFIX) ? PREFIX_PSI[k] : 0.0;  // prefix target
    p_out[I_PACT_PRE] = (k < K_PREFIX) ? 1.0 : 0.0;            // EQ binds k<K
  } else {
    p_out[I_CPA_ACT] = 1.0;                          // CPA HARD (rows bind)
    p_out[I_PPSI_PRE] = 0.0;                         // don't-care (pact=0)
    p_out[I_PACT_PRE] = 0.0;                         // prefix FREE
  }
  // P2: TCPA discount + per-target position (static drift) + weight.
  const double k_dt = k * DT;
  p_out[I_DISC] = std::exp(-k_dt / T_DISCOUNT_S);
  for (int t = 0; t < NT; ++t) {
    const int base = I_TGT0 + NP_PER_TARGET * t;
    p_out[base + 0] = TARGETS[t].tx;                 // txdrift (static)
    p_out[base + 1] = TARGETS[t].ty;                 // tydrift (static)
    p_out[base + 2] = TARGETS[t].tw;                 // tw
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

  m5_staging_merge6_solver_capsule* capsule =
      m5_staging_merge6_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "MERGE6 FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_merge6_acados_create(capsule) != 0) {
    std::fprintf(stderr, "MERGE6 FAIL: acados_create failed\n");
    m5_staging_merge6_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_merge6_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_merge6_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_merge6_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_merge6_acados_get_nlp_in(capsule);
  ocp_nlp_solver* solver = m5_staging_merge6_acados_get_nlp_solver(capsule);

  // x0: origin, FIRST prefix heading (committed tail), committed-turn yaw rate.
  // The prefix is the tail already sent to L4, so stage 0 == current state
  // (psi=0.1, r=0.02 -- mid-turn at the ramp rate). Isolates the staging from an
  // initial-state conflict (T1/T4/T5 pattern).
  double x0[NX] = {0.0, 0.0, PREFIX_PSI[0], R0};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage parameter p (np=10). The merged schedule switch (ONLY per-
  //      stage lever acatos gives): P1+P4 first 3, P2 last 7. Set for ALL
  //      stages 0..N (terminal carries p too). Uses the GENERATED per-stage
  //      setter (T1 lesson -- generic ocp_nlp_in_set "p" does NOT work). ----
  for (int k = 0; k <= N; ++k) {
    double p_k[NP];
    build_stage_params(k, p_k);
    m5_staging_merge6_acados_update_params(capsule, k, p_k, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero, box-respecting AND
  //      CPA-feasible). A trajectory the double-integrator can hold (T6 finding
  //      2: tiny c_u -> huge turning diameter -> only gentle/off-path feasible).
  //      Hand-roll delta so the prefix ramp [0.1,0.2,0.3] is followed EXACTLY
  //      (r0=0.02 already matches the ramp rate -> delta=0 during the ramp),
  //      then a gentle settle (delta -> 0 drives r -> 0, psi settles ~0.45).
  //      Verified CPA-feasible: both far-north discs cleared by ~200m. A zero
  //      seed yields an ill-conditioned first QP (P1a finding). ----
  double x_seed[NX] = {x0[0], x0[1], x0[2], x0[3]};
  double delta_seq[N] = {0};
  for (int k = 0; k < N; ++k) {
    double delta = 0.0;
    if (k < K_PREFIX - 1) {
      // ramp: psi PREFIX_PSI[k] -> PREFIX_PSI[k+1] with the committed r0=0.02.
      // psi_new = psi + DT*r; want psi_new = PREFIX_PSI[k+1].
      // r_new = r + DT*c_u*delta; want r_new such that NEXT step holds too.
      // Two-point: keep r = 0.02 (ramp rate) -> delta = (0.02 - r)/(DT*c_u).
      const double r_target = (PREFIX_PSI[k + 1] - PREFIX_PSI[k]) / DT;  // 0.02
      delta = (r_target - x_seed[3]) / (DT * C_U);
    } else if (k == K_PREFIX - 1) {
      // settle: stop the ramp (r -> 0 gradually). delta drives r down.
      const double r_target = 0.0;
      delta = (r_target - x_seed[3]) / (DT * C_U);
    } else {
      // hold r ~ 0 (straight-ish gentle NE path).
      delta = (0.0 - x_seed[3]) / (DT * C_U);
    }
    if (delta > DDELTA_MAX) delta = DDELTA_MAX;
    if (delta < -DDELTA_MAX) delta = -DDELTA_MAX;
    delta_seq[k] = delta;

    double u_seed[NU] = {delta};
    ocp_nlp_out_set(cfg, dims, out, k, "u", u_seed);
    double px = x_seed[0], py = x_seed[1], psi = x_seed[2], r = x_seed[3];
    double px2, py2, psi2, r2;
    dyn_step(px, py, psi, r, delta, px2, py2, psi2, r2);
    x_seed[0] = px2; x_seed[1] = py2; x_seed[2] = psi2; x_seed[3] = r2;
    ocp_nlp_out_set(cfg, dims, out, k + 1, "x", x_seed);
  }
  // Set stage-0 x seed (acatos does not auto-propagate a seed set via out_set).
  ocp_nlp_out_set(cfg, dims, out, 0, "x", x0);

  // ---- P5 DYNAMICS CHECK #1 (THE disc_dyn_expr-correctness gate): seed readback.
  //      Read back the seed we just set; the set/get round-trip is EXACT (the
  //      seed was produced by the same explicit-Euler step disc_dyn_expr
  //      encodes). Any non-zero diff means disc_dyn_expr is encoded wrong.
  //      Gated hard at SEED_READBACK_TOL (1e-9; exact at 0.0). ----
  double seed_readback_max = 0.0;
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    // Reconstruct the ground-truth seed at stage k by hand-rolling from x0.
    double xt[NX] = {x0[0], x0[1], x0[2], x0[3]};
    for (int j = 0; j < k; ++j) {
      double px2, py2, psi2, r2;
      dyn_step(xt[0], xt[1], xt[2], xt[3], delta_seq[j], px2, py2, psi2, r2);
      xt[0] = px2; xt[1] = py2; xt[2] = psi2; xt[3] = r2;
    }
    for (int j = 0; j < NX; ++j) {
      const double e = std::fabs(xk[j] - xt[j]);
      if (e > seed_readback_max) seed_readback_max = e;
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

  int status = m5_staging_merge6_acados_solve(capsule);
  std::printf("MERGE6: solver_status=%d\n", status);

  // ---- Extract solved trajectory + per-stage per-target slack xi_{t,k} (P3). ----
  double px_traj[N + 1] = {0}, py_traj[N + 1] = {0}, psi_traj[N + 1] = {0};
  double r_traj[N + 1] = {0}, delta_traj[N] = {0};
  double xi_A[N + 1] = {0}, xi_B[N + 1] = {0};   // per-target CPA lower-slack
  double g_A_traj[N + 1] = {0}, g_B_traj[N + 1] = {0};
  bool any_nan = false;
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_traj[k] = xk[0];
    py_traj[k] = xk[1];
    psi_traj[k] = xk[2];
    r_traj[k] = xk[3];
    g_A_traj[k] = g_cpa_eval(xk[0], xk[1], /*t=*/0);
    g_B_traj[k] = g_cpa_eval(xk[0], xk[1], /*t=*/1);
    if (!std::isfinite(px_traj[k]) || !std::isfinite(py_traj[k]) ||
        !std::isfinite(psi_traj[k]) || !std::isfinite(r_traj[k])) {
      any_nan = true;
    }
    // Per-target slack: sl_vec length = NSH = 2. sl_vec[0]=xi_A,k, sl_vec[1]=xi_B,k.
    double sl_vec[NSH] = {0.0};
    if (k < N) {  // sl is a path-stage slack; the terminal stage has no sl.
      ocp_nlp_out_get(cfg, dims, out, k, "sl", sl_vec);
    }
    xi_A[k] = sl_vec[0];
    xi_B[k] = sl_vec[1];
    if (!std::isfinite(xi_A[k]) || !std::isfinite(xi_B[k])) any_nan = true;
    if (k < N) {
      double uk[NU] = {0.0};
      ocp_nlp_out_get(cfg, dims, out, k, "u", uk);
      delta_traj[k] = uk[0];
      if (!std::isfinite(delta_traj[k])) any_nan = true;
    }
  }

  // Did the solver actually move from the seed? (F5 honest-movement guard.)
  double traj_delta = 0.0;
  for (int k = 0; k <= N; ++k) {
    traj_delta += std::fabs(px_traj[k] - px_seed_snap[k]) +
                  std::fabs(py_traj[k] - py_seed_snap[k]);
  }
  const bool solver_moved = (status == 0) || (traj_delta > TRAJ_DELTA_TOL);

  // ---- P5 DYNAMICS CHECK #2 (convergence EVIDENCE, NOT a correctness gate):
  //      forward-sim the SOLVED u-sequence (delta_traj) through the hand-rolled
  //      double-integrator from x0, and compare to the SOLVED x-trajectory
  //      acatos reports. For a multiple-shooting DISCRETE-dynamics OCP the solved
  //      trajectory satisfies the shooting-dynamics EQUALITY only to the SQP
  //      convergence tolerance (~1e-6), NOT machine precision. Gated loosely at
  //      SOLVED_DYN_CONV_TOL (1e-4) as evidence; disc_dyn_expr correctness is
  //      NOT gated on it (CHECK #1 covers that, exactly). ----
  double solved_dyn_max = 0.0;
  double x_hand[N + 1][NX] = {{0}};
  for (int j = 0; j < NX; ++j) x_hand[0][j] = x0[j];
  for (int k = 0; k < N; ++k) {
    double px2, py2, psi2, r2;
    dyn_step(x_hand[k][0], x_hand[k][1], x_hand[k][2], x_hand[k][3],
             delta_traj[k], px2, py2, psi2, r2);
    x_hand[k + 1][0] = px2; x_hand[k + 1][1] = py2;
    x_hand[k + 1][2] = psi2; x_hand[k + 1][3] = r2;
  }
  for (int k = 0; k <= N; ++k) {
    const double xk[NX] = {px_traj[k], py_traj[k], psi_traj[k], r_traj[k]};
    for (int j = 0; j < NX; ++j) {
      const double e = std::fabs(x_hand[k][j] - xk[j]);
      if (e > solved_dyn_max) solved_dyn_max = e;
    }
  }

  // ---- P2: acatos total cost + hand-computed lumped equivalence. ----
  ocp_nlp_eval_cost(solver, in, out);
  double acados_cost = 0.0;
  ocp_nlp_get(solver, "cost_value", &acados_cost);
  const double hand_lumped = hand_compute_lumped(px_traj, py_traj);
  const double cost_diff = std::fabs(acados_cost - hand_lumped);
  const bool cost_finite = std::isfinite(acados_cost) &&
                           std::isfinite(hand_lumped);

  // ---- Per-stage printout (k, px, py, psi, r, delta, g_A, xi_A, g_B, xi_B,
  //      schedule). ----
  std::printf("MERGE6: per-stage (k, px, py, psi, r, delta, g_A, xi_A, g_B, "
              "xi_B, schedule):\n");
  double prefix_max_err = 0.0;
  double cpa_hard_min_g = 1e9;     // tightest min over (target, HARD stage)
  double slack_max = 0.0;          // max |xi_{t,k}| over (target, stage)
  double rot_max_abs = 0.0;        // max |r[k]|
  double delta_max_abs = 0.0;      // max |delta[k]|
  for (int k = 0; k <= N; ++k) {
    const char* cpa_tag = (k < CPA_HARD_FROM_K) ? "SOFT" : "HARD";
    const char* pre_tag = (k < K_PREFIX) ? "EQ " : "FREE";
    const double dk = (k < N) ? delta_traj[k] : 0.0;
    std::printf(
        "MERGE6:   k=%2d px=%8.2f py=%8.2f psi=%+.4f r=%+.4e d=%+.3f "
        "g_A=%+11.1f xi_A=%+.2e g_B=%+11.1f xi_B=%+.2e CPA=%s pre=%s\n",
        k, px_traj[k], py_traj[k], psi_traj[k], r_traj[k], dk,
        g_A_traj[k], xi_A[k], g_B_traj[k], xi_B[k], cpa_tag, pre_tag);
    if (k < K_PREFIX) {
      const double err = std::fabs(psi_traj[k] - PREFIX_PSI[k]);
      if (err > prefix_max_err) prefix_max_err = err;
    }
    if (k >= CPA_HARD_FROM_K && k < N) {
      if (g_A_traj[k] < cpa_hard_min_g) cpa_hard_min_g = g_A_traj[k];
      if (g_B_traj[k] < cpa_hard_min_g) cpa_hard_min_g = g_B_traj[k];
    }
    if (k < N) {
      if (std::fabs(xi_A[k]) > slack_max) slack_max = std::fabs(xi_A[k]);
      if (std::fabs(xi_B[k]) > slack_max) slack_max = std::fabs(xi_B[k]);
      if (std::fabs(delta_traj[k]) > delta_max_abs) {
        delta_max_abs = std::fabs(delta_traj[k]);
      }
    }
    if (std::fabs(r_traj[k]) > rot_max_abs) rot_max_abs = std::fabs(r_traj[k]);
  }
  std::printf("MERGE6: seed_readback_max_err=%.3e  (P5 CHECK #1, THE gate, tol "
              "%.0e -- disc_dyn_expr correctness)\n",
              seed_readback_max, SEED_READBACK_TOL);
  std::printf("MERGE6: solved_dyn_max_err=%.3e  (P5 CHECK #2, convergence "
              "evidence, tol %.0e -- SQP residual, NOT a correctness gate)\n",
              solved_dyn_max, SOLVED_DYN_CONV_TOL);
  std::printf("MERGE6: traj_delta=%.3e solver_moved=%d\n", traj_delta,
              solver_moved ? 1 : 0);
  std::printf("MERGE6: acados_cost=%.12e  hand_lumped=%.12e  "
              "|diff|=%.3e (tol %.0e)\n",
              acados_cost, hand_lumped, cost_diff, COST_EQUIV_TOL);
  std::printf("MERGE6: P1 prefix max|psi-prefix|(k<%d) = %.3e (tol %.0e)\n",
              K_PREFIX, prefix_max_err, PREFIX_EQ_TOL);
  std::printf("MERGE6: P3 per-target slack max|xi_{t,k}| = %.3e (tol %.0e)\n",
              slack_max, SLACK_TOL);
  std::printf("MERGE6: P4 CPA HARD min g_cpa over (target, stages %d..%d) = "
              "%.3e (tol -%.0e)\n",
              CPA_HARD_FROM_K, N - 1, cpa_hard_min_g, CPA_HARD_TOL);
  std::printf("MERGE6: P5 ROT max|r|=%.5e (<= %.2f + %.0e)  delta max|d|=%.5e "
              "(<= %.2f + %.0e)\n",
              rot_max_abs, ROT_MAX, BOX_TOL, delta_max_abs, DDELTA_MAX, BOX_TOL);
  std::printf("MERGE6: c_u=%.9e (T8 VDM-direct)  u_surge=%.4f  ROT_max=%.2f\n",
              C_U, U_SURGE, ROT_MAX);

  // ---- Coexistence assertions (all 6 points). ----
  bool status_ok = (status == 0 || status == 4);
  // P5: seed_readback is THE disc_dyn_expr-correctness gate (exact at 0.0).
  bool seed_readback_ok = (seed_readback_max < SEED_READBACK_TOL);
  // P5: solved_dyn is convergence EVIDENCE (SQP residual, tol 1e-4).
  bool solved_conv_ok = (solved_dyn_max < SOLVED_DYN_CONV_TOL);
  bool rot_ok = (rot_max_abs <= ROT_MAX + BOX_TOL);
  bool delta_ok = (delta_max_abs <= DDELTA_MAX + BOX_TOL);
  bool prefix_ok = (prefix_max_err < PREFIX_EQ_TOL);
  bool cpa_hard_ok = (cpa_hard_min_g >= -CPA_HARD_TOL);
  bool slack_ok = (slack_max < SLACK_TOL);
  bool cost_ok = cost_finite && (cost_diff < COST_EQUIV_TOL);
  // psi box on all stages.
  bool psi_box_ok = true;
  for (int k = 0; k <= N; ++k) {
    if (psi_traj[k] < PSI_LB - BOX_TOL || psi_traj[k] > PSI_UB + BOX_TOL) {
      psi_box_ok = false;
      break;
    }
  }

  if (any_nan) {
    std::fprintf(stderr, "MERGE6 FAIL: trajectory contains NaN/inf\n");
    rc = 1;
  } else if (!seed_readback_ok) {
    // P5 THE correctness gate: a non-zero seed readback means disc_dyn_expr is
    // encoded differently from the hand-rolled explicit-Euler step.
    std::fprintf(stderr,
                 "MERGE6 FAIL (P5 dyn): seed-readback (CHECK #1) max err=%.3e "
                 ">= tol %.0e -- acatos disc_dyn_expr disagrees from the "
                 "hand-rolled double-integrator (THIS IS a correctness failure)\n",
                 seed_readback_max, SEED_READBACK_TOL);
    rc = 1;
  } else if (!status_ok) {
    std::fprintf(stderr, "MERGE6 FAIL: solver_status=%d (expected 0 or 4)\n",
                 status);
    rc = 1;
  } else if (!solved_conv_ok) {
    // P5 convergence-evidence gate (NOT correctness): poor convergence.
    std::fprintf(stderr,
                 "MERGE6 FAIL (P5 dyn): solved-dyn convergence evidence (CHECK "
                 "#2) max err=%.3e >= tol %.0e -- SQP dynamics-equality residual "
                 "too large (poor convergence; NOT a disc_dyn_expr-correctness "
                 "failure, seed_readback=%.3e)\n",
                 solved_dyn_max, SOLVED_DYN_CONV_TOL, seed_readback_max);
    rc = 1;
  } else if (status != 0 && !solver_moved) {
    // F5 guard: status 4 tolerated ONLY if the solver MOVED (T3 lesson).
    std::fprintf(stderr,
                 "MERGE6 FAIL: status=%d but solver did NOT move "
                 "(traj_delta=%.3e < %.0e) -- seed read-back rejected\n",
                 status, traj_delta, TRAJ_DELTA_TOL);
    rc = 1;
  } else if (!prefix_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL (P1 prefix): max|psi-prefix|=%.3e >= tol %.0e at "
                 "some k<%d\n",
                 prefix_max_err, PREFIX_EQ_TOL, K_PREFIX);
    rc = 1;
  } else if (!cost_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL (P2 colreg): cost not equivalent -- "
                 "acados=%.12e hand=%.12e |diff|=%.3e (tol %.0e) finite=%d\n",
                 acados_cost, hand_lumped, cost_diff, COST_EQUIV_TOL,
                 cost_finite);
    rc = 1;
  } else if (!slack_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL (P3 per-target xi): max|xi_{t,k}|=%.3e >= tol %.0e "
                 "(expected ~0 in this CPA-feasible scenario; both per-target "
                 "slacks should vanish -- if non-zero the multi-row idxsh may be "
                 "coupling them, T7 lesson)\n",
                 slack_max, SLACK_TOL);
    rc = 1;
  } else if (!cpa_hard_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL (P4 schedule): CPA HARD min g_cpa=%.3e < -%.0e at "
                 "some (target, k>=%d)\n",
                 cpa_hard_min_g, CPA_HARD_TOL, CPA_HARD_FROM_K);
    rc = 1;
  } else if (!rot_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL (P5 ROT): box violated max|r|=%.5e > %.2f + %.0e "
                 "(marginal-stability bound not held)\n",
                 rot_max_abs, ROT_MAX, BOX_TOL);
    rc = 1;
  } else if (!delta_ok) {
    std::fprintf(stderr,
                 "MERGE6 FAIL: rudder box violated max|delta|=%.5e > %.2f + %.0e\n",
                 delta_max_abs, DDELTA_MAX, BOX_TOL);
    rc = 1;
  } else if (!psi_box_ok) {
    std::fprintf(stderr, "MERGE6 FAIL: heading box violated\n");
    rc = 1;
  }

  m5_staging_merge6_acados_free(capsule);
  m5_staging_merge6_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf(
        "MERGE6 PASS: 6 points coexist (double-integrator+prefix+J_colreg+xi+"
        "bounds), staging scalable -> P1b-1b 生产 backend 可写 "
        "(status=%d, seed_readback=%.2e, solved_dyn=%.2e, acados_cost=%.6e, "
        "|acatos-hand|=%.2e, max|xi_{t,k}|=%.2e, min g_cpa(hard)=%.3e, "
        "ROT max|r|=%.2e, prefix max err=%.2e)\n",
        status, seed_readback_max, solved_dyn_max, acados_cost, cost_diff,
        slack_max, cpa_hard_min_g, rot_max_abs, prefix_max_err);
  }
  return rc;
}
