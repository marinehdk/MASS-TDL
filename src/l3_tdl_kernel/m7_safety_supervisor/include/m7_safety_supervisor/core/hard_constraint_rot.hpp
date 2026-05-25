#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_ROT_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_ROT_HPP_

namespace mass_l3::m7::core {

struct RotLimitResult {
  bool compliant{false};
  bool violation{false};
  float excess_pct{0.0F};
};

RotLimitResult check_rot_limit(float rot, float rot_limit) noexcept;

}  // namespace mass_l3::m7::core

#endif
