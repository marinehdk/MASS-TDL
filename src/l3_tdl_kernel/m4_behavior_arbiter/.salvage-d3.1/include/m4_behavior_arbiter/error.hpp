#pragma once

#include <cstdint>

namespace mass_l3::m4 {

enum class ErrorCode : uint16_t {
  Ok                 = 0,
  YamlLoadFailed     = 4001,
  YamlMissingKey     = 4002,
  YamlInvalidValue   = 4003,
  DictionaryNotLoaded = 4010,
  InvalidBehaviorType = 4011,
  SolverTimeout      = 4020,
  SolverInfeasible   = 4021,
};

}  // namespace mass_l3::m4
