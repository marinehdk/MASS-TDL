#ifndef MASS_L3_M5_BC_MPC_COLLISION_DETECTOR_HPP_
#define MASS_L3_M5_BC_MPC_COLLISION_DETECTOR_HPP_

// M5 Tactical Planner — BC-MPC Collision Detector
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Evaluates all candidate branches produced by BcMpcBranchFormulation, selects
// the branch with maximum worst-case CPA over all targets (minimax), and
// returns a BcMpcSolution. Holds a const-reference to the formulation to avoid
// ownership / lifetime complexity at the evaluation hot-path.

#include <cstdint>
#include <vector>

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcCollisionDetector {
 public:
  explicit BcMpcCollisionDetector(const BcMpcBranchFormulation& formulation);

  // Evaluate all candidate branches; return the safest heading as BcMpcSolution.
  // Status::Override → worst_case_cpa_m < cpa_safe * override_cpa_multiplier
  // Status::Resolved → worst_case_cpa_m ≥ cpa_safe * override_cpa_multiplier
  [[nodiscard]] BcMpcSolution evaluate(const BcMpcInput& input) const;

  [[nodiscard]] const BcMpcBranchFormulation& formulation() const noexcept {
    return formulation_;
  }

 private:
  const BcMpcBranchFormulation& formulation_;

  // Compute urgency ∈ [0, 1] from short-horizon CPA and consecutive failures.
  [[nodiscard]] double compute_urgency_(const BcMpcInput& input) const noexcept;

  // Worst-case CPA [m] for candidate heading psi_rad over all input targets.
  // Returns 1e9 when input.targets is empty (trivially safe).
  [[nodiscard]] double worst_case_cpa_m_(
      double psi_rad, const BcMpcInput& input) const;
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_COLLISION_DETECTOR_HPP_
