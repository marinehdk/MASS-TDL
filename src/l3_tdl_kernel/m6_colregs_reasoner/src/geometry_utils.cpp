#include "m6_colregs_reasoner/geometry_utils.hpp"
#include <algorithm>
#include <cmath>

namespace mass_l3::m6_colregs {

double normalize_bearing_deg(double bearing) {
  bearing = std::fmod(bearing, 360.0);
  if (bearing < 0.0) {
    bearing += 360.0;
  }
  return bearing;
}

double angle_diff_deg(double a, double b) {
  const double kDiff = std::fmod(std::abs(a - b), 360.0);
  return std::min(kDiff, 360.0 - kDiff);
}

double relative_bearing_deg(double own_heading_deg, double target_bearing_deg) {
  const double kRaw = target_bearing_deg - own_heading_deg;
  return normalize_bearing_deg(kRaw);
}

double aspect_angle_deg(double own_heading_deg, double target_heading_deg,
                        double relative_bearing_deg) {
  const double kRaw = target_heading_deg - own_heading_deg - relative_bearing_deg + 180.0;
  return normalize_bearing_deg(kRaw);
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double haversine_m(double lat1_deg, double lon1_deg, double lat2_deg, double lon2_deg) {
  constexpr double kEarthRadiusM = 6371000.0;
  const double kDLat = deg2rad(lat2_deg - lat1_deg);
  const double kDLon = deg2rad(lon2_deg - lon1_deg);
  const double kLat1Rad = deg2rad(lat1_deg);
  const double kLat2Rad = deg2rad(lat2_deg);

  const double kA = (std::sin(kDLat / 2.0) * std::sin(kDLat / 2.0)) +
                    (std::cos(kLat1Rad) * std::cos(kLat2Rad) *
                    (std::sin(kDLon / 2.0) * std::sin(kDLon / 2.0)));
  const double kC = 2.0 * std::atan2(std::sqrt(kA), std::sqrt(1.0 - kA));
  return kEarthRadiusM * kC;
}

}  // namespace mass_l3::m6_colregs
