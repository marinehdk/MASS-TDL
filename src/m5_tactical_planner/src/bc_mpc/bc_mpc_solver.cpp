#include "m5_tactical_planner/bc_mpc/bc_mpc_solver.hpp"

#include <chrono>
#include <cstdint>

namespace mass_l3::m5::bc_mpc {

BcMpcSolver::BcMpcSolver(const BcMpcBranchFormulation& formulation)
    : detector_(formulation) {}

// solve() — evaluate branches and record timing.
// Speed hold (Phase E1): optimal_speed_mps copies input so L4 tracks the
// current speed unchanged; no speed optimisation is attempted yet.
// Consecutive-failure counter: only NotInitialized (detector internal error)
// counts as a failure; Override and Resolved are both valid outcomes.
BcMpcSolution BcMpcSolver::solve(const BcMpcInput& input)
{
  const auto t_start = std::chrono::steady_clock::now();

  BcMpcSolution sol = detector_.evaluate(input);

  const auto t_end = std::chrono::steady_clock::now();

  sol.optimal_speed_mps  = input.own_ship.u_mps;
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
