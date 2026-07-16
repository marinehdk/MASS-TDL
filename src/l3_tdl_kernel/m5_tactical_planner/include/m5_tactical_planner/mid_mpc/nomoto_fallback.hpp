#ifndef MASS_L3_M5_MID_MPC_NOMOTO_FALLBACK_HPP_
#define MASS_L3_M5_MID_MPC_NOMOTO_FALLBACK_HPP_

// M5 Tactical Planner — Nomoto 1st-order fallback solver (DEMO-2 P0)
// PATH-D (MISRA C++:2023): no CasADi dependency; stand-alone heading-branch
// fallback for when the Mid-MPC NLP (CasADi/IPOPT) fails.
//
// Generates n_branches constant-heading trajectories using a discretised
// Nomoto 1st-order model (δ = 0, yaw-rate decay) and selects the branch
// with the best worst-case CPA against all tracked targets.
//
// The Nomoto 1st-order model (Nomoto 1957 [R6]):
//   T * r_dot + r = K * δ
// Discretised via Forward Euler (δ = 0 for fallback hold-heading mode):
//   r_{k+1} = r_k - (dt / T) * r_k
//   ψ_{k+1} = ψ_k + r_k * dt
//   x_{k+1} = x_k + u * cos(ψ_k) * dt
//   y_{k+1} = y_k + u * sin(ψ_k) * dt

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/shared/capability_manifest.hpp"

namespace mass_l3::m5::mid_mpc {

// ---------------------------------------------------------------------------
// NomotoFallbackConfig — tunable parameters for the fallback solver
// All values tagged [TBD-HAZID] pending HAZID RUN-001 calibration.
// ---------------------------------------------------------------------------
struct NomotoFallbackConfig {
  int32_t n_steps{12};       // number of integration steps per branch
  double dt_s{5.0};          // time step [s] (60 s horizon with n_steps=12)
  int32_t n_branches{13};    // number of heading branches (odd → symmetric)
  double delta_psi_rad{10.0 * 0.0174533};  // inter-branch heading spacing [rad]
};

// ---------------------------------------------------------------------------
// NomotoFallbackSolution — output from one solve() cycle
// ---------------------------------------------------------------------------
struct NomotoFallbackSolution {
  std::vector<std::vector<Eigen::Vector2d>> trajectories;  // [n_branches × (n_steps+1)]
  std::vector<double> cpa_vals;       // worst-case CPA per branch [m]
  std::vector<double> headings_rad;   // heading per branch [rad]
  int32_t primary_branch_idx{6};      // index of the center (straight) branch
};

// ---------------------------------------------------------------------------
// NomotoFallback — fast heading-branch fallback solver
// No CasADi dependency — compiles and runs on any platform.
// ---------------------------------------------------------------------------
class NomotoFallback {
 public:
  NomotoFallback(const NomotoFallbackConfig& cfg,
                 const shared::CapabilityManifest& manifest);

  /// Solve: generate branch trajectories and compute worst-case CPA per branch.
  [[nodiscard]] NomotoFallbackSolution solve(
      const MidMpcInput& input) const;

 private:
  NomotoFallbackConfig cfg_;
  double nomoto_T_s_;   // Nomoto time constant [s]
  double nomoto_K_s_;   // Nomoto rudder gain K [1/s], Tṙ+r=Kδ (stored for future use)

  /// Forward-Euler integration of one heading branch (δ = 0, hold heading).
  /// Returns (n_steps + 1) positions starting from (x0_m, y0_m).
  [[nodiscard]] std::vector<Eigen::Vector2d> integrate_branch(
      double x0_m, double y0_m, double psi_rad, double u_mps) const;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NOMOTO_FALLBACK_HPP_
