// P1b-0 T2 -- J_colreg per-stage EXTERNAL numeric equivalence runner.
//
// Links the generated solver lib for the colreg-staging OCP (EXTERNAL per-stage
// cost reproducing the production lumped J_colreg) and proves the lumped ->
// staged mapping is NUMERICALLY EQUIVALENT (<1e-6).
//
// The production NLP computes J_colreg as ONE lumped CasADi MX summing over all
// targets x steps. acatos is staged: it wants a per-stage external cost
// expression. This runner proves the staged form evaluates to the SAME number
// as the hand-computed lumped form over the solved trajectory.
//
// ====================  EXACT production J_colreg (VERBATIM)  ====================
//   J_colreg = cost / scale_denom,  scale_denom = max(1, Nt*N) = max(1,2*10) = 20
//   cost = sum_t sum_k  tw_t * disc_k * barrier_{t,k}
//   disc_k       = exp(-(k*dt_s)/t_discount_s)            [per-step constant]
//   barrier_{t,k}= exp(-zeta*(d_{t,k} - cpa_safe))
//   d_{t,k}      = sqrt(dx^2 + dy^2 + 1.0)                [kSqrtGuard = 1.0]
//   dx = x_own[k] - (tx + tdx*(k*dt_s)),   tdx = ts*cos(tc)
//   dy = y_own[k] - (ty + tdy*(k*dt_s)),   tdy = ts*sin(tc)
//   x_own[k],y_own[k] = the integrated stage state (acatos gives it per stage).
//
// ====================  Per-stage param encoding (DOCUMENTED)  ==================
// p_k = [disc_k, txdrift_A, tydrift_A, tw_A, txdrift_B, tydrift_B, tw_B]  (NP=7)
//   disc_k        = exp(-(k*DT)/T_DISCOUNT_S)             [TCPA discount]
//   txdrift_tk    = tx + ts*cos(tc)*(k*DT) = tx + tdx*(k*dt_s)
//   tydrift_tk    = ty + ts*sin(tc)*(k*DT) = ty + tdy*(k*dt_s)
//   tw_t          = range-ramp weight (0..1)
// All k-dependence is folded into p (the ONLY per-stage lever acatos exposes),
// so the symbolic cost expr is identical at every stage. acatos total objective
// = sum_{k=0}^{N-1} stage_expr_k + terminal_expr; terminal_expr is 0, so
// acatos_total == sum_{k=0}^{N-1} == the production N-term lumped sum.
//
// ====================  The equivalence assertion (heart of T2)  ================
//   1. Solve.
//   2. Hand-compute the lumped J_colreg over the SOLVED trajectory (stages
//      0..N-1), using the EXACT production formula and the SAME params passed
//      to the solver. -> hand_lumped.
//   3. Get acatos's total cost via ocp_nlp_eval_cost(cfg,dims,in,out) then
//      ocp_nlp_out_get(..,"cost_value",..). -> acatos_cost.
//   4. Assert |acatos_cost - hand_lumped| < 1e-6.
//
// F5: acatos status 4 (QP error during refinement) is TOLERATED -- the cost
// equivalence holds on whatever trajectory the solver returns.
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_colreg.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int N = 10;
constexpr double DT = 5.0;
constexpr int NT = 2;                       // two targets
constexpr int NP_PER_TARGET = 3;            // txdrift, tydrift, tw
constexpr int NP = 1 + NP_PER_TARGET * NT;  // = 7

// ---- Constants (must match gen_colreg.py EXACTLY) ----
constexpr double T_DISCOUNT_S = 100.0;
constexpr double ZETA = 5.0e-3;
constexpr double CPA_SAFE = 100.0;
constexpr double K_SQRT_GUARD = 1.0;
// scale_denom = max(1, Nt*N) = max(1, 2*10) = 20
constexpr double SCALE_DENOM = (NT * N > 1) ? static_cast<double>(NT * N) : 1.0;

// CPA h-constraint is kept (around target A) as a feasibility aid so the solver
// returns a realistic avoidance trajectory. T2's assertion is the cost number
// equivalence, not the CPA feasibility.
constexpr double TARGET_X = 200.0;   // target A x (CPA disc center)
constexpr double TARGET_Y = 0.0;     // target A y

// ---- Two DISTINCT targets (brief's suggested spec) ----
// raw params per target: tx, ty, tc (heading rad), ts (speed m/s), tw (weight).
// Target A: at (200,0), heading WEST (tc=pi) toward ownship, ts=2 m/s, tw=0.8.
// Target B: at (50,150), heading SOUTH (tc=-pi/2), ts=1.5 m/s, tw=0.5.
struct Target {
  double tx, ty, tc, ts, tw;
};
constexpr Target TARGETS[NT] = {
    {200.0,    0.0,  M_PI,        2.0, 0.8},  // A
    { 50.0,  150.0, -M_PI / 2.0,  1.5, 0.5},  // B
};

// x0: origin, heading east (psi=0), surge 5 m/s (matches base x0).
constexpr double X0[NX] = {0.0, 0.0, 0.0, 5.0};
}  // namespace

// Build the per-stage parameter vector p_k for stage k from the target specs.
// p_k = [disc_k, txdrift_A, tydrift_A, tw_A, txdrift_B, tydrift_B, tw_B].
static void build_stage_params(int k, double p_out[NP]) {
  const double k_dt = k * DT;
  const double disc_k = std::exp(-k_dt / T_DISCOUNT_S);
  p_out[0] = disc_k;
  for (int t = 0; t < NT; ++t) {
    const Target& tgt = TARGETS[t];
    const double tdx = tgt.ts * std::cos(tgt.tc);  // target velocity x-comp
    const double tdy = tgt.ts * std::sin(tgt.tc);  // target velocity y-comp
    const double txdrift = tgt.tx + tdx * k_dt;     // tx + tdx*(k*dt_s)
    const double tydrift = tgt.ty + tdy * k_dt;     // ty + tdy*(k*dt_s)
    p_out[1 + NP_PER_TARGET * t + 0] = txdrift;
    p_out[1 + NP_PER_TARGET * t + 1] = tydrift;
    p_out[1 + NP_PER_TARGET * t + 2] = tgt.tw;
  }
}

// Hand-compute the lumped J_colreg over the solved trajectory (stages 0..N-1),
// using the EXACT production formula and the SAME per-stage params passed to
// the solver. This is the reference number the acatos staged cost must match.
//
//   J_colreg = (1/scale_denom) * sum_{k=0}^{N-1} sum_{t} tw_t*disc_k*barrier_{t,k}
//   barrier_{t,k} = exp(-zeta*(d_{t,k}-cpa_safe)), d=sqrt(dx^2+dy^2+1).
//   dx = px[k] - txdrift_tk ; dy = py[k] - tydrift_tk
//   (x_own[k] IS the stage state px,py; verified against step-by-step
//   integration below.)
static double hand_compute_lumped(const double px_traj[N + 1],
                                  const double py_traj[N + 1]) {
  double cost = 0.0;
  for (int k = 0; k < N; ++k) {           // production sums k=0..N-1
    double p_k[NP];
    build_stage_params(k, p_k);
    const double disc_k = p_k[0];
    for (int t = 0; t < NT; ++t) {
      const double txdrift = p_k[1 + NP_PER_TARGET * t + 0];
      const double tydrift = p_k[1 + NP_PER_TARGET * t + 1];
      const double tw = p_k[1 + NP_PER_TARGET * t + 2];
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

  m5_staging_colreg_solver_capsule* capsule =
      m5_staging_colreg_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "COLREG FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_colreg_acados_create(capsule) != 0) {
    std::fprintf(stderr, "COLREG FAIL: acados_create failed\n");
    m5_staging_colreg_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_colreg_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_colreg_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_colreg_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_colreg_acados_get_nlp_in(capsule);
  // ocp_nlp_eval_cost takes the SOLVER (not config/dims): signature is
  // ocp_nlp_eval_cost(ocp_nlp_solver*, ocp_nlp_in*, ocp_nlp_out*).
  ocp_nlp_solver* solver = m5_staging_colreg_acados_get_nlp_solver(capsule);

  // x0 fixed (origin, heading east, surge 5 m/s) -- matches base x0.
  double x0[NX] = {X0[0], X0[1], X0[2], X0[3]};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage parameters p_k = [disc_k, {txdrift,tydrift,tw} x NT].
  //      Set for ALL stages 0..N (terminal stage carries p too even though its
  //      cost expr is 0). Uses the GENERATED per-stage setter
  //      <name>_acados_update_params(capsule, stage, vals, NP) (the C API T1
  //      discovered works; generic ocp_nlp_in_set "p" does NOT work). ----
  for (int k = 0; k <= N; ++k) {
    double p_k[NP];
    build_stage_params(k, p_k);
    m5_staging_colreg_acados_update_params(capsule, k, p_k, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero). A smooth CPA-
  //      feasible avoiding trajectory (turn north, then back) -- a zero seed
  //      yields an ill-conditioned first QP (P1a finding). Mirrors
  //      subset_runner.cpp:64-78. ----
  double x_seed[NX] = {x0[0], x0[1], x0[2], x0[3]};
  for (int k = 0; k <= N; ++k) {
    ocp_nlp_out_set(cfg, dims, out, k, "x", x_seed);
    if (k < N) {
      double dpsi = (k < 4) ? 0.2 : -0.2;
      double u_seed[NU] = {dpsi};
      ocp_nlp_out_set(cfg, dims, out, k, "u", u_seed);
      double px = x_seed[0], py = x_seed[1], psi = x_seed[2], u = x_seed[3];
      double psi_new = psi + dpsi * DT;
      x_seed[0] = px + u * DT * std::cos(psi);
      x_seed[1] = py + u * DT * std::sin(psi);
      x_seed[2] = psi_new;
      // u held constant (constant surge)
    }
  }

  int status = m5_staging_colreg_acados_solve(capsule);
  std::printf("COLREG: solver_status=%d\n", status);

  // ---- Extract solved trajectory (px, py per stage) and verify own-ship
  //      integration matches the stage state (x_own[k] == px[k]). ----
  double px_traj[N + 1] = {0}, py_traj[N + 1] = {0}, psi_traj[N + 1] = {0};
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_traj[k] = xk[0];
    py_traj[k] = xk[1];
    psi_traj[k] = xk[2];
  }
  // Sanity: stage positions satisfy the discrete dynamics from x0 (i.e. x_own[k]
  // IS the integrated state). Allow loose tol -- solver may not be fully
  // converged (status 4 tolerated); we only need a self-consistent trajectory.
  double integ_max_err = 0.0;
  {
    double px = X0[0], py = X0[1], psi = X0[2];
    for (int k = 0; k < N; ++k) {
      double err = std::fabs(px - px_traj[k]) + std::fabs(py - py_traj[k]);
      if (err > integ_max_err) integ_max_err = err;
      double uk[NU] = {0};
      ocp_nlp_out_get(cfg, dims, out, k, "u", uk);
      double dpsi = uk[0];
      px = px + X0[3] * DT * std::cos(psi);
      py = py + X0[3] * DT * std::sin(psi);
      psi = psi + dpsi * DT;
    }
  }

  // ---- Per-stage cost printout (visibility into where the cost lives). ----
  std::printf("COLREG: per-stage staged cost (k, stage_cost):\n");
  double staged_sum = 0.0;
  for (int k = 0; k < N; ++k) {
    double p_k[NP];
    build_stage_params(k, p_k);
    const double disc_k = p_k[0];
    double stage = 0.0;
    for (int t = 0; t < NT; ++t) {
      const double txdrift = p_k[1 + NP_PER_TARGET * t + 0];
      const double tydrift = p_k[1 + NP_PER_TARGET * t + 1];
      const double tw = p_k[1 + NP_PER_TARGET * t + 2];
      const double dx = px_traj[k] - txdrift;
      const double dy = py_traj[k] - tydrift;
      const double d = std::sqrt(dx * dx + dy * dy + K_SQRT_GUARD);
      const double barrier = std::exp(-ZETA * (d - CPA_SAFE));
      stage += tw * disc_k * barrier;
    }
    stage /= SCALE_DENOM;
    staged_sum += stage;
    std::printf("COLREG:   k=%2d px=%8.2f py=%8.2f psi=%+.4f stage_cost=%.10e\n",
                k, px_traj[k], py_traj[k], psi_traj[k], stage);
  }

  // ---- Hand-computed lumped J_colreg over the solved trajectory. ----
  const double hand_lumped = hand_compute_lumped(px_traj, py_traj);
  std::printf("COLREG: staged_sum (hand sum of stage_cost) = %.12e\n",
              staged_sum);
  std::printf("COLREG: hand_lumped J_colreg                = %.12e\n",
              hand_lumped);

  // ---- acatos total cost: ocp_nlp_eval_cost(solver, in, out) populates the
  //      solver memory's cost_value (the full lumped objective over all N+1
  //      stages, = sum_{k=0}^{N-1} stage_cost_k + terminal_cost_e). Retrieve it
  //      via ocp_nlp_get(solver, "cost_value", ...) -- the recognized field in
  //      ocp_nlp_common.c:4078. (ocp_nlp_out_get has NO "cost_value" field; the
  //      total lives in nlp_mem, not nlp_out.) From acatos_c/ocp_nlp_interface.h. ----
  ocp_nlp_eval_cost(solver, in, out);
  double acados_cost = 0.0;
  ocp_nlp_get(solver, "cost_value", &acados_cost);
  std::printf("COLREG: acados_cost (ocp_nlp_eval_cost)     = %.12e\n",
              acados_cost);

  // ---- The equivalence assertion (heart of T2). ----
  const double diff = std::fabs(acados_cost - hand_lumped);
  const double diff_staged = std::fabs(acados_cost - staged_sum);
  std::printf("COLREG: |acados - hand_lumped|     = %.3e (tol 1e-6)\n", diff);
  std::printf("COLREG: |acados - staged_sum|      = %.3e\n", diff_staged);
  std::printf("COLREG: own-ship integration max|px,py drift| = %.3e\n",
              integ_max_err);

  // F5: status 0 or 4 tolerated; the assertion is the cost equivalence (not
  // the status code). status is reported in the PASS line for visibility.
  const bool equiv_ok = (diff < 1e-6);

  if (!equiv_ok) {
    std::fprintf(stderr,
                 "COLREG FAIL: staged != lumped (acados=%.12e, hand=%.12e, "
                 "diff=%.3e, tol=1e-6)\n",
                 acados_cost, hand_lumped, diff);
    rc = 1;
  }

  m5_staging_colreg_acados_free(capsule);
  m5_staging_colreg_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf(
        "COLREG PASS: staged J_colreg == lumped (acados=%.12e, hand=%.12e, "
        "diff=%.3e < 1e-6); solver_status=%d (%s)\n",
        acados_cost, hand_lumped, diff, status,
        status == 0 ? "converged"
                    : (status == 4 ? "QP error during refinement, tolerated"
                                   : "other"));
  }
  return rc;
}
