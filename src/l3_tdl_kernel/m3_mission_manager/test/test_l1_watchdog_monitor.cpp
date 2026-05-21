// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// Unit tests for L1WatchdogMonitor — spec D2.3 §7.2

#include <gtest/gtest.h>
#include <chrono>
#include "m3_mission_manager/l1_watchdog_monitor.hpp"

namespace mass_l3::m3 {
namespace {

L1WatchdogConfig make_cfg() {
  return L1WatchdogConfig{60.0, 120.0, 0.6F, 0.4F};
}

// §7.2 case 1: 30s elapsed → OK, factor = 1.0
TEST(L1WatchdogMonitorTest, Normal30s) {
  L1WatchdogMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.notify_voyage_task_received(now - std::chrono::seconds(30));
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.status, L1WatchdogStatus::OK);
  EXPECT_FLOAT_EQ(r.confidence_factor, 1.0F);
}

// §7.2 case 2: 61s elapsed → WARNING, factor = 0.6
TEST(L1WatchdogMonitorTest, WarningBoundary61s) {
  L1WatchdogMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.notify_voyage_task_received(now - std::chrono::seconds(61));
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.status, L1WatchdogStatus::WARNING);
  EXPECT_FLOAT_EQ(r.confidence_factor, 0.6F);
}

// §7.2 case 3: 121s elapsed → TIMEOUT, factor = 0.4
TEST(L1WatchdogMonitorTest, TimeoutBoundary121s) {
  L1WatchdogMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.notify_voyage_task_received(now - std::chrono::seconds(121));
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.status, L1WatchdogStatus::TIMEOUT);
  EXPECT_FLOAT_EQ(r.confidence_factor, 0.4F);
}

// §7.2 case 4: recovery after 150s — notify resets to OK
TEST(L1WatchdogMonitorTest, RecoveryAfterTimeout) {
  L1WatchdogMonitor mon(make_cfg());
  const auto base = std::chrono::steady_clock::now();
  mon.notify_voyage_task_received(base - std::chrono::seconds(150));
  EXPECT_EQ(mon.evaluate(base).status, L1WatchdogStatus::TIMEOUT);
  mon.notify_voyage_task_received(base);
  const auto r = mon.evaluate(base);
  EXPECT_EQ(r.status, L1WatchdogStatus::OK);
  EXPECT_FLOAT_EQ(r.confidence_factor, 1.0F);
}

// §7.2 case 5: OK → WARNING → notify → OK → WARNING → notify → OK
TEST(L1WatchdogMonitorTest, MultipleRecoveries) {
  L1WatchdogMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();

  mon.notify_voyage_task_received(now - std::chrono::seconds(70));
  EXPECT_EQ(mon.evaluate(now).status, L1WatchdogStatus::WARNING);

  mon.notify_voyage_task_received(now);
  EXPECT_EQ(mon.evaluate(now).status, L1WatchdogStatus::OK);

  mon.notify_voyage_task_received(now - std::chrono::seconds(70));
  EXPECT_EQ(mon.evaluate(now).status, L1WatchdogStatus::WARNING);

  mon.notify_voyage_task_received(now);
  const auto final_r = mon.evaluate(now);
  EXPECT_EQ(final_r.status, L1WatchdogStatus::OK);
  EXPECT_FLOAT_EQ(final_r.confidence_factor, 1.0F);
}

}  // namespace
}  // namespace mass_l3::m3
