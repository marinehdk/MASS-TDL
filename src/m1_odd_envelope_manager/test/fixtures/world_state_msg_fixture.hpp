#pragma once

#include <cstdint>
#include <vector>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/zone_constraint.hpp"

namespace mass_l3::m1::test {

/// Minimum OwnShipState fixture.
inline l3_msgs::msg::OwnShipState make_own_ship_state(
    double sog_kn = 12.0,
    double cog_deg = 45.0,
    double heading_deg = 45.0) {
  l3_msgs::msg::OwnShipState oss{};
  oss.stamp = builtin_interfaces::msg::Time{};
  oss.sog_kn = sog_kn;
  oss.cog_deg = cog_deg;
  oss.heading_deg = heading_deg;
  oss.u_water = sog_kn * 0.514444;  // approximate m/s
  oss.v_water = 0.0;
  oss.r_dot_deg_s = 0.0;
  oss.current_speed_kn = 0.0;
  oss.current_direction_deg = 0.0;
  oss.nav_mode = "OPTIMAL";
  return oss;
}

/// Build a single TrackedTarget fixture.
inline l3_msgs::msg::TrackedTarget make_target(
    uint64_t id,
    double sog_kn,
    double cog_deg,
    double cpa_m,
    double tcpa_s) {
  l3_msgs::msg::TrackedTarget tgt{};
  tgt.stamp = builtin_interfaces::msg::Time{};
  tgt.target_id = id;
  tgt.sog_kn = sog_kn;
  tgt.cog_deg = cog_deg;
  tgt.cpa_m = cpa_m;
  tgt.tcpa_s = tcpa_s;
  tgt.classification = "vessel";
  tgt.classification_confidence = 0.95f;
  tgt.confidence = 0.9f;
  tgt.source_sensor = "radar";
  return tgt;
}

/// Build a WorldState fixture with optional targets.
inline l3_msgs::msg::WorldState make_world_state_msg(
    float confidence = 1.0f,
    const std::string& rationale = "test world state",
    double lat = 0.0,
    double lon = 0.0) {
  l3_msgs::msg::WorldState ws{};
  ws.stamp = builtin_interfaces::msg::Time{};
  ws.own_ship = make_own_ship_state();
  ws.confidence = confidence;
  ws.rationale = rationale;
  return ws;
}

}  // namespace mass_l3::m1::test
