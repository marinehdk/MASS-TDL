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
#include <utility>
#include <vector>

// Eigen 3 — column-major Dense matrices; NO_MODULE ensures modern CMake target.
#include <Eigen/Dense>

#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/tail_builder/tail_builder.hpp"  // v3.1 B6: EncounterState enum reuse
#include "l3_msgs/msg/avoidance_plan.hpp"

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

// Dead-reckon trajectory x_m/y_m from a heading/speed sequence.
// point[k] position = start + Σ_{j<k} u[j]·(cos(psi[j]), sin(psi[j]))·dt.
// psi_rad and u_mps must already be set on each point; x_m/y_m are overwritten.
//
// The Mid-MPC NLP decision variable is x=[psi; u] (no position state), so
// without this reconstruction the solved trajectory carries x_m=y_m=0 and every
// position-based acceptance gate (tail-gate terminal lateral offset) sees 0 —
// Bug B: every converged solution rejected as wrong_m6_side.
inline void propagate_trajectory_positions(std::vector<TrajectoryPoint>& traj,
                                            double dt_s,
                                            double x0_m = 0.0,
                                            double y0_m = 0.0) {
  double x = x0_m;
  double y = y0_m;
  for (auto& p : traj) {
    p.x_m = x;
    p.y_m = y;
    x += p.u_mps * std::cos(p.psi_rad) * dt_s;
    y += p.u_mps * std::sin(p.psi_rad) * dt_s;
  }
}

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
  double cpa_sigma_m{0.0};  // 1σ CPA uncertainty [m], sourced from M2 covariance when available
  double tcpa_s{0.0};    // time to CPA [s]; negative = already passed
  double confidence{0.0};  // track confidence ∈ [0, 1]
  Intent predicted_intent{Intent::Unknown};

  // P7: OU/Intent fields (Q7, spec §4.2)
  double intent_confidence{0.5};      // [0,1], M2 rule-based, def 0.5 for unknown
  double target_compliance{0.5};      // [0,1], M2 CPA/range trend, def 0.5 for unknown

  enum class Classification : std::uint8_t {
    Unknown     = 0u,
    Vessel      = 1u,
    FixedObject = 2u,
  };
  Classification classification{Classification::Unknown};
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

  // [TBD-HAZID] cpa_hard_m: the HARD CPA floor used by compile_cpa_distance.
  // Distinct from cpa_safe_m: the node bumps cpa_safe_m→2500 during conflict
  // for SOFT cost-scaling (the colreg barrier), but that bump must NOT leak
  // into the hard floor. Spec committed-route-design-v2 §L84: the hard floor
  // is odd_aware_thresholds.yaml cpa_hard_m (shared with M6/M2, =1852); M5 must
  // not self-define it. Before this field existed the hard floor tracked the
  // bumped cpa_safe (2500) → Infeasible whenever a target was inside 2500 m
  // (Bug C deep, RC-C).
  double cpa_hard_m{1852.0};

  // v2.1 §4.5: terminal lateral feasibility band (6th tail-gate check).
  // Mirrors MidMpcNlpFormulation::Config defaults (terminal_l_min/max_feasible_m).
  // Packed by mid_mpc_node from the formulation Config so accept_tail_gate
  // (which receives MidMpcInput only) can enforce the band the NLP softend.
  double terminal_l_min_feasible_m{30.0};
  double terminal_l_max_feasible_m{400.0};

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

  // v2.2 §4.6 reachability 合约（M4 publish via BehaviorPlan.msg schema 113,
  // M5 consume into MidMpcInput.constraints). 0 sentinel = M4 未升级 → M5 退化
  // v2.1 ROT-only 公式.
  double heading_box_reachable_from_psi0_deg{0.0};
  double rot_step_deg{0.0};
  double min_alt_required_rad{0.0};
  double earliest_min_alt_k{0.0};
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

struct TargetRiskSnapshot {
  std::string target_id;
  double risk_score{0.0};
  double warning_margin_m{0.0};
  double danger_margin_m{0.0};
  double tcpa_s{0.0};
  double closing_speed_mps{0.0};
  bool primary{false};
};

struct MidMpcInput {
  TrajectoryPoint own_ship;               // current own-ship state
  std::vector<TargetState> targets;       // max 16 per spec §4.2
  std::vector<TargetState> tail_gate_targets;  // Raw M2 target CPA/covariance before optimizer weighting
  ConstraintInputs constraints;
  double planned_route_bearing_rad{0.0};  // current route leg bearing [rad]
  double route_xte_m{0.0};
  double route_corridor_limit_m{500.0};
  std::vector<TargetRiskSnapshot> target_risks;

  // Slice R1: route-frame parameters (spec §4). Computed in assemble_input_
  // (active-leg bearing + normal + cross-leg guard), packed into kIdx slots.
  double route_frame_origin_x_m{0.0};            // active-leg origin, NED north
  double route_frame_origin_y_m{0.0};            // active-leg origin, NED east
  double route_frame_normal_x{0.0};              // active-leg normal unit vector, north comp
  double route_frame_normal_y{1.0};              // active-leg normal unit vector, east comp
  double route_frame_active_leg_bearing_rad{0.0};
  double lateral_scale_m{400.0};                 // GncExecutionOdd.max_lateral_offset_m
  // High-4 review fix: default 0.0 (inert) so legacy callers that do not build a
  // route frame do not silently enable J_route and change behaviour. assemble_input_
  // sets 1.0 only when a valid active leg + cross-leg guard passes. The cross-leg
  // guard still drives this to 0.0 when the trajectory would cross an L2 corner.
  double route_weight{0.0};                      // cross-leg guard: 1.0 active, 0.0 inert/cross-corner

  bool colregs_conflict_active{false};
  // M6-owned primary role: 0=STAND_ON, 1=GIVE_WAY, 2=BOTH_GIVE_WAY, 3=FREE/UNKNOWN.
  std::uint8_t colregs_primary_role{3U};
  ColregsPreferredDirection colregs_preferred_direction{ColregsPreferredDirection::Hold};
  double colregs_min_alteration_rad{0.0};

  // v3.1 acatos dispatch gate (memo 2026-07-19-m5-acados-dispatch-gate-v3-event-
  // based-design.md §2.4 B6): M6 encounter lifecycle consumed by
  // MidMpcSolver::compute_acatos_feasibility (C2 criterion). These are INTERNAL
  // MidMpcInput fields populated from the existing M6 COLREGsConstraint msg; they
  // do NOT change the ROS2 wire format (M6 already publishes these, M5 previously
  // ignored them).
  //
  // B6 rationale: reuse tail_builder::EncounterState (single source of truth) +
  // a bool flag to avoid sentinel-vs-RELEASE=3 collision (M7 F-I1). The enum
  // numeric values are DIFFERENT from M6 ENCOUNTER_* constants (M6: CLEAR=0,
  // ONSET=1, ACTIVE=2, RELEASE=3; tail_builder: Active=0, Release=1, Clear=2,
  // Onset=3), so mid_mpc_node::assemble_input_ MUST map by logical name via the
  // switch case in memo §2.4, never by raw numeric cast.
  tail_builder::EncounterState colregs_encounter_state{tail_builder::EncounterState::Clear};
  bool has_m6_encounter_state{false};  // false until first M6 msg received (fail-closed)
  std::string colregs_phase;           // "T_standOn" | "T_act" | "T_postAvoid" (M6 phase string)

  // [TBD-HAZID] planned_speed_mps: from L2 SpeedProfile; default 5.0 m/s ≈ 9.7 kn.
  // Calibrate per vessel service speed profile.
  double planned_speed_mps{5.0};
  double decel_max_mps2{0.08};
  bool speed_gap_infeasible{false};  // v2.2 §4.7: own_u/planned_u gap > N·decel_max·dt，dispatch 信号

  /// D3.2: dynamic ROT max [rad/s] from VesselDynamicsModel (replaces D0.1 hardcoded stub)
  double rot_max_rad_s{0.2094};

  // ── Slice C1: continuity H_commit prefix (spec §6) ─────────────────────────
  // The committed-route prefix is frozen WGS84 geometry (the GNC guard inner
  // waypoints). assemble_input_ reprojects that geometry to the current cycle's
  // ownship NED origin (spec §6.2) and back-infers the per-step psi/u that the
  // NLP equality rows pin. These fields hold the NED-reprojected values so the
  // formulation's pack_parameters can write them directly to kIdxPrefixPsi/U.
  // prefix_active_k=0 (default) ⇒ no prefix (K=0, first commit, full suffix free).
  int32_t prefix_active_k{0};
  std::vector<double> prefix_psi_rad;   // [K] NLP psi[k] equality targets (reprojected)
  std::vector<double> prefix_u_mps;     // [K] NLP u[k]   equality targets (reprojected)
  // Own-ship WGS84 position for the cycle (the reprojection origin). Packed so
  // the reprojection test can verify WGS84 continuity across two different
  // NED origins. Both own_lat/lon and own_n/own_e describe the same current
  // position; they are redundant representations kept for test clarity.
  double own_lat_deg{0.0};
  double own_lon_deg{0.0};

  std::int64_t stamp_ns{0};  // cycle start [nanoseconds since epoch]

  // ── L0 input degradation tracking (ARCH-DECISION-03, 2026-07-20) ──────────
  // When assemble_input_ detects an invalid/abnormal upstream value (NaN, Inf,
  // negative, out-of-range), it applies a fallback AND sets the corresponding
  // flag here. This lets L4/L5 reason about solution trustworthiness and lets
  // LX trace root cause (which upstream field went bad this cycle).
  // Design rule: NEVER silently substitute — always mark degraded so downstream
  // can distinguish "real input" from "fallback". See L0 grilling conclusion
  // (docs/superpowers/design-logs/2026-07-20-l0-grilling-conclusion.md §2 L0-A).
  struct InputDegradation {
    // Bitmask of degraded fields. Each bit = one upstream source went bad.
    // Using individual bools (not std::bitset) to keep the struct POD and
    // trivially serializable for diagnostic capture.
    bool own_psi_degraded{false};       // M2 own_ship heading NaN/Inf
    bool own_u_degraded{false};         // M2 own_ship sog negative/NaN
    bool target_degraded{false};        // M2 any target lat/lon/sog invalid
    bool speed_box_degraded{false};     // M4 speed_min/max invalid or max<min
    bool reachability_degraded{false};  // M4 rot_step/min_alt/earliest_k invalid
    bool planned_speed_degraded{false}; // L2 speed_profile invalid

    // Human-readable list of which fields were substituted (for rationale /
    // LX X1 snapshot). Cleared at the start of each assemble_input_ cycle.
    std::string summary() const;
    bool any() const {
      return own_psi_degraded || own_u_degraded || target_degraded
          || speed_box_degraded || reachability_degraded || planned_speed_degraded;
    }
    void reset() {
      own_psi_degraded = own_u_degraded = target_degraded = false;
      speed_box_degraded = reachability_degraded = planned_speed_degraded = false;
    }
  };
  InputDegradation degradation;
};

// v2.2 §4.7 (D2): speed-gap feasibility check. Free function so the dispatch
// rule is unit-testable without the private MidMpcNode::assemble_input_().
// Returns true when |own_u - planned_u| exceeds the reachable decel envelope
// decel_max·dt·N over the MPC horizon → BC-MPC take-over candidate (§13.1).
inline bool compute_speed_gap_infeasible(
    double own_u_mps, double planned_u_mps,
    double decel_max_mps2, int n_horizon, double dt_s) {
  const double max_delta =
      decel_max_mps2 * dt_s * static_cast<double>(n_horizon);
  return std::fabs(own_u_mps - planned_u_mps) > max_delta;
}

// v2.2 §13.1: BC-MPC take-over dispatch rule. Free function so the OR condition
// is unit-testable without the private MidMpcNode dispatch block. Spec:
//   take-over = consecutive_failures >= kThreshold
//               OR minalt_box_infeasible
//               OR speed_gap_infeasible
// The latter two can fire on the FIRST solve (consecutive=0): minalt_box when
// the M4 heading-box upper < own+min_alt (architectural infeasibility),
// speed_gap when |own_u - planned_u| exceeds N·decel_max·dt. Either is grounds
// for immediate BC-MPC dispatch — holding a stale NLP corridor would be wrong.
inline bool compute_bc_mpc_take_over(
    std::int64_t consecutive_failures,
    std::int64_t threshold,
    bool minalt_box_infeasible,
    bool speed_gap_infeasible) {
  return (consecutive_failures >= threshold)
      || minalt_box_infeasible
      || speed_gap_infeasible;
}

inline void synchronize_mid_mpc_constraint_context(MidMpcInput& input) {
  if (input.tail_gate_targets.empty()) {
    input.tail_gate_targets = input.targets;
  }
  input.constraints.targets = input.targets;
  input.constraints.own_ship_psi_rad = input.own_ship.psi_rad;
}

// L0 InputDegradation::summary() — builds a human-readable list of which
// upstream fields were substituted with fallbacks this cycle. Used by
// MidMpcSolution.rationale and LX X1 snapshot for root-cause tracing.
inline std::string MidMpcInput::InputDegradation::summary() const {
  std::string out;
  if (own_psi_degraded)       out += "own_psi ";
  if (own_u_degraded)         out += "own_u ";
  if (target_degraded)        out += "target ";
  if (speed_box_degraded)     out += "speed_box ";
  if (reachability_degraded)  out += "reachability ";
  if (planned_speed_degraded) out += "planned_speed ";
  return out;
}

// ---------------------------------------------------------------------------
// MidMpcSolution — result from one Mid-MPC solve cycle
// ---------------------------------------------------------------------------
struct MidMpcSolution {
  // ── L4 layered status (VR-05, 2026-07-21) ────────────────────────────────
  // The legacy `status` field conflates solver outcome, safety assessment, and
  // execution readiness. L4-T3 adds two orthogonal status layers that separate
  // these concerns while keeping `status` for backward compatibility.
  //
  //   solver_status:  what the acados NLP solver returned (raw 0..7 mapped to
  //                   a semantically-correct enum). QP-recovered (raw=4,
  //                   solver_moved, primal feasible) is DISTINCT from Converged
  //                   (raw=0, all KKT conditions met). This is the PRIMARY signal
  //                   for solver health telemetry and fallback dispatch.
  //
  //   safety_status:  independent safety assessment of the solved trajectory.
  //                   Computed from the D1 committed-prefix CPA witness,
  //                   NaN/Inf trajectory check, and L0 input degradation.
  //                   This is the PRIMARY signal for M7/MRM escalation.
  //
  //   status:         backward-compatible mapping from solver_status for
  //                   downstream consumers that have not been updated to read
  //                   solver_status directly. Mapping:
  //                     Converged       → Converged
  //                     QpRecovered     → NumericalFailure
  //                     Timeout         → Timeout
  //                     Infeasible      → Infeasible
  //                     NumericalFailure→ NumericalFailure
  //                     NotInitialized  → NotInitialized
  // ──────────────────────────────────────────────────────────────────────────
  enum class Status : std::uint8_t {
    Converged       = 0u,
    Timeout         = 1u,
    Infeasible      = 2u,
    NumericalFailure = 3u,
    NotInitialized  = 4u,
  };

  enum class SolverStatus : std::uint8_t {
    Converged        = 0u,  // raw 0, all KKT conditions met
    QpRecovered      = 1u,  // raw 4, QP error during refinement, primal feasible
    Timeout          = 2u,  // raw 1, max iterations reached
    Infeasible       = 3u,  // raw 2, QP infeasible
    NumericalFailure = 4u,  // raw 3 or other unexpected
    NotInitialized   = 5u,
  };

  enum class SafetyStatus : std::uint8_t {
    Nominal  = 0u,  // all checks pass, trajectory safe
    Degraded = 1u,  // input degraded or solver had non-fatal issues
    Unsafe   = 2u,  // prefix D1 witness failed, hard CPA violated, or NaN trajectory
    Unknown  = 3u,
  };

  Status status{Status::NotInitialized};
  SolverStatus solver_status{SolverStatus::NotInitialized};
  SafetyStatus safety_status{SafetyStatus::Unknown};
  std::vector<TrajectoryPoint> trajectory;  // N-point solution (horizon)
  double cost_total{0.0};
  double cost_colreg{0.0};
  double cost_dist{0.0};
  double cost_vel{0.0};
  // Phase 3.1/3.4 (spec v2.3 §2/§4): CPA slack value at the optimum. 0 when
  // NLP did not need slack (geometry compliant); >0 means the maneuver could
  // not fully open CPA inside the horizon and σ softened a hard-infeasibility
  // window. Reported to ASDR/SAT so "tuned green" (σ always active) is
  // distinguishable from "genuine fix" (σ zero except close-range).
  double cpa_slack{0.0};
  // P3: per-target ξ breakdown (max over stage, per target slot).
  // Length = max_targets (16); slot t is max |ξ_{t,k}| over stages k.
  // Empty target slots (t >= n_targets) are 0.0. For observability/认证
  // (CCS i-Ship ξ 行为可追溯) + SIL ρ-exact-penalty analysis.
  std::array<double, 16> cpa_slack_per_target{};
  // Step5 方案 B (FB-2 telemetry remedy, VR-01 final): with nsh=0 there is no
  // slack vector to read "soft aspiration (d < cpa_safe=2500 during conflict)
  // violation degree" from. These fields carry the soft-aspiration signal that
  // cpa_slack used to provide:
  //   soft_aspiration_d_min_m       = min over (stage k, real target t) of
  //                                   sqrt(dx_kt^2 + dy_kt^2) on the solved
  //                                   trajectory. 0 when no real target seen.
  //   soft_aspiration_violation_m   = max(0, cpa_safe - d_min_m).
  //                                   >0 means the trajectory is INSIDE the
  //                                   soft 2500 band but OUTSIDE the hard 1852
  //                                   floor (legal but not ample-time). The
  //                                   hard floor itself is enforced by the CPA
  //                                   constraint row (true hard, nsh=0).
  // Populated by MidMpcAcadosSolver::constraints_satisfied_ (acados path only).
  // The IPOPT path leaves these at 0 (it has cpa_slack instead).
  double soft_aspiration_d_min_m{0.0};
  double soft_aspiration_violation_m{0.0};
  std::int32_t solve_duration_ms{0};
  std::int32_t ipopt_iterations{0};
  std::int64_t stamp_ns{0};
  // L4-T1: CMM-style human-readable solver cycle conditions string.
  // Populated from L0 InputDegradation flags at solve() entry so downstream
  // L4/L5/LX/M8 can distinguish "real input" from "fallback" (ARCH-DECISION-03).
  // Format: "L0:own_psi own_u ..." when degraded, empty string otherwise.
  // May be enriched by other solve() paths (e.g. status-4 rejection) by
  // appending "; reason:...".
  std::string rationale;
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

// Normalize heading to [-π, +π] — the NLP psi variable box (Fix C-2b). Rule17
// and direction/min_alt constraints use raw psi - own_psi subtraction; if
// own_psi is at 2π (positive normalization) while NLP psi ∈ [-π, π], the
// subtraction yields π instead of 0, making the constraint set empty. All
// own_psi values fed to the NLP (kIdxOwnPsi, constraint_inputs.own_ship_psi_rad)
// must pass through this to stay in the same branch as the NLP psi box.
inline double normalize_heading_signed(double angle) {
  double normalized = normalize_heading_positive(angle);
  if (normalized > M_PI) {
    normalized -= 2.0 * M_PI;
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

inline bool heading_window_is_wrapped(double h_min, double h_max) {
  const double window_span = std::fabs(h_max - h_min);
  if (window_span >= (2.0 * M_PI - 1e-9)) {
    return false;
  }
  return h_min > h_max;
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

// Resolve an M4 raw heading window [h_min_raw, h_max_raw] (rad, absolute) into
// IPOPT box-safe bounds (lb <= ub). Returned bounds feed the Mid-MPC per-variable
// box (lbx/ubx); CasADi nlpsol asserts "lb <= ub" so inversion is fatal.
//
// A near-full-circle window (span ≈ 2π — e.g. M4 TRANSIT emits [0, 360 deg] or
// the quantised [0, 359 deg]) means "no heading constraint" and resolves to the
// unconstrained [-π, +π] box. Without this, normalize_angle maps [0, 359 deg]
// to an inverted psi-relative window (min > max) that throws every transit
// cycle. Narrow corridors are unwrapped near ref_psi (own-ship heading) to stay
// contiguous without crossing the ±π seam mid-window.
inline std::pair<double, double> resolve_heading_box_bounds(
    double h_min_raw, double h_max_raw, double ref_psi) {
  constexpr double kTwoPi = 6.283185307179586;
  constexpr double kFullCircleTolRad = 0.05;  // ~3 deg
  if ((h_max_raw - h_min_raw) >= (kTwoPi - kFullCircleTolRad)) {
    return {-M_PI, M_PI};
  }
  auto normalize_angle = [](double angle, double ref) {
    double diff = angle - ref;
    diff = std::fmod(diff + M_PI, kTwoPi);
    if (diff < 0.0) {
      diff += kTwoPi;
    }
    diff -= M_PI;
    return ref + diff;
  };
  double lb = normalize_angle(h_min_raw, ref_psi);
  double ub = normalize_angle(h_max_raw, ref_psi);
  // Contract: guarantee lb <= ub for CasADi nlpsol (asserts "lb <= ub").
  // normalize_angle wraps each bound independently around ref_psi; when the
  // original window is narrow but ref_psi has drifted so the window now straddles
  // the ref_psi ± pi seam, the two normalized values can invert (lb > ub). A
  // single contiguous [lb, ub] cannot represent a wrapped-around corridor, so
  // fall back to the unconstrained full box — same policy as the full-circle
  // case above. This is conservative (the corridor is widened for that cycle)
  // but never produces an invalid bound that would trip the NLP solver.
  if (lb > ub) {
    return {-M_PI, M_PI};
  }
  return {lb, ub};
}

inline bool is_m4_fallback_rationale(const std::string& rationale) {
  return rationale.find("infeasible fallback") != std::string::npos
      || rationale.find("Failsafe") != std::string::npos
      || rationale.find("geometric fallback") != std::string::npos;
}

// L0-A: speed box validation helper. Returns (speed_min_mps, speed_max_mps).
// Invalid/non-finite/negative values or max<min degrade to [0, nominal] + flag.
// M4 fallback rationale triggers nominal substitution (breaks feedback loop).
inline std::pair<double, double> validate_speed_box(
    double speed_min_kn, double speed_max_kn,
    double nominal_speed_kn,
    const std::string& m4_rationale,
    MidMpcInput::InputDegradation& deg) {
  double speed_min_mps = speed_min_kn * units::kMsPerKn;
  double speed_max_raw = speed_max_kn;

  // R3 fix: M4 fallback → use nominal speed (not current SOG).
  if (is_m4_fallback_rationale(m4_rationale)) {
    speed_max_raw = nominal_speed_kn;
  }

  const bool min_bad = !std::isfinite(speed_min_mps) || speed_min_mps < 0.0;
  const bool max_bad = !std::isfinite(speed_max_raw) || speed_max_raw <= 0.0;
  const double speed_max_mps = speed_max_raw * units::kMsPerKn;
  const bool max_lt_min = !min_bad && !max_bad && (speed_max_mps < speed_min_mps);
  if (min_bad || max_bad || max_lt_min) {
    deg.speed_box_degraded = true;
    return {0.0, nominal_speed_kn * units::kMsPerKn};
  }
  return {speed_min_mps, speed_max_mps};
}

// L0-A: earliest_min_alt_k validation helper. Returns validated k, or 0 on
// out-of-range (degrade to v2.1 ROT-only sentinel).
inline double validate_earliest_min_alt_k(double earliest_k, int32_t n_horizon,
                                           MidMpcInput::InputDegradation& deg) {
  if (!std::isfinite(earliest_k) || earliest_k < 0.0
      || earliest_k > static_cast<double>(n_horizon)) {
    deg.reachability_degraded = true;
    return 0.0;
  }
  return earliest_k;
}

inline double geometric_fallback_target_speed_kn(
    double planned_speed_mps, double nominal_speed_kn) {
  if (std::isfinite(planned_speed_mps) && planned_speed_mps > 0.5) {
    return units::mps_to_kn(planned_speed_mps);
  }
  return nominal_speed_kn;
}

inline double geometric_fallback_target_speed_kn(
    double planned_speed_mps,
    double nominal_speed_kn,
    double speed_max_mps) {
  const double preferred_speed_kn =
      geometric_fallback_target_speed_kn(planned_speed_mps, nominal_speed_kn);
  if (std::isfinite(speed_max_mps) && speed_max_mps > 0.0) {
    return std::min(preferred_speed_kn, units::mps_to_kn(speed_max_mps));
  }
  return preferred_speed_kn;
}

inline const TargetRiskSnapshot* primary_target_risk(const MidMpcInput& input) {
  const TargetRiskSnapshot* best = nullptr;
  for (const auto& risk : input.target_risks) {
    if (risk.primary) {
      return &risk;
    }
    if (best == nullptr || risk.risk_score > best->risk_score) {
      best = &risk;
    }
  }
  return best;
}

inline ColregsPreferredDirection risk_aware_fallback_direction(const MidMpcInput& input) {
  if (!input.colregs_conflict_active) {
    return input.colregs_preferred_direction;
  }
  const TargetRiskSnapshot* risk = primary_target_risk(input);
  if (risk == nullptr) {
    return input.colregs_preferred_direction;
  }

  const bool danger_intrusion = risk->danger_margin_m < 0.0;
  if (danger_intrusion) {
    return input.colregs_preferred_direction;
  }

  constexpr double kReturnXteThresholdM = 350.0;
  const bool xte_pressure = std::fabs(input.route_xte_m) >= kReturnXteThresholdM;
  const bool outside_warning = risk->warning_margin_m >= 0.0;
  const bool opening_or_clear = risk->closing_speed_mps <= 0.0;
  if (xte_pressure && outside_warning && opening_or_clear) {
    return ColregsPreferredDirection::Hold;
  }

  const bool speed_cap_active =
      std::isfinite(input.constraints.speed_max_mps) &&
      input.constraints.speed_max_mps > 0.0 &&
      input.planned_speed_mps > input.constraints.speed_max_mps + 0.1;
  const bool ample_tcpa = risk->tcpa_s > 180.0;
  const bool turn_requested =
      input.colregs_preferred_direction == ColregsPreferredDirection::Starboard ||
      input.colregs_preferred_direction == ColregsPreferredDirection::Port;
  if (turn_requested && speed_cap_active && ample_tcpa && outside_warning) {
    return ColregsPreferredDirection::ReduceSpeed;
  }

  return input.colregs_preferred_direction;
}

inline double geometric_fallback_delta_heading_rad(double own_psi, double target_psi) {
  double delta = target_psi - own_psi;
  while (delta > units::kPi) {
    delta -= units::kTwoPi;
  }
  while (delta < -units::kPi) {
    delta += units::kTwoPi;
  }
  return delta;
}

inline double geometric_fallback_rot_rad_s(double rot_max_rad_s) {
  return std::max(rot_max_rad_s, 1e-4);
}

inline double geometric_fallback_turn_radius_m(double speed_mps, double rot_max_rad_s) {
  return std::max(speed_mps / geometric_fallback_rot_rad_s(rot_max_rad_s), 50.0);
}

inline double geometric_fallback_waypoint_time_s(int waypoint_index) {
  constexpr double kFirstExecutableLookaheadS = 60.0;
  constexpr double kStepS = 10.0;
  return kFirstExecutableLookaheadS
      + (static_cast<double>(std::max(waypoint_index, 0)) * kStepS);
}

inline TrajectoryPoint geometric_fallback_arc_point(
    double own_psi,
    double target_psi,
    double speed_mps,
    double rot_max_rad_s,
    double t_s) {
  const double delta_psi = geometric_fallback_delta_heading_rad(own_psi, target_psi);
  const double rot = geometric_fallback_rot_rad_s(rot_max_rad_s);
  const double turn_radius_m = geometric_fallback_turn_radius_m(speed_mps, rot_max_rad_s);
  const double r_rad_s = (delta_psi >= 0.0) ? rot : -rot;
  const double turn_duration_s = std::abs(delta_psi) / rot;

  TrajectoryPoint point;
  point.u_mps = speed_mps;
  point.t_s = t_s;

  if (t_s <= turn_duration_s) {
    const double dpsi = r_rad_s * t_s;
    point.x_m = turn_radius_m * (std::sin(own_psi + dpsi) - std::sin(own_psi));
    point.y_m = turn_radius_m * (-std::cos(own_psi + dpsi) + std::cos(own_psi));
    point.psi_rad = own_psi + dpsi;
    point.r_rad_s = r_rad_s;
    return point;
  }

  const double x_n_arc = turn_radius_m
      * (std::sin(own_psi + delta_psi) - std::sin(own_psi));
  const double x_e_arc = turn_radius_m
      * (-std::cos(own_psi + delta_psi) + std::cos(own_psi));
  const double t_after = t_s - turn_duration_s;
  point.x_m = x_n_arc + speed_mps * t_after * std::cos(target_psi);
  point.y_m = x_e_arc + speed_mps * t_after * std::sin(target_psi);
  point.psi_rad = target_psi;
  point.r_rad_s = 0.0;
  return point;
}

// Phase 4 RECOVERY gradual return-to-route (architecture §8.3 + §7.2).
// Produces a relative NED trajectory point from the current own-ship position.
// The target global XTE decays linearly toward zero over horizon_s, so the
// relative lateral displacement is inward from the current route_xte_m.
// [TBD-HAZID] linear decay rate; initial value per architecture §7.2.
inline TrajectoryPoint recovery_route_point(
    double route_bearing_rad,
    double route_xte_m,
    double speed_mps,
    double t_s,
    double horizon_s) {
  const double fraction = (horizon_s > 1.0e-6)
      ? std::clamp(std::max(0.0, t_s) / horizon_s, 0.0, 1.0)
      : 1.0;
  const double along_m = std::max(0.0, speed_mps) * std::max(0.0, t_s);
  const double lateral_m = -route_xte_m * fraction;  // relative inward correction
  // Route frame → NED: along = forward, lateral = starboard (east for brg=0).
  const double along_n = std::cos(route_bearing_rad);
  const double along_e = std::sin(route_bearing_rad);
  const double right_n = -std::sin(route_bearing_rad);
  const double right_e = std::cos(route_bearing_rad);
  TrajectoryPoint point;
  point.x_m = along_m * along_n + lateral_m * right_n;
  point.y_m = along_m * along_e + lateral_m * right_e;
  point.psi_rad = std::atan2(point.y_m, point.x_m);
  point.u_mps = speed_mps;
  point.t_s = t_s;
  return point;
}

inline double fallback_min_alteration_rad(
    double route_brg, double h_min, double h_max, double min_alt_rad) {
  if (min_alt_rad > 0.0) {
    return min_alt_rad;
  }
  return std::min(
      circular_heading_distance(h_max, route_brg),
      circular_heading_distance(route_brg, h_min));
}

inline double fallback_target_heading(
    double route_brg,
    double h_min,
    double h_max,
    double min_alt_rad,
    ColregsPreferredDirection direction) {
  double target = route_brg;
  if (direction == ColregsPreferredDirection::Starboard) {
    target = (min_alt_rad > 0.0) ? (route_brg + min_alt_rad) : h_max;
  } else if (direction == ColregsPreferredDirection::Port) {
    target = (min_alt_rad > 0.0) ? (route_brg - min_alt_rad) : h_min;
  }
  return clamp_heading_window(target, h_min, h_max);
}

inline double fallback_target_heading(
    double route_brg, double h_min, double h_max, double min_alt_rad) {
  return fallback_target_heading(
      route_brg, h_min, h_max, min_alt_rad,
      ColregsPreferredDirection::Starboard);
}

inline double risk_aware_fallback_target_heading(
    const MidMpcInput& input,
    double route_brg,
    double h_min,
    double h_max,
    double min_alt_rad,
    ColregsPreferredDirection direction) {
  const TargetRiskSnapshot* risk = primary_target_risk(input);
  const bool inside_warning_domain = risk != nullptr && risk->warning_margin_m < 0.0;
  const bool warning_entry_imminent = risk != nullptr &&
      std::isfinite(risk->warning_margin_m) &&
      std::isfinite(risk->closing_speed_mps) &&
      risk->closing_speed_mps > 0.0 &&
      risk->warning_margin_m <=
          risk->closing_speed_mps * geometric_fallback_waypoint_time_s(0);
  if (input.colregs_conflict_active && (inside_warning_domain || warning_entry_imminent)) {
    if (direction == ColregsPreferredDirection::Starboard) {
      return h_max;
    }
    if (direction == ColregsPreferredDirection::Port) {
      return h_min;
    }
  }
  return fallback_target_heading(route_brg, h_min, h_max, min_alt_rad, direction);
}

inline bool trajectory_reaches_heading(
    const std::vector<TrajectoryPoint>& trajectory,
    double target_heading_rad,
    double tolerance_rad) {
  for (const auto& point : trajectory) {
    if (circular_heading_distance(point.psi_rad, target_heading_rad) <= tolerance_rad) {
      return true;
    }
  }
  return false;
}

inline bool trajectory_reaches_colregs_target(
    const std::vector<TrajectoryPoint>& trajectory,
    double route_brg,
    double h_min,
    double h_max,
    double min_alt_rad,
    ColregsPreferredDirection direction,
    double tolerance_rad) {
  if (direction == ColregsPreferredDirection::Hold ||
      direction == ColregsPreferredDirection::ReduceSpeed) {
    return true;
  }
  const double target_min_alt = fallback_min_alteration_rad(
      route_brg, h_min, h_max, min_alt_rad);
  const double target_heading = fallback_target_heading(
      route_brg, h_min, h_max, target_min_alt, direction);
  return trajectory_reaches_heading(trajectory, target_heading, tolerance_rad);
}

struct TailGateAcceptance {
  bool accepted{false};
  bool nlp_tail_gate_failed{true};
  std::string reason{"not_evaluated"};
};

inline double trajectory_terminal_lateral_offset_m(
    const TrajectoryPoint& point,
    double route_brg) {
  return (-std::sin(route_brg) * point.x_m) + (std::cos(route_brg) * point.y_m);
}

inline bool terminal_offset_matches_m6_direction(
    const TrajectoryPoint& terminal,
    double route_brg,
    ColregsPreferredDirection direction) {
  constexpr double kMinTailLateralOffsetM = 25.0;
  const double lateral_m = trajectory_terminal_lateral_offset_m(terminal, route_brg);
  if (direction == ColregsPreferredDirection::Starboard) {
    return lateral_m >= kMinTailLateralOffsetM;
  }
  if (direction == ColregsPreferredDirection::Port) {
    return lateral_m <= -kMinTailLateralOffsetM;
  }
  return true;
}

inline const TargetState* primary_tail_gate_target(const MidMpcInput& input) {
  const auto& candidates = input.tail_gate_targets.empty()
      ? input.targets
      : input.tail_gate_targets;
  if (candidates.empty()) {
    return nullptr;
  }
  const TargetRiskSnapshot* risk = primary_target_risk(input);
  if (risk != nullptr) {
    for (const auto& target : candidates) {
      if (std::to_string(target.id) == risk->target_id) {
        return &target;
      }
    }
  }
  return &candidates.front();
}

// Projected CPA from the NLP trajectory's TERMINAL state (own at terminal
// position/velocity, target projected forward by the terminal time). This is
// the "achieved" CPA after the avoidance maneuver — what the tail-gate must
// verify per committed-route-design-v2 §9.5 (terminal hold CPA), NOT M2's
// pre-maneuver do-nothing CPA (target.cpa_m), which is by definition small
// during active approach and would reject every converged NLP route (Bug C).
inline double trajectory_terminal_state_cpa_m(
    const MidMpcSolution& solution, const TargetState& target) {
  if (solution.trajectory.empty()) {
    return std::max(target.cpa_m, 0.0);
  }
  const auto& term = solution.trajectory.back();
  const double t_N = std::max(term.t_s, 0.0);
  // NED: x_m = north, y_m = east; psi/cog: 0 = north, positive clockwise.
  // velocity = speed * (sin(hdg), cos(hdg))? No — per propagate_trajectory_positions
  // north-vel = u*cos(psi), east-vel = u*sin(psi). Match that convention.
  const double own_vn = term.u_mps * std::cos(term.psi_rad);
  const double own_ve = term.u_mps * std::sin(term.psi_rad);
  const double tgt_vn = target.sog_mps * std::cos(target.cog_rad);
  const double tgt_ve = target.sog_mps * std::sin(target.cog_rad);
  // Target projected to terminal time, relative to own terminal position.
  const double rn = (target.x_m + tgt_vn * t_N) - term.x_m;
  const double re = (target.y_m + tgt_ve * t_N) - term.y_m;
  const double rvn = tgt_vn - own_vn;
  const double rve = tgt_ve - own_ve;
  const double rv2 = rvn * rvn + rve * rve;
  double tcpa = 0.0;
  if (rv2 > 1.0e-9) {
    tcpa = -((rn * rvn) + (re * rve)) / rv2;
    if (tcpa < 0.0) {
      tcpa = 0.0;
    }
  }
  const double cpa_n = rn + rvn * tcpa;
  const double cpa_e = re + rve * tcpa;
  return std::hypot(cpa_n, cpa_e);
}

inline bool tail_gate_cpa_release_clear(const MidMpcSolution& solution,
                                        const MidMpcInput& input) {
  const TargetState* target = primary_tail_gate_target(input);
  if (target == nullptr) {
    return true;
  }
  // v2.2 §13.4: tail-gate is a deterministic NLP PUBLISH GATE (defense-in-depth),
  // NOT an IEC 61508 SIL2 independent checker. tail-gate shares numeric inputs
  // (rot_max_rad_s, cpa_hard_m, decel_max_mps2) with the NLP constraint compiler
  // (types.hpp vs mid_mpc_solver.cpp), so it does NOT meet SIL2 independence
  // criteria. SIL2 responsibility: M7 X-axis Deterministic Checker (架构 §11.7).
  // Spec ref: docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md §13.4
  //
  // Bug C context (still relevant): the CPA-floor is a RELEASE concern (is the
  // target finally clear?), not an active-avoidance concern. During active
  // approach (target closing) the NLP maneuver IS the CPA-opening action;
  // requiring the CPA already safe would reject every active-avoidance route
  // -> geometric fallback (Bug C). Skip the floor while the target is closing;
  // apply it once the target is opening (release/recovery), where the terminal
  // CPA must be safe. Spec committed-route-design-v2 §3.1: M6 owns the
  // release/clearing signal; the closing_speed trend is the M2-backed proxy.
  const TargetRiskSnapshot* risk = primary_target_risk(input);
  const bool target_opening = (risk == nullptr) || (risk->closing_speed_mps <= 0.0);
  if (!target_opening) {
    return true;  // active approach: the maneuver opens CPA, do not pre-require it safe
  }
  const double sigma_m = std::max(target->cpa_sigma_m, 0.0);
  const double terminal_cpa_m = trajectory_terminal_state_cpa_m(solution, *target);
  // v2.1 §4.3 B6-r2: release check uses cpa_hard_m (unbumped), not cpa_safe_m
  // (bumped to 2500 during conflict — that's the J_colreg soft barrier radius,
  // not a hard floor). Using bumped value made the release floor unreachable.
  return (terminal_cpa_m - (3.0 * sigma_m)) >= input.constraints.cpa_hard_m;
}

inline bool tail_gate_turns_are_feasible(
    const std::vector<TrajectoryPoint>& trajectory,
    double own_ship_psi_rad,
    double rot_max_rad_s) {
  if (trajectory.empty() || rot_max_rad_s <= 0.0) {
    return true;
  }
  // trajectory[0].t_s == 0: it is the first command over interval [0, dt_s],
  // not a zero-duration step. The own_ship→traj[0] turn rate applies over one
  // control step, so seed prev_time one step before t=0. Dividing by ~0
  // (clamped 1e-6) instead rejected every converged NLP whose psi[0] differed
  // from own_ship psi (Bug C deep, RC-A).
  const double first_step_dt = (trajectory.size() >= 2u)
      ? std::max(trajectory[1].t_s - trajectory[0].t_s, 1.0e-6)
      : std::max(trajectory[0].t_s, 1.0e-6);
  double prev_heading = own_ship_psi_rad;
  double prev_time = -first_step_dt;
  for (const auto& cur : trajectory) {
    const double dt_s = std::max(cur.t_s - prev_time, 1.0e-6);
    if ((circular_heading_distance(cur.psi_rad, prev_heading) / dt_s) >
        (rot_max_rad_s + 1.0e-6)) {
      return false;
    }
    prev_heading = cur.psi_rad;
    prev_time = cur.t_s;
  }
  return true;
}

inline bool tail_gate_decel_is_feasible(
    const std::vector<TrajectoryPoint>& trajectory,
    double own_ship_speed_mps,
    double decel_max_mps2) {
  if (trajectory.empty() || decel_max_mps2 <= 0.0) {
    return true;
  }
  // trajectory[0].t_s == 0: it is the first command over interval [0, dt_s],
  // not a zero-duration step. The own_ship→traj[0] decel applies over one
  // control step, so seed prev_time one step before t=0. Dividing by ~0
  // (clamped 1e-6) instead rejected every converged NLP whose u[0] differed
  // from own_ship speed (decel_infeasible=512/512 in rule14-ho; Bug C deep, RC-A).
  const double first_step_dt = (trajectory.size() >= 2u)
      ? std::max(trajectory[1].t_s - trajectory[0].t_s, 1.0e-6)
      : std::max(trajectory[0].t_s, 1.0e-6);
  double prev_speed = own_ship_speed_mps;
  double prev_time = -first_step_dt;
  for (const auto& cur : trajectory) {
    const double dt_s = std::max(cur.t_s - prev_time, 1.0e-6);
    const double decel = (prev_speed - cur.u_mps) / dt_s;
    if (decel > decel_max_mps2 + 1.0e-6) {
      return false;
    }
    prev_speed = cur.u_mps;
    prev_time = cur.t_s;
  }
  return true;
}

inline bool trajectory_crosses_ahead_of_target(
    const std::vector<TrajectoryPoint>& trajectory,
    const TargetState& target,
    double route_brg) {
  constexpr double kAheadAlongMarginM = 0.0;
  constexpr double kCrossTrackWindowM = 100.0;
  const double route_n = std::cos(route_brg);
  const double route_e = std::sin(route_brg);
  const double right_n = -std::sin(route_brg);
  const double right_e = std::cos(route_brg);
  for (const auto& own : trajectory) {
    const double tgt_x = target.x_m + target.sog_mps * own.t_s * std::cos(target.cog_rad);
    const double tgt_y = target.y_m + target.sog_mps * own.t_s * std::sin(target.cog_rad);
    const double rel_x = own.x_m - tgt_x;
    const double rel_y = own.y_m - tgt_y;
    const double along_m = rel_x * route_n + rel_y * route_e;
    const double lateral_m = rel_x * right_n + rel_y * right_e;
    if (along_m > kAheadAlongMarginM && std::fabs(lateral_m) <= kCrossTrackWindowM) {
      return true;
    }
  }
  return false;
}

inline bool tail_gate_no_crossing_ahead(
    const MidMpcSolution& solution,
    const MidMpcInput& input) {
  const TargetState* target = primary_tail_gate_target(input);
  return target == nullptr || !trajectory_crosses_ahead_of_target(
      solution.trajectory, *target, input.planned_route_bearing_rad);
}

inline void apply_tail_gate_publish_contract(
    const MidMpcInput& input,
    l3_msgs::msg::AvoidancePlan& plan) {
  if (input.colregs_conflict_active && input.colregs_primary_role == 0U) {
    plan.latitude.clear();
    plan.longitude.clear();
    plan.command_speed_mps.clear();
    plan.navigation_mode.clear();
    plan.segment_source.clear();
    plan.waypoints.clear();
    plan.status = "NORMAL";
    plan.confidence = 1.0F;
    plan.rationale = "M5 stand-on hold — no avoidance tail published";
    plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
    plan.nlp_tail_gate_failed = false;
    plan.allow_degraded_execution = false;
  }
}

// v2.1 spec §4.5 B7-gap2: terminal lateral band feasibility (6th tail-gate
// check). Uses existing trajectory_terminal_lateral_offset_m(point, route_brg).
// Role guard via lateral_colreg_active: stand-on / ReduceSpeed / HOLD skip.
// pref_dir is the enum (Starboard/Port are lateral; ReduceSpeed/Hold are not).
inline bool tail_gate_terminal_lateral_feasible(
    const MidMpcSolution& solution,
    double route_brg_rad,
    ColregsPreferredDirection pref_dir,
    bool   lateral_colreg_active,
    double l_min_feasible_m,
    double l_max_feasible_m) {
  if (!lateral_colreg_active) return true;  // C4-r2 role guard: non-lateral skip
  if (solution.trajectory.empty()) return true;
  const double lN = trajectory_terminal_lateral_offset_m(
      solution.trajectory.back(), route_brg_rad);
  const double signed_pref = (pref_dir == ColregsPreferredDirection::Starboard) ? +1.0
                           : (pref_dir == ColregsPreferredDirection::Port)       ? -1.0
                           : 0.0;
  if (signed_pref * lN < l_min_feasible_m) return false;  // wrong side / insufficient
  if (lN < -l_max_feasible_m || lN > l_max_feasible_m) return false;  // out of band
  return true;
}

inline TailGateAcceptance accept_tail_gate(
    const MidMpcSolution& solution,
    const MidMpcInput& input) {
  TailGateAcceptance result;
  if (solution.status != MidMpcSolution::Status::Converged || solution.trajectory.empty()) {
    result.reason = "solver_not_converged";
    return result;
  }

  const auto& terminal = solution.trajectory.back();
  if (input.colregs_primary_role == 0U) {
    constexpr double kStandOnHeadingToleranceRad = 2.0 * units::kRadPerDeg;
    constexpr double kStandOnOffsetToleranceM = 10.0;
    const bool biased_heading = circular_heading_distance(
        terminal.psi_rad, input.planned_route_bearing_rad) > kStandOnHeadingToleranceRad;
    const bool biased_offset = std::fabs(trajectory_terminal_lateral_offset_m(
        terminal, input.planned_route_bearing_rad)) > kStandOnOffsetToleranceM;
    if (biased_heading || biased_offset) {
      result.reason = "stand_on_heading_violation";
      return result;
    }
    result.accepted = true;
    result.nlp_tail_gate_failed = false;
    result.reason = "accepted";
    return result;
  }

  if (!terminal_offset_matches_m6_direction(
          terminal,
          input.planned_route_bearing_rad,
          input.colregs_preferred_direction)) {
    result.reason = "wrong_m6_side";
    return result;
  }
  if (!tail_gate_no_crossing_ahead(solution, input)) {
    result.reason = "crossing_ahead";
    return result;
  }
  if (!tail_gate_cpa_release_clear(solution, input)) {
    result.reason = "cpa_release_floor";
    return result;
  }
  if (!tail_gate_decel_is_feasible(
          solution.trajectory, input.own_ship.u_mps, input.decel_max_mps2)) {
    result.reason = "decel_infeasible";
    return result;
  }
  if (!tail_gate_turns_are_feasible(
          solution.trajectory, input.own_ship.psi_rad, input.rot_max_rad_s)) {
    result.reason = "turn_radius_infeasible";
    return result;
  }

  // v2.1 §4.5: 6th check — terminal lateral band. Backstops the NLP terminal
  // rows being softened (terminal_nlp_soft=true default). lateral_active
  // mirrors the solver give-way-lateral condition (mid_mpc_solver.cpp:195):
  //   role == 1U || role == 2U  AND  pref_dir ∈ {Starboard, Port}
  const bool lateral_colreg_active =
      (input.colregs_primary_role == 1U || input.colregs_primary_role == 2U) &&
      (input.colregs_preferred_direction == ColregsPreferredDirection::Starboard ||
       input.colregs_preferred_direction == ColregsPreferredDirection::Port);
  if (!tail_gate_terminal_lateral_feasible(
          solution,
          input.planned_route_bearing_rad,
          input.colregs_preferred_direction,
          lateral_colreg_active,
          input.constraints.terminal_l_min_feasible_m,
          input.constraints.terminal_l_max_feasible_m)) {
    result.reason = "terminal_lateral_out_of_band";
    return result;
  }

  result.accepted = true;
  result.nlp_tail_gate_failed = false;
  result.reason = "accepted";
  return result;
}

}  // namespace mass_l3::m5

#endif  // MASS_L3_M5_COMMON_TYPES_HPP_
