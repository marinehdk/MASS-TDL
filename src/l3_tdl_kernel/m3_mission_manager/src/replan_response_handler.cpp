#include "m3_mission_manager/replan_response_handler.hpp"

#include <string>

namespace mass_l3::m3 {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ReplanResponseHandler::ReplanResponseHandler(ReplanResponseHandlerConfig config)
    : config_(config) {}

// ---------------------------------------------------------------------------
// handle_response — RFC-006 4-status branching
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_response(
    const l3_external_msgs::msg::ReplanResponse& response,
    int32_t attempt_count) const {
  switch (response.status) {
    case l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS:
      return handle_success();
    case l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT:
      return handle_failed_timeout(attempt_count);
    case l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_INFEASIBLE:
      return handle_failed_infeasible();
    case l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_NO_RESOURCES:
      return handle_failed_no_resources();

    default:
      return ReplanOutcome{false,
                           true,
                           "unknown replan response status: " +
                               std::to_string(static_cast<int>(response.status))};
  }
}

// ---------------------------------------------------------------------------
// handle_success — STATUS_SUCCESS: L2 accepted the request.
// Per spec RFC-006: new PlannedRoute arrives separately on /l2/planned_route,
// not embedded in ReplanResponse. Accept unconditionally here.
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_success() const {
  return ReplanOutcome{true, false, "replan accepted by L2"};
}

// ---------------------------------------------------------------------------
// handle_failed_timeout — retry-eligible; escalate only when exhausted
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_failed_timeout(int32_t attempt_count) const {
  if (attempt_count >= config_.attempt_max_count) {
    return ReplanOutcome{false,
                         true,
                         "replan failed: timeout, max attempts (" +
                             std::to_string(config_.attempt_max_count) +
                             ") reached"};
  }
  return ReplanOutcome{false,
                       false,
                       "replan failed: timeout, attempt " +
                           std::to_string(attempt_count + 1) + "/" +
                           std::to_string(config_.attempt_max_count) +
                           " — retrying"};
}

// ---------------------------------------------------------------------------
// handle_failed_infeasible — route cannot be found, escalate immediately
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_failed_infeasible() const {
  return ReplanOutcome{false, true, "L2: route infeasible"};
}

// ---------------------------------------------------------------------------
// handle_failed_no_resources — L2 overloaded, escalate immediately
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_failed_no_resources() const {
  return ReplanOutcome{false, true, "L2: no resources available"};
}

}  // namespace mass_l3::m3
