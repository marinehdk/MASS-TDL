// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// L1WatchdogMonitor — D2.3 §4.2. MISRA C++:2023; no dynamic_cast, no recursion.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace mass_l3::m3 {

enum class L1WatchdogStatus : uint8_t { OK = 0, WARNING = 1, TIMEOUT = 2 };

struct L1WatchdogConfig {
  double warning_s          = 60.0;   // [HAZID 校准] disconnect → WARNING
  double timeout_s          = 120.0;  // [HAZID 校准] disconnect → TIMEOUT + ToR
  float  confidence_warning = 0.6F;   // confidence_factor when WARNING
  float  confidence_timeout = 0.4F;   // confidence_factor when TIMEOUT
};

struct L1WatchdogResult {
  L1WatchdogStatus status;
  double elapsed_s;          // seconds since last VoyageTask (0 if never received)
  float  confidence_factor;  // multiply base confidence by this
};

class L1WatchdogMonitor {
 public:
  explicit L1WatchdogMonitor(L1WatchdogConfig cfg);
  ~L1WatchdogMonitor() = default;
  L1WatchdogMonitor(const L1WatchdogMonitor&) = delete;
  L1WatchdogMonitor& operator=(const L1WatchdogMonitor&) = delete;
  L1WatchdogMonitor(L1WatchdogMonitor&&) = default;
  L1WatchdogMonitor& operator=(L1WatchdogMonitor&&) = default;

  // Call on every VoyageTask callback (valid or invalid — any arrival resets timer).
  void notify_voyage_task_received(std::chrono::steady_clock::time_point now);

  [[nodiscard]] L1WatchdogResult evaluate(
      std::chrono::steady_clock::time_point now) const;

 private:
  L1WatchdogConfig cfg_;
  std::optional<std::chrono::steady_clock::time_point> last_received_;
};

}  // namespace mass_l3::m3
