// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
//
// SPDX-License-Identifier: Proprietary
//
// Integration smoke test — M3 MissionManagerNode creation and destruction.
// Per v1.1.2 §7 Node Topology.

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include "m3_mission_manager/mission_manager_node.hpp"

TEST(MissionManagerNodeSmoke, CreateNode)
{
  rclcpp::init(0, nullptr);
  ASSERT_NO_THROW({
    auto node = std::make_shared<mass_l3::m3::MissionManagerNode>();
    ASSERT_NE(node, nullptr);
  });
  rclcpp::shutdown();
}
