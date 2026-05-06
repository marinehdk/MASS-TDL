#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mass_l3::m1 {

/// ODD zone classification per COLREG context.
enum class OddZone : std::uint8_t { A = 0, B = 1, C = 2, D = 3 };

/// Autonomous level supported by the system.
enum class AutoLevel : std::uint8_t { D2 = 2, D3 = 3, D4 = 4 };

/// System health status.
enum class SystemHealth : std::uint8_t { Full = 0, Degraded = 1, Critical = 2 };

/// 5-state FSM + OVERRIDDEN for operational envelope.
enum class EnvelopeState : std::uint8_t {
  In = 0,
  Edge = 1,
  Out = 2,
  MrCPrep = 3,
  MrCActive = 4,
  Overridden = 5
};

/// Types of Minimal Risk Conditions available.
enum class MrcType : std::uint8_t { Drift = 1, Anchor = 2, HeaveTo = 3, Moored = 4 };

/// Per-axis conformance scores (E=environment, T=task, H=human).
struct ScoreTriple {
  double e_score;
  double t_score;
  double h_score;
  double conformance_score;
};

/// Weighting for the composite conformance score.
struct ScoreWeights {
  double w_e;
  double w_t;
  double w_h;
};

/// Environment score thresholds (visibility and sea state).
struct EScoreThresholds {
  double visibility_full_nm;
  double visibility_degraded_nm;
  double visibility_marginal_nm;
  double sea_state_full_hs;
  double sea_state_degraded_hs;
  double sea_state_marginal_hs;
};

/// Thresholds for the 5-state FSM transitions.
struct StateMachineThresholds {
  double in_to_edge;
  double edge_to_out;
  double stale_degradation_factor;  // [TBD-HAZID] 0.5; score multiplier when inputs stale
};

/// Event flags consumed by step().
struct EventFlags {
  bool override_active;
  bool reflex_activation;
  bool m7_safety_critical;
  bool m7_safety_mrc_required;
  bool m2_input_stale;
  bool m7_input_stale;
};

/// Forecast result from a state.
struct StateForecast {
  EnvelopeState predicted;
  double uncertainty;
};

/// Time-to-MRC (TMR) and Time-to-Decision-Loss (TDL) pair.
struct TmrTdlPair {
  double tmr_s;
  double tdl_s;
};

// ---------------------------------------------------------------------------
// Scoring input / threshold types for Tasks 2-5.
// ---------------------------------------------------------------------------

/// Telemetry / health inputs for Task (T) score evaluation.
struct TScoreInputs {
  bool gnss_quality_good;
  bool radar_health_ok;
  bool comm_ok;
  double comm_delay_s;
  bool any_sensor_critical;
};

/// Human / operator (H) score inputs.
struct HScoreInputs {
  bool tmr_available;
  bool comm_ok;
};

/// Composite inputs driving all three score axes (E, T, H).
struct ScoringInputs {
  double visibility_nm;
  double sea_state_hs;
  bool gnss_quality_good;
  bool radar_health_ok;
  bool comm_ok;
  double comm_delay_s;
  bool any_sensor_critical;
  bool tmr_available;
};

/// Thresholds for the Task (T) score computation.
struct TScoreThresholds {
  double comm_delay_ok_s;     // [TBD-HAZID] 2.0
  double t_score_comm_ok;     // [TBD-HAZID] 0.6
  double t_score_comm_bad;    // [TBD-HAZID] 0.3
};

/// Thresholds for the Human (H) score computation.
struct HScoreThresholds {
  double h_score_tmr_available;   // [TBD-HAZID] 1.0
  double h_score_tmr_unavailable; // [TBD-HAZID] 0.5
};

// ---------------------------------------------------------------------------
// TMR/TDL input types (Task 3).
// ---------------------------------------------------------------------------

/// Snapshot of system health for TDL estimation.
struct SystemHealthSnapshot {
  double mttf_estimate_s;          // estimated mean time to failure [s]
  double heartbeat_recency_s;      // seconds since last heartbeat [s]
  uint32_t fault_count;             // faults in recent window
  bool has_redundancy;              // redundant systems available
};

/// Inputs for TMR and TDL computation.
struct TmrTdlInputs {
  double tcpa_min_s;               // closest TCPA across all targets [s]
  double current_rtt_s;            // communication round-trip time [s]
  SystemHealthSnapshot system_health;
  bool h_score_tmr_available;      // from HScoreInputs
};

/// Configuration parameters for TMR/TDL estimation.
struct TmrTdlParams {
  double tmr_baseline_s;      // [TBD-HAZID] 60.0 -- Veitch 2024 baseline
  double tcpa_coefficient;    // [TBD-HAZID] 0.6 -- TCPA multiplier for TDL
  double tmr_min_s;           // 30.0 -- minimum TMR clamp
  double tmr_max_s;           // 600.0 -- maximum TMR clamp
  double tdl_min_s;           // 0.0 -- minimum TDL clamp
  double tdl_max_s;           // 600.0 -- maximum TDL clamp
};

// ---------------------------------------------------------------------------
// MRC selection types (Task 4).
// ---------------------------------------------------------------------------

/// Configuration parameters for MRC feasibility checks.
struct MrcParams {
  double max_anchor_depth_m;        // [TBD-HAZID] 50.0
  double max_heave_to_sea_state_hs; // [TBD-HAZID] 4.0
  double max_heave_to_wind_kn;      // [TBD-HAZID] 40.0
};

/// A selected Minimum Risk Condition with speed command and rationale.
struct MrcSelection {
  MrcType type;
  double speed_cmd_kn;
  std::string_view rationale;
};

/// Inputs driving the MRC selection algorithm.
struct MrcSelectionInputs {
  bool m7_safety_mrc_required;
  MrcType m7_recommended_mrm;
  double water_depth_m;
  bool in_anchorage_zone;
  double sea_state_hs;
  double wind_speed_kn;
  bool is_moored;
  EnvelopeState current_state;
};

/// YAML-loaded superset of all M1 runtime parameters.
/// All [TBD-HAZID] parameters are loaded here -- no hardcoded thresholds.
/// Individual domain classes (OddStateMachine, ConformanceScoreCalculator,
/// TmrTdlEstimator, MrcTriggerLogic) receive their subset during node init.
struct ParameterSet {
  // State machine
  double in_to_edge;
  double edge_to_out;
  double stale_degradation_factor;

  // Scoring weights
  double w_e;
  double w_t;
  double w_h;

  // E-score thresholds (visibility in nm, sea state in Hs metres)
  double visibility_full_nm;
  double visibility_degraded_nm;
  double visibility_marginal_nm;
  double sea_state_full_hs;
  double sea_state_degraded_hs;
  double sea_state_marginal_hs;

  // T-score thresholds
  double comm_delay_ok_s;
  double t_score_nominal;
  double t_score_degraded;
  double t_score_critical;

  // H-score thresholds
  double h_score_available;
  double h_score_unavailable;

  // TMR/TDL
  double tmr_baseline_s;
  double tcpa_coefficient;
  double tmr_min_s;
  double tmr_max_s;
  double tdl_min_s;
  double tdl_max_s;

  // MRC
  double max_anchor_depth_m;
  double max_heave_to_sea_state_hs;
  double max_heave_to_wind_kn;
};

}  // namespace mass_l3::m1
