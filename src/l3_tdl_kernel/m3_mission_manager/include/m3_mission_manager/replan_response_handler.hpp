#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "l3_external_msgs/msg/replan_response.hpp"
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
  /// Per spec: STATUS_SUCCESS → accept (REPLAN_WAIT→ACTIVE); new PlannedRoute
  /// arrives separately on /l2/planned_route, not attached to this response.
  [[nodiscard]] ReplanOutcome handle_response(
      const l3_external_msgs::msg::ReplanResponse& response,
      int32_t attempt_count) const;

 private:
  [[nodiscard]] ReplanOutcome handle_success() const;
  [[nodiscard]] ReplanOutcome handle_failed_timeout(int32_t attempt_count) const;
  [[nodiscard]] ReplanOutcome handle_failed_infeasible() const;
  [[nodiscard]] ReplanOutcome handle_failed_no_resources() const;

  ReplanResponseHandlerConfig config_;
};

}  // namespace mass_l3::m3
