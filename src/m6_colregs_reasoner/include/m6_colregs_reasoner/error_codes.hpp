#pragma once
#include <cstdint>
#include <string_view>

namespace mass_l3::m6_colregs {

enum class ErrorCode : int32_t {
  Ok = 0,
  ParameterOutOfRange = 6001,
  RuleLibraryNotFound = 6002,
  RuleLibraryLoadFailed = 6003,
  RuleEvaluationError = 6004,
  PhaseClassificationError = 6005,
  ConstraintGenerationError = 6006,
  TargetStateOverflow = 6007,
  OddDomainUnknown = 6008,
  WorldStateStale = 6009,
  ODDStateStale = 6010,
};

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept;
}  // namespace mass_l3::m6_colregs
