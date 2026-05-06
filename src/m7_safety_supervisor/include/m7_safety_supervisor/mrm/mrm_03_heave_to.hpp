#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_

#include <string>
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-03: Heave-to — emergency turn when multiple close targets present.
// Applicable when >= 2 targets have CPA < 1 NM.
// IEC 61508 SIL 2: no dynamic allocation, all methods noexcept.
class Mrm03HeaveTo {
public:
  explicit Mrm03HeaveTo(Mrm03Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] std::string rationale() const noexcept;

private:
  Mrm03Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_03_HEAVE_TO_HPP_
