#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_

#include <string>
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-01: Drift — decelerate to safe speed.
// Applicable when own ship speed exceeds the target drift speed.
// IEC 61508 SIL 2: no dynamic allocation, all methods noexcept.
class Mrm01Drift {
public:
  explicit Mrm01Drift(Mrm01Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] std::string rationale() const noexcept;

private:
  Mrm01Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_01_DRIFT_HPP_
