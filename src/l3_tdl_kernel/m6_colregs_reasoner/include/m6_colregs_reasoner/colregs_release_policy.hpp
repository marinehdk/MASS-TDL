#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace mass_l3::m6_colregs {

constexpr double kGiveWayProjectionReleaseRangeMultiple = 1.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 40.0;
constexpr double kGiveWayReleaseKnToMps = 0.514444;
constexpr double kGiveWayReleasePi = 3.14159265358979323846;
constexpr double kStandOnEmergencyReleaseCpaM = 185.2;
constexpr double kStandOnEmergencyReleaseRangeMultiple = 2.0;
constexpr double kTcpaClampedPastEpsilonS = 0.5;

enum class GiveWayProjectionReleaseGate {
  REFERENCE_CLEAR,
  CURRENT_ABAFT,
};

inline double give_way_reference_heading_cpa_m(
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(bearing_deg) ||
      !std::isfinite(target_heading_deg) ||
      !std::isfinite(target_speed_kn) ||
      !std::isfinite(own_speed_kn) ||
      !std::isfinite(reference_heading_deg) ||
      range_m < 0.0) {
    return std::numeric_limits<double>::quiet_NaN();
  }

  const double bearing_rad = bearing_deg * kGiveWayReleasePi / 180.0;
  const double target_heading_rad = target_heading_deg * kGiveWayReleasePi / 180.0;
  const double reference_heading_rad = reference_heading_deg * kGiveWayReleasePi / 180.0;
  const double rel_x = range_m * std::sin(bearing_rad);
  const double rel_y = range_m * std::cos(bearing_rad);
  const double target_speed_mps = target_speed_kn * kGiveWayReleaseKnToMps;
  const double own_speed_mps = own_speed_kn * kGiveWayReleaseKnToMps;
  const double rel_vx = target_speed_mps * std::sin(target_heading_rad) -
      own_speed_mps * std::sin(reference_heading_rad);
  const double rel_vy = target_speed_mps * std::cos(target_heading_rad) -
      own_speed_mps * std::cos(reference_heading_rad);
  const double rel_v2 = rel_vx * rel_vx + rel_vy * rel_vy;
  double tcpa_s = 0.0;
  if (rel_v2 > 1e-9) {
    tcpa_s = -((rel_x * rel_vx) + (rel_y * rel_vy)) / rel_v2;
    if (tcpa_s < 0.0) {
      tcpa_s = 0.0;
    }
  }
  const double cpa_x = rel_x + rel_vx * tcpa_s;
  const double cpa_y = rel_y + rel_vy * tcpa_s;
  return std::hypot(cpa_x, cpa_y);
}

inline bool give_way_reference_heading_release_safe(
    double range_m,
    double bearing_deg,
    double target_heading_deg,
    double target_speed_kn,
    double own_speed_kn,
    double reference_heading_deg,
    double cpa_safe_m) {
  if (!std::isfinite(range_m) ||
      !std::isfinite(cpa_safe_m) ||
      cpa_safe_m <= 0.0 ||
      range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple) {
    return false;
  }
  const double cpa_m = give_way_reference_heading_cpa_m(
      range_m,
      bearing_deg,
      target_heading_deg,
      target_speed_kn,
      own_speed_kn,
      reference_heading_deg);
  return std::isfinite(cpa_m) && cpa_m >= cpa_safe_m;
}

inline bool give_way_projection_release_safe(
    bool cpa_projection_past_and_safe,
    double range_m,
    double cpa_safe_m,
    double current_relative_bearing_abs_deg,
    double reference_relative_bearing_abs_deg,
    GiveWayProjectionReleaseGate gate) {
  if (!cpa_projection_past_and_safe ||
      !std::isfinite(range_m) ||
      !std::isfinite(cpa_safe_m) ||
      !std::isfinite(current_relative_bearing_abs_deg) ||
      !std::isfinite(reference_relative_bearing_abs_deg) ||
      cpa_safe_m <= 0.0) {
    return false;
  }
  if (range_m < cpa_safe_m * kGiveWayProjectionReleaseRangeMultiple ||
      reference_relative_bearing_abs_deg < kGiveWayProjectionReleaseReferenceBowClearDeg) {
    return false;
  }
  return gate == GiveWayProjectionReleaseGate::REFERENCE_CLEAR ||
      current_relative_bearing_abs_deg >= kGiveWayProjectionReleaseCurrentAbaftDeg;
}

inline double stand_on_release_cpa_floor_m(double configured_cpa_safe_m) {
  if (!std::isfinite(configured_cpa_safe_m) || configured_cpa_safe_m <= 0.0) {
    return kStandOnEmergencyReleaseCpaM;
  }
  return std::min(configured_cpa_safe_m, kStandOnEmergencyReleaseCpaM);
}

inline bool stand_on_late_action_release_safe(
    bool latched,
    bool range_closing,
    double range_m,
    double cpa_m,
    double tcpa_s,
    double configured_cpa_safe_m,
    double current_relative_bearing_abs_deg) {
  const double cpa_floor_m = stand_on_release_cpa_floor_m(configured_cpa_safe_m);
  return latched &&
      !range_closing &&
      std::isfinite(range_m) &&
      std::isfinite(cpa_m) &&
      std::isfinite(tcpa_s) &&
      std::isfinite(current_relative_bearing_abs_deg) &&
      range_m >= cpa_floor_m * kStandOnEmergencyReleaseRangeMultiple &&
      cpa_m >= cpa_floor_m &&
      tcpa_s <= kTcpaClampedPastEpsilonS;
}

}  // namespace mass_l3::m6_colregs
