#include "m6_colregs_reasoner/error_codes.hpp"

namespace mass_l3::m6_colregs {

std::string_view error_code_str(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "Ok";
    case ErrorCode::ParameterOutOfRange:
      return "ParameterOutOfRange";
    case ErrorCode::RuleLibraryNotFound:
      return "RuleLibraryNotFound";
    case ErrorCode::RuleLibraryLoadFailed:
      return "RuleLibraryLoadFailed";
    case ErrorCode::RuleEvaluationError:
      return "RuleEvaluationError";
    case ErrorCode::PhaseClassificationError:
      return "PhaseClassificationError";
    case ErrorCode::ConstraintGenerationError:
      return "ConstraintGenerationError";
    case ErrorCode::TargetStateOverflow:
      return "TargetStateOverflow";
    case ErrorCode::OddDomainUnknown:
      return "OddDomainUnknown";
    case ErrorCode::WorldStateStale:
      return "WorldStateStale";
    case ErrorCode::ODDStateStale:
      return "ODDStateStale";
    default:
      return "UnknownErrorCode";
  }
}

}  // namespace mass_l3::m6_colregs
