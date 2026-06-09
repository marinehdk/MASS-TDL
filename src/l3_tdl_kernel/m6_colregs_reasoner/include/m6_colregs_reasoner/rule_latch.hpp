#pragma once

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

// Onset-latched COLREG hysteresis (Rule 13(d): classification fixed at onset;
// later bearing changes do not reclassify). Release follows Rule 16 "finally past
// and clear": the target has drawn abaft the beam (past_and_clear) AND the range
// is opening.
//
// NOTE (2026-06-08): a former CPA-magnitude fallback (release when predicted CPA
// climbed above 1.5×cpa_safe while opening) was REMOVED — it could not tell
// "CPA large because the target passed" from "CPA large because own-ship's own
// give-way maneuver opened it". During a successful avoidance the maneuver opens
// CPA past the threshold, so the fallback released mid-maneuver → conflict
// chatter → M4 flapped AVOID↔TRANSIT → rudder fishtailed. Release now requires
// the target to be genuinely abaft the beam. The bridge geometry-release
// (TCPA<0 & DCPA≥cpa_safe) remains the independent backup if abaft is never met.
//
// ONSET CLASSIFICATION (2026-06-08): the latch also snapshots the give-way
// CLASSIFICATION at the latching cycle (role/encounter/direction). Once own ship
// turns to starboard, the raw geometric rule (e.g. Rule 14 head-on) falls out of
// its ±6° cone and re-evaluates to role=FREE — so holding only `is_active` left
// `requires_action()` (which needs a give-way role) false, and conflict_detected
// was carried by flickering secondary rules → flap. Holding the ONSET role through
// the maneuver (Rule 13(d)) keeps requires_action()/conflict_detected stable until
// finally past and clear (Rule 8(d)).
class RuleLatch {
 public:
  RuleLatch(double cpa_safe_m, double release_factor)
      : cpa_safe_m_(cpa_safe_m), release_cpa_m_(cpa_safe_m * release_factor) {}

  // Returns whether the rule should be treated as ACTIVE this cycle.
  // past_and_clear: target is abaft the beam (Rule 16 finally-past-and-clear test).
  // current_eval (optional): the raw evaluation this cycle. On the latching cycle
  // its classification is snapshotted as the onset classification and held
  // (Rule 13(d)); a later raw re-evaluation does NOT overwrite it.
  bool update(bool rule_active, double cpa_m, bool range_closing, bool past_and_clear,
              const RuleEvaluation* current_eval = nullptr) {
    if (!latched_) {
      // Latch only on a genuine onset: rule fired AND threat is real.
      if (rule_active && cpa_m < cpa_safe_m_ && range_closing) {
        latched_ = true;
        if (current_eval != nullptr) {  // fix classification at onset (Rule 13(d))
          onset_role_ = current_eval->role;
          onset_encounter_ = current_eval->encounter_type;
          onset_phase_ = current_eval->phase;
          onset_direction_ = current_eval->preferred_direction;
          onset_min_alteration_deg_ = current_eval->min_alteration_deg;
          has_onset_ = true;
        }
      }
      return latched_;
    }
    // Latched: release only once the encounter is finally past and clear
    // (target abaft the beam) AND the range is opening. Do NOT release on a
    // CPA-magnitude heuristic — own-ship's give-way maneuver itself opens CPA,
    // which would release the latch mid-maneuver and chatter (see class note).
    (void)cpa_m;  // retained in signature for onset gate above; not used for release
    const bool opening = !range_closing;
    if (opening && past_and_clear) {
      latched_ = false;
      has_onset_ = false;  // encounter resolved → forget onset classification
    }
    return latched_;
  }

  bool latched() const { return latched_; }

  // True once an onset classification has been snapshotted (and not yet released).
  bool has_onset() const { return has_onset_; }

  // Overlay the held onset classification onto an evaluation whose raw geometry has
  // gone inactive mid-maneuver. Marks the rule active and restores the give-way
  // role/encounter/direction so requires_action()/conflict_detected stay stable.
  // No-op if no onset has been captured.
  void apply_onset(RuleEvaluation& eval) const {
    if (!has_onset_) return;
    eval.is_active = true;
    eval.role = onset_role_;
    eval.encounter_type = onset_encounter_;
    eval.phase = onset_phase_;
    eval.preferred_direction = onset_direction_;
    eval.min_alteration_deg = onset_min_alteration_deg_;
  }

 private:
  double cpa_safe_m_;
  double release_cpa_m_;
  bool latched_{false};

  // Onset classification snapshot (Rule 13(d): fixed at onset, held through maneuver).
  bool has_onset_{false};
  Role onset_role_{Role::FREE};
  EncounterType onset_encounter_{EncounterType::NONE};
  TimingPhase onset_phase_{TimingPhase::PRESERVE_COURSE};
  std::string onset_direction_{"HOLD"};
  double onset_min_alteration_deg_{0.0};
};

}  // namespace mass_l3::m6_colregs
