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
constexpr double kMinTailSpacingM = 50.0;
constexpr double kMaxTailSpacingM = 150.0;
constexpr double kNearRejoinToleranceM = 1.0;
constexpr double kPi = 3.14159265358979323846;
constexpr double kMaxRouteCornerHeadingDeltaRad = kPi / 4.0;
// Slice W1 (spec §5.2): minimum dwell time used to extrapolate s_clear when the
// target's tcpa_s is shorter than a credible past-clear window. [TBD] calibrate.
constexpr double kActiveMinDwellS = 30.0;

[[nodiscard]] bool is_active_phase(const TailInputs& inputs) noexcept
{
  // Active encounter: not yet released (no past_clear, encounter ACTIVE).
  // Rejoin is deferred to the release phase.
  return !inputs.m6_past_clear &&
      inputs.m6_encounter_state == static_cast<std::uint8_t>(EncounterState::Active);
}

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

[[nodiscard]] double heading_rad(double dx, double dy) noexcept
{
  return std::atan2(dy, dx);
}

[[nodiscard]] double heading_delta_rad(double a_rad, double b_rad) noexcept
{
  double delta = std::fmod(a_rad - b_rad + kPi, 2.0 * kPi);
  if (delta < 0.0) {
    delta += 2.0 * kPi;
  }
  return std::abs(delta - kPi);
}

[[nodiscard]] bool target_cpa_evidence_available(const TailInputs& inputs) noexcept
{
  if (inputs.targets.empty()) {
    return false;
  }
  return std::all_of(inputs.targets.begin(), inputs.targets.end(), [](const TargetSnapshot& target) {
    return std::isfinite(target.cpa_m) && std::isfinite(target.cpa_sigma_m) &&
        target.cpa_sigma_m >= 0.0;
  });
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

[[nodiscard]] bool ship_domain_floor_clear(const TailInputs& inputs) noexcept
{
  for (const auto& target : inputs.targets) {
    if ((target.cpa_m - target.cpa_sigma_m) < inputs.cpa_safe_m) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] bool route_frame_has_sharp_corner(const RouteFrame& route_frame) noexcept
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

[[nodiscard]] bool validate_tail_segment(
    const TailInputs& inputs, const TailSegment& segment, std::string& reject_reason,
    bool expect_rejoin)
{
  if (!target_cpa_evidence_available(inputs)) {
    reject_reason = "missing_m2_targets";
    return false;
  }
  // cpa_release_floor / ship_domain_floor are RELEASE criteria (spec §5.2). In
  // the active phase the target is expected to remain inside the CPA band, so
  // these gates are relaxed; they re-engage for release-phase hold+rejoin.
  if (expect_rejoin) {
    if (!cpa_release_floor_clear(inputs)) {
      reject_reason = "cpa_release_floor";
      return false;
    }
    if (!ship_domain_floor_clear(inputs)) {
      reject_reason = "ship_domain_floor";
      return false;
    }
  }
  if (segment.waypoints.empty() || segment.waypoints.size() != segment.source_labels.size()) {
    reject_reason = "tail_waypoints_invalid";
    return false;
  }

  RouteProjection previous_projection{};
  GeoWP previous_wp{};
  double previous_heading = 0.0;
  double previous_spacing = 0.0;
  bool have_previous = false;
  bool have_previous_heading = false;

  for (std::size_t i = 0; i < segment.waypoints.size(); ++i) {
    const auto& waypoint = segment.waypoints[i];
    if (!std::isfinite(waypoint.speed_mps) || waypoint.speed_mps < 0.0) {
      reject_reason = "tail_speed_invalid";
      return false;
    }

    const auto projection = inputs.route_frame.project(waypoint);
    if (!projection.valid) {
      reject_reason = "tail_route_projection_failed";
      return false;
    }

    const bool final_waypoint = (i + 1U == segment.waypoints.size());
    if (!final_waypoint && std::abs(projection.l_m) <= kNearRejoinToleranceM) {
      reject_reason = "tail_crosses_route_early";
      return false;
    }
    if (std::abs(projection.l_m) > kNearRejoinToleranceM &&
        !side_matches_m6(inputs.protected_side, projection.l_m)) {
      reject_reason = "m6_side_violation";
      return false;
    }
    if (expect_rejoin && final_waypoint && std::abs(projection.l_m) > kNearRejoinToleranceM) {
      reject_reason = "tail_rejoin_failed";
      return false;
    }

    if (have_previous) {
      if (projection.s_m + 1.0e-3 < previous_projection.s_m) {
        reject_reason = "tail_reverse_station";
        return false;
      }

      const double dx = waypoint.x_m - previous_wp.x_m;
      const double dy = waypoint.y_m - previous_wp.y_m;
      const double spacing = std::hypot(dx, dy);
      if (spacing < kMinTailSpacingM - 1.0e-3 || spacing > kMaxTailSpacingM + 1.0e-3) {
        reject_reason = "tail_spacing_invalid";
        return false;
      }

      const double current_heading = heading_rad(dx, dy);
      if (have_previous_heading) {
        const double delta = heading_delta_rad(current_heading, previous_heading);
        if (delta > 1.0e-3) {
          const double radius = std::min(previous_spacing, spacing) / (2.0 * std::sin(delta / 2.0));
          if (radius + 1.0e-3 < inputs.gnc_odd.min_turn_radius_m) {
            reject_reason = "tail_turn_radius";
            return false;
          }
        }
      }
      previous_heading = current_heading;
      previous_spacing = spacing;
      have_previous_heading = true;
    }

    previous_projection = projection;
    previous_wp = waypoint;
    have_previous = true;
  }

  return true;
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

  // Slice W1 (spec §5.2): two-phase semantics. The m6_reports_clear gate no
  // longer blocks the whole build. Active encounters (!past_clear, ACTIVE) now
  // generate a hold-only tail to the predicted s_clear (rejoin deferred to
  // release). Only an encounter that is NEITHER active NOR released (abnormal
  // state) rejects m6_not_past_clear.
  const bool active = is_active_phase(inputs);
  const bool released = m6_reports_clear(inputs);
  if (!active && !released) {
    result.reject_reason = "m6_not_past_clear";
    return result;
  }
  if (!target_cpa_evidence_available(inputs)) {
    result.reject_reason = "missing_m2_targets";
    return result;
  }
  // cpa_release_floor / ship_domain_floor are release criteria and are enforced
  // inside validate_tail_segment only when a rejoin is expected (release phase).
  // The active phase relaxes them (target still inside CPA band is expected).
  if (route_frame_has_sharp_corner(inputs.route_frame)) {
    result.reject_reason = "route_frame_sharp_corner";
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
  const double spacing_m = std::clamp(inputs.gnc_odd.min_segment_length_m, 50.0, 150.0);
  const double route_len = inputs.route_frame.length_m();

  if (active) {
    // ── Active phase (spec §5.2): hold-only to the PREDICTED s_clear.
    // Rejoin is deferred until the encounter releases. s_clear is the station
    // the own ship reaches once the target has passed and cleared:
    //   s_clear ≈ s_pN + own_u_hold · max(tcpa_s, T_min_dwell).
    // Honest degradation (spec §14.3): if release is not predicted AND no valid
    // tcpa_s is available to extrapolate, do NOT fabricate a hold-to-horizon
    // tail — reject so the candidate falls back to DegradedHold.
    const double own_u_hold = std::max(inputs.uN_mps, 0.1);
    double dwell_s = 0.0;
    bool s_clear_available = inputs.m6_release_predicted;
    if (!s_clear_available) {
      // Try to extrapolate from the primary target's tcpa_s.
      if (!inputs.targets.empty() && std::isfinite(inputs.targets.front().tcpa_s) &&
          inputs.targets.front().tcpa_s > 0.0) {
        dwell_s = inputs.targets.front().tcpa_s;
        s_clear_available = true;
      }
    } else if (!inputs.targets.empty() && std::isfinite(inputs.targets.front().tcpa_s) &&
               inputs.targets.front().tcpa_s > 0.0) {
      // Release predicted AND a tcpa is available → use the tighter of the two.
      dwell_s = inputs.targets.front().tcpa_s;
    }
    if (!s_clear_available) {
      result.reject_reason = "active_s_clear_unavailable";
      return result;
    }
    dwell_s = std::max(dwell_s, kActiveMinDwellS);
    const double advance_m = own_u_hold * dwell_s;
    const double s_clear = std::min(route_len, projection.s_m + advance_m);
    if (s_clear <= projection.s_m + inputs.gnc_odd.min_segment_length_m) {
      result.reject_reason = "insufficient_route_for_active_hold";
      return result;
    }

    TailSegment segment{};
    for (double s = projection.s_m; s < s_clear; s += spacing_m) {
      segment.waypoints.push_back(inputs.route_frame.sample(s, l_hold, inputs.uN_mps));
      segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);
    }
    segment.waypoints.push_back(inputs.route_frame.sample(s_clear, l_hold, inputs.uN_mps));
    segment.source_labels.push_back(l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);

    if (!validate_tail_segment(inputs, segment, result.reject_reason, /*expect_rejoin=*/false)) {
      return result;
    }

    result.hold_then_rejoin = std::move(segment);
    return result;
  }

  // ── Release phase (spec §5.2): hold + rejoin (existing semantics).
  const double dwell_m = std::clamp(4.0 * inputs.gnc_odd.ship_length_m,
                                    inputs.gnc_odd.min_segment_length_m,
                                    5.0 * inputs.gnc_odd.ship_length_m);
  const double s_clear = projection.s_m;
  const double rejoin_start = s_clear + dwell_m;
  const double rejoin_len = feasible_rejoin_length_m(inputs, l_hold);
  const double end_s = std::min(route_len, rejoin_start + rejoin_len);
  if (end_s <= rejoin_start + inputs.gnc_odd.min_segment_length_m) {
    result.reject_reason = "insufficient_route_for_rejoin";
    return result;
  }

  TailSegment segment{};
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

  if (!validate_tail_segment(inputs, segment, result.reject_reason, /*expect_rejoin=*/true)) {
    return result;
  }

  result.hold_then_rejoin = std::move(segment);
  return result;
}

}  // namespace mass_l3::m5::tail_builder
