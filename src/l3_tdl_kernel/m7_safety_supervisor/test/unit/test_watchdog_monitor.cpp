#include <gtest/gtest.h>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

using namespace mass_l3::m7::iec61508;
using namespace std::chrono_literals;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

WatchdogConfig build_uniform_cfg(
    std::chrono::milliseconds timeout_ms, std::uint32_t tolerance)
{
  WatchdogConfig cfg{};
  cfg.timeout_ms.fill(timeout_ms);
  cfg.tolerance_count.fill(tolerance);
  return cfg;
}

// Base time_point for test reproducibility
auto const kT0 = std::chrono::steady_clock::time_point{} + std::chrono::seconds{1000};

std::chrono::steady_clock::time_point t(std::chrono::milliseconds offset) {
  return kT0 + offset;
}

}  // namespace

// ---------------------------------------------------------------------------
// Test 1: Before any message — startup grace means no loss reported
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(WatchdogMonitorTest, BeforeFirstMessage_StartupGrace_NoLoss) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  // No messages sent — evaluate well past timeout
  auto const kResult = monitor.evaluate(t(10000ms));

  for (std::size_t i = 0U; i < static_cast<std::size_t>(MonitoredModule::kCount); ++i) {
    EXPECT_TRUE(kResult.heartbeat_ok[i]) << "Module " << i << " should be OK during startup grace";
    EXPECT_EQ(kResult.loss_count[i], 0U);
  }
  EXPECT_FALSE(kResult.any_critical);
  EXPECT_EQ(kResult.critical_count, 0U);
}

// ---------------------------------------------------------------------------
// Test 2: After first message, within timeout — heartbeat OK
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, AfterFirstMessage_WithinTimeout_HeartbeatOk) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Evaluate 100ms later — well within 300ms timeout
  auto const kResult = monitor.evaluate(t(100ms));

  auto const kIdx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_TRUE(kResult.heartbeat_ok[kIdx]);
  EXPECT_EQ(kResult.loss_count[kIdx], 0U);
  EXPECT_FALSE(kResult.any_critical);
}

// ---------------------------------------------------------------------------
// Test 3: Message stops arriving — past timeout — loss incremented
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MessageStopsArriving_AfterTimeout_LossIncremented) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Evaluate 301ms after last message — just past 300ms timeout
  auto const kResult = monitor.evaluate(t(301ms));

  auto const kIdx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_FALSE(kResult.heartbeat_ok[kIdx]);
  EXPECT_EQ(kResult.loss_count[kIdx], 1U);
}

// ---------------------------------------------------------------------------
// Test 4: Loss exceeds tolerance — any_critical becomes true
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, LossExceedsTolerance_AnyCriticalTrue) {
  // tolerance=1: loss_count must exceed 1 (i.e., be >= 2) to be critical
  auto const kCfg = build_uniform_cfg(300ms, 1U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // First timeout: loss_count = 1 (not yet > tolerance=1)
  auto result = monitor.evaluate(t(301ms));
  EXPECT_FALSE(result.any_critical);

  // Second timeout call: loss_count = 2 (now > tolerance=1) — critical
  result = monitor.evaluate(t(602ms));
  EXPECT_TRUE(result.any_critical);
  EXPECT_EQ(result.critical_count, 1U);
}

// ---------------------------------------------------------------------------
// Test 5: Message recovery after loss — loss count resets
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MessageRecovery_AfterLoss_LossCountReset) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Accumulate one loss
  auto result = monitor.evaluate(t(301ms));
  auto const kIdx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_EQ(result.loss_count[kIdx], 1U);

  // Message recovers
  monitor.on_message_received(MonitoredModule::kM2, t(350ms));

  // Evaluate within new timeout window
  result = monitor.evaluate(t(400ms));
  EXPECT_TRUE(result.heartbeat_ok[kIdx]);
  EXPECT_EQ(result.loss_count[kIdx], 0U);
  EXPECT_FALSE(result.any_critical);
}

// ---------------------------------------------------------------------------
// Test 6: Two modules down — critical_count == 2
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MultipleModulesDown_CriticalCountCorrect) {
  // tolerance=0: any timeout immediately goes critical on second evaluate call
  // Actually tolerance=0 means loss_count > 0 is critical — use tolerance=0
  auto const kCfg = build_uniform_cfg(300ms, 0U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM1, t(0ms));
  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Both modules timed out — first evaluate: loss=1 each, which is > tolerance=0
  auto const kResult = monitor.evaluate(t(301ms));

  auto const kIdx1 = static_cast<std::size_t>(MonitoredModule::kM1);
  auto const kIdx2 = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_FALSE(kResult.heartbeat_ok[kIdx1]);
  EXPECT_FALSE(kResult.heartbeat_ok[kIdx2]);
  EXPECT_TRUE(kResult.any_critical);
  EXPECT_EQ(kResult.critical_count, 2U);
}

// ---------------------------------------------------------------------------
// Test 7: reset(mod) clears loss state for that module
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, ResetModule_ClearsLossState) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));
  // Accumulate loss
  (void)monitor.evaluate(t(301ms));

  // Reset that module
  monitor.reset(MonitoredModule::kM2);

  // After reset, module is uninitialized again → startup grace → OK
  auto const kResult = monitor.evaluate(t(5000ms));
  auto const kIdx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_TRUE(kResult.heartbeat_ok[kIdx]);
  EXPECT_EQ(kResult.loss_count[kIdx], 0U);
}

// ---------------------------------------------------------------------------
// Test 8: make_default() produces correct timeouts
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MakeDefault_TimeoutsCorrect) {
  auto const kCfg = WatchdogConfig::make_default();

  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM1)],
            std::chrono::milliseconds{1500});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM2)],
            std::chrono::milliseconds{300});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM3)],
            std::chrono::milliseconds{7500});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM4)],
            std::chrono::milliseconds{750});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM5)],
            std::chrono::milliseconds{1000});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM6)],
            std::chrono::milliseconds{750});
  EXPECT_EQ(kCfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM8)],
            std::chrono::milliseconds{150});

  // Tolerances: M3 gets 2, all others get 3
  EXPECT_EQ(kCfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM3)], 2U);
  EXPECT_EQ(kCfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM1)], 3U);
  EXPECT_EQ(kCfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM2)], 3U);
}

// ---------------------------------------------------------------------------
// Test 9: reset_all clears all module state
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TEST(WatchdogMonitorTest, ResetAll_ClearsAllModules) {
  auto const kCfg = build_uniform_cfg(300ms, 3U);
  WatchdogMonitor monitor{kCfg};

  // Initialize several modules and accumulate loss
  monitor.on_message_received(MonitoredModule::kM1, t(0ms));
  monitor.on_message_received(MonitoredModule::kM2, t(0ms));
  (void)monitor.evaluate(t(301ms));

  monitor.reset_all();

  // All modules back to startup grace
  auto const kResult = monitor.evaluate(t(10000ms));
  for (std::size_t i = 0U; i < static_cast<std::size_t>(MonitoredModule::kCount); ++i) {
    EXPECT_TRUE(kResult.heartbeat_ok[i]) << "Module " << i << " should be in startup grace";
  }
  EXPECT_FALSE(kResult.any_critical);
}
