// P1b-1a Task 6 -- double-integrator heading dynamics runner (Path B).
//
// Links the generated solver lib for the double-integrator OCP
// (m5_staging_doubleint) and verifies the honest VDM-direct yaw channel
// (dr/dt = c(u)*delta, NO N_r*r damping -- T8 Path B finding) maps cleanly to
// acatos disc_dyn_expr and SOLVES despite the marginal-stability pole at z=1
// (r is a pure integrator of delta), with the ROT box |r|<=rot_max bounding r.
//
// ====================  the double-integrator OCP  ============================
//   state  x = [px, py, psi, r]          (nx = 4; r is the yaw rate)
//   ctrl   u = [delta]                   (nu = 1; rudder; surge constant)
//   disc_dyn_expr (acatos DISCRETE uses this directly, no RK):
//     r_new   = r   + DT*c_u*delta       (c_u = 9.825342e-3, VDM-direct)
//     psi_new = psi + DT*r               (explicit Euler -- r[k], NOT r[k+1])
//     px_new  = px  + u_surge*DT*cos(psi)
//     py_new  = py  + u_surge*DT*sin(psi)
//   model.p = []  (c_u, u_surge baked as literals in gen_doubleint.py)
//
//   Boxes : |delta|<=0.2 (idxbu 0); psi in [-1.2,1.2] (idxbx 2);
//           ROT |r|<=rot_max=0.2 (idxbx 3) -- marginal-stability bound.
//   CPA   : g_cpa = (px-tx)^2+(py-ty)^2-cpa_hard^2 >= 0 (one-sided, nh=1).
//   Cost  : NONLINEAR_LS y=[psi,delta], yref=[psi_ref,0], W=diag(1,1e-2).
//   Solver: SQP FULL_CONDENSING_HPIPM EXACT MERIT_BACKTRACKING max_iter=200.
//
// ====================  the CORE assertion: dynamics forward-match  ===========
// The gate for T6 is numerical correctness of the disc_dyn_expr mapping, NOT
// optimality. We validate this on the WARM-START SEED (before the optimizer
// moves anything):
//   1. Pick a fixed delta step sequence (non-trivial: +0.1 rad for the first
//      few stages, then 0). This drives r away from 0 (exercises the
//      integrator) AND keeps |r| well inside rot_max.
//   2. Hand-roll the SAME sequence through the double-integrator (same c_u,
//      u_surge, DT, explicit Euler) from x0 -- this is the ground truth.
//   3. Set the seed x/u into acatos via ocp_nlp_out_set.
//   4. Read back ocp_nlp_out_get(stage,"x") -- acatos re-propagates the seed
//      through disc_dyn_expr internally when make_consistent / the integrator
//      runs. Compare to ground truth.
//   Max abs diff over (px,py,psi,r) across all stages must be < 1e-9. (The
//   solve may then move the trajectory; the dynamics-match check is about
//   whether acatos applies disc_dyn_expr correctly.)
//
//   Implementation note: acatos does NOT auto-forward-propagate a seed you set
//   via ocp_nlp_out_set(stage,"x"/"u"). The seed IS what you set. So the
//   forward-match check is: the seed we SET (computed by our hand-rolled
//   forward sim) must match what we READ BACK (ocp_nlp_out_get) to <1e-9 --
//   this confirms the set/get round-trip is exact. THEN, to validate acatos's
//   OWN application of disc_dyn_expr, we additionally call the integrator
//   explicitly via ocp_nlp_eval_param_hash is unavailable; instead we rely on
//   the solve itself: SQP's first QP evaluates the dynamics Jacobian and the
//   constraint residuals on our seed -- if acatos's disc_dyn_expr disagreed
//   with our hand-rolled forward sim, the post-solve trajectory (which the
//   solver propagates via the SAME disc_dyn_expr) would carry the discrepancy.
//   We therefore ALSO forward-sim the SOLVED u-sequence through our hand-rolled
//   double-integrator and compare to the SOLVED x-trajectory acatos reports:
//   they must match to <1e-9. This is the load-bearing dynamics-match check
//   (it exercises acatos's disc_dyn_expr rollout over the solved controls).
//
// ====================  secondary assertions  =================================
// F5: acatos status 0 OR 4 tolerated, BUT only if the solver MOVED
// (traj_delta>1e-6 -- T3 honest-movement guard: reject a status-4 that returns
// the seed unchanged). Plus:
//   ROT box : |r[k]| <= rot_max + tol for all k.
//   Delta   : |delta[k]| <= delta_max + tol for all k.
//   CPA     : report min g_cpa; assert g_cpa >= -tol where evaluated.
//   no NaN/inf in trajectory.
//
// NOT production code. spike/external only.
#include <cmath>
#include <cstdio>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_doubleint.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int N = 10;
constexpr double DT = 5.0;

// ---- Constants (must match gen_doubleint.py EXACTLY) ----
// c_u = 9.825342e-3 rad/s^2/rad (T8 VDM-direct yaw gain, Path B).
constexpr double C_U = 9.825342e-3;
constexpr double U_SURGE = 9.26;        // T8 u_cruise, surge held constant
constexpr double ROT_MAX = 0.2;         // ROT box (marginal-stability bound)
constexpr double DDELTA_MAX = 0.2;      // rudder box
constexpr double PSI_LB = -1.2;
constexpr double PSI_UB = 1.2;
constexpr double TARGET_X = 0.0;     // off-path (north) -- T6 dynamics test, not avoidance
constexpr double TARGET_Y = 300.0;
constexpr double CPA_HARD = 100.0;
constexpr double UH_INF = 1.0e10;

// ---- Tolerances (do NOT widen to hide a violation). ----
constexpr double DYN_MATCH_TOL = 1.0e-9;   // THE gate: dynamics forward-match
constexpr double BOX_TOL = 1.0e-6;         // psi/r/delta box slack
constexpr double CPA_TOL = 1.0e-6;         // g_cpa >= -tol
constexpr double TRAJ_DELTA_TOL = 1.0e-6;  // F5 solver-moved guard
}  // namespace

// g_cpa = (px-tx)^2 + (py-ty)^2 - cpa_hard^2 (matches gen_doubleint.py).
static inline double g_cpa_eval(double px, double py) {
  const double dx = px - TARGET_X;
  const double dy = py - TARGET_Y;
  return dx * dx + dy * dy - CPA_HARD * CPA_HARD;
}

// One double-integrator discrete step (matches gen_doubleint.py disc_dyn_expr
// and common.py::forward_seed_doubleint EXACTLY -- explicit Euler, psi uses
// r[k] pre-update). Returns the next state via out-params.
static inline void dyn_step(double px, double py, double psi, double r,
                            double delta, double& px2, double& py2,
                            double& psi2, double& r2) {
  px2 = px + U_SURGE * DT * std::cos(psi);
  py2 = py + U_SURGE * DT * std::sin(psi);
  psi2 = psi + DT * r;            // r[k] pre-update
  r2 = r + DT * C_U * delta;
}

int main() {
  int rc = 0;

  m5_staging_doubleint_solver_capsule* capsule =
      m5_staging_doubleint_acados_create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "DOUBLEINT FAIL: create_capsule returned NULL\n");
    return 1;
  }
  if (m5_staging_doubleint_acados_create(capsule) != 0) {
    std::fprintf(stderr, "DOUBLEINT FAIL: acatos_create failed\n");
    m5_staging_doubleint_acados_free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = m5_staging_doubleint_acados_get_nlp_config(capsule);
  ocp_nlp_dims* dims = m5_staging_doubleint_acados_get_nlp_dims(capsule);
  ocp_nlp_out* out = m5_staging_doubleint_acados_get_nlp_out(capsule);
  ocp_nlp_in* in = m5_staging_doubleint_acados_get_nlp_in(capsule);
  (void)in;  // T6 does not call ocp_nlp_eval_cost (NONLINEAR_LS cost not
             // validated numerically -- only dynamics + feasibility).

  // x0: origin, heading north (psi=0), zero yaw rate (r=0).
  double x0[NX] = {0.0, 0.0, 0.0, 0.0};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- F1: warm-start seed. Non-trivial delta step sequence: +0.1 rad for
  //      the first 4 stages (drives r up via the integrator), then 0 (hold).
  //      With DT=5, c_u=9.8e-3, delta=0.1: dr per stage = 5*9.8e-3*0.1 =
  //      4.9e-3 rad/s; after 4 stages r ~= 1.96e-2, well inside rot_max=0.2.
  //      This exercises the r-integrator (r != 0 in the seed) without nearing
  //      the box. A zero seed yields an ill-conditioned first QP (P1a F1). ----
  double delta_seq[N] = {0};
  constexpr int K_STEP = 4;          // first K_STEP stages command +0.1 rad
  constexpr double DELTA_STEP = 0.1;
  for (int k = 0; k < N; ++k) {
    delta_seq[k] = (k < K_STEP) ? DELTA_STEP : 0.0;
  }

  // Hand-roll the forward sim of delta_seq from x0 (ground-truth seed traj).
  double x_seed_truth[N + 1][NX] = {{0}};
  for (int j = 0; j < NX; ++j) x_seed_truth[0][j] = x0[j];
  for (int k = 0; k < N; ++k) {
    dyn_step(x_seed_truth[k][0], x_seed_truth[k][1], x_seed_truth[k][2],
             x_seed_truth[k][3], delta_seq[k],
             x_seed_truth[k + 1][0], x_seed_truth[k + 1][1],
             x_seed_truth[k + 1][2], x_seed_truth[k + 1][3]);
  }

  // Set the seed into acatos (x for all stages, u for path stages).
  for (int k = 0; k <= N; ++k) {
    ocp_nlp_out_set(cfg, dims, out, k, "x", x_seed_truth[k]);
    if (k < N) {
      double uk[NU] = {delta_seq[k]};
      ocp_nlp_out_set(cfg, dims, out, k, "u", uk);
    }
  }

  // ---- DYNAMICS FORWARD-MATCH CHECK #1: seed round-trip. Read back the seed
  //      we just set; the set/get round-trip must be exact. (acatos does not
  //      auto-propagate a seed set via ocp_nlp_out_set -- the read-back IS the
  //      seed.) ----
  double seed_readback_max = 0.0;
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    for (int j = 0; j < NX; ++j) {
      const double e = std::fabs(xk[j] - x_seed_truth[k][j]);
      if (e > seed_readback_max) seed_readback_max = e;
    }
  }

  // Snapshot the seed trajectory BEFORE solve (px,py only -- for the F5
  // honest-movement guard).
  double px_seed_snap[N + 1] = {0}, py_seed_snap[N + 1] = {0};
  for (int k = 0; k <= N; ++k) {
    px_seed_snap[k] = x_seed_truth[k][0];
    py_seed_snap[k] = x_seed_truth[k][1];
  }

  int status = m5_staging_doubleint_acados_solve(capsule);
  std::printf("DOUBLEINT: solver_status=%d\n", status);

  // ---- Extract solved trajectory + controls. ----
  double px_traj[N + 1] = {0}, py_traj[N + 1] = {0};
  double psi_traj[N + 1] = {0}, r_traj[N + 1] = {0};
  double delta_traj[N] = {0};
  double g_cpa_traj[N + 1] = {0};
  bool any_nan = false;
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_traj[k] = xk[0]; py_traj[k] = xk[1];
    psi_traj[k] = xk[2]; r_traj[k] = xk[3];
    g_cpa_traj[k] = g_cpa_eval(xk[0], xk[1]);
    if (!std::isfinite(px_traj[k]) || !std::isfinite(py_traj[k]) ||
        !std::isfinite(psi_traj[k]) || !std::isfinite(r_traj[k])) {
      any_nan = true;
    }
    if (k < N) {
      double uk[NU] = {0.0};
      ocp_nlp_out_get(cfg, dims, out, k, "u", uk);
      delta_traj[k] = uk[0];
      if (!std::isfinite(delta_traj[k])) any_nan = true;
    }
  }

  // Did the solver move from the seed? (F5 honest-movement guard.)
  double traj_delta = 0.0;
  for (int k = 0; k <= N; ++k) {
    traj_delta += std::fabs(px_traj[k] - px_seed_snap[k]) +
                  std::fabs(py_traj[k] - py_seed_snap[k]);
  }
  const bool solver_moved = (status == 0) || (traj_delta > TRAJ_DELTA_TOL);

  // ---- DYNAMICS FORWARD-MATCH CHECK #2 (LOAD-BEARING): forward-sim the
  //      SOLVED u-sequence (delta_traj) through our hand-rolled double-
  //      integrator from x0, and compare to the SOLVED x-trajectory acatos
  //      reports. acatos propagates the solved controls through the SAME
  //      disc_dyn_expr; if our hand-rolled dyn_step disagrees with acatos's
  //      disc_dyn_expr, this diff blows up. Max abs diff over (px,py,psi,r)
  //      across all stages must be < 1e-9. ----
  double solved_dyn_max = 0.0;
  double x_hand[N + 1][NX] = {{0}};
  for (int j = 0; j < NX; ++j) x_hand[0][j] = x0[j];
  for (int k = 0; k < N; ++k) {
    dyn_step(x_hand[k][0], x_hand[k][1], x_hand[k][2], x_hand[k][3],
             delta_traj[k],
             x_hand[k + 1][0], x_hand[k + 1][1], x_hand[k + 1][2],
             x_hand[k + 1][3]);
  }
  for (int k = 0; k <= N; ++k) {
    const double xk[NX] = {px_traj[k], py_traj[k], psi_traj[k], r_traj[k]};
    for (int j = 0; j < NX; ++j) {
      const double e = std::fabs(x_hand[k][j] - xk[j]);
      if (e > solved_dyn_max) solved_dyn_max = e;
    }
  }
  // The load-bearing metric is the larger of (seed round-trip, solved rollout).
  const double dyn_match_max =
      (solved_dyn_max > seed_readback_max) ? solved_dyn_max : seed_readback_max;

  // ---- Box / CPA checks. ----
  double rot_max_abs = 0.0;          // max |r[k]| over all stages
  double delta_max_abs = 0.0;        // max |delta[k]| over path stages
  double g_cpa_min = 1e9;            // min g_cpa over all stages
  for (int k = 0; k <= N; ++k) {
    if (std::fabs(r_traj[k]) > rot_max_abs) rot_max_abs = std::fabs(r_traj[k]);
    if (k < N && std::fabs(delta_traj[k]) > delta_max_abs) {
      delta_max_abs = std::fabs(delta_traj[k]);
    }
    if (g_cpa_traj[k] < g_cpa_min) g_cpa_min = g_cpa_traj[k];
  }

  // ---- Per-stage printout. ----
  std::printf("DOUBLEINT: per-stage (k, px, py, psi, r, delta, g_cpa):\n");
  for (int k = 0; k <= N; ++k) {
    const double dk = (k < N) ? delta_traj[k] : 0.0;
    std::printf(
        "DOUBLEINT:   k=%2d px=%9.2f py=%9.2f psi=%+.5f r=%+.5e delta=%+.4f "
        "g_cpa=%+12.1f\n",
        k, px_traj[k], py_traj[k], psi_traj[k], r_traj[k], dk, g_cpa_traj[k]);
  }
  std::printf("DOUBLEINT: seed_readback_max_err=%.3e\n", seed_readback_max);
  std::printf("DOUBLEINT: solved_dyn_max_err=%.3e  (forward-sim solved u)\n",
              solved_dyn_max);
  std::printf("DOUBLEINT: DYN_MATCH_MAX=%.3e (tol %.0e)\n", dyn_match_max,
              DYN_MATCH_TOL);
  std::printf("DOUBLEINT: traj_delta=%.3e solver_moved=%d\n", traj_delta,
              solver_moved ? 1 : 0);
  std::printf("DOUBLEINT: ROT max|r|=%.5e (<= %.2f + %.0e)\n", rot_max_abs,
              ROT_MAX, BOX_TOL);
  std::printf("DOUBLEINT: delta max|delta|=%.5e (<= %.2f + %.0e)\n",
              delta_max_abs, DDELTA_MAX, BOX_TOL);
  std::printf("DOUBLEINT: min g_cpa=%.3e (>= -%.0e)\n", g_cpa_min, CPA_TOL);
  std::printf("DOUBLEINT: c_u=%.9e (T8 VDM-direct)  u_surge=%.4f\n", C_U,
              U_SURGE);

  // ---- Assertions. ----
  bool status_ok = (status == 0 || status == 4);
  bool dyn_ok = (dyn_match_max < DYN_MATCH_TOL);
  bool rot_ok = (rot_max_abs <= ROT_MAX + BOX_TOL);
  bool delta_ok = (delta_max_abs <= DDELTA_MAX + BOX_TOL);
  // CPA: g_cpa is a >= 0 path constraint enforced at all stages in this OCP.
  // The solver may soften slightly with the box; tolerate CPA_TOL.
  bool cpa_ok = (g_cpa_min >= -CPA_TOL);
  // psi box on all stages.
  bool psi_box_ok = true;
  for (int k = 0; k <= N; ++k) {
    if (psi_traj[k] < PSI_LB - BOX_TOL || psi_traj[k] > PSI_UB + BOX_TOL) {
      psi_box_ok = false;
      break;
    }
  }

  if (any_nan) {
    std::fprintf(stderr, "DOUBLEINT FAIL: trajectory contains NaN/inf\n");
    rc = 1;
  } else if (!dyn_ok) {
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: dynamics forward-match max err=%.3e >= tol "
                 "%.0e (seed_readback=%.3e solved_dyn=%.3e) -- acatos "
                 "disc_dyn_expr disagrees from hand-rolled double-integrator\n",
                 dyn_match_max, DYN_MATCH_TOL, seed_readback_max,
                 solved_dyn_max);
    rc = 1;
  } else if (!status_ok) {
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: solver_status=%d (expected 0 or 4)\n",
                 status);
    rc = 1;
  } else if (status != 0 && !solver_moved) {
    // F5 guard: status 4 tolerated ONLY if the solver MOVED (T3 lesson).
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: status=%d but solver did NOT move "
                 "(traj_delta=%.3e < %.0e) -- seed read-back rejected\n",
                 status, traj_delta, TRAJ_DELTA_TOL);
    rc = 1;
  } else if (!rot_ok) {
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: ROT box violated max|r|=%.5e > %.2f + %.0e "
                 "(marginal-stability bound not held)\n",
                 rot_max_abs, ROT_MAX, BOX_TOL);
    rc = 1;
  } else if (!delta_ok) {
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: rudder box violated max|delta|=%.5e > %.2f + "
                 "%.0e\n",
                 delta_max_abs, DDELTA_MAX, BOX_TOL);
    rc = 1;
  } else if (!cpa_ok) {
    std::fprintf(stderr,
                 "DOUBLEINT FAIL: CPA violated min g_cpa=%.3e < -%.0e\n",
                 g_cpa_min, CPA_TOL);
    rc = 1;
  } else if (!psi_box_ok) {
    std::fprintf(stderr, "DOUBLEINT FAIL: heading box violated\n");
    rc = 1;
  }

  m5_staging_doubleint_acados_free(capsule);
  m5_staging_doubleint_acados_free_capsule(capsule);

  if (rc == 0) {
    std::printf(
        "DOUBLEINT PASS: dynamics forward-match (max err %.3e), status=%d, "
        "ROT max|r|=%.3e, c_u=%.6e\n",
        dyn_match_max, status, rot_max_abs, C_U);
  }
  return rc;
}
