#include <gtest/gtest.h>

#include "m6_colregs_reasoner/rule_latch.hpp"

namespace mass_l3::m6_colregs {
namespace {

TEST(RuleLatch, LatchesOnOnsetHoldsThroughBearingSwing) {
  RuleLatch latch{/*cpa_safe_m=*/1852.0, /*release_factor=*/1.5};
  // onset: rule active, range closing, cpa unsafe
  EXPECT_TRUE(latch.update(/*rule_active=*/true, /*cpa_m=*/900.0, /*range_closing=*/true,
                           /*past_and_clear=*/false));
  // own-ship turned; rule geometry says inactive, but cpa still unsafe → STAY latched
  EXPECT_TRUE(latch.update(/*rule_active=*/false, /*cpa_m=*/900.0, /*range_closing=*/true,
                           /*past_and_clear=*/false));
}

TEST(RuleLatch, DoesNotReleaseOnCpaOpeningWithoutPastAndClear) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true, false);  // onset
  // Own-ship's give-way maneuver opens CPA (range opening, CPA climbing past the
  // old 1.5×cpa_safe=2778 threshold) but the target is NOT yet abaft the beam.
  // The latch MUST hold — releasing here caused the mid-maneuver conflict chatter.
  EXPECT_TRUE(latch.update(false, 2000.0, false, false));
  EXPECT_TRUE(latch.update(false, 3000.0, false, false));
  EXPECT_TRUE(latch.update(false, 5000.0, false, false));
}

TEST(RuleLatch, NeverLatchesIfNeverOnset) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_FALSE(latch.update(false, 5000.0, false, false));
}

// Rule 16 "finally past and clear": once the target draws abaft the beam and the
// range is opening, release immediately even though predicted CPA has not yet
// climbed past the conservative 1.5×cpa_safe fallback threshold. This is what
// hands control back to the route-return autopilot in time.
TEST(RuleLatch, ReleasesWhenPastAndClearBelowCpaThreshold) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_TRUE(latch.update(true, 900.0, true, false));   // onset
  // cpa 2000 < release 2778, but target abaft beam & opening → release
  EXPECT_FALSE(latch.update(false, 2000.0, false, true));
}

TEST(RuleLatch, PastAndClearDoesNotReleaseWhileStillClosing) {
  RuleLatch latch{1852.0, 1.5};
  latch.update(true, 900.0, true, false);  // onset
  // past_and_clear asserted but range still closing → not finally clear, hold
  EXPECT_TRUE(latch.update(false, 2000.0, true, true));
}

// Rule 13(d): once latched, the give-way CLASSIFICATION is held through own ship's
// maneuver. After own turns to starboard the raw head-on rule re-evaluates to
// role=FREE/inactive; apply_onset() must restore the onset give-way role so
// requires_action()/conflict_detected stay stable (the fix for the mid-maneuver
// rudder fishtail).
TEST(RuleLatch, PreservesOnsetGiveWayClassificationThroughRawInactive) {
  RuleLatch latch{1852.0, 1.5};
  RuleEvaluation onset{};
  onset.is_active = true;
  onset.role = Role::BOTH_GIVE_WAY;
  onset.encounter_type = EncounterType::HEAD_ON;
  onset.preferred_direction = "STARBOARD";
  onset.min_alteration_deg = 15.0;
  onset.phase = TimingPhase::INDEPENDENT_ACTION;

  // Onset cycle: latches AND snapshots the classification.
  EXPECT_TRUE(latch.update(true, 900.0, true, false, &onset));
  EXPECT_TRUE(latch.has_onset());

  // Own ship turned to starboard: raw geometry now says FREE/inactive, but the
  // encounter is not past-and-clear → stays latched.
  RuleEvaluation raw{};
  raw.is_active = false;
  raw.role = Role::FREE;
  raw.encounter_type = EncounterType::NONE;
  raw.preferred_direction = "HOLD";
  raw.min_alteration_deg = 0.0;
  EXPECT_TRUE(latch.update(false, 900.0, true, false, &raw));

  // Overlay the onset classification: give-way role restored → requires_action true.
  latch.apply_onset(raw);
  EXPECT_TRUE(raw.is_active);
  EXPECT_EQ(raw.role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(raw.encounter_type, EncounterType::HEAD_ON);
  EXPECT_EQ(raw.preferred_direction, "STARBOARD");
  EXPECT_DOUBLE_EQ(raw.min_alteration_deg, 15.0);
}

// Onset classification is FIXED at the latching cycle: a later raw re-evaluation
// (e.g. a transient secondary-rule role) must NOT overwrite the held onset role.
TEST(RuleLatch, OnsetClassificationIsFixedAtOnsetNotRefreshed) {
  RuleLatch latch{1852.0, 1.5};
  RuleEvaluation onset{};
  onset.is_active = true;
  onset.role = Role::BOTH_GIVE_WAY;
  onset.encounter_type = EncounterType::HEAD_ON;
  latch.update(true, 900.0, true, false, &onset);

  // A later cycle where the raw rule briefly re-fires with a DIFFERENT role.
  RuleEvaluation later{};
  later.is_active = true;
  later.role = Role::STAND_ON;
  later.encounter_type = EncounterType::CROSSING;
  latch.update(true, 800.0, true, false, &later);

  RuleEvaluation probe{};
  probe.role = Role::FREE;
  latch.apply_onset(probe);
  EXPECT_EQ(probe.role, Role::BOTH_GIVE_WAY);                 // onset, not the later STAND_ON
  EXPECT_EQ(probe.encounter_type, EncounterType::HEAD_ON);
}

// On release (finally past and clear) the onset snapshot is dropped so a stale
// classification cannot bleed into a later encounter.
TEST(RuleLatch, ClearsOnsetClassificationOnRelease) {
  RuleLatch latch{1852.0, 1.5};
  RuleEvaluation onset{};
  onset.is_active = true;
  onset.role = Role::GIVE_WAY;
  EXPECT_TRUE(latch.update(true, 900.0, true, false, &onset));
  EXPECT_TRUE(latch.has_onset());
  // target abaft beam & opening → release
  EXPECT_FALSE(latch.update(false, 2000.0, false, true, nullptr));
  EXPECT_FALSE(latch.has_onset());
  RuleEvaluation probe{};
  probe.is_active = false;
  probe.role = Role::FREE;
  latch.apply_onset(probe);  // no-op after release
  EXPECT_FALSE(probe.is_active);
  EXPECT_EQ(probe.role, Role::FREE);
}

}  // namespace
}  // namespace mass_l3::m6_colregs
