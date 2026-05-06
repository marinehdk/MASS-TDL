/// Unit tests for OddStateMachine (5-state FSM + OVERRIDDEN).
/// PATH-S constraints apply: no exceptions, no dynamic allocation in step().
/// Test count: 28 (>= 20 required by spec).

#include <chrono>
#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include <tl/expected.hpp>

#include "m1_odd_envelope_manager/odd_state_machine.hpp"

namespace mass_l3::m1 {
namespace {

using TimePoint = std::chrono::steady_clock::time_point;

// ---------------------------------------------------------------------------
// Test fixture: common setup for OddStateMachine tests.
// ---------------------------------------------------------------------------
class OddStateMachineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    t0_ = std::chrono::steady_clock::now();
    t1_ = t0_ + std::chrono::milliseconds(100);
    t2_ = t0_ + std::chrono::milliseconds(200);
    t3_ = t0_ + std::chrono::milliseconds(300);
    t4_ = t0_ + std::chrono::milliseconds(400);

    no_events_ = EventFlags{};

    default_thresholds_ = StateMachineThresholds{.in_to_edge = 0.8,
                                               .edge_to_out = 0.5,
                                               .stale_degradation_factor = 0.5};
  }

  /// Create a valid default FSM (IN state).
  tl::expected<OddStateMachine, ErrorCode> CreateDefault() {
    return OddStateMachine::create(default_thresholds_);
  }

  TimePoint t0_, t1_, t2_, t3_, t4_;
  EventFlags no_events_;
  StateMachineThresholds default_thresholds_;
};

// ===========================================================================
// Required test cases (spec Section 7.1)
// ===========================================================================

/// 1. ConstructionRejectsNonMonotonicThresholds
/// in_to_edge must be > edge_to_out, otherwise ThresholdsNonMonotonic.
TEST_F(OddStateMachineTest, ConstructionRejectsNonMonotonicThresholds) {
  StateMachineThresholds bad{.in_to_edge = 0.3,
                             .edge_to_out = 0.6,
                             .stale_degradation_factor = 0.5};  // in_to_edge < edge_to_out
  auto result = OddStateMachine::create(bad);
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(ErrorCode::ThresholdsNonMonotonic, result.error());
}

/// 2. InitialStateIsIn
/// After successful create, the FSM must start in the In state.
TEST_F(OddStateMachineTest, InitialStateIsIn) {
  auto fsm = OddStateMachine::create(default_thresholds_);
  ASSERT_TRUE(fsm.has_value());
  EXPECT_EQ(EnvelopeState::In, fsm->current());
}

/// 3. InToEdgeAtThreshold
/// Score below in_to_edge (0.8) but above edge_to_out (0.5) -> Edge.
TEST_F(OddStateMachineTest, InToEdgeAtThreshold) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EnvelopeState result = fsm->step(0.79, 100.0, 60.0, no_events_, t1_);
  EXPECT_EQ(EnvelopeState::Edge, result);
  EXPECT_EQ(EnvelopeState::Edge, fsm->current());
}

/// 4. EdgeToOutWhenScoreDropsBelowEdge
/// Score below edge_to_out (0.5) from Edge state -> Out.
TEST_F(OddStateMachineTest, EdgeToOutWhenScoreDropsBelowEdge) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  EnvelopeState result = fsm->step(0.49, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::Out, result);
  EXPECT_EQ(EnvelopeState::Out, fsm->current());
}

/// 5. EdgeToMrcPrepWhenTdlBelowTmr
/// From Edge, when TDL <= TMR, transition to MRC_Prep.
TEST_F(OddStateMachineTest, EdgeToMrcPrepWhenTdlBelowTmr) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  // TDL (50) <= TMR (60): transition to MRC_Prep
  EnvelopeState result = fsm->step(0.65, 50.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::MrCPrep, result);
  EXPECT_EQ(EnvelopeState::MrCPrep, fsm->current());
}

/// 6. OverrideHasHighestPriority
/// override_active=true should force Overridden regardless of score.
TEST_F(OddStateMachineTest, OverrideHasHighestPriority) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EventFlags override_event{};
  override_event.override_active = true;
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, override_event, t1_);
  EXPECT_EQ(EnvelopeState::Overridden, result);
  EXPECT_EQ(EnvelopeState::Overridden, fsm->current());
}

/// 7. ReflexActivationOverridesEvenInIn
/// reflex_activation=true forces Overridden even from In.
TEST_F(OddStateMachineTest, ReflexActivationOverridesEvenInIn) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EventFlags reflex_event{};
  reflex_event.reflex_activation = true;
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, reflex_event, t1_);
  EXPECT_EQ(EnvelopeState::Overridden, result);
  EXPECT_EQ(EnvelopeState::Overridden, fsm->current());
}

/// 8. OutOnAnyAxisZero
/// Score <= 0 should trigger immediate Out.
TEST_F(OddStateMachineTest, OutOnAnyAxisZero) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EnvelopeState result = fsm->step(0.0, 100.0, 60.0, no_events_, t1_);
  EXPECT_EQ(EnvelopeState::Out, result);
  EXPECT_EQ(EnvelopeState::Out, fsm->current());
}

/// 9. InStaysInWhenScoreIsNormal
/// Score >= in_to_edge (0.8) should keep the state In.
TEST_F(OddStateMachineTest, InStaysInWhenScoreIsNormal) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, no_events_, t1_);
  EXPECT_EQ(EnvelopeState::In, result);
  EXPECT_EQ(EnvelopeState::In, fsm->current());
}

/// 10. EdgeStaysEdgeInMiddleRange
/// From Edge, score in [edge_to_out, in_to_edge) with TDL > TMR stays Edge.
TEST_F(OddStateMachineTest, EdgeStaysEdgeInMiddleRange) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  // Score 0.65 is in the middle range, TDL100 > TMR60, so stay Edge
  EnvelopeState result = fsm->step(0.65, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::Edge, result);
  EXPECT_EQ(EnvelopeState::Edge, fsm->current());
}

/// 11. OutRecoversToEdgeWhenScoreImproves
/// From Out, score improving to [edge_to_out, in_to_edge) -> Edge.
TEST_F(OddStateMachineTest, OutRecoversToEdgeWhenScoreImproves) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.4, 100.0, 60.0, no_events_, t1_);   // In -> Out
  // Score improves to 0.6 (in Edge range)
  EnvelopeState result = fsm->step(0.6, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::Edge, result);
  EXPECT_EQ(EnvelopeState::Edge, fsm->current());
}

/// 12. MrcPrepToMrcActiveOnTrigger
/// From MRC_Prep, m7_safety_mrc_required -> MRC_Active.
TEST_F(OddStateMachineTest, MrcPrepToMrcActiveOnTrigger) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  fsm->step(0.65, 50.0, 60.0, no_events_, t2_);    // Edge -> MrcPrep
  EventFlags mrc_trigger{};
  mrc_trigger.m7_safety_mrc_required = true;
  EnvelopeState result = fsm->step(0.65, 50.0, 60.0, mrc_trigger, t3_);
  EXPECT_EQ(EnvelopeState::MrCActive, result);
  EXPECT_EQ(EnvelopeState::MrCActive, fsm->current());
}

/// 13. M2InputStaleCausesScoreDegradation
/// Stale M2/M7 inputs degrade the effective score, causing conservative
/// transitions. With staleness, a score of 0.9 from In drops to effective
/// 0.45 (< edge_to_out=0.5) and transitions to Out.
TEST_F(OddStateMachineTest, M2InputStaleCausesScoreDegradation) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // Baseline: no staleness, score 0.9 stays In
  ASSERT_EQ(EnvelopeState::In, fsm->step(0.9, 100.0, 60.0, no_events_, t1_));

  // With stale inputs: effective score = 0.9 * 0.5 = 0.45 < 0.5 -> Out
  EventFlags stale{};
  stale.m2_input_stale = true;
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, stale, t2_);
  EXPECT_EQ(EnvelopeState::Out, result);
}

/// 14. StateMachineCreateReturnsErrorForZeroThresholds
/// Both thresholds at 0 should produce an error (non-monotonic).
TEST_F(OddStateMachineTest, StateMachineCreateReturnsErrorForZeroThresholds) {
  StateMachineThresholds zero{.in_to_edge = 0.0,
                              .edge_to_out = 0.0,
                              .stale_degradation_factor = 0.5};
  auto result = OddStateMachine::create(zero);
  ASSERT_FALSE(result.has_value());
}

/// 15. StepDoesNotModifyInternalStateIfOutOfOrder
/// When step() receives inputs that do not cause a transition, internal
/// state (current_, last_transition_at_) must remain unchanged.
TEST_F(OddStateMachineTest, StepDoesNotModifyInternalStateIfOutOfOrder) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // First step: In with good score -> stays In
  fsm->step(0.9, 100.0, 60.0, no_events_, t1_);
  ASSERT_EQ(EnvelopeState::In, fsm->current());
  TimePoint last = fsm->last_transition_at();

  // Second step with identical inputs -> no transition, timestamp unchanged
  fsm->step(0.9, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::In, fsm->current());
  EXPECT_EQ(last, fsm->last_transition_at());
}

/// 16. ForecastReturnsCurrentStateForZeroHorizon
/// For horizon=0, forecast must return the current state with uncertainty=0.
TEST_F(OddStateMachineTest, ForecastReturnsCurrentStateForZeroHorizon) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  StateForecast fc = fsm->forecast(std::chrono::seconds(0));
  EXPECT_EQ(EnvelopeState::In, fc.predicted);
  EXPECT_DOUBLE_EQ(0.0, fc.uncertainty);
}

/// 17. ForecastWithNonZeroHorizon
/// Forecast with non-zero horizon must return a predicted state and
/// uncertainty > 0.
TEST_F(OddStateMachineTest, ForecastWithNonZeroHorizon) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  StateForecast fc = fsm->forecast(std::chrono::seconds(10));
  EXPECT_GT(fc.uncertainty, 0.0);
  EXPECT_LE(fc.uncertainty, 1.0);
}

/// 18. RationaleNotEmptyAfterTransition
/// After any transition, rationale() must return a non-empty description.
TEST_F(OddStateMachineTest, RationaleNotEmptyAfterTransition) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EXPECT_FALSE(fsm->rationale().empty());

  // Trigger a transition and check rationale again
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  EXPECT_FALSE(fsm->rationale().empty());
}

/// 19. LastTransitionAtUpdatedOnTransition
/// When a transition occurs, last_transition_at() must reflect the new time.
TEST_F(OddStateMachineTest, LastTransitionAtUpdatedOnTransition) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  TimePoint initial_tp = fsm->last_transition_at();

  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  EXPECT_NE(initial_tp, fsm->last_transition_at());
  // The stored time should match the time passed to step()
  EXPECT_EQ(t1_, fsm->last_transition_at());
}

/// 20. RapidToggleBetweenInAndEdge
/// Toggling the score around the in_to_edge threshold must produce
/// consistent, non-oscillating behavior.
TEST_F(OddStateMachineTest, RapidToggleBetweenInAndEdge) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // In -> Edge (score drops to 0.79)
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);
  ASSERT_EQ(EnvelopeState::Edge, fsm->current());

  // Edge -> In (score recovers to 0.9)
  fsm->step(0.9, 100.0, 60.0, no_events_, t2_);
  ASSERT_EQ(EnvelopeState::In, fsm->current());

  // In -> Edge again (score drops)
  fsm->step(0.79, 100.0, 60.0, no_events_, t3_);
  ASSERT_EQ(EnvelopeState::Edge, fsm->current());

  // Same score again -> no oscillation, stays Edge
  fsm->step(0.79, 100.0, 60.0, no_events_, t4_);
  EXPECT_EQ(EnvelopeState::Edge, fsm->current());
}

// ===========================================================================
// Additional coverage: rules not fully exercised by required test cases
// ===========================================================================

/// 21. InToOutDirectly
/// Score below edge_to_out (0.5) from In goes directly to Out, skipping Edge.
TEST_F(OddStateMachineTest, InToOutDirectly) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EnvelopeState result = fsm->step(0.4, 100.0, 60.0, no_events_, t1_);
  EXPECT_EQ(EnvelopeState::Out, result);
}

/// 22. EdgeToInWhenScoreImproves
/// From Edge, score >= in_to_edge -> In (recovery path).
TEST_F(OddStateMachineTest, EdgeToInWhenScoreImproves) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::In, result);
}

/// 23. MrcPrepStaysWithoutTrigger
/// MRC_Prep without m7_safety_mrc_required stays in MRC_Prep.
TEST_F(OddStateMachineTest, MrcPrepStaysWithoutTrigger) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);  // In -> Edge
  fsm->step(0.65, 50.0, 60.0, no_events_, t2_);    // Edge -> MrcPrep
  // No MRC trigger: stays MrcPrep
  EnvelopeState result = fsm->step(0.65, 50.0, 60.0, no_events_, t3_);
  EXPECT_EQ(EnvelopeState::MrCPrep, result);
}

/// 24. MrcActiveRecoversToIn
/// From MRC_Active, when safety critical clears and score is good, go to In.
TEST_F(OddStateMachineTest, MrcActiveRecoversToIn) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // Navigate to MRC_Active
  fsm->step(0.79, 100.0, 60.0, no_events_, t1_);   // In -> Edge
  fsm->step(0.65, 50.0, 60.0, no_events_, t2_);     // Edge -> MrcPrep
  EventFlags mrc_trigger{};
  mrc_trigger.m7_safety_mrc_required = true;
  fsm->step(0.65, 50.0, 60.0, mrc_trigger, t3_);    // MrcPrep -> MrcActive
  ASSERT_EQ(EnvelopeState::MrCActive, fsm->current());

  // Recovery: safety critical cleared, score good
  EventFlags recovery{};
  recovery.m7_safety_critical = false;
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, recovery, t4_);
  EXPECT_EQ(EnvelopeState::In, result);
}

/// 25. OverriddenToInWhenOverrideClears
/// From Overridden, when override clears and score recovers, transition to In.
TEST_F(OddStateMachineTest, OverriddenToInWhenOverrideClears) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // Enter Overridden
  EventFlags override_event{};
  override_event.override_active = true;
  fsm->step(0.9, 100.0, 60.0, override_event, t1_);
  ASSERT_EQ(EnvelopeState::Overridden, fsm->current());

  // Release override with good score -> In
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, no_events_, t2_);
  EXPECT_EQ(EnvelopeState::In, result);
}

/// 26. OverriddenStaysWhenOverrideActive
/// From Overridden, if override is still active, stay Overridden.
TEST_F(OddStateMachineTest, OverriddenStaysWhenOverrideActive) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());

  // Enter Overridden
  EventFlags override_event{};
  override_event.override_active = true;
  fsm->step(0.9, 100.0, 60.0, override_event, t1_);
  ASSERT_EQ(EnvelopeState::Overridden, fsm->current());

  // Override still active -> stays Overridden
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, override_event, t2_);
  EXPECT_EQ(EnvelopeState::Overridden, result);
}

/// 27. NaNScoreTriggersOut
/// NaN score must trigger immediate Out per IEC 61508 safety guard.
TEST_F(OddStateMachineTest, NaNScoreTriggersOut) {
  auto fsm = CreateDefault();
  ASSERT_TRUE(fsm.has_value());
  EnvelopeState result = fsm->step(std::numeric_limits<double>::quiet_NaN(),
                                   100.0, 60.0, no_events_, t1_);
  EXPECT_EQ(EnvelopeState::Out, result);
}

/// 28. StaleDegradationFactorApplied
/// Stale input factor should be configurable via thresholds.
TEST_F(OddStateMachineTest, StaleDegradationFactorApplied) {
  StateMachineThresholds agg{.in_to_edge = 0.8, .edge_to_out = 0.5,
                              .stale_degradation_factor = 0.1};
  auto fsm = OddStateMachine::create(agg);
  ASSERT_TRUE(fsm.has_value());
  EventFlags stale{};
  stale.m2_input_stale = true;
  // score 0.9 * 0.1 = 0.09 -> below edge_to_out -> Out
  EnvelopeState result = fsm->step(0.9, 100.0, 60.0, stale, t1_);
  EXPECT_EQ(EnvelopeState::Out, result);
}

}  // namespace
}  // namespace mass_l3::m1
