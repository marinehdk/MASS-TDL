#pragma once

#include <cmath>

namespace mass_l3::m6_colregs {

constexpr double kGiveWayProjectionReleaseRangeMultiple = 2.0;
constexpr double kGiveWayProjectionReleaseCurrentAbaftDeg = 150.0;
constexpr double kGiveWayProjectionReleaseReferenceBowClearDeg = 40.0;

enum class GiveWayProjectionReleaseGate {
  REFERENCE_CLEAR,
  CURRENT_ABAFT,
};

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

}  // namespace mass_l3::m6_colregs
