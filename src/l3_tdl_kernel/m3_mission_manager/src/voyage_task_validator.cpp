#include "m3_mission_manager/voyage_task_validator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "geographic_msgs/msg/geo_path.hpp"
#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"

namespace mass_l3::m3 {
namespace {

constexpr double kEarthRadiusKm = 6371.0;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr int64_t kNanoPerSec = 1'000'000'000LL;
constexpr int64_t kTimestampToleranceNs = 5LL * kNanoPerSec;
constexpr double kMinLatitude = -90.0;
constexpr double kMaxLatitude = 90.0;
constexpr double kMinLongitude = -180.0;
constexpr double kMaxLongitude = 180.0;

// Haversine great-circle distance in km between two WGS84 lat/lon points.
double haversineKm(double lat1_deg, double lon1_deg,
                   double lat2_deg, double lon2_deg) noexcept {
  const double lat1 = lat1_deg * kDegToRad;
  const double lon1 = lon1_deg * kDegToRad;
  const double lat2 = lat2_deg * kDegToRad;
  const double lon2 = lon2_deg * kDegToRad;

  const double dlat = lat2 - lat1;
  const double dlon = lon2 - lon1;
  const double sin_dlat = std::sin(dlat * 0.5);
  const double sin_dlon = std::sin(dlon * 0.5);
  const double a = sin_dlat * sin_dlat +
                   std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));

  return kEarthRadiusKm * c;
}

bool isKnownOptimizationPriority(const std::string& optimization_priority) noexcept {
  const std::string known_types[] = {"fuel_optimal", "time_optimal", "balanced"};
  for (const auto& known : known_types) {
    if (optimization_priority == known) {
      return true;
    }
  }
  return false;
}

bool arePointsEqual(const geographic_msgs::msg::GeoPoint& a,
                    const geographic_msgs::msg::GeoPoint& b) noexcept {
  constexpr double kEpsilon = 1e-6;
  return (std::abs(a.latitude - b.latitude) < kEpsilon) &&
         (std::abs(a.longitude - b.longitude) < kEpsilon);
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

VoyageTaskValidator::VoyageTaskValidator(const VoyageTaskValidatorConfig& config)
    : config_(config) {}

ValidationResult VoyageTaskValidator::validate(
    const l3_external_msgs::msg::VoyageTask& task,
    const geographic_msgs::msg::GeoPoint& current_position,
    int64_t current_time_ns) const {
  // 1-3. Timestamp, departure distance, destination
  {
    const int64_t stamp_ns = static_cast<int64_t>(task.stamp.sec) * kNanoPerSec +
                             static_cast<int64_t>(task.stamp.nanosec);
    auto result = check_timestamp(stamp_ns, current_time_ns);
    if (!result.is_valid) { return result; }
    result = check_departure_distance(task.departure, current_position);
    if (!result.is_valid) { return result; }
    result = check_destination(task.destination);
    if (!result.is_valid) { return result; }
  }
  // 4. ETA window: (latest - stamp) must be in [min_s, max_s]
  {
    const int64_t kEtaS = static_cast<int64_t>(task.eta_window.latest.sec) -
                          static_cast<int64_t>(task.stamp.sec);
    const auto result = check_eta_window(kEtaS);
    if (!result.is_valid) { return result; }
  }
  // 5. Mandatory waypoints (already GeoPoint[], no conversion needed)
  {
    const auto result = check_mandatory_waypoints(task.mandatory_waypoints);
    if (!result.is_valid) { return result; }
  }
  // 6. Exclusion zones
  {
    const auto result = check_exclusion_zones(task.exclusion_zones, task.departure);
    if (!result.is_valid) { return result; }
  }
  // 7. Optimization priority must be a known value
  {
    const auto result = check_optimization_priority(task.optimization_priority);
    if (!result.is_valid) { return result; }
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// ---------------------------------------------------------------------------
// Private check implementations (each <= 40 lines)
// ---------------------------------------------------------------------------

// 1. Reject if task timestamp is more than 5s in the future
ValidationResult VoyageTaskValidator::check_timestamp(int64_t task_stamp_ns,
                                                       int64_t now_ns) const {
  if (task_stamp_ns > now_ns + kTimestampToleranceNs) {
    return ValidationResult{false, ErrorCode::VoyageTaskParseError,
                            "voyage task timestamp is in the future"};
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 2. Reject if departure point is too far from ownship position
ValidationResult VoyageTaskValidator::check_departure_distance(
    const geographic_msgs::msg::GeoPoint& departure,
    const geographic_msgs::msg::GeoPoint& current_position) const {
  const double dist_km = haversineKm(
      current_position.latitude, current_position.longitude,
      departure.latitude, departure.longitude);

  if (dist_km > config_.departure_distance_max_km) {
    return ValidationResult{false, ErrorCode::InvalidDeparture,
                            "departure point exceeds max distance from current position"};
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 3. Reject if destination lat/lon are outside valid geographic range
ValidationResult VoyageTaskValidator::check_destination(
    const geographic_msgs::msg::GeoPoint& dest) const {
  if (dest.latitude < kMinLatitude || dest.latitude > kMaxLatitude) {
    return ValidationResult{false, ErrorCode::InvalidDestination,
                            "destination latitude out of range [-90, 90]"};
  }
  if (dest.longitude < kMinLongitude || dest.longitude > kMaxLongitude) {
    return ValidationResult{false, ErrorCode::InvalidDestination,
                            "destination longitude out of range [-180, 180]"};
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 4. Reject if eta_requirement_s is outside [min_s, max_s].
// VoyageTask carries a single ETA duration, not an earliest/latest pair.
ValidationResult VoyageTaskValidator::check_eta_window(
    int64_t eta_requirement_s) const {
  if (eta_requirement_s < config_.eta_window_min_s) {
    return ValidationResult{false, ErrorCode::EtaOutOfBounds,
                            "ETA requirement below minimum window (too short voyage)"};
  }
  if (eta_requirement_s > config_.eta_window_max_s) {
    return ValidationResult{false, ErrorCode::EtaWindowExceeded,
                            "ETA requirement exceeds maximum window (72h operational ceiling)"};
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 5. Reject if waypoints are empty, have duplicates, or exceed geographic bounds
ValidationResult VoyageTaskValidator::check_mandatory_waypoints(
    const std::vector<geographic_msgs::msg::GeoPoint>& waypoints) const {
  if (waypoints.empty()) {
    return ValidationResult{false, ErrorCode::VoyageTaskParseError,
                            "voyage task has no mandatory waypoints"};
  }

  for (size_t i = 0; i < waypoints.size(); ++i) {
    const auto& wp = waypoints[i];
    if (wp.latitude < kMinLatitude || wp.latitude > kMaxLatitude ||
        wp.longitude < kMinLongitude || wp.longitude > kMaxLongitude) {
      return ValidationResult{false, ErrorCode::VoyageTaskParseError,
                              "waypoint coordinate out of range"};
    }
    // Check for consecutive duplicate waypoints
    if (i > 0 && arePointsEqual(waypoints[i - 1], wp)) {
      return ValidationResult{false, ErrorCode::VoyageTaskParseError,
                              "consecutive duplicate waypoints detected"};
    }
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 6. Reject if any exclusion zone contains the departure point (with buffer)
ValidationResult VoyageTaskValidator::check_exclusion_zones(
    const std::vector<geographic_msgs::msg::GeoPath>& exclusion_zones,
    const geographic_msgs::msg::GeoPoint& departure) const {
  for (const auto& zone : exclusion_zones) {
    for (const auto& pose : zone.poses) {
      const double dist_km = haversineKm(
          departure.latitude, departure.longitude,
          pose.pose.position.latitude, pose.pose.position.longitude);
      const double dist_m = dist_km * 1000.0;
      if (dist_m < config_.exclusion_zone_buffer_m) {
        return ValidationResult{false, ErrorCode::ExclusionZoneOverlap,
                                "departure point overlaps with exclusion zone"};
      }
    }
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

// 7. Reject if optimization priority is not one of the known values
ValidationResult VoyageTaskValidator::check_optimization_priority(
    const std::string& optimization_priority) const {
  if (!isKnownOptimizationPriority(optimization_priority)) {
    return ValidationResult{false, ErrorCode::VoyageTaskParseError,
                            "unknown optimization priority: " + optimization_priority};
  }
  return ValidationResult{true, ErrorCode::Ok, ""};
}

}  // namespace mass_l3::m3
