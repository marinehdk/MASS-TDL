#include "m3_mission_manager/replan_request_trigger.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace mass_l3::m3 {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ReplanRequestTrigger::ReplanRequestTrigger(ReplanTriggerConfig config)
    : config_(config) {}

// ---------------------------------------------------------------------------
// evaluate — check 4 reasons in priority order
// ---------------------------------------------------------------------------

ReplanDecision ReplanRequestTrigger::evaluate(
    const l3_msgs::msg::ODDState& odd_state,
    double current_eta_s,
    double planned_eta_s,
    int32_t replan_attempt_count,
    std::chrono::steady_clock::time_point now) const {
  // Priority order: MRC_REQUIRED > ODD_EXIT > MISSION_INFEASIBLE > CONGESTION

  // 1. MRC required (highest priority)
  if (check_mrc_required(odd_state)) {
    return ReplanDecision{true,
                          ReplanReason::MrcRequired,
                          config_.deadline_mrc_required_s,
                          "MRC required: envelope state signals MRC transition"};
  }

  // 2. ODD exit (debounced)
  if (check_odd_exit(static_cast<double>(odd_state.conformance_score), now)) {
    if (odd_state.conformance_score < static_cast<float>(config_.odd_critical_threshold)) {
      return ReplanDecision{true,
                            ReplanReason::OddExit,
                            config_.deadline_odd_exit_critical_s,
                            "ODD exit: conformance score critically low"};
    }
    return ReplanDecision{true,
                          ReplanReason::OddExit,
                          config_.deadline_odd_exit_degraded_s,
                          "ODD exit: conformance score degraded below threshold"};
  }

  // 3. Mission infeasible
  if (check_mission_infeasible(current_eta_s, planned_eta_s)) {
    return ReplanDecision{true,
                          ReplanReason::MissionInfeasible,
                          config_.deadline_mission_infeasible_s,
                          "Mission infeasible: current ETA exceeds planned ETA by margin"};
  }

  // 4. Congestion
  if (check_congestion(replan_attempt_count)) {
    return ReplanDecision{true,
                          ReplanReason::Congestion,
                          config_.deadline_congestion_s,
                          "Replan congestion: too many consecutive replan attempts"};
  }

  // No trigger
  return ReplanDecision{false, ReplanReason::OddExit, 0.0, ""};
}

// ---------------------------------------------------------------------------
// check_odd_exit — debounced check of conformance score
// ---------------------------------------------------------------------------

bool ReplanRequestTrigger::check_odd_exit(
    double conformance_score,
    std::chrono::steady_clock::time_point now) const {
  // Critical score triggers immediately
  if (conformance_score < config_.odd_critical_threshold) {
    degraded_since_.reset();
    return true;
  }

  // Degraded score: apply debounce
  if (conformance_score < config_.odd_degraded_threshold) {
    if (!degraded_since_.has_value()) {
      degraded_since_ = now;
      return false;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - degraded_since_.value());
    if (elapsed.count() >= config_.odd_degraded_buffer_s) {
      return true;
    }
    return false;
  }

  // Score is normal: reset debounce timer
  degraded_since_.reset();
  return false;
}

// ---------------------------------------------------------------------------
// check_mission_infeasible — ETA exceeds planned + margin
// ---------------------------------------------------------------------------

bool ReplanRequestTrigger::check_mission_infeasible(
    double current_eta_s,
    double planned_eta_s) const {
  return (current_eta_s > planned_eta_s + config_.eta_infeasible_margin_s);
}

// ---------------------------------------------------------------------------
// check_mrc_required — ODD envelope signals MRC
// ---------------------------------------------------------------------------

bool ReplanRequestTrigger::check_mrc_required(
    const l3_msgs::msg::ODDState& odd) const {
  return (odd.envelope_state == l3_msgs::msg::ODDState::ENVELOPE_MRC_PREP ||
          odd.envelope_state == l3_msgs::msg::ODDState::ENVELOPE_MRC_ACTIVE);
}

// ---------------------------------------------------------------------------
// check_congestion — too many consecutive replan attempts
// ---------------------------------------------------------------------------

bool ReplanRequestTrigger::check_congestion(
    int32_t replan_attempt_count) const {
  return (replan_attempt_count >= config_.attempt_max_count);
}

}  // namespace mass_l3::m3
