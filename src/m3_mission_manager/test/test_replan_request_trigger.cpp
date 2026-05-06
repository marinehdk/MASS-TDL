#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <thread>

#include "l3_msgs/msg/odd_state.hpp"
#include "m3_mission_manager/replan_request_trigger.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {
namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

ReplanTriggerConfig make_default_replan_config() {
  return ReplanTriggerConfig{
      0.7,    // odd_degraded_threshold
      0.3,    // odd_critical_threshold
      1.0,    // odd_degraded_buffer_s
      600.0,  // eta_infeasible_margin_s
      3,      // attempt_max_count
      30.0,   // deadline_mrc_required_s
      60.0,   // deadline_odd_exit_critical_s
      120.0,  // deadline_odd_exit_degraded_s
      120.0,  // deadline_mission_infeasible_s
      300.0   // deadline_congestion_s
  };
}

l3_msgs::msg::ODDState make_odd_state(
    float conformance_score = 0.9f,
    uint8_t envelope_state = l3_msgs::msg::ODDState::ENVELOPE_IN) {
  l3_msgs::msg::ODDState msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.current_zone = l3_msgs::msg::ODDState::ODD_ZONE_A;
  msg.auto_level = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
  msg.health = l3_msgs::msg::ODDState::HEALTH_FULL;
  msg.envelope_state = envelope_state;
  msg.conformance_score = conformance_score;
  msg.tmr_s = 60.0f;
  msg.tdl_s = 10.0f;
  msg.confidence = 0.95f;
  msg.rationale = "test odd state";
  return msg;
}

// ---------------------------------------------------------------------------
// 1. NoTriggerWhenScoreNormal — conformance 0.9 -> no trigger
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, NoTriggerWhenScoreNormal) {
  const auto config = make_default_replan_config();
  const ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.9f);

  const auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
  EXPECT_FALSE(result.should_trigger);
}

// ---------------------------------------------------------------------------
// 2. TriggerOddExitWhenScoreDegraded — conformance 0.5 for > 1s -> ODD_EXIT
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, TriggerOddExitWhenScoreDegraded) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.5f);

  // First call: score < threshold but debounce not expired
  auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
  EXPECT_FALSE(result.should_trigger);

  // Second call 2 seconds later: debounce expired
  const auto later = now + std::chrono::seconds(2);
  result = trigger.evaluate(odd, 3600.0, 3600.0, 0, later);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::OddExit);
  EXPECT_GT(result.deadline_s, 0.0);
  EXPECT_FALSE(result.rationale.empty());
}

// ---------------------------------------------------------------------------
// 3. TriggerMissionInfeasibleWhenEtaExceeded — ETA > planned + margin
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, TriggerMissionInfeasibleWhenEtaExceeded) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.9f);

  // ETA 1000s, planned 300s, margin 600s -> 1000 > 300 + 600 = 900 -> trigger
  const auto result = trigger.evaluate(odd, 1000.0, 300.0, 0, now);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::MissionInfeasible);
  EXPECT_DOUBLE_EQ(result.deadline_s, config.deadline_mission_infeasible_s);
}

// ---------------------------------------------------------------------------
// 4. TriggerMrCRequiredFromOddState — envelope state signals MRC
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, TriggerMrCRequiredFromOddState) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();

  // MRC_PREP should trigger
  {
    const auto odd = make_odd_state(
        0.9f, l3_msgs::msg::ODDState::ENVELOPE_MRC_PREP);
    const auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
    EXPECT_TRUE(result.should_trigger);
    EXPECT_EQ(result.reason, ReplanReason::MrCRequired);
    EXPECT_DOUBLE_EQ(result.deadline_s, config.deadline_mrc_required_s);
  }

  // MRC_ACTIVE should also trigger
  {
    const auto odd = make_odd_state(
        0.9f, l3_msgs::msg::ODDState::ENVELOPE_MRC_ACTIVE);
    const auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
    EXPECT_TRUE(result.should_trigger);
    EXPECT_EQ(result.reason, ReplanReason::MrCRequired);
  }
}

// ---------------------------------------------------------------------------
// 5. TriggerCongestionAfterMaxAttempts — attempt count >= max
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, TriggerCongestionAfterMaxAttempts) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.9f);

  // attempt_max_count = 3, so count = 3 should trigger
  const auto result = trigger.evaluate(odd, 3600.0, 3600.0, 3, now);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::Congestion);
  EXPECT_DOUBLE_EQ(result.deadline_s, config.deadline_congestion_s);
}

// ---------------------------------------------------------------------------
// 6. CriticalScoreTriggersImmediately — score < 0.3 -> immediate trigger
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, CriticalScoreTriggersImmediately) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.2f);  // below critical threshold 0.3

  // Should trigger immediately without debounce
  const auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::OddExit);
  EXPECT_DOUBLE_EQ(result.deadline_s, config.deadline_odd_exit_critical_s);
}

// ---------------------------------------------------------------------------
// 7. ResetDegradedTimer — reset clears degraded state
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, ResetDegradedTimer) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.5f);  // degraded

  // First call starts debounce timer
  auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
  EXPECT_FALSE(result.should_trigger);

  // Reset timer
  trigger.reset_degraded_timer();

  // Even after a long time, timer was reset so should not trigger
  const auto later = now + std::chrono::seconds(10);
  result = trigger.evaluate(odd, 3600.0, 3600.0, 0, later);
  EXPECT_FALSE(result.should_trigger);  // timer restarted, not yet expired
}

// ---------------------------------------------------------------------------
// 8. OddExitRespectsDebounce — score < 0.7 but < buffer -> no trigger
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, OddExitRespectsDebounce) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();
  const auto odd = make_odd_state(0.5f);  // below degraded (0.7) but above critical (0.3)

  // Call at t=0: starts debounce, no trigger
  auto result = trigger.evaluate(odd, 3600.0, 3600.0, 0, now);
  EXPECT_FALSE(result.should_trigger);

  // Call at t=0.5s: still within debounce buffer (1.0s), no trigger
  const auto t_half = now + std::chrono::milliseconds(500);
  result = trigger.evaluate(odd, 3600.0, 3600.0, 0, t_half);
  EXPECT_FALSE(result.should_trigger);

  // Call at t=1.5s: debounce expired, should trigger
  const auto t_over = now + std::chrono::milliseconds(1500);
  result = trigger.evaluate(odd, 3600.0, 3600.0, 0, t_over);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::OddExit);
}

// ---------------------------------------------------------------------------
// 9. MrcRequiredHasHighestPriority — MRC overrides ETA issues
// ---------------------------------------------------------------------------
TEST(ReplanRequestTriggerTest, MrcRequiredHasHighestPriority) {
  const auto config = make_default_replan_config();
  ReplanRequestTrigger trigger(config);

  const auto now = std::chrono::steady_clock::now();

  // Both MRC and mission infeasible conditions present
  // MRC should take priority
  const auto odd = make_odd_state(
      0.9f, l3_msgs::msg::ODDState::ENVELOPE_MRC_PREP);
  const auto result = trigger.evaluate(odd, 5000.0, 100.0, 5, now);
  EXPECT_TRUE(result.should_trigger);
  EXPECT_EQ(result.reason, ReplanReason::MrCRequired);
}

}  // namespace
}  // namespace mass_l3::m3
