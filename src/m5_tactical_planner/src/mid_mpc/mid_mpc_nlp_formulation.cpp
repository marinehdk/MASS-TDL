// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10
// (dynamic-link boundary).
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// ===========================================================================
// Anonymous namespace: IPOPT options and helper builders.
// ===========================================================================
namespace {

// [TBD-HAZID] IPOPT max iterations per Mid-MPC cycle.
// Default 150; calibrate from FCB sea-trial timing data (HAZID RUN-001 WP-04).
constexpr int32_t kIpoptMaxIter = 150;

// [TBD-HAZID] IPOPT convergence tolerance.
// Default 1e-4; calibrate per detailed design §5.2.4 SLA budget.
constexpr double kIpoptTol = 1.0e-4;

// IPOPT max CPU time [s] — 450 ms ≤ 500 ms cycle SLA (detailed design §5.2.4).
constexpr double kIpoptMaxCpuTime = 0.45;

// [TBD-HAZID] Default ROT max [rad/s] when caller does not override via p_.
// 0.2094 rad/s ≈ 12°/s; FCB nominal at 18 kn (vessel_dynamics_model default).
// Calibrate per vessel/sea-state during HAZID RUN-001 WP-02.
constexpr double kDefaultRotMaxRadS = 0.2094;

// Numerical guards for weight denominator (avoid division-by-zero).
constexpr double kMinCpaForWeight  = 1.0;   // [m]
constexpr double kMinTcpaForWeight = 1.0;   // [s]

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
// g_dim() — total constraint count. 2N (heading) + 2N (speed) + (N-1) (ROT) = 5N-1.
// ===========================================================================
int32_t MidMpcNlpFormulation::g_dim() const noexcept {
  const int32_t N = cfg_.n_horizon;
  return 5 * N - 1;
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
  return cost;
}

// ===========================================================================
// build_constraints_() — heading bounds + speed bounds + ROT differential.
//
// Constraint convention: g >= 0 (lower bound = 0, upper bound = +inf).
// Phase E1: COLREGs rules handled as soft cost in J_colreg; hard constraints
// deferred to Phase E2.
// ===========================================================================
casadi::MX MidMpcNlpFormulation::build_constraints_() const {
  const int32_t N = cfg_.n_horizon;

  // Heading bounds: psi[k] - hmin >= 0  and  hmax - psi[k] >= 0
  const casadi::MX hmin_rep = casadi::MX::repmat(slot(p_, kIdxHeadingMin), N, 1);
  const casadi::MX hmax_rep = casadi::MX::repmat(slot(p_, kIdxHeadingMax), N, 1);
  const casadi::MX g_h_lo = psi_ - hmin_rep;
  const casadi::MX g_h_hi = hmax_rep - psi_;

  // Speed bounds: u[k] - umin >= 0  and  umax - u[k] >= 0
  const casadi::MX umin_rep = casadi::MX::repmat(slot(p_, kIdxSpeedMin), N, 1);
  const casadi::MX umax_rep = casadi::MX::repmat(slot(p_, kIdxSpeedMax), N, 1);
  const casadi::MX g_s_lo = u_ - umin_rep;
  const casadi::MX g_s_hi = umax_rep - u_;

  // ROT differential: rot_max*dt - |psi[k+1] - psi[k]| >= 0 for k ∈ [0, N-2]
  const casadi::MX dpsi = psi_(casadi::Slice(1, N)) - psi_(casadi::Slice(0, N - 1));
  const casadi::MX rot_step = slot(p_, kIdxRotMax) * casadi::DM(cfg_.dt_s);
  const casadi::MX rot_step_rep = casadi::MX::repmat(rot_step, N - 1, 1);
  const casadi::MX g_rot = rot_step_rep - casadi::MX::abs(dpsi);

  return casadi::MX::vertcat({g_h_lo, g_h_hi, g_s_lo, g_s_hi, g_rot});
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
  opts["ipopt.print_level"]           = 0;
  opts["ipopt.linear_solver"]         = std::string{"mumps"};
  opts["ipopt.hessian_approximation"] = std::string{"limited-memory"};
  opts["ipopt.max_cpu_time"]          = kIpoptMaxCpuTime;
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

  // [TBD-HAZID] ROT max: hard-coded FCB default (0.2094 rad/s ≈ 12°/s).
  // Phase E2: thread rot_max_rad_s through ConstraintInputs from VesselDynamicsModel.
  p(kIdxRotMax) = kDefaultRotMaxRadS;

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
    p(base + 4) = 1.0 / (cpa * tcpa);
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
        ipopt_status == "Feasible_Point_Found") {
      sol.status = MidMpcSolution::Status::Converged;
    } else if (ipopt_status == "Maximum_Iterations_Exceeded" ||
               ipopt_status == "Maximum_CpuTime_Exceeded") {
      sol.status = MidMpcSolution::Status::Timeout;
    } else if (ipopt_status == "Infeasible_Problem_Detected") {
      sol.status = MidMpcSolution::Status::Infeasible;
    } else {
      sol.status = MidMpcSolution::Status::NumericalFailure;
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
        static_cast<int64_t>(stats.at("iter_count")));
  }
  return sol;
}

}  // namespace mass_l3::m5::mid_mpc
