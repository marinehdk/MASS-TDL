// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
//
// SPDX-License-Identifier: Proprietary
//
// M3 Mission Manager — real ROS2 entry point.
// Per v1.1.2 §7 + §3.3 Node Topology.

#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m3_mission_manager/mission_manager_node.hpp"

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m3::MissionManagerNode>();
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
