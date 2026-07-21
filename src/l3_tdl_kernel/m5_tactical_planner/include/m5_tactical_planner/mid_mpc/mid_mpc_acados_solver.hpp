#ifndef MASS_L3_M5_MID_MPC_ACADOS_SOLVER_HPP_
#define MASS_L3_M5_MID_MPC_ACADOS_SOLVER_HPP_

// M5 Tactical Planner — Mid-MPC production acados solver wrapper (Task 17,
// P1b-1b core). Wraps the generated acados OCP solver lib
// (libacados_ocp_solver_m5_mid_mpc_acados.so, produced by T15 codegen + T16
// CMake) behind the SAME MidMpcSolution output contract as the IPOPT
// MidMpcSolver (downstream M4/L4/tail_gate is agnostic to the backend switch).
//
// The wrapper owns one solver capsule + nlp handles. The CONSTRUCTOR runs a
// cold-capsule warm-up solve (warm_up_capsule_) to prime the SQP/HPIPM state
// (acatos v0.4.4 first-solve cold-start effect — see the ctor comment in
// mid_mpc_acados_solver.cpp). Per cycle it:
//   1. pack_parameters(input) -> {global[106], per-stage[N+1][35]}
//   2. write the concatenated per-stage param vector (global++per-stage = 141)
//      to every acados stage via the GENERATED m5_mid_mpc_acados_acados_update_params
//      (P1b-1a finding 3: NOT generic ocp_nlp_in_set "p").
//   3. pin the initial state x0=[px,py,psi,r,u_surge] via lbx0/ubx0 equality.
//   4. seed x/u via ocp_nlp_out_set using a F1 forward-propagated NON-ZERO seed
//      (zero seed -> ill-conditioned first QP).
//   5. m5_mid_mpc_acados_acados_solve(capsule).
//   6. reconstruct MidMpcSolution: psi/u/x/y from acatos state trajectory,
//      cost_total from ocp_nlp_get("cost_value") (real value, IPOPT E1 is 0 --
//      improvement, not a contract break), cpa_slack = max per-target xi from
//      "sl", solve_duration_ms wall-clock, ipopt_iterations = SQP iter count
//      (the field name stays ipopt_iterations even for acatos -- downstream
//      reads it; do NOT rename).
//
// F1-F5 (locked P1b-1a + T15 findings):
//   F1 seed forward-propagated NON-ZERO (double-integrator Path B dynamics).
//   F2 uh=1e10 (bounded pseudo-infinity; t_renderer rejects JSON Infinity).
//   F3 EXACT hessian (formulation).
//   F4 MERIT_BACKTRACKING globalization (formulation).
//   F5 status 4 (QP error during refinement) tolerated ONLY when constraints
//      are satisfied AND solver-moved (traj_delta > tol). status != 0 && != 4
//      -> NumericalFailure.
//
// acados C lib 2-Clause BSD; internal MISRA violations exempted per coding-
// standards.md §10 (dynamic-link boundary).
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no bare
// new/delete.
#include <memory>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"

namespace mass_l3::m5::mid_mpc {

// L1b reachability schedule — per-cycle compute from vessel dynamics + M4
// contract + target TCPA geometry. Mirrors IPOPT derive_row_bound_config.
// Stages before k_minalt/k_head_earliest have those constraint rows softened;
// k_head_latest is the CPA-deadline bound; k_cpa_suffix gates CPA floor hardening.
// See compute_reachability_schedule() in the .cpp for the derivation logic.
struct ReachabilitySchedule {
  int k_minalt{0};
  int k_head_earliest{0};
  int k_head_latest{0};        // 0 = active from stage 0 (conservative default)
  int k_cpa_suffix{0};         // 0 = active from stage 0 (conservative default)
  bool minalt_box_infeasible{false};
  bool direction_wrong_side{false};
};

// ── L4-T3 status contract functions (VR-05, 2026-07-21) ───────────────────
// These are public so the L4 contract tests can verify the status mapping
// without instantiating a full acados solver. The solve() method calls these
// internally; they are NOT part of the solver class itself (pure functions).

// Map the acados integer solver status (raw 0..7) to MidMpcSolution::SolverStatus.
// This is the PRIMARY mapping — the backward-compatible Status is derived from
// SolverStatus via solver_status_to_status().
// Contract (fail-closed):
//   raw 0 → Converged        (all KKT conditions met)
//   raw 1 → Timeout          (max iterations)
//   raw 2 → Infeasible       (QP infeasible)
//   raw 3 → NumericalFailure (QP solver failed)
//   raw 4 → QpRecovered      (QP error during refinement — caller must verify
//                              solver_moved + primal feasibility before use)
//   other→ NumericalFailure  (unexpected)
// VR-05: raw 4 NEVER maps to Converged. This is the contract the L4 tests assert.
inline MidMpcSolution::SolverStatus map_acados_status_to_solver_status(int raw_status) {
  switch (raw_status) {
    case 0:  return MidMpcSolution::SolverStatus::Converged;
    case 1:  return MidMpcSolution::SolverStatus::Timeout;
    case 2:  return MidMpcSolution::SolverStatus::Infeasible;
    case 3:  return MidMpcSolution::SolverStatus::NumericalFailure;
    // VR-05: raw 4 is QpRecovered — NOT Converged. This is the contract the
    // L4 tests assert. The caller (solve()) may further refine this to
    // NumericalFailure if solver_moved is false or constraints are violated.
    case 4:  return MidMpcSolution::SolverStatus::QpRecovered;
    default: return MidMpcSolution::SolverStatus::NumericalFailure;
  }
}

// Map SolverStatus to the backward-compatible MidMpcSolution::Status.
// This preserves the existing downstream contract (L4/L5/M7 read `status`) while
// allowing new consumers to read `solver_status` directly.
//   Converged        → Converged
//   QpRecovered      → NumericalFailure  (NOT Converged — VR-05)
//   Timeout          → Timeout
//   Infeasible       → Infeasible
//   NumericalFailure → NumericalFailure
//   NotInitialized   → NotInitialized
inline MidMpcSolution::Status solver_status_to_status(MidMpcSolution::SolverStatus ss) {
  switch (ss) {
    case MidMpcSolution::SolverStatus::Converged:
      return MidMpcSolution::Status::Converged;
    case MidMpcSolution::SolverStatus::QpRecovered:
      // VR-05: QpRecovered NEVER maps to Converged. The backward-compatible
      // status is NumericalFailure — downstream consumers that have NOT been
      // updated to read solver_status will see a non-converged status, which
      // triggers the existing fallback/degraded paths. This is fail-closed.
      return MidMpcSolution::Status::NumericalFailure;
    case MidMpcSolution::SolverStatus::Timeout:
      return MidMpcSolution::Status::Timeout;
    case MidMpcSolution::SolverStatus::Infeasible:
      return MidMpcSolution::Status::Infeasible;
    case MidMpcSolution::SolverStatus::NumericalFailure:
      return MidMpcSolution::Status::NumericalFailure;
    case MidMpcSolution::SolverStatus::NotInitialized:
    default:
      return MidMpcSolution::Status::NotInitialized;
  }
}

// MidMpcAcadosSolver — production acatos backend for Mid-MPC (Path B 5-dim).
//
// Construct once per MidMpcSolver lifetime (the capsule is expensive to build).
// solve() is NOT reentrant — callers must serialise concurrent calls. The
// output MidMpcSolution shape is byte-identical to the IPOPT MidMpcSolver
// output (same Status enum, same trajectory[k] field semantics) so the
// dispatch branch in MidMpcSolver::solve() is the ONLY place that picks the
// backend.
class MidMpcAcadosSolver {
 public:
  // SolveStatus mirrors MidMpcSolution::Status for test-readable assertions.
  using SolveStatus = MidMpcSolution::Status;

  // Construct the acatos backend wrapping an already-built formulation.
  // @pre formulation.build_symbolic_graph() has been called (the MX graph
  //      dimensions are read for parity asserts; the actual solver graph is
  //      the codegen SX in c_generated_code/).
  // @throws std::runtime_error if acatos_create fails (capsule alloc or
  //         create returned non-zero) — the caller must treat a failed backend
  //         as a build/lifecycle error and fall back to IPOPT.
  explicit MidMpcAcadosSolver(const MidMpcAcadosFormulation& formulation);

  // TEST-ONLY constructor option (T17 review-fix Step 1 diagnostic). Allows a
  // test friend to construct the solver WITHOUT the cold-capsule warm-up so the
  // cold first-solve can be observed in isolation. This is NOT a production
  // knob: only MidMpcAcadosSolverColdCapsuleTest (friend) can set
  // skip_warm_up=true; the production ctor path always warms up. The struct is
  // public so the friend can name it, but the second ctor is PRIVATE — only the
  // friend reaches it.
  struct CtorOpts {
    bool skip_warm_up{false};
  };

  // Release the capsule + nlp handles (acatos_free + free_capsule).
  ~MidMpcAcadosSolver();

  MidMpcAcadosSolver(const MidMpcAcadosSolver&) = delete;
  MidMpcAcadosSolver& operator=(const MidMpcAcadosSolver&) = delete;
  MidMpcAcadosSolver(MidMpcAcadosSolver&&) = delete;
  MidMpcAcadosSolver& operator=(MidMpcAcadosSolver&&) = delete;

  // Solve one Mid-MPC cycle through the acatos backend.
  // @param input       Time-aligned runtime snapshot for this cycle.
  // @param warm_start  Previous-cycle solution (may be nullptr → cold start).
  //                    NOTE: the acatos warm-start seed (F1) is forward-
  //                    propagated from own_ship, NOT copied from the previous
  //                    trajectory (same discipline as the IPOPT C1 suffix
  //                    cold-start; warm_start is accepted for signature parity
  //                    and to keep the MidMpcSolver::solve dispatch shape
  //                    identical, but the seed is NOT derived from it).
  // @return  MidMpcSolution including status, trajectory (N points),
  //          cost_total (real value), cpa_slack (max per-target xi),
  //          solve_duration_ms, ipopt_iterations (SQP iter count).
  [[nodiscard]] MidMpcSolution solve(const MidMpcInput& input,
                                     const MidMpcSolution* warm_start);

  // TEST-ONLY diagnostic accessors (T17 review-fix Step 1 cold-capsule matrix).
  // Mirror the last solve()'s raw acatos status int / SQP iter count / trajectory
  // delta. Production code MUST NOT read these (they exist solely so the cold-
  // capsule diagnostic can record the matrix {skip_warm_up x route_weight} x
  // {first, second solve}). Defined in the .cpp (Impl is incomplete here).
  [[nodiscard]] int last_raw_status() const noexcept;
  [[nodiscard]] int last_sqp_iter() const noexcept;
  [[nodiscard]] double last_traj_delta() const noexcept;

  // TEST-ONLY C1 verification hook (T17 review-fix C1): re-run the constraint-
  // satisfaction re-check on the LAST solved trajectory (impl_->out), using the
  // packed params + active-row derivation for `input`. The `debug_` prefix and
  // this comment mark it test-only (NO production caller; the MidMpcSolver
  // dispatch path never calls it). It exercises the status-4 constraint
  // recompute path on a CONVERGED solve; MUST return true on a genuinely-
  // converged solve. Kept public (like the last_* accessors) so the test TU
  // can reach it without fragile cross-namespace friendship; the risk of
  // production misuse is bounded by it being a pure read with no side effects.
  [[nodiscard]] bool debug_constraints_satisfied_after_solve(const MidMpcInput& input);

  // ── 7-layer regression-baseline diagnostic interfaces (2026-07-21) ─────────
  // TEST-ONLY. Two narrow hooks for the L1/L2 contract tests + the G+H scan
  // (spec docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md
  // §3/§4/§5). The production solve() path NEVER calls these; they exist
  // solely so the diagnostic test binaries can:
  //   (a) cap SQP max_iter (H plan: shrink 400→100 to keep the 8-point scan
  //       under the 120s budget; cold warm-up already pays the cost). The
  //       override writes to nlp_opts via ocp_nlp_solver_opts_set; subsequent
  //       solve() calls read the new value. Must be called AFTER the ctor
  //       warm-up (the ctor calls warm_up_capsule_ at the codegen default).
  //   (b) read per-stage box bounds (lbx/ubx) AFTER a solve() so L2-T1 can
  //       assert the heading-delayed-to-k_head_earliest schedule. Pure read
  //       via ocp_nlp_get; returns lbx/ubx for the psi slot (index 2 of nx=5)
  //       at the requested stage. Production never inspects stage bounds.
  //
  // Both are marked `debug_` to make grep audits trivial and are commented
  // TEST-ONLY. Behavior contracts (no production caller) are enforced by code
  // review, not by access modifiers (mirrors last_* / debug_constraints_*).
  void debug_set_max_iter_diagnostic(int max_iter);
  // Returns {psi_lb, psi_ub, rot_lb, rot_ub, spd_lb, spd_ub} at stage k.
  // stage must be in [0, kAcadosN]; returns NaN-filled on out-of-range.
  struct StageBounds {
    double psi_lb{0.0}; double psi_ub{0.0};
    double rot_lb{0.0}; double rot_ub{0.0};
    double spd_lb{0.0}; double spd_ub{0.0};
  };
  [[nodiscard]] StageBounds debug_get_stage_bounds(int stage) const noexcept;

 private:
  // TEST-ONLY friend (T17 review-fix Step 1): sole non-member allowed to invoke
  // the private CtorOpts ctor with skip_warm_up=true. Production code has no
  // way to reach that ctor. The friend is a small factory in the test TU
  // (test/unit/test_mid_mpc_acados_solver.cpp); the gtest fixture itself does
  // not need friendship (it calls the factory).
  friend class MidMpcAcadosSolverColdCapsuleFactory;

  // pimpl hides the acatos C handles from the header (keeps the include
  // surface clean for OFF builds where the acatos headers are absent).
  struct Impl;
  std::unique_ptr<Impl> impl_;
  const MidMpcAcadosFormulation& formulation_;

  // Cold-capsule warm-up outcome (S1 safety gate): true iff the ctor warm-up
  // either converged OR was intentionally skipped (test-only). The MidMpcSolver
  // dispatch reads warm_up_succeeded() and, if false, refuses to dispatch to
  // this (known-degraded) backend and falls back to IPOPT instead.
  bool warm_up_succeeded_{false};

  // TEST-ONLY private ctor (T17 review-fix Step 1 diagnostic). Only reachable
  // via the friend class. When opts.skip_warm_up=true the cold-capsule warm-up
  // is NOT run, so the first real solve() observes the raw cold capsule.
  explicit MidMpcAcadosSolver(const MidMpcAcadosFormulation& formulation,
                              const CtorOpts& opts);

  // Cold-capsule warm-up (P1b-1b Task 17 lifecycle fix): run one throwaway
  // benign-scenario solve in the ctor to prime the capsule's internal SQP/
  // HPIPM state. See the ctor comment in mid_mpc_acados_solver.cpp for the
  // full rationale (acatos v0.4.4 first-solve cold-start effect).
  void warm_up_capsule_();

  // L4-T3 (VR-05): status-4 constraint-satisfaction re-check (T17 review-fix C1).
  // Recomputes the constraint residuals h(x,u,p) per stage from the formulation's
  // MX con_h_expr (the SAME expression the acatos codegen derives from), evaluated
  // on the SOLVED trajectory, and verifies each row satisfies its lh/uh bound
  // within kBoxTol (CPA softened rows use the per-stage slack xi). Returns true
  // iff every active row is satisfied.
  //
  // VR-05: raw 4 (QP error during refinement) is NEVER re-mapped to Converged.
  // This function is still called for diagnostic logging + soft-aspiration
  // telemetry, and to determine whether solver_status should be QpRecovered
  // (primal feasible) vs NumericalFailure (hard violation). The backward-
  // compatible status always stays NumericalFailure for raw 4.
  // @param g    global params (np_global), as packed by pack_parameters.
  // @param ps   per-stage params [N+1][np_per_stage], as packed.
  // @param lateral_active  the active-row derivation for this cycle.
  // @param n_targets       the real-target count for this cycle.
  // @return true iff all active constraint rows are satisfied within kBoxTol.
  [[nodiscard]] bool constraints_satisfied_(const std::vector<double>& g,
                                            const std::vector<std::vector<double>>& ps,
                                            bool lateral_active,
                                            int n_targets,
                                            int prefix_K,
                                            const ReachabilitySchedule& sched);

  // L4-T2: compute soft-aspiration d_min + violation_m telemetry from solved
  // trajectory geometry. Walks px_traj/py_traj against target (x,y) positions and
  // stores the minimum distance into impl_->last_soft_aspiration_d_min_m and
  // _violation_m. Uses cpa_safe from global params for the violation degree.
  // Works purely from trajectory arrays + target positions + global params — no
  // CasADi dependency. Callable at ALL solve() exit paths, including failure
  // statuses where the h_fn cache is invalid or constraints_satisfied_ is skipped.
  // The trajectory arrays must have size kAcadosN+1; target arrays must have size
  // >= n_targets (only the first n_targets entries are used).
  // @param px_traj   solved x positions [N+1], read from acados "x" output
  // @param py_traj   solved y positions [N+1], read from acados "x" output
  // @param target_x  per-target x positions (undrifted, from input.targets or ps[0])
  // @param target_y  per-target y positions (undrifted, from input.targets or ps[0])
  // @param n_targets number of real targets (0..kAcadosMaxTargets)
  // @param g         global params vector (for cpa_safe slot index)
  void compute_soft_aspiration_telemetry_(
      const std::vector<double>& px_traj,
      const std::vector<double>& py_traj,
      const std::vector<double>& target_x,
      const std::vector<double>& target_y,
      int n_targets,
      const std::vector<double>& g);

 public:
  // S1 safety gate accessor: the MidMpcSolver dispatch reads this to decide
  // whether to dispatch to acados (true) or fall back to IPOPT (false). False
  // means the ctor warm-up did NOT converge and the capsule is suspect — using
  // it would risk spuriously reporting Infeasible on the first real cycle.
  [[nodiscard]] bool warm_up_succeeded() const noexcept {
    return warm_up_succeeded_;
  }
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_ACADOS_SOLVER_HPP_
