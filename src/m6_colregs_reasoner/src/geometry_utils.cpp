#include "m6_colregs_reasoner/geometry_utils.hpp"
#include <Eigen/Core>
#include <cmath>
#include <algorithm>

namespace mass_l3::m6_colregs {

double normalize_bearing_deg(double bearing) {
  bearing = std::fmod(bearing, 360.0);
  if (bearing < 0.0) {
    bearing += 360.0;
  }
  return bearing;
}

double angle_diff_deg(double a, double b) {
  const double diff = std::fmod(std::abs(a - b), 360.0);
  return std::min(diff, 360.0 - diff);
}

double relative_bearing_deg(double own_heading_deg, double target_bearing_deg) {
  const double raw = target_bearing_deg - own_heading_deg;
  return normalize_bearing_deg(raw);
}

double aspect_angle_deg(double own_heading_deg, double target_heading_deg,
                        double relative_bearing_deg) {
  const double raw = target_heading_deg - own_heading_deg - relative_bearing_deg + 180.0;
  return normalize_bearing_deg(raw);
}

double haversine_m(double lat1_deg, double lon1_deg,
                   double lat2_deg, double lon2_deg) {
  constexpr double kEarthRadiusM = 6371000.0;
  const double d_lat = deg2rad(lat2_deg - lat1_deg);
  const double d_lon = deg2rad(lon2_deg - lon1_deg);
  const double lat1_rad = deg2rad(lat1_deg);
  const double lat2_rad = deg2rad(lat2_deg);

  const double a = std::sin(d_lat / 2.0) * std::sin(d_lat / 2.0) +
                   std::cos(lat1_rad) * std::cos(lat2_rad) *
                   std::sin(d_lon / 2.0) * std::sin(d_lon / 2.0);
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusM * c;
}

}  // namespace mass_l3::m6_colregs
