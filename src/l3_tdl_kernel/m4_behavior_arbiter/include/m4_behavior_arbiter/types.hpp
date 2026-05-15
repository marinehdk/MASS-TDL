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

namespace mass_l3::m4 {

using ODDStateMsg = l3_msgs::msg::ODDState;
using WorldStateMsg = l3_msgs::msg::WorldState;
using ModeCmdMsg = l3_msgs::msg::ModeCmd;
using MissionGoalMsg = l3_msgs::msg::MissionGoal;
using COLREGsConstraintMsg = l3_msgs::msg::COLREGsConstraint;
using BehaviorPlanMsg = l3_msgs::msg::BehaviorPlan;

}  // namespace mass_l3::m4
