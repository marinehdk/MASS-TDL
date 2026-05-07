#ifndef MASS_L3_M5_SHARED_CPA_CALCULATOR_HPP_
#define MASS_L3_M5_SHARED_CPA_CALCULATOR_HPP_

// M5 Tactical Planner — CPA (Closest Point of Approach) Calculator
// PATH-D (MISRA C++:2023): [[nodiscard]], no mutable state (pure static API).
//
// Two CPA computation modes:
//   1. Linear (closed-form): constant-velocity assumption; O(1)
//   2. Trajectory-based: min-distance over sampled trajectory pair; O(N)
//
// CPA formula reference (linear mode):
//   TCPA = -dot(dp, dv) / |dv|^2
//   CPA  = |dp + TCPA * dv|
// where dp = relative position, dv = relative velocity.
//
// Both own-ship and target must be time-aligned to the same epoch before
// calling compute_linear().

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::shared {

class CpaCalculator {
 public:
  // -------------------------------------------------------------------------
  // CpaResult — output of both compute modes
  // -------------------------------------------------------------------------
  struct CpaResult {
    double cpa_m{0.0};         // closest point of approach [m]; always ≥ 0
    double tcpa_s{0.0};        // time to CPA [s]; negative = already past
    double uncertainty_m{0.0}; // propagated position uncertainty estimate [m]
  };

  // -------------------------------------------------------------------------
  // compute_linear() — closed-form CPA assuming constant-velocity motion
  // @param own    Own-ship state (x_m, y_m, psi_rad, u_mps, v_mps)
  // @param target Target state (x_m, y_m, cog_rad, sog_mps)
  // @returns CpaResult with cpa_m ≥ 0; tcpa_s may be negative (past CPA)
  //
  // Edge case: |dv| < epsilon → ships on same course/speed;
  //            returns CPA = current separation, TCPA = +∞ (→ 1e9)
  // -------------------------------------------------------------------------
  [[nodiscard]] static CpaResult compute_linear(
      const mass_l3::m5::TrajectoryPoint& own,
      const mass_l3::m5::TargetState& target);

  // -------------------------------------------------------------------------
  // compute_trajectory() — sample-by-sample minimum distance
  // @param own_traj  Own-ship (x, y) trajectory [metres, NED]
  // @param tgt_traj  Target   (x, y) trajectory [metres, NED]
  // @param dt_s      Timestep between consecutive samples [s]
  // @returns CpaResult; tcpa_s = index_min * dt_s
  // Pre-condition: own_traj.size() == tgt_traj.size(), size ≥ 1
  // -------------------------------------------------------------------------
  [[nodiscard]] static CpaResult compute_trajectory(
      const std::vector<Eigen::Vector2d>& own_traj,
      const std::vector<Eigen::Vector2d>& tgt_traj,
      double dt_s);

 private:
  // Velocity components from heading + speed (NED convention)
  [[nodiscard]] static Eigen::Vector2d vel_from_cog_sog(
      double cog_rad, double sog_mps) noexcept;
};

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_CPA_CALCULATOR_HPP_
