#include <gtest/gtest.h>

#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"

using namespace mass_l3::m7::arbitrator;

// ---------------------------------------------------------------------------
// Helper: build a stamp
// ---------------------------------------------------------------------------
static builtin_interfaces::msg::Time make_stamp(int32_t sec, uint32_t nanosec)
{
  builtin_interfaces::msg::Time t{};
  t.sec     = sec;
  t.nanosec = nanosec;
  return t;
}

// ---------------------------------------------------------------------------
// Test 1: build_safety_alert — correct alert_type, severity, mrm, confidence
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSafetyAlert_FieldsCorrect)
{
  auto const stamp = make_stamp(1000, 0U);
  auto const msg = AlertGenerator::build_safety_alert(
    stamp,
    SafetyAlertParams{
      l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT,
      l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL,
      "MRM-01-DRIFT",
      0.90F,
      "watchdog timeout",
      "M2 heartbeat lost"});

  EXPECT_EQ(msg.alert_type,      l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT);
  EXPECT_EQ(msg.severity,        l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
  EXPECT_EQ(msg.recommended_mrm, "MRM-01-DRIFT");
  EXPECT_FLOAT_EQ(msg.confidence, 0.90F);
  EXPECT_EQ(msg.rationale,       "watchdog timeout");
  EXPECT_EQ(msg.description,     "M2 heartbeat lost");
}

// ---------------------------------------------------------------------------
// Test 2: build_asdr_record — source_module, decision_type correct;
//          signature is 32 bytes (SHA-256 of decision_json)
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildAsdrRecord_FieldsAndSignature)
{
  auto const stamp = make_stamp(2000, 500U);
  auto const msg = AlertGenerator::build_asdr_record(
    stamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "mrm_triggered",
      "mrm_id=MRM-01-DRIFT confidence=0.90"});

  EXPECT_EQ(msg.stamp.sec,       2000);
  EXPECT_EQ(msg.stamp.nanosec,   500U);
  EXPECT_EQ(msg.source_module,   "M7_Safety_Supervisor");
  EXPECT_EQ(msg.decision_type,   "mrm_triggered");
  EXPECT_EQ(msg.decision_json,   "mrm_id=MRM-01-DRIFT confidence=0.90");
  EXPECT_EQ(msg.signature.size(), 32U);
  // Verify signature is idempotent (same input -> same digest)
  auto const msg2 = AlertGenerator::build_asdr_record(
    stamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "mrm_triggered",
      "mrm_id=MRM-01-DRIFT confidence=0.90"});
  EXPECT_EQ(msg.signature, msg2.signature);
}

// ---------------------------------------------------------------------------
// Test 3: build_sat_data — state_summary and source_module set
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSatData_StateSummarySet)
{
  auto const stamp = make_stamp(3000, 0U);
  auto const msg = AlertGenerator::build_sat_data(
    stamp,
    "M7: nominal",
    0.85F,
    "periodic");

  EXPECT_EQ(msg.stamp.sec,          3000);
  EXPECT_EQ(msg.source_module,      "M7_Safety_Supervisor");
  EXPECT_EQ(msg.sat1.state_summary, "M7: nominal");
  EXPECT_EQ(msg.sat2.trigger_reason, "periodic");
  EXPECT_FLOAT_EQ(msg.sat2.system_confidence, 0.85F);
}

// ---------------------------------------------------------------------------
// Test 4: build_safety_alert with SEVERITY_MRC_REQUIRED
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSafetyAlert_MrcRequired)
{
  auto const stamp = make_stamp(4000, 0U);
  auto const msg = AlertGenerator::build_safety_alert(
    stamp,
    SafetyAlertParams{
      l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION,
      l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED,
      "MRM-01-DRIFT",
      0.95F,
      "extreme scenario",
      "3+ SOTIF assumptions violated"});

  EXPECT_EQ(msg.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
  EXPECT_EQ(msg.alert_type, l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
}

// ---------------------------------------------------------------------------
// Test 5: build_asdr_record with long decision_summary
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildAsdrRecord_LongDecisionSummary)
{
  auto const stamp = make_stamp(5000, 0U);
  std::string const long_summary(512, 'x');
  auto const msg = AlertGenerator::build_asdr_record(
    stamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "periodic_health_check",
      long_summary});

  EXPECT_EQ(msg.decision_json, long_summary);
  EXPECT_EQ(msg.signature.size(), 32U);
  // Verify signature is idempotent
  auto const msg2 = AlertGenerator::build_asdr_record(
    stamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "periodic_health_check",
      long_summary});
  EXPECT_EQ(msg.signature, msg2.signature);
}

// ---------------------------------------------------------------------------
// Test 6: build_sat_data confidence range stays within [0, 1]
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSatData_ConfidenceRange)
{
  auto const stamp = make_stamp(6000, 0U);

  auto const low = AlertGenerator::build_sat_data(stamp, "state", 0.0F, "test");
  EXPECT_FLOAT_EQ(low.sat2.system_confidence, 0.0F);

  auto const high = AlertGenerator::build_sat_data(stamp, "state", 1.0F, "test");
  EXPECT_FLOAT_EQ(high.sat2.system_confidence, 1.0F);

  auto const mid = AlertGenerator::build_sat_data(stamp, "state", 0.5F, "test");
  EXPECT_FLOAT_EQ(mid.sat2.system_confidence, 0.5F);
}
