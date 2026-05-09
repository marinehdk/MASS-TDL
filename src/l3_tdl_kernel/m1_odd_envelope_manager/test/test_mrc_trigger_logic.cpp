/// Unit tests for MrcTriggerLogic (MRC selection).
/// PATH-S constraints: noexcept, no exceptions, no dynamic allocation.
/// Required test count: >=10 (13 defined below).

#include <cmath>
#include <limits>

#include <gtest/gtest.h>
#include <tl_expected/expected.hpp>

#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"

namespace mass_l3::m1 {
namespace {

// ---------------------------------------------------------------------------
// Helpers: default parameter set and inputs
// ---------------------------------------------------------------------------

MrcParams MakeDefaultParams() {
  MrcParams p{};
  p.max_anchor_depth_m = 50.0;
  p.max_heave_to_sea_state_hs = 4.0;
  p.max_heave_to_wind_kn = 40.0;
  return p;
}

/// Build inputs for a normal (non-MRC) scenario: state=In, no M7 request.
MrcSelectionInputs MakeNormalInputs() {
  MrcSelectionInputs in{};
  in.m7_safety_mrc_required = false;
  in.m7_recommended_mrm = MrcType::Drift;
  in.water_depth_m = 30.0;
  in.in_anchorage_zone = true;
  in.sea_state_hs = 2.0;
  in.wind_speed_kn = 20.0;
  in.is_moored = false;
  in.current_state = EnvelopeState::In;
  return in;
}

// ===========================================================================
// Factory / creation tests
// ===========================================================================

/// 1. CreateWithValidParams — factory accepts valid params.
TEST(MrcTriggerLogicTest, CreateWithValidParams) {
  auto result = MrcTriggerLogic::create(MakeDefaultParams());
  EXPECT_TRUE(result.has_value());
}

/// 2. CreateRejectsInvalidParams — negative depths or thresholds.
TEST(MrcTriggerLogicTest, CreateRejectsInvalidParams) {
  MrcParams p = MakeDefaultParams();

  p.max_anchor_depth_m = -1.0;
  EXPECT_FALSE(MrcTriggerLogic::create(p).has_value());

  p = MakeDefaultParams();
  p.max_heave_to_sea_state_hs = -0.1;
  EXPECT_FALSE(MrcTriggerLogic::create(p).has_value());

  p = MakeDefaultParams();
  p.max_heave_to_wind_kn = -5.0;
  EXPECT_FALSE(MrcTriggerLogic::create(p).has_value());
}

// ===========================================================================
// Selection logic tests
// ===========================================================================

/// 3. SelectReturnsNulloptWhenNotRequired — no MRC needed in In state.
TEST(MrcTriggerLogicTest, SelectReturnsNulloptWhenNotRequired) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());
  auto result = logic->select(MakeNormalInputs());
  EXPECT_FALSE(result.has_value());
}

/// 4. SelectDriftWhenNoOtherPossible — only drift is available.
TEST(MrcTriggerLogicTest, SelectDriftWhenNoOtherPossible) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  // Force all higher priorities to fail.
  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = true;   // force MRC
  in.is_moored = false;
  in.water_depth_m = 999.0;           // too deep for anchor
  in.in_anchorage_zone = false;
  in.sea_state_hs = 10.0;             // too rough for heave-to
  in.wind_speed_kn = 100.0;

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::Drift, result->type);
  EXPECT_DOUBLE_EQ(4.0, result->speed_cmd_kn);
}

/// 5. SelectAnchorWhenConditionsAllow — depth and zone ok.
TEST(MrcTriggerLogicTest, SelectAnchorWhenConditionsAllow) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = true;   // force MRC
  in.is_moored = false;
  in.water_depth_m = 30.0;            // < 50.0
  in.in_anchorage_zone = true;
  in.sea_state_hs = 10.0;             // too rough for heave-to
  in.wind_speed_kn = 100.0;

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::Anchor, result->type);
  EXPECT_DOUBLE_EQ(0.0, result->speed_cmd_kn);
}

/// 6. SelectHeaveToWhenSeaStateOk — Hs and wind constraints.
TEST(MrcTriggerLogicTest, SelectHeaveToWhenSeaStateOk) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = true;
  in.is_moored = false;
  in.water_depth_m = 999.0;           // too deep for anchor
  in.in_anchorage_zone = false;
  in.sea_state_hs = 3.0;              // <= 4.0, ok
  in.wind_speed_kn = 30.0;            // <= 40.0, ok
  in.current_state = EnvelopeState::Out;

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::HeaveTo, result->type);
  EXPECT_DOUBLE_EQ(2.0, result->speed_cmd_kn);
}

/// 7. SelectMooredWhenAlreadyBerthed — already moored.
TEST(MrcTriggerLogicTest, SelectMooredWhenAlreadyBerthed) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = true;
  in.is_moored = true;
  in.current_state = EnvelopeState::Out;

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::Moored, result->type);
  EXPECT_DOUBLE_EQ(0.0, result->speed_cmd_kn);
}

// ===========================================================================
// Predicate tests
// ===========================================================================

/// 8. AnchorPredicateTrue — can_anchor returns true for valid conditions.
TEST(MrcTriggerLogicTest, AnchorPredicateTrue) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());
  EXPECT_TRUE(logic->can_anchor(30.0, true));
  EXPECT_TRUE(logic->can_anchor(50.0, true));   // exactly at limit
}

/// 9. AnchorPredicateFalse — can_anchor returns false (too deep / no zone).
TEST(MrcTriggerLogicTest, AnchorPredicateFalse) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());
  EXPECT_FALSE(logic->can_anchor(51.0, true));   // too deep
  EXPECT_FALSE(logic->can_anchor(30.0, false));  // not in anchorage
  EXPECT_FALSE(logic->can_anchor(100.0, false)); // both fail
}

/// 10. HeaveToPredicateTrue — can_heave_to returns true for valid conditions.
TEST(MrcTriggerLogicTest, HeaveToPredicateTrue) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());
  EXPECT_TRUE(logic->can_heave_to(4.0, 40.0));   // exactly at limits
  EXPECT_TRUE(logic->can_heave_to(2.0, 20.0));
}

/// 11. HeaveToPredicateFalse — can_heave_to returns false.
TEST(MrcTriggerLogicTest, HeaveToPredicateFalse) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());
  EXPECT_FALSE(logic->can_heave_to(4.1, 40.0));   // sea state too high
  EXPECT_FALSE(logic->can_heave_to(3.0, 41.0));   // too windy
  EXPECT_FALSE(logic->can_heave_to(5.0, 50.0));   // both fail
}

// ===========================================================================
// M7 interaction and priority order
// ===========================================================================

/// 12. M7RecommendationOverridesDefault — M7's recommended MRM used.
TEST(MrcTriggerLogicTest, M7RecommendationOverridesDefault) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  // Default priority would select MOORED (is_moored=true).
  // M7 recommends ANCHOR, which is feasible -> ANCHOR should win.
  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = true;
  in.m7_recommended_mrm = MrcType::Anchor;
  in.is_moored = true;                   // default says MOORED
  in.water_depth_m = 30.0;               // anchor feasible
  in.in_anchorage_zone = true;
  in.sea_state_hs = 10.0;                // heave-to not feasible
  in.wind_speed_kn = 100.0;
  in.current_state = EnvelopeState::Out;

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::Anchor, result->type)
      << "M7 recommendation of ANCHOR should override default MOORED";
}

/// 13. MrcPriorityOrder — anchor > heave_to > drift.
TEST(MrcTriggerLogicTest, MrcPriorityOrder) {
  auto logic = MrcTriggerLogic::create(MakeDefaultParams());
  ASSERT_TRUE(logic.has_value());

  // All conditions feasible: anchor, heave_to, and drift.
  MrcSelectionInputs in = MakeNormalInputs();
  in.m7_safety_mrc_required = false;     // no M7 override
  in.is_moored = false;
  in.water_depth_m = 30.0;               // anchor feasible
  in.in_anchorage_zone = true;
  in.sea_state_hs = 2.0;                 // heave-to feasible
  in.wind_speed_kn = 20.0;
  in.current_state = EnvelopeState::Out; // force MRC via state

  auto result = logic->select(in);
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(MrcType::Anchor, result->type)
      << "ANCHOR has higher priority than HEAVE_TO or DRIFT";
}

}  // namespace
}  // namespace mass_l3::m1
