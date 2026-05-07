#ifndef MASS_L3_M5_COMMON_TYPES_HPP_
#define MASS_L3_M5_COMMON_TYPES_HPP_

// M5 Tactical Planner — Internal shared types
// PATH-D (MISRA C++:2023): <cstdint>, no float, no bare new/delete.
//
// All parameters marked [TBD-HAZID] must be calibrated during HAZID RUN-001
// (FCB sea trials, target completion 2026-08-19 per docs/Design/HAZID/RUN-001-kickoff.md).

#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

// Eigen 3 — column-major Dense matrices; NO_MODULE ensures modern CMake target.
#include <Eigen/Dense>

namespace mass_l3::m5 {

// ---------------------------------------------------------------------------
// TrajectoryPoint — single MPC trajectory sample
// Represents the 6-DOF state of own ship at a discrete time step.
// Used by both Mid-MPC (N-step solution) and BC-MPC (short-horizon check).
// ---------------------------------------------------------------------------
struct TrajectoryPoint {
  double x_m{0.0};      // NED north position [m]
  double y_m{0.0};      // NED east position [m]
  double psi_rad{0.0};  // heading [rad], 0 = north, positive clockwise
  double u_mps{0.0};    // surge (forward) speed [m/s]
  double v_mps{0.0};    // sway (lateral) speed [m/s]
  double r_rad_s{0.0};  // yaw rate [rad/s], positive = turn to starboard
  double t_s{0.0};      // time offset from cycle start [s]
};

// ---------------------------------------------------------------------------
// TargetState — tracked obstacle state, sourced from M2 WorldState
// ---------------------------------------------------------------------------
struct TargetState {
  // Intent classification (§5.3.1 of M5 detailed design)
  enum class Intent : std::uint8_t {
    Unknown       = 0u,
    Maintain      = 1u,
    TurnPort      = 2u,
    TurnStarboard = 3u,
    Decelerate    = 4u,
  };

  std::int32_t id{0};
  double x_m{0.0};
  double y_m{0.0};
  double cog_rad{0.0};   // course over ground [rad]
  double sog_mps{0.0};   // speed over ground [m/s]
  double cpa_m{0.0};     // closest point of approach [m]
  double tcpa_s{0.0};    // time to CPA [s]; negative = already passed
  double confidence{0.0};  // track confidence ∈ [0, 1]
  Intent predicted_intent{Intent::Unknown};
};

// ---------------------------------------------------------------------------
// ConstraintInputs — compiled constraint context passed to ConstraintCompiler
// All values sourced from upstream M1/M4/M6 messages; no vessel constants here.
// ---------------------------------------------------------------------------
struct ConstraintInputs {
  // [TBD-HAZID] cpa_safe_m: from M1 ODD_StateMsg; default 1 NM = 1852 m.
  // Calibrate via HAZID RUN-001 workpackage 03 (SOTIF thresholds).
  double cpa_safe_m{1852.0};

  std::vector<TargetState> targets;

  // COLREGs rule set received from M6 COLREGsConstraint.
  // Values are rule numbers per COLREG 1972 (e.g., 14, 15, 16, 17).
  std::vector<std::uint8_t> applicable_rules;

  // Behavior bounds from M4 BehaviorPlan — set by Behavior Arbiter.
  double heading_min_rad{-M_PI};
  double heading_max_rad{M_PI};

  // [TBD-HAZID] speed_max_mps: from M1 ODD speed_limit_kn field.
  // Default 15 m/s ≈ 29 kn; calibrate per ODD domain (coastal vs. open sea).
  double speed_min_mps{0.0};
  double speed_max_mps{15.0};
};

// ---------------------------------------------------------------------------
// MidMpcInput — assembled runtime input for one Mid-MPC solve cycle
// Assembled in M5Node from latest upstream messages.
// ---------------------------------------------------------------------------
struct MidMpcInput {
  TrajectoryPoint own_ship;               // current own-ship state
  std::vector<TargetState> targets;       // max 16 per spec §4.2
  ConstraintInputs constraints;
  double planned_route_bearing_rad{0.0};  // current route leg bearing [rad]

  // [TBD-HAZID] planned_speed_mps: from L2 SpeedProfile; default 5.0 m/s ≈ 9.7 kn.
  // Calibrate per vessel service speed profile.
  double planned_speed_mps{5.0};

  std::int64_t stamp_ns{0};  // cycle start [nanoseconds since epoch]
};

// ---------------------------------------------------------------------------
// MidMpcSolution — result from one Mid-MPC solve cycle
// ---------------------------------------------------------------------------
struct MidMpcSolution {
  enum class Status : std::uint8_t {
    Converged       = 0u,
    Timeout         = 1u,
    Infeasible      = 2u,
    NumericalFailure = 3u,
    NotInitialized  = 4u,
  };

  Status status{Status::NotInitialized};
  std::vector<TrajectoryPoint> trajectory;  // N-point solution (horizon)
  double cost_total{0.0};
  double cost_colreg{0.0};
  double cost_dist{0.0};
  double cost_vel{0.0};
  std::int32_t solve_duration_ms{0};
  std::int32_t ipopt_iterations{0};
  std::int64_t stamp_ns{0};
};

// ---------------------------------------------------------------------------
// BcMpcInput — assembled input for one BC-MPC evaluation (short-horizon)
// ---------------------------------------------------------------------------
struct BcMpcInput {
  TrajectoryPoint own_ship;
  std::vector<TargetState> targets;

  // [TBD-HAZID] cpa_safe_m: same calibration as ConstraintInputs::cpa_safe_m.
  double cpa_safe_m{1852.0};

  // Consecutive Mid-MPC failure count — triggers BC-MPC escalation.
  std::int32_t mid_mpc_consecutive_failures{0};

  // Pre-computed short-horizon CPA from MidMpcSolution step k=2.
  // Used as trigger threshold for BC-MPC activation.
  double predicted_short_horizon_cpa_m{1.0e6};

  std::int64_t stamp_ns{0};
};

// ---------------------------------------------------------------------------
// TargetIntent — alias for TrajectoryPropagator API clarity
// ---------------------------------------------------------------------------
using TargetIntent = TargetState::Intent;

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_TYPES_HPP_
