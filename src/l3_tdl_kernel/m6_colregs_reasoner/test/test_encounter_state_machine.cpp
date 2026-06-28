// EncounterStateMachine golden tests (T1-T9).
//
// These encode the behavior contracts migrated from RuleLatch (T1-T7, behavior
// preservation) plus the new TCPA gate (T8) and D-5 guard contract (T9 is in
// m4). T6/T7 are multi-rule integration behaviors covered by
// test_colregs_chain.cpp at the node level.
//
// NOTE: the container build runs with BUILD_TESTING=OFF, so these do not
// execute in CI; they are behavior-contract documentation and run locally
// where ROS toolchain is available.
#include <gtest/gtest.h>

#include "m6_colregs_reasoner/encounter_state_machine.hpp"

namespace mass_l3::m6_colregs {
namespace {

EncounterParams make_test_params() {
  EncounterParams p{};
  p.t_plan_s = 720.0;
  p.t_monitor_s = 1500.0;
  p.cpa_hard_m = 1852.0;
  p.cpa_soft_m = 2778.0;
  p.cpa_safe_m = 1852.0;
  p.cpa_release_m = 1000.0;  // give-way RELEASE gate (< cpa_hard)
  p.t_dwell_s = 60.0;
  p.t_standOn_s = 480.0;
  p.t_act_s = 240.0;
  p.t_emergency_s = 60.0;
  p.min_alteration_deg = 30.0;
  return p;
}

TargetSnapshot snap(double tcpa_s, double cpa_m) {
  TargetSnapshot t{};
  t.tcpa_s = tcpa_s;
  t.cpa_m = cpa_m;
  return t;
}

// Drive a fresh FSM through CLEAR->DETECTED->CANDIDATE->PREPLAN->ACTIVE.
// Caller controls the final ACTIVE-entry snapshot via tcpa/cpa on the last
// transition. Returns the FSM at ACTIVE.
EncounterStateMachine drive_to_active(const EncounterParams& p) {
  EncounterStateMachine fsm(p);
  // CLEAR -> DETECTED (target enters world)
  fsm.transition(snap(2000.0, 0.0), /*rule_hit=*/false, false, false, 0.0);
  // DETECTED -> CANDIDATE (rule geometry holds)
  fsm.transition(snap(2000.0, 0.0), /*rule_hit=*/true, false, false, 1.0);
  // CANDIDATE -> PREPLAN (TCPA within monitor, CPA below soft)
  fsm.transition(snap(1400.0, 2500.0), true, true, false, 2.0);
  // PREPLAN -> ACTIVE (TCPA <= t_plan, CPA < hard, closing)
  RuleEvaluation raw{};
  raw.role = Role::BOTH_GIVE_WAY;
  raw.encounter_type = EncounterType::HEAD_ON;
  raw.preferred_direction = "STARBOARD";
  raw.min_alteration_deg = 30.0;
  fsm.transition(snap(500.0, 800.0), true, true, false, 3.0, &raw);
  return fsm;
}

// --- Early-state transitions ------------------------------------------------

TEST(EncounterStateMachine, StartsInClear) {
  EncounterStateMachine fsm(make_test_params());
  EXPECT_EQ(fsm.state(), EncounterState::CLEAR);
}

TEST(EncounterStateMachine, ClearToDetectedWhenTargetEnters) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);
  EXPECT_EQ(fsm.state(), EncounterState::DETECTED);
}

TEST(EncounterStateMachine, DetectedToCandidateWhenGeometryHits) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);  // -> DETECTED
  fsm.transition(snap(2000.0, 0.0), true, false, false, 1.0);   // geometry hits
  EXPECT_EQ(fsm.state(), EncounterState::CANDIDATE);
}

TEST(EncounterStateMachine, CandidateToPreplanWhenTcpaCpaEnterWindow) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);
  fsm.transition(snap(2000.0, 0.0), true, false, false, 1.0);   // -> CANDIDATE
  // TCPA=1400 <= t_monitor(1500), CPA=2500 < soft(2778)
  fsm.transition(snap(1400.0, 2500.0), true, true, false, 2.0);
  EXPECT_EQ(fsm.state(), EncounterState::PREPLAN);
}

// --- T8: TCPA gate (the core D-3 fix) ---------------------------------------

// CPA=0 but TCPA > t_plan must stay in PREPLAN, NOT enter ACTIVE. This is the
// Rule 16 "ample time" gate (A-level C-12 case law).
TEST(EncounterStateMachine, T8_TcpaGate_StaysPreplanWhenTcpaAboveTplan) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);  // -> DETECTED
  fsm.transition(snap(2000.0, 0.0), true, false, false, 1.0);   // -> CANDIDATE
  fsm.transition(snap(1400.0, 2500.0), true, true, false, 2.0);  // -> PREPLAN
  // CPA=0 but TCPA=1000 > t_plan(720): must NOT enter ACTIVE.
  fsm.transition(snap(1000.0, 0.0), true, true, false, 3.0);
  EXPECT_EQ(fsm.state(), EncounterState::PREPLAN)
      << "CPA=0 but TCPA>T_plan must stay PREPLAN (Rule 16 ample time)";
}

// When TCPA finally drops below t_plan AND closing, enter ACTIVE.
TEST(EncounterStateMachine, T8_TcpaGate_EntersActiveWhenTcpaAtOrBelowTplan) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);
  fsm.transition(snap(2000.0, 0.0), true, false, false, 1.0);
  fsm.transition(snap(1400.0, 2500.0), true, true, false, 2.0);  // -> PREPLAN
  RuleEvaluation raw{};
  raw.role = Role::BOTH_GIVE_WAY;
  raw.encounter_type = EncounterType::HEAD_ON;
  raw.preferred_direction = "STARBOARD";
  raw.min_alteration_deg = 30.0;
  fsm.transition(snap(720.0, 800.0), true, true, false, 3.0, &raw);
  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE);
  EXPECT_TRUE(fsm.onset().valid);
  EXPECT_EQ(fsm.onset().role, Role::BOTH_GIVE_WAY);
}

TEST(EncounterStateMachine, T8_TcpaGate_UsesEarlierHardCpaBreachForSameEncounter) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);
  fsm.transition(snap(1120.0, 1730.0), true, true, false, 1.0);
  EXPECT_EQ(fsm.state(), EncounterState::CANDIDATE);
  fsm.transition(snap(1120.0, 1730.0), true, true, false, 2.0);
  EXPECT_EQ(fsm.state(), EncounterState::PREPLAN);

  RuleEvaluation raw{};
  raw.role = Role::GIVE_WAY;
  raw.encounter_type = EncounterType::CROSSING;
  raw.preferred_direction = "STARBOARD";
  raw.min_alteration_deg = 50.0;
  fsm.transition(snap(650.0, 2035.0), true, true, false, 3.0, &raw);

  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE);
  EXPECT_TRUE(fsm.requires_action());
}

TEST(EncounterStateMachine, PreservesPreplanOnsetWhenGeometryDropsBeforeActive) {
  EncounterStateMachine fsm(make_test_params());
  RuleEvaluation head_on{};
  head_on.is_active = true;
  head_on.role = Role::BOTH_GIVE_WAY;
  head_on.encounter_type = EncounterType::HEAD_ON;
  head_on.phase = TimingPhase::SOUND_WARNING;
  head_on.preferred_direction = "STARBOARD";
  head_on.min_alteration_deg = 30.0;

  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);  // -> DETECTED
  fsm.transition(snap(1450.0, 900.0), true, true, false, 1.0, &head_on);
  EXPECT_EQ(fsm.state(), EncounterState::CANDIDATE);
  fsm.transition(snap(1400.0, 900.0), true, true, false, 2.0, &head_on);
  EXPECT_EQ(fsm.state(), EncounterState::PREPLAN);

  RuleEvaluation raw_free{};
  raw_free.is_active = false;
  raw_free.role = Role::FREE;
  raw_free.encounter_type = EncounterType::NONE;
  raw_free.preferred_direction = "HOLD";
  fsm.transition(snap(700.0, 900.0), false, true, false, 3.0, &raw_free);

  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE);
  ASSERT_TRUE(fsm.onset().valid);
  EXPECT_EQ(fsm.onset().role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(fsm.onset().encounter_type, EncounterType::HEAD_ON);
  RuleEvaluation held = raw_free;
  fsm.apply_onset(held);
  EXPECT_TRUE(held.is_active);
  EXPECT_EQ(held.role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(held.encounter_type, EncounterType::HEAD_ON);
  EXPECT_EQ(held.preferred_direction, "STARBOARD");
}

// --- T1: onset snapshot held through own-ship maneuver ----------------------

// Own-ship turns starboard; raw rule geometry falls out (rule_hit=false), but
// the FSM must hold ACTIVE and keep the onset classification (Rule 13(d)).
TEST(EncounterStateMachine, T1_OnsetHoldsWhenGeometryFallsOutMidManeuver) {
  auto fsm = drive_to_active(make_test_params());
  ASSERT_EQ(fsm.state(), EncounterState::ACTIVE);
  // Geometry fell out (own-ship turned), CPA transiently improving.
  fsm.transition(snap(400.0, 1200.0), /*rule_hit=*/false, true, false, 4.0);
  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE)
      << "must hold ACTIVE through own-ship maneuver (Rule 13(d))";
  EXPECT_TRUE(fsm.requires_action());
  EXPECT_TRUE(fsm.onset().valid);
  // apply_onset must restore the held classification onto an inactive eval.
  RuleEvaluation eval{};
  eval.is_active = false;
  eval.role = Role::FREE;
  fsm.apply_onset(eval);
  EXPECT_TRUE(eval.is_active);
  EXPECT_EQ(eval.role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(eval.preferred_direction, "STARBOARD");
}

// --- T4: range not closing -> no ACTIVE -------------------------------------

TEST(EncounterStateMachine, T4_NoActiveWhenRangeNotClosing) {
  EncounterStateMachine fsm(make_test_params());
  fsm.transition(snap(2000.0, 0.0), false, false, false, 0.0);
  fsm.transition(snap(2000.0, 0.0), true, false, false, 1.0);   // -> CANDIDATE
  fsm.transition(snap(1400.0, 2500.0), true, /*closing=*/false, false, 2.0);  // -> PREPLAN
  // TCPA <= t_plan and CPA < hard, but NOT closing -> must stay PREPLAN.
  fsm.transition(snap(500.0, 800.0), true, /*closing=*/false, false, 3.0);
  EXPECT_EQ(fsm.state(), EncounterState::PREPLAN);
}

// --- ACTIVE -> MONITOR -> RELEASE flow --------------------------------------

TEST(EncounterStateMachine, ActiveToMonitorWhenCpaImprovesTwoCycles) {
  auto fsm = drive_to_active(make_test_params());
  ASSERT_EQ(fsm.state(), EncounterState::ACTIVE);
  // CPA improving cycle 1 (800 -> 1000)
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE);  // need 2 consecutive
  // CPA improving cycle 2 (1000 -> 1200)
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  EXPECT_EQ(fsm.state(), EncounterState::MONITOR);
}

// T2: release uses caller-computed past_and_clear (which uses the onset
// reference heading, not the current avoidance heading — Spec 3.3.2). The FSM
// itself treats past_and_clear as opaque.
TEST(EncounterStateMachine, T2_MonitorToReleaseWhenPastAndClearAndCpaSafe) {
  auto fsm = drive_to_active(make_test_params());
  // Graduate to MONITOR via 2 improving cycles.
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  ASSERT_EQ(fsm.state(), EncounterState::MONITOR);
  // Caller signals past_and_clear (computed vs onset reference heading),
  // range opening, CPA >= safe.
  fsm.transition(snap(-10.0, 2000.0), false, /*closing=*/false,
                 /*past_and_clear=*/true, 6.0);
  EXPECT_EQ(fsm.state(), EncounterState::RELEASE);
}

// --- T3: projection release (caller-computed) -------------------------------

// Caller may pass past_and_clear=true from a CPA projection (give-way backup)
// when geometry can never satisfy the abaft-beam test. The FSM is agnostic.
TEST(EncounterStateMachine, T3_ProjectionReleaseViaCallerPastAndClear) {
  auto fsm = drive_to_active(make_test_params());
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  ASSERT_EQ(fsm.state(), EncounterState::MONITOR);
  // Target stays forward (no abaft-beam), but caller passes past_and_clear
  // from a CPA projection (tcpa<=epsilon, cpa>=safe, opening).
  fsm.transition(snap(-5.0, 2000.0), false, /*closing=*/false,
                 /*past_and_clear=*/true, 6.0);
  EXPECT_EQ(fsm.state(), EncounterState::RELEASE);
}

// --- RELEASE -> CLEAR dwell -------------------------------------------------

TEST(EncounterStateMachine, ReleaseToClearAfterDwell) {
  auto fsm = drive_to_active(make_test_params());
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  fsm.transition(snap(-10.0, 2000.0), false, false, true, 6.0);  // -> RELEASE
  ASSERT_EQ(fsm.state(), EncounterState::RELEASE);
  // Before dwell elapses, still RELEASE.
  fsm.transition(snap(-20.0, 2100.0), false, false, true, 6.5);
  EXPECT_EQ(fsm.state(), EncounterState::RELEASE);
  // After dwell (t_dwell=60s), -> CLEAR.
  fsm.transition(snap(-30.0, 2200.0), false, false, true, 70.0);
  EXPECT_EQ(fsm.state(), EncounterState::CLEAR);
  EXPECT_TRUE(fsm.had_been_released());
  EXPECT_FALSE(fsm.onset().valid);  // onset forgotten after resolution
}

// RELEASE condition breaks -> back to MONITOR (no premature clear).
TEST(EncounterStateMachine, ReleaseBreaksBackToMonitor) {
  auto fsm = drive_to_active(make_test_params());
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  fsm.transition(snap(-10.0, 2000.0), false, false, true, 6.0);  // -> RELEASE
  // Condition breaks: CPA drops below safe, past_and_clear false.
  fsm.transition(snap(100.0, 1500.0), false, true, false, 7.0);
  EXPECT_EQ(fsm.state(), EncounterState::MONITOR);
}

// MONITOR -> ACTIVE regression when CPA deteriorates inside T_plan window.
TEST(EncounterStateMachine, MonitorToActiveRegressionOnCpaDeterioration) {
  auto fsm = drive_to_active(make_test_params());
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  ASSERT_EQ(fsm.state(), EncounterState::MONITOR);
  // CPA deteriorates (1200 -> 900) and still within T_plan.
  fsm.transition(snap(400.0, 900.0), false, true, false, 6.0);
  EXPECT_EQ(fsm.state(), EncounterState::ACTIVE);
}

// --- T5: reset --------------------------------------------------------------

// Release CPA threshold is separated from the onset (cpa_hard) threshold. A
// give-way crossing maneuver typically opens CPA to a value below cpa_hard
// (1.0 nm) but well clear of the ship domain -- RELEASE must be reachable at
// cpa_release_m, otherwise route_return starves on slower crossings.
TEST(EncounterStateMachine, ReleaseReachesAtCpaReleaseBelowCpaHard) {
  auto p = make_test_params();
  ASSERT_LT(p.cpa_release_m, p.cpa_hard_m);  // sanity: separation exists
  auto fsm = drive_to_active(p);
  // Graduate to MONITOR.
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  ASSERT_EQ(fsm.state(), EncounterState::MONITOR);
  // CPA=1300 is >= cpa_release(1000) but < cpa_hard(1852): with past_and_clear
  // and range opening, RELEASE must be reachable (this is the rule15-cs case).
  fsm.transition(snap(-10.0, 1300.0), false, /*closing=*/false,
                 /*past_and_clear=*/true, 6.0);
  EXPECT_EQ(fsm.state(), EncounterState::RELEASE)
      << "RELEASE must be reachable at cpa>=cpa_release even if <cpa_hard";
}

// Below cpa_release, MONITOR must NOT release even if past_and_clear.
// Use a large TCPA (> t_plan) so the CPA drop does not trigger the
// MONITOR->ACTIVE regression path (which is a separate concern).
TEST(EncounterStateMachine, MonitorHoldsBelowCpaReleaseEvenIfPastAndClear) {
  auto fsm = drive_to_active(make_test_params());
  fsm.transition(snap(300.0, 1000.0), false, true, false, 4.0);
  fsm.transition(snap(200.0, 1200.0), false, true, false, 5.0);
  ASSERT_EQ(fsm.state(), EncounterState::MONITOR);
  // CPA=800 < cpa_release(1000), TCPA=999 > t_plan(720) so no ACTIVE regression:
  // too close, must stay MONITOR (no release).
  fsm.transition(snap(999.0, 800.0), false, /*closing=*/false,
                 /*past_and_clear=*/true, 6.0);
  EXPECT_EQ(fsm.state(), EncounterState::MONITOR);
}

TEST(EncounterStateMachine, T5_ResetClearsAllState) {
  auto fsm = drive_to_active(make_test_params());
  ASSERT_EQ(fsm.state(), EncounterState::ACTIVE);
  ASSERT_TRUE(fsm.onset().valid);
  fsm.reset();
  EXPECT_EQ(fsm.state(), EncounterState::CLEAR);
  EXPECT_FALSE(fsm.onset().valid);
  EXPECT_FALSE(fsm.had_been_released());
}

}  // namespace
}  // namespace mass_l3::m6_colregs
