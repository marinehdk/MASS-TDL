// W2: M3 RouteReceived wiring verification
//
// Validates the 4-step FSM path:
//   Init → Idle → TaskValidation → AwaitingRoute → Active
// All tests should pass on baseline (state machine logic is correct).
// The real W2 deliverable is the [M3 FSM] structured logging added to the node.
//
// NOTE: plan's test_route_received.cpp used reset() which sets state to Idle,
// not Init. Corrected here: use NodeReady to drive Init→Idle properly.

#include <gtest/gtest.h>

#include "m3_mission_manager/mission_state_machine.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {

class MissionStateTransitionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MissionStateMachineConfig cfg;
    cfg.distance_completion_m = 50.0;
    state_machine_ = std::make_unique<MissionStateMachine>(cfg);
  }

  std::unique_ptr<MissionStateMachine> state_machine_;
};

// 1. Init → Idle on NodeReady
TEST_F(MissionStateTransitionTest, StateTransitionInit_to_Idle) {
  EXPECT_EQ(state_machine_->current(), MissionState::Init);

  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);

  EXPECT_EQ(state_machine_->current(), MissionState::Idle);
}

// 2. Idle → TaskValidation on VoyageTaskReceived
TEST_F(MissionStateTransitionTest, StateTransitionIdle_to_TaskValidation) {
  // Drive to Idle via NodeReady
  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);
  EXPECT_EQ(state_machine_->current(), MissionState::Idle);

  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);

  EXPECT_EQ(state_machine_->current(), MissionState::TaskValidation);
}

// 3. TaskValidation → AwaitingRoute on ValidationPassed
TEST_F(MissionStateTransitionTest, StateTransitionTaskValidation_to_AwaitingRoute) {
  // Drive to TaskValidation
  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);
  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);
  EXPECT_EQ(state_machine_->current(), MissionState::TaskValidation);

  MissionEvent pass_event;
  pass_event.type = MissionEvent::Type::ValidationPassed;
  state_machine_->handle_event(pass_event);

  EXPECT_EQ(state_machine_->current(), MissionState::AwaitingRoute);
}

// 4. AwaitingRoute → Active on RouteReceived (W2 critical path)
TEST_F(MissionStateTransitionTest, StateTransitionAwaitingRoute_to_Active) {
  // Drive to AwaitingRoute
  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);
  MissionEvent task_event;
  task_event.type = MissionEvent::Type::VoyageTaskReceived;
  state_machine_->handle_event(task_event);
  MissionEvent pass_event;
  pass_event.type = MissionEvent::Type::ValidationPassed;
  state_machine_->handle_event(pass_event);
  EXPECT_EQ(state_machine_->current(), MissionState::AwaitingRoute);

  // W2 critical: RouteReceived → Active
  MissionEvent route_event;
  route_event.type = MissionEvent::Type::RouteReceived;
  state_machine_->handle_event(route_event);

  EXPECT_EQ(state_machine_->current(), MissionState::Active);
  EXPECT_TRUE(state_machine_->has_active_mission());
}

}  // namespace mass_l3::m3
