/// Unit tests for W9 — M1 M3 ACTIVE stale watchdog.
///
/// Verifies:
///   - Watchdog increments each 4 Hz tick (0.25 s per tick)
///   - Watchdog resets on task_validity = VALID
///   - Watchdog emits SafetyConcernEvent at configured threshold
///   - Watchdog stops emitting after reset
///   - Non-ACTIVE state does not trigger watchdog

#include <gtest/gtest.h>

#include <cmath>
#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/mission_goal.hpp"
#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

namespace {

class M3ActiveWatchdogTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }

  void SetUp() override {
    node_ = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();
    node_->params_.m3_route_stale_threshold_s = 2.0;
    node_->m3_active_duration_s_ = 0.0;
    node_->watchdog_concern_emitted_ = false;
    node_->last_mission_goal_.reset();
  }

  void TearDown() override {
    node_.reset();
  }

  void set_active_not_valid() {
    auto mg = std::make_shared<l3_msgs::msg::MissionGoal>();
    mg->fsm_state = l3_msgs::msg::MissionGoal::FSM_ACTIVE;
    mg->task_validity = l3_msgs::msg::MissionGoal::TASK_VALIDITY_PENDING;
    node_->last_mission_goal_ = mg;
  }

  void set_active_valid() {
    auto mg = std::make_shared<l3_msgs::msg::MissionGoal>();
    mg->fsm_state = l3_msgs::msg::MissionGoal::FSM_ACTIVE;
    mg->task_validity = l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID;
    node_->last_mission_goal_ = mg;
  }

  void set_idle() {
    auto mg = std::make_shared<l3_msgs::msg::MissionGoal>();
    mg->fsm_state = l3_msgs::msg::MissionGoal::FSM_IDLE;
    mg->task_validity = l3_msgs::msg::MissionGoal::TASK_VALIDITY_PENDING;
    node_->last_mission_goal_ = mg;
  }

  void tick_n_times(int n) {
    for (int i = 0; i < n; ++i) {
      node_->on_main_loop_tick();
    }
  }

  std::shared_ptr<mass_l3::m1::OddEnvelopeManagerNode> node_;
};

}  // namespace

TEST_F(M3ActiveWatchdogTest, WatchdogIncrementsEachTick) {
  set_active_not_valid();

  EXPECT_DOUBLE_EQ(node_->m3_active_duration_s_, 0.0);

  tick_n_times(4);

  EXPECT_NEAR(node_->m3_active_duration_s_, 1.0, 1e-6);
}

TEST_F(M3ActiveWatchdogTest, WatchdogResetsOnTaskValidityValid) {
  set_active_not_valid();
  tick_n_times(4);
  EXPECT_NEAR(node_->m3_active_duration_s_, 1.0, 1e-6);

  set_active_valid();
  node_->on_main_loop_tick();

  EXPECT_DOUBLE_EQ(node_->m3_active_duration_s_, 0.0);
}

TEST_F(M3ActiveWatchdogTest, WatchdogEmitsSafetyConcernAtThreshold) {
  node_->params_.m3_route_stale_threshold_s = 0.5;
  set_active_not_valid();

  tick_n_times(2);
  EXPECT_FALSE(node_->watchdog_concern_emitted_);

  tick_n_times(1);
  EXPECT_TRUE(node_->watchdog_concern_emitted_);
  EXPECT_NEAR(node_->m3_active_duration_s_, 0.75, 1e-6);
}

TEST_F(M3ActiveWatchdogTest, WatchdogStopsEmittingAfterReset) {
  node_->params_.m3_route_stale_threshold_s = 0.5;
  set_active_not_valid();

  tick_n_times(3);
  ASSERT_TRUE(node_->watchdog_concern_emitted_);
  EXPECT_NEAR(node_->m3_active_duration_s_, 0.75, 1e-6);

  set_active_valid();
  node_->on_main_loop_tick();
  EXPECT_DOUBLE_EQ(node_->m3_active_duration_s_, 0.0);
  EXPECT_FALSE(node_->watchdog_concern_emitted_);

  set_active_not_valid();
  tick_n_times(2);
  EXPECT_FALSE(node_->watchdog_concern_emitted_);
  EXPECT_NEAR(node_->m3_active_duration_s_, 0.5, 1e-6);
}

TEST_F(M3ActiveWatchdogTest, NonActiveStateDoesNotTriggerWatchdog) {
  set_idle();

  tick_n_times(40);

  EXPECT_DOUBLE_EQ(node_->m3_active_duration_s_, 0.0);
  EXPECT_FALSE(node_->watchdog_concern_emitted_);
}

TEST_F(M3ActiveWatchdogTest, SafetyConcernPublishedWhenThresholdExceeded) {
  auto goal = std::make_shared<l3_msgs::msg::MissionGoal>();
  goal->fsm_state = l3_msgs::msg::MissionGoal::FSM_ACTIVE;
  goal->task_validity = l3_msgs::msg::MissionGoal::TASK_VALIDITY_PENDING;
  node_->on_mission_goal(goal);

  int ticks_needed = static_cast<int>(std::ceil(
      node_->params_.m3_route_stale_threshold_s / 0.25)) + 2;
  for (int i = 0; i < ticks_needed; ++i) {
    node_->on_main_loop_tick();
  }

  EXPECT_TRUE(node_->watchdog_concern_emitted_);
  EXPECT_GT(node_->m3_active_duration_s_, node_->params_.m3_route_stale_threshold_s);
  EXPECT_NE(node_->safety_concern_pub_, nullptr);
}
