#include "m3_mission_manager/error_codes.hpp"

namespace mass_l3::m3 {

[[nodiscard]] std::string_view error_code_str(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::Ok:
      return "Ok";
    case ErrorCode::ParameterOutOfRange:
      return "ParameterOutOfRange";
    case ErrorCode::VoyageTaskParseError:
      return "VoyageTaskParseError";
    case ErrorCode::InvalidDeparture:
      return "InvalidDeparture";
    case ErrorCode::InvalidDestination:
      return "InvalidDestination";
    case ErrorCode::EtaOutOfBounds:
      return "EtaOutOfBounds";
    case ErrorCode::EtaWindowExceeded:
      return "EtaWindowExceeded";
    case ErrorCode::PlannedRouteMismatch:
      return "PlannedRouteMismatch";
    case ErrorCode::ReplanDeadlineExceeded:
      return "ReplanDeadlineExceeded";
    case ErrorCode::ReplanResponseTimeout:
      return "ReplanResponseTimeout";
    case ErrorCode::MissionStateTransition:
      return "MissionStateTransition";
    case ErrorCode::SpeedProfileMissing:
      return "SpeedProfileMissing";
    case ErrorCode::ExclusionZoneOverlap:
      return "ExclusionZoneOverlap";
    case ErrorCode::SerializationFailed:
      return "SerializationFailed";
  }
  return "Unknown";
}

}  // namespace mass_l3::m3
