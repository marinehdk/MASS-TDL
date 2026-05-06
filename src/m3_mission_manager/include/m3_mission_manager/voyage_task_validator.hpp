#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <Eigen/Core>
#include "geographic_msgs/msg/geo_point.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "m3_mission_manager/error_codes.hpp"

namespace mass_l3::m3 {

struct ValidationResult {
  bool is_valid;
  ErrorCode error_code;
  std::string failed_check;
};

struct VoyageTaskValidatorConfig {
  double departure_distance_max_km;  // [TBD-HAZID] 2.0
  int64_t eta_window_min_s;          // [TBD-HAZID] 600
  int64_t eta_window_max_s;          // 259200
  double waypoint_max_distance_nm;   // [TBD-HAZID] 50.0
  double exclusion_zone_buffer_m;    // [TBD-HAZID] 500.0
};

class VoyageTaskValidator {
 public:
  explicit VoyageTaskValidator(const VoyageTaskValidatorConfig& config);
  ~VoyageTaskValidator() = default;
  VoyageTaskValidator(const VoyageTaskValidator&) = delete;
  VoyageTaskValidator& operator=(const VoyageTaskValidator&) = delete;
  VoyageTaskValidator(VoyageTaskValidator&&) = default;
  VoyageTaskValidator& operator=(VoyageTaskValidator&&) = default;

  ValidationResult validate(const l3_external_msgs::msg::VoyageTask& task,
                            const geographic_msgs::msg::GeoPoint& current_position,
                            int64_t current_time_ns) const;

 private:
  // 8 individual check methods (each <= 40 lines)
  // 1. check_timestamp -- task stamp not in future
  // 2. check_departure_distance -- departure within max distance of current pos
  // 3. check_destination -- valid lat/lon range
  // 4. check_eta_window -- earliest/latest within bounds
  // 5. check_mandatory_waypoints -- reasonable number, no duplicates
  // 6. check_exclusion_zones -- valid zones, no overlap with departure
  // 7. check_task_type -- must be known type
  // 8. check_speed_cmd -- speed within [1, 30] kn

  ValidationResult check_timestamp(int64_t task_stamp_ns, int64_t now_ns) const;
  ValidationResult check_departure_distance(
      const geographic_msgs::msg::GeoPoint& departure,
      const geographic_msgs::msg::GeoPoint& current_position) const;
  ValidationResult check_destination(const geographic_msgs::msg::GeoPoint& dest) const;
  ValidationResult check_eta_window(int64_t earliest_ns, int64_t latest_ns,
                                    int64_t now_ns) const;
  ValidationResult check_mandatory_waypoints(
      const std::vector<geographic_msgs::msg::GeoPoint>& waypoints) const;
  ValidationResult check_exclusion_zones(
      const std::vector<geographic_msgs::msg::GeoPoint>& exclusion_zones,
      const geographic_msgs::msg::GeoPoint& departure) const;
  ValidationResult check_task_type(const std::string& task_type) const;
  ValidationResult check_speed_cmd(double speed_kn) const;

  VoyageTaskValidatorConfig config_;
};

}  // namespace mass_l3::m3
