#include "m7_safety_supervisor/mrm/mrm_02_anchor.hpp"

#include <string>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"

namespace mass_l3::m7::mrm {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Mrm02Anchor::Mrm02Anchor(Mrm02Params const& params) noexcept
  : params_{params}
{}

// ---------------------------------------------------------------------------
// is_applicable()
// Anchor requires: speed <= max_speed_kn AND water depth <= max_water_depth_m.
// ---------------------------------------------------------------------------

bool Mrm02Anchor::is_applicable(
    l3_msgs::msg::ODDState const& /*odd*/,
    l3_msgs::msg::WorldState const& world) const noexcept
{
  bool const kSpeedOk = world.own_ship.sog_kn <= params_.max_speed_kn;
  bool const kDepthOk =
      static_cast<double>(world.zone.min_water_depth_m) <= params_.max_water_depth_m;
  return kSpeedOk && kDepthOk;
}

// ---------------------------------------------------------------------------
// rationale()
// ---------------------------------------------------------------------------

std::string Mrm02Anchor::rationale() noexcept {
  return std::string{"MRM-02 ANCHOR: water depth + speed conditions met"};
}

}  // namespace mass_l3::m7::mrm
