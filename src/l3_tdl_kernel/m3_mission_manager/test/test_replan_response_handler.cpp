#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "l3_external_msgs/msg/replan_response.hpp"
#include "m3_mission_manager/replan_response_handler.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ReplanResponseHandlerConfig make_default_replan_config() {
  return ReplanResponseHandlerConfig{3};  // attempt_max_count
}

l3_external_msgs::msg::ReplanResponse make_replan_response(
    uint8_t status,
    const std::string& rationale = "test") {
  l3_external_msgs::msg::ReplanResponse msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.status = status;
  msg.failure_reason = rationale;
  msg.retry_recommended = false;
  msg.confidence = 0.95f;
  msg.rationale = rationale;
  return msg;
}

// ---------------------------------------------------------------------------
// 1. SuccessAccepted — STATUS_SUCCESS -> success (route arrives separately)
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessAccepted) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);

  const auto outcome = handler.handle_response(response, 0);

  EXPECT_TRUE(outcome.success);
  EXPECT_FALSE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 2. SuccessRouteArrivesSepa — STATUS_SUCCESS: per RFC-006 route comes
// separately on /l2/planned_route, not attached to ReplanResponse
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessRouteArrivesSepa) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);

  // Second invocation (e.g. attempt_count 0) also succeeds — route is not
  // part of the response, so the accept is unconditional.
  const auto outcome = handler.handle_response(response, 0);

  EXPECT_TRUE(outcome.success);
  EXPECT_FALSE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 3. FailedTimeoutBelowMax — STATUS_FAILED_TIMEOUT, attempt < max -> no esc
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, FailedTimeoutBelowMax) {
  const auto config = make_default_replan_config();  // max = 3
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT);

  const auto outcome = handler.handle_response(response, 1);

  EXPECT_FALSE(outcome.success);
  EXPECT_FALSE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 4. FailedTimeoutAtMax — STATUS_FAILED_TIMEOUT at max attempts -> escalate
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, FailedTimeoutAtMax) {
  const auto config = make_default_replan_config();  // max = 3
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT);

  const auto outcome = handler.handle_response(response, 3);

  EXPECT_FALSE(outcome.success);
  EXPECT_TRUE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 5. FailedInfeasible — STATUS_FAILED_INFEASIBLE -> escalate
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, FailedInfeasible) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_INFEASIBLE);

  const auto outcome = handler.handle_response(response, 0);

  EXPECT_FALSE(outcome.success);
  EXPECT_TRUE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 6. FailedNoResources — STATUS_FAILED_NO_RESOURCES -> escalate
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, FailedNoResources) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_NO_RESOURCES);

  const auto outcome = handler.handle_response(response, 0);

  EXPECT_FALSE(outcome.success);
  EXPECT_TRUE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 7. MultipleAttemptsIncrement — attempts tracked correctly
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, MultipleAttemptsIncrement) {
  const auto config = make_default_replan_config();  // max = 3
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT);

  // attempt 1/3: below max, no escalate
  {
    const auto outcome = handler.handle_response(response, 1);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("retrying") != std::string::npos);
  }

  // attempt 2/3: below max, no escalate
  {
    const auto outcome = handler.handle_response(response, 2);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("retrying") != std::string::npos);
  }

  // attempt 3/3: at max, escalate
  {
    const auto outcome = handler.handle_response(response, 3);
    EXPECT_FALSE(outcome.success);
    EXPECT_TRUE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("max") != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// 8. SuccessAfterAttempts — success accepted after previous failures
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessAfterAttempts) {
  const auto config = make_default_replan_config();  // max = 3
  ReplanResponseHandler handler(config);

  const auto timeout_resp = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT);
  const auto success_resp = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);

  // Two failures, then success
  auto outcome1 = handler.handle_response(timeout_resp, 1);
  EXPECT_FALSE(outcome1.success);
  EXPECT_FALSE(outcome1.escalate_to_mrc);

  auto outcome2 = handler.handle_response(timeout_resp, 2);
  EXPECT_FALSE(outcome2.success);
  EXPECT_FALSE(outcome2.escalate_to_mrc);

  // Success on third attempt (attempt_count resets to 0 for a success check)
  auto outcome3 = handler.handle_response(success_resp, 0);

  EXPECT_TRUE(outcome3.success);
  EXPECT_FALSE(outcome3.escalate_to_mrc);
  EXPECT_FALSE(outcome3.rationale.empty());
}

}  // namespace
}  // namespace mass_l3::m3
