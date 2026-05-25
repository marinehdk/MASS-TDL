#ifndef M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_COLREGS_HPP_
#define M7_SAFETY_SUPERVISOR_CORE_HARD_CONSTRAINT_COLREGS_HPP_

#include <cstdint>

namespace mass_l3::m7::core {

enum class ColregsRule : std::uint8_t {
  kUnknown = 0,
  kRule13Overtaking,
  kRule14HeadOn,
  kRule15GiveWay,
  kRule15StandOn,
  kRule16GiveWay,
  kRule17StandOn,
  kMrmTriggered,
  kCount
};

struct ColregsGeometryResult {
  bool consistent{false};
  float min_expected_alteration_deg{0.0F};
  std::uint8_t violation_reason{0};
};

ColregsGeometryResult check_colregs_geometry(
    ColregsRule rule, float heading_change_deg) noexcept;

}  // namespace mass_l3::m7::core

#endif
