#include <gtest/gtest.h>
#include <chrono>
#include "m7_safety_supervisor/core/hard_constraint_watchdog.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"

namespace mass_l3::m7::core {
namespace {
using namespace std::chrono_literals;

TEST(HardConstraintWatchdog, SingleModuleCriticalTriggersMrm01)
{
  iec61508::WatchdogMonitor wd(iec61508::WatchdogConfig::make_default());
  auto now = std::chrono::steady_clock::now();
  wd.on_message_received(iec61508::MonitoredModule::kM2, now);
  auto const result = evaluate_watchdog_constraint(wd, now + 1000ms);
  EXPECT_TRUE(result.any_critical);
  EXPECT_GE(result.critical_count, 1U);
}

TEST(HardConstraintWatchdog, MultipleModulesCriticalTriggersMrm02)
{
  iec61508::WatchdogMonitor wd(iec61508::WatchdogConfig::make_default());
  auto now = std::chrono::steady_clock::now();
  wd.on_message_received(iec61508::MonitoredModule::kM2, now);
  wd.on_message_received(iec61508::MonitoredModule::kM6, now);
  auto const result = evaluate_watchdog_constraint(wd, now + 2000ms);
  EXPECT_TRUE(result.multi_critical);
}

TEST(HardConstraintWatchdog, AllModulesHealthyPasses)
{
  iec61508::WatchdogMonitor wd(iec61508::WatchdogConfig::make_default());
  auto now = std::chrono::steady_clock::now();
  for (std::uint8_t i = 0; i < static_cast<std::uint8_t>(iec61508::MonitoredModule::kCount); ++i) {
    wd.on_message_received(static_cast<iec61508::MonitoredModule>(i), now);
  }
  auto const result = evaluate_watchdog_constraint(wd, now);
  EXPECT_FALSE(result.any_critical);
  EXPECT_EQ(result.critical_count, 0U);
}

}  // namespace
}  // namespace mass_l3::m7::core
