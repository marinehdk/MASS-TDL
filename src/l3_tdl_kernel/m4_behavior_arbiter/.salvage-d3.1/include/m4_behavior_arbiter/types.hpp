#pragma once

#include <cstdint>
#include <string>

namespace mass_l3::m4 {

enum class BehaviorType : uint8_t {
  Transit       = 0,
  ColregAvoid   = 1,
  DpHold        = 2,
  Berth         = 3,
  MrcDrift      = 4,
  MrcAnchor     = 5,
  MrcHeaveTo    = 6,
};

constexpr size_t kBehaviorCount = 7;

enum class HealthState : uint8_t {
  Normal   = 0,
  Degraded = 1,
  Critical = 2,
};

}  // namespace mass_l3::m4
