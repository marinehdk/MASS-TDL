#include "m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp"

#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "l3_msgs/msg/avoidance_waypoint.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::mid_mpc {

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
  const int32_t num_wp = (cfg_.num_waypoints < N) ? cfg_.num_waypoints : N;

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
    wp.turn_radius_m     = 0.0;  // Phase E1 placeholder; Phase E2 derives from ROT

    const int32_t traj_idx = (num_wp == 1) ? 0 : i * (N - 1) / (num_wp - 1);
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
