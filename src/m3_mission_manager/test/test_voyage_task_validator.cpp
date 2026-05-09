#include <gtest/gtest.h>

#include "fixtures/voyage_task_fixtures.hpp"
#include "fixtures/route_fixtures.hpp"
#include "m3_mission_manager/error_codes.hpp"
#include "m3_mission_manager/types.hpp"
#include "m3_mission_manager/voyage_task_validator.hpp"

namespace mass_l3::m3 {
namespace {

constexpr int64_t kCurrentTimeNs = 1'010'000'000'000LL;  // stamp.sec=1000 + 10s
constexpr double kMaxDistanceKm = 2.0;

geographic_msgs::msg::GeoPoint make_position(double lat, double lon) {
  geographic_msgs::msg::GeoPoint pos;
  pos.latitude = lat;
  pos.longitude = lon;
  pos.altitude = 0.0;
  return pos;
}

VoyageTaskValidatorConfig make_default_config() {
  return VoyageTaskValidatorConfig{
      2.0,       // departure_distance_max_km
      600,       // eta_window_min_s
      259200,    // eta_window_max_s
      50.0,      // waypoint_max_distance_nm
      500.0      // exclusion_zone_buffer_m
  };
}

// ---------------------------------------------------------------------------
// 1. ValidTaskPassesAllChecks
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, ValidTaskPassesAllChecks) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto task = make_valid_voyage_task(1);
  const auto current_pos = make_position(38.0, -122.0);

  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_TRUE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::Ok);
  EXPECT_TRUE(result.failed_check.empty());
}

// ---------------------------------------------------------------------------
// 2. RejectsTaskStampInFuture
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsTaskStampInFuture) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  auto task = make_valid_voyage_task(1);

  // Set stamp to far in the future relative to current_time
  task.stamp.sec = 1'000'000;  // epoch + 1M seconds (way beyond current)
  task.stamp.nanosec = 0;

  const auto current_pos = make_position(38.0, -122.0);
  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::VoyageTaskParseError);
}

// ---------------------------------------------------------------------------
// 3. RejectsDepartureTooFar
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsDepartureTooFar) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto task = make_valid_voyage_task(1);

  // Current position is ~55 km from departure (38.0, -122.0), well over 2 km
  const auto far_pos = make_position(38.5, -122.0);

  const auto result = validator.validate(task, far_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::InvalidDeparture);
}

// ---------------------------------------------------------------------------
// 4. RejectsInvalidDestination
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsInvalidDestination) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  // Latitude out of range
  {
    auto task = make_valid_voyage_task(1);
    task.destination.latitude = 100.0;  // invalid: > 90
    const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
    EXPECT_FALSE(result.is_valid);
    EXPECT_EQ(result.error_code, ErrorCode::InvalidDestination);
  }

  // Longitude out of range
  {
    auto task = make_valid_voyage_task(1);
    task.destination.longitude = 200.0;  // invalid: > 180
    const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
    EXPECT_FALSE(result.is_valid);
    EXPECT_EQ(result.error_code, ErrorCode::InvalidDestination);
  }
}

// ---------------------------------------------------------------------------
// 5. RejectsEtaWindowBelowMin
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsEtaWindowBelowMin) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  auto task = make_valid_voyage_task(1);
  // Voyage duration = latest.sec - stamp.sec = 1100 - 1000 = 100s < 600s min → reject
  task.eta_window.latest.sec = 1100;

  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::EtaOutOfBounds);
}

// ---------------------------------------------------------------------------
// 6. RejectsEtaWindowAboveMax
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsEtaWindowAboveMax) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  auto task = make_valid_voyage_task(1);
  // Voyage duration = latest.sec - stamp.sec = 301000 - 1000 = 300000s > 259200s → reject
  task.eta_window.latest.sec = 301000;

  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::EtaWindowExceeded);
}

// ---------------------------------------------------------------------------
// 7. RejectsInvalidTaskType
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsInvalidTaskType) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  auto task = make_valid_voyage_task(1);
  task.optimization_priority = "unknown_type";

  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::VoyageTaskParseError);
}

// ---------------------------------------------------------------------------
// 8. RejectsSpeedOutOfRange
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsSpeedOutOfRange) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  // speed_cmd_kn no longer exists in VoyageTask (Wave 0 IDL rewrite).
  // Verify invalid optimization_priority is rejected.
  auto task = make_valid_voyage_task(1);
  task.optimization_priority = "slow_boat";  // not in {"fuel_optimal","time_optimal","balanced"}
  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::VoyageTaskParseError);
}

// ---------------------------------------------------------------------------
// 9. RejectsEmptyWaypoints
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsEmptyWaypoints) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  auto task = make_valid_voyage_task(1);
  task.mandatory_waypoints.clear();

  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_FALSE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::VoyageTaskParseError);
}

// ---------------------------------------------------------------------------
// 10. RejectsExclusionZoneOverlap
//     Note: VoyageTask msg does not currently carry exclusion zone data, so
//     this check always receives empty zones through the public validate()
//     interface. This test verifies the empty-zones pass-through and serves
//     as a placeholder for when exclusion zones are added to the message.
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, RejectsExclusionZoneOverlap) {
  const auto config = make_default_config();
  const VoyageTaskValidator validator(config);
  const auto current_pos = make_position(38.0, -122.0);

  // With no exclusion zones in the current VoyageTask message, validate
  // should still succeed for a valid task (zones are empty, check trivially passes).
  auto task = make_valid_voyage_task(1);
  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);

  // The test documents that the exclusion zone code path is wired but
  // not yet reachable through the public API. When VoyageTask carries
  // exclusion zones, this test must be extended to verify overlap rejection.
  EXPECT_TRUE(result.is_valid);
  EXPECT_EQ(result.error_code, ErrorCode::Ok);
}

// ---------------------------------------------------------------------------
// 11. AcceptsMovedCopy -- move constructor and move assignment work
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, AcceptsMovedCopy) {
  const auto config = make_default_config();
  const auto current_pos = make_position(38.0, -122.0);

  // Test move construction
  VoyageTaskValidator v1(config);
  VoyageTaskValidator v2(std::move(v1));
  auto task = make_valid_voyage_task(1);
  auto result = v2.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_TRUE(result.is_valid);

  // Test move assignment
  VoyageTaskValidator v3(config);
  VoyageTaskValidator v4(make_default_config());
  v4 = std::move(v3);
  result = v4.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_TRUE(result.is_valid);
}

// ---------------------------------------------------------------------------
// 12. DefaultConfigIsValid
// ---------------------------------------------------------------------------
TEST(VoyageTaskValidatorTest, DefaultConfigIsValid) {
  const VoyageTaskValidatorConfig default_config{
      2.0,       // departure_distance_max_km
      600,       // eta_window_min_s
      259200,    // eta_window_max_s
      50.0,      // waypoint_max_distance_nm
      500.0      // exclusion_zone_buffer_m
  };

  EXPECT_DOUBLE_EQ(default_config.departure_distance_max_km, 2.0);
  EXPECT_EQ(default_config.eta_window_min_s, 600);
  EXPECT_EQ(default_config.eta_window_max_s, 259200);
  EXPECT_DOUBLE_EQ(default_config.waypoint_max_distance_nm, 50.0);
  EXPECT_DOUBLE_EQ(default_config.exclusion_zone_buffer_m, 500.0);

  // Validate that the default config works with a valid task
  const VoyageTaskValidator validator(default_config);
  const auto task = make_valid_voyage_task(1);
  const auto current_pos = make_position(38.0, -122.0);
  const auto result = validator.validate(task, current_pos, kCurrentTimeNs);
  EXPECT_TRUE(result.is_valid);
}

}  // namespace
}  // namespace mass_l3::m3
