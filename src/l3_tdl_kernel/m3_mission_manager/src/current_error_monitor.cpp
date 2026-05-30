// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary

#include "m3_mission_manager/current_error_monitor.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::m3 {

CurrentErrorMonitor::CurrentErrorMonitor(CurrentErrorMonitorConfig cfg)
    : cfg_(cfg) {}

void CurrentErrorMonitor::update_route_state(
    double own_lat_deg, double own_lon_deg,
    double seg_start_lat_deg, double seg_start_lon_deg,
    double seg_end_lat_deg,   double seg_end_lon_deg)
{
  const double kDeg2Rad = 3.14159265358979323846 / 180.0;
  const double kEarthNm = 3440.06479;

  const double cos_lat = std::cos(seg_start_lat_deg * kDeg2Rad);
  const double own_N = (own_lat_deg         - seg_start_lat_deg) * kDeg2Rad * kEarthNm;
  const double own_E = (own_lon_deg         - seg_start_lon_deg) * kDeg2Rad * kEarthNm * cos_lat;
  const double end_N = (seg_end_lat_deg     - seg_start_lat_deg) * kDeg2Rad * kEarthNm;
  const double end_E = (seg_end_lon_deg     - seg_start_lon_deg) * kDeg2Rad * kEarthNm * cos_lat;

  const double seg_len = std::sqrt(end_N * end_N + end_E * end_E);
  if (seg_len < 1e-3) {
    last_geometric_xte_nm_ = -1.0F;
    return;
  }
  const double xte_nm = (own_N * end_E - own_E * end_N) / seg_len;
  last_geometric_xte_nm_ = static_cast<float>(std::abs(xte_nm));
}

void CurrentErrorMonitor::update_tracking_error(
    const l3_external_msgs::msg::TrackingError& msg,
    std::chrono::steady_clock::time_point now)
{
  last_xte_nm_ = msg.xte_nm;
  last_tracking_error_time_ = now;
}

void CurrentErrorMonitor::update_world_state(
    const l3_msgs::msg::WorldState& msg,
    std::chrono::steady_clock::time_point /*now*/)
{
  last_sea_current_kn_ = static_cast<float>(msg.own_ship.current_speed_kn);
}

CurrentErrorReading CurrentErrorMonitor::evaluate(
    std::chrono::steady_clock::time_point now) const
{
  bool l4_available = false;
  float effective_xte = -1.0F;

  if (last_tracking_error_time_.has_value()) {
    const double elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
        now - last_tracking_error_time_.value()).count();
    if (elapsed <= cfg_.l4_stale_s) {
      l4_available = true;
      effective_xte = last_xte_nm_;
    }
  }

  if (!l4_available) {
    effective_xte = last_geometric_xte_nm_;
  }

  const CurrentErrorSeverity sev = compute_severity(effective_xte, last_sea_current_kn_);
  return CurrentErrorReading{sev, effective_xte, last_sea_current_kn_, l4_available};
}

CurrentErrorSeverity CurrentErrorMonitor::compute_severity(
    float effective_xte_nm, float sea_current_kn) const
{
  const bool xte_high = (effective_xte_nm >= 0.0F) && (effective_xte_nm > cfg_.xte_high_nm);
  const bool cur_high = (sea_current_kn > cfg_.current_high_kn);
  if (xte_high || cur_high) {
    return CurrentErrorSeverity::HIGH;
  }

  const bool xte_med = (effective_xte_nm >= 0.0F) && (effective_xte_nm > cfg_.xte_medium_nm);
  const bool cur_med = (sea_current_kn > cfg_.current_medium_kn);
  if (xte_med || cur_med) {
    return CurrentErrorSeverity::MEDIUM;
  }

  return CurrentErrorSeverity::NORMAL;
}

}  // namespace mass_l3::m3
