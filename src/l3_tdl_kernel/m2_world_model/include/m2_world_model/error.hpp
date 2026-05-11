#pragma once
#include <cstdint>
#include <string_view>

namespace mass_l3::m2 {

enum class ErrorCode : int32_t {
  Ok = 0,
  ParameterOutOfRange = 2001,
  ParameterFileNotFound = 2002,
  ParameterFileMalformed = 2003,
  TrackBufferOverflow = 2004,
  TrackNotFound = 2005,
  ExtrapolationFailed = 2006,
  InvalidCovariance = 2007,
  MonotonicityViolation = 2008,
  EncDataNotFound = 2009,
  EncDataMalformed = 2010,
  CoordTransformError = 2011,
  AggregationFailed = 2012,
};

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept;
}  // namespace mass_l3::m2
