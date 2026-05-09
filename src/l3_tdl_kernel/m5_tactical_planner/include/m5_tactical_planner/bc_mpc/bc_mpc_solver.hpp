#ifndef MASS_L3_M5_BC_MPC_SOLVER_HPP_
#define MASS_L3_M5_BC_MPC_SOLVER_HPP_

// M5 Tactical Planner — BC-MPC Solver
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Wraps BcMpcCollisionDetector with wall-clock timing, speed passthrough,
// and a consecutive-failure counter for health monitoring.

#include <cstdint>

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_collision_detector.hpp"
#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcSolver {
 public:
  explicit BcMpcSolver(const BcMpcBranchFormulation& formulation);

  // Solve one BC-MPC cycle: evaluate branches, return BcMpcSolution.
  // Sets optimal_speed_mps = input.own_ship.u_mps (Phase E1 speed hold).
  // Sets solve_duration_us from wall-clock timing.
  [[nodiscard]] BcMpcSolution solve(const BcMpcInput& input);

  [[nodiscard]] int64_t consecutive_failures() const noexcept {
    return consecutive_failures_;
  }

 private:
  BcMpcCollisionDetector detector_;
  int64_t consecutive_failures_{0};
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_SOLVER_HPP_
