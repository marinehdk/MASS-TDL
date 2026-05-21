// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// CurrentErrorMonitor — D2.3 §4.1. MISRA C++:2023; no dynamic_cast, no recursion.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include "l3_external_msgs/msg/tracking_error.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m3 {

enum class CurrentErrorSeverity : uint8_t { NORMAL = 0, MEDIUM = 1, HIGH = 2 };

struct CurrentErrorMonitorConfig {
  float  xte_high_nm       = 0.5F;   // [HAZID 校准] XTE > this → HIGH
  float  xte_medium_nm     = 0.3F;   // [HAZID 校准] XTE > this → MEDIUM
  float  current_high_kn   = 2.0F;   // [HAZID 校准] sea_current > this → HIGH
  float  current_medium_kn = 1.5F;   // [HAZID 校准] sea_current > this → MEDIUM
  double l4_stale_s        = 2.0;    // L4 msg age threshold → xte_nm = -1
};

struct CurrentErrorReading {
  CurrentErrorSeverity severity;
  float xte_nm;           // -1.0F = L4 unavailable / stale
  float sea_current_kn;   // from WorldState.own_ship.current_speed_kn
  bool  l4_available;
};

class CurrentErrorMonitor {
 public:
  explicit CurrentErrorMonitor(CurrentErrorMonitorConfig cfg);
  ~CurrentErrorMonitor() = default;
  CurrentErrorMonitor(const CurrentErrorMonitor&) = delete;
  CurrentErrorMonitor& operator=(const CurrentErrorMonitor&) = delete;
  CurrentErrorMonitor(CurrentErrorMonitor&&) = default;
  CurrentErrorMonitor& operator=(CurrentErrorMonitor&&) = default;

  void update_tracking_error(
      const l3_external_msgs::msg::TrackingError& msg,
      std::chrono::steady_clock::time_point now);

  void update_world_state(
      const l3_msgs::msg::WorldState& msg,
      std::chrono::steady_clock::time_point now);

  [[nodiscard]] CurrentErrorReading evaluate(
      std::chrono::steady_clock::time_point now) const;

 private:
  [[nodiscard]] CurrentErrorSeverity compute_severity(
      float effective_xte_nm, float sea_current_kn) const;

  CurrentErrorMonitorConfig cfg_;
  float  last_xte_nm_         = -1.0F;
  float  last_sea_current_kn_ =  0.0F;
  std::optional<std::chrono::steady_clock::time_point> last_tracking_error_time_;
};

}  // namespace mass_l3::m3
