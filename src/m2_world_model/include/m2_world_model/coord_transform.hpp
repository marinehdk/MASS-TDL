#pragma once

#include <cmath>

#include <GeographicLib/LocalCartesian.hpp>
#include <Eigen/Core>

#include "m2_world_model/error.hpp"
#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/// WGS84 ↔ ENU coordinate transforms using GeographicLib.
/// Single static origin set by init(); all transforms relative to that origin.
class CoordTransform final {
 public:
  CoordTransform() = default;
  /// Set ENU origin at given WGS84 lat/lon.
  void init(double origin_lat_deg, double origin_lon_deg, double origin_alt_m = 0.0) noexcept;

  /// Convert WGS84 lat/lon/heading to ENU position + velocity.
  /// @param pos   ENU position [east_m, north_m] (output)[]
  /// @param vel   ENU velocity [east_mps, north_mps] (output)[]
  [[nodiscard]] bool wgs84_to_enu(
      double lat_deg, double lon_deg, double sog_kn, double cog_deg,
      Eigen::Vector2d& pos, Eigen::Vector2d& vel) const noexcept;

  /// Convert ENU position back to WGS84 lat/lon.
  [[nodiscard]] bool enu_to_wgs84(
      double east_m, double north_m, double& lat_deg, double& lon_deg) const noexcept;

  bool initialized() const noexcept { return initialized_; }

 private:
  GeographicLib::LocalCartesian proj_;
  bool initialized_ = false;
};

}  // namespace mass_l3::m2
