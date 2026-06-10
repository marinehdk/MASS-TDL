#ifndef MASS_L3_M5_COMMON_TYPES_HPP_
#define MASS_L3_M5_COMMON_TYPES_HPP_

// M5 Tactical Planner — Internal shared types
// PATH-D (MISRA C++:2023): <cstdint>, no float, no bare new/delete.
//
// All parameters marked [TBD-HAZID] must be calibrated during HAZID RUN-001
// (FCB sea trials, target completion 2026-08-19 per docs/Design/HAZID/RUN-001-kickoff.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
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
// Polygon2D: 2D convex/non-convex polygon (ENC/TSS zone boundary).
// Vertices listed counter-clockwise; closes automatically (last→first implied).
// ---------------------------------------------------------------------------
using Polygon2D = std::vector<Eigen::Vector2d>;

// ---------------------------------------------------------------------------
// ZoneConstraint: ENC or TSS zone that own-ship must stay inside (or outside).
// ---------------------------------------------------------------------------
struct ZoneConstraint {
  Polygon2D polygon;
  bool must_stay_inside{true};  // true = stay inside (TSS lane); false = avoid
  std::string name;             // for active-set logging
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

  // Current own-ship heading [rad] — used by COLREGs directional constraints
  // (Rule 14/15/16/17) as the reference initial heading psi_0.
  double own_ship_psi_rad{0.0};

  // ENC / TSS zone constraints (stay-inside lanes or avoid zones).
  std::vector<ZoneConstraint> zone_constraints;
};

// ---------------------------------------------------------------------------
// MidMpcInput — assembled runtime input for one Mid-MPC solve cycle
// Assembled in M5Node from latest upstream messages.
// ---------------------------------------------------------------------------
enum class ColregsPreferredDirection : std::uint8_t {
  Hold = 0u,
  Starboard = 1u,
  Port = 2u,
  ReduceSpeed = 3u,
};

inline ColregsPreferredDirection parse_colregs_preferred_direction(const std::string& direction) {
  if (direction == "STARBOARD") {
    return ColregsPreferredDirection::Starboard;
  }
  if (direction == "PORT") {
    return ColregsPreferredDirection::Port;
  }
  if (direction == "REDUCE_SPEED") {
    return ColregsPreferredDirection::ReduceSpeed;
  }
  return ColregsPreferredDirection::Hold;
}

struct MidMpcInput {
  TrajectoryPoint own_ship;               // current own-ship state
  std::vector<TargetState> targets;       // max 16 per spec §4.2
  ConstraintInputs constraints;
  double planned_route_bearing_rad{0.0};  // current route leg bearing [rad]

  bool colregs_conflict_active{false};
  ColregsPreferredDirection colregs_preferred_direction{ColregsPreferredDirection::Hold};
  double colregs_min_alteration_rad{0.0};

  // [TBD-HAZID] planned_speed_mps: from L2 SpeedProfile; default 5.0 m/s ≈ 9.7 kn.
  // Calibrate per vessel service speed profile.
  double planned_speed_mps{5.0};

  /// D3.2: dynamic ROT max [rad/s] from VesselDynamicsModel (replaces D0.1 hardcoded stub)
  double rot_max_rad_s{0.2094};

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

// ---------------------------------------------------------------------------
// BcMpcSolution — result from one BC-MPC evaluation cycle
// ---------------------------------------------------------------------------
struct BcMpcSolution {
  enum class Status : std::uint8_t {
    Override       = 0u,   // heading command issued; L4 must track
    Resolved       = 1u,   // CPA restored to safe; revert to Mid-MPC
    NotInitialized = 2u,
  };

  Status status{Status::NotInitialized};
  double heading_cmd_rad{0.0};    // optimal branch heading [rad]
  double worst_case_cpa_m{0.0};  // worst-case CPA of selected branch [m]
  std::int32_t selected_branch_idx{0};
  // [TBD-HAZID] validity_s: calibrate via HAZID RUN-001 WP-04 FM-3 (1-3 s range).
  double validity_s{1.0};         // override validity [s]
  std::string trigger_reason;     // "CONDITION_A".."CONDITION_D"
  double confidence{0.0};         // ∈ [0, 1]
  double optimal_speed_mps{0.0};  // Phase E1: maintain current speed; Phase E2: optimize
  double rot_cmd_rad_s{0.0};      // Phase E1: 0.0 (no ROT cmd); Phase E2: from ROT solver
  std::int64_t solve_duration_us{0};  // solve wall-clock time [microseconds]
  std::int64_t stamp_ns{0};
};

inline double normalize_heading_positive(double angle) {
  const double two_pi = 2.0 * M_PI;
  double normalized = std::fmod(angle, two_pi);
  if (normalized < 0.0) {
    normalized += two_pi;
  }
  return normalized;
}

inline double circular_heading_distance(double lhs, double rhs) {
  const double two_pi = 2.0 * M_PI;
  double diff = std::fabs(normalize_heading_positive(lhs) - normalize_heading_positive(rhs));
  if (diff > M_PI) {
    diff = two_pi - diff;
  }
  return diff;
}

inline bool heading_inside_window(double target, double h_min, double h_max) {
  const double window_span = std::fabs(h_max - h_min);
  if (window_span >= (2.0 * M_PI - 1e-9)) {
    return true;
  }

  const double target_norm = normalize_heading_positive(target);
  const double min_norm = normalize_heading_positive(h_min);
  const double max_norm = normalize_heading_positive(h_max);
  if (min_norm <= max_norm) {
    return target_norm >= min_norm && target_norm <= max_norm;
  }
  return target_norm >= min_norm || target_norm <= max_norm;
}

inline double clamp_heading_window(double target, double h_min, double h_max) {
  if (heading_inside_window(target, h_min, h_max)) {
    return target;
  }
  const double min_distance = circular_heading_distance(target, h_min);
  const double max_distance = circular_heading_distance(target, h_max);
  return (min_distance <= max_distance) ? h_min : h_max;
}

inline bool is_m4_fallback_rationale(const std::string& rationale) {
  return rationale.find("infeasible fallback") != std::string::npos
      || rationale.find("Failsafe") != std::string::npos
      || rationale.find("geometric fallback") != std::string::npos;
}

inline double fallback_target_heading(
    double route_brg,
    double h_min,
    double h_max,
    double min_alt_rad,
    ColregsPreferredDirection direction) {
  double target = route_brg;
  if (direction == ColregsPreferredDirection::Starboard) {
    target = route_brg + min_alt_rad;
  } else if (direction == ColregsPreferredDirection::Port) {
    target = route_brg - min_alt_rad;
  }
  return clamp_heading_window(target, h_min, h_max);
}

inline double fallback_target_heading(
    double route_brg, double h_min, double h_max, double min_alt_rad) {
  return fallback_target_heading(
      route_brg, h_min, h_max, min_alt_rad,
      ColregsPreferredDirection::Starboard);
}

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_TYPES_HPP_
