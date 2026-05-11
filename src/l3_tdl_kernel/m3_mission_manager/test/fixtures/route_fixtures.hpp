#pragma once

#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"

inline l3_external_msgs::msg::PlannedRoute make_valid_route(uint64_t route_id = 1) {
  l3_external_msgs::msg::PlannedRoute msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.route_id = route_id;
  msg.total_distance_nm = 50.0;
  msg.estimated_duration_s = 3600.0;
  msg.confidence = 0.95f;
  msg.rationale = "test route";
  return msg;
}

inline l3_external_msgs::msg::SpeedProfile make_valid_speed_profile(uint64_t profile_id = 1) {
  l3_external_msgs::msg::SpeedProfile msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.profile_id = profile_id;
  msg.confidence = 0.95f;
  msg.rationale = "test speed profile";
  return msg;
}
