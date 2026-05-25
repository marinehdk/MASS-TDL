#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_SPEED_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_SPEED_HPP_

namespace mass_l3::m7::core {

struct SpeedLimitResult {
  bool compliant{false};
  bool violation{false};
  float excess_pct{0.0F};
};

SpeedLimitResult check_speed_limit(float speed, float speed_limit) noexcept;

}  // namespace mass_l3::m7::core

#endif
