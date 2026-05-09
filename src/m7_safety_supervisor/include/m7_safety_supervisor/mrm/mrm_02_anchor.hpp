#ifndef M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_
#define M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_

#include <string>
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"

namespace mass_l3::m7::mrm {

// MRM-02: Anchor — requires shallow water and low speed.
// Applicable when water_depth <= 30m AND own ship speed <= 4 kn.
// IEC 61508 SIL 2: no dynamic allocation, all methods noexcept.
class Mrm02Anchor {
public:
  explicit Mrm02Anchor(Mrm02Params const& params) noexcept;

  [[nodiscard]] bool is_applicable(l3_msgs::msg::ODDState const& odd,
                                   l3_msgs::msg::WorldState const& world) const noexcept;

  [[nodiscard]] static std::string rationale() noexcept;

private:
  Mrm02Params params_;
};

}  // namespace mass_l3::m7::mrm

#endif  // M7_SAFETY_SUPERVISOR_MRM_MRM_02_ANCHOR_HPP_
