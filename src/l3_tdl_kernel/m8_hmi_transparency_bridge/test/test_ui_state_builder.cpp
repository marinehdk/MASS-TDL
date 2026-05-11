// test/test_ui_state_builder.cpp
#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/ui_state_builder.hpp"

using mass_l3::m8::SatAggregator;
using mass_l3::m8::SatTriggerDecision;
using mass_l3::m8::TorProtocol;
using mass_l3::m8::UiStateBuilder;

namespace {

UiStateBuilder::BuildContext make_transit_ctx(UiStateBuilder::Role role)
{
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = role;
  ctx.scenario = UiStateBuilder::Scenario::kTransit;
  ctx.sat_decision.sat1_visible = true;
  ctx.sat_decision.sat2_visible = false;
  ctx.sat_decision.sat3_visible = true;
  ctx.tor_state = TorProtocol::State::kIdle;
  ctx.tor_remaining_s = 0.0;
  ctx.override_active = false;
  ctx.m7_degradation_alert_active = false;
  return ctx;
}

l3_msgs::msg::ODDState make_odd(const std::string& zone_reason, float tdl_s = 90.0F)
{
  l3_msgs::msg::ODDState odd{};
  odd.zone_reason = zone_reason;
  odd.tdl_s = tdl_s;
  odd.tmr_s = tdl_s + 30.0F;
  odd.conformance_score = 0.95F;
  return odd;
}

l3_msgs::msg::SafetyAlert make_alert(uint8_t severity)
{
  l3_msgs::msg::SafetyAlert a{};
  a.severity = severity;
  a.description = "Test alert";
  return a;
}

l3_msgs::msg::BehaviorPlan make_behavior(float confidence)
{
  l3_msgs::msg::BehaviorPlan b{};
  b.behavior = l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT;
  b.confidence = confidence;
  b.rationale = "IvP solved";
  return b;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1 (spec): Transit scenario for ShipCaptain → active_alert_count hidden
// (Captain sees simplified view; SAT-detail suppressed)
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, TransitScenarioProducesSat1OnlyForCaptain)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kShipCaptain);
  ctx.latest_alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_WARNING);

  auto msg = builder.build(ctx, sat_cache);

  // Captain view: alert count hidden (0) in transit — simplified view
  EXPECT_EQ(msg.active_alert_count, 0U);
  EXPECT_EQ(msg.view_mode, "dashboard");
}

// ---------------------------------------------------------------------------
// Test 2 (spec): MRC preparation → rationale contains MRC context info;
// sat3_priority_high is in SatTriggerDecision (not UIState.msg), so check
// rationale and view_mode for correct MRC scenario handling.
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, MrcPreparationShowsRadarViewAndRationaleContainsToRInfo)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kRocOperator;
  ctx.scenario = UiStateBuilder::Scenario::kMrcPreparation;
  ctx.sat_decision.sat1_visible = true;
  ctx.sat_decision.sat2_visible = true;
  ctx.sat_decision.sat3_visible = true;
  ctx.sat_decision.sat3_priority_high = true;
  ctx.sat_decision.sat3_alert_color = "bold_red";
  ctx.tor_state = TorProtocol::State::kRequested;
  ctx.tor_remaining_s = 45.0;
  ctx.override_active = false;
  ctx.m7_degradation_alert_active = false;
  ctx.odd = make_odd("ODD_B");

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.view_mode, "radar");
  // auto_level_text must contain scenario info
  EXPECT_NE(msg.auto_level_text.find("mrc_preparation"), std::string::npos);
  // rationale must contain ToR state context
  EXPECT_NE(msg.rationale.find("requested"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 3 (spec): Override active + M7 degradation alert present
// → rationale mentions override; view_mode == "chart"
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, OverrideActiveShowsM7DegradationAlert)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kRocOperator;
  ctx.scenario = UiStateBuilder::Scenario::kOverrideActive;
  ctx.sat_decision.sat1_visible = true;
  ctx.tor_state = TorProtocol::State::kIdle;
  ctx.tor_remaining_s = 0.0;
  ctx.override_active = true;
  ctx.m7_degradation_alert_active = true;
  ctx.m7_degradation_alert_text = "IEC61508 fault: sensor degraded";

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.view_mode, "chart");
  EXPECT_NE(msg.rationale.find("override"), std::string::npos);
  EXPECT_NE(msg.rationale.find("IEC61508"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 4: ROC operator in transit → alert count populated from latest_alert
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, RocOperatorTransitShowsAlertCount)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  ctx.latest_alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_WARNING);

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.active_alert_count, 1U);
  EXPECT_EQ(msg.critical_alert_count, 0U);
}

// ---------------------------------------------------------------------------
// Test 5: Critical alert → critical_alert_count == 1
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, CriticalAlertSetsBothCounters)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  ctx.latest_alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.active_alert_count, 1U);
  EXPECT_EQ(msg.critical_alert_count, 1U);
}

// ---------------------------------------------------------------------------
// Test 6: No alert → both counts are 0
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, NoAlertKeepsCountsZero)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  // no latest_alert

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.active_alert_count, 0U);
  EXPECT_EQ(msg.critical_alert_count, 0U);
}

// ---------------------------------------------------------------------------
// Test 7: Confidence comes from BehaviorPlan when available
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, ConfidenceFromBehaviorPlan)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  ctx.behavior = make_behavior(0.87F);

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_FLOAT_EQ(msg.confidence, 0.87F);
}

// ---------------------------------------------------------------------------
// Test 8: No BehaviorPlan → confidence is 0
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, ConfidenceIsZeroWithoutBehaviorPlan)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  // no behavior

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_FLOAT_EQ(msg.confidence, 0.0F);
}

// ---------------------------------------------------------------------------
// Test 9: ODD present → auto_level_text contains zone_reason
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, AutoLevelTextContainsZoneReasonWhenOddPresent)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  ctx.odd = make_odd("open_water");

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_NE(msg.auto_level_text.find("open_water"), std::string::npos);
  EXPECT_NE(msg.auto_level_text.find("transit"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 10: No ODD → auto_level_text still contains scenario string
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, AutoLevelTextFallsBackToScenarioWhenNoOdd)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  auto ctx = make_transit_ctx(UiStateBuilder::Role::kRocOperator);
  // no odd

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_NE(msg.auto_level_text.find("transit"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 11: COLREGs avoidance scenario → view_mode == "radar"
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, ColregAvoidanceScenarioHasRadarViewMode)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kRocOperator;
  ctx.scenario = UiStateBuilder::Scenario::kColregAvoidance;
  ctx.tor_state = TorProtocol::State::kIdle;

  auto msg = builder.build(ctx, sat_cache);

  EXPECT_EQ(msg.view_mode, "radar");
}

// ---------------------------------------------------------------------------
// Test 12: MRC active scenario (ship captain) → alerts NOT hidden
// (MRC scenarios always show full regardless of role)
// ---------------------------------------------------------------------------
TEST(UiStateBuilder, MrcActiveScenarioShowsAlertsEvenForCaptain)
{
  UiStateBuilder builder;
  SatAggregator sat_cache;
  UiStateBuilder::BuildContext ctx{};
  ctx.now = SatAggregator::Clock::now();
  ctx.role = UiStateBuilder::Role::kShipCaptain;
  ctx.scenario = UiStateBuilder::Scenario::kMrcActive;
  ctx.tor_state = TorProtocol::State::kAcknowledged;
  ctx.tor_remaining_s = 0.0;
  ctx.override_active = false;
  ctx.m7_degradation_alert_active = false;
  ctx.latest_alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_WARNING);

  auto msg = builder.build(ctx, sat_cache);

  // MRC scenarios always show full → alert must appear
  EXPECT_EQ(msg.active_alert_count, 1U);
}
