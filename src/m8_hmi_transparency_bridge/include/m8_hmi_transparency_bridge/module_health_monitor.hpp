#ifndef MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
#define MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_

#include <cstdint>

#include "m8_hmi_transparency_bridge/sat_aggregator.hpp"

namespace mass_l3::m8 {

enum class ModuleHealth : uint8_t {
  kOk = 0,
  kDegraded = 1,
  kCritical = 2,
  kLost = 3,
};

class ModuleHealthMonitor final {
 public:
  ModuleHealthMonitor() = default;
  ~ModuleHealthMonitor() = default;
  ModuleHealthMonitor(const ModuleHealthMonitor&) = delete;
  ModuleHealthMonitor& operator=(const ModuleHealthMonitor&) = delete;

  [[nodiscard]] ModuleHealth evaluate(SatAggregator::SourceModule src,
                                      const SatAggregator& aggregator,
                                      SatAggregator::TimePoint now,
                                      double heartbeat_timeout_s) const;
};

}  // namespace mass_l3::m8

#endif  // MASS_L3_M8_MODULE_HEALTH_MONITOR_HPP_
