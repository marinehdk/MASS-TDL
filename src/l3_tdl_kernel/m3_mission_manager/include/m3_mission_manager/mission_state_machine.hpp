#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "m3_mission_manager/types.hpp"
#include "m3_mission_manager/error_codes.hpp"
#include "m3_mission_manager/replan_response_handler.hpp"

namespace mass_l3::m3 {

struct MissionStateMachineConfig {
  double distance_completion_m;  // [TBD-HAZID] 50.0
};

/// Events that drive state transitions.
struct MissionEvent {
  enum class Type : uint8_t {
    // Lifecycle: node setup complete, Init → Idle
    NodeReady,
    VoyageTaskReceived,
    ValidationPassed,
    ValidationFailed,
    RouteReceived,
    ReplanTriggered,
    ReplanResponseReceived,
    ReplanDeadlineExpired,
    MrcComplete,
    MissionComplete,
  };
  Type type;
  // Associated data (varies by type)
  std::optional<ReplanOutcome> replan_outcome;
};

/// Task validity status (substate within ACTIVE).
enum class TaskValidity : uint8_t {
  Pending = 0,   // Still evaluating conditions
  Valid = 1,     // All 4 conditions met
  Invalid = 2,   // At least one condition failed
  Replanning = 3 // In replan attempt
};

class MissionStateMachine {
 public:
  explicit MissionStateMachine(MissionStateMachineConfig config);
  ~MissionStateMachine() = default;
  MissionStateMachine(const MissionStateMachine&) = delete;
  MissionStateMachine& operator=(const MissionStateMachine&) = delete;

  /// Process an event and transition state. Returns current state.
  MissionState handle_event(const MissionEvent& event);

  /// Current state.
  [[nodiscard]] MissionState current() const { return state_; }

  /// Human-readable state name.
  [[nodiscard]] std::string_view state_name() const;

  /// Whether in a mission-active state (has task).
  [[nodiscard]] bool has_active_mission() const {
    return state_ == MissionState::Active ||
           state_ == MissionState::ReplanWait;
  }

  /// Current task validity substate (when in ACTIVE).
  [[nodiscard]] TaskValidity task_validity() const { return task_validity_; }

  [[nodiscard]] double distance_completion_m() const noexcept { return config_.distance_completion_m; }

  /// Check and update task validity based on conditions.
  /// Returns true if state changed.
  bool update_task_validity(
      bool has_l1_task, bool has_l2_route,
      bool has_enc_check, bool autonomy_ok);

  /// Reset to Idle.
  void reset();

 private:
  MissionState transit_(MissionState next);

  MissionState state_;
  TaskValidity task_validity_;
  MissionStateMachineConfig config_;
};

}  // namespace mass_l3::m3
