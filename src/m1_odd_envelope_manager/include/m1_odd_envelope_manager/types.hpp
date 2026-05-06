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
  MrcPrep = 3,
  MrcActive = 4,
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
// Placeholder types for Tasks 2-5.
// Keep minimal for compilation; will be expanded in future tasks.
// ---------------------------------------------------------------------------

struct TScoreInputs {};
struct HScoreInputs {};
struct ScoringInputs {};
struct TmrTdlInputs {};
struct TmrTdlParams {};
struct MrcParams {};
struct MrcSelectionInputs {};
struct MrcSelection {};
struct SystemHealthSnapshot {};
struct TScoreThresholds {};
struct HScoreThresholds {};
struct ParameterSet {};

}  // namespace mass_l3::m1
