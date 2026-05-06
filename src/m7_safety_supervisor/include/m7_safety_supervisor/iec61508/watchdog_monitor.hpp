#ifndef M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_
#define M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_

#include <array>
#include <chrono>
#include <cstdint>

#include "m7_safety_supervisor/common/error.hpp"

namespace mass_l3::m7::iec61508 {

// Monitored modules: M1–M6 + M8 (7 Doer modules; M7 monitors itself separately)
enum class MonitoredModule : std::uint8_t {
  kM1 = 0, kM2, kM3, kM4, kM5, kM6, kM8,
  kCount  // sentinel — must remain last
};

struct WatchdogConfig {
  // timeout_ms[i]: expected inter-message interval × 1.5 + tolerance buffer
  // Indexed by MonitoredModule enum value
  std::array<std::chrono::milliseconds,
             static_cast<std::size_t>(MonitoredModule::kCount)> timeout_ms;
  // tolerance_count[i]: consecutive missed beats before critical
  std::array<std::uint32_t,
             static_cast<std::size_t>(MonitoredModule::kCount)> tolerance_count;

  // Build default config from m7_params.yaml thresholds
  // M1: 1500ms (1 Hz × 1.5), M2: 300ms (4 Hz × 1.5), M3: 7500ms (0.2 Hz × 1.5)
  // M4: 750ms, M5: 1000ms, M6: 750ms, M8: 150ms; tolerance: 3 (M3: 2)
  [[nodiscard]] static WatchdogConfig make_default() noexcept;
};

// WatchdogMonitor: single-threaded (main_loop callback group)
// No internal mutex — caller ensures single-thread access
class WatchdogMonitor {
public:
  explicit WatchdogMonitor(WatchdogConfig const& cfg) noexcept;

  // Call when a message arrives from a monitored module
  void on_message_received(MonitoredModule mod,
                           std::chrono::steady_clock::time_point now) noexcept;

  struct WatchdogResult {
    std::array<bool, static_cast<std::size_t>(MonitoredModule::kCount)> heartbeat_ok;
    std::array<std::uint32_t, static_cast<std::size_t>(MonitoredModule::kCount)> loss_count;
    bool any_critical;        // at least one module exceeded tolerance
    std::uint32_t critical_count;  // number of modules in critical state
  };

  // Evaluate current heartbeat health — call on main loop tick
  // loss_count_ is mutable to allow logical-const update inside const evaluate()
  [[nodiscard]] WatchdogResult evaluate(std::chrono::steady_clock::time_point now) const noexcept;

  // Reset a module's loss counter (when its heartbeat recovers)
  void reset(MonitoredModule mod) noexcept;

  // Reset all modules
  void reset_all() noexcept;

private:
  WatchdogConfig cfg_;
  std::array<std::chrono::steady_clock::time_point,
             static_cast<std::size_t>(MonitoredModule::kCount)> last_received_;
  // mutable: updated inside const evaluate() — standard logical-const pattern
  mutable std::array<std::uint32_t,
                     static_cast<std::size_t>(MonitoredModule::kCount)> loss_count_;
  // Track whether we've received at least one message (avoids false alarms at startup)
  std::array<bool, static_cast<std::size_t>(MonitoredModule::kCount)> initialized_;
};

}  // namespace mass_l3::m7::iec61508

#endif  // M7_SAFETY_SUPERVISOR_IEC61508_WATCHDOG_MONITOR_HPP_
