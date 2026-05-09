#ifndef MASS_L3_M5_MID_MPC_SOLVER_HPP_
#define MASS_L3_M5_MID_MPC_SOLVER_HPP_

// M5 Tactical Planner — Mid-MPC Solver (Task 2.2)
// Wraps the CasADi nlpsol Function from MidMpcNlpFormulation for per-cycle use.
//
// Call pattern (1 Hz):
//   MidMpcSolver solver(formulation, opts);
//   MidMpcSolution sol = solver.solve(input, &last_sol);  // warm start
//
// Thread-safety: NOT reentrant. Callers must serialise concurrent solve() calls.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no
// bare new/delete.

#include <casadi/casadi.hpp>
#include <cstdint>
#include <string>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"

namespace mass_l3::m5::mid_mpc {

class MidMpcSolver {
 public:
  // -------------------------------------------------------------------------
  // IpoptOptions — recorded at construction; baked into nlpsol by MidMpcNlpFormulation.
  // Stored for traceability / Phase E2 re-instantiation support.
  // All [TBD-HAZID] tunables; see detailed design §5.2.4.
  // -------------------------------------------------------------------------
  struct IpoptOptions {
    int32_t max_iter{150};                               // [TBD-HAZID]
    double tol{1.0e-4};                                  // [TBD-HAZID]
    double timeout_s{0.5};                               // [TBD-HAZID] 500 ms SLA cap
    std::string linear_solver{"mumps"};                  // open-source default; ma27/ma57 optional
    std::string hessian_approximation{"limited-memory"}; // L-BFGS (FCB state dim ~30)
    int32_t print_level{0};                              // suppress IPOPT stdout in production
  };

  // SolveStatus mirrors MidMpcSolution::Status for test-readable assertions
  // (e.g. EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)).
  using SolveStatus = MidMpcSolution::Status;

  // Construct solver wrapping an already-built MidMpcNlpFormulation.
  // @pre formulation.build_symbolic_graph() has been called.
  MidMpcSolver(const MidMpcNlpFormulation& formulation, const IpoptOptions& opts);

  // Solve one Mid-MPC cycle.
  // @param input  Time-aligned runtime snapshot for this cycle.
  // @param warm_start  Previous-cycle solution (nullptr → cold start).
  // @return  MidMpcSolution including status, trajectory, solve_duration_ms.
  [[nodiscard]] MidMpcSolution solve(const MidMpcInput& input,
                                     const MidMpcSolution* warm_start);

  // Count of consecutive non-Converged cycles since last success.
  [[nodiscard]] int64_t consecutive_failures() const noexcept {
    return consecutive_failures_;
  }

 private:
  const MidMpcNlpFormulation& formulation_;
  IpoptOptions opts_;
  casadi::Dict ipopt_dict_;          // reserved for Phase E2 call-level overrides
  int64_t consecutive_failures_{0};

  // Pack previous-cycle trajectory into x0 ∈ R^{2N}: [psi; u].
  [[nodiscard]] casadi::DM pack_warm_start_(const MidMpcSolution& warm) const;

  // Build x0 from constant heading + constant speed (ROT = 0 extrapolation).
  [[nodiscard]] casadi::DM pack_cold_start_(const MidMpcInput& input) const;

  // Delegate to formulation_.g_dim() for internal use.
  [[nodiscard]] int32_t g_dim_() const noexcept;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_SOLVER_HPP_
