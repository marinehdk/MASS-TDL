#include "m5_tactical_planner/tail_builder/tail_builder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include "l3_msgs/msg/avoidance_plan.hpp"

namespace mass_l3::m5::tail_builder {

namespace {

[[nodiscard]] double dot(double ax, double ay, double bx, double by) noexcept
{
  return ax * bx + ay * by;
}

}  // namespace

double RouteFrame::length_m() const
{
  double total = 0.0;
  for (std::size_t i = 1; i < waypoints.size(); ++i) {
    total += std::hypot(waypoints[i].x_m - waypoints[i - 1].x_m,
                        waypoints[i].y_m - waypoints[i - 1].y_m);
  }
  return total;
}

RouteProjection RouteFrame::project(const GeoWP& point) const
{
  RouteProjection best{};
  if (waypoints.size() < 2U) {
    return best;
  }

  double best_dist = std::numeric_limits<double>::max();
  double station_base = 0.0;
  for (std::size_t i = 1; i < waypoints.size(); ++i) {
    const auto& a = waypoints[i - 1];
    const auto& b = waypoints[i];
    const double vx = b.x_m - a.x_m;
    const double vy = b.y_m - a.y_m;
    const double len = std::hypot(vx, vy);
    if (len <= 1.0e-6) {
      continue;
    }
    const double px = point.x_m - a.x_m;
    const double py = point.y_m - a.y_m;
    const double t = std::clamp(dot(px, py, vx, vy) / (len * len), 0.0, 1.0);
    const double qx = a.x_m + t * vx;
    const double qy = a.y_m + t * vy;
    const double dx = point.x_m - qx;
    const double dy = point.y_m - qy;
    const double dist = std::hypot(dx, dy);
    if (dist < best_dist) {
      best_dist = dist;
      best.valid = true;
      best.s_m = station_base + t * len;
      best.l_m = ((vx * dy) - (vy * dx)) / len;
    }
    station_base += len;
  }
  return best;
}

GeoWP RouteFrame::sample(double s_m, double lateral_m, double speed_mps) const
{
  if (waypoints.empty()) {
    return GeoWP{0.0, lateral_m, speed_mps, "tail_builder"};
  }
  if (waypoints.size() == 1U) {
    auto point = waypoints.front();
    point.y_m += lateral_m;
    point.speed_mps = speed_mps;
    return point;
  }

  double remaining = std::clamp(s_m, 0.0, length_m());
  for (std::size_t i = 1; i < waypoints.size(); ++i) {
    const auto& a = waypoints[i - 1];
    const auto& b = waypoints[i];
    const double vx = b.x_m - a.x_m;
    const double vy = b.y_m - a.y_m;
    const double len = std::hypot(vx, vy);
    if (len <= 1.0e-6) {
      continue;
    }
    if (remaining <= len || i + 1U == waypoints.size()) {
      const double t = std::clamp(remaining / len, 0.0, 1.0);
      const double nx = -vy / len;
      const double ny = vx / len;
      return GeoWP{a.x_m + t * vx + lateral_m * nx,
                   a.y_m + t * vy + lateral_m * ny,
                   speed_mps,
                   "tail_builder"};
    }
    remaining -= len;
  }
  auto point = waypoints.back();
  point.speed_mps = speed_mps;
  return point;
}

namespace {

constexpr double kMaxRouteCornerHeadingDeltaRad = 3.14159265358979323846 / 4.0;
constexpr double kLocalPi = 3.14159265358979323846;

[[nodiscard]] double heading_rad(double dx, double dy) noexcept
{
  return std::atan2(dy, dx);
}

[[nodiscard]] double heading_delta_rad(double a_rad, double b_rad) noexcept
{
  double delta = std::fmod(a_rad - b_rad + kLocalPi, 2.0 * kLocalPi);
  if (delta < 0.0) {
    delta += 2.0 * kLocalPi;
  }
  return std::abs(delta - kLocalPi);
}

}  // namespace

// route_frame_has_sharp_corner (public, retained for M6 integration).
bool route_frame_has_sharp_corner(const RouteFrame& route_frame) noexcept
{
  if (route_frame.waypoints.size() < 3U) {
    return false;
  }

  for (std::size_t i = 1; i + 1U < route_frame.waypoints.size(); ++i) {
    const auto& prev = route_frame.waypoints[i - 1U];
    const auto& corner = route_frame.waypoints[i];
    const auto& next = route_frame.waypoints[i + 1U];
    const double ax = corner.x_m - prev.x_m;
    const double ay = corner.y_m - prev.y_m;
    const double bx = next.x_m - corner.x_m;
    const double by = next.y_m - corner.y_m;
    const double a_len = std::hypot(ax, ay);
    const double b_len = std::hypot(bx, by);
    if (a_len <= 1.0e-6 || b_len <= 1.0e-6) {
      return true;
    }
    if (heading_delta_rad(heading_rad(ax, ay), heading_rad(bx, by)) >
        kMaxRouteCornerHeadingDeltaRad) {
      return true;
    }
  }
  return false;
}

}  // namespace mass_l3::m5::tail_builder
