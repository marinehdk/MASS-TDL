#ifndef MASS_L3_M5_BC_MPC_BRANCH_FORMULATION_HPP_
#define MASS_L3_M5_BC_MPC_BRANCH_FORMULATION_HPP_

// M5 Tactical Planner — BC-MPC Branch Formulation
// PATH-D (MISRA C++:2023): [[nodiscard]], no float, no bare new/delete.
//
// Computes candidate heading branches and trajectory predictions for the
// minimax CPA evaluation in BcMpcCollisionDetector. Pure-math, no ROS2.
//
// NED convention throughout: x = north, y = east.
// Heading psi = 0 → north, positive clockwise.
// vx = u * cos(psi),  vy = u * sin(psi)

#include <cstdint>
#include <vector>

#include <Eigen/Dense>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::bc_mpc {

class BcMpcBranchFormulation {
 public:
  struct Config {
    // [TBD-HAZID] k_high/k_low: number of heading candidates. Calibrate via
    // HAZID RUN-001 WP-04 (BC-MPC coverage vs. latency tradeoff).
    std::int32_t k_high{7};   // branches when urgency > urgency_threshold
    std::int32_t k_low{5};    // branches when urgency <= urgency_threshold

    // [TBD-HAZID] delta_psi_rad: 10 deg step. Calibrate per FCB turning dynamics.
    double delta_psi_rad{10.0 * mass_l3::m5::units::kRadPerDeg};  // 10 deg in radians

    // [TBD-HAZID] Prediction step and horizon. Calibrate via HAZID RUN-001 WP-04.
    double dt_s{2.0};
    double horizon_s{20.0};

    // [TBD-HAZID] urgency_threshold [0,1] separating k_low from k_high branches.
    double urgency_threshold{0.8};

    // [TBD-HAZID] override_cpa_multiplier: OVERRIDE if worst_cpa < cpa_safe * this.
    double override_cpa_multiplier{0.8};
  };

  explicit BcMpcBranchFormulation(const Config& cfg = Config{});

  // Return k candidate headings centred on current_heading_rad.
  // k = k_high if urgency_level > urgency_threshold, else k_low.
  // Offsets: (i - k/2) * delta_psi for i in [0, k).
  [[nodiscard]] std::vector<double> candidate_headings(
      double current_heading_rad, double urgency_level) const;

  // Predict own-ship NED positions for N = round(horizon_s/dt_s) steps at
  // constant heading psi_rad, surge speed u_mps. Returns N+1 points, index 0
  // is the initial position passed in.
  [[nodiscard]] std::vector<Eigen::Vector2d> predict_own_positions(
      double x0_m, double y0_m, double psi_rad, double u_mps) const;

  // Predict target NED positions for the same N+1 steps (constant COG/SOG).
  [[nodiscard]] std::vector<Eigen::Vector2d> predict_target_positions(
      const TargetState& target) const;

  [[nodiscard]] const Config& config() const noexcept { return cfg_; }

 private:
  Config cfg_;

  // Number of prediction steps; derived from config at construction time so
  // both predict_own/target_positions stay in sync with a single source.
  [[nodiscard]] std::int32_t num_steps_() const noexcept;
};

}  // namespace mass_l3::m5::bc_mpc

#endif  // MASS_L3_M5_BC_MPC_BRANCH_FORMULATION_HPP_
