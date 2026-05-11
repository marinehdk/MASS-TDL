#pragma once

#include <cstdint>
#include <string_view>

namespace mass_l3::m1 {

/// Error codes for M1 ODD/Envelope Manager.
/// Range 1000-1999 reserved for M1 module errors.
enum class ErrorCode : int32_t {
  Ok = 0,
  ParameterOutOfRange = 1001,
  ParameterFileNotFound = 1002,
  ParameterFileMalformed = 1003,
  WeightsNotNormalized = 1004,
  ThresholdsNonMonotonic = 1005,
  EScoreInvalidInputs = 1006,
  TScoreInvalidInputs = 1007,
  HScoreInvalidInputs = 1008,
  TmrOutOfBounds = 1009,
  TdlOutOfBounds = 1010,
  MrcUnselectable = 1011,
  StateMachineInvalidTransition = 1012,
  InputStaleness = 1013,
  AsdrSerializationFailed = 1014,
};

/// Return a human-readable string for an error code.
[[nodiscard]] inline std::string_view error_code_str(ErrorCode code) noexcept {
  using namespace std::string_view_literals;
  switch (code) {
    case ErrorCode::Ok:
      return "Ok"sv;
    case ErrorCode::ParameterOutOfRange:
      return "ParameterOutOfRange"sv;
    case ErrorCode::ParameterFileNotFound:
      return "ParameterFileNotFound"sv;
    case ErrorCode::ParameterFileMalformed:
      return "ParameterFileMalformed"sv;
    case ErrorCode::WeightsNotNormalized:
      return "WeightsNotNormalized"sv;
    case ErrorCode::ThresholdsNonMonotonic:
      return "ThresholdsNonMonotonic"sv;
    case ErrorCode::EScoreInvalidInputs:
      return "EScoreInvalidInputs"sv;
    case ErrorCode::TScoreInvalidInputs:
      return "TScoreInvalidInputs"sv;
    case ErrorCode::HScoreInvalidInputs:
      return "HScoreInvalidInputs"sv;
    case ErrorCode::TmrOutOfBounds:
      return "TmrOutOfBounds"sv;
    case ErrorCode::TdlOutOfBounds:
      return "TdlOutOfBounds"sv;
    case ErrorCode::MrcUnselectable:
      return "MrcUnselectable"sv;
    case ErrorCode::StateMachineInvalidTransition:
      return "StateMachineInvalidTransition"sv;
    case ErrorCode::InputStaleness:
      return "InputStaleness"sv;
    case ErrorCode::AsdrSerializationFailed:
      return "AsdrSerializationFailed"sv;
  }
  return "Unknown"sv;
}

}  // namespace mass_l3::m1
