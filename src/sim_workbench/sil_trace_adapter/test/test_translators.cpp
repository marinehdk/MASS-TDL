// Unit tests for sil_trace_adapter translators (Track A A5b).
// Pure field-map verification — no ROS spinning.
#include <gtest/gtest.h>

#include <string>

#include "sil_trace_adapter/translators.hpp"

using sil_trace_adapter::asdr_record_to_event;

namespace {
void fill_stamp(builtin_interfaces::msg::Time& t, uint32_t sec) {
  t.sec = sec;
  t.nanosec = 0u;
}
}  // namespace

TEST(AsdrRecordToEvent, StampPropagated) {
  l3_msgs::msg::ASDRRecord rec;
  fill_stamp(rec.stamp, 123u);
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.stamp.sec, 123u);
}

TEST(AsdrRecordToEvent, DecisionTypeBecomesEventType) {
  l3_msgs::msg::ASDRRecord rec;
  rec.decision_type = "encounter_classification";
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.event_type, "encounter_classification");
}

TEST(AsdrRecordToEvent, SourceModuleBecomesRuleRef) {
  l3_msgs::msg::ASDRRecord rec;
  rec.source_module = "M7_Safety_Supervisor";
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.rule_ref, "M7_Safety_Supervisor");
}

TEST(AsdrRecordToEvent, DecisionJsonBecomesPayloadJson) {
  l3_msgs::msg::ASDRRecord rec;
  rec.decision_json = "{\"rule\":\"13\"}";
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.payload_json, "{\"rule\":\"13\"}");
}

TEST(AsdrRecordToEvent, RationaleBecomesDecisionId) {
  l3_msgs::msg::ASDRRecord rec;
  rec.rationale = "SAT-2 summary text";
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.decision_id, "SAT-2 summary text");
}

TEST(AsdrRecordToEvent, RationaleTruncatedTo64Chars) {
  l3_msgs::msg::ASDRRecord rec;
  rec.rationale = std::string(100, 'x');  // 100 chars
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.decision_id.size(), 64u);
  EXPECT_EQ(out.decision_id, std::string(64, 'x'));
}

TEST(AsdrRecordToEvent, EmptyRationaleGivesEmptyDecisionId) {
  l3_msgs::msg::ASDRRecord rec;
  // rationale defaults to empty
  auto out = asdr_record_to_event(rec);
  EXPECT_EQ(out.decision_id, "");
}

TEST(AsdrRecordToEvent, VerdictDefaultsToPass) {
  l3_msgs::msg::ASDRRecord rec;
  auto out = asdr_record_to_event(rec);
  // ASDREvent verdict enum: 0=UNSPECIFIED 1=PASS 2=RISK 3=FAIL.
  // Bridge sets 0 (PASS) as the default downstream-audit value.
  EXPECT_EQ(out.verdict, 0u);
}
