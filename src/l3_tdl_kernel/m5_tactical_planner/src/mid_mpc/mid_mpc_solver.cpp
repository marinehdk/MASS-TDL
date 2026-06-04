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
// pack_cold_start_() — constant-heading + constant-speed (ROT = 0).
// Neutral initial guess; IPOPT finds the optimum from the feasibility interior.
// ===========================================================================
casadi::DM MidMpcSolver::pack_cold_start_(const MidMpcInput& input) const {
  const int32_t N = formulation_.config().n_horizon;
  casadi::DM x0 = casadi::DM::zeros(2 * N, 1);
  const double psi_seed = 0.5 * (input.constraints.heading_min_rad + input.constraints.heading_max_rad);
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

  // Phase 3.5 in-loop calibration: re-evaluate the cost sub-terms at the
  // solved trajectory using the SAME arithmetic as
  // build_colreg_cost_/build_distance_cost_/build_velocity_cost_. We hardcode
  // the current formulation defaults (w_colreg=1000, w_dist=10, w_vel=1,
  // kColregZeta=1e-3) and the current build formula (exp_flip) to give us
  // ground-truth per-term magnitudes from the actual IPOPT solution. After
  // we pick the right weight / formula combination, this block is removed
  // and the formulation's own cost split (currently zero-init) is filled
  // in properly.
  if (sol.status == SolveStatus::Converged && !sol.trajectory.empty()) {
    const int32_t N = static_cast<int32_t>(sol.trajectory.size());
    const double dt = formulation_.config().dt_s;
    const double w_c = 1000.0, w_d = 10.0, w_v = 1.0;
    const double Z   = 1.0e-3;
    const double cpa_safe = input.constraints.cpa_safe_m;
    // Build own trajectory by integrating psi/u from initial state.
    double cx = input.own_ship.x_m, cy = input.own_ship.y_m;
    const double u0 = input.own_ship.u_mps;
    std::vector<double> x_own(static_cast<std::size_t>(N)), y_own(static_cast<std::size_t>(N));
    for (int32_t k = 0; k < N; ++k) {
      cx += u0 * dt * std::cos(sol.trajectory[static_cast<std::size_t>(k)].psi_rad);
      cy += u0 * dt * std::sin(sol.trajectory[static_cast<std::size_t>(k)].psi_rad);
      x_own[static_cast<std::size_t>(k)] = cx;
      y_own[static_cast<std::size_t>(k)] = cy;
    }
    double J_colreg = 0.0, J_dist = 0.0, J_vel = 0.0;
    for (int32_t k = 0; k < N; ++k) {
      J_dist += std::pow(sol.trajectory[static_cast<std::size_t>(k)].psi_rad
                          - input.planned_route_bearing_rad, 2);
      J_vel  += std::pow(sol.trajectory[static_cast<std::size_t>(k)].u_mps
                          - input.planned_speed_mps, 2);
      const double kdt = static_cast<double>(k) * dt;
      for (const auto& tgt : input.targets) {
        const double w_cpa = std::max(tgt.cpa_m * 0.2, 50.0);
        const double w_tcpa = std::max(tgt.tcpa_s * 0.2, 10.0);
        const double tw = 1.0 / (w_cpa * w_tcpa);
        const double tdx = tgt.sog_mps * std::cos(tgt.cog_rad);
        const double tdy = tgt.sog_mps * std::sin(tgt.cog_rad);
        const double dx = x_own[static_cast<std::size_t>(k)] - (tgt.x_m + tdx * kdt);
        const double dy = y_own[static_cast<std::size_t>(k)] - (tgt.y_m + tdy * kdt);
        const double d  = std::sqrt(dx*dx + dy*dy + 1.0);
        // Build currently uses exp(-Z * (cpa - d))  [per D3.3 fix commit].
        J_colreg += tw * std::exp(-Z * (cpa_safe - d));
      }
    }
    sol.cost_colreg = J_colreg;
    sol.cost_dist   = J_dist;
    sol.cost_vel    = J_vel;
    sol.cost_total  = w_c * J_colreg + w_d * J_dist + w_v * J_vel;
    spdlog::info("[M5][Diag] J_colreg={:.4e} J_dist={:.4e} J_vel={:.4e} | "
                 "w_c*Jc={:.4e} w_d*Jd={:.4e} w_v*Jv={:.4e} | "
                 "psi0={:.1f}deg psiN={:.1f}deg",
                 J_colreg, J_dist, J_vel,
                 w_c * J_colreg, w_d * J_dist, w_v * J_vel,
                 sol.trajectory.front().psi_rad * (180.0 / M_PI),
                 sol.trajectory.back().psi_rad  * (180.0 / M_PI));
  }

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
