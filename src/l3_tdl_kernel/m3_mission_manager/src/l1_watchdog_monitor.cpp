// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary

#include "m3_mission_manager/l1_watchdog_monitor.hpp"

namespace mass_l3::m3 {

L1WatchdogMonitor::L1WatchdogMonitor(L1WatchdogConfig cfg)
    : cfg_(cfg) {}

void L1WatchdogMonitor::notify_voyage_task_received(
    std::chrono::steady_clock::time_point now)
{
  last_received_ = now;
}

L1WatchdogResult L1WatchdogMonitor::evaluate(
    std::chrono::steady_clock::time_point now) const
{
  // Never received → mission not yet started; treat as OK
  if (!last_received_.has_value()) {
    return L1WatchdogResult{L1WatchdogStatus::OK, 0.0, 1.0F};
  }

  const double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
      now - last_received_.value()).count();

  if (elapsed > cfg_.timeout_s) {
    return L1WatchdogResult{L1WatchdogStatus::TIMEOUT, elapsed, cfg_.confidence_timeout};
  }
  if (elapsed > cfg_.warning_s) {
    return L1WatchdogResult{L1WatchdogStatus::WARNING, elapsed, cfg_.confidence_warning};
  }
  return L1WatchdogResult{L1WatchdogStatus::OK, elapsed, 1.0F};
}

}  // namespace mass_l3::m3
