#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_WATCHDOG_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_WATCHDOG_HPP_

#include <chrono>
#include <cstdint>
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

namespace mass_l3::m7::core {

struct WatchdogConstraintResult {
  bool any_critical{false};
  bool multi_critical{false};
  std::uint32_t critical_count{0};
  std::uint32_t critical_modules_bitmask{0};
};

WatchdogConstraintResult evaluate_watchdog_constraint(
    iec61508::WatchdogMonitor const& watchdog,
    std::chrono::steady_clock::time_point now) noexcept;

}  // namespace mass_l3::m7::core

#endif
