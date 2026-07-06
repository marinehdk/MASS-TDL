#pragma once

#include <cstdint>

#include "l3_msgs/msg/behavior_plan.hpp"

namespace mass_l3::m5 {

inline bool should_emit_collision_avoidance_waypoints(
    bool colregs_conflict_active,
    std::uint8_t m4_behavior) {
  return colregs_conflict_active &&
      m4_behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID;
}

}  // namespace mass_l3::m5
