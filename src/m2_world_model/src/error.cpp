#include "m2_world_model/error.hpp"

namespace mass_l3::m2 {

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:                  return "Ok";
    case ErrorCode::ParameterOutOfRange: return "ParameterOutOfRange";
    case ErrorCode::ParameterFileNotFound:  return "ParameterFileNotFound";
    case ErrorCode::ParameterFileMalformed: return "ParameterFileMalformed";
    case ErrorCode::TrackBufferOverflow: return "TrackBufferOverflow";
    case ErrorCode::TrackNotFound:       return "TrackNotFound";
    case ErrorCode::ExtrapolationFailed: return "ExtrapolationFailed";
    case ErrorCode::InvalidCovariance:   return "InvalidCovariance";
    case ErrorCode::MonotonicityViolation: return "MonotonicityViolation";
    case ErrorCode::EncDataNotFound:     return "EncDataNotFound";
    case ErrorCode::EncDataMalformed:    return "EncDataMalformed";
    case ErrorCode::CoordTransformError: return "CoordTransformError";
    case ErrorCode::AggregationFailed:   return "AggregationFailed";
    default:                             return "UnknownErrorCode";
  }
}

}  // namespace mass_l3::m2
