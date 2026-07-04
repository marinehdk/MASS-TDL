#ifndef MASS_L3_M5_MID_MPC_WAYPOINT_GENERATOR_HPP_
#define MASS_L3_M5_MID_MPC_WAYPOINT_GENERATOR_HPP_

// M5 Tactical Planner — Mid-MPC Waypoint Generator (Task 2.3)
// Converts a MidMpcSolution (N-step NED trajectory) to an AvoidancePlan
// with 4 Kongsberg K-Pos compatible GeoPoints per RFC-001 方案 B.
//
// NED → WGS84: Phase E1 uses flat-earth approximation (accurate < 0.01% for
// MPC horizons < 1 km). Phase E2 upgrades to GeographicLib::Geodesic.
//
// PATH-D (MISRA C++:2023): ≤60 lines per function, CC ≤10, no float.

#include <cstdint>
#include <string>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/avoidance_waypoint.hpp"

#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/gnc_avoidance_preflight.hpp"

namespace mass_l3::m5::mid_mpc {

inline constexpr double kAvoidancePlanHeartbeat_s = 60.0;
inline constexpr double kAvoidancePlanTtl_s = 70.0;

void populate_canonical_route_from_selected_plan(
    l3_msgs::msg::AvoidancePlan& plan,
    const MidMpcSolution& solution,
    const std::string& plan_id,
    const std::string& parent_route_id,
    const std::string& navigation_mode);

[[nodiscard]] GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin,
    bool wps_has_anchor = false);

[[nodiscard]] bool append_l2_nominal_suffix_if_preflight_feasible(
    l3_msgs::msg::AvoidancePlan& plan,
    const l3_external_msgs::msg::PlannedRoute::SharedPtr& planned_route,
    const WaypointLatLon& origin,
    double speed_mps);

class MidMpcWaypointGenerator {
 public:
  struct Config {
    // Dense route points: default follows Mid-MPC NLP step resolution (N=18, dt=5s).
    int32_t num_waypoints{18};
    // [TBD-HAZID] Safety corridor [m] — nominal COLREGs separation margin.
    // Calibrate from FCB CPA safe distance (HAZID RUN-001 WP-03).
    double safety_corridor_m{500.0};
    // [TBD-HAZID] NLP step duration [s] — must match MidMpcNlpFormulation::Config::dt_s.
    double dt_s{5.0};
  };

  explicit MidMpcWaypointGenerator(const Config& cfg);

  // Convert a Converged MidMpcSolution to a dense AvoidancePlan.
  // Non-Converged status returns an empty plan with status="DEGRADED".
  // @param solution  Solved MPC solution (unpack_solution() output).
  // @param own_ship_lat  Initial latitude [WGS84 deg].
  // @param own_ship_lon  Initial longitude [WGS84 deg].
  [[nodiscard]] l3_msgs::msg::AvoidancePlan generate(
      const MidMpcSolution& solution,
      double own_ship_lat,
      double own_ship_lon) const;

  [[nodiscard]] const Config& config() const noexcept { return cfg_; }

 private:
  Config cfg_;

  // Sample at NLP step resolution or denser, capped by trajectory length.
  // Uses flat-earth NED → WGS84 (Phase E1 approximation).
  [[nodiscard]] std::vector<geographic_msgs::msg::GeoPoint> sample_waypoints_(
      const std::vector<TrajectoryPoint>& trajectory,
      double own_ship_lat,
      double own_ship_lon) const;

  // Build AvoidanceWaypoint vector from sampled geopoints.
  // Extracted from generate() to keep each function ≤60 lines (PATH-D).
  [[nodiscard]] std::vector<l3_msgs::msg::AvoidanceWaypoint> build_waypoints_(
      const std::vector<geographic_msgs::msg::GeoPoint>& geopoints,
      const MidMpcSolution& solution) const;

  // Build SAT-2 rationale string.
  [[nodiscard]] std::string compose_rationale_(
      const MidMpcSolution& solution) const;

  // Flat-earth NED (dx=north, dy=east, metres) → WGS84 GeoPoint.
  // Phase E1 approximation; replace with GeographicLib::Geodesic in Phase E2.
  [[nodiscard]] static geographic_msgs::msg::GeoPoint ned_to_geopoint_(
      double lat0_deg, double lon0_deg, double dx_m, double dy_m);
};

}  // namespace mass_l3::m5::mid_mpc

#endif  // MASS_L3_M5_MID_MPC_WAYPOINT_GENERATOR_HPP_
