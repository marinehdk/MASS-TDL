#include <gtest/gtest.h>

#include <chrono>

#include "m2_world_model/types.hpp"
#include "m2_world_model/view_health_monitor.hpp"

namespace mass_l3::m2 {
namespace {

using time_point = std::chrono::steady_clock::time_point;

// Default config per spec §4.6.
auto default_cfg() {
  return ViewHealthMonitor::Config{
      2,      // dv_loss_periods_to_degraded
      5,      // dv_loss_periods_to_critical
      30.0,   // dv_degraded_to_critical_timeout_s
      100.0,  // ev_loss_ms_to_critical
      30.0,   // sv_loss_s_to_degraded
      0.5,    // confidence_floor_dv_degraded
  };
}

// ──────────────────────────────────────────────
// Test 1: Initial health is Full for all views
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, InitialHealthFull) {
  ViewHealthMonitor vhm(default_cfg());

  auto health = vhm.aggregated_health();

  EXPECT_EQ(health.dv_health, ViewHealth::Full);
  EXPECT_EQ(health.ev_health, ViewHealth::Full);
  EXPECT_EQ(health.sv_health, ViewHealth::Full);
  EXPECT_DOUBLE_EQ(health.dv_confidence, 1.0);
  EXPECT_DOUBLE_EQ(health.ev_confidence, 1.0);
  EXPECT_DOUBLE_EQ(health.sv_confidence, 1.0);
  EXPECT_DOUBLE_EQ(health.aggregated, 1.0);
}

// ──────────────────────────────────────────────
// Test 2: DV transitions to Degraded after enough misses
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, DvDegradedAfterMisses) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // 1st miss — still Full (threshold is 2).
  vhm.report_dv_update(false, t0);
  EXPECT_EQ(vhm.aggregated_health().dv_health, ViewHealth::Full);

  // 2nd miss — Degraded.
  vhm.report_dv_update(false, t0 + std::chrono::seconds(1));
  auto health = vhm.aggregated_health();
  EXPECT_EQ(health.dv_health, ViewHealth::Degraded);
  EXPECT_DOUBLE_EQ(health.dv_confidence, 0.5);
}

// ──────────────────────────────────────────────
// Test 3: DV transitions to Critical after more misses
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, DvCriticalAfterMoreMisses) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // 5 consecutive misses → dv_loss_periods_to_critical (5).
  for (int i = 0; i < 5; ++i) {
    vhm.report_dv_update(false, t0 + std::chrono::seconds(i));
  }

  auto health = vhm.aggregated_health();
  EXPECT_EQ(health.dv_health, ViewHealth::Critical);
  EXPECT_DOUBLE_EQ(health.dv_confidence, 0.0);
}

// ──────────────────────────────────────────────
// Test 4: EV transitions to Critical when age exceeds threshold
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, EvCriticalAfterAge) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // Age = 50 ms < 100 ms → Full.
  vhm.report_ev_age(0.05, t0);
  EXPECT_EQ(vhm.aggregated_health().ev_health, ViewHealth::Full);

  // Age = 150 ms >= 100 ms → Critical.
  vhm.report_ev_age(0.15, t0 + std::chrono::seconds(1));
  auto health = vhm.aggregated_health();
  EXPECT_EQ(health.ev_health, ViewHealth::Critical);
  EXPECT_DOUBLE_EQ(health.ev_confidence, 0.0);
}

// ──────────────────────────────────────────────
// Test 5: SV transitions to Degraded after timeout
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, SvDegradedAfterTimeout) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // Age = 10 s < 30 s → Full.
  vhm.report_sv_age(10.0, t0);
  EXPECT_EQ(vhm.aggregated_health().sv_health, ViewHealth::Full);

  // Age = 35 s >= 30 s → Degraded.
  vhm.report_sv_age(35.0, t0 + std::chrono::seconds(1));
  auto health = vhm.aggregated_health();
  EXPECT_EQ(health.sv_health, ViewHealth::Degraded);
  EXPECT_DOUBLE_EQ(health.sv_confidence, 0.5);
}

// ──────────────────────────────────────────────
// Test 6: Aggregated confidence is the minimum of three
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, AggregatedConfidenceMin) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // Degrade DV to 0.5 via 2 misses.
  vhm.report_dv_update(false, t0);
  vhm.report_dv_update(false, t0 + std::chrono::seconds(1));

  // EV and SV still Full (1.0).
  auto health = vhm.aggregated_health();
  EXPECT_DOUBLE_EQ(health.dv_confidence, 0.5);
  EXPECT_DOUBLE_EQ(health.ev_confidence, 1.0);
  EXPECT_DOUBLE_EQ(health.sv_confidence, 1.0);
  EXPECT_DOUBLE_EQ(health.aggregated, 0.5);  // min(0.5, 1.0, 1.0)

  // Now also degrade EV to Critical (0.0).
  vhm.report_ev_age(0.15, t0 + std::chrono::seconds(2));
  health = vhm.aggregated_health();
  EXPECT_DOUBLE_EQ(health.aggregated, 0.0);  // min(0.5, 0.0, 1.0)
}

// ──────────────────────────────────────────────
// Test 7: DV recovers from Degraded to Full on received update
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, RecoveryFromDegraded) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // 2 misses → Degraded.
  vhm.report_dv_update(false, t0);
  vhm.report_dv_update(false, t0 + std::chrono::seconds(1));
  EXPECT_EQ(vhm.aggregated_health().dv_health, ViewHealth::Degraded);

  // Recovery: received update.
  vhm.report_dv_update(true, t0 + std::chrono::seconds(2));
  auto health = vhm.aggregated_health();
  EXPECT_EQ(health.dv_health, ViewHealth::Full);
  EXPECT_DOUBLE_EQ(health.dv_confidence, 1.0);
}

// ──────────────────────────────────────────────
// Test 8: Reset restores all views to Full
// ──────────────────────────────────────────────
TEST(ViewHealthMonitorTest, ResetClearsAll) {
  ViewHealthMonitor vhm(default_cfg());

  auto t0 = time_point{};

  // Degrade all views.
  vhm.report_dv_update(false, t0);
  vhm.report_dv_update(false, t0 + std::chrono::seconds(1));  // DV → Degraded
  vhm.report_ev_age(0.15, t0 + std::chrono::seconds(2));      // EV → Critical
  vhm.report_sv_age(35.0, t0 + std::chrono::seconds(3));      // SV → Degraded

  auto before = vhm.aggregated_health();
  EXPECT_EQ(before.dv_health, ViewHealth::Degraded);
  EXPECT_EQ(before.ev_health, ViewHealth::Critical);
  EXPECT_EQ(before.sv_health, ViewHealth::Degraded);

  vhm.reset();

  auto after = vhm.aggregated_health();
  EXPECT_EQ(after.dv_health, ViewHealth::Full);
  EXPECT_EQ(after.ev_health, ViewHealth::Full);
  EXPECT_EQ(after.sv_health, ViewHealth::Full);
  EXPECT_DOUBLE_EQ(after.dv_confidence, 1.0);
  EXPECT_DOUBLE_EQ(after.ev_confidence, 1.0);
  EXPECT_DOUBLE_EQ(after.sv_confidence, 1.0);
  EXPECT_DOUBLE_EQ(after.aggregated, 1.0);
}

}  // namespace
}  // namespace mass_l3::m2
