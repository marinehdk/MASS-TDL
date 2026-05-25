#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_CPA_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_CPA_HPP_

#include <cstdint>
#include <string>

namespace mass_l3::m7::core {

struct CpaConsistencyResult {
  bool consistent{false};
  float deviation_pct{0.0F};
  std::uint8_t violation_source{0};
};

CpaConsistencyResult check_cpa_consistency(
    float cpa_m2, float dcpa_m5, float cpa_m7,
    float threshold = 0.10F) noexcept;

float compute_cpa_m7(float own_x, float own_y, float own_vx, float own_vy,
                     float tgt_x, float tgt_y, float tgt_vx, float tgt_vy) noexcept;

}  // namespace mass_l3::m7::core

#endif
