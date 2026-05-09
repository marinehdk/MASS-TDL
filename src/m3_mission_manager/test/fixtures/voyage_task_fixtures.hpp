#pragma once

#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"

inline l3_external_msgs::msg::VoyageTask make_valid_voyage_task(uint64_t task_id = 1) {
  l3_external_msgs::msg::VoyageTask msg;
  msg.stamp.sec = 1000;
  msg.stamp.nanosec = 0;
  msg.task_id = task_id;
  msg.optimization_priority = "fuel_optimal";
  // eta_window: stamp.sec=1000, latest=4600 → voyage duration=3600s (1 hour)
  msg.eta_window.earliest.sec = 2600;  // 26.7 min after stamp
  msg.eta_window.earliest.nanosec = 0;
  msg.eta_window.latest.sec = 4600;    // 60 min after stamp
  msg.eta_window.latest.nanosec = 0;
  msg.departure.latitude = 38.0;
  msg.departure.longitude = -122.0;
  msg.departure.altitude = 0.0;
  msg.destination.latitude = 37.5;
  msg.destination.longitude = -121.5;
  msg.destination.altitude = 0.0;
  {
    geographic_msgs::msg::GeoPoint wp1, wp2;
    wp1.latitude = 37.9; wp1.longitude = -121.8; wp1.altitude = 0.0;
    wp2.latitude = 37.7; wp2.longitude = -121.6; wp2.altitude = 0.0;
    msg.mandatory_waypoints = {wp1, wp2};
  }
  msg.confidence = 1.0f;
  msg.rationale = "test";
  return msg;
}
