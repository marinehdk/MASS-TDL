#include "m3_mission_manager/replan_response_handler.hpp"

#include <string>
#include <utility>

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
    const std::optional<l3_external_msgs::msg::PlannedRoute>& new_route,
    int32_t attempt_count) {
  switch (response.status) {
    case l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS:
      return handle_success(new_route);
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
// handle_success — SUCCESS with or without valid route
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_success(
    const std::optional<l3_external_msgs::msg::PlannedRoute>& new_route) {
  if (new_route.has_value() &&
      new_route->route_id > 0 &&
      new_route->total_distance_nm > 0.0) {
    return ReplanOutcome{true, false, "route accepted"};
  }
  return ReplanOutcome{false, true, "no valid route provided"};
}

// ---------------------------------------------------------------------------
// handle_failed_timeout — retry-eligible; escalate only when exhausted
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_failed_timeout(int32_t attempt_count) {
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

ReplanOutcome ReplanResponseHandler::handle_failed_infeasible() {
  return ReplanOutcome{false, true, "L2: route infeasible"};
}

// ---------------------------------------------------------------------------
// handle_failed_no_resources — L2 overloaded, escalate immediately
// ---------------------------------------------------------------------------

ReplanOutcome ReplanResponseHandler::handle_failed_no_resources() {
  return ReplanOutcome{false, true, "L2: no resources available"};
}

}  // namespace mass_l3::m3
