#include "m7_safety_supervisor/core/hard_constraint_colregs.hpp"
#include <cmath>

namespace mass_l3::m7::core {
namespace {
inline constexpr float kSmallTurnDeg = 5.0F;
inline constexpr float kLargeTurnDeg = 15.0F;
}  // namespace

ColregsGeometryResult check_colregs_geometry(
    ColregsRule rule, float heading_change_deg) noexcept
{
  ColregsGeometryResult result{};
  float const abs_hdg = std::abs(heading_change_deg);
  switch (rule) {
    case ColregsRule::kRule13Overtaking:
      result.min_expected_alteration_deg = 0.0F;
      result.consistent = (abs_hdg < kSmallTurnDeg);
      if (!result.consistent) result.violation_reason = 1U;
      break;
    case ColregsRule::kRule14HeadOn:
      result.min_expected_alteration_deg = kLargeTurnDeg;
      result.consistent = (heading_change_deg > kSmallTurnDeg);
      if (!result.consistent) result.violation_reason = 1U;
      break;
    case ColregsRule::kRule15GiveWay:
      result.min_expected_alteration_deg = kLargeTurnDeg;
      result.consistent = (heading_change_deg > kSmallTurnDeg);
      if (!result.consistent) result.violation_reason = 1U;
      break;
    case ColregsRule::kRule15StandOn:
      result.min_expected_alteration_deg = 0.0F;
      result.consistent = (abs_hdg < kSmallTurnDeg);
      if (!result.consistent) result.violation_reason = 2U;
      break;
    case ColregsRule::kRule16GiveWay:
      result.min_expected_alteration_deg = kLargeTurnDeg;
      result.consistent = (abs_hdg > kLargeTurnDeg);
      if (!result.consistent) result.violation_reason = 2U;
      break;
    case ColregsRule::kRule17StandOn:
      result.min_expected_alteration_deg = 0.0F;
      result.consistent = (abs_hdg < kSmallTurnDeg);
      if (!result.consistent) result.violation_reason = 2U;
      break;
    case ColregsRule::kMrmTriggered:
      result.min_expected_alteration_deg = 0.0F;
      result.consistent = (abs_hdg < 0.1F);
      if (!result.consistent) result.violation_reason = 1U;
      break;
    default:
      result.min_expected_alteration_deg = 0.0F;
      result.consistent = true;
      break;
  }
  return result;
}

}  // namespace mass_l3::m7::core
