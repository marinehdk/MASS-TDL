#pragma once

#include <string>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

// Onset-latched COLREG hysteresis (Rule 13(d): classification fixed at onset;
// later bearing changes do not reclassify). Release follows Rule 16 "finally past
// and clear": the target has drawn abaft the beam (past_and_clear), range is
// opening, and CPA is safe. Some give-way classifiers may also use a
// role-specific CPA-projection backup when their geometry cannot satisfy the
// abaft-beam test.
//
// CPA-magnitude-only fallback remains forbidden: own-ship's give-way maneuver can
// open CPA before the encounter is past. The TCPA/CPA projection is the backup
// release for geometry that never satisfies the abaft-beam test.
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
  RuleLatch(double cpa_safe_m, double release_factor) : cpa_safe_m_(cpa_safe_m) {
    (void)release_factor;  // retained for API compatibility after CPA release removal
  }

  // Returns whether the rule should be treated as ACTIVE this cycle.
  // past_and_clear: target is abaft the beam (Rule 16 finally-past-and-clear test).
  // current_eval (optional): the raw evaluation this cycle. On the latching cycle
  // its classification is snapshotted as the onset classification and held
  // (Rule 13(d)); a later raw re-evaluation does NOT overwrite it.
  bool update(bool rule_active, double cpa_m, bool range_closing, bool past_and_clear,
              const RuleEvaluation* current_eval = nullptr,
              bool cpa_projection_past_and_safe = false,
              bool allow_projection_release = true) {
    if (!latched_) {
      if (released_past_clear_ || cpa_projection_past_and_safe) {
        return false;
      }
      // Latch only on a genuine onset: rule fired AND threat is real.
      if (rule_active && cpa_m < cpa_safe_m_ && range_closing && !past_and_clear) {
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
    // (target abaft the encounter reference beam), the range is opening, AND CPA
    // is safe. Do NOT release on a CPA-magnitude heuristic alone — own-ship's
    // give-way maneuver itself opens CPA, which would release the latch
    // mid-maneuver and chatter (see class note).
    const bool opening = !range_closing;
    const bool past_clear_and_safe = opening && past_and_clear && (cpa_m >= cpa_safe_m_);
    const bool projection_past_and_safe =
        allow_projection_release && opening && cpa_projection_past_and_safe;
    if (projection_past_and_safe) {
      latched_ = false;
      has_onset_ = false;
      released_past_clear_ = true;
    }
    if (past_clear_and_safe) {
      latched_ = false;
      has_onset_ = false;  // encounter resolved → forget onset classification
      released_past_clear_ = true;
    }
    return latched_;
  }

  bool latched() const { return latched_; }
  bool released() const { return released_past_clear_; }

  // True once an onset classification has been snapshotted (and not yet released).
  bool has_onset() const { return has_onset_; }
  Role onset_role() const { return onset_role_; }

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
  bool latched_{false};

  // Onset classification snapshot (Rule 13(d): fixed at onset, held through maneuver).
  bool has_onset_{false};
  bool released_past_clear_{false};
  Role onset_role_{Role::FREE};
  EncounterType onset_encounter_{EncounterType::NONE};
  TimingPhase onset_phase_{TimingPhase::PRESERVE_COURSE};
  std::string onset_direction_{"HOLD"};
  double onset_min_alteration_deg_{0.0};
};

}  // namespace mass_l3::m6_colregs
