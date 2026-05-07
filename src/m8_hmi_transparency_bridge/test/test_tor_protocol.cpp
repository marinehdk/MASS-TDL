// test/test_tor_protocol.cpp
#include <chrono>

#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/tor_protocol.hpp"

using mass_l3::m8::TorProtocol;

namespace {

TorProtocol::Config default_cfg()
{
  return TorProtocol::Config{60.0, 5.0, 30.0, 1};
}

}  // namespace

// ---------------------------------------------------------------------------
// Spec test 1: IDLE at construction
// ---------------------------------------------------------------------------
TEST(TorProtocol, IdleAtConstruction)
{
  TorProtocol p(default_cfg());
  EXPECT_EQ(p.state(), TorProtocol::State::kIdle);
}

// ---------------------------------------------------------------------------
// Spec test 2: trigger → REQUESTED; second trigger rejected
// ---------------------------------------------------------------------------
TEST(TorProtocol, TriggerMovesToRequested)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  EXPECT_TRUE(p.trigger(TorProtocol::Reason::kOddExit, t0));
  EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
  EXPECT_FALSE(p.trigger(TorProtocol::Reason::kManualRequest, t0));
}

// ---------------------------------------------------------------------------
// Spec test 3: button disabled < 5s, enabled ≥ 5s
// ---------------------------------------------------------------------------
TEST(TorProtocol, ButtonDisabledWithin5sOfRequest)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  EXPECT_FALSE(p.is_acknowledgment_button_enabled(t0 + std::chrono::seconds(3)));
  EXPECT_TRUE(p.is_acknowledgment_button_enabled(t0 + std::chrono::seconds(6)));
}

// ---------------------------------------------------------------------------
// Spec test 4: click before 5s rejected (nullopt)
// ---------------------------------------------------------------------------
TEST(TorProtocol, ClickBefore5sIsRejected)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto snap = p.on_acknowledgment_clicked(
      t0 + std::chrono::seconds(3), {}, "ODD_A", 0.9F, "OP-001");
  EXPECT_FALSE(snap.has_value());
  EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
}

// ---------------------------------------------------------------------------
// Spec test 5: click after 5s → ACKNOWLEDGED + snapshot fields correct
// ---------------------------------------------------------------------------
TEST(TorProtocol, ClickAfter5sCapturesSnapshotAndAdvances)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_click = t0 + std::chrono::seconds(7);
  auto snap = p.on_acknowledgment_clicked(
      t_click, {"target_42", "target_57"}, "ODD_B", 0.72F, "OP-001");
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(p.state(), TorProtocol::State::kAcknowledged);
  EXPECT_DOUBLE_EQ(snap->sat1_display_duration_s, 7.0);
  EXPECT_EQ(snap->sat1_threats_visible.size(), 2U);
  EXPECT_EQ(snap->odd_zone_at_click, "ODD_B");
  EXPECT_FLOAT_EQ(snap->conformance_score_at_click, 0.72F);
}

// ---------------------------------------------------------------------------
// Spec test 6: tick at 61s → kTimeoutMrc
// ---------------------------------------------------------------------------
TEST(TorProtocol, Tick60sTimeoutLeadsToMrc)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  EXPECT_TRUE(p.tick(t0 + std::chrono::seconds(61)));
  EXPECT_EQ(p.state(), TorProtocol::State::kTimeoutMrc);
}

// ---------------------------------------------------------------------------
// Spec test 7: reset → IDLE
// ---------------------------------------------------------------------------
TEST(TorProtocol, ResetReturnsToIdle)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  p.reset();
  EXPECT_EQ(p.state(), TorProtocol::State::kIdle);
}

// ---------------------------------------------------------------------------
// Additional test 8: tick before deadline returns false
// ---------------------------------------------------------------------------
TEST(TorProtocol, TickBeforeDeadlineReturnsFalse)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  EXPECT_FALSE(p.tick(t0 + std::chrono::seconds(30)));
  EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
}

// ---------------------------------------------------------------------------
// Additional test 9: remaining_deadline_s correct at t+20s
// ---------------------------------------------------------------------------
TEST(TorProtocol, RemainingDeadlineCorrectAt20s)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  EXPECT_DOUBLE_EQ(p.remaining_deadline_s(t0 + std::chrono::seconds(20)), 40.0);
}

// ---------------------------------------------------------------------------
// Additional test 10: remaining_deadline_s returns 0 when IDLE
// ---------------------------------------------------------------------------
TEST(TorProtocol, RemainingDeadlineIsZeroWhenIdle)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  EXPECT_DOUBLE_EQ(p.remaining_deadline_s(t0), 0.0);
}

// ---------------------------------------------------------------------------
// Additional test 11: button not enabled when IDLE (before any trigger)
// ---------------------------------------------------------------------------
TEST(TorProtocol, ButtonNotEnabledWhenIdle)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  EXPECT_FALSE(p.is_acknowledgment_button_enabled(t0 + std::chrono::seconds(10)));
}

// ---------------------------------------------------------------------------
// Additional test 12: button not enabled when ACKNOWLEDGED
// ---------------------------------------------------------------------------
TEST(TorProtocol, ButtonNotEnabledWhenAcknowledged)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  // advance past guard + acknowledge
  auto t_click = t0 + std::chrono::seconds(6);
  p.on_acknowledgment_clicked(t_click, {}, "ODD_A", 1.0F, "OP-001");
  ASSERT_EQ(p.state(), TorProtocol::State::kAcknowledged);
  EXPECT_FALSE(p.is_acknowledgment_button_enabled(t_click + std::chrono::seconds(1)));
}

// ---------------------------------------------------------------------------
// Additional test 13: click rejected when state == ACKNOWLEDGED (double-click protection)
// ---------------------------------------------------------------------------
TEST(TorProtocol, DoubleClickProtection)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_click = t0 + std::chrono::seconds(6);
  auto first = p.on_acknowledgment_clicked(t_click, {}, "ODD_A", 1.0F, "OP-001");
  ASSERT_TRUE(first.has_value());
  // second click must be rejected
  auto second = p.on_acknowledgment_clicked(
      t_click + std::chrono::milliseconds(100), {}, "ODD_A", 1.0F, "OP-001");
  EXPECT_FALSE(second.has_value());
}

// ---------------------------------------------------------------------------
// Additional test 14: all three Reason values can trigger
// ---------------------------------------------------------------------------
TEST(TorProtocol, AllReasonValuesCanTrigger)
{
  for (auto reason : {TorProtocol::Reason::kOddExit,
                      TorProtocol::Reason::kManualRequest,
                      TorProtocol::Reason::kSafetyAlert}) {
    TorProtocol p(default_cfg());
    auto t0 = TorProtocol::Clock::now();
    EXPECT_TRUE(p.trigger(reason, t0));
    EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
    EXPECT_EQ(p.last_reason(), reason);
  }
}

// ---------------------------------------------------------------------------
// Additional test 15: tick when IDLE returns false (no-op)
// ---------------------------------------------------------------------------
TEST(TorProtocol, TickWhenIdleIsNoop)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  EXPECT_FALSE(p.tick(t0 + std::chrono::seconds(120)));
  EXPECT_EQ(p.state(), TorProtocol::State::kIdle);
}

// ---------------------------------------------------------------------------
// Additional test 16: snapshot.operator_id matches what was passed
// ---------------------------------------------------------------------------
TEST(TorProtocol, SnapshotOperatorIdMatchesPassed)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kManualRequest, t0);
  auto snap = p.on_acknowledgment_clicked(
      t0 + std::chrono::seconds(6), {}, "ODD_C", 0.88F, "CAPTAIN-BRIDGE");
  ASSERT_TRUE(snap.has_value());
  EXPECT_EQ(snap->operator_id, "CAPTAIN-BRIDGE");
}

// ---------------------------------------------------------------------------
// Additional test 17: after reset, new trigger cycle works correctly
// ---------------------------------------------------------------------------
TEST(TorProtocol, AfterResetNewTriggerCycleWorks)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  p.reset();
  ASSERT_EQ(p.state(), TorProtocol::State::kIdle);

  // Second cycle
  auto t1 = t0 + std::chrono::seconds(10);
  EXPECT_TRUE(p.trigger(TorProtocol::Reason::kSafetyAlert, t1));
  EXPECT_EQ(p.state(), TorProtocol::State::kRequested);
  EXPECT_TRUE(p.tick(t1 + std::chrono::seconds(61)));
  EXPECT_EQ(p.state(), TorProtocol::State::kTimeoutMrc);
}

// ---------------------------------------------------------------------------
// Additional test 18: remaining_deadline_s returns 0 when ACKNOWLEDGED
// ---------------------------------------------------------------------------
TEST(TorProtocol, RemainingDeadlineIsZeroWhenAcknowledged)
{
  TorProtocol p(default_cfg());
  auto t0 = TorProtocol::Clock::now();
  p.trigger(TorProtocol::Reason::kOddExit, t0);
  auto t_click = t0 + std::chrono::seconds(6);
  p.on_acknowledgment_clicked(t_click, {}, "ODD_A", 1.0F, "OP-X");
  ASSERT_EQ(p.state(), TorProtocol::State::kAcknowledged);
  EXPECT_DOUBLE_EQ(p.remaining_deadline_s(t_click + std::chrono::seconds(5)), 0.0);
}
