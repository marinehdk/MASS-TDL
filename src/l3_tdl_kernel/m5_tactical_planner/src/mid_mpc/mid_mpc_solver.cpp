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

// [TBD-HAZID] Consecutive-failure threshold before MRM-02 escalation.
// Calibrate via HAZID RUN-001 WP-04 FM-2 sensitivity analysis (detailed design §7.1).
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
// If warm trajectory is shorter than N (degenerate), repeats the last valid
// point rather than leaving zeros (avoids psi=0/u=0 starting outside bounds).
// ===========================================================================
casadi::DM MidMpcSolver::pack_warm_start_(const MidMpcSolution& warm) const {
  const int32_t N     = formulation_.config().n_horizon;
  const int32_t n_warm = static_cast<int32_t>(warm.trajectory.size());
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    const int32_t src = (k < n_warm) ? k : (n_warm - 1);
    if (src >= 0) {
      const auto& pt = warm.trajectory[static_cast<std::size_t>(src)];
      x0(k)     = pt.psi_rad;
      x0(N + k) = pt.u_mps;
    }
  }
  return x0;
}

// ===========================================================================
// pack_cold_start_() — initial guess from current own-ship state.
// Uses own-ship heading as psi seed (natural initial point near optimum)
// and current speed (or planned/nominal if speed < 0.1 m/s).
// ===========================================================================
casadi::DM MidMpcSolver::pack_cold_start_(const MidMpcInput& input) const {
  const int32_t N = formulation_.config().n_horizon;
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  const double psi_seed = input.own_ship.psi_rad;
  const double u_seed = (input.own_ship.u_mps > 0.1) ? input.own_ship.u_mps
                        : (input.planned_speed_mps > 0.1 ? input.planned_speed_mps : 5.14);
  for (int32_t k = 0; k < N; ++k) {
    x0(k)     = psi_seed;
    x0(N + k) = u_seed;
  }
  return x0;
}

// ===========================================================================
// solve() — pack params, call IPOPT, record timing, update failure counter.
//
// Constraint convention (g >= 0): lbg = zeros, ubg = +inf by default.
// Slice N1 (spec §3.8): the per-class lbg/ubg are now built from the
// formulation's RowRegistry + the per-cycle RowBoundConfig, enabling:
//   - prefix-equality rows (active k<K → [0,0], inactive → [-inf,+inf] double)
//   - COLREG prefix softening (k<K → [-inf,+inf], k>=K → [0,+inf])
//   - direction_disabled (all direction/min_alt → [-inf,+inf])
// Default RowBoundConfig (K=0, no flags) reproduces the legacy zeros/inf
// everywhere except prefix-equality (which becomes [-inf,+inf] = no-op), so
// N1 first version does not change runtime behaviour.
// ===========================================================================
MidMpcSolution MidMpcSolver::solve(const MidMpcInput& input,
                                    const MidMpcSolution* warm_start,
                                    const RowBoundConfig& row_bounds) {
  const auto t_start = std::chrono::steady_clock::now();

  const casadi::DM p_val = formulation_.pack_parameters(input);
  const casadi::DM x0_val = (warm_start != nullptr)
      ? pack_warm_start_(*warm_start)
      : pack_cold_start_(input);

  const int32_t gdim = g_dim_();

  // Heading & speed box limits as per-variable bounds (lbx/ubx). x = [psi; u].
  // psi[k] in [heading_min, heading_max], u[k] in [speed_min, speed_max].
  // IPOPT keeps every iterate strictly inside these bounds and auto-projects
  // x0, so a box-active optimum is the robust case (vs. restoration-fragile
  // general inequality rows — see MidMpcNlpFormulation::g_dim rationale).
  const int32_t N = formulation_.config().n_horizon;
  const auto& cst = input.constraints;
  casadi::DM lbx = casadi::DM::zeros(2 * N, 1);
  casadi::DM ubx = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    lbx(k)     = cst.heading_min_rad;
    ubx(k)     = cst.heading_max_rad;
    lbx(N + k) = cst.speed_min_mps;
    ubx(N + k) = cst.speed_max_mps;
  }

  // Slice N1: per-class lbg/ubg from the formulation's RowRegistry.
  // RowBoundConfig (default {}) reproduces legacy zeros/inf except for the
  // prefix-equality class, which is double-disabled [-inf,+inf] when K=0 (no
  // prefix → the placeholder rows are unconstrained, a no-op).
  const BoundArray bounds =
      formulation_.row_registry().build_bounds(row_bounds);
  const int32_t nb = static_cast<int32_t>(bounds.lbg.size());
  // FAIL-CLOSED (spec §3.8/§10.1, review High): the registry is rebuilt per-cycle
  // in build_constraints_, so total_rows() MUST equal g_dim(). A size mismatch is
  // a row-contract bug (registry and the symbolic g built out of sync). Falling
  // back to legacy zeros/inf would silently re-harden softened COLREG rows /
  // degrade active equalities into half-constraints — so do NOT solve; return a
  // NumericalFailure instead (same path as an IPOPT throw, below).
  if (nb != gdim) {
    const auto t_mm = std::chrono::steady_clock::now();
    spdlog::error("[M5][MidMPC] row registry size mismatch: registry={} g_dim={}",
                  nb, gdim);
    ++consecutive_failures_;
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
    MidMpcSolution fail;
    fail.status = SolveStatus::NumericalFailure;
    fail.solve_duration_ms = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t_mm - t_start).count());
    return fail;
  }

  casadi::DM lbg = casadi::DM::zeros(gdim, 1);
  casadi::DM ubg = casadi::DM::inf(gdim, 1);
  for (int32_t i = 0; i < gdim; ++i) {
    lbg(i) = bounds.lbg[static_cast<std::size_t>(i)];
    ubg(i) = bounds.ubg[static_cast<std::size_t>(i)];
  }

  const casadi::DMDict arg = {
      {"x0",  x0_val},
      {"p",   p_val},
      {"lbx", lbx},
      {"ubx", ubx},
      {"lbg", lbg},
      {"ubg", ubg},
  };

  casadi::DMDict res;
  try {
    res = formulation_.solver()(arg);
  } catch (const std::exception& e) {
    const auto t_ex = std::chrono::steady_clock::now();
    spdlog::error("[M5][MidMPC] IPOPT threw: {}", e.what());
    ++consecutive_failures_;
    if (consecutive_failures_ > kConsecutiveFailureEscalation) {
      spdlog::critical("[M5][MidMPC] {} consecutive failures; M7 MRM-02 escalation",
                       consecutive_failures_);
    }
    MidMpcSolution fail;
    fail.status = SolveStatus::NumericalFailure;
    fail.solve_duration_ms = static_cast<int32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t_ex - t_start).count());
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
