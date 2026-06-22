#pragma once

/// @file types.hpp
/// @brief M4 Behavior Arbiter — type aliases for all consumed L3 ROS2 messages.
///
/// Provides short-form aliases (e.g. ODDStateMsg) for the full ROS2 generated
/// types, centralising the include footprint in one place.

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"

// D3.1: Only behaviors 0-4 are active in this release.
// DpHold(2), Berth(3), MrcAnchor(5), MrcHeaveTo(6) are reserved for DEMO-3 (8/31).
enum class BehaviorType : uint8_t {
  TRANSIT = 0,
  COLREG_AVOID = 1,
  DP_HOLD = 2,
  BERTH = 3,
  MRC_DRIFT = 4,
  MRC_ANCHOR = 5,
  MRC_HEAVE_TO = 6,
  RECOVERY = 7,
};

namespace mass_l3::m4 {

using ODDStateMsg = l3_msgs::msg::ODDState;
using WorldStateMsg = l3_msgs::msg::WorldState;
using ModeCmdMsg = l3_msgs::msg::ModeCmd;
using MissionGoalMsg = l3_msgs::msg::MissionGoal;
using COLREGsConstraintMsg = l3_msgs::msg::COLREGsConstraint;
using BehaviorPlanMsg = l3_msgs::msg::BehaviorPlan;

}  // namespace mass_l3::m4
