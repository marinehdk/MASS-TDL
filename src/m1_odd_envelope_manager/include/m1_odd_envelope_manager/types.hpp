#pragma once

#include <array>
#include <cstdint>

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
// Placeholder types for Tasks 3-5 (remaining).
// Keep as empty structs; will be expanded in future tasks.
// ---------------------------------------------------------------------------

struct TmrTdlInputs {};
struct TmrTdlParams {};
struct MrcParams {};
struct MrcSelectionInputs {};
struct MrcSelection {};
struct SystemHealthSnapshot {};
struct ParameterSet {};

}  // namespace mass_l3::m1
