// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// Unit tests for CurrentErrorMonitor — spec D2.3 §7.1

#include <gtest/gtest.h>
#include <chrono>
#include "m3_mission_manager/current_error_monitor.hpp"

namespace mass_l3::m3 {
namespace {

CurrentErrorMonitorConfig make_cfg() {
  return CurrentErrorMonitorConfig{0.5F, 0.3F, 2.0F, 1.5F, 2.0};
}

l3_external_msgs::msg::TrackingError make_te(float xte_nm) {
  l3_external_msgs::msg::TrackingError msg;
  msg.xte_nm = xte_nm;
  msg.along_track_error_nm = 0.0F;
  msg.confidence = 1.0F;
  return msg;
}

l3_msgs::msg::WorldState make_ws(double sea_current_kn) {
  l3_msgs::msg::WorldState msg;
  msg.own_ship.current_speed_kn = sea_current_kn;
  return msg;
}

// §7.1 case 1: both sources normal → NORMAL
TEST(CurrentErrorMonitorTest, BothSourcesNormal) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.update_tracking_error(make_te(0.2F), now);
  mon.update_world_state(make_ws(1.0), now);
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.severity, CurrentErrorSeverity::NORMAL);
  EXPECT_TRUE(r.l4_available);
  EXPECT_FLOAT_EQ(r.sea_current_kn, 1.0F);
}

// §7.1 case 2: XTE > 0.5 → HIGH
TEST(CurrentErrorMonitorTest, XteExceedsHighThreshold) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.update_tracking_error(make_te(0.6F), now);
  mon.update_world_state(make_ws(0.5), now);
  EXPECT_EQ(mon.evaluate(now).severity, CurrentErrorSeverity::HIGH);
}

// §7.1 case 3: sea_current > 2.0 kn → HIGH (cur主导)
TEST(CurrentErrorMonitorTest, SeaCurrentExceedsHighThreshold) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.update_tracking_error(make_te(0.1F), now);
  mon.update_world_state(make_ws(2.5), now);
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.severity, CurrentErrorSeverity::HIGH);
}

// §7.1 case 4: sea_current 1.8 kn → MEDIUM
TEST(CurrentErrorMonitorTest, SeaCurrentMedium) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.update_tracking_error(make_te(0.2F), now);
  mon.update_world_state(make_ws(1.8), now);
  EXPECT_EQ(mon.evaluate(now).severity, CurrentErrorSeverity::MEDIUM);
}

// §7.1 case 5: L4 stale > 2s + cur normal → NORMAL, xte=-1
TEST(CurrentErrorMonitorTest, L4TimeoutCurrentNormal) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now  = std::chrono::steady_clock::now();
  const auto past = now - std::chrono::seconds(5);
  mon.update_tracking_error(make_te(0.6F), past);
  mon.update_world_state(make_ws(0.8), now);
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.severity, CurrentErrorSeverity::NORMAL);
  EXPECT_FALSE(r.l4_available);
  EXPECT_FLOAT_EQ(r.xte_nm, -1.0F);
}

// §7.1 case 6: L4 stale + cur > 2.0 → HIGH (cur主导)
TEST(CurrentErrorMonitorTest, L4TimeoutCurrentHigh) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now  = std::chrono::steady_clock::now();
  const auto past = now - std::chrono::seconds(5);
  mon.update_tracking_error(make_te(0.6F), past);
  mon.update_world_state(make_ws(2.5), now);
  const auto r = mon.evaluate(now);
  EXPECT_EQ(r.severity, CurrentErrorSeverity::HIGH);
  EXPECT_FALSE(r.l4_available);
}

// §7.1 case 7: HIGH → NORMAL no hysteresis
TEST(CurrentErrorMonitorTest, HighToNormalNoHysteresis) {
  CurrentErrorMonitor mon(make_cfg());
  const auto now = std::chrono::steady_clock::now();
  mon.update_tracking_error(make_te(0.6F), now);
  mon.update_world_state(make_ws(0.5), now);
  EXPECT_EQ(mon.evaluate(now).severity, CurrentErrorSeverity::HIGH);
  mon.update_tracking_error(make_te(0.3F), now);
  EXPECT_EQ(mon.evaluate(now).severity, CurrentErrorSeverity::NORMAL);
}

}  // namespace
}  // namespace mass_l3::m3
