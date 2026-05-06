#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

#include <algorithm>

namespace mass_l3::m7::iec61508 {

namespace {
constexpr std::size_t kModuleCount = static_cast<std::size_t>(MonitoredModule::kCount);
}  // namespace

// ---------------------------------------------------------------------------
// WatchdogConfig::make_default
// ---------------------------------------------------------------------------

WatchdogConfig WatchdogConfig::make_default() noexcept {
  WatchdogConfig cfg{};
  // Timeouts: expected period × 1.5; M3 is slowest at 0.2 Hz (5s period → 7500ms)
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM1)] = std::chrono::milliseconds{1500};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM2)] = std::chrono::milliseconds{300};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM3)] = std::chrono::milliseconds{7500};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM4)] = std::chrono::milliseconds{750};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM5)] = std::chrono::milliseconds{1000};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM6)] = std::chrono::milliseconds{750};
  cfg.timeout_ms[static_cast<std::size_t>(MonitoredModule::kM8)] = std::chrono::milliseconds{150};

  // Tolerance: 3 for most modules; M3 gets 2 (fewer heartbeats expected per window)
  cfg.tolerance_count.fill(3u);
  cfg.tolerance_count[static_cast<std::size_t>(MonitoredModule::kM3)] = 2u;

  return cfg;
}

// ---------------------------------------------------------------------------
// WatchdogMonitor constructor
// ---------------------------------------------------------------------------

WatchdogMonitor::WatchdogMonitor(WatchdogConfig const& cfg) noexcept
  : cfg_{cfg}
{
  last_received_.fill(std::chrono::steady_clock::time_point{});
  loss_count_.fill(0u);
  initialized_.fill(false);
}

// ---------------------------------------------------------------------------
// on_message_received
// ---------------------------------------------------------------------------

void WatchdogMonitor::on_message_received(
    MonitoredModule mod,
    std::chrono::steady_clock::time_point now) noexcept
{
  auto const idx = static_cast<std::size_t>(mod);
  last_received_[idx] = now;
  loss_count_[idx] = 0u;
  initialized_[idx] = true;
}

// ---------------------------------------------------------------------------
// evaluate — logical const: loss_count_ is mutable
// ---------------------------------------------------------------------------

WatchdogMonitor::WatchdogResult
WatchdogMonitor::evaluate(std::chrono::steady_clock::time_point now) const noexcept
{
  WatchdogResult result{};
  result.any_critical = false;
  result.critical_count = 0u;

  for (std::size_t i = 0u; i < kModuleCount; ++i) {
    if (!initialized_[i]) {
      // Startup grace: not yet heard from this module — treat as OK
      result.heartbeat_ok[i] = true;
      result.loss_count[i] = 0u;
      continue;
    }

    auto const elapsed = now - last_received_[i];
    if (elapsed > cfg_.timeout_ms[i]) {
      ++loss_count_[i];
      result.heartbeat_ok[i] = false;
      result.loss_count[i] = loss_count_[i];
    } else {
      loss_count_[i] = 0u;
      result.heartbeat_ok[i] = true;
      result.loss_count[i] = 0u;
    }

    if (loss_count_[i] > cfg_.tolerance_count[i]) {
      result.any_critical = true;
      ++result.critical_count;
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// reset — single module
// ---------------------------------------------------------------------------

void WatchdogMonitor::reset(MonitoredModule mod) noexcept {
  auto const idx = static_cast<std::size_t>(mod);
  loss_count_[idx] = 0u;
  initialized_[idx] = false;
  last_received_[idx] = std::chrono::steady_clock::time_point{};
}

// ---------------------------------------------------------------------------
// reset_all
// ---------------------------------------------------------------------------

void WatchdogMonitor::reset_all() noexcept {
  last_received_.fill(std::chrono::steady_clock::time_point{});
  loss_count_.fill(0u);
  initialized_.fill(false);
}

}  // namespace mass_l3::m7::iec61508
