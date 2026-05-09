#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {

struct ReplanTriggerConfig {
  double odd_degraded_threshold;        // [TBD-HAZID] 0.7
  double odd_critical_threshold;        // [TBD-HAZID] 0.3
  double odd_degraded_buffer_s;         // [TBD-HAZID] 1.0
  double eta_infeasible_margin_s;       // [TBD-HAZID] 600.0
  int32_t attempt_max_count;            // [TBD-HAZID] 3
  double deadline_mrc_required_s;       // [TBD-HAZID] 30.0
  double deadline_odd_exit_critical_s;  // [TBD-HAZID] 60.0
  double deadline_odd_exit_degraded_s;  // [TBD-HAZID] 120.0
  double deadline_mission_infeasible_s; // [TBD-HAZID] 120.0
  double deadline_congestion_s;         // [TBD-HAZID] 300.0
};

struct ReplanDecision {
  bool should_trigger;
  ReplanReason reason;
  double deadline_s;
  std::string rationale;
};

class ReplanRequestTrigger {
 public:
  explicit ReplanRequestTrigger(ReplanTriggerConfig config);
  ~ReplanRequestTrigger() = default;
  ReplanRequestTrigger(const ReplanRequestTrigger&) = delete;
  ReplanRequestTrigger& operator=(const ReplanRequestTrigger&) = delete;
  ReplanRequestTrigger(ReplanRequestTrigger&&) = default;
  ReplanRequestTrigger& operator=(ReplanRequestTrigger&&) = default;

  /// Evaluate whether replan should be triggered.
  [[nodiscard]] ReplanDecision evaluate(
      const l3_msgs::msg::ODDState& odd_state,
      double current_eta_s,
      double planned_eta_s,
      int32_t replan_attempt_count,
      std::chrono::steady_clock::time_point now) const;

  /// Reset internal state (e.g., after successful replan).
  void reset_degraded_timer() { degraded_since_.reset(); }

 private:
  // 4 reason evaluators
  [[nodiscard]] bool check_odd_exit(double conformance_score,
                                    std::chrono::steady_clock::time_point now) const;
  [[nodiscard]] bool check_mission_infeasible(double current_eta_s,
                                               double planned_eta_s) const;
  [[nodiscard]] bool check_mrc_required(const l3_msgs::msg::ODDState& odd) const;
  [[nodiscard]] bool check_congestion(int32_t replan_attempt_count) const;

  ReplanTriggerConfig config_;
  mutable std::optional<std::chrono::steady_clock::time_point> degraded_since_;
};

}  // namespace mass_l3::m3
