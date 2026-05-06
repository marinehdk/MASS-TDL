#pragma once

#include "l3_external_msgs/msg/voyage_task.hpp"

inline l3_external_msgs::msg::VoyageTask make_valid_voyage_task(uint64_t task_id = 1) {
  l3_external_msgs::msg::VoyageTask msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.task_id = task_id;
  msg.task_type = "transit";
  msg.speed_cmd_kn = 18.0;
  msg.eta_requirement_s = 3600.0f;  // 1 hour
  msg.origin.latitude = 38.0;
  msg.origin.longitude = -122.0;
  msg.origin.altitude = 0.0;
  msg.destination.latitude = 37.5;
  msg.destination.longitude = -121.5;
  msg.destination.altitude = 0.0;
  msg.waypoint_latitudes = {37.9, 37.7};
  msg.waypoint_longitudes = {-121.8, -121.6};
  msg.confidence = 1.0f;
  msg.rationale = "test";
  return msg;
}
