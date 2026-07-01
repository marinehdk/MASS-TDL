#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <string>

#include "m5_tactical_planner/gnc_preflight.hpp"
#include "m5_tactical_planner/tail_builder/tail_builder.hpp"

using mass_l3::m5::gnc_preflight::GncPreflightInput;
using mass_l3::m5::gnc_preflight::GncPreflightProtection;
using mass_l3::m5::gnc_preflight::GncPreflightRoute;
using mass_l3::m5::gnc_preflight::validate;
using mass_l3::m5::tail_builder::ColregSide;
using mass_l3::m5::tail_builder::GncExecutionOdd;

namespace {

constexpr std::uint8_t kMidMpcSource = 1U;

GncExecutionOdd odd()
{
  GncExecutionOdd result;
  result.ship_length_m = 50.0;
  result.max_lateral_offset_m = 400.0;
  result.min_segment_length_m = 50.0;
  result.min_turn_radius_m = 120.0;
  result.max_yaw_rate_rad_s = 0.04;
  result.max_lateral_accel_mps2 = 0.25;
  result.max_decel_mps2 = 0.20;
  result.min_first_changed_distance_m = 100.0;
  result.min_update_interval_s = 1.0;
  return result;
}

GncPreflightRoute route()
{
  GncPreflightRoute result;
  result.latitude_deg = {30.0000, 30.0010, 30.0020, 30.0030};
  result.longitude_deg = {122.0000, 122.0000, 122.0000, 122.0000};
  result.x_m = {0.0, 120.0, 240.0, 360.0};
  result.y_m = {0.0, 0.0, 0.0, 0.0};
  result.speed_mps = {3.0, 3.0, 3.0, 3.0};
  result.segment_source = {kMidMpcSource, kMidMpcSource, kMidMpcSource, kMidMpcSource};
  return result;
}

GncPreflightInput input()
{
  GncPreflightInput result;
  result.route = route();
  result.previous_route = route();
  result.odd = odd();
  result.update_interval_s = 1.5;
  result.current_x_m = 0.0;
  result.current_y_m = 0.0;
  return result;
}

void expect_rejects_with_reason(const GncPreflightInput& request, const std::string& reason)
{
  const auto result = validate(request);
  EXPECT_FALSE(result.accepted);
  EXPECT_NE(result.reject_reason.find(reason), std::string::npos) << result.reject_reason;
}

}  // namespace

TEST(GncPreflight, accepts_valid_candidate)
{
  const auto result = validate(input());

  EXPECT_TRUE(result.accepted);
  EXPECT_TRUE(result.reject_reason.empty());
}

TEST(GncPreflight, rejects_min_waypoint_count)
{
  auto request = input();
  request.route.latitude_deg = {30.0};
  request.route.longitude_deg = {122.0};
  request.route.x_m = {0.0};
  request.route.y_m = {0.0};
  request.route.speed_mps = {3.0};
  request.route.segment_source = {kMidMpcSource};

  expect_rejects_with_reason(request, "min_waypoint_count");
}

TEST(GncPreflight, rejects_equal_length_violation)
{
  auto request = input();
  request.route.longitude_deg.pop_back();

  expect_rejects_with_reason(request, "equal_length");
}

TEST(GncPreflight, rejects_nonfinite_wgs84)
{
  auto request = input();
  request.route.latitude_deg[1] = std::numeric_limits<double>::quiet_NaN();

  expect_rejects_with_reason(request, "finite_wgs84");
}

TEST(GncPreflight, rejects_wgs84_out_of_range)
{
  auto request = input();
  request.route.longitude_deg[1] = 181.0;

  expect_rejects_with_reason(request, "finite_wgs84");
}

TEST(GncPreflight, rejects_too_short_segment)
{
  auto request = input();
  request.route.x_m[1] = 20.0;

  expect_rejects_with_reason(request, "min_segment_length");
}

TEST(GncPreflight, rejects_reverse_segment)
{
  auto request = input();
  request.route.x_m = {0.0, 120.0, 60.0, 180.0};

  expect_rejects_with_reason(request, "reverse_segment");
}

TEST(GncPreflight, rejects_turn_radius_violation)
{
  auto request = input();
  request.route.x_m = {0.0, 120.0, 120.0, 240.0};
  request.route.y_m = {0.0, 0.0, 120.0, 120.0};

  expect_rejects_with_reason(request, "turn_radius");
}

TEST(GncPreflight, rejects_yaw_rate_violation)
{
  auto request = input();
  request.route.x_m = {0.0, 120.0, 240.0, 360.0};
  request.route.y_m = {0.0, 25.0, 0.0, 0.0};
  request.route.speed_mps = {10.0, 10.0, 10.0, 10.0};
  request.odd.min_turn_radius_m = 20.0;
  request.odd.max_lateral_accel_mps2 = 100.0;
  request.odd.max_yaw_rate_rad_s = 0.02;

  expect_rejects_with_reason(request, "yaw_rate");
}

TEST(GncPreflight, rejects_lateral_accel_violation)
{
  auto request = input();
  request.route.x_m = {0.0, 120.0, 240.0, 360.0};
  request.route.y_m = {0.0, 25.0, 0.0, 0.0};
  request.route.speed_mps = {10.0, 10.0, 10.0, 10.0};
  request.odd.min_turn_radius_m = 20.0;
  request.odd.max_yaw_rate_rad_s = 100.0;
  request.odd.max_lateral_accel_mps2 = 0.25;

  expect_rejects_with_reason(request, "lateral_accel");
}

TEST(GncPreflight, rejects_deceleration_violation)
{
  auto request = input();
  request.route.speed_mps = {8.0, 1.0, 1.0, 1.0};

  expect_rejects_with_reason(request, "decel");
}

TEST(GncPreflight, rejects_first_changed_point_too_near)
{
  auto request = input();
  request.route.y_m[0] = 20.0;

  expect_rejects_with_reason(request, "first_changed_distance");
}

TEST(GncPreflight, rejects_lateral_delta_too_large)
{
  auto request = input();
  request.route.y_m[2] = request.odd.max_lateral_offset_m + 1.0;
  request.route.y_m[3] = request.odd.max_lateral_offset_m + 1.0;

  expect_rejects_with_reason(request, "max_lateral_delta");
}

TEST(GncPreflight, rejects_min_update_interval_violation)
{
  auto request = input();
  request.update_interval_s = 0.5;

  expect_rejects_with_reason(request, "min_update_interval");
}

TEST(GncPreflight, rejects_malformed_protected_colreg_exception)
{
  auto request = input();
  request.route.y_m[1] = 20.0;
  request.protection = GncPreflightProtection{true, ColregSide::NONE, ""};

  expect_rejects_with_reason(request, "protected_colreg_exception");
}

TEST(GncPreflight, accepts_explicit_protected_colreg_exception_for_near_first_change)
{
  auto request = input();
  request.route.y_m[0] = 20.0;
  request.protection = GncPreflightProtection{true, ColregSide::STBD, "m6_onset_latch:target=17"};

  const auto result = validate(request);

  EXPECT_TRUE(result.accepted) << result.reject_reason;
}
