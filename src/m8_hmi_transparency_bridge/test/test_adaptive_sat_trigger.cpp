// test/test_adaptive_sat_trigger.cpp
#include <algorithm>

#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/adaptive_sat_trigger.hpp"

using mass_l3::m8::AdaptiveSatTrigger;
using mass_l3::m8::SatAggregator;
using mass_l3::m8::SatTriggerDecision;

namespace {

AdaptiveSatTrigger::Thresholds default_thresholds()
{
  return AdaptiveSatTrigger::Thresholds{30.0, 0.6, 0.7F, 0.8F};
}

l3_msgs::msg::ODDState make_odd(float tdl_s)
{
  l3_msgs::msg::ODDState m{};
  m.tdl_s = tdl_s;
  m.tmr_s = tdl_s + 30.0F;
  m.current_zone = l3_msgs::msg::ODDState::ODD_ZONE_A;
  return m;
}

l3_msgs::msg::SafetyAlert make_alert(uint8_t severity, uint8_t type)
{
  l3_msgs::msg::SafetyAlert a{};
  a.severity = severity;
  a.alert_type = type;
  return a;
}

l3_msgs::msg::COLREGsConstraint make_colreg(bool conflict)
{
  l3_msgs::msg::COLREGsConstraint c{};
  c.conflict_detected = conflict;
  c.confidence = 0.95F;
  return c;
}

bool has_reason(const SatTriggerDecision& d, const std::string& reason)
{
  return std::find(d.sat2_trigger_reasons.begin(),
                   d.sat2_trigger_reasons.end(), reason)
         != d.sat2_trigger_reasons.end();
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1 (spec): normal transit — only SAT-1 and SAT-3 visible, no SAT-2.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, NormalTransitOnlySat1And3Visible)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat1_visible);
  EXPECT_FALSE(d.sat2_visible);
  EXPECT_TRUE(d.sat3_visible);
  EXPECT_FALSE(d.sat3_priority_high);
  EXPECT_EQ(d.sat3_alert_color, "normal");
  EXPECT_TRUE(d.sat2_trigger_reasons.empty());
}

// ---------------------------------------------------------------------------
// Test 2 (spec): TDL < 30 s → sat3_priority_high + bold_red.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, TdlBelow30sPromotesSat3)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto d = trig.decide(make_odd(25.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat3_priority_high);
  EXPECT_EQ(d.sat3_alert_color, "bold_red");
  EXPECT_TRUE(d.sat1_visible);
  EXPECT_TRUE(d.sat3_visible);
}

// ---------------------------------------------------------------------------
// Test 3 (spec): M7 SOTIF alert (severity=WARNING) → SAT-2 with reason.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, M7SotifAlertTriggersSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_WARNING,
                          l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
  auto d = trig.decide(make_odd(90.0F), agg,
                       alert, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(has_reason(d, "m7_sotif_warning"));
}

// ---------------------------------------------------------------------------
// Test 4 (spec): operator request → SAT-2 even without any other condition.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, OperatorRequestTriggersSat2EvenWithoutConflict)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, std::nullopt,
                       true /*operator_requested*/, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(has_reason(d, "operator_request"));
}

// ---------------------------------------------------------------------------
// Test 5 (add): COLREGs conflict_detected=true → SAT-2 with "colreg_conflict".
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, ColregConflictTriggersSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto colreg = make_colreg(true);
  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, colreg,
                       false, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(has_reason(d, "colreg_conflict"));
}

// ---------------------------------------------------------------------------
// Test 6 (add): no conflict (conflict_detected=false) → no SAT-2.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, ColregNoConflict_NulloptDoesNotTriggerSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto colreg_ok = make_colreg(false);
  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, colreg_ok,
                       false, now);

  EXPECT_FALSE(d.sat2_visible);
  EXPECT_FALSE(has_reason(d, "colreg_conflict"));
}

// ---------------------------------------------------------------------------
// Test 7 (add): M7 alert severity below WARNING → does NOT trigger SAT-2.
//              (severity=0 is below SEVERITY_WARNING=1)
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, M7AlertSeverityBelowWarning_NoSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  // Construct an alert with severity 0 (SEVERITY_INFO, below SEVERITY_WARNING = 1)
  l3_msgs::msg::SafetyAlert low_alert{};
  low_alert.severity = l3_msgs::msg::SafetyAlert::SEVERITY_INFO;
  low_alert.alert_type = l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION;

  auto d = trig.decide(make_odd(90.0F), agg,
                       low_alert, std::nullopt,
                       false, now);

  EXPECT_FALSE(d.sat2_visible);
}

// ---------------------------------------------------------------------------
// Test 8 (add): system confidence drop from sat_cache → SAT-2 with reason.
//              Ingest M7 data with system_confidence=0.4 (< threshold 0.6).
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, SystemConfidenceDrop_TriggersSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  l3_msgs::msg::SATData sat_msg{};
  sat_msg.source_module = "M7";
  sat_msg.sat2.system_confidence = 0.4F;
  auto now = SatAggregator::Clock::now();
  agg.ingest(sat_msg, now);

  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(has_reason(d, "system_confidence_drop"));
}

// ---------------------------------------------------------------------------
// Test 9 (add): system confidence above threshold → no "system_confidence_drop".
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, SystemConfidenceAboveThreshold_NoDrop)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  l3_msgs::msg::SATData sat_msg{};
  sat_msg.source_module = "M1";
  sat_msg.sat2.system_confidence = 0.85F;  // above threshold 0.6
  auto now = SatAggregator::Clock::now();
  agg.ingest(sat_msg, now);

  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_FALSE(d.sat2_visible);
  EXPECT_FALSE(has_reason(d, "system_confidence_drop"));
}

// ---------------------------------------------------------------------------
// Test 10 (add): multiple conditions fire simultaneously — all reasons reported.
//               colreg_conflict + m7_sotif_warning + operator_request + TDL low.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, MultipleConditions_AllReasonsPresent)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto alert = make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL,
                          l3_msgs::msg::SafetyAlert::ALERT_PERFORMANCE_DEGRADED);
  auto colreg = make_colreg(true);

  auto d = trig.decide(make_odd(10.0F), agg,
                       alert, colreg,
                       true /*operator_requested*/, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(d.sat3_priority_high);
  EXPECT_EQ(d.sat3_alert_color, "bold_red");
  EXPECT_TRUE(has_reason(d, "colreg_conflict"));
  EXPECT_TRUE(has_reason(d, "m7_sotif_warning"));
  EXPECT_TRUE(has_reason(d, "operator_request"));
  EXPECT_EQ(d.sat2_trigger_reasons.size(), 3U);
}

// ---------------------------------------------------------------------------
// Test 11 (add): TDL exactly at threshold (30.0s) is NOT promoted (strict <).
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, TdlExactlyAtThreshold_NotPromoted)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  auto d = trig.decide(make_odd(30.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_FALSE(d.sat3_priority_high);
  EXPECT_EQ(d.sat3_alert_color, "normal");
}

// ---------------------------------------------------------------------------
// Test 12 (add): TDL trigger also fires SAT-2 "system_confidence_drop" via
//               system_confidence_dropped path — verify both independent.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, TdlPromotion_IndependentOfSat2)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  auto now = SatAggregator::Clock::now();

  // TDL below threshold but no SAT-2 conditions
  auto d = trig.decide(make_odd(5.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat3_priority_high);
  EXPECT_EQ(d.sat3_alert_color, "bold_red");
  EXPECT_FALSE(d.sat2_visible);  // TDL alone does NOT trigger SAT-2
  EXPECT_TRUE(d.sat2_trigger_reasons.empty());
}

// ---------------------------------------------------------------------------
// Test 13 (add): test-access helpers delegate to private methods correctly.
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, TestHelpers_DelegateToPrivateMethods)
{
  AdaptiveSatTrigger trig(default_thresholds());

  // has_colreg_conflict_for_test
  EXPECT_FALSE(trig.has_colreg_conflict_for_test(std::nullopt));
  EXPECT_TRUE(trig.has_colreg_conflict_for_test(make_colreg(true)));
  EXPECT_FALSE(trig.has_colreg_conflict_for_test(make_colreg(false)));

  // m7_alert_triggers_sat2_for_test
  EXPECT_FALSE(trig.m7_alert_triggers_sat2_for_test(std::nullopt));
  EXPECT_TRUE(trig.m7_alert_triggers_sat2_for_test(
      make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_WARNING,
                 l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT)));
  EXPECT_TRUE(trig.m7_alert_triggers_sat2_for_test(
      make_alert(l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL,
                 l3_msgs::msg::SafetyAlert::ALERT_PERFORMANCE_DEGRADED)));
}

// ---------------------------------------------------------------------------
// Test 14 (add): system confidence drop: ANY module below threshold fires.
//               Ingest M2 with low confidence (not M7).
// ---------------------------------------------------------------------------
TEST(AdaptiveSatTrigger, SystemConfidenceDrop_AnyModuleTriggered)
{
  AdaptiveSatTrigger trig(default_thresholds());
  SatAggregator agg;
  l3_msgs::msg::SATData sat_msg{};
  sat_msg.source_module = "M2";
  sat_msg.sat2.system_confidence = 0.3F;  // clearly below 0.6
  auto now = SatAggregator::Clock::now();
  agg.ingest(sat_msg, now);

  auto d = trig.decide(make_odd(90.0F), agg,
                       std::nullopt, std::nullopt,
                       false, now);

  EXPECT_TRUE(d.sat2_visible);
  EXPECT_TRUE(has_reason(d, "system_confidence_drop"));
}
