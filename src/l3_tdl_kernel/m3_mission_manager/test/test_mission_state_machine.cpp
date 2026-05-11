#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#include "m3_mission_manager/mission_state_machine.hpp"
#include "m3_mission_manager/replan_response_handler.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

MissionStateMachineConfig make_default_msm_config() {
  return MissionStateMachineConfig{50.0};  // distance_completion_m
}

MissionEvent make_event(MissionEvent::Type type) {
  return MissionEvent{type, std::nullopt};
}

MissionEvent make_replan_event(const ReplanOutcome& outcome) {
  return MissionEvent{
      MissionEvent::Type::ReplanResponseReceived,
      std::optional<ReplanOutcome>(outcome)};
}

// ---------------------------------------------------------------------------
// 1. InitialStateIsInit — after construction state = Init
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, InitialStateIsInit) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  EXPECT_EQ(msm.current(), MissionState::Init);
  EXPECT_EQ(msm.state_name(), "Init");
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 2. InitToIdle — NodeReady event transitions Init -> Idle
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, InitToIdle) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::NodeReady));

  EXPECT_EQ(result, MissionState::Idle);
  EXPECT_EQ(msm.current(), MissionState::Idle);
  EXPECT_EQ(msm.state_name(), "Idle");
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 3. IdleToTaskValidation — VoyageTaskReceived -> TaskValidation
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, IdleToTaskValidation) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  // Idle -> TaskValidation
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::VoyageTaskReceived));

  EXPECT_EQ(result, MissionState::TaskValidation);
  EXPECT_EQ(msm.current(), MissionState::TaskValidation);
  EXPECT_EQ(msm.state_name(), "TaskValidation");
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 4. TaskValidationToAwaitingRoute — ValidationPassed -> AwaitingRoute
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, TaskValidationToAwaitingRoute) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  // TaskValidation -> AwaitingRoute
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::ValidationPassed));

  EXPECT_EQ(result, MissionState::AwaitingRoute);
  EXPECT_EQ(msm.current(), MissionState::AwaitingRoute);
  EXPECT_EQ(msm.state_name(), "AwaitingRoute");
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 5. AwaitingRouteToActive — RouteReceived -> Active
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, AwaitingRouteToActive) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  // AwaitingRoute -> Active
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::RouteReceived));

  EXPECT_EQ(result, MissionState::Active);
  EXPECT_EQ(msm.current(), MissionState::Active);
  EXPECT_EQ(msm.state_name(), "Active");
  EXPECT_TRUE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 6. ActiveToReplanWait — ReplanTriggered -> ReplanWait
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, ActiveToReplanWait) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  // Active -> ReplanWait
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::ReplanTriggered));

  EXPECT_EQ(result, MissionState::ReplanWait);
  EXPECT_EQ(msm.current(), MissionState::ReplanWait);
  EXPECT_EQ(msm.state_name(), "ReplanWait");
  EXPECT_TRUE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 7. ReplanWaitToActive — success outcome -> back to Active
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, ReplanWaitToActive) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active -> ReplanWait
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  msm.handle_event(make_event(MissionEvent::Type::ReplanTriggered));
  // ReplanWait -> Active (success)
  const ReplanOutcome success_outcome{true, false, "replan succeeded"};
  const auto result = msm.handle_event(make_replan_event(success_outcome));

  EXPECT_EQ(result, MissionState::Active);
  EXPECT_EQ(msm.current(), MissionState::Active);
  EXPECT_TRUE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 8. ReplanWaitToMrcTransit — escalate outcome -> MrcTransit
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, ReplanWaitToMrcTransit) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active -> ReplanWait
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  msm.handle_event(make_event(MissionEvent::Type::ReplanTriggered));
  // ReplanWait -> MrcTransit (escalate)
  const ReplanOutcome escalate_outcome{false, true, "replan failed"};
  const auto result = msm.handle_event(make_replan_event(escalate_outcome));

  EXPECT_EQ(result, MissionState::MrcTransit);
  EXPECT_EQ(msm.current(), MissionState::MrcTransit);
  EXPECT_EQ(msm.state_name(), "MrcTransit");
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 9. ActiveToIdleMissionComplete — MissionComplete -> Idle
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, ActiveToIdleMissionComplete) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  // Active -> Idle
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::MissionComplete));

  EXPECT_EQ(result, MissionState::Idle);
  EXPECT_EQ(msm.current(), MissionState::Idle);
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 10. MrcTransitToIdle — MrcComplete -> Idle
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, MrcTransitToIdle) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active -> ReplanWait
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  msm.handle_event(make_event(MissionEvent::Type::ReplanTriggered));
  // ReplanWait -> MrcTransit
  const ReplanOutcome escalate_outcome{false, true, "replan failed"};
  msm.handle_event(make_replan_event(escalate_outcome));
  // MrcTransit -> Idle
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::MrcComplete));

  EXPECT_EQ(result, MissionState::Idle);
  EXPECT_EQ(msm.current(), MissionState::Idle);
  EXPECT_FALSE(msm.has_active_mission());
}

// ---------------------------------------------------------------------------
// 11. InvalidTransitionReturnsError — unexpected event in current state
// ---------------------------------------------------------------------------
TEST(MissionStateMachineTest, InvalidTransitionReturnsError) {
  const auto config = make_default_msm_config();
  MissionStateMachine msm(config);

  // Init -> Idle -> TaskValidation -> AwaitingRoute -> Active
  msm.handle_event(make_event(MissionEvent::Type::NodeReady));
  msm.handle_event(make_event(MissionEvent::Type::VoyageTaskReceived));
  msm.handle_event(make_event(MissionEvent::Type::ValidationPassed));
  msm.handle_event(make_event(MissionEvent::Type::RouteReceived));
  // State is now Active

  // VoyageTaskReceived is only valid from Idle — ignored in Active
  const auto result = msm.handle_event(make_event(
      MissionEvent::Type::VoyageTaskReceived));

  EXPECT_EQ(result, MissionState::Active);
  EXPECT_EQ(msm.current(), MissionState::Active);
}

}  // namespace
}  // namespace mass_l3::m3
