#include "m7_safety_supervisor/core/hard_constraint_rot.hpp"
#include <cmath>

namespace mass_l3::m7::core {
namespace {
inline constexpr float kRotTolerance = 1.05F;
}  // namespace

RotLimitResult check_rot_limit(float rot, float rot_limit) noexcept
{
  RotLimitResult result{};
  if (rot_limit <= 0.0F) {
    result.violation = true;
    result.compliant = false;
    result.excess_pct = 100.0F;
    return result;
  }
  float const abs_rot = std::abs(rot);
  float const effective_limit = rot_limit * kRotTolerance;
  result.compliant = (abs_rot <= effective_limit);
  result.violation = !result.compliant;
  if (result.violation) {
    result.excess_pct = ((abs_rot - effective_limit) / rot_limit) * 100.0F;
  }
  return result;
}

}  // namespace mass_l3::m7::core
