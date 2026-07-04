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
  plan.schema_version = 114;
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
    append_route_point(
        plan,
        wp.position.latitude,
        wp.position.longitude,
        wp.target_speed_kn * units::kMsPerKn,
        navigation_mode,
        l3_msgs::msg::AvoidancePlan::MID_MPC_OPTIMIZED);
  }
}

GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin,
    bool wps_has_anchor = false) {
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

  l3_msgs::msg::AvoidancePlan candidate = plan;
  const double last_lat = candidate.latitude.back();
  const double last_lon = candidate.longitude.back();
  std::size_t closest_idx = 0U;
  double closest_m = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0U; i < planned_route->route.poses.size(); ++i) {
    const auto& pos = planned_route->route.poses[i].pose.position;
    const double distance_m = route_point_distance_m(last_lat, last_lon, pos.latitude, pos.longitude);
    if (distance_m < closest_m) {
      closest_m = distance_m;
      closest_idx = i;
    }
  }

  constexpr double kDuplicateWaypointToleranceM = 1.0;
  const std::size_t first_suffix_idx = closest_m <= kDuplicateWaypointToleranceM
      ? closest_idx + 1U
      : closest_idx;
  for (std::size_t i = first_suffix_idx; i < planned_route->route.poses.size(); ++i) {
    const auto& pos = planned_route->route.poses[i].pose.position;
    append_route_point(
        candidate,
        pos.latitude,
        pos.longitude,
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

  std::vector<geographic_msgs::msg::GeoPoint> result;
  result.reserve(static_cast<std::size_t>(num_wp));

  for (int32_t k = 0; k < num_wp; ++k) {
    const int32_t idx = (num_wp == 1) ? 0 : k * (N - 1) / (num_wp - 1);
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

  std::vector<l3_msgs::msg::AvoidanceWaypoint> waypoints;
  waypoints.reserve(static_cast<std::size_t>(num_wp));

  for (int32_t i = 0; i < num_wp; ++i) {
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.position          = geopoints[static_cast<std::size_t>(i)];
    wp.safety_corridor_m = cfg_.safety_corridor_m;

    const int32_t traj_idx = (num_wp == 1) ? 0 : i * (N - 1) / (num_wp - 1);

    // Compute local ROT from adjacent trajectory steps to derive turn_radius_m.
    // Bridge gate: abs(turn_radius_m) > 1e-6 must be true for avoidance to activate.
    // Formula: R = u / |omega|, omega = dpsi / dt_s (rad/s).
    // Use next available step; for last trajectory point use previous step.
    // Guard: if N==1 there's no adjacent step — fall back to straight-line radius.
    constexpr double kMinRot        = 1e-4;    // rad/s — below ~0.006°/s treated as straight
    constexpr double kMaxTurnRadius = 500.0;   // m — straight-line fallback
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

    if (i == 0) {
      wp.wp_distance_m = 0.0;
    } else {
      const int32_t prev_idx = (num_wp == 1) ? 0 : (i - 1) * (N - 1) / (num_wp - 1);
      const auto& cur  = ned_pos[static_cast<std::size_t>(traj_idx)];
      const auto& prev = ned_pos[static_cast<std::size_t>(prev_idx)];
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
