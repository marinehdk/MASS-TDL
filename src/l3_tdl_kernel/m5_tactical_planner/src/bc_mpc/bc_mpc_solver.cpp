#include "m5_tactical_planner/bc_mpc/bc_mpc_solver.hpp"

#include <chrono>
#include <cstdint>

namespace mass_l3::m5::bc_mpc {

BcMpcSolver::BcMpcSolver(const BcMpcBranchFormulation& formulation,
                         Config cfg)
    : detector_(formulation), cfg_(cfg) {}

// solve() — evaluate branches and record timing.
// P7 Phase E2 speed optimization (spec §4.6): when BC-MPC issues Override
// and the worst-case CPA is below the trigger threshold, reduce speed.
// Resolved or high-CPA override: maintain current speed.
// Consecutive-failure counter: only NotInitialized (detector internal error)
// counts as a failure; Override and Resolved are both valid outcomes.
BcMpcSolution BcMpcSolver::solve(const BcMpcInput& input)
{
  const auto t_start = std::chrono::steady_clock::now();

  BcMpcSolution sol = detector_.evaluate(input);

  const auto t_end = std::chrono::steady_clock::now();

  // P7: acceleration optimization (spec §4.6).
  // Decelerate when Override + CPA below threshold; otherwise hold speed.
  if (sol.status == BcMpcSolution::Status::Override) {
    const double cpa_threshold = input.cpa_safe_m * cfg_.decel_trigger_ratio;
    if (sol.worst_case_cpa_m < cpa_threshold) {
      sol.optimal_speed_mps = input.own_ship.u_mps * cfg_.decel_factor;
      sol.trigger_reason = "CONDITION_A_DECEL";
    } else {
      sol.optimal_speed_mps = input.own_ship.u_mps;
    }
  } else {
    sol.optimal_speed_mps = input.own_ship.u_mps;
  }

  sol.rot_cmd_rad_s      = 0.0;
  sol.solve_duration_us  =
      std::chrono::duration_cast<std::chrono::microseconds>(t_end - t_start)
          .count();

  if (sol.status == BcMpcSolution::Status::NotInitialized) {
    ++consecutive_failures_;
  } else {
    consecutive_failures_ = 0;
  }

  return sol;
}

}  // namespace mass_l3::m5::bc_mpc
