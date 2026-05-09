// test/test_tor_request_generator.cpp
#include <gtest/gtest.h>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/to_r_request.hpp"
#include "m8_hmi_transparency_bridge/tor_protocol.hpp"
#include "m8_hmi_transparency_bridge/tor_request_generator.hpp"

using mass_l3::m8::TorProtocol;
using mass_l3::m8::ToRRequestGenerator;

namespace {

l3_msgs::msg::ODDState make_odd(const std::string& zone_reason)
{
  l3_msgs::msg::ODDState odd{};
  odd.zone_reason = zone_reason;
  odd.tdl_s = 60.0F;
  odd.tmr_s = 90.0F;
  odd.conformance_score = 0.85F;
  return odd;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: kOddExit → reason enum == REASON_ODD_EXIT (0)
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, OddExitSetsCorrectReasonEnum)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kOddExit, odd, "SAT-1 summary", 60.0);

  EXPECT_EQ(msg.reason, l3_msgs::msg::ToRRequest::REASON_ODD_EXIT);
}

// ---------------------------------------------------------------------------
// Test 2: kManualRequest → reason enum == REASON_MANUAL_REQUEST (1)
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, ManualRequestSetsCorrectReasonEnum)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("narrow_channel");
  auto msg = gen.generate(TorProtocol::Reason::kManualRequest, odd, "summary", 60.0);

  EXPECT_EQ(msg.reason, l3_msgs::msg::ToRRequest::REASON_MANUAL_REQUEST);
}

// ---------------------------------------------------------------------------
// Test 3: kSafetyAlert → reason enum == REASON_SAFETY_ALERT (2)
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, SafetyAlertSetsCorrectReasonEnum)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("harbour");
  auto msg = gen.generate(TorProtocol::Reason::kSafetyAlert, odd, "alert context", 60.0);

  EXPECT_EQ(msg.reason, l3_msgs::msg::ToRRequest::REASON_SAFETY_ALERT);
}

// ---------------------------------------------------------------------------
// Test 4: deadline_s correctly cast to float
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, DeadlineCorrectlyCastToFloat)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kOddExit, odd, "summary", 60.0);

  EXPECT_FLOAT_EQ(msg.deadline_s, 60.0F);
}

// ---------------------------------------------------------------------------
// Test 5: context_summary passed through unchanged
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, ContextSummaryPassedThrough)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  const std::string summary = "Target 42 on collision course, bearing 045";
  auto msg = gen.generate(TorProtocol::Reason::kOddExit, odd, summary, 60.0);

  EXPECT_EQ(msg.context_summary, summary);
}

// ---------------------------------------------------------------------------
// Test 6: recommended_action for kOddExit mentions odd.zone_reason
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, OddExitRecommendedActionMentionsZoneReason)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("restricted_visibility");
  auto msg = gen.generate(TorProtocol::Reason::kOddExit, odd, "summary", 60.0);

  EXPECT_NE(msg.recommended_action.find("restricted_visibility"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 7: recommended_action for kManualRequest has operator context text
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, ManualRequestRecommendedActionMentionsOperator)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kManualRequest, odd, "summary", 60.0);

  EXPECT_NE(msg.recommended_action.find("Operator"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 8: recommended_action for kSafetyAlert has safety takeover text
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, SafetyAlertRecommendedActionMentionsTakeover)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kSafetyAlert, odd, "alert ctx", 60.0);

  EXPECT_NE(msg.recommended_action.find("Safety"), std::string::npos);
}

// ---------------------------------------------------------------------------
// Test 9: target_level is TARGET_LEVEL_D2 by default
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, TargetLevelIsD2ByDefault)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kOddExit, odd, "summary", 60.0);

  EXPECT_EQ(msg.target_level, l3_msgs::msg::ToRRequest::TARGET_LEVEL_D2);
}

// ---------------------------------------------------------------------------
// Test 10: confidence is 0.9 (deterministic protocol action)
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, ConfidenceIsDeterministicValue)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kManualRequest, odd, "summary", 60.0);

  EXPECT_FLOAT_EQ(msg.confidence, 0.9F);
}

// ---------------------------------------------------------------------------
// Test 11: custom deadline (non-60) correctly propagated
// ---------------------------------------------------------------------------
TEST(ToRRequestGenerator, CustomDeadlinePropagated)
{
  ToRRequestGenerator gen;
  auto odd = make_odd("open_water");
  auto msg = gen.generate(TorProtocol::Reason::kSafetyAlert, odd, "summary", 45.0);

  EXPECT_FLOAT_EQ(msg.deadline_s, 45.0F);
}
