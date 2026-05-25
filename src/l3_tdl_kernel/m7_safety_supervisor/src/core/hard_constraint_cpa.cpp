#include "m7_safety_supervisor/core/hard_constraint_cpa.hpp"
#include <algorithm>
#include <cmath>

namespace mass_l3::m7::core {
namespace {
inline constexpr float kEpsilon = 0.001F;
}  // namespace

float compute_cpa_m7(float own_x, float own_y, float own_vx, float own_vy,
                     float tgt_x, float tgt_y, float tgt_vx, float tgt_vy) noexcept
{
  float const dx = tgt_x - own_x;
  float const dy = tgt_y - own_y;
  float const dvx = tgt_vx - own_vx;
  float const dvy = tgt_vy - own_vy;
  float const dv_sq = dvx * dvx + dvy * dvy;
  if (dv_sq < kEpsilon) {
    return std::sqrt(dx * dx + dy * dy);
  }
  float const t_cpa = -(dx * dvx + dy * dvy) / dv_sq;
  if (t_cpa <= 0.0F) {
    return std::sqrt(dx * dx + dy * dy);
  }
  float const cpa_x = dx + dvx * t_cpa;
  float const cpa_y = dy + dvy * t_cpa;
  return std::sqrt(cpa_x * cpa_x + cpa_y * cpa_y);
}

CpaConsistencyResult check_cpa_consistency(
    float cpa_m2, float dcpa_m5, float cpa_m7, float threshold) noexcept
{
  CpaConsistencyResult result{};
  float const denom = std::max(cpa_m7, kEpsilon);
  float const dev_m2 = std::abs(cpa_m2 - cpa_m7) / denom;
  float const dev_m5 = std::abs(dcpa_m5 - cpa_m7) / denom;
  result.deviation_pct = std::max(dev_m2, dev_m5) * 100.0F;
  if (dev_m2 > threshold) { result.violation_source |= 0x01U; }
  if (dev_m5 > threshold) { result.violation_source |= 0x02U; }
  result.consistent = (result.violation_source == 0U);
  return result;
}

}  // namespace mass_l3::m7::core
