#ifndef MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
#define MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_

// M5 Tactical Planner — Mid-MPC NLP Formulation (Task 2.1)
// Builds the parametric CasADi MX symbolic graph for the Mid-MPC NLP.
//
// Construction-time work: build_symbolic_graph() instantiates an nlpsol
// IPOPT Function once. MidMpcSolver (Task 2.2) re-uses this Function each
// 1 Hz cycle, only re-packing the parameter vector p ∈ R^93.
//
// Decision variables: x = [psi[0..N-1]; u[0..N-1]] ∈ R^{2N}
// Parameters:         p ∈ R^93 (initial state + bounds + 16 targets)
// Objective:          J = w_colreg * J_colreg + w_dist * J_dist + w_vel * J_vel
// Constraints:        g(x, p) >= 0, dim = 5N - 1
//
// Phase E1 scope:
//   - Soft COLREGs cost (J_colreg) only; hard rule constraints (Rule 14/15/16/17)
//     deferred to Phase E2 once parametric encounter geometry is available.
//   - Heading/speed/ROT bounds compiled as parametric constraints from p.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float, no
// bare new/delete. CasADi LGPL-3.0: internal MISRA violations exempted per
// coding-standards.md §10 (dynamic-link boundary).
//
// Parameters marked [TBD-HAZID] must be calibrated during HAZID RUN-001
// (FCB sea trials, target completion 2026-08-19 per
// docs/Design/HAZID/RUN-001-kickoff.md).

#include <casadi/casadi.hpp>
#include <cstdint>

#include "m5_tactical_planner/common/types.hpp"

namespace mass_l3::m5::mid_mpc {

// ---------------------------------------------------------------------------
// Parameter vector layout — public constants for caller readability.
// kParamDim is fixed at compile time (16 targets × 5 slots + 13 head slots).
// ---------------------------------------------------------------------------
constexpr int32_t kIdxPsi0           = 0;
constexpr int32_t kIdxU0             = 1;
constexpr int32_t kIdxX0             = 2;
constexpr int32_t kIdxY0             = 3;
constexpr int32_t kIdxRouteBearing   = 4;
constexpr int32_t kIdxPlannedSpeed   = 5;
constexpr int32_t kIdxHeadingMin     = 6;
constexpr int32_t kIdxHeadingMax     = 7;
constexpr int32_t kIdxSpeedMin       = 8;
constexpr int32_t kIdxSpeedMax       = 9;
constexpr int32_t kIdxCpaSafe        = 10;
constexpr int32_t kIdxRotMax         = 11;
constexpr int32_t kIdxOwnPsi         = 12;
constexpr int32_t kIdxTargets        = 13;
constexpr int32_t kTargetStride      = 5;
constexpr int32_t kMaxTargets        = 16;
constexpr int32_t kParamDim          = kIdxTargets + kMaxTargets * kTargetStride;  // 93

class MidMpcNlpFormulation {
 public:
  // -------------------------------------------------------------------------
  // Config — Mid-MPC NLP formulation hyperparameters.
  // All [TBD-HAZID] tunables. Defaults aligned with detailed design §5.2.3.
  // -------------------------------------------------------------------------
  struct Config {
    // [TBD-HAZID] Horizon length N (12–18); calibrate from FCB stopping distance.
    int32_t n_horizon{12};
    // Step duration [s] (aligned with L4 LOS period, detailed design §5.2.3).
    double dt_s{5.0};
    // [TBD-HAZID] COLREGs compliance cost weight (highest priority).
    double w_colreg{1000.0};
    // [TBD-HAZID] Route-track deviation cost weight.
    double w_dist{10.0};
    // [TBD-HAZID] Speed efficiency cost weight.
    double w_vel{1.0};
    // Max obstacle count per cycle (parametric upper bound, must be ≤ kMaxTargets).
    int32_t max_targets{kMaxTargets};
  };

  explicit MidMpcNlpFormulation(const Config& cfg);

  // Build symbolic NLP graph (call once after construction). Caches nlpsol Function.
  void build_symbolic_graph();

  // Cached nlpsol Function (called by MidMpcSolver per cycle).
  [[nodiscard]] const casadi::Function& solver() const noexcept { return solver_; }

  // Pack MidMpcInput into IPOPT parameter vector p ∈ R^kParamDim.
  [[nodiscard]] casadi::DM pack_parameters(const MidMpcInput& input) const;

  // Unpack IPOPT solution x* + solver stats into MidMpcSolution.
  [[nodiscard]] MidMpcSolution unpack_solution(
      const casadi::DM& x_opt,
      const casadi::Dict& stats) const;

  // Constraint dimension (used by MidMpcSolver for lbg/ubg sizing). 5N - 1.
  [[nodiscard]] int32_t g_dim() const noexcept;

  // Public access to active config (read-only).
  [[nodiscard]] const Config& config() const noexcept { return cfg_; }

 private:
  Config cfg_;
  casadi::MX psi_;     // [N×1] symbolic decision variable: heading sequence [rad]
  casadi::MX u_;       // [N×1] symbolic decision variable: speed sequence [m/s]
  casadi::MX p_;       // [kParamDim×1] parameter vector
  casadi::MX J_;       // objective expression
  casadi::MX g_;       // constraint vector (g >= 0 convention)
  casadi::Function solver_;  // nlpsol-cached IPOPT Function

  // Dimension helper (kept for symmetry with CasADi MX::sym signature).
  [[nodiscard]] static int32_t parameter_dim_() noexcept { return kParamDim; }

  // Objective sub-terms (called from build_symbolic_graph).
  [[nodiscard]] casadi::MX build_colreg_cost_() const;
  [[nodiscard]] casadi::MX build_distance_cost_() const;
  [[nodiscard]] casadi::MX build_velocity_cost_() const;

  // Constraint helper.
  [[nodiscard]] casadi::MX build_constraints_() const;
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_NLP_FORMULATION_HPP_
