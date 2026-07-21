// P1b-1a T7 -- per-target per-step xi high-dim slack runner.
//
// Links the TWO generated solver libs for the xi-staging OCPs
// (m5_staging_xi_feas: both targets CPA-feasible; m5_staging_xi_infeas: target A
// feasible, target B unavoidably infeasible) and proves the DIMENSION UPGRADE
// from P1b-0's single-scalar sigma (idxsh=[0], one slack per stage) to per-target
// per-step xi (idxsh=[0,1], ns=2 per stage, one slack per CPA row) preserves the
// per-target INDEPENDENT exact-penalty semantics.
//
// ====================  the per-target xi OCP (per scenario)  ====================
//   con_h_expr = vertcat(g_cpa_A, g_cpa_B)                               (nh = 2)
//     g_cpa_t = (px - txdrift_t)^2 + (py - tydrift_t)^2 - cpa_hard^2
//   Stage-UNIFORM bounds: lh = [0,0], uh = [UH_INF, UH_INF] (one-sided >= 0).
//   idxsh = [0,1]  -> soften BOTH rows; ns=2 lower-slacks per stage.
//     sl[0] = xi_A,k (relaxes row 0, target A's CPA)
//     sl[1] = xi_B,k (relaxes row 1, target B's CPA)
//   cost_type = NONLINEAR_LS (psi tracking + heading-rate; T3 style).
//
// ====================  acatos slack read-back API (used here)  =================
// The per-stage slack vector `sl` has length ns = len(idxsh) = 2 (one per softened
// CPA row). Read it back via ocp_nlp_out_get(cfg, dims, out, stage, "sl", sl_vec):
//   sl_vec[0] = xi_A,k   (the target-A CPA lower-side relaxation at stage k)
//   sl_vec[1] = xi_B,k   (the target-B CPA lower-side relaxation at stage k)
// The CPA h value for target t at stage k is
//   g_cpa_t = (px - txdrift_t)^2 + (py - tydrift_t)^2 - cpa_hard^2.
// Hard constraint: g_cpa_t >= 0. Softened: g_cpa_t + xi_{t,k} >= 0 (xi relaxes
// the lower bound of that row only -- INDEPENDENT per row). When g_cpa_t >= 0
// already holds, exact-penalty drives xi_{t,k} -> 0 (no relaxation needed). When
// g_cpa_t < 0 (infeasible), xi_{t,k} > 0 and g_cpa_t + xi_{t,k} >= 0 holds.
//
// ====================  Per-stage params (T2 update_params pattern)  =============
// model.p = [txdrift_A, tydrift_A, txdrift_B, tydrift_B]  (NP=4).
// Set per stage via the GENERATED <name>_acados_update_params(capsule, stage,
// vals, NP) (T2 pattern; staging-finding 3: NOT ocp_nlp_in_set "p"). The target
// positions are static here (the per-stage mechanism is exercised -- production
// has moving targets); the per-stage setter is called for all stages 0..N.
//
// ====================  Per-target INDEPENDENCE assertions (heart of T7)  =======
// Scenario 1 (BOTH feasible): both targets far. Assert max over (target, stage)
//   |xi_{t,k}| < tol (both ~0 -- exact-penalty: nothing to relax).
// Scenario 2 (A feasible, B infeasible -- FUTURE-VIOLATION): target A far, target
//   B unavoidably entered around k>=3. Assert:
//   (i)  xi_A,k ~ 0 at EVERY stage (target A INDEPENDENT exact-penalty -- A's
//        slack stays 0 regardless of B's violation; NO coupling through the
//        multi-row idxsh).
//   (ii) xi_B,k > tol at some stage (target B relaxed by its violation).
//   (iii)g_cpa_B + xi_B,k >= -tol at that stage (B's row relaxed by exactly its
//        violation).
// CRITICAL (T3 lesson / staging-finding 4): xi activates on FUTURE-VIOLATION, NOT
// start-inside-disc. Scenario 2's target B is feasible at the START (dist 102 >
// 22) but unavoidably enters the CPA disc at a FUTURE stage (the tight box
// prevents southward clearance around k>=3). Do NOT start the seed inside B's
// disc -- HPIPM cannot iterate from an infeasible seed.
//
// F5: acatos status 0 OR 4 tolerated, BUT only if the solver MOVED
// (traj_delta>1e-6 -- T3 lesson: reject a status-4 that returns the seed
// unchanged). The per-target xi assertions are what matter.
//
// NOT production code. staging/external only.
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "acados_c/ocp_nlp_interface.h"
#include "acados_solver_m5_staging_xi_feas.h"
#include "acados_solver_m5_staging_xi_infeas.h"

namespace {
constexpr int NX = 4;
constexpr int NU = 1;
constexpr int N = 10;
constexpr double DT = 5.0;
constexpr int NT = 2;    // two CPA targets (two rows in con_h_expr)
constexpr int NP = 2 * NT;  // = 4: [txdrift_A, tydrift_A, txdrift_B, tydrift_B]
constexpr int NSH = 2;   // two softened rows (idxsh=[0,1]); ns per stage = 2
constexpr double UH_INF = 1.0e10;  // F2 bounded pseudo-infinity (matches gen)

// x0: origin, heading east (psi=0), surge 5 m/s (matches base x0).
constexpr double X0[NX] = {0.0, 0.0, 0.0, 5.0};

// Per-scenario baked-in params (must match gen_xi.py SCENARIOS exactly). cpa_hard
// and the heading box are BAKED into the generated solver; the targets' positions
// are PARAMETRIC (set per stage at runtime). The box is carried here ONLY to
// shape a BOX-RESPECTING warm-start seed (the seed psi must stay within
// [psi_lb, psi_ub] at every stage, else the first QP starts box-infeasible).
struct ScenarioSpec {
  const char* tag;
  double cpa_hard;       // baked into con_h_expr (CPA clearance radius)
  double psi_ref;        // cost reference (for reporting)
  double psi_lb;         // heading box lower bound (baked into solver)
  double psi_ub;         // heading box upper bound (baked into solver)
  double tx_A, ty_A;     // target A position (static; set per stage)
  double tx_B, ty_B;     // target B position (static; set per stage)
  double seed_dpsi;      // stage-0 seed heading-rate (box-respecting mild turn)
};

// Scenario 1 (BOTH FEASIBLE): cpa_hard=100, wide box +-1.2. Both targets far
// (A at (500,0), B at (500,500)). Seed: hard north turn dpsi=+0.2 at stage 0
// -> psi=1.0 (within +-1.2), hold.
constexpr ScenarioSpec FEAS{
    "both_feasible", 100.0, 0.3, -1.2, 1.2,
    500.0, 0.0, 500.0, 500.0, 0.2};
// Scenario 2 (A feasible, B infeasible -- FUTURE-VIOLATION): cpa_hard=22, TIGHT
// box psi in [-0.02, 0.02]. Target A (500,0) far (dist >= 250 > 22 -> feasible
// throughout -> xi_A ~ 0 INDEPENDENT of B). Target B (100,20) close: vessel
// starts OUTSIDE B's disc (stage-0 dist 102 > 22) but unavoidably enters it
// around stage 4 (best south avoidance py~-1.5 -> dist 21.5 < 22 -> g_cpa_B~-22).
// xi_B > 0 relaxes B's row; xi_A stays ~0 -- per-target exact-penalty
// independence. Seed: straight east dpsi=0 (the least-violating trajectory).
constexpr ScenarioSpec INFEAS{
    "A_feasible_B_infeasible", 22.0, 0.0, -0.02, 0.02,
    500.0, 0.0, 100.0, 20.0, 0.0};

// Exact-penalty tolerances (do NOT widen to hide a non-zero feasible-slack).
constexpr double FEAS_MAX_SLACK_TOL = 1.0e-4;   // scenario 1: max|xi_{t,k}| < tol
constexpr double INDEP_XI_A_TOL = 1.0e-4;       // scenario 2: xi_A,k < tol (every k)
constexpr double INFEAS_MIN_SLACK_TOL = 1.0e-3; // scenario 2: xi_B,k > tol (some k)
constexpr double RELAX_RESIDUAL_TOL = 1.0e-6;   // g_cpa_B + xi_B >= -tol
constexpr double TRAJ_DELTA_TOL = 1.0e-6;       // F5 solver-moved guard
}  // namespace

// Evaluate g_cpa for target t at a state (for verifying the slack relaxes the
// constraint). Matches gen_xi.py con_h_expr row t. Target index t selects A (0)
// or B (1).
static inline double g_cpa_eval(double px, double py, const ScenarioSpec& s,
                                int t) {
  const double tx = (t == 0) ? s.tx_A : s.tx_B;
  const double ty = (t == 0) ? s.ty_A : s.ty_B;
  const double dx = px - tx;
  const double dy = py - ty;
  return dx * dx + dy * dy - s.cpa_hard * s.cpa_hard;
}

// Build the per-stage parameter vector p_k for stage k. p_k = [txdrift_A,
// tydrift_A, txdrift_B, tydrift_B] (NP=4). Targets are static here; the per-stage
// mechanism is exercised (production has moving targets).
static void build_stage_params(const ScenarioSpec& s, int k, double p_out[NP]) {
  (void)k;  // static targets (no per-stage drift); param mechanism still used.
  p_out[0] = s.tx_A;
  p_out[1] = s.ty_A;
  p_out[2] = s.tx_B;
  p_out[3] = s.ty_B;
}

// ---- Run one scenario end-to-end. templated on the generated solver API. ----
// Each generated solver exposes its own capsule/create/solve/free symbols, so we
// template the runner on the solver's header-defined C-API as function-pointer
// bundles (acatos does not emit a uniform C++ wrapper; each <name>_solver.h has
// its own prefixed functions).
struct FeasApi {
  using capsule_t = m5_staging_xi_feas_solver_capsule;
  static capsule_t* create_capsule() {
    return m5_staging_xi_feas_acados_create_capsule();
  }
  static int create(capsule_t* c) { return m5_staging_xi_feas_acados_create(c); }
  static void free(capsule_t* c) { m5_staging_xi_feas_acados_free(c); }
  static void free_capsule(capsule_t* c) {
    m5_staging_xi_feas_acados_free_capsule(c);
  }
  static int solve(capsule_t* c) { return m5_staging_xi_feas_acados_solve(c); }
  static ocp_nlp_config* get_cfg(capsule_t* c) {
    return m5_staging_xi_feas_acados_get_nlp_config(c);
  }
  static ocp_nlp_dims* get_dims(capsule_t* c) {
    return m5_staging_xi_feas_acados_get_nlp_dims(c);
  }
  static ocp_nlp_out* get_out(capsule_t* c) {
    return m5_staging_xi_feas_acados_get_nlp_out(c);
  }
  static ocp_nlp_in* get_in(capsule_t* c) {
    return m5_staging_xi_feas_acados_get_nlp_in(c);
  }
  static int update_params(capsule_t* c, int stage, double* p, int np) {
    return m5_staging_xi_feas_acados_update_params(c, stage, p, np);
  }
};

struct InfeasApi {
  using capsule_t = m5_staging_xi_infeas_solver_capsule;
  static capsule_t* create_capsule() {
    return m5_staging_xi_infeas_acados_create_capsule();
  }
  static int create(capsule_t* c) { return m5_staging_xi_infeas_acados_create(c); }
  static void free(capsule_t* c) { m5_staging_xi_infeas_acados_free(c); }
  static void free_capsule(capsule_t* c) {
    m5_staging_xi_infeas_acados_free_capsule(c);
  }
  static int solve(capsule_t* c) { return m5_staging_xi_infeas_acados_solve(c); }
  static ocp_nlp_config* get_cfg(capsule_t* c) {
    return m5_staging_xi_infeas_acados_get_nlp_config(c);
  }
  static ocp_nlp_dims* get_dims(capsule_t* c) {
    return m5_staging_xi_infeas_acados_get_nlp_dims(c);
  }
  static ocp_nlp_out* get_out(capsule_t* c) {
    return m5_staging_xi_infeas_acados_get_nlp_out(c);
  }
  static ocp_nlp_in* get_in(capsule_t* c) {
    return m5_staging_xi_infeas_acados_get_nlp_in(c);
  }
  static int update_params(capsule_t* c, int stage, double* p, int np) {
    return m5_staging_xi_infeas_acados_update_params(c, stage, p, np);
  }
};

// Generic per-scenario driver. Templated on the solver API bundle. Returns 0 on
// PASS, non-zero on FAIL. expect_feasible selects the scenario-1 (both-feasible)
// vs scenario-2 (A-feasible-B-infeasible) assertion cascade.
template <typename Api>
int run_scenario(const ScenarioSpec& s, bool expect_feasible, int* status_out) {
  using capsule_t = typename Api::capsule_t;
  capsule_t* capsule = Api::create_capsule();
  if (capsule == nullptr) {
    std::fprintf(stderr, "XI FAIL [%s]: create_capsule NULL\n", s.tag);
    return 1;
  }
  if (Api::create(capsule) != 0) {
    std::fprintf(stderr, "XI FAIL [%s]: acados_create failed\n", s.tag);
    Api::free_capsule(capsule);
    return 1;
  }

  ocp_nlp_config* cfg = Api::get_cfg(capsule);
  ocp_nlp_dims* dims = Api::get_dims(capsule);
  ocp_nlp_out* out = Api::get_out(capsule);
  ocp_nlp_in* in = Api::get_in(capsule);

  // x0 fixed (origin, heading east, surge 5 m/s).
  double x0[NX] = {X0[0], X0[1], X0[2], X0[3]};
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "lbx", x0);
  ocp_nlp_constraints_model_set(cfg, dims, in, 0, "ubx", x0);

  // ---- Per-stage params (T2 update_params pattern). Static targets here; the
  //      per-stage mechanism is exercised for all stages 0..N. ----
  for (int k = 0; k <= N; ++k) {
    double p_k[NP];
    build_stage_params(s, k, p_k);
    Api::update_params(capsule, k, p_k, NP);
  }

  // ---- F1: warm-start seed (forward-propagated, non-zero, BOX-RESPECTING).
  //      Per-scenario seed_dpsi at stage 0 (a mild north turn that steers AWAY
  //      from an eastward target), then HOLD. The seed psi must stay within the
  //      scenario's heading box [psi_lb, psi_ub] at every stage. For scenario 1
  //      (wide box +-1.2) seed_dpsi=+0.2 -> psi=1.0; for scenario 2 (tight box
  //      +-0.02) seed_dpsi=0.0 -> psi=0 (straight east, the least-violating
  //      trajectory). Non-zero (F1: a zero seed yields an ill-conditioned first
  //      QP). Mirrors subset_runner.cpp:64-78.
  //
  //      NOTE: the slack "sl" is deliberately NOT warm-started (T3 lesson).
  //      Seeding sl to the CPA violation makes the OUTPUT show a non-zero slack
  //      even when the solver returns the seed unchanged (status 4, traj_delta=
  //      0) -- a FALSE pass. We leave sl at 0 and rely on the solver_moved check.
  double x_seed[NX] = {x0[0], x0[1], x0[2], x0[3]};
  for (int k = 0; k <= N; ++k) {
    ocp_nlp_out_set(cfg, dims, out, k, "x", x_seed);
    if (k < N) {
      const double dpsi = (k == 0) ? s.seed_dpsi : 0.0;
      double u_seed[NU] = {dpsi};
      ocp_nlp_out_set(cfg, dims, out, k, "u", u_seed);
      double px = x_seed[0], py = x_seed[1], psi = x_seed[2], u = x_seed[3];
      double psi_new = psi + dpsi * DT;
      x_seed[0] = px + u * DT * std::cos(psi);
      x_seed[1] = py + u * DT * std::sin(psi);
      x_seed[2] = psi_new;
      // u held constant.
    }
  }

  // Snapshot the seed trajectory BEFORE solve so we can detect a non-converged
  // solve that returns the seed unchanged (status 4 on the first QP). Honest-
  // movement guard (T3/F5 lesson): if the solver did not move, any non-zero
  // slack is read-back of warm-start, NOT a solver-confirmed relaxation.
  double px_seed_snap[N + 1] = {0}, py_seed_snap[N + 1] = {0};
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_seed_snap[k] = xk[0];
    py_seed_snap[k] = xk[1];
  }

  const int status = Api::solve(capsule);
  *status_out = status;
  std::printf("XI [%s]: solver_status=%d\n", s.tag, status);

  // ---- Extract solved trajectory + per-stage per-target slack xi_{t,k}. ----
  // sl_vec length = NSH = 2: sl_vec[0]=xi_A,k, sl_vec[1]=xi_B,k.
  double px_traj[N + 1] = {0}, py_traj[N + 1] = {0};
  double xi_A[N + 1] = {0}, xi_B[N + 1] = {0};
  double g_A[N + 1] = {0}, g_B[N + 1] = {0};
  for (int k = 0; k <= N; ++k) {
    double xk[NX] = {0, 0, 0, 0};
    ocp_nlp_out_get(cfg, dims, out, k, "x", xk);
    px_traj[k] = xk[0];
    py_traj[k] = xk[1];
    g_A[k] = g_cpa_eval(xk[0], xk[1], s, /*t=*/0);
    g_B[k] = g_cpa_eval(xk[0], xk[1], s, /*t=*/1);
    double sl_vec[NSH] = {0.0};
    if (k < N) {  // sl is a path-stage slack; the terminal stage has no sl.
      ocp_nlp_out_get(cfg, dims, out, k, "sl", sl_vec);
    }
    xi_A[k] = sl_vec[0];
    xi_B[k] = sl_vec[1];
  }

  // Did the solver actually move from the seed? (F5 honest-movement guard.)
  double traj_delta = 0.0;
  for (int k = 0; k <= N; ++k) {
    traj_delta += std::fabs(px_traj[k] - px_seed_snap[k]) +
                  std::fabs(py_traj[k] - py_seed_snap[k]);
  }
  const bool solver_moved = (status == 0) || (traj_delta > TRAJ_DELTA_TOL);

  // ---- Per-stage printout (k, px, py, g_A, xi_A, g_B, xi_B, relaxed rows). ----
  std::printf("XI [%s]: per-stage (k, px, py, g_A, xi_A, g_A+xi_A, g_B, xi_B, "
              "g_B+xi_B):\n", s.tag);
  double max_xi_A = 0.0, max_xi_B = 0.0;
  for (int k = 0; k <= N; ++k) {
    const double relax_A = g_A[k] + xi_A[k];
    const double relax_B = g_B[k] + xi_B[k];
    std::printf(
        "XI [%s]:   k=%2d px=%9.2f py=%9.2f | g_A=%+11.1f xi_A=%+.4e "
        "g_A+xi_A=%+11.1f | g_B=%+11.1f xi_B=%+.4e g_B+xi_B=%+11.1f\n",
        s.tag, k, px_traj[k], py_traj[k], g_A[k], xi_A[k], relax_A,
        g_B[k], xi_B[k], relax_B);
    if (std::fabs(xi_A[k]) > max_xi_A) max_xi_A = std::fabs(xi_A[k]);
    if (std::fabs(xi_B[k]) > max_xi_B) max_xi_B = std::fabs(xi_B[k]);
  }
  std::printf("XI [%s]: max|xi_A|=%.6e max|xi_B|=%.6e traj_delta=%.3e "
              "solver_moved=%d\n", s.tag, max_xi_A, max_xi_B, traj_delta,
              solver_moved ? 1 : 0);

  // ---- Per-target independence assertion cascade. ----
  int rc = 0;
  if (!solver_moved) {
    std::fprintf(stderr,
                 "XI FAIL [%s]: solver did NOT move (status=%d, "
                 "traj_delta=%.3e) -- the first QP failed before the slack "
                 "engaged; relaxation UNCONFIRMED (any read-back slack is "
                 "warm-start, not a solver result)\n", s.tag, status,
                 traj_delta);
    rc = 1;
  } else if (expect_feasible) {
    // Scenario 1: BOTH targets CPA-feasible -> max over (target, stage) of
    // |xi_{t,k}| < tol (both ~0 -- exact-penalty: nothing to relax).
    const double max_slack = (max_xi_A > max_xi_B) ? max_xi_A : max_xi_B;
    if (max_slack < FEAS_MAX_SLACK_TOL) {
      std::printf(
          "XI PASS [%s] scenario-1 (both feasible): max|xi_{t,k}|=%.3e "
          "(< tol=%.0e) -- BOTH per-target slacks ~0 (exact-penalty)\n",
          s.tag, max_slack, FEAS_MAX_SLACK_TOL);
    } else {
      std::fprintf(stderr,
                   "XI FAIL [%s] scenario-1 (both feasible): max|xi_{t,k}|=%.3e "
                   ">= tol=%.0e -- slack non-zero when both CPA rows are "
                   "feasible (NOT exact-penalty; try raising rho/zl)\n",
                   s.tag, max_slack, FEAS_MAX_SLACK_TOL);
      rc = 1;
    }
  } else {
    // Scenario 2: A feasible, B infeasible (future-violation). Three assertions.
    // (i)  xi_A,k ~ 0 at EVERY stage (target A INDEPENDENT exact-penalty -- A's
    //      slack stays 0 regardless of B's violation).
    // (ii) xi_B,k > tol at some stage (target B relaxed by its violation).
    // (iii)g_cpa_B + xi_B,k >= -tol at that stage (B's row relaxed by its slack).
    const bool xi_A_indep = (max_xi_A < INDEP_XI_A_TOL);
    // Find the stage with max xi_B; verify it relaxes the constraint there.
    int k_max_B = 0;
    double xi_B_max = 0.0;
    for (int k = 0; k <= N; ++k) {
      if (std::fabs(xi_B[k]) > xi_B_max) {
        xi_B_max = std::fabs(xi_B[k]);
        k_max_B = k;
      }
    }
    const double relax_B_at_kmax = g_B[k_max_B] + xi_B[k_max_B];
    const bool xi_B_engaged = (xi_B_max > INFEAS_MIN_SLACK_TOL);
    const bool relax_ok = (relax_B_at_kmax >= -RELAX_RESIDUAL_TOL);

    if (!xi_A_indep) {
      std::fprintf(stderr,
                   "XI FAIL [%s] scenario-2 (A feasible, B infeasible): xi_A "
                   "NOT independent -- max|xi_A,k|=%.3e >= tol=%.0e at some "
                   "stage while B is relaxed (xi_B,max=%.3e). This indicates "
                   "the multi-row idxsh slacks are COUPLED (acatos may not "
                   "isolate per-row exact-penalty). Check idxsh=[0,1], Zl/zl "
                   "length=2, and rho magnitude (exact-penalty needs "
                   "rho>||lambda*||inf; two targets have different lambda*, "
                   "rho takes max).\n", s.tag, max_xi_A, INDEP_XI_A_TOL,
                   xi_B_max);
      rc = 1;
    } else if (!xi_B_engaged) {
      std::fprintf(stderr,
                   "XI FAIL [%s] scenario-2 (A feasible, B infeasible): xi_B "
                   "did NOT engage -- max|xi_B,k|=%.3e <= tol=%.0e (expected > "
                   "tol at B's violating stage). B's future-violation may not "
                   "have materialized (check the box tightness / target B "
                   "position).\n", s.tag, xi_B_max, INFEAS_MIN_SLACK_TOL);
      rc = 1;
    } else if (!relax_ok) {
      std::fprintf(stderr,
                   "XI FAIL [%s] scenario-2 (A feasible, B infeasible): B's "
                   "slack did NOT relax the constraint -- g_cpa_B+xi_B=%+.4e < "
                   "-%.0e at k=%d (xi_B=%+.4e, g_B=%+11.1f)\n", s.tag,
                   relax_B_at_kmax, RELAX_RESIDUAL_TOL, k_max_B, xi_B[k_max_B],
                   g_B[k_max_B]);
      rc = 1;
    } else {
      std::printf(
          "XI PASS [%s] scenario-2 (A feasible, B infeasible): xi_A "
          "INDEPENDENT (max|xi_A,k|=%.3e < tol=%.0e); xi_B engaged "
          "(max|xi_B,k|=%.3e at k=%d, g_cpa_B+xi_B=%+.4e >= -%.0e) -- "
          "per-target exact-penalty independence holds\n",
          s.tag, max_xi_A, INDEP_XI_A_TOL, xi_B_max, k_max_B,
          relax_B_at_kmax, RELAX_RESIDUAL_TOL);
    }
  }

  Api::free(capsule);
  Api::free_capsule(capsule);
  return rc;
}

int main() {
  int rc1 = 0, rc2 = 0;
  int status_feas = -1, status_infeas = -1;

  std::printf("=== XI scenario 1 (BOTH CPA feasible) -- "
              "m5_staging_xi_feas ===\n");
  rc1 = run_scenario<FeasApi>(FEAS, /*expect_feasible=*/true, &status_feas);

  std::printf("\n=== XI scenario 2 (A feasible, B infeasible) -- "
              "m5_staging_xi_infeas ===\n");
  rc2 = run_scenario<InfeasApi>(INFEAS, /*expect_feasible=*/false,
                                &status_infeas);

  std::printf("\n=== XI summary ===\n");
  std::printf("XI scenario-1 (both feasible):           status=%d -> %s\n",
              status_feas, rc1 == 0 ? "PASS" : "FAIL");
  std::printf("XI scenario-2 (A feasible, B infeasible): status=%d -> %s\n",
              status_infeas, rc2 == 0 ? "PASS" : "FAIL");

  // ---- Final verdict: dimension upgrade validated iff BOTH scenarios pass. ----
  if (rc1 == 0 && rc2 == 0) {
    std::printf(
        "XI PASS: per-target exact-penalty (A feasible xi~0, B infeasible "
        "xi>0) -- dimension upgrade single-scalar sigma (idxsh=[0]) -> "
        "per-target xi (idxsh=[0,1]) validated\n");
    return 0;
  }
  // If per-target independence failed (scenario-2 xi_A NOT ~0 while B relaxed),
  // the verdict is BLOCKED -- do NOT fudge by widening tol; report the slack
  // values and the rho tried (brief Step 4).
  std::printf(
      "XI FAIL: per-target exact-penalty NOT confirmed on both scenarios "
      "(see FAIL lines above) -- BLOCKED, see report\n");
  return 1;
}
