#pragma once

#include <cstdint>

#include "l3_msgs/msg/avoidance_plan.hpp"

namespace mass_l3::m5 {

std::uint32_t avoidance_route_hash(const l3_msgs::msg::AvoidancePlan& plan);

}  // namespace mass_l3::m5
