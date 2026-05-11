#include <chrono>

#include <gtest/gtest.h>

#include "m8_hmi_transparency_bridge/module_health_monitor.hpp"

namespace {

using mass_l3::m8::ModuleHealthMonitor;
using mass_l3::m8::SatAggregator;
using Src = SatAggregator::SourceModule;

ModuleHealthMonitor make_monitor()
{
    ModuleHealthMonitor::Thresholds t{};
    t.m1_timeout_s = 2.0;
    t.m2_timeout_s = 1.0;
    t.m4_timeout_s = 1.0;
    t.m6_timeout_s = 1.0;
    t.m7_timeout_s = 2.0;
    return ModuleHealthMonitor{t};
}

ModuleHealthMonitor::TimePoint epoch()
{
    return ModuleHealthMonitor::Clock::now();
}

ModuleHealthMonitor::TimePoint offset(
    ModuleHealthMonitor::TimePoint base, double seconds)
{
    using namespace std::chrono;
    return base + duration_cast<steady_clock::duration>(duration<double>(seconds));
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Test 1: module never seen -> is_timed_out == true
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, NeverSeen_IsTimedOut)
{
    auto mon = make_monitor();
    auto now = epoch();

    EXPECT_TRUE(mon.is_timed_out(Src::kM1, now));
    EXPECT_TRUE(mon.is_timed_out(Src::kM2, now));
    EXPECT_TRUE(mon.is_timed_out(Src::kM7, now));
}

// ---------------------------------------------------------------------------
// Test 2: heartbeat recorded -> is_timed_out == false immediately
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, AfterHeartbeat_NotTimedOut)
{
    auto mon = make_monitor();
    auto now = epoch();

    mon.record_heartbeat(Src::kM2, now);

    // Check at the exact same time point -- age=0, well within 1.0s timeout
    EXPECT_FALSE(mon.is_timed_out(Src::kM2, now));
}

// ---------------------------------------------------------------------------
// Test 3: heartbeat too old -> is_timed_out == true
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, HeartbeatExpired_TimedOut)
{
    auto mon = make_monitor();
    auto then = epoch();
    auto now  = offset(then, 1.5);  // M2 timeout is 1.0s

    mon.record_heartbeat(Src::kM2, then);

    EXPECT_TRUE(mon.is_timed_out(Src::kM2, now));
}

// ---------------------------------------------------------------------------
// Test 4: M7 timeout -> is_m7_timed_out true
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, M7Timeout_IsM7TimedOut)
{
    auto mon = make_monitor();
    auto then = epoch();
    auto now  = offset(then, 3.0);  // M7 timeout is 2.0s

    mon.record_heartbeat(Src::kM7, then);

    EXPECT_TRUE(mon.is_m7_timed_out(now));
}

// ---------------------------------------------------------------------------
// Test 5: M7 not timed out -> is_m7_timed_out false
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, M7NotExpired_IsM7TimedOut_False)
{
    auto mon = make_monitor();
    auto now = epoch();

    mon.record_heartbeat(Src::kM7, now);

    // Check 1 second later -- still within the 2.0s timeout
    EXPECT_FALSE(mon.is_m7_timed_out(offset(now, 1.0)));
}

// ---------------------------------------------------------------------------
// Test 6: any_module_timed_out when one module missing (never received)
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, AnyModuleTimedOut_WhenOneMissing)
{
    auto mon = make_monitor();
    auto now = epoch();

    // Record heartbeats for all modules except kM4
    mon.record_heartbeat(Src::kM1, now);
    mon.record_heartbeat(Src::kM2, now);
    mon.record_heartbeat(Src::kM6, now);
    mon.record_heartbeat(Src::kM7, now);

    EXPECT_TRUE(mon.any_module_timed_out(now));
}

// ---------------------------------------------------------------------------
// Test 7: all modules healthy -> any_module_timed_out == false
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, AllModulesHealthy_NotTimedOut)
{
    auto mon = make_monitor();
    auto now = epoch();

    mon.record_heartbeat(Src::kM1, now);
    mon.record_heartbeat(Src::kM2, now);
    mon.record_heartbeat(Src::kM4, now);
    mon.record_heartbeat(Src::kM6, now);
    mon.record_heartbeat(Src::kM7, now);

    EXPECT_FALSE(mon.any_module_timed_out(now));
}

// ---------------------------------------------------------------------------
// Test 8: heartbeat update resets timeout countdown
// ---------------------------------------------------------------------------
TEST(ModuleHealthMonitor, HeartbeatUpdated_ResetsTimeout)
{
    auto mon = make_monitor();
    auto t0 = epoch();

    mon.record_heartbeat(Src::kM2, t0);

    // Advance past M2 timeout -- would now be timed out
    auto t1 = offset(t0, 1.5);
    EXPECT_TRUE(mon.is_timed_out(Src::kM2, t1));

    // Re-issue heartbeat at t1 -- now healthy again at t1
    mon.record_heartbeat(Src::kM2, t1);
    EXPECT_FALSE(mon.is_timed_out(Src::kM2, t1));
}
