#ifndef MASS_L3_M5_TAIL_BUILDER_ROUTE_FRAME_HPP_
#define MASS_L3_M5_TAIL_BUILDER_ROUTE_FRAME_HPP_

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace mass_l3::m5::tail_builder {

struct GeoWP {
  double x_m{0.0};
  double y_m{0.0};
  double speed_mps{0.0};
  std::string nav_mode;
};

struct RouteProjection {
  double s_m{0.0};
  double l_m{0.0};
  bool valid{false};
};

struct RouteFrame {
  std::vector<GeoWP> waypoints;

  [[nodiscard]] double length_m() const;
  [[nodiscard]] RouteProjection project(const GeoWP& point) const;
  [[nodiscard]] GeoWP sample(double s_m, double lateral_m, double speed_mps) const;
};

// route_frame_has_sharp_corner (retained for M6 integration, P4 VR-02).
[[nodiscard]] bool route_frame_has_sharp_corner(const RouteFrame& route_frame) noexcept;

}  // namespace mass_l3::m5::tail_builder

#endif  // MASS_L3_M5_TAIL_BUILDER_ROUTE_FRAME_HPP_
