#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {

struct ReplanResponseHandlerConfig {
  int32_t attempt_max_count;       // [TBD-HAZID] 3
};

/// Result of handling a ReplanResponse.
struct ReplanOutcome {
  bool success;                    // true = route updated, back to ACTIVE
  bool escalate_to_mrc;            // true = failed, go to MRC_TRANSIT
  std::string rationale;
};

class ReplanResponseHandler {
 public:
  explicit ReplanResponseHandler(ReplanResponseHandlerConfig config);
  ~ReplanResponseHandler() = default;
  ReplanResponseHandler(const ReplanResponseHandler&) = delete;
  ReplanResponseHandler& operator=(const ReplanResponseHandler&) = delete;
  ReplanResponseHandler(ReplanResponseHandler&&) = default;
  ReplanResponseHandler& operator=(ReplanResponseHandler&&) = default;

  /// Process ReplanResponse per RFC-006 4-status branching.
  ReplanOutcome handle_response(
      const l3_external_msgs::msg::ReplanResponse& response,
      const std::optional<l3_external_msgs::msg::PlannedRoute>& new_route,
      int32_t attempt_count);

  /// Reset attempt counter (e.g., after successful cycle).
  void reset() { /* no-op for now */ }

 private:
  ReplanOutcome handle_success(
      const std::optional<l3_external_msgs::msg::PlannedRoute>& new_route);
  ReplanOutcome handle_failed_timeout(int32_t attempt_count);
  ReplanOutcome handle_failed_infeasible();
  ReplanOutcome handle_failed_no_resources();

  ReplanResponseHandlerConfig config_;
};

}  // namespace mass_l3::m3
