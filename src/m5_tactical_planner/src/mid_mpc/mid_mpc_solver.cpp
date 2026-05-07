// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>

#include <casadi/casadi.hpp>
#include <spdlog/spdlog.h>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// Consecutive failures before escalating to CRITICAL (detailed design §7.1).
constexpr int64_t kConsecutiveFailureEscalation = 5;

// ===========================================================================
// Constructor — store formulation reference and opts (nlpsol already built).
// ===========================================================================
MidMpcSolver::MidMpcSolver(const MidMpcNlpFormulation& formulation,
                             const IpoptOptions& opts)
    : formulation_(formulation), opts_(opts) {}

// ===========================================================================
// g_dim_() — delegate to formulation.
// ===========================================================================
int32_t MidMpcSolver::g_dim_() const noexcept {
  return formulation_.g_dim();
}

// ===========================================================================
// pack_warm_start_() — extract previous-cycle trajectory into x0 = [psi; u].
// If warm trajectory is shorter than N (degenerate), zero-pads remainder.
// ===========================================================================
casadi::DM MidMpcSolver::pack_warm_start_(const MidMpcSolution& warm) const {
  const int32_t N = formulation_.config().n_horizon;
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  const int32_t n_warm = static_cast<int32_t>(warm.trajectory.size());
  for (int32_t k = 0; k < N; ++k) {
    if (k < n_warm) {
      const auto& pt = warm.trajectory[static_cast<std::size_t>(k)];
      x0(k)     = pt.psi_rad;
      x0(N + k) = pt.u_mps;
    }
  }
  return x0;
}

// ===========================================================================
// pack_cold_start_() — constant-heading + constant-speed (ROT = 0).
// Neutral initial guess; IPOPT finds the optimum from the feasibility interior.
// ===========================================================================
casadi::DM MidMpcSolver::pack_cold_start_(const MidMpcInput& input) const {
  const int32_t N = formulation_.config().n_horizon;
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x0(k)     = input.own_ship.psi_rad;  // hold current heading (ROT = 0)
    x0(N + k) = input.own_ship.u_mps;   // hold current speed
  }
  return x0;
}

// ===========================================================================
// solve() — pack params, call IPOPT, record timing, update failure counter.
//
// Constraint convention (g >= 0): lbg = zeros, ubg = +inf.
// Spec snippet §5.4 has lbg/ubg inverted; implementation follows the
// MidMpcNlpFormulation header comment ("g >= 0: lower bound = 0, upper bound = +inf").
// ===========================================================================
MidMpcSolution MidMpcSolver::solve(const MidMpcInput& input,
                                    const MidMpcSolution* warm_start) {
  const auto t_start = std::chrono::steady_clock::now();

  const casadi::DM p_val = formulation_.pack_parameters(input);
  const casadi::DM x0_val = (warm_start != nullptr)
      ? pack_warm_start_(*warm_start)
      : pack_cold_start_(input);

  const int32_t gdim = g_dim_();
  const casadi::DMDict arg = {
      {"x0", x0_val},
      {"p",  p_val},
      {"lbg", casadi::DM::zeros(gdim, 1)},
      {"ubg", casadi::DM::inf(gdim, 1)},
  };

  casadi::DMDict res;
  try {
    res = formulation_.solver()(arg);
  } catch (const std::exception& e) {
    spdlog::error("[M5][MidMPC] IPOPT threw: {}", e.what());
    ++consecutive_failures_;
    MidMpcSolution fail;
    fail.status = SolveStatus::NumericalFailure;
    return fail;
  }

  const casadi::Dict stats = formulation_.solver().stats();
  const auto t_end = std::chrono::steady_clock::now();
  const int32_t duration_ms = static_cast<int32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count());

  MidMpcSolution sol = formulation_.unpack_solution(res.at("x"), stats);
  sol.solve_duration_ms = duration_ms;

  if (sol.status == SolveStatus::Converged) {
    consecutive_failures_ = 0;
  } else {
    ++consecutive_failures_;
    if (sol.status == SolveStatus::Infeasible) {
      // FM-2: collision unavoidable — M7 MRM-02 expected (detailed design §7.1).
      spdlog::critical("[M5][MidMPC] Infeasible: collision unavoidable; M7 MRM expected");
    }
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
  }
  return sol;
}

}  // namespace mass_l3::m5::mid_mpc
