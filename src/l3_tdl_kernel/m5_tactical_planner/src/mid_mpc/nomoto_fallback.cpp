#include "m5_tactical_planner/mid_mpc/nomoto_fallback.hpp"

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/shared/cpa_calculator.hpp"

namespace mass_l3::m5::mid_mpc {

// ---------------------------------------------------------------------------
// Constructor: extract Nomoto coefficients from the vessel capability manifest
// ---------------------------------------------------------------------------
NomotoFallback::NomotoFallback(
    const NomotoFallbackConfig& cfg,
    const shared::CapabilityManifest& manifest)
    : cfg_(cfg),
      nomoto_T_s_(manifest.config().nomoto_T_s),
      nomoto_K_s_(manifest.config().nomoto_K_s) {}

// ---------------------------------------------------------------------------
// integrate_branch — Forward-Euler integration of one constant-heading branch
//
// Uses the Nomoto 1st-order yaw-rate dynamics with zero rudder command (δ=0):
//   r_{k+1} = r_k - (dt / T) * r_k
//
// With r_0 = 0 (hold initial heading), the yaw rate remains zero and the
// vessel holds the given heading psi_rad throughout the horizon.
// ---------------------------------------------------------------------------
std::vector<Eigen::Vector2d> NomotoFallback::integrate_branch(
    double x0_m, double y0_m, double psi_rad, double u_mps) const {

  const std::size_t n_points =
      static_cast<std::size_t>(cfg_.n_steps) + 1u;
  std::vector<Eigen::Vector2d> traj;
  traj.reserve(n_points);

  double x = x0_m;
  double y = y0_m;
  double psi = psi_rad;
  double r = 0.0;  // initial yaw rate: hold heading

  traj.emplace_back(x, y);

  for (int32_t k = 0; k < cfg_.n_steps; ++k) {
    // Nomoto 1st-order discrete: yaw-rate decay (δ = 0)
    r = r - (cfg_.dt_s / nomoto_T_s_) * r;

    // Kinematic update
    psi += r * cfg_.dt_s;
    x += u_mps * std::cos(psi) * cfg_.dt_s;
    y += u_mps * std::sin(psi) * cfg_.dt_s;

    traj.emplace_back(x, y);
  }

  return traj;
}

// ---------------------------------------------------------------------------
// solve — generate all branch trajectories and compute worst-case CPA
//
// For each branch i at heading psi = psi0 + delta_psi * (i - half):
//   1. Integrate the branch trajectory (for output / downstream analysis)
//   2. Compute linear CPA against each target using CpaCalculator::compute_linear
//   3. Store the minimum CPA across all targets as the branch CPA
//
// The primary (center) branch index is at half = (n_branches - 1) / 2.
// ---------------------------------------------------------------------------
NomotoFallbackSolution NomotoFallback::solve(
    const MidMpcInput& input) const {

  NomotoFallbackSolution sol;

  const double psi0 = input.own_ship.psi_rad;
  const double u    = input.own_ship.u_mps;
  const double x0   = input.own_ship.x_m;
  const double y0   = input.own_ship.y_m;

  const int32_t half = (cfg_.n_branches - 1) / 2;
  const std::size_t nb =
      static_cast<std::size_t>(cfg_.n_branches);

  sol.trajectories.resize(nb);
  sol.cpa_vals.resize(nb, 1.0e9);
  sol.headings_rad.resize(nb);
  sol.primary_branch_idx = half;

  for (int32_t i = 0; i < cfg_.n_branches; ++i) {
    const std::size_t idx = static_cast<std::size_t>(i);
    const double heading  = psi0 + cfg_.delta_psi_rad *
        static_cast<double>(i - half);

    sol.headings_rad[idx] = heading;

    // Generate trajectory positions for this branch
    sol.trajectories[idx] = integrate_branch(x0, y0, heading, u);

    // Compute linear CPA against every target
    // Uses the starting position with the branch heading to compute
    // constant-velocity CPA (fast, no CasADi dependency).
    TrajectoryPoint own_state;
    own_state.x_m     = x0;
    own_state.y_m     = y0;
    own_state.psi_rad = heading;
    own_state.u_mps   = u;
    own_state.v_mps   = 0.0;

    for (const auto& target : input.targets) {
      const auto cpa = shared::CpaCalculator::compute_linear(
          own_state, target);
      if (cpa.cpa_m < sol.cpa_vals[idx]) {
        sol.cpa_vals[idx] = cpa.cpa_m;
      }
    }
  }

  return sol;
}

}  // namespace mass_l3::m5::mid_mpc
