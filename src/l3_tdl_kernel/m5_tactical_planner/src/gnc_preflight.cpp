#include "m5_tactical_planner/gnc_preflight.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace mass_l3::m5::gnc_preflight {
namespace {

constexpr std::size_t kMinWaypointCount = 2U;
constexpr double kEpsilon = 1.0e-9;
constexpr double kPointCompareEpsilon = 1.0e-6;

GncPreflightResult accept()
{
  return GncPreflightResult{true, ""};
}

GncPreflightResult reject(const std::string& reason)
{
  return GncPreflightResult{false, reason};
}

std::size_t route_size(const GncPreflightRoute& route)
{
  return route.latitude_deg.size();
}

bool equal_lengths(const GncPreflightRoute& route)
{
  const std::size_t n = route_size(route);
  return route.longitude_deg.size() == n && route.x_m.size() == n && route.y_m.size() == n &&
         route.speed_mps.size() == n && route.segment_source.size() == n;
}

bool finite_xy_speed(const GncPreflightRoute& route)
{
  const std::size_t n = route_size(route);
  for (std::size_t i = 0U; i < n; ++i) {
    if (!std::isfinite(route.x_m[i]) || !std::isfinite(route.y_m[i]) ||
        !std::isfinite(route.speed_mps[i]) || route.speed_mps[i] < 0.0) {
      return false;
    }
  }
  return true;
}

bool finite_wgs84(const GncPreflightRoute& route)
{
  const std::size_t n = route_size(route);
  for (std::size_t i = 0U; i < n; ++i) {
    const double lat = route.latitude_deg[i];
    const double lon = route.longitude_deg[i];
    if (!std::isfinite(lat) || !std::isfinite(lon) || lat < -90.0 || lat > 90.0 || lon < -180.0 ||
        lon > 180.0) {
      return false;
    }
  }
  return true;
}

double segment_length(const GncPreflightRoute& route, const std::size_t from)
{
  const double dx = route.x_m[from + 1U] - route.x_m[from];
  const double dy = route.y_m[from + 1U] - route.y_m[from];
  return std::hypot(dx, dy);
}

bool has_reverse_segment(const GncPreflightRoute& route)
{
  const std::size_t n = route_size(route);
  for (std::size_t i = 1U; i + 1U < n; ++i) {
    const double prev_dx = route.x_m[i] - route.x_m[i - 1U];
    const double prev_dy = route.y_m[i] - route.y_m[i - 1U];
    const double next_dx = route.x_m[i + 1U] - route.x_m[i];
    const double next_dy = route.y_m[i + 1U] - route.y_m[i];
    if ((prev_dx * next_dx + prev_dy * next_dy) < -kEpsilon) {
      return true;
    }
  }
  return false;
}

bool collinear_or_degenerate(const double area2)
{
  return std::abs(area2) <= kEpsilon;
}

double turn_radius(const GncPreflightRoute& route, const std::size_t center)
{
  const double ax = route.x_m[center - 1U];
  const double ay = route.y_m[center - 1U];
  const double bx = route.x_m[center];
  const double by = route.y_m[center];
  const double cx = route.x_m[center + 1U];
  const double cy = route.y_m[center + 1U];

  const double ab = std::hypot(bx - ax, by - ay);
  const double bc = std::hypot(cx - bx, cy - by);
  const double ca = std::hypot(ax - cx, ay - cy);
  const double area2 = std::abs(((bx - ax) * (cy - ay)) - ((by - ay) * (cx - ax)));
  if (collinear_or_degenerate(area2)) {
    return std::numeric_limits<double>::infinity();
  }
  return (ab * bc * ca) / (2.0 * area2);
}

bool point_changed(const GncPreflightRoute& route, const GncPreflightRoute& previous, const std::size_t i)
{
  return std::abs(route.latitude_deg[i] - previous.latitude_deg[i]) > kPointCompareEpsilon ||
         std::abs(route.longitude_deg[i] - previous.longitude_deg[i]) > kPointCompareEpsilon ||
         std::abs(route.x_m[i] - previous.x_m[i]) > kPointCompareEpsilon ||
         std::abs(route.y_m[i] - previous.y_m[i]) > kPointCompareEpsilon ||
         std::abs(route.speed_mps[i] - previous.speed_mps[i]) > kPointCompareEpsilon ||
         route.segment_source[i] != previous.segment_source[i];
}

bool valid_protected_exception(const GncPreflightProtection& protection)
{
  if (!protection.enabled) {
    return true;
  }
  return protection.side != mass_l3::m5::tail_builder::ColregSide::NONE && !protection.metadata.empty();
}

bool protection_active(const GncPreflightProtection& protection)
{
  return protection.enabled && valid_protected_exception(protection);
}

bool finite_positive(const double value)
{
  return std::isfinite(value) && value > 0.0;
}

GncPreflightResult validate_odd(const mass_l3::m5::tail_builder::GncExecutionOdd& odd)
{
  if (!finite_positive(odd.ship_length_m)) {
    return reject("odd_ship_length_m");
  }
  if (!finite_positive(odd.max_lateral_offset_m)) {
    return reject("odd_max_lateral_offset_m");
  }
  if (!finite_positive(odd.min_segment_length_m)) {
    return reject("odd_min_segment_length_m");
  }
  if (!finite_positive(odd.min_turn_radius_m)) {
    return reject("odd_min_turn_radius_m");
  }
  if (!finite_positive(odd.max_yaw_rate_rad_s)) {
    return reject("odd_max_yaw_rate_rad_s");
  }
  if (!finite_positive(odd.max_lateral_accel_mps2)) {
    return reject("odd_max_lateral_accel_mps2");
  }
  if (!finite_positive(odd.max_decel_mps2)) {
    return reject("odd_max_decel_mps2");
  }
  if (!finite_positive(odd.min_first_changed_distance_m)) {
    return reject("odd_min_first_changed_distance_m");
  }
  if (!finite_positive(odd.min_update_interval_s)) {
    return reject("odd_min_update_interval_s");
  }
  return accept();
}

double waypoint_displacement(const GncPreflightRoute& route, const GncPreflightRoute& previous,
                             const std::size_t i)
{
  return std::hypot(route.x_m[i] - previous.x_m[i], route.y_m[i] - previous.y_m[i]);
}

}  // namespace

GncPreflightResult validate(const GncPreflightInput& input)
{
  const auto& odd_result = validate_odd(input.odd);
  if (!odd_result.accepted) {
    return odd_result;
  }

  const auto& route = input.route;
  if (!equal_lengths(route)) {
    return reject("equal_length");
  }
  const std::size_t n = route_size(route);
  if (n < kMinWaypointCount) {
    return reject("min_waypoint_count");
  }
  if (!std::isfinite(input.update_interval_s) ||
      input.update_interval_s < input.odd.min_update_interval_s) {
    return reject("min_update_interval");
  }
  if (!std::isfinite(input.current_x_m) || !std::isfinite(input.current_y_m)) {
    return reject("finite_current_position");
  }
  if (!valid_protected_exception(input.protection)) {
    return reject("protected_colreg_exception");
  }
  if (!finite_wgs84(route)) {
    return reject("finite_wgs84");
  }
  if (!finite_xy_speed(route)) {
    return reject("finite_route");
  }

  for (std::size_t i = 0U; i + 1U < n; ++i) {
    const double distance = segment_length(route, i);
    if (distance < input.odd.min_segment_length_m) {
      return reject("min_segment_length");
    }
    const double decel = ((route.speed_mps[i] * route.speed_mps[i]) -
                          (route.speed_mps[i + 1U] * route.speed_mps[i + 1U])) /
                         (2.0 * distance);
    if (decel > input.odd.max_decel_mps2) {
      return reject("decel");
    }
  }

  if (has_reverse_segment(route)) {
    return reject("reverse_segment");
  }

  for (std::size_t i = 1U; i + 1U < n; ++i) {
    const double radius = turn_radius(route, i);
    if (radius < input.odd.min_turn_radius_m) {
      return reject("turn_radius");
    }
    const double speed = route.speed_mps[i];
    const double yaw_rate = speed / radius;
    if (yaw_rate > input.odd.max_yaw_rate_rad_s) {
      return reject("yaw_rate");
    }
    const double lateral_accel = (speed * speed) / radius;
    if (lateral_accel > input.odd.max_lateral_accel_mps2) {
      return reject("lateral_accel");
    }
  }

  if (input.previous_route.has_value()) {
    const auto& previous = input.previous_route.value();
    if (!equal_lengths(previous) || route_size(previous) != n) {
      return reject("previous_equal_length");
    }
    if (!finite_wgs84(previous) || !finite_xy_speed(previous)) {
      return reject("previous_finite_route");
    }

    for (std::size_t i = 0U; i < n; ++i) {
      const double lateral_delta = waypoint_displacement(route, previous, i);
      if (lateral_delta > input.odd.max_lateral_offset_m && !protection_active(input.protection)) {
        return reject("max_lateral_delta");
      }
    }

    for (std::size_t i = 0U; i < n; ++i) {
      if (point_changed(route, previous, i)) {
        const double distance = std::hypot(route.x_m[i] - input.current_x_m,
                                           route.y_m[i] - input.current_y_m);
        if (distance < input.odd.min_first_changed_distance_m &&
            !protection_active(input.protection)) {
          return reject("first_changed_distance");
        }
        break;
      }
    }
  }

  return accept();
}

}  // namespace mass_l3::m5::gnc_preflight
