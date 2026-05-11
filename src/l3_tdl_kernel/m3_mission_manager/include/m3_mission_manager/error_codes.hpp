#pragma once

#include <cstdint>
#include <string_view>

namespace mass_l3::m3 {

enum class ErrorCode : int32_t {
  Ok = 0,
  ParameterOutOfRange = 3001,
  VoyageTaskParseError = 3002,
  InvalidDeparture = 3003,
  InvalidDestination = 3004,
  EtaOutOfBounds = 3005,
  EtaWindowExceeded = 3006,
  PlannedRouteMismatch = 3007,
  ReplanDeadlineExceeded = 3008,
  ReplanResponseTimeout = 3009,
  MissionStateTransition = 3010,
  SpeedProfileMissing = 3011,
  ExclusionZoneOverlap = 3012,
  SerializationFailed = 3013,
};

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept;

}  // namespace mass_l3::m3
