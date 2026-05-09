#include <gtest/gtest.h>

#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "builtin_interfaces/msg/time.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/sat_data.hpp"

using namespace mass_l3::m7::arbitrator;

namespace {

// ---------------------------------------------------------------------------
// Helper: build a stamp
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
builtin_interfaces::msg::Time make_stamp(int32_t sec, uint32_t nanosec)
{
  builtin_interfaces::msg::Time t{};
  t.sec     = sec;
  t.nanosec = nanosec;
  return t;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: build_safety_alert — correct alert_type, severity, mrm, confidence
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSafetyAlert_FieldsCorrect)
{
  auto const kStamp = make_stamp(1000, 0U);
  auto const kMsg = AlertGenerator::build_safety_alert(
    kStamp,
    SafetyAlertParams{
      l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT,
      l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL,
      "MRM-01-DRIFT",
      0.90F,
      "watchdog timeout",
      "M2 heartbeat lost"});

  EXPECT_EQ(kMsg.alert_type,      l3_msgs::msg::SafetyAlert::ALERT_IEC61508_FAULT);
  EXPECT_EQ(kMsg.severity,        l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
  EXPECT_EQ(kMsg.recommended_mrm, "MRM-01-DRIFT");
  EXPECT_FLOAT_EQ(kMsg.confidence, 0.90F);
  EXPECT_EQ(kMsg.rationale,       "watchdog timeout");
  EXPECT_EQ(kMsg.description,     "M2 heartbeat lost");
}

// ---------------------------------------------------------------------------
// Test 2: build_asdr_record — source_module, decision_type correct;
//          signature is 32 bytes (SHA-256 of decision_json)
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildAsdrRecord_FieldsAndSignature)
{
  auto const kStamp = make_stamp(2000, 500U);
  auto const kMsg = AlertGenerator::build_asdr_record(
    kStamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "mrm_triggered",
      "mrm_id=MRM-01-DRIFT confidence=0.90"});

  EXPECT_EQ(kMsg.stamp.sec,       2000);
  EXPECT_EQ(kMsg.stamp.nanosec,   500U);
  EXPECT_EQ(kMsg.source_module,   "M7_Safety_Supervisor");
  EXPECT_EQ(kMsg.decision_type,   "mrm_triggered");
  EXPECT_EQ(kMsg.decision_json,   "mrm_id=MRM-01-DRIFT confidence=0.90");
  EXPECT_EQ(kMsg.signature.size(), 32U);
  // Verify signature is idempotent (same input -> same digest)
  auto const kMsg2 = AlertGenerator::build_asdr_record(
    kStamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "mrm_triggered",
      "mrm_id=MRM-01-DRIFT confidence=0.90"});
  EXPECT_EQ(kMsg.signature, kMsg2.signature);
}

// ---------------------------------------------------------------------------
// Test 3: build_sat_data — state_summary and source_module set
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSatData_StateSummarySet)
{
  auto const kStamp = make_stamp(3000, 0U);
  auto const kMsg = AlertGenerator::build_sat_data(
    kStamp,
    "M7: nominal",
    0.85F,
    "periodic");

  EXPECT_EQ(kMsg.stamp.sec,          3000);
  EXPECT_EQ(kMsg.source_module,      "M7_Safety_Supervisor");
  EXPECT_EQ(kMsg.sat1.state_summary, "M7: nominal");
  EXPECT_EQ(kMsg.sat2.trigger_reason, "periodic");
  EXPECT_FLOAT_EQ(kMsg.sat2.system_confidence, 0.85F);
}

// ---------------------------------------------------------------------------
// Test 4: build_safety_alert with SEVERITY_MRC_REQUIRED
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSafetyAlert_MrcRequired)
{
  auto const kStamp = make_stamp(4000, 0U);
  auto const kMsg = AlertGenerator::build_safety_alert(
    kStamp,
    SafetyAlertParams{
      l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION,
      l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED,
      "MRM-01-DRIFT",
      0.95F,
      "extreme scenario",
      "3+ SOTIF assumptions violated"});

  EXPECT_EQ(kMsg.severity, l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
  EXPECT_EQ(kMsg.alert_type, l3_msgs::msg::SafetyAlert::ALERT_SOTIF_ASSUMPTION);
}

// ---------------------------------------------------------------------------
// Test 5: build_asdr_record with long decision_summary
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildAsdrRecord_LongDecisionSummary)
{
  auto const kStamp = make_stamp(5000, 0U);
  std::string const kLongSummary(512, 'x');
  auto const kMsg = AlertGenerator::build_asdr_record(
    kStamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "periodic_health_check",
      kLongSummary});

  EXPECT_EQ(kMsg.decision_json, kLongSummary);
  EXPECT_EQ(kMsg.signature.size(), 32U);
  // Verify signature is idempotent
  auto const kMsg2 = AlertGenerator::build_asdr_record(
    kStamp,
    AsdrRecordParams{
      "M7_Safety_Supervisor",
      "periodic_health_check",
      kLongSummary});
  EXPECT_EQ(kMsg.signature, kMsg2.signature);
}

// ---------------------------------------------------------------------------
// Test 6: build_sat_data confidence range stays within [0, 1]
// ---------------------------------------------------------------------------
TEST(AlertGeneratorTest, BuildSatData_ConfidenceRange)
{
  auto const kStamp = make_stamp(6000, 0U);

  auto const kLow = AlertGenerator::build_sat_data(kStamp, "state", 0.0F, "test");
  EXPECT_FLOAT_EQ(kLow.sat2.system_confidence, 0.0F);

  auto const kHigh = AlertGenerator::build_sat_data(kStamp, "state", 1.0F, "test");
  EXPECT_FLOAT_EQ(kHigh.sat2.system_confidence, 1.0F);

  auto const kMid = AlertGenerator::build_sat_data(kStamp, "state", 0.5F, "test");
  EXPECT_FLOAT_EQ(kMid.sat2.system_confidence, 0.5F);
}
