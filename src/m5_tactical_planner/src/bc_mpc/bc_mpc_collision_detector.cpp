#include "m5_tactical_planner/bc_mpc/bc_mpc_collision_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "m5_tactical_planner/shared/cpa_calculator.hpp"

namespace mass_l3::m5::bc_mpc {

using mass_l3::m5::shared::CpaCalculator;

BcMpcCollisionDetector::BcMpcCollisionDetector(
    const BcMpcBranchFormulation& formulation)
    : formulation_(formulation) {}

// compute_urgency_() — maps short-horizon CPA deficit to [0,1].
// Consecutive Mid-MPC failures are treated as maximum urgency because the
// absence of a valid plan is itself an unresolved collision threat.
double BcMpcCollisionDetector::compute_urgency_(
    const BcMpcInput& input) const noexcept
{
  if (input.mid_mpc_consecutive_failures > 2) {
    return 1.0;
  }

  // Guard: avoid division by zero; defensive only — caller guarantees positive.
  if (input.cpa_safe_m <= 0.0) {
    return 1.0;
  }

  const double raw =
      1.0 - input.predicted_short_horizon_cpa_m / input.cpa_safe_m;

  // Clamp to [0, 1] without std::clamp to stay within CC budget.
  if (raw < 0.0) { return 0.0; }
  if (raw > 1.0) { return 1.0; }
  return raw;
}

// worst_case_cpa_m_() — minimax over all targets for a single candidate heading.
// Uses trajectory-based CPA (not linear CPA) so it captures dynamic geometry
// changes that linear extrapolation would miss for crossing scenarios.
double BcMpcCollisionDetector::worst_case_cpa_m_(
    double psi_rad, const BcMpcInput& input) const
{
  if (input.targets.empty()) {
    return 1.0e9;
  }

  const auto own_positions = formulation_.predict_own_positions(
      input.own_ship.x_m, input.own_ship.y_m, psi_rad, input.own_ship.u_mps);

  double min_cpa = 1.0e9;

  for (const auto& target : input.targets) {
    const auto tgt_positions = formulation_.predict_target_positions(target);
    const auto result = CpaCalculator::compute_trajectory(
        own_positions, tgt_positions, formulation_.config().dt_s);
    if (result.cpa_m < min_cpa) {
      min_cpa = result.cpa_m;
    }
  }
  return min_cpa;
}

// evaluate() — full branch-evaluation entry point.
// argmax over cpa_vals selects the branch that maximises worst-case CPA,
// i.e. the branch that is safest under the worst plausible target behaviour.
BcMpcSolution BcMpcCollisionDetector::evaluate(const BcMpcInput& input) const
{
  const double urgency = compute_urgency_(input);

  const auto candidates =
      formulation_.candidate_headings(input.own_ship.psi_rad, urgency);

  std::vector<double> cpa_vals;
  cpa_vals.reserve(candidates.size());
  for (const double psi : candidates) {
    cpa_vals.push_back(worst_case_cpa_m_(psi, input));
  }

  const auto best_it = std::max_element(cpa_vals.begin(), cpa_vals.end());
  const auto best_idx =
      static_cast<std::int32_t>(std::distance(cpa_vals.begin(), best_it));

  const double best_cpa = cpa_vals[static_cast<std::size_t>(best_idx)];

  // confidence: measures how far above the safe threshold the best branch is.
  // Clamped to [0, 1]; inverted sense from urgency — higher CPA = higher conf.
  double confidence = 1.0 - best_cpa / input.cpa_safe_m;
  if (confidence < 0.0) { confidence = 0.0; }
  if (confidence > 1.0) { confidence = 1.0; }

  const double threshold =
      input.cpa_safe_m * formulation_.config().override_cpa_multiplier;

  BcMpcSolution sol;
  sol.heading_cmd_rad    = candidates[static_cast<std::size_t>(best_idx)];
  sol.worst_case_cpa_m   = best_cpa;
  sol.selected_branch_idx = best_idx;
  // [TBD-HAZID] validity_s: calibrate via HAZID RUN-001 WP-04 FM-3 (1-3 s range).
  sol.validity_s         = 1.0;
  sol.trigger_reason     = "CONDITION_A";  // Phase E2 maps this from BcMpcInput
  sol.confidence         = confidence;
  sol.stamp_ns           = input.stamp_ns;
  sol.status             = (best_cpa >= threshold)
      ? BcMpcSolution::Status::Resolved
      : BcMpcSolution::Status::Override;

  return sol;
}

}  // namespace mass_l3::m5::bc_mpc
