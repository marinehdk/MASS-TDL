// EncounterStateMachine implementation.
//
// Transition table follows Spec 2026-06-17-colregs-avoidance-fsm-design §3.2:
//   CLEAR -> DETECTED -> CANDIDATE -> PREPLAN -> ACTIVE <-> MONITOR -> RELEASE -> CLEAR
//
// The FSM is pure transition logic. range_closing and past_and_clear are
// caller-computed booleans (see header doc); the FSM does not re-derive
// geometry. The onset snapshot (Rule 13(d)) is captured on PREPLAN->ACTIVE
// entry and held through own-ship's maneuver so the raw geometric rule
// falling out of its cone mid-turn does not reclassify the encounter.

#include "m6_colregs_reasoner/encounter_state_machine.hpp"

namespace mass_l3::m6_colregs {

EncounterStateMachine::EncounterStateMachine(const EncounterParams& params)
    : params_(params) {}

bool EncounterStateMachine::requires_action() const {
  return state_ == EncounterState::ACTIVE || state_ == EncounterState::MONITOR;
}

void EncounterStateMachine::reset() {
  state_ = EncounterState::CLEAR;
  onset_ = OnsetSnapshot{};
  release_condition_met_since_s_ = -1.0;
  last_cpa_m_ = -1.0;
  cpa_improve_counter_ = 0;
  cpa_hard_seen_ = false;
  had_been_released_ = false;
}

void EncounterStateMachine::apply_onset(RuleEvaluation& eval) const {
  if (!onset_.valid) return;
  // Restore the onset classification onto an eval whose raw geometry went
  // inactive mid-maneuver (own-ship's turn rotated the target off the cone).
  eval.is_active = true;
  eval.role = onset_.role;
  eval.encounter_type = onset_.encounter_type;
  eval.phase = onset_.phase;
  eval.preferred_direction = onset_.preferred_direction;
  eval.min_alteration_deg = onset_.min_alteration_deg;
}

void EncounterStateMachine::capture_onset_if_classified_(const RuleEvaluation* raw_eval) {
  if (onset_.valid || raw_eval == nullptr) return;
  const bool classified =
      raw_eval->role != Role::FREE ||
      raw_eval->encounter_type != EncounterType::NONE ||
      raw_eval->preferred_direction != "HOLD" ||
      raw_eval->min_alteration_deg > 0.0;
  if (!classified) return;
  onset_.valid = true;
  onset_.role = raw_eval->role;
  onset_.encounter_type = raw_eval->encounter_type;
  onset_.phase = raw_eval->phase;
  onset_.preferred_direction = raw_eval->preferred_direction;
  onset_.min_alteration_deg = raw_eval->min_alteration_deg;
}

EncounterState EncounterStateMachine::transition(const TargetSnapshot& target,
                                                  bool rule_geometric_hit,
                                                  bool range_closing,
                                                  bool past_and_clear,
                                                  double now_s,
                                                  const RuleEvaluation* raw_eval) {
  const double prev_cpa_m = last_cpa_m_;
  // Track CPA trend across cycles for ACTIVE<->MONITOR. last_cpa_m_ is updated
  // every cycle at the bottom; prev_cpa_m is the value from the prior cycle.
  const bool cpa_improved =
      prev_cpa_m > 0.0 && target.cpa_m > prev_cpa_m;
  const bool cpa_hard_hit = target.cpa_m < params_.cpa_hard_m;
  const bool cpa_soft_context = target.cpa_m < params_.cpa_soft_m;

  switch (state_) {
    case EncounterState::CLEAR:
      // Target present this cycle -> begin tracking. We unconditionally move
      // to DETECTED; the caller only constructs/retains an FSM instance when a
      // target is in WorldState, so any transition() call means a target is
      // present.
      state_ = EncounterState::DETECTED;
      break;

    case EncounterState::DETECTED:
      // Rule 13/14/15 raw geometry holds -> classify as a real encounter.
      if (rule_geometric_hit) {
        cpa_hard_seen_ = cpa_hard_seen_ || cpa_hard_hit;
        capture_onset_if_classified_(raw_eval);
        state_ = EncounterState::CANDIDATE;
      }
      break;

    case EncounterState::CANDIDATE: {
      if (rule_geometric_hit) {
        cpa_hard_seen_ = cpa_hard_seen_ || cpa_hard_hit;
        capture_onset_if_classified_(raw_eval);
      }
      // Enter pre-planning when TCPA is within the monitor window AND CPA is
      // below the soft threshold (threat is becoming real). Spec §3.2 row
      // CANDIDATE->PREPLAN.
      const bool tcpa_in_window = target.tcpa_s <= params_.t_monitor_s;
      const bool cpa_below_soft = target.cpa_m < params_.cpa_soft_m;
      if (tcpa_in_window && cpa_below_soft) {
        state_ = EncounterState::PREPLAN;
      }
      break;
    }

    case EncounterState::PREPLAN: {
      if (rule_geometric_hit) {
        cpa_hard_seen_ = cpa_hard_seen_ || cpa_hard_hit;
        capture_onset_if_classified_(raw_eval);
      }
      // T8 TCPA gate (D-3): the core fix. Enter ACTIVE only when ALL of:
      //   TCPA <= t_plan  (A-level C-12 ample time -- not too early)
      //   CPA  <  cpa_hard (threat is real at action range)
      //   range closing   (target is actually approaching)
      // A far target with CPA~0 but TCPA > t_plan stays in PREPLAN.
      const bool tcpa_ripe = target.tcpa_s <= params_.t_plan_s;
      const bool cpa_action_context = cpa_hard_hit || (cpa_hard_seen_ && cpa_soft_context);
      if (tcpa_ripe && cpa_action_context && range_closing) {
        state_ = EncounterState::ACTIVE;
        // Onset snapshot (Rule 13(d)): use the first classifier geometry seen
        // during CANDIDATE/PREPLAN, falling back to ACTIVE-entry raw eval.
        capture_onset_if_classified_(raw_eval);
        cpa_improve_counter_ = 0;
      }
      break;
    }

    case EncounterState::ACTIVE: {
      // T1 onset hold: raw geometry may fall out as own-ship turns; stay
      // ACTIVE regardless of rule_geometric_hit (onset classification held).
      //
      // Graduate to MONITOR once CPA has improved for >=2 consecutive cycles
      // -- the maneuver is taking effect and we shift to checking it.
      if (cpa_improved) {
        ++cpa_improve_counter_;
        if (cpa_improve_counter_ >= 2) {
          state_ = EncounterState::MONITOR;
          cpa_improve_counter_ = 0;
        }
      } else {
        cpa_improve_counter_ = 0;
      }
      // ACTIVE is sticky: only MONITOR (CPA improving) or the caller-side
      // release path moves us forward. Geometry dropout does not regress.
      break;
    }

    case EncounterState::MONITOR: {
      // Regression: CPA deteriorating AND still within the T_plan action
      // window means the maneuver is not working -- go back to ACTIVE.
      const bool cpa_deteriorating =
          prev_cpa_m > 0.0 && target.cpa_m < prev_cpa_m;
      if (cpa_deteriorating && target.tcpa_s <= params_.t_plan_s) {
        state_ = EncounterState::ACTIVE;
        cpa_improve_counter_ = 0;
        break;
      }
      // Release: caller signals past_and_clear (Rule 16 finally past and
      // clear, computed vs the onset reference heading), range opening, and
      // CPA at/above the release threshold. cpa_release_m is smaller than
      // cpa_hard_m: a give-way maneuver typically opens CPA to a value that
      // is well clear of the ship domain but below the 1.0 nm onset
      // threshold; requiring cpa_hard here starves RELEASE on slower
      // crossings. Spec §3.2 row MONITOR->RELEASE.
      const bool opening = !range_closing;
      const bool cpa_safe = target.cpa_m >= params_.cpa_release_m;
      if (past_and_clear && opening && cpa_safe) {
        state_ = EncounterState::RELEASE;
        release_condition_met_since_s_ = now_s;
      }
      break;
    }

    case EncounterState::RELEASE: {
      // The release condition must persist for the dwell window before we
      // declare the encounter fully resolved (guards against a transient
      // geometry blip dropping the latch prematurely).
      const bool opening = !range_closing;
      const bool cpa_safe = target.cpa_m >= params_.cpa_release_m;
      const bool still_past_clear_safe = past_and_clear && opening && cpa_safe;
      if (!still_past_clear_safe) {
        // Condition broke mid-dwell -> back to MONITOR, restart dwell clock.
        state_ = EncounterState::MONITOR;
        release_condition_met_since_s_ = -1.0;
        break;
      }
      if (release_condition_met_since_s_ >= 0.0 &&
          (now_s - release_condition_met_since_s_) >= params_.t_dwell_s) {
        // Dwell satisfied -> encounter resolved.
        state_ = EncounterState::CLEAR;
        onset_ = OnsetSnapshot{};  // forget onset (Rule 13(d) hold released)
        release_condition_met_since_s_ = -1.0;
        cpa_hard_seen_ = false;
        had_been_released_ = true;
      }
      break;
    }
  }

  last_cpa_m_ = target.cpa_m;
  return state_;
}

}  // namespace mass_l3::m6_colregs
