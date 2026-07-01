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

constexpr double kMinSafeOffsetM = 25.0;

[[nodiscard]] double clamp_abs(double value, double min_abs, double max_abs) noexcept
{
  const double sign = value < 0.0 ? -1.0 : 1.0;
  const double mag = std::clamp(std::abs(value), min_abs, max_abs);
  return sign * mag;
}

[[nodiscard]] double smoothstep(double x) noexcept
{
  const double t = std::clamp(x, 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] bool m6_reports_clear(const TailInputs& inputs) noexcept
{
  return inputs.m6_past_clear ||
      inputs.m6_encounter_state == static_cast<std::uint8_t>(EncounterState::Release) ||
      inputs.m6_encounter_state == static_cast<std::uint8_t>(EncounterState::Clear);
}

[[nodiscard]] bool cpa_release_floor_clear(const TailInputs& inputs) noexcept
{
  for (const auto& target : inputs.targets) {
    if ((target.cpa_m - 3.0 * target.cpa_sigma_m) < inputs.cpa_release_m) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool side_matches_m6(ColregSide side, double lateral_m) noexcept
{
  if (side == ColregSide::STBD) {
    return lateral_m > 0.0;
  }
  if (side == ColregSide::PORT) {
    return lateral_m < 0.0;
  }
  return false;
}

[[nodiscard]] double feasible_rejoin_length_m(const TailInputs& inputs, double lateral_m) noexcept
{
  const double speed = std::max(inputs.uN_mps, 0.1);
  const double yaw_limited = std::abs(lateral_m) / std::max(inputs.gnc_odd.max_yaw_rate_rad_s, 1.0e-3);
  const double accel_limited = speed * std::sqrt(std::abs(lateral_m) / std::max(inputs.gnc_odd.max_lateral_accel_mps2, 1.0e-3));
  const double turn_limited = 2.0 * std::sqrt(std::max(inputs.gnc_odd.min_turn_radius_m * std::abs(lateral_m), 0.0));
  return std::max({3.0 * inputs.gnc_odd.min_segment_length_m, yaw_limited, accel_limited, turn_limited});
}

}  // namespace

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
TailResult TailBuilder::build(const TailInputs& inputs)
{
  TailResult result{};

  if (inputs.role == ColregRole::StandOn) {
    return result;
  }
  if (inputs.role != ColregRole::GiveWay && inputs.role != ColregRole::BothGiveWay) {
    result.reject_reason = "not_give_way";
    return result;
  }
  if (inputs.protected_side == ColregSide::NONE) {
    result.reject_reason = "missing_m6_side";
    return result;
  }
  if (!m6_reports_clear(inputs)) {
    result.reject_reason = "m6_not_past_clear";
    return result;
  }
  if (!cpa_release_floor_clear(inputs)) {
    result.reject_reason = "cpa_release_floor";
    return result;
  }

  const auto projection = inputs.route_frame.project(inputs.pN);
  if (!projection.valid) {
    result.reject_reason = "route_projection_failed";
    return result;
  }
  if (!side_matches_m6(inputs.protected_side, projection.l_m)) {
    result.reject_reason = "m6_side_mismatch";
    return result;
  }
  if (std::abs(projection.l_m) < kMinSafeOffsetM) {
    result.reject_reason = "l_hold_too_small";
    return result;
  }

  const double max_offset = std::max(kMinSafeOffsetM, inputs.gnc_odd.max_lateral_offset_m);
  const double l_hold = clamp_abs(projection.l_m, kMinSafeOffsetM, max_offset);
  const double dwell_m = std::clamp(4.0 * inputs.gnc_odd.ship_length_m,
                                    inputs.gnc_odd.min_segment_length_m,
                                    5.0 * inputs.gnc_odd.ship_length_m);
  const double s_clear = projection.s_m;
  const double rejoin_start = s_clear + dwell_m;
  const double rejoin_len = feasible_rejoin_length_m(inputs, l_hold);
  const double end_s = std::min(inputs.route_frame.length_m(), rejoin_start + rejoin_len);
  if (end_s <= rejoin_start + inputs.gnc_odd.min_segment_length_m) {
    result.reject_reason = "insufficient_route_for_rejoin";
    return result;
  }

  TailSegment segment{};
  const double spacing_m = std::clamp(inputs.gnc_odd.min_segment_length_m, 50.0, 150.0);

  for (double s = s_clear; s < rejoin_start; s += spacing_m) {
    segment.waypoints.push_back(inputs.route_frame.sample(s, l_hold, inputs.uN_mps));
    segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);
  }
  segment.waypoints.push_back(inputs.route_frame.sample(rejoin_start, l_hold, inputs.uN_mps));
  segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);

  for (double s = rejoin_start + spacing_m; s < end_s; s += spacing_m) {
    const double progress = (s - rejoin_start) / std::max(rejoin_len, 1.0);
    const double lateral = l_hold * (1.0 - smoothstep(progress));
    if (!side_matches_m6(inputs.protected_side, lateral) && std::abs(lateral) > 1.0) {
      result.reject_reason = "m6_side_violation";
      return result;
    }
    segment.waypoints.push_back(inputs.route_frame.sample(s, lateral, inputs.uN_mps));
    segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2);
  }
  segment.waypoints.push_back(inputs.route_frame.sample(end_s, 0.0, inputs.uN_mps));
  segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2);

  result.hold_then_rejoin = std::move(segment);
  return result;
}

}  // namespace mass_l3::m5::tail_builder
