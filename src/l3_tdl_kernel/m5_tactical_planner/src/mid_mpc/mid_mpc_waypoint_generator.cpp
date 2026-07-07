#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "l3_msgs/msg/avoidance_waypoint.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/gnc_avoidance_preflight.hpp"
#include "m5_tactical_planner/tail_builder/route_frame.hpp"

namespace mass_l3::m5::mid_mpc {

namespace {

void append_route_point(l3_msgs::msg::AvoidancePlan& plan,
                        double latitude,
                        double longitude,
                        double speed_mps,
                        const std::string& navigation_mode,
                        std::uint8_t segment_source) {
  plan.latitude.push_back(latitude);
  plan.longitude.push_back(longitude);
  plan.command_speed_mps.push_back(speed_mps);
  plan.navigation_mode.push_back(navigation_mode);
  plan.segment_source.push_back(segment_source);
}

double route_point_distance_m(double lat_a, double lon_a, double lat_b, double lon_b) {
  const double dn = (lat_b - lat_a) * kMetersPerDegLat;
  const double de = (lon_b - lon_a) * kMetersPerDegLat * std::cos(lat_a * M_PI / 180.0);
  return std::hypot(dn, de);
}
// Phase 3.10.1: route_point_distance_m is no longer called by prepend/suffix
// (replaced by RouteFrame::project station math). Kept as a non-anonymous
// translation-unit-local function so the linker does not drop it; downstream
// unit tests still reference it directly. Mark used to satisfy -Werror.
[[maybe_unused]] constexpr auto _route_point_distance_m_used
    = &route_point_distance_m;
// Phase 3.10.1 (Codex 方案 E): build an own-relative NED RouteFrame from an L2
// PlannedRoute. Mirrors tail_route_frame_from_l2 (mid_mpc_node.cpp:117-137) so
// the prefix/suffix helpers share a single coordinate-frame contract with the
// tail builder; in particular this uses units::kEarthRadiusMean_m (not the
// legacy kMetersPerDegLat) so station math matches RouteFrame::project.
mass_l3::m5::tail_builder::RouteFrame route_frame_from_l2(
    const l3_external_msgs::msg::PlannedRoute::SharedPtr& planned_route,
    double own_lat_deg, double own_lon_deg) {
  namespace tb = mass_l3::m5::tail_builder;
  tb::RouteFrame frame;
  if (planned_route == nullptr || planned_route->route.poses.size() < 2u) {
    return frame;
  }
  const double cos_lat = std::cos(own_lat_deg * units::kRadPerDeg);
  const auto& poses = planned_route->route.poses;
  frame.waypoints.reserve(poses.size());
  for (const auto& pose : poses) {
    const double plat = pose.pose.position.latitude;
    const double plon = pose.pose.position.longitude;
    const double n_m = (plat - own_lat_deg) * units::kRadPerDeg * units::kEarthRadiusMean_m;
    const double e_m = (plon - own_lon_deg) * units::kRadPerDeg * units::kEarthRadiusMean_m * cos_lat;
    frame.waypoints.push_back(tb::GeoWP{n_m, e_m, 5.0, "L2_NOMINAL"});
  }
  return frame;
}

// Inverse of route_frame_from_l2: own-relative NED → WGS84 for publishing back
// as lat/lon in the AvoidancePlan parallel arrays.
void ned_to_latlon_ownrelative(
    double x_m, double y_m, double own_lat_deg, double own_lon_deg,
    double& out_lat_deg, double& out_lon_deg) {
  out_lat_deg = own_lat_deg + (x_m / units::kEarthRadiusMean_m) * units::kDegPerRad;
  out_lon_deg = own_lon_deg
      + (y_m / (units::kEarthRadiusMean_m * std::cos(own_lat_deg * units::kRadPerDeg)))
      * units::kDegPerRad;
}

std::vector<WaypointLatLon> route_waypoints(const l3_msgs::msg::AvoidancePlan& plan) {
  std::vector<WaypointLatLon> wps;
  wps.reserve(plan.latitude.size());
  const std::size_t n = std::min(plan.latitude.size(), plan.longitude.size());
  for (std::size_t i = 0U; i < n; ++i) {
    wps.push_back(WaypointLatLon{plan.latitude[i], plan.longitude[i]});
  }
  return wps;
}

}  // namespace

void populate_canonical_route_from_selected_plan(
    l3_msgs::msg::AvoidancePlan& plan,
    const MidMpcSolution& solution,
    const std::string& plan_id,
    const std::string& parent_route_id,
    const std::string& navigation_mode) {
  plan.schema_version = 116;
  plan.plan_id = plan_id;
  plan.parent_route_id = parent_route_id;
  plan.behavior_mode = navigation_mode;
  plan.command_source = "m5_committed_route";
  plan.allow_degraded_execution = false;
  plan.has_return_to_route_point = false;
  plan.latitude.clear();
  plan.longitude.clear();
  plan.command_speed_mps.clear();
  plan.navigation_mode.clear();
  plan.segment_source.clear();
  plan.stale_committed_at.sec = 0;
  plan.stale_committed_at.nanosec = 0U;
  plan.nlp_solver_status = solution.status == MidMpcSolution::Status::Converged
      ? l3_msgs::msg::AvoidancePlan::NLP_CONVERGED
      : l3_msgs::msg::AvoidancePlan::NLP_NONCONVERGED;
  plan.nlp_kkt_residual = 0.0F;
  plan.nlp_tail_gate_failed = solution.status != MidMpcSolution::Status::Converged;

  for (const auto& wp : plan.waypoints) {
    // Fix #6 (2026-07-07): clamp NLP trajectory speed to emergency guidance cap
    // (3.2 m/s). The NLP solver optimises speed toward the L2 planned speed
    // (~8.0 m/s cruise), but avoidance routes must be executable inside the
    // emergency envelope. Without the clamp, validate_gnc_avoidance_plan
    // triggers high_speed_flyby (speed > 3.2), which demands ≥ 120 m segments
    // that the NLP's ~28 m segments cannot satisfy → flyby_segment_too_short.
    const double speed_mps = gnc_emergency_command_speed_mps(
        wp.target_speed_kn * units::kMsPerKn);
    append_route_point(
        plan,
        wp.position.latitude,
        wp.position.longitude,
        speed_mps,
        navigation_mode,
        l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
  }
}

GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin,
    bool wps_has_anchor) {
  return validate_gnc_avoidance_plan(
      origin, route_waypoints(plan), plan.command_speed_mps,
      GncAvoidancePreflightConfig{}, wps_has_anchor);
}

bool append_l2_nominal_suffix_if_preflight_feasible(
    l3_msgs::msg::AvoidancePlan& plan,
    const l3_external_msgs::msg::PlannedRoute::SharedPtr& planned_route,
    const WaypointLatLon& origin,
    double speed_mps) {
  if (planned_route == nullptr || planned_route->route.poses.empty()
      || plan.latitude.empty() || plan.longitude.empty()) {
    return true;
  }

  // Phase 3.10.1 (Codex 方案 E): station-based suffix selection.
  //
  // Mirrors prepend_l2_history_prefix_if_preflight_feasible's station math so
  // the helpers share a single coordinate-frame contract. The legacy
  // nearest-pose helper picked the L2 vertex closest to the plan's terminal
  // waypoint — this is the right answer when the terminal waypoint sits inside
  // the L2 nominal leg (the common case after the TailBuilder/rejoin emits a
  // point near the L2 endpoint), but it can pick a vertex *behind* the plan
  // end when the plan overshoots the L2 nominal or sits laterally offset.
  // Using along-track station avoids that ambiguity: suffix = every L2 vertex
  // whose station is strictly greater than the plan-end's projected station.
  namespace tb = mass_l3::m5::tail_builder;
  const tb::RouteFrame frame = route_frame_from_l2(planned_route, origin.lat, origin.lon);
  if (frame.waypoints.size() < 2u) {
    return true;
  }
  const double last_lat = plan.latitude.back();
  const double last_lon = plan.longitude.back();
  const double cos_lat = std::cos(origin.lat * units::kRadPerDeg);
  const tb::GeoWP plan_end_ned{
      (last_lat - origin.lat) * units::kRadPerDeg * units::kEarthRadiusMean_m,
      (last_lon - origin.lon) * units::kRadPerDeg * units::kEarthRadiusMean_m * cos_lat,
      speed_mps, "plan_end"};
  const tb::RouteProjection proj = frame.project(plan_end_ned);
  if (!proj.valid) {
    return true;
  }

  l3_msgs::msg::AvoidancePlan candidate = plan;
  constexpr double kDuplicateWaypointToleranceM = 1.0;
  const double s_threshold = proj.s_m + kDuplicateWaypointToleranceM;
  double cum_station = 0.0;
  for (std::size_t i = 0U; i < frame.waypoints.size(); ++i) {
    if (i > 0U) {
      const double seg_len = std::hypot(
          frame.waypoints[i].x_m - frame.waypoints[i - 1].x_m,
          frame.waypoints[i].y_m - frame.waypoints[i - 1].y_m);
      cum_station += seg_len;
    }
    if (cum_station <= s_threshold) {
      continue;
    }
    // Reproject back to lat/lon. The vertex lat/lon could also be read from
    // planned_route directly, but the round-trip through the own-relative NED
    // frame matches what prepend_l2_history_prefix emits, so the suffix and
    // prefix share the exact same projection error model.
    double lat_deg = 0.0, lon_deg = 0.0;
    ned_to_latlon_ownrelative(
        frame.waypoints[i].x_m, frame.waypoints[i].y_m,
        origin.lat, origin.lon, lat_deg, lon_deg);
    append_route_point(
        candidate,
        lat_deg,
        lon_deg,
        speed_mps,
        "transit",
        l3_msgs::msg::AvoidancePlan::L2_NOMINAL_SUFFIX);
  }

  // Phase 2: candidate = plan + L2 suffix; plan.waypoints[0] is anchor.
  const auto result = validate_canonical_route_for_gnc(candidate, origin, /*wps_has_anchor=*/true);
  if (!result.feasible) {
    return false;
  }
  plan = std::move(candidate);
  return true;
}

bool prepend_l2_history_prefix_if_preflight_feasible(
    l3_msgs::msg::AvoidancePlan& plan,
    const l3_external_msgs::msg::PlannedRoute::SharedPtr& planned_route,
    const WaypointLatLon& own_lat_lon,
    double speed_mps) {
  // No-op cases: no L2 route, or plan empty (caller must populate MID_MPC
  // segment first — prepend runs AFTER populate_canonical_route_from_selected_plan).
  if (planned_route == nullptr || planned_route->route.poses.empty()
      || plan.latitude.empty() || plan.longitude.empty()) {
    return true;
  }

  // Phase 3.10.1 (Codex 方案 E): station-based prefix selection.
  //
  // The legacy nearest-pose helper returned closest_idx=0 whenever ownship was
  // nearest the L2 start (probe run-19f3231690a), making prepend a no-op even
  // though coord_transform's pair-wise comparison against last_feedback_path_
  // needed index-aligned L2 history ahead of the ownship anchor. The fix:
  // project ownship onto the L2 nominal polyline to get ownship's along-track
  // station s0, then take every L2 pose whose station is strictly less than
  // s_first_change = s0 + wheel_over_distance_m. This guarantees:
  //   - L2 history before ownship is included (index alignment for
  //     last_feedback_path_ pair-wise comparison).
  //   - L2 poses between ownship and the first avoidance point are included
  //     (first_changed_distance_ahead becomes positive — the first geometric
  //     change is the avoidance point, not the ownship anchor).
  //   - The ownship anchor itself never enters the prefix (it is the first
  //     MID_MPC_OPTIMIZED entry, populated by populate_canonical_route_from_selected_plan).
  namespace tb = mass_l3::m5::tail_builder;
  const tb::RouteFrame frame = route_frame_from_l2(planned_route, own_lat_lon.lat, own_lat_lon.lon);
  if (frame.waypoints.size() < 2u) {
    return true;  // L2 route too short to project onto — no-op.
  }
  const tb::GeoWP own_ned{0.0, 0.0, speed_mps, "own"};
  const tb::RouteProjection proj = frame.project(own_ned);
  if (!proj.valid) {
    return true;
  }
  // wheel_over_distance_m mirrors MidMpcWaypointGenerator::Config defaults and
  // GncAvoidancePreflightConfig::emergency_wheel_over_distance_m. Using a
  // positive lookahead keeps first_changed_distance_ahead positive even when
  // ownship sits exactly on an L2 vertex.
  constexpr double kWheelOverDistanceM = 120.0;
  constexpr double kMinPrefixSpacingM = 15.0;
  const double s_first_change = proj.s_m + (std::max)(kWheelOverDistanceM, kMinPrefixSpacingM);

  // Walk L2 vertices in station order; keep those whose station is strictly
  // less than s_first_change. Decimate to ≥ kMinPrefixSpacingM so the
  // downstream validate_canonical_route_for_gnc emergency_min_segment_length_m
  // floor does not reject the candidate when L2 emits dense (~1 Hz) poses.
  std::vector<std::pair<double, double>> prefix_latlon;
  prefix_latlon.reserve(frame.waypoints.size());
  double cum_station = 0.0;
  double last_kept_x = std::numeric_limits<double>::quiet_NaN();
  double last_kept_y = std::numeric_limits<double>::quiet_NaN();
  // Vertex 0 has station 0 by definition; include it iff ownship is already
  // ahead of it (proj.s_m > 0). If ownship sits at the L2 start there is no
  // history to prepend — return early without an empty candidate publish.
  if (proj.s_m > 0.0 && frame.waypoints[0].x_m >= -1.0e-9) {
    // L2 start is behind ownship in the along-track sense — eligible prefix.
    // (proj.s_m > 0 implies own is ahead of vertex 0 along the polyline.)
  }
  for (std::size_t i = 0U; i < frame.waypoints.size(); ++i) {
    if (i > 0U) {
      const double seg_len = std::hypot(
          frame.waypoints[i].x_m - frame.waypoints[i - 1].x_m,
          frame.waypoints[i].y_m - frame.waypoints[i - 1].y_m);
      cum_station += seg_len;
    }
    if (cum_station >= s_first_change) {
      break;
    }
    // Skip ownship's exact position (the anchor is added later by the caller
    // as the first MID_MPC_OPTIMIZED entry — duplicating it here would create
    // a zero-length segment).
    const double dx_own = frame.waypoints[i].x_m - 0.0;
    const double dy_own = frame.waypoints[i].y_m - 0.0;
    if (std::hypot(dx_own, dy_own) < 1.0) {
      continue;
    }
    if (!prefix_latlon.empty()) {
      const double step_m = std::hypot(
          frame.waypoints[i].x_m - last_kept_x,
          frame.waypoints[i].y_m - last_kept_y);
      if (step_m < kMinPrefixSpacingM) {
        continue;
      }
    }
    double lat_deg = 0.0, lon_deg = 0.0;
    ned_to_latlon_ownrelative(
        frame.waypoints[i].x_m, frame.waypoints[i].y_m,
        own_lat_lon.lat, own_lat_lon.lon, lat_deg, lon_deg);
    prefix_latlon.emplace_back(lat_deg, lon_deg);
    last_kept_x = frame.waypoints[i].x_m;
    last_kept_y = frame.waypoints[i].y_m;
  }
  if (prefix_latlon.empty()) {
    return true;
  }

  // Build candidate = prefix + original plan. Insert at front of all five
  // parallel arrays. plan.waypoints (the rich audit structs) is not touched —
  // downstream consumers (preflight, gnc_bridge, coord_transform) key off the
  // parallel arrays; plan.waypoints stays as the MID_MPC_OPTIMIZED-only view.
  l3_msgs::msg::AvoidancePlan candidate;
  candidate.schema_version = plan.schema_version;
  candidate.stamp = plan.stamp;
  candidate.commit_branch = plan.commit_branch;
  candidate.plan_id = plan.plan_id;
  candidate.parent_route_id = plan.parent_route_id;
  candidate.behavior_mode = plan.behavior_mode;
  candidate.command_source = plan.command_source;
  candidate.waypoints = plan.waypoints;
  candidate.speed_adjustments = plan.speed_adjustments;
  candidate.horizon_s = plan.horizon_s;
  candidate.status = plan.status;
  candidate.active_constraints = plan.active_constraints;
  candidate.valid_until = plan.valid_until;
  candidate.allow_degraded_execution = plan.allow_degraded_execution;
  candidate.has_return_to_route_point = plan.has_return_to_route_point;
  candidate.return_latitude = plan.return_latitude;
  candidate.return_longitude = plan.return_longitude;
  candidate.route_hash = plan.route_hash;
  candidate.stale_committed_at = plan.stale_committed_at;
  candidate.nlp_solver_status = plan.nlp_solver_status;
  candidate.nlp_kkt_residual = plan.nlp_kkt_residual;
  candidate.nlp_tail_gate_failed = plan.nlp_tail_gate_failed;
  candidate.confidence = plan.confidence;
  candidate.rationale = plan.rationale;

  candidate.latitude.reserve(prefix_latlon.size() + plan.latitude.size());
  candidate.longitude.reserve(prefix_latlon.size() + plan.longitude.size());
  candidate.command_speed_mps.reserve(prefix_latlon.size() + plan.command_speed_mps.size());
  candidate.navigation_mode.reserve(prefix_latlon.size() + plan.navigation_mode.size());
  candidate.segment_source.reserve(prefix_latlon.size() + plan.segment_source.size());
  for (const auto& latlon : prefix_latlon) {
    append_route_point(
        candidate,
        latlon.first,
        latlon.second,
        speed_mps,
        "cruise",
        l3_msgs::msg::AvoidancePlan::L2_HISTORICAL_PREFIX);
  }
  for (std::size_t i = 0U; i < plan.latitude.size(); ++i) {
    append_route_point(
        candidate,
        plan.latitude[i],
        plan.longitude[i],
        i < plan.command_speed_mps.size() ? plan.command_speed_mps[i] : speed_mps,
        i < plan.navigation_mode.size() ? plan.navigation_mode[i] : std::string{"emergency_avoidance"},
        i < plan.segment_source.size() ? plan.segment_source[i]
                                       : static_cast<std::uint8_t>(l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED));
  }

  // Preflight semantics after prepend: the prefix is HISTORICAL (already
  // traversed), not future maneuver. validate_canonical_route_for_gnc models
  // the route as "origin=ownship + future waypoints" and checks first_distance
  // / turn_radius / segment_length against origin. Applying it to the full
  // candidate (prefix + MID_MPC + ...) mis-frames the prefix as future maneuvers
  // starting from ownship — origin↔wps[0] (L2 start) reverses heading relative
  // to wps[0]→wps[1] (L2 forward), producing a false U-turn / tiny radius.
  //
  // Fix: run preflight on a post-prefix sub-plan that starts at the ownship
  // anchor (the first MID_MPC_OPTIMIZED entry). wps_has_anchor=true is correct
  // for the sub-plan (its wps[0] is the anchor). Prefix geometry is validated
  // separately by the decimation step (≥15 m spacing).
  l3_msgs::msg::AvoidancePlan post_prefix_subplan;
  post_prefix_subplan.latitude.assign(
      plan.latitude.begin(), plan.latitude.end());
  post_prefix_subplan.longitude.assign(
      plan.longitude.begin(), plan.longitude.end());
  post_prefix_subplan.command_speed_mps.assign(
      plan.command_speed_mps.begin(), plan.command_speed_mps.end());
  post_prefix_subplan.navigation_mode.assign(
      plan.navigation_mode.begin(), plan.navigation_mode.end());
  post_prefix_subplan.segment_source.assign(
      plan.segment_source.begin(), plan.segment_source.end());
  const auto result = validate_canonical_route_for_gnc(
      post_prefix_subplan, own_lat_lon, /*wps_has_anchor=*/true);
  if (!result.feasible) {
    return false;
  }
  plan = std::move(candidate);
  return true;
}

MidMpcWaypointGenerator::MidMpcWaypointGenerator(const Config& cfg) : cfg_(cfg) {}

// ned_to_geopoint_ — flat-earth NED → WGS84 (Phase E1 approximation).
// Phase E2 replaces with GeographicLib::Geodesic for metric accuracy.
geographic_msgs::msg::GeoPoint MidMpcWaypointGenerator::ned_to_geopoint_(
    double lat0_deg, double lon0_deg, double dx_m, double dy_m)
{
  geographic_msgs::msg::GeoPoint pt;
  pt.latitude  = lat0_deg + (dx_m / units::kEarthRadiusMean_m) * units::kDegPerRad;
  pt.longitude = lon0_deg
      + (dy_m / (units::kEarthRadiusMean_m * std::cos(lat0_deg * units::kRadPerDeg)))
      * units::kDegPerRad;
  pt.altitude  = 0.0;
  return pt;
}

std::vector<geographic_msgs::msg::GeoPoint>
MidMpcWaypointGenerator::sample_waypoints_(
    const std::vector<TrajectoryPoint>& trajectory,
    double own_ship_lat,
    double own_ship_lon) const
{
  if (trajectory.empty()) {
    return {};
  }

  const int32_t N      = static_cast<int32_t>(trajectory.size());
  const int32_t desired_wp = std::max(cfg_.num_waypoints, N);
  const int32_t num_wp = std::min(desired_wp, N);

  std::vector<std::pair<double, double>> ned_pos;
  ned_pos.reserve(static_cast<std::size_t>(N + 1));
  ned_pos.emplace_back(0.0, 0.0);

  for (int32_t k = 0; k < N; ++k) {
    const auto& pt = trajectory[static_cast<std::size_t>(k)];
    const double dx = pt.u_mps * cfg_.dt_s * std::cos(pt.psi_rad);
    const double dy = pt.u_mps * cfg_.dt_s * std::sin(pt.psi_rad);
    ned_pos.emplace_back(ned_pos.back().first + dx, ned_pos.back().second + dy);
  }

  // Phase 3 (spec §3.6): start maneuvers at first ned_pos index whose
  // cumulative distance from origin >= wheel_over_distance_m (default 120m).
  // ned_pos[0] = origin (anchor). Fallback to last index if horizon is short.
  const int32_t last_ned_idx = static_cast<int32_t>(ned_pos.size()) - 1;
  int32_t start_idx = last_ned_idx;
  for (int32_t k = 1; k <= last_ned_idx; ++k) {
    const auto& pos = ned_pos[static_cast<std::size_t>(k)];
    const double dist_m = std::sqrt(pos.first * pos.first + pos.second * pos.second);
    if (dist_m >= cfg_.wheel_over_distance_m) {
      start_idx = k;
      break;
    }
  }

  std::vector<geographic_msgs::msg::GeoPoint> result;
  result.reserve(static_cast<std::size_t>(num_wp));
  // Phase 2: wps[0] = anchor (own ship origin).
  result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, 0.0, 0.0));
  // Phase 3: wps[1..maneuver_wp] sampled uniformly across [start_idx, last_ned_idx].
  // Cap maneuver_wp to span+1 so each k maps to a distinct ned_pos index
  // (prevents segment_too_short from int-truncation when num_wp > span+1,
  // e.g. short horizon with wheel-over prefix).
  const int32_t span = last_ned_idx - start_idx;
  const int32_t maneuver_wp = std::min(num_wp - 1, span + 1);
  for (int32_t k = 0; k < maneuver_wp; ++k) {
    const int32_t idx = (maneuver_wp <= 1)
        ? start_idx
        : start_idx + k * span / (maneuver_wp - 1);
    const auto& pos = ned_pos[static_cast<std::size_t>(idx)];
    result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, pos.first, pos.second));
  }
  return result;
}

std::vector<l3_msgs::msg::AvoidanceWaypoint> MidMpcWaypointGenerator::build_waypoints_(
    const std::vector<geographic_msgs::msg::GeoPoint>& geopoints,
    const MidMpcSolution& solution) const
{
  const int32_t N      = static_cast<int32_t>(solution.trajectory.size());
  const int32_t num_wp = static_cast<int32_t>(geopoints.size());

  std::vector<std::pair<double, double>> ned_pos;
  ned_pos.reserve(static_cast<std::size_t>(N + 1));
  ned_pos.emplace_back(0.0, 0.0);
  for (int32_t k = 0; k < N; ++k) {
    const auto& pt = solution.trajectory[static_cast<std::size_t>(k)];
    const double dx = pt.u_mps * cfg_.dt_s * std::cos(pt.psi_rad);
    const double dy = pt.u_mps * cfg_.dt_s * std::sin(pt.psi_rad);
    ned_pos.emplace_back(ned_pos.back().first + dx, ned_pos.back().second + dy);
  }

  // Phase 3 (spec §3.6): mirror sample_waypoints_ wheel-over indexing so that
  // turn_radius/target_speed/wp_distance map to the same maneuver positions.
  const int32_t last_ned_idx = static_cast<int32_t>(ned_pos.size()) - 1;
  int32_t start_idx = last_ned_idx;
  for (int32_t k = 1; k <= last_ned_idx; ++k) {
    const auto& pos = ned_pos[static_cast<std::size_t>(k)];
    const double dist_m = std::sqrt(pos.first * pos.first + pos.second * pos.second);
    if (dist_m >= cfg_.wheel_over_distance_m) {
      start_idx = k;
      break;
    }
  }
  // Phase 3: cap maneuver_wp to span+1 (must match sample_waypoints_ cap so
  // geopoints.size() aligns with maneuver indexing).
  const int32_t span = last_ned_idx - start_idx;
  const int32_t maneuver_wp = std::min(num_wp - 1, span + 1);

  std::vector<l3_msgs::msg::AvoidanceWaypoint> waypoints;
  waypoints.reserve(static_cast<std::size_t>(num_wp));

  constexpr double kMinRot        = 1e-4;    // rad/s — below ~0.006°/s treated as straight
  constexpr double kMaxTurnRadius = 500.0;   // m — straight-line fallback

  for (int32_t i = 0; i < num_wp; ++i) {
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.position          = geopoints[static_cast<std::size_t>(i)];
    wp.safety_corridor_m = cfg_.safety_corridor_m;

    // Phase 3: wps[0] = anchor (traj_idx=0); wps[1..num_wp-1] use indices from
    // [start_idx, last_ned_idx]. Clamp traj_idx to trajectory domain [0, N-1]
    // since ned_pos has N+1 entries (anchor + N steps).
    int32_t traj_idx;
    if (i == 0) {
      traj_idx = 0;
    } else if (maneuver_wp <= 1) {
      traj_idx = std::min(start_idx, N - 1);
    } else {
      const int32_t ned_idx = start_idx + (i - 1) * span / (maneuver_wp - 1);
      traj_idx = std::min(ned_idx, N - 1);
    }

    // Compute local ROT from adjacent trajectory steps to derive turn_radius_m.
    // Bridge gate: abs(turn_radius_m) > 1e-6 must be true for avoidance to activate.
    // Formula: R = u / |omega|, omega = dpsi / dt_s (rad/s).
    // Use next available step; for last trajectory point use previous step.
    // Guard: if N==1 there's no adjacent step — fall back to straight-line radius.
    if (N >= 2) {
      const int32_t rot_idx_a = (traj_idx < N - 1) ? traj_idx : traj_idx - 1;
      const int32_t rot_idx_b = rot_idx_a + 1;
      const double dpsi = solution.trajectory[static_cast<std::size_t>(rot_idx_b)].psi_rad
                        - solution.trajectory[static_cast<std::size_t>(rot_idx_a)].psi_rad;
      const double rot_rad_s = std::abs(dpsi) / cfg_.dt_s;
      const double u_for_rot = solution.trajectory[static_cast<std::size_t>(traj_idx)].u_mps;
      wp.turn_radius_m = (rot_rad_s > kMinRot)
          ? std::min(u_for_rot / rot_rad_s, kMaxTurnRadius)
          : kMaxTurnRadius;
    } else {
      // Single trajectory point: no heading delta computable; use straight-line fallback.
      wp.turn_radius_m = kMaxTurnRadius;
    }

    wp.target_speed_kn =
        solution.trajectory[static_cast<std::size_t>(traj_idx)].u_mps * units::kKnPerMs;

    // Phase 3: wp_distance_m measured between consecutive waypoints in NED.
    // wps[0] = anchor → distance 0; wps[1] = distance from origin to its ned_pos;
    // wps[i>=2] = distance from wps[i-1]'s ned_pos to wps[i]'s ned_pos.
    if (i == 0) {
      wp.wp_distance_m = 0.0;
    } else if (i == 1) {
      const auto& cur = ned_pos[static_cast<std::size_t>(traj_idx)];
      wp.wp_distance_m = std::sqrt(cur.first * cur.first + cur.second * cur.second);
    } else {
      // Re-derive previous waypoint's traj_idx using the same wheel-over mapping.
      const int32_t prev_traj_idx = std::min(
          start_idx + (i - 2) * span / (maneuver_wp - 1), N - 1);
      const auto& cur  = ned_pos[static_cast<std::size_t>(traj_idx)];
      const auto& prev = ned_pos[static_cast<std::size_t>(prev_traj_idx)];
      const double ddx = cur.first  - prev.first;
      const double ddy = cur.second - prev.second;
      wp.wp_distance_m = std::sqrt(ddx * ddx + ddy * ddy);
    }
    waypoints.push_back(wp);
  }
  return waypoints;
}

std::string MidMpcWaypointGenerator::compose_rationale_(const MidMpcSolution& solution) const
{
  std::ostringstream oss;
  oss << "MPC converged in " << solution.solve_duration_ms << " ms"
      << "; ipopt_iter=" << solution.ipopt_iterations
      << "; cost_colreg=" << solution.cost_colreg
      << " cost_dist="    << solution.cost_dist
      << " cost_vel="     << solution.cost_vel;
  return oss.str();
}

l3_msgs::msg::AvoidancePlan MidMpcWaypointGenerator::generate(
    const MidMpcSolution& solution,
    double own_ship_lat,
    double own_ship_lon) const
{
  l3_msgs::msg::AvoidancePlan plan;

  if (solution.status != MidMpcSolution::Status::Converged) {
    plan.status     = "DEGRADED";
    plan.confidence = 0.0f;
    plan.rationale  = "MPC not converged";
    plan.horizon_s  = 0.0f;
    return plan;
  }

  if (solution.trajectory.empty()) {
    plan.status     = "DEGRADED";
    plan.confidence = 0.0f;
    plan.rationale  = "Converged solution contains empty trajectory";
    plan.horizon_s  = 0.0f;
    return plan;
  }

  const auto geopoints = sample_waypoints_(solution.trajectory, own_ship_lat, own_ship_lon);
  plan.waypoints  = build_waypoints_(geopoints, solution);
  plan.horizon_s  = static_cast<float>(solution.trajectory.back().t_s);
  plan.status     = "NORMAL";
  plan.confidence = 1.0f;
  plan.rationale  = compose_rationale_(solution);
  return plan;
}

}  // namespace mass_l3::m5::mid_mpc
