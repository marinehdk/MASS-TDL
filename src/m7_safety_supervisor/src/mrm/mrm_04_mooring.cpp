#include "m7_safety_supervisor/mrm/mrm_04_mooring.hpp"

#include <string>

#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"

namespace mass_l3::m7::mrm {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

Mrm04Mooring::Mrm04Mooring(Mrm04Params const& params) noexcept
  : params_{params}
{}

// ---------------------------------------------------------------------------
// is_applicable()
// Mooring requires: ODD-C (harbor) zone AND speed <= max_speed_kn.
// ---------------------------------------------------------------------------

bool Mrm04Mooring::is_applicable(
    l3_msgs::msg::ODDState const& odd,
    l3_msgs::msg::WorldState const& world) const noexcept
{
  bool const kInHarbor = (!params_.requires_harbor_zone) ||
                          (odd.current_zone == l3_msgs::msg::ODDState::ODD_ZONE_C);
  bool const kSpeedOk = world.own_ship.sog_kn <= params_.max_speed_kn;
  return kInHarbor && kSpeedOk;
}

// ---------------------------------------------------------------------------
// rationale()
// ---------------------------------------------------------------------------

std::string Mrm04Mooring::rationale() noexcept {
  return std::string{"MRM-04 MOORING: harbor zone with speed constraint met"};
}

}  // namespace mass_l3::m7::mrm
