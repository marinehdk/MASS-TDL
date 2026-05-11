#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_

#include <string>
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-04: Mooring — harbor mooring maneuver.
// Applicable when in ODD-C (harbor) zone AND own ship speed <= 2 kn.
// IEC 61508 SIL 2: no dynamic allocation, all methods noexcept.
class Mrm04Mooring {
public:
  explicit Mrm04Mooring(Mrm04Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] static std::string rationale() noexcept;

private:
  Mrm04Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_04_MOORING_HPP_
