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

  /// Reset to Idle.
  void reset();

 private:
  MissionState transit_(MissionState next);

  MissionState state_;
  MissionStateMachineConfig config_;
};

}  // namespace mass_l3::m3
