#include "m5_tactical_planner/bc_mpc/bc_mpc_override_generator.hpp"

#include "m5_tactical_planner/common/units.hpp"

namespace mass_l3::m5::bc_mpc {

// generate() — unit conversion at the ROS2 message boundary.
// Heading normalisation to [0, 360): heading_cmd_rad may be in (-π, π] from
// branch selection; ReactiveOverrideCmd convention is [0, 360) display range.
l3_msgs::msg::ReactiveOverrideCmd BcMpcOverrideGenerator::generate(
    const BcMpcSolution& solution,
    const rclcpp::Time& trigger_time) const
{
  l3_msgs::msg::ReactiveOverrideCmd cmd;

  cmd.trigger_time   = trigger_time;
  cmd.trigger_reason = solution.trigger_reason;

  double heading_deg = solution.heading_cmd_rad * units::kDegPerRad;
  if (heading_deg < 0.0) {
    heading_deg += 360.0;
  }
  cmd.heading_cmd_deg = static_cast<float>(heading_deg);
  cmd.speed_cmd_kn    = static_cast<float>(solution.optimal_speed_mps * units::kKnPerMs);
  cmd.rot_cmd_deg_s   = static_cast<float>(solution.rot_cmd_rad_s * units::kDegPerRad);
  cmd.validity_s      = static_cast<float>(solution.validity_s);
  cmd.confidence      = static_cast<float>(solution.confidence);

  return cmd;
}

}  // namespace mass_l3::m5::bc_mpc
