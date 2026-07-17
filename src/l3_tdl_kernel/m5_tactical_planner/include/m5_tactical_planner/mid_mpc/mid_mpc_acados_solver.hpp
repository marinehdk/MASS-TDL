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

  // C1 status-4 constraint-satisfaction re-check (T17 review-fix C1). Recomputes
  // the constraint residuals h(x,u,p) per stage from the formulation's MX
  // con_h_expr (the SAME expression the acatos codegen derives from), evaluated
  // on the SOLVED trajectory, and verifies each row satisfies its lh/uh bound
  // within kBoxTol (CPA softened rows use the per-stage slack xi). Returns true
  // iff every active row is satisfied. Used to gate the status-4 -> Converged
  // re-map: a status-4 solve that MOVED but VIOLATED a constraint (e.g. CPA
  // hard floor breached) must NOT be reported Converged.
  // @param g    global params (np_global), as packed by pack_parameters.
  // @param ps   per-stage params [N+1][np_per_stage], as packed.
  // @param lateral_active  the active-row derivation for this cycle.
  // @param n_targets       the real-target count for this cycle.
  // @return true iff all active constraint rows are satisfied within kBoxTol.
  [[nodiscard]] bool constraints_satisfied_(const std::vector<double>& g,
                                            const std::vector<std::vector<double>>& ps,
                                            bool lateral_active,
                                            int n_targets);

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
