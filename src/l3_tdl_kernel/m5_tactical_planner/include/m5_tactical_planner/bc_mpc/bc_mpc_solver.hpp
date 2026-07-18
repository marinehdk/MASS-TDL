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
  // P7: BC-MPC speed optimization config (spec §4.6).
  struct Config {
    // CPA trigger ratio: when worst_case_cpa < cpa_safe * decel_trigger_ratio,
    // the solver applies deceleration.
    double decel_trigger_ratio;
    // Deceleration factor: optimal_speed = current_speed * decel_factor.
    double decel_factor;
  };

  // Factory for default config (avoids NSDMI issue in GCC 11 with nested struct
  // default argument).
  [[nodiscard]] static Config default_config() noexcept {
    return Config{0.7, 0.5};
  }

  explicit BcMpcSolver(const BcMpcBranchFormulation& formulation,
                       Config cfg = default_config());

  // Solve one BC-MPC cycle: evaluate branches, return BcMpcSolution.
  // P7 Phase E2: decelerates when Override + CPA below threshold.
  // Sets solve_duration_us from wall-clock timing.
  [[nodiscard]] BcMpcSolution solve(const BcMpcInput& input);

  [[nodiscard]] int64_t consecutive_failures() const noexcept {
    return consecutive_failures_;
  }

 private:
  BcMpcCollisionDetector detector_;
  Config cfg_;
  int64_t consecutive_failures_{0};
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_SOLVER_HPP_
