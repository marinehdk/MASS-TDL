// include/m8_hmi_transparency_bridge/module_health_monitor.hpp
#ifndef MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
#define MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_

#include <chrono>
#include <map>
#include <mutex>
#include <string>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

/// Heartbeat monitor for upstream M1-M7 modules.
///
/// Records last receive time per source module; exposes health queries.
class ModuleHealthMonitor final {
 public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct Thresholds {
    double m1_timeout_s{2.0};
    double m2_timeout_s{1.0};
    double m4_timeout_s{1.0};
    double m6_timeout_s{1.0};
    double m7_timeout_s{2.0};
  };

  ModuleHealthMonitor() noexcept = default;
  explicit ModuleHealthMonitor(Thresholds t) noexcept : thresholds_(t) {}

  /// Update heartbeat for a module (called when any message arrives from that module)
  void record_heartbeat(SatAggregator::SourceModule src, TimePoint now) noexcept;

  /// True if module has not sent a message within its timeout window
  [[nodiscard]] bool is_timed_out(
      SatAggregator::SourceModule src, TimePoint now) const noexcept;

  /// Check if M7 specifically has timed out (critical -- triggers forced D2)
  [[nodiscard]] bool is_m7_timed_out(TimePoint now) const noexcept;

  /// Check any module is timed out (DEGRADED condition)
  [[nodiscard]] bool any_module_timed_out(TimePoint now) const noexcept;

 private:
  Thresholds thresholds_;
  mutable std::mutex mutex_;
  std::map<SatAggregator::SourceModule, TimePoint> last_heartbeat_{};

  [[nodiscard]] double timeout_for(SatAggregator::SourceModule src) const noexcept;

  static constexpr double kDefaultTimeoutS{1.0};
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
