#include <gtest/gtest.h>
#include <chrono>

#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

using namespace mass_l3::m7::iec61508;
using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WatchdogConfig build_uniform_cfg(
    std::chrono::milliseconds timeout_ms, std::uint32_t tolerance)
{
  WatchdogConfig cfg{};
  cfg.timeout_ms.fill(timeout_ms);
  cfg.tolerance_count.fill(tolerance);
  return cfg;
}

// Base time_point for test reproducibility
static const auto kT0 = std::chrono::steady_clock::time_point{} + std::chrono::seconds{1000};

static std::chrono::steady_clock::time_point t(std::chrono::milliseconds offset) {
  return kT0 + offset;
}

// ---------------------------------------------------------------------------
// Test 1: Before any message — startup grace means no loss reported
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, BeforeFirstMessage_StartupGrace_NoLoss) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  // No messages sent — evaluate well past timeout
  auto const result = monitor.evaluate(t(10000ms));

  for (std::size_t i = 0u; i < static_cast<std::size_t>(MonitoredModule::kCount); ++i) {
    EXPECT_TRUE(result.heartbeat_ok[i]) << "Module " << i << " should be OK during startup grace";
    EXPECT_EQ(result.loss_count[i], 0u);
  }
  EXPECT_FALSE(result.any_critical);
  EXPECT_EQ(result.critical_count, 0u);
}

// ---------------------------------------------------------------------------
// Test 2: After first message, within timeout — heartbeat OK
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, AfterFirstMessage_WithinTimeout_HeartbeatOk) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Evaluate 100ms later — well within 300ms timeout
  auto const result = monitor.evaluate(t(100ms));

  auto const idx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_TRUE(result.heartbeat_ok[idx]);
  EXPECT_EQ(result.loss_count[idx], 0u);
  EXPECT_FALSE(result.any_critical);
}

// ---------------------------------------------------------------------------
// Test 3: Message stops arriving — past timeout — loss incremented
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MessageStopsArriving_AfterTimeout_LossIncremented) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Evaluate 301ms after last message — just past 300ms timeout
  auto const result = monitor.evaluate(t(301ms));

  auto const idx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_FALSE(result.heartbeat_ok[idx]);
  EXPECT_EQ(result.loss_count[idx], 1u);
}

// ---------------------------------------------------------------------------
// Test 4: Loss exceeds tolerance — any_critical becomes true
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, LossExceedsTolerance_AnyCriticalTrue) {
  // tolerance=1: loss_count must exceed 1 (i.e., be >= 2) to be critical
  auto const cfg = build_uniform_cfg(300ms, 1u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // First timeout: loss_count = 1 (not yet > tolerance=1)
  auto result = monitor.evaluate(t(301ms));
  EXPECT_FALSE(result.any_critical);

  // Second timeout call: loss_count = 2 (now > tolerance=1) — critical
  result = monitor.evaluate(t(602ms));
  EXPECT_TRUE(result.any_critical);
  EXPECT_EQ(result.critical_count, 1u);
}

// ---------------------------------------------------------------------------
// Test 5: Message recovery after loss — loss count resets
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MessageRecovery_AfterLoss_LossCountReset) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Accumulate one loss
  auto result = monitor.evaluate(t(301ms));
  auto const idx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_EQ(result.loss_count[idx], 1u);

  // Message recovers
  monitor.on_message_received(MonitoredModule::kM2, t(350ms));

  // Evaluate within new timeout window
  result = monitor.evaluate(t(400ms));
  EXPECT_TRUE(result.heartbeat_ok[idx]);
  EXPECT_EQ(result.loss_count[idx], 0u);
  EXPECT_FALSE(result.any_critical);
}

// ---------------------------------------------------------------------------
// Test 6: Two modules down — critical_count == 2
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MultipleModulesDown_CriticalCountCorrect) {
  // tolerance=0: any timeout immediately goes critical on second evaluate call
  // Actually tolerance=0 means loss_count > 0 is critical — use tolerance=0
  auto const cfg = build_uniform_cfg(300ms, 0u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM1, t(0ms));
  monitor.on_message_received(MonitoredModule::kM2, t(0ms));

  // Both modules timed out — first evaluate: loss=1 each, which is > tolerance=0
  auto const result = monitor.evaluate(t(301ms));

  auto const idx1 = static_cast<std::size_t>(MonitoredModule::kM1);
  auto const idx2 = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_FALSE(result.heartbeat_ok[idx1]);
  EXPECT_FALSE(result.heartbeat_ok[idx2]);
  EXPECT_TRUE(result.any_critical);
  EXPECT_EQ(result.critical_count, 2u);
}

// ---------------------------------------------------------------------------
// Test 7: reset(mod) clears loss state for that module
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, ResetModule_ClearsLossState) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  monitor.on_message_received(MonitoredModule::kM2, t(0ms));
  // Accumulate loss
  monitor.evaluate(t(301ms));

  // Reset that module
  monitor.reset(MonitoredModule::kM2);

  // After reset, module is uninitialized again → startup grace → OK
  auto const result = monitor.evaluate(t(5000ms));
  auto const idx = static_cast<std::size_t>(MonitoredModule::kM2);
  EXPECT_TRUE(result.heartbeat_ok[idx]);
  EXPECT_EQ(result.loss_count[idx], 0u);
}

// ---------------------------------------------------------------------------
// Test 8: make_default() produces correct timeouts
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, MakeDefault_TimeoutsCorrect) {
  auto const cfg = WatchdogConfig::make_default();

  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM1)],
            std::chrono::milliseconds{1500});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM2)],
            std::chrono::milliseconds{300});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM3)],
            std::chrono::milliseconds{7500});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM4)],
            std::chrono::milliseconds{750});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM5)],
            std::chrono::milliseconds{1000});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM6)],
            std::chrono::milliseconds{750});
  EXPECT_EQ(cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM8)],
            std::chrono::milliseconds{150});

  // Tolerances: M3 gets 2, all others get 3
  EXPECT_EQ(cfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM3)], 2u);
  EXPECT_EQ(cfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM1)], 3u);
  EXPECT_EQ(cfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM2)], 3u);
}

// ---------------------------------------------------------------------------
// Test 9: reset_all clears all module state
// ---------------------------------------------------------------------------

TEST(WatchdogMonitorTest, ResetAll_ClearsAllModules) {
  auto const cfg = build_uniform_cfg(300ms, 3u);
  WatchdogMonitor monitor{cfg};

  // Initialize several modules and accumulate loss
  monitor.on_message_received(MonitoredModule::kM1, t(0ms));
  monitor.on_message_received(MonitoredModule::kM2, t(0ms));
  monitor.evaluate(t(301ms));

  monitor.reset_all();

  // All modules back to startup grace
  auto const result = monitor.evaluate(t(10000ms));
  for (std::size_t i = 0u; i < static_cast<std::size_t>(MonitoredModule::kCount); ++i) {
    EXPECT_TRUE(result.heartbeat_ok[i]) << "Module " << i << " should be in startup grace";
  }
  EXPECT_FALSE(result.any_critical);
}
