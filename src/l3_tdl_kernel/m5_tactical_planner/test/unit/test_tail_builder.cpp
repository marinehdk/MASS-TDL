#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "m5_tactical_planner/tail_builder/tail_builder.hpp"

using mass_l3::m5::tail_builder::ColregRole;
using mass_l3::m5::tail_builder::ColregSide;
using mass_l3::m5::tail_builder::EncounterState;
using mass_l3::m5::tail_builder::GeoWP;
using mass_l3::m5::tail_builder::GncExecutionOdd;
using mass_l3::m5::tail_builder::RouteFrame;
using mass_l3::m5::tail_builder::TailBuilder;
using mass_l3::m5::tail_builder::TailInputs;
using mass_l3::m5::tail_builder::TargetSnapshot;

namespace {

RouteFrame straight_route()
{
  RouteFrame route;
  route.waypoints = {
      GeoWP{0.0, 0.0, 5.0, "TRANSIT"},
      GeoWP{1000.0, 0.0, 5.0, "TRANSIT"},
      GeoWP{2000.0, 0.0, 5.0, "TRANSIT"},
      GeoWP{3000.0, 0.0, 5.0, "TRANSIT"}};
  return route;
}

GncExecutionOdd nominal_gnc()
{
  GncExecutionOdd odd;
  odd.ship_length_m = 50.0;
  odd.max_lateral_offset_m = 400.0;
  odd.min_segment_length_m = 50.0;
  odd.min_turn_radius_m = 120.0;
  odd.max_yaw_rate_rad_s = 0.04;
  odd.max_lateral_accel_mps2 = 0.25;
  return odd;
}

TailInputs give_way_starboard_fixture()
{
  TailInputs inputs;
  inputs.role = ColregRole::GiveWay;
  inputs.pN = GeoWP{600.0, 120.0, 5.0, "MID_MPC"};
  inputs.psiN_rad = 0.0;
  inputs.uN_mps = 5.0;
  inputs.protected_side = ColregSide::STBD;
  inputs.m6_past_clear = true;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Release);
  inputs.m6_release_predicted = true;
  inputs.route_frame = straight_route();
  inputs.targets = {TargetSnapshot{7, 2200.0, 180.0, 50.0}};
  inputs.cpa_release_m = 1800.0;
  inputs.cpa_safe_m = 1500.0;
  inputs.gnc_odd = nominal_gnc();
  return inputs;
}

}  // namespace

TEST(TailBuilder, give_way_starboard_hold_then_rejoin)
{
  const auto result = TailBuilder::build(give_way_starboard_fixture());

  ASSERT_TRUE(result.hold_then_rejoin.has_value()) << result.reject_reason;
  const auto& segment = result.hold_then_rejoin.value();
  ASSERT_EQ(segment.waypoints.size(), segment.source_labels.size());
  ASSERT_GE(segment.waypoints.size(), 3U);

  EXPECT_EQ(segment.source_labels.front(), l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD);
  EXPECT_EQ(segment.source_labels.back(), l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2);
  EXPECT_GT(segment.waypoints.front().y_m, 0.0);
  EXPECT_NEAR(segment.waypoints.back().y_m, 0.0, 1.0e-6);
  EXPECT_TRUE(result.reject_reason.empty());
}

TEST(TailBuilder, stand_on_returns_no_tail)
{
  auto inputs = give_way_starboard_fixture();
  inputs.role = ColregRole::StandOn;

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_TRUE(result.reject_reason.empty());
}

TEST(TailBuilder, s_clear_consumes_m6_past_clear_not_self_computed)
{
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = false;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Active);
  inputs.m6_release_predicted = false;
  inputs.targets.front().relative_bearing_deg = 175.0;  // tempting abaft value; must be ignored.

  const auto blocked = TailBuilder::build(inputs);
  EXPECT_FALSE(blocked.hold_then_rejoin.has_value());
  EXPECT_EQ(blocked.reject_reason, "m6_not_past_clear");

  inputs.m6_past_clear = true;
  const auto released = TailBuilder::build(inputs);

  ASSERT_TRUE(released.hold_then_rejoin.has_value()) << released.reject_reason;
  const auto first_rejoin = std::find(
      released.hold_then_rejoin->source_labels.begin(),
      released.hold_then_rejoin->source_labels.end(),
      l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2);
  ASSERT_NE(first_rejoin, released.hold_then_rejoin->source_labels.end());
  const auto index = static_cast<std::size_t>(
      std::distance(released.hold_then_rejoin->source_labels.begin(), first_rejoin));
  EXPECT_GT(released.hold_then_rejoin->waypoints[index].x_m, inputs.pN.x_m);
}

TEST(TailBuilder, rejects_when_l_hold_too_small)
{
  auto inputs = give_way_starboard_fixture();
  inputs.pN.y_m = 5.0;

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "l_hold_too_small");
}

TEST(TailBuilder, rejects_empty_m2_target_snapshot)
{
  auto inputs = give_way_starboard_fixture();
  inputs.targets.clear();

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "missing_m2_targets");
}

TEST(TailBuilder, rejects_target_below_cpa_release_floor)
{
  auto inputs = give_way_starboard_fixture();
  inputs.targets = {TargetSnapshot{7, 1900.0, 180.0, 50.0}};
  inputs.cpa_release_m = 1800.0;

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "cpa_release_floor");
}

TEST(TailBuilder, rejects_target_below_ship_domain_floor)
{
  auto inputs = give_way_starboard_fixture();
  inputs.targets = {TargetSnapshot{7, 2140.0, 180.0, 50.0}};
  inputs.cpa_release_m = 1800.0;
  inputs.cpa_safe_m = 2100.0;

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "ship_domain_floor");
}

TEST(TailBuilder, rejects_route_frame_with_sharp_corner)
{
  auto inputs = give_way_starboard_fixture();
  inputs.pN = GeoWP{900.0, 120.0, 5.0, "MID_MPC"};
  inputs.route_frame.waypoints = {
      GeoWP{0.0, 0.0, 5.0, "TRANSIT"},
      GeoWP{1000.0, 0.0, 5.0, "TRANSIT"},
      GeoWP{1000.0, 1000.0, 5.0, "TRANSIT"}};

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "route_frame_sharp_corner");
}

TEST(TailBuilder, rejects_unsafe_tail_waypoint_speed)
{
  auto inputs = give_way_starboard_fixture();
  inputs.uN_mps = std::numeric_limits<double>::infinity();

  const auto result = TailBuilder::build(inputs);

  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "tail_speed_invalid");
}
