#pragma once

#include <cstdint>
#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

// Explicit encounter state machine replacing the implicit RuleLatch.
// 7 states aligned to the COLREGs Rule 14 typical operation flow:
//   CLEAR (start/return-to-route) -> DETECTED (target in WorldState)
//   -> CANDIDATE (Rule geometry holds) -> PREPLAN (TCPA within monitor window,
//   M5 shadow solve) -> ACTIVE (TCPA <= T_plan, onset snapshot frozen,
//   give-way published) -> MONITOR (CPA trend improving) -> RELEASE
//   (past-and-clear) -> CLEAR (after dwell).
//
// The FSM is per (target, primary rule 13/14/15) instance. Caller
// (colregs_reasoner_node) computes range_closing and past_and_clear from
// cross-cycle geometry and feeds them as opaque booleans so the FSM stays
// pure transition logic. See Spec 2026-06-17-colregs-avoidance-fsm-design
// section 3.
enum class EncounterState : uint8_t {
  CLEAR = 0,
  DETECTED = 1,
  CANDIDATE = 2,
  PREPLAN = 3,
  ACTIVE = 4,
  MONITOR = 5,
  RELEASE = 6
};

// Phase 1.1 (R8 fix, spec v2.3 §3.1): rank EncounterState for per-target
// semantic_state aggregation. The legacy "first-write + CLEAR-to-non-CLEAR"
// gate at colregs_reasoner_node.cpp:988-994 let Rule13 (rule-library order
// 13 before 14) populate semantic_state with DETECTED before Rule14's ACTIVE,
// suppressing the ACTIVE write and pinning M6 at ONSET
// (m6_not_past_clear × 874 in V2.3 phase 3b probe). Rank-based aggregation is
// evaluation-order independent.
//
// Rank ordering: CLEAR < DETECTED < CANDIDATE < PREPLAN
//   < ACTIVE == MONITOR (parity) < RELEASE (terminal past-clear).
// RELEASE dominates: it is the authoritative "encounter over" signal. The
// natural CLEAR-after-dwell return at rank 0 is allowed to overwrite only
// when no higher-rank state is present this cycle.
inline int encounter_state_rank(EncounterState s) noexcept {
  switch (s) {
    case EncounterState::CLEAR:    return 0;
    case EncounterState::DETECTED: return 1;
    case EncounterState::CANDIDATE:return 2;
    case EncounterState::PREPLAN:  return 3;
    case EncounterState::ACTIVE:   return 4;
    case EncounterState::MONITOR:  return 4;  // parity with ACTIVE
    case EncounterState::RELEASE:  return 5;
  }
  return 0;
}

// Onset snapshot (Rule 13(d): classification fixed at first stable classifier
// geometry and held through own-ship/target maneuvering so the raw geometric
// rule falling out of its cone does not reclassify the encounter mid-action).
struct OnsetSnapshot {
  bool valid{false};
  Role role{Role::FREE};
  EncounterType encounter_type{EncounterType::NONE};
  TimingPhase phase{TimingPhase::PRESERVE_COURSE};
  std::string preferred_direction{"HOLD"};
  double min_alteration_deg{0.0};
};

// Per-encounter parameters (ODD-aware; loaded from odd_aware_thresholds.yaml).
// Fields marked [A-level] are written-fixed from A-grade sources and are NOT
// adjusted by HAZID; fields marked [ref] are reference baselines pending
// HAZID RUN-001 calibration (2026-08-19).
struct EncounterParams {
  double t_plan_s{720.0};          // [A-level C-12] PREPLAN->ACTIVE TCPA gate
  double t_monitor_s{1500.0};      // [ref] CANDIDATE->PREPLAN TCPA gate
  double cpa_hard_m{1852.0};       // [ref] PREPLAN->ACTIVE CPA gate (onset)
  double cpa_soft_m{2778.0};       // [ref] CANDIDATE->PREPLAN CPA gate
  double cpa_safe_m{1852.0};       // [ref] RELEASE CPA gate (legacy, kept for
                                   // stand-on/projection parity; give-way
                                   // RELEASE uses cpa_release_m below)
  double cpa_release_m{1000.0};    // [ref] RELEASE CPA gate for give-way
                                   // encounters. Smaller than cpa_hard: a
                                   // crossing give-way maneuver typically
                                   // opens CPA to ~0.7 nm which is well clear
                                   // of the ship domain but below the 1.0 nm
                                   // onset threshold. Using cpa_hard here
                                   // starves RELEASE on the slower crossing
                                   // probes (route_return fails).
  double t_dwell_s{60.0};          // [ref] RELEASE->CLEAR dwell
  double min_alteration_deg{30.0}; // [A-level Rule8] used by caller for constraint
  // Rule17 stand-on (PhaseClassifier, unchanged from legacy)
  double t_standOn_s{480.0};
  double t_act_s{240.0};
  double t_emergency_s{60.0};
};

// Per-cycle target snapshot fed to transition(). Pure data; the FSM does not
// own or mutate the source WorldState.
struct TargetSnapshot {
  double tcpa_s{0.0};
  double cpa_m{0.0};
};

// EncounterStateMachine: one instance per (target, primary rule) pair.
// Stateless w.r.t. ROS; the node owns a map of these and calls transition()
// each reasoning cycle (2 Hz).
//
// past_and_clear is caller-computed. For give-way / BOTH_GIVE_WAY the caller
// ORs the abaft-beam test (against the onset reference heading) with a CPA
// projection backup. For stand-on (Rule 17 in-extremis) the caller passes the
// abaft-beam test only — projection release is forbidden so the own-ship does
// not hand back to route-following before the target is genuinely past.
class EncounterStateMachine {
 public:
  explicit EncounterStateMachine(const EncounterParams& params);

  // Evaluate the state transition for this cycle. Returns the new (current)
  // state. now_s is a monotonically increasing cycle clock in seconds, used
  // only for the RELEASE->CLEAR dwell timer.
  //
  // rule_geometric_hit: raw Rule 13/14/15 geometry holds this cycle.
  // raw_eval: optional; on PREPLAN->ACTIVE its classification is snapshotted
  //           as the onset (Rule 13(d)).
  EncounterState transition(const TargetSnapshot& target, bool rule_geometric_hit,
                            bool range_closing, bool past_and_clear, double now_s,
                            const RuleEvaluation* raw_eval = nullptr);

  EncounterState state() const { return state_; }
  const OnsetSnapshot& onset() const { return onset_; }

  // ACTIVE or MONITOR: the encounter requires give-way / stand-on action.
  bool requires_action() const;

  // True once the FSM has passed through RELEASE into CLEAR. Used by the node
  // to drop per-target encounter state once the encounter is fully resolved
  // (replaces RuleLatch::released()).
  bool had_been_released() const { return had_been_released_; }

  // Apply the held onset classification onto an evaluation whose raw geometry
  // went inactive mid-maneuver (own-ship's starboard turn rotated the target
  // off the head-on cone). No-op if no onset captured. Mirrors RuleLatch API.
  void apply_onset(RuleEvaluation& eval) const;

  // Clear all state (new-run reset when sim time jumps backward).
  void reset();

 private:
  void capture_onset_if_classified_(const RuleEvaluation* raw_eval);

  EncounterParams params_;
  EncounterState state_{EncounterState::CLEAR};
  OnsetSnapshot onset_;
  double release_condition_met_since_s_{-1.0};  // RELEASE dwell start clock
  double last_cpa_m_{-1.0};                      // for dCPA/dt ACTIVE<->MONITOR
  int cpa_improve_counter_{0};                   // consecutive improving cycles
  bool cpa_hard_seen_{false};                    // hard-zone breach in this encounter
  bool had_been_released_{false};
};

}  // namespace mass_l3::m6_colregs
