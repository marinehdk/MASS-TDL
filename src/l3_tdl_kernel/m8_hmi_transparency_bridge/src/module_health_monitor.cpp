#include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

#include <chrono>
#include <mutex>

namespace mass_l3::m8 {

void ModuleHealthMonitor::record_heartbeat(
    SatAggregator::SourceModule src, TimePoint now) noexcept
{
    std::lock_guard<std::mutex> lock{mutex_};
    last_heartbeat_[src] = now;
}

bool ModuleHealthMonitor::is_timed_out(
    SatAggregator::SourceModule src, TimePoint now) const noexcept
{
    std::lock_guard<std::mutex> lock{mutex_};
    auto it = last_heartbeat_.find(src);
    if (it == last_heartbeat_.end()) {
        return true;  // never heard from = timed out
    }
    double age = std::chrono::duration<double>(now - it->second).count();
    return age > timeout_for(src);
}

bool ModuleHealthMonitor::is_m7_timed_out(TimePoint now) const noexcept
{
    // Inline to avoid re-locking (is_timed_out acquires mutex; std::mutex is not recursive)
    std::lock_guard<std::mutex> lock{mutex_};
    auto it = last_heartbeat_.find(SatAggregator::SourceModule::kM7);
    if (it == last_heartbeat_.end()) {
        return true;
    }
    double age = std::chrono::duration<double>(now - it->second).count();
    return age > thresholds_.m7_timeout_s;
}

bool ModuleHealthMonitor::any_module_timed_out(TimePoint now) const noexcept
{
    // Single lock over the entire scan to avoid deadlock
    // (cannot call is_timed_out which also acquires the same non-recursive mutex)
    std::lock_guard<std::mutex> lock{mutex_};
    for (uint8_t i = 0U; i < static_cast<uint8_t>(SatAggregator::SourceModule::kCount); ++i) {
        auto src = static_cast<SatAggregator::SourceModule>(i);
        auto it = last_heartbeat_.find(src);
        if (it == last_heartbeat_.end()) {
            return true;
        }
        double age = std::chrono::duration<double>(now - it->second).count();
        if (age > timeout_for(src)) {
            return true;
        }
    }
    return false;
}

double ModuleHealthMonitor::timeout_for(
    SatAggregator::SourceModule src) const noexcept
{
    switch (src) {
        case SatAggregator::SourceModule::kM1: return thresholds_.m1_timeout_s;
        case SatAggregator::SourceModule::kM2: return thresholds_.m2_timeout_s;
        case SatAggregator::SourceModule::kM4: return thresholds_.m4_timeout_s;
        case SatAggregator::SourceModule::kM6: return thresholds_.m6_timeout_s;
        case SatAggregator::SourceModule::kM7: return thresholds_.m7_timeout_s;
        default: return kDefaultTimeoutS;
    }
}

}  // namespace mass_l3::m8
