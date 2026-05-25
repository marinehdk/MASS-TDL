#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"

#include <cmath>
#include <cstdint>

namespace mass_l3::m5::bc_mpc {

BcMpcBranchFormulation::BcMpcBranchFormulation(const Config& cfg) : cfg_(cfg) {}

// num_steps_() centralises the horizon→step count conversion so both predict
// functions remain byte-for-byte consistent (avoid rounding skew from two
// independent calls to round()).
std::int32_t BcMpcBranchFormulation::num_steps_() const noexcept {
  return static_cast<std::int32_t>(std::round(cfg_.horizon_s / cfg_.dt_s));
}

// candidate_headings() — 3-tier urgency branching (P2-B-01).
// urgency > 0.95  → k_critical (13 branches, ±60°)
// urgency > 0.80  → k_high     ( 7 branches, ±30°)
// else           → k_low      ( 5 branches, ±20°)
//
// Normalises each heading into [-π, π) so downstream CPA arithmetic stays
// consistent (no wrap-around artefacts from large offsets).
std::vector<double> BcMpcBranchFormulation::candidate_headings(
    double current_heading_rad, double urgency_level) const
{
  std::int32_t k;
  if (urgency_level > 0.95) {
    k = cfg_.k_critical;
  } else if (urgency_level > cfg_.urgency_threshold) {
    k = cfg_.k_high;
  } else {
    k = cfg_.k_low;
  }

  std::vector<double> headings;
  headings.reserve(static_cast<std::size_t>(k));

  const std::int32_t half = k / 2;
  for (std::int32_t i = 0; i < k; ++i) {
    double psi = current_heading_rad
               + static_cast<double>(i - half) * cfg_.delta_psi_rad;
    while (psi >= M_PI)  { psi -= 2.0 * M_PI; }
    while (psi < -M_PI)  { psi += 2.0 * M_PI; }
    headings.push_back(psi);
  }
  return headings;
}

// predict_own_positions() — constant-heading, constant-surge dead-reckoning.
// sway (v_mps) is not used here; BC-MPC short-horizon NED motion is dominated
// by surge. Phase E2 may add sway coupling via vessel_dynamics_model.
std::vector<Eigen::Vector2d> BcMpcBranchFormulation::predict_own_positions(
    double x0_m, double y0_m, double psi_rad, double u_mps) const
{
  const std::int32_t N = num_steps_();
  std::vector<Eigen::Vector2d> pos;
  pos.reserve(static_cast<std::size_t>(N + 1));
  pos.emplace_back(x0_m, y0_m);

  const double dx = u_mps * cfg_.dt_s * std::cos(psi_rad);
  const double dy = u_mps * cfg_.dt_s * std::sin(psi_rad);

  for (std::int32_t k = 1; k <= N; ++k) {
    pos.emplace_back(
        pos.back().x() + dx,
        pos.back().y() + dy);
  }
  return pos;
}

// predict_target_positions() — constant COG/SOG dead-reckoning for one target.
// Mirrors predict_own_positions structure so both vectors align index-by-index
// before being passed to CpaCalculator::compute_trajectory.
std::vector<Eigen::Vector2d> BcMpcBranchFormulation::predict_target_positions(
    const TargetState& target) const
{
  const std::int32_t N = num_steps_();
  std::vector<Eigen::Vector2d> pos;
  pos.reserve(static_cast<std::size_t>(N + 1));
  pos.emplace_back(target.x_m, target.y_m);

  const double dx = target.sog_mps * cfg_.dt_s * std::cos(target.cog_rad);
  const double dy = target.sog_mps * cfg_.dt_s * std::sin(target.cog_rad);

  for (std::int32_t k = 1; k <= N; ++k) {
    pos.emplace_back(
        pos.back().x() + dx,
        pos.back().y() + dy);
  }
  return pos;
}

}  // namespace mass_l3::m5::bc_mpc
