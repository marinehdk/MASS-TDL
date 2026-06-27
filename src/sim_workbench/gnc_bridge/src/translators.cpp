// gnc_bridge field-mapping implementations (Track A A4). Pure functions; no
// behavior logic. Each map mirrors the corresponding source/target msg contract
// verified against third_party/gnc_ws ship_interfaces definitions.
#include "gnc_bridge/translators.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gnc_bridge {
namespace {
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr int64_t kNsecPerSec = 1000000000LL;

double deg_to_rad(double deg) {
  return deg * kDegToRad;
}

int64_t time_to_ns(const builtin_interfaces::msg::Time& t) {
  return static_cast<int64_t>(t.sec) * kNsecPerSec + static_cast<int64_t>(t.nanosec);
}

builtin_interfaces::msg::Time ns_to_time(int64_t ns) {
  builtin_interfaces::msg::Time t;
  t.sec = static_cast<int32_t>(ns / kNsecPerSec);
  t.nanosec = static_cast<uint32_t>(ns % kNsecPerSec);
  return t;
}

bool time_is_zero(const builtin_interfaces::msg::Time& t) {
  return t.sec == 0 && t.nanosec == 0;
}
}  // namespace

ship_interfaces::msg::AvoidancePlan to_gnc_avoidance_plan(
    const l3_external_msgs::msg::AvoidanceWaypoints& src,
    const builtin_interfaces::msg::Time& stamp) {
  ship_interfaces::msg::AvoidancePlan dst;
  dst.header.stamp = stamp;
  dst.plan_id          = src.plan_id;
  dst.parent_route_id  = src.parent_route_id;
  dst.behavior_mode    = src.behavior_mode;
  dst.command_source   = src.command_source;
  dst.latitude         = src.latitude;
  dst.longitude        = src.longitude;
  dst.command_speed_mps = src.command_speed_mps;
  // command_heading_deg left empty: GNC follows waypoint geometry, not an
  // explicit heading command (docking doc §9.2; active_route_manager ignores it
  // unless populated, and the feasibility gate does not require it).
  dst.navigation_mode  = src.navigation_mode;
  dst.valid_until      = src.valid_until;
  dst.require_exact_heading  = false;
  dst.require_exact_speed    = false;
  dst.allow_degraded_execution = src.allow_degraded_execution;
  dst.has_return_to_route_point = src.has_return_to_route_point;
  dst.return_latitude  = src.return_latitude;
  dst.return_longitude = src.return_longitude;
  return dst;
}

void rebase_avoidance_plan_timebase(
    ship_interfaces::msg::AvoidancePlan& plan,
    const builtin_interfaces::msg::Time& target_stamp) {
  const auto source_stamp = plan.header.stamp;
  plan.header.stamp = target_stamp;
  if (time_is_zero(plan.valid_until)) {
    return;
  }

  const int64_t ttl_ns = time_to_ns(plan.valid_until) - time_to_ns(source_stamp);
  if (ttl_ns <= 0) {
    plan.valid_until = target_stamp;
    return;
  }
  plan.valid_until = ns_to_time(time_to_ns(target_stamp) + ttl_ns);
}

ship_interfaces::msg::RoutePlan to_gnc_route_plan(
    const l3_external_msgs::msg::PlannedRoute& src,
    const builtin_interfaces::msg::Time& stamp) {
  ship_interfaces::msg::RoutePlan dst;
  dst.header.stamp = stamp;
  // PlannedRoute carries the full path as geographic_msgs/GeoPath; flatten it
  // into the GNC RoutePlan lat/lon arrays. GeoPath.poses[].pose.position holds
  // the geographic points.
  const auto& poses = src.route.poses;
  dst.latitude.reserve(poses.size());
  dst.longitude.reserve(poses.size());
  for (const auto& gp : poses) {
    dst.latitude.push_back(gp.pose.position.latitude);
    dst.longitude.push_back(gp.pose.position.longitude);
  }
  // speed_profile_kn is per-segment; GNC RoutePlan wants per-waypoint speed in
  // m/s. Map segment i to waypoint i when lengths line up; otherwise leave empty
  // (GNC derives internally). Convert kn -> m/s.
  if (!src.speed_profile_kn.empty() && src.speed_profile_kn.size() == poses.size()) {
    dst.speed_limit_mps.reserve(src.speed_profile_kn.size());
    for (const double kn : src.speed_profile_kn) {
      dst.speed_limit_mps.push_back(kn * 0.514444);
    }
  }
  dst.route_id   = std::to_string(src.route_id);
  dst.route_type = "transit";
  return dst;
}

sil_msgs::msg::OwnShipState to_sil_own_ship_state(
    const ship_interfaces::msg::GeoPosition& src,
    const builtin_interfaces::msg::Time& stamp) {
  sil_msgs::msg::OwnShipState dst;
  dst.stamp   = stamp;
  dst.lat     = src.latitude;
  dst.lon     = src.longitude;
  dst.heading = deg_to_rad(src.heading_deg);
  dst.sog     = src.speed_mps;             // GNC speed_mps is SOG magnitude
  dst.cog     = deg_to_rad(src.course_deg);
  dst.rot     = deg_to_rad(src.yaw_rate_deg_s);
  dst.u       = src.surge_mps;
  dst.v       = src.sway_mps;
  dst.r       = src.yaw_rate_rads;
  // rudder_angle / throttle are not in GeoPosition; leave at default 0.
  return dst;
}

l3_external_msgs::msg::GncExecutionStatus to_l3_gnc_execution_status(
    const ship_interfaces::msg::RouteExecutionStatus& src,
    const builtin_interfaces::msg::Time& stamp) {
  l3_external_msgs::msg::GncExecutionStatus dst;
  dst.stamp           = stamp;
  dst.schema_version  = 1;
  dst.plan_id         = src.plan_id;
  dst.active_route_id = src.active_route_id;
  dst.command_source  = src.command_source;
  dst.accepted        = src.accepted;
  dst.executing       = src.executing;
  dst.degraded        = src.degraded;
  dst.rejected        = src.rejected;
  dst.execution_state  = src.execution_state;
  dst.reason           = src.reason;
  dst.suggested_action = src.suggested_action;
  dst.requested_speed_mps = src.requested_speed_mps;
  dst.applied_speed_mps   = src.applied_speed_mps;
  dst.suggested_max_speed_mps = src.suggested_max_speed_mps;
  dst.current_latitude  = src.current_latitude;
  dst.current_longitude = src.current_longitude;
  dst.current_heading_deg = src.current_heading_deg;
  dst.current_speed_mps   = src.current_speed_mps;
  dst.cross_track_error_m = src.cross_track_error_m;
  dst.confidence = 1.0F;  // bridge-translated direct copy; no fusion uncertainty
  dst.rationale  = "bridge-translated from ship_interfaces/RouteExecutionStatus";
  return dst;
}

}  // namespace gnc_bridge
