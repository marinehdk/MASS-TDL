#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "fixtures/route_fixtures.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
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
// 1. SuccessWithValidRoute — STATUS_SUCCESS + new route -> success
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessWithValidRoute) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);
  std::optional<l3_external_msgs::msg::PlannedRoute> new_route =
      make_valid_route(42);

  const auto outcome = handler.handle_response(response, new_route, 0);

  EXPECT_TRUE(outcome.success);
  EXPECT_FALSE(outcome.escalate_to_mrc);
  EXPECT_FALSE(outcome.rationale.empty());
}

// ---------------------------------------------------------------------------
// 2. SuccessWithoutRoute — STATUS_SUCCESS without route -> escalate
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessWithoutRoute) {
  const auto config = make_default_replan_config();
  ReplanResponseHandler handler(config);

  const auto response = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);
  const std::optional<l3_external_msgs::msg::PlannedRoute> no_route =
      std::nullopt;

  const auto outcome = handler.handle_response(response, no_route, 0);

  EXPECT_FALSE(outcome.success);
  EXPECT_TRUE(outcome.escalate_to_mrc);
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
  const std::optional<l3_external_msgs::msg::PlannedRoute> no_route =
      std::nullopt;

  const auto outcome = handler.handle_response(response, no_route, 1);

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
  const std::optional<l3_external_msgs::msg::PlannedRoute> no_route =
      std::nullopt;

  const auto outcome = handler.handle_response(response, no_route, 3);

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

  const auto outcome = handler.handle_response(
      response, std::nullopt, 0);

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

  const auto outcome = handler.handle_response(
      response, std::nullopt, 0);

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
    const auto outcome = handler.handle_response(response, std::nullopt, 1);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("retrying") != std::string::npos);
  }

  // attempt 2/3: below max, no escalate
  {
    const auto outcome = handler.handle_response(response, std::nullopt, 2);
    EXPECT_FALSE(outcome.success);
    EXPECT_FALSE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("retrying") != std::string::npos);
  }

  // attempt 3/3: at max, escalate
  {
    const auto outcome = handler.handle_response(response, std::nullopt, 3);
    EXPECT_FALSE(outcome.success);
    EXPECT_TRUE(outcome.escalate_to_mrc);
    EXPECT_TRUE(outcome.rationale.find("max") != std::string::npos);
  }
}

// ---------------------------------------------------------------------------
// 8. SuccessAfterAttempts — later success accepted after failures
// ---------------------------------------------------------------------------
TEST(ReplanResponseHandlerTest, SuccessAfterAttempts) {
  const auto config = make_default_replan_config();  // max = 3
  ReplanResponseHandler handler(config);

  const auto timeout_resp = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT);
  const auto success_resp = make_replan_response(
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS);

  // Two failures, then success
  auto outcome1 = handler.handle_response(timeout_resp, std::nullopt, 1);
  EXPECT_FALSE(outcome1.success);
  EXPECT_FALSE(outcome1.escalate_to_mrc);

  auto outcome2 = handler.handle_response(timeout_resp, std::nullopt, 2);
  EXPECT_FALSE(outcome2.success);
  EXPECT_FALSE(outcome2.escalate_to_mrc);

  // Success on third attempt
  std::optional<l3_external_msgs::msg::PlannedRoute> new_route =
      make_valid_route(99);
  auto outcome3 = handler.handle_response(
      success_resp, new_route, 0);  // attempt_count resets for success

  EXPECT_TRUE(outcome3.success);
  EXPECT_FALSE(outcome3.escalate_to_mrc);
  EXPECT_FALSE(outcome3.rationale.empty());
}

}  // namespace
}  // namespace mass_l3::m3
