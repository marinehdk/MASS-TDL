// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// DoD-5: schema_version = 121 (v1.2.1) in MissionGoal and 120 (v1.2.0) in RouteReplanRequest — spec §8

#include <gtest/gtest.h>
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"

namespace {

TEST(IDLSchemaVersionTest, MissionGoalDefaultFieldsAreZeroSafe) {
  l3_msgs::msg::MissionGoal msg;
  EXPECT_EQ(msg.current_error_severity, 0U);
  EXPECT_FLOAT_EQ(msg.xte_nm, 0.0F);
  EXPECT_FLOAT_EQ(msg.sea_current_kn, 0.0F);
  EXPECT_EQ(msg.l1_watchdog_status, 0U);
}

TEST(IDLSchemaVersionTest, MissionGoalSchemaVersionField) {
  l3_msgs::msg::MissionGoal msg;
  msg.schema_version = 121U;
  EXPECT_EQ(msg.schema_version, 121U);
}

TEST(IDLSchemaVersionTest, RouteReplanRequestSchemaVersionField) {
  l3_msgs::msg::RouteReplanRequest msg;
  msg.schema_version = 120U;
  EXPECT_EQ(msg.schema_version, 120U);
}

TEST(IDLSchemaVersionTest, MissionGoalCurrentErrorSeverityHighValue) {
  l3_msgs::msg::MissionGoal msg;
  msg.current_error_severity = 2U;
  EXPECT_EQ(msg.current_error_severity, 2U);
  msg.xte_nm = -1.0F;
  EXPECT_FLOAT_EQ(msg.xte_nm, -1.0F);
}

}
