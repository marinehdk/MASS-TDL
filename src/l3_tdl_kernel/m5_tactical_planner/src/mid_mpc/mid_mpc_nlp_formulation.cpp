// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <casadi/casadi.hpp>
#include <spdlog/spdlog.h>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// ===========================================================================
// Anonymous namespace: IPOPT options and helper builders.
// ===========================================================================
namespace {

// [TBD-HAZID] IPOPT max iterations per Mid-MPC cycle.
// Default 150; calibrate from FCB sea-trial timing data (HAZID RUN-001 WP-04).
constexpr int32_t kIpoptMaxIter = 500;

// [TBD-HAZID] IPOPT convergence tolerance.
// Default 1e-4; calibrate per detailed design §5.2.4 SLA budget.
constexpr double kIpoptTol = 1.0e-4;

// [TBD-HAZID] IPOPT acceptable convergence tolerance (looser than tol).
// If IPOPT finds a point within this tolerance after acceptable_iter, it returns success.
// Enables graceful degradation within the cycle SLA.
constexpr double kIpoptAcceptableTol = 1.0e-3;

// [TBD-HAZID] Minimum IPOPT iterations before acceptable_tol-based early exit.
constexpr int32_t kIpoptAcceptableIter = 5;

// [TBD-HAZID] IPOPT max CPU time [s] — 2.0 s within 1 Hz cycle (detailed design §5.2.4).
constexpr double kIpoptMaxCpuTime = 2.0;

// [TBD-HAZID] Default ROT max [rad/s] when caller does not override via p_.
// 0.2094 rad/s ≈ 12°/s; FCB nominal at 18 kn (vessel_dynamics_model default).
// Calibrate per vessel/sea-state during HAZID RUN-001 WP-02.
constexpr double kDefaultRotMaxRadS = 0.2094;

// Numerical guards for weight denominator (avoid division-by-zero).
constexpr double kMinCpaForWeight  = 1.0;   // [m]
constexpr double kMinTcpaForWeight = 1.0;   // [s]

// [TBD-HAZID] Maximum per-target weight cap to prevent gradient explosion
// from very small CPA/TCPA products.
constexpr double kMaxTargetWeight = 100.0;
constexpr double kTargetWeightDenomMin = 1.0;  // [m·s] minimum CPA×TCPA product

// Slice helper: extract scalar p[i] as 1×1 MX.
casadi::MX slot(const casadi::MX& p, int32_t i) {
  return p(casadi::Slice(i, i + 1));
}

}  // namespace

// ===========================================================================
// Constructor — store config (no graph build yet; explicit build call required).
// ===========================================================================
MidMpcNlpFormulation::MidMpcNlpFormulation(const Config& cfg) : cfg_(cfg) {
  if (cfg_.max_targets > kMaxTargets) {
    cfg_.max_targets = kMaxTargets;
  }
  if (cfg_.max_targets < 0) {
    cfg_.max_targets = 0;
  }
  if (cfg_.n_horizon < 2) {
    cfg_.n_horizon = 2;  // require ≥2 steps for ROT differential constraint
  }
}

// ===========================================================================
// g_dim() — general-constraint count = 2*(N-1) ROT differential rows only
// (upper + lower smooth linear bound per step; see build_constraints_).
//
// Heading and speed box limits are simple per-variable bounds; they are passed
// to IPOPT as lbx/ubx (set per-cycle in MidMpcSolver::solve), NOT as general
// inequality rows in g. Encoding a box optimum as an active *general* constraint
// under limited-memory Hessian + adaptive mu is restoration-fragile (the
// cost optimum is pinned to the box edge when the route bearing lies outside
// the avoidance window), which produced intermittent Restoration_Failed /
// Maximum_Iterations. Variable bounds make a box-active optimum IPOPT's
// canonical robust case and auto-project the warm start into [lbx,ubx].
// ===========================================================================
int32_t MidMpcNlpFormulation::g_dim() const noexcept {
  const int32_t N = cfg_.n_horizon;
  return 2 * (N - 1);  // two smooth ROT rows (upper + lower) per step
}

// ===========================================================================
// build_distance_cost_() — sum_k (psi[k] - planned_route_bearing)^2
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_distance_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX bearing = slot(p_, kIdxRouteBearing);
  const casadi::MX bearing_rep = casadi::MX::repmat(bearing, N, 1);
  const casadi::MX err = psi_ - bearing_rep;
  return casadi::MX::dot(err, err);
}

// ===========================================================================
// build_velocity_cost_() — sum_k (u[k] - planned_speed)^2
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_velocity_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX planned = slot(p_, kIdxPlannedSpeed);
  const casadi::MX planned_rep = casadi::MX::repmat(planned, N, 1);
  const casadi::MX err = u_ - planned_rep;
  return casadi::MX::dot(err, err);
}

// ===========================================================================
// build_colreg_cost_() — soft CPA penalty over (N steps × max_targets).
//
// NED convention (types.hpp:29: psi=0 → north, positive clockwise):
//   dx[j] = u[j]*dt*cos(psi[j])    (north component)
//   dy[j] = u[j]*dt*sin(psi[j])    (east  component)
// Cumulative own-ship position relative to (x0, y0) is integrated step-by-step.
//
// Phase E1: COLREGs rules handled as soft cost in J_colreg; hard constraints
// deferred to Phase E2.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_colreg_cost_() const {
  const int32_t N  = cfg_.n_horizon;
  const int32_t Nt = cfg_.max_targets;
  const casadi::MX dt   = casadi::DM(cfg_.dt_s);
  const casadi::MX cpa  = slot(p_, kIdxCpaSafe);
  const casadi::MX cpa2 = cpa * cpa;

  // Pre-integrate own-ship cumulative position at each step k ∈ [0, N-1].
  std::vector<casadi::MX> x_own(static_cast<std::size_t>(N));
  std::vector<casadi::MX> y_own(static_cast<std::size_t>(N));
  casadi::MX cx = slot(p_, kIdxX0);
  casadi::MX cy = slot(p_, kIdxY0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
    x_own[static_cast<std::size_t>(k)] = cx;
    y_own[static_cast<std::size_t>(k)] = cy;
  }

  // Accumulate per-target, per-step penalty.
  casadi::MX cost(0.0);
  const casadi::MX zero = casadi::DM(0.0);
  for (int32_t t = 0; t < Nt; ++t) {
    const int32_t base = kIdxTargets + t * kTargetStride;
    const casadi::MX tx = slot(p_, base + 0);
    const casadi::MX ty = slot(p_, base + 1);
    const casadi::MX tc = slot(p_, base + 2);
    const casadi::MX ts = slot(p_, base + 3);
    const casadi::MX tw = slot(p_, base + 4);
    const casadi::MX tdx = ts * casadi::MX::cos(tc);
    const casadi::MX tdy = ts * casadi::MX::sin(tc);
    for (int32_t k = 0; k < N; ++k) {
      const casadi::MX kdt = casadi::DM(static_cast<double>(k) * cfg_.dt_s);
      const casadi::MX dx  = x_own[static_cast<std::size_t>(k)] - (tx + tdx * kdt);
      const casadi::MX dy  = y_own[static_cast<std::size_t>(k)] - (ty + tdy * kdt);
      const casadi::MX d2  = dx * dx + dy * dy;
      cost = cost + tw * casadi::MX::fmax(zero, cpa2 - d2);
    }
  }
  // Return per-target per-step average cost to normalize scale across
  // varying target counts and horizon lengths.
  const casadi::MX scale_denom = casadi::DM(
      static_cast<double>(std::max(1, Nt * N)));
  return cost / scale_denom;
}

// ===========================================================================
// build_constraints_() — ROT differential only (g >= 0).
//
// Heading/speed box limits are NOT here — they are per-variable bounds passed
// to IPOPT as lbx/ubx by MidMpcSolver::solve (see g_dim() rationale). Only the
// inter-step rate-of-turn coupling remains a general constraint.
//
// Constraint convention: g >= 0 (lower bound = 0, upper bound = +inf).
// Phase E1: COLREGs rules handled as soft cost in J_colreg; hard constraints
// deferred to Phase E2.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_constraints_() const {
  const int32_t N = cfg_.n_horizon;

  // ROT differential: |psi[k+1] - psi[k]| <= rot_max*dt for k ∈ [0, N-2].
  // Encoded as TWO smooth linear rows per step rather than one |.| row:
  //   rot_step - dpsi >= 0   (dpsi <=  rot_step)
  //   rot_step + dpsi >= 0   (dpsi >= -rot_step)
  // Exactly equivalent feasible set, but smooth. The abs() form has a gradient
  // kink at dpsi=0 — precisely where a near-constant-heading trajectory sits —
  // so its constraint Jacobian sign-flips every iteration even while the bound
  // is slack, which destabilises the limited-memory Hessian and contributed to
  // intermittent Restoration_Failed / Maximum_Iterations.
  const casadi::MX dpsi = psi_(casadi::Slice(1, N)) - psi_(casadi::Slice(0, N - 1));
  const casadi::MX rot_step = slot(p_, kIdxRotMax) * casadi::DM(cfg_.dt_s);
  const casadi::MX rot_step_rep = casadi::MX::repmat(rot_step, N - 1, 1);
  const casadi::MX g_rot_hi = rot_step_rep - dpsi;
  const casadi::MX g_rot_lo = rot_step_rep + dpsi;

  return casadi::MX::vertcat({g_rot_hi, g_rot_lo});
}

// ===========================================================================
// build_symbolic_graph() — assemble decision vars, parameters, J, g, nlpsol.
// ===========================================================================
void MidMpcNlpFormulation::build_symbolic_graph() {
  const int32_t N = cfg_.n_horizon;
  psi_ = casadi::MX::sym("psi", N, 1);
  u_   = casadi::MX::sym("u",   N, 1);
  p_   = casadi::MX::sym("p", parameter_dim_(), 1);
  const casadi::MX x = casadi::MX::vertcat({psi_, u_});

  // Objective: weighted sum of three sub-costs.
  J_ = casadi::DM(cfg_.w_colreg) * build_colreg_cost_()
     + casadi::DM(cfg_.w_dist)   * build_distance_cost_()
     + casadi::DM(cfg_.w_vel)    * build_velocity_cost_();

  g_ = build_constraints_();

  const casadi::MXDict nlp = {{"x", x}, {"p", p_}, {"f", J_}, {"g", g_}};
  casadi::Dict opts;
  opts["ipopt.max_iter"]              = kIpoptMaxIter;
  opts["ipopt.tol"]                   = kIpoptTol;
  opts["ipopt.acceptable_tol"]        = kIpoptAcceptableTol;
  opts["ipopt.acceptable_iter"]       = kIpoptAcceptableIter;
  opts["ipopt.print_level"]           = 0;
  opts["ipopt.linear_solver"]         = std::string{"mumps"};
  opts["ipopt.hessian_approximation"] = std::string{"limited-memory"};
  opts["ipopt.max_cpu_time"]          = kIpoptMaxCpuTime;
  opts["ipopt.bound_push"]            = 1.0e-4;
  opts["ipopt.bound_frac"]            = 1.0e-4;
  opts["ipopt.mu_strategy"]           = std::string{"adaptive"};
  opts["ipopt.constr_viol_tol"]       = 1.0e-3;
  opts["ipopt.acceptable_constr_viol_tol"] = 1.0e-2;
  opts["print_time"]                  = false;
  solver_ = casadi::nlpsol("mid_mpc_solver", "ipopt", nlp, opts);
}

// ===========================================================================
// pack_parameters() — MidMpcInput → DM column vector in p layout.
// ===========================================================================
casadi::DM MidMpcNlpFormulation::pack_parameters(const MidMpcInput& input) const {
  casadi::DM p = casadi::DM::zeros(parameter_dim_(), 1);

  // Initial state.
  // kIdxPsi0/kIdxU0 reserved for Phase E2 warm-start initial heading/speed.
  p(kIdxPsi0) = input.own_ship.psi_rad;
  p(kIdxU0)   = input.own_ship.u_mps;
  p(kIdxX0)   = input.own_ship.x_m;
  p(kIdxY0)   = input.own_ship.y_m;

  // Route + planned speed.
  p(kIdxRouteBearing) = input.planned_route_bearing_rad;
  p(kIdxPlannedSpeed) = input.planned_speed_mps;

  // Constraint bounds + COLREGs reference state from ConstraintInputs.
  p(kIdxHeadingMin) = input.constraints.heading_min_rad;
  p(kIdxHeadingMax) = input.constraints.heading_max_rad;
  p(kIdxSpeedMin)   = input.constraints.speed_min_mps;
  p(kIdxSpeedMax)   = input.constraints.speed_max_mps;
  p(kIdxCpaSafe)    = input.constraints.cpa_safe_m;
  // kIdxOwnPsi reserved for Phase E2 hard COLREGs directional constraints.
  p(kIdxOwnPsi)     = input.constraints.own_ship_psi_rad;

  // [TBD-HAZID] ROT max: from VesselDynamicsModel via MidMpcInput (MUST-5).
  // Fallback to kDefaultRotMaxRadS if MidMpcInput default is unchanged.
  p(kIdxRotMax) = input.rot_max_rad_s;

  // Targets: zero-padded up to cfg_.max_targets.
  const int32_t n_t = std::min(
      static_cast<int32_t>(input.targets.size()), cfg_.max_targets);
  for (int32_t t = 0; t < n_t; ++t) {
    const auto& tgt = input.targets[static_cast<std::size_t>(t)];
    const int32_t base = kIdxTargets + t * kTargetStride;
    p(base + 0) = tgt.x_m;
    p(base + 1) = tgt.y_m;
    p(base + 2) = tgt.cog_rad;
    p(base + 3) = tgt.sog_mps;
    const double cpa  = std::max(tgt.cpa_m,  kMinCpaForWeight);
    const double tcpa = std::max(tgt.tcpa_s, kMinTcpaForWeight);
    const double cpa_tcpa_product = std::max(cpa * tcpa, kTargetWeightDenomMin);
    p(base + 4) = std::min(1.0 / cpa_tcpa_product, kMaxTargetWeight);
  }
  return p;
}

// ===========================================================================
// unpack_solution() — IPOPT x* + stats → MidMpcSolution
//
// Phase E1: cost_total / cost_colreg / cost_dist / cost_vel are not split out
// from CasADi stats (would require separate Functions). They remain zero-init.
// ===========================================================================
MidMpcSolution MidMpcNlpFormulation::unpack_solution(
    const casadi::DM& x_opt, const casadi::Dict& stats) const {
  MidMpcSolution sol;
  const int32_t N = cfg_.n_horizon;

  // Map IPOPT return_status string to Status enum.
  if (stats.count("return_status") > 0u) {
    const std::string ipopt_status =
        static_cast<std::string>(stats.at("return_status"));
    if (ipopt_status == "Solve_Succeeded" ||
        ipopt_status == "Feasible_Point_Found" ||
        ipopt_status == "Solved_To_Acceptable_Level") {
      sol.status = MidMpcSolution::Status::Converged;
    } else if (ipopt_status == "Maximum_Iterations_Exceeded" ||
               ipopt_status == "Maximum_CpuTime_Exceeded") {
      sol.status = MidMpcSolution::Status::Timeout;
    } else if (ipopt_status == "Infeasible_Problem_Detected") {
      sol.status = MidMpcSolution::Status::Infeasible;
    } else {
      sol.status = MidMpcSolution::Status::NumericalFailure;
    }

    if (sol.status != MidMpcSolution::Status::Converged) {
      int32_t iter = 0;
      if (stats.count("iter_count") > 0u) {
        iter = static_cast<int32_t>(static_cast<int>(stats.at("iter_count")));
      }
      spdlog::warn("[M5][MidMPC] IPOPT status={} iter={} x_dim={}",
                   ipopt_status, iter,
                   static_cast<int32_t>(x_opt.numel()));
    }
  }

  // Guard against degenerate x_opt (IPOPT failure may return wrong-size vector).
  const int32_t expected_dim = 2 * N;
  if (static_cast<int32_t>(x_opt.numel()) != expected_dim) {
    sol.status = MidMpcSolution::Status::NumericalFailure;
    return sol;  // trajectory stays empty
  }

  sol.trajectory.resize(static_cast<std::size_t>(N));
  for (int32_t k = 0; k < N; ++k) {
    auto& point = sol.trajectory[static_cast<std::size_t>(k)];
    point.psi_rad = static_cast<double>(x_opt(k));
    point.u_mps   = static_cast<double>(x_opt(N + k));
    point.t_s     = static_cast<double>(k) * cfg_.dt_s;
  }
  if (stats.count("iter_count") > 0u) {
    sol.ipopt_iterations = static_cast<int32_t>(
        static_cast<int>(stats.at("iter_count")));
  }
  return sol;
}

}  // namespace mass_l3::m5::mid_mpc
