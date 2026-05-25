#include "m7_safety_supervisor/core/hard_constraint_speed.hpp"

namespace mass_l3::m7::core {
namespace {
inline constexpr float kSpeedTolerance = 1.05F;
}  // namespace

SpeedLimitResult check_speed_limit(float speed, float speed_limit) noexcept
{
  SpeedLimitResult result{};
  if (speed < 0.0F || speed_limit <= 0.0F) {
    result.violation = true;
    result.compliant = false;
    result.excess_pct = 100.0F;
    return result;
  }
  float const effective_limit = speed_limit * kSpeedTolerance;
  result.compliant = (speed <= effective_limit);
  result.violation = !result.compliant;
  if (result.violation) {
    result.excess_pct = ((speed - effective_limit) / speed_limit) * 100.0F;
  }
  return result;
}

}  // namespace mass_l3::m7::core
