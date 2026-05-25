#include "m7_safety_supervisor/core/hard_constraint_watchdog.hpp"

namespace mass_l3::m7::core {

WatchdogConstraintResult evaluate_watchdog_constraint(
    iec61508::WatchdogMonitor const& watchdog,
    std::chrono::steady_clock::time_point now) noexcept
{
  auto const wd_result = watchdog.evaluate(now);
  WatchdogConstraintResult result{};
  result.any_critical = wd_result.any_critical;
  result.critical_count = wd_result.critical_count;
  result.multi_critical = (wd_result.critical_count >= 2U);
  for (std::size_t i = 0; i < wd_result.heartbeat_ok.size(); ++i) {
    if (!wd_result.heartbeat_ok[i]) {
      result.critical_modules_bitmask |= (1U << static_cast<std::uint32_t>(i));
    }
  }
  return result;
}

}  // namespace mass_l3::m7::core
