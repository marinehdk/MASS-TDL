#include "m3_mission_manager/mission_state_machine.hpp"

#include <array>
#include <string>
#include <string_view>

namespace mass_l3::m3 {

namespace {

// State names for diagnostics / SAT-3 transparency.
constexpr std::array<std::string_view, 7> kStateNames = {{
    "Init",           // 0
    "Idle",           // 1
    "TaskValidation",  // 2
    "AwaitingRoute",  // 3
    "Active",         // 4
    "ReplanWait",     // 5
    "MrcTransit",     // 6
}};

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MissionStateMachine::MissionStateMachine(MissionStateMachineConfig config)
    : state_(MissionState::Init), config_(config) {}

// ---------------------------------------------------------------------------
// state_name — human-readable state name
// ---------------------------------------------------------------------------

std::string_view MissionStateMachine::state_name() const {
  const auto idx = static_cast<uint8_t>(state_);
  if (idx < kStateNames.size()) {
    return kStateNames[idx];
  }
  return "Unknown";
}

// ---------------------------------------------------------------------------
// reset — reset to Idle (but go through the clean transition)
// ---------------------------------------------------------------------------

void MissionStateMachine::reset() {
  state_ = MissionState::Idle;
}

// ---------------------------------------------------------------------------
// handle_event — process event and transition state per spec §3.5
// ---------------------------------------------------------------------------

MissionState MissionStateMachine::handle_event(const MissionEvent& event) {
  switch (state_) {
    // INIT → IDLE: any first event triggers initialisation
    case MissionState::Init:
      return transit_(MissionState::Idle);

    // IDLE: waiting for a voyage task
    case MissionState::Idle:
      if (event.type == MissionEvent::Type::VoyageTaskReceived) {
        return transit_(MissionState::TaskValidation);
      }
      break;

    // TASK_VALIDATION: waiting for validation result
    case MissionState::TaskValidation:
      if (event.type == MissionEvent::Type::ValidationFailed) {
        return transit_(MissionState::Idle);
      }
      if (event.type == MissionEvent::Type::ValidationPassed) {
        return transit_(MissionState::AwaitingRoute);
      }
      break;

    // AWAITING_ROUTE: waiting for L2 route
    case MissionState::AwaitingRoute:
      if (event.type == MissionEvent::Type::RouteReceived) {
        return transit_(MissionState::Active);
      }
      break;

    // ACTIVE: normal mission execution
    case MissionState::Active:
      if (event.type == MissionEvent::Type::ReplanTriggered) {
        return transit_(MissionState::ReplanWait);
      }
      if (event.type == MissionEvent::Type::MissionComplete) {
        return transit_(MissionState::Idle);
      }
      break;

    // REPLAN_WAIT: waiting for replan response or deadline
    case MissionState::ReplanWait:
      if (event.type == MissionEvent::Type::ReplanResponseReceived) {
        if (event.replan_outcome.has_value()) {
          if (event.replan_outcome->success) {
            return transit_(MissionState::Active);
          }
          if (event.replan_outcome->escalate_to_mrc) {
            return transit_(MissionState::MrcTransit);
          }
        }
        // No actionable outcome: stay in ReplanWait
        return state_;
      }
      if (event.type == MissionEvent::Type::ReplanDeadlineExpired) {
        return transit_(MissionState::MrcTransit);
      }
      break;

    // MRC_TRANSIT: minimum-risk condition transit
    case MissionState::MrcTransit:
      if (event.type == MissionEvent::Type::MrcComplete) {
        return transit_(MissionState::Idle);
      }
      break;
  }

  // Invalid transition: stay in current state
  return state_;
}

// ---------------------------------------------------------------------------
// transit_ — set state and return it (trace point for future instrumentation)
// ---------------------------------------------------------------------------

MissionState MissionStateMachine::transit_(MissionState next) {
  state_ = next;
  return state_;
}

}  // namespace mass_l3::m3
