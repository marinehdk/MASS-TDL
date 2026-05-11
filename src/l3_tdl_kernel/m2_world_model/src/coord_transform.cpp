#include "m2_world_model/coord_transform.hpp"

namespace mass_l3::m2 {

void CoordTransform::init(double origin_lat_deg, double origin_lon_deg, double origin_alt_m) noexcept {
  proj_.Reset(origin_lat_deg, origin_lon_deg, origin_alt_m);
  initialized_ = true;
}

bool CoordTransform::wgs84_to_enu(
    double lat_deg, double lon_deg, double sog_kn, double cog_deg,
    Eigen::Vector2d& pos, Eigen::Vector2d& vel) const noexcept {
  if (!initialized_) {
    return false;
  }

  double east = 0.0;
  double north = 0.0;
  double up = 0.0;
  try {
    proj_.Forward(lat_deg, lon_deg, 0.0, east, north, up);
  } catch (...) {
    return false;
  }

  pos(0) = east;
  pos(1) = north;

  // SOG: knots -> m/s  (1 kn = 0.514444 m/s)
  // COG: degrees -> radians
  constexpr double kKnToMs = 0.514444;
  constexpr double kDegToRad = M_PI / 180.0;

  double speed_ms = sog_kn * kKnToMs;
  double cog_rad = cog_deg * kDegToRad;

  // ENU frame: east = speed * sin(cog), north = speed * cos(cog)
  // COG is measured clockwise from true north
  vel(0) = speed_ms * std::sin(cog_rad);
  vel(1) = speed_ms * std::cos(cog_rad);

  return true;
}

bool CoordTransform::enu_to_wgs84(
    double east_m, double north_m, double& lat_deg, double& lon_deg) const noexcept {
  if (!initialized_) {
    return false;
  }

  double up = 0.0;
  try {
    proj_.Reverse(east_m, north_m, 0.0, lat_deg, lon_deg, up);
  } catch (...) {
    return false;
  }

  return true;
}

}  // namespace mass_l3::m2
