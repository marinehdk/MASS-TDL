#include "m5_tactical_planner/shared/cpa_calculator.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

namespace mass_l3::m5::shared {

using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::TargetState;

// ---------------------------------------------------------------------------
// vel_from_cog_sog() — NED velocity vector from course + speed
// NED convention: north = +x, east = +y, psi/cog measured from north clockwise.
// vx = sog * cos(cog),  vy = sog * sin(cog)
// ---------------------------------------------------------------------------
Eigen::Vector2d CpaCalculator::vel_from_cog_sog(double cog_rad,
                                                  double sog_mps) noexcept {
  return Eigen::Vector2d{sog_mps * std::cos(cog_rad),
                          sog_mps * std::sin(cog_rad)};
}

// ---------------------------------------------------------------------------
// compute_linear() — closed-form CPA (constant velocity assumption)
// ---------------------------------------------------------------------------
CpaCalculator::CpaResult CpaCalculator::compute_linear(
    const TrajectoryPoint& own,
    const TargetState& target) {

  constexpr double kEpsilon = 1.0e-6;  // m/s — zero-velocity threshold

  // Own-ship velocity (NED)
  // own.psi_rad is heading; u_mps/v_mps are body-frame speeds.
  // Convert to NED: vx = u*cos(psi) - v*sin(psi), vy = u*sin(psi) + v*cos(psi)
  const Eigen::Vector2d own_vel{
      own.u_mps * std::cos(own.psi_rad) - own.v_mps * std::sin(own.psi_rad),
      own.u_mps * std::sin(own.psi_rad) + own.v_mps * std::cos(own.psi_rad)};

  // Target velocity (NED) from COG + SOG
  const Eigen::Vector2d tgt_vel =
      vel_from_cog_sog(target.cog_rad, target.sog_mps);

  // Relative position: dp = target - own
  const Eigen::Vector2d dp{target.x_m - own.x_m,
                             target.y_m - own.y_m};

  // Relative velocity: dv = target_vel - own_vel
  const Eigen::Vector2d dv = tgt_vel - own_vel;

  const double dv_sq = dv.squaredNorm();

  CpaResult result;

  if (dv_sq < kEpsilon * kEpsilon) {
    // Same velocity (or both stationary): CPA = current separation, TCPA = +∞
    result.cpa_m  = dp.norm();
    result.tcpa_s = 1.0e9;  // sentinel for "never converges"
    return result;
  }

  // TCPA = -dot(dp, dv) / |dv|^2
  result.tcpa_s = -dp.dot(dv) / dv_sq;

  // CPA = |dp + TCPA * dv|
  const Eigen::Vector2d cpa_vec = dp + result.tcpa_s * dv;
  result.cpa_m = cpa_vec.norm();

  // uncertainty_m: not computed in linear mode (requires covariance input)
  result.uncertainty_m = 0.0;

  return result;
}

// ---------------------------------------------------------------------------
// compute_trajectory() — sample-by-sample minimum distance
// ---------------------------------------------------------------------------
CpaCalculator::CpaResult CpaCalculator::compute_trajectory(
    const std::vector<Eigen::Vector2d>& own_traj,
    const std::vector<Eigen::Vector2d>& tgt_traj,
    double dt_s) {

  assert(!own_traj.empty() && !tgt_traj.empty());
  assert(own_traj.size() == tgt_traj.size());

  const std::size_t n = std::min(own_traj.size(), tgt_traj.size());

  double min_dist_m    = std::numeric_limits<double>::max();
  std::size_t min_idx  = 0u;

  for (std::size_t i = 0u; i < n; ++i) {
    const double dist = (own_traj[i] - tgt_traj[i]).norm();
    if (dist < min_dist_m) {
      min_dist_m = dist;
      min_idx    = i;
    }
  }

  CpaResult result;
  result.cpa_m  = min_dist_m;
  result.tcpa_s = static_cast<double>(min_idx) * dt_s;
  result.uncertainty_m = 0.0;
  return result;
}

}  // namespace mass_l3::m5::shared
