#include "m7_safety_supervisor/mrm/mrm_01_drift.hpp"

#include <string>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"

namespace mass_l3::m7::mrm {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Mrm01Drift::Mrm01Drift(Mrm01Params const& params) noexcept
  : params_{params}
{}

// ---------------------------------------------------------------------------
// is_applicable()
// Drift-down is applicable only when own ship speed is above target speed.
// ---------------------------------------------------------------------------

bool Mrm01Drift::is_applicable(
    l3_msgs::msg::ODDState const& /*odd*/,
    l3_msgs::msg::WorldState const& world) const noexcept
{
  return world.own_ship.sog_kn > params_.target_speed_kn;
}

// ---------------------------------------------------------------------------
// rationale()
// ---------------------------------------------------------------------------

std::string Mrm01Drift::rationale() const noexcept {
  return std::string{"MRM-01 DRIFT: decelerating to target speed "} +
         std::to_string(params_.target_speed_kn) + " kn";
}

}  // namespace mass_l3::m7::mrm
