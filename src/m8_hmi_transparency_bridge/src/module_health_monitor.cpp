#include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

namespace mass_l3::m8 {

ModuleHealth ModuleHealthMonitor::evaluate(
    SatAggregator::SourceModule src,
    const SatAggregator& aggregator,
    SatAggregator::TimePoint now,
    double heartbeat_timeout_s) const
{
  if (aggregator.is_stale(src, now, heartbeat_timeout_s)) {
    return ModuleHealth::kLost;
  }
  return ModuleHealth::kOk;
}

}  // namespace mass_l3::m8
