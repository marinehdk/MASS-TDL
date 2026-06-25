#pragma once
// gnc_bridge field translators. Pure functions, no ROS node state. TDD-covered.
// Spec D3/D6: the ONLY place ship_interfaces types appear in L3-land. L3 core
// modules use l3_external_msgs/sil_msgs; this header maps to/from ship_interfaces.
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
#include "l3_external_msgs/msg/gnc_execution_status.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "ship_interfaces/msg/avoidance_plan.hpp"
#include "ship_interfaces/msg/geo_position.hpp"
#include "ship_interfaces/msg/route_execution_status.hpp"
#include "ship_interfaces/msg/route_plan.hpp"
#include "sil_msgs/msg/own_ship_state.hpp"

#include <builtin_interfaces/msg/time.hpp>

namespace gnc_bridge {

// L3 AvoidanceWaypoints -> GNC ship_interfaces/AvoidancePlan.
// GNC follows waypoint geometry (docking doc §9.2): command_heading_deg is left
// empty so GNC derives heading from the corridor.
ship_interfaces::msg::AvoidancePlan to_gnc_avoidance_plan(
    const l3_external_msgs::msg::AvoidanceWaypoints& src,
    const builtin_interfaces::msg::Time& stamp);

// L3 PlannedRoute -> GNC ship_interfaces/RoutePlan. Only the lat/lon + per-wp
// speed/note carry over; RoutePlan is the simpler GNC nominal-route contract.
ship_interfaces::msg::RoutePlan to_gnc_route_plan(
    const l3_external_msgs::msg::PlannedRoute& src,
    const builtin_interfaces::msg::Time& stamp);

// GNC GeoPosition -> sil_msgs/OwnShipState (trace/scoring compatibility). The
// GNC geo msg is far richer (roll, NED, nav_state); we surface the fields the
// L3 trace/scoring path reads (lat/lon/heading/speed/cog + XTE).
sil_msgs::msg::OwnShipState to_sil_own_ship_state(
    const ship_interfaces::msg::GeoPosition& src,
    const builtin_interfaces::msg::Time& stamp);

// GNC RouteExecutionStatus -> L3 GncExecutionStatus. L3 consumes accept/reject/
// degrade + reason/suggested_action + current ship state to react.
l3_external_msgs::msg::GncExecutionStatus to_l3_gnc_execution_status(
    const ship_interfaces::msg::RouteExecutionStatus& src,
    const builtin_interfaces::msg::Time& stamp);

}  // namespace gnc_bridge
