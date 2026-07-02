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

// ---------------------------------------------------------------------------
// Slice W1 (spec §5.2): TailBuilder active-phase two-phase semantics.
// Active encounter (!past_clear) must generate a hold-only tail extending the
// NLP terminal state to the predicted s_clear, NOT reject m6_not_past_clear.
// Rejoin is deferred until release.
// ---------------------------------------------------------------------------

namespace {

bool segment_has_label(const mass_l3::m5::tail_builder::TailSegment& segment, std::uint8_t label)
{
  return std::find(segment.source_labels.begin(), segment.source_labels.end(), label) !=
      segment.source_labels.end();
}

}  // namespace

TEST(TailBuilderActive, generatesHoldOnlyWithoutRejoin)
{
  // Active encounter (!past_clear), release predicted, valid tcpa_s → TailBuilder
  // must produce a TERMINAL_HOLD segment with NO REJOIN_TO_L2 (spec §5.2).
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = false;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Active);
  inputs.m6_release_predicted = true;
  inputs.targets.front().tcpa_s = 180.0;  // valid, positive

  const auto result = TailBuilder::build(inputs);
  ASSERT_TRUE(result.hold_then_rejoin.has_value()) << result.reject_reason;
  const auto& segment = result.hold_then_rejoin.value();

  EXPECT_TRUE(segment_has_label(segment, l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD));
  EXPECT_FALSE(segment_has_label(segment, l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2));
  EXPECT_TRUE(result.reject_reason.empty());
  ASSERT_GE(segment.waypoints.size(), 2U);
  // Hold stays on the protected (STBD, y>0) side — no early rejoin.
  for (const auto& wp : segment.waypoints) {
    EXPECT_GT(wp.y_m, 0.0);
  }
}

TEST(TailBuilderActive, activeHoldExtendsToPredictedSClear)
{
  // s_clear advance = own_u_hold · max(tcpa_s, T_min_dwell=30).
  // Fixture: uN=5 m/s, tcpa_s=180s → advance=900m; pN projected at s=600.
  // Hold must reach ~s=1500 along the route (x-axis).
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = false;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Active);
  inputs.m6_release_predicted = true;
  inputs.targets.front().tcpa_s = 180.0;

  const auto result = TailBuilder::build(inputs);
  ASSERT_TRUE(result.hold_then_rejoin.has_value()) << result.reject_reason;
  const auto& segment = result.hold_then_rejoin.value();

  // First hold waypoint near pN (s≈600), last near predicted s_clear (s≈1500).
  EXPECT_NEAR(segment.waypoints.front().x_m, 600.0, 75.0);
  EXPECT_NEAR(segment.waypoints.back().x_m, 1500.0, 75.0);
}

TEST(TailBuilderActive, relaxesCpaReleaseGateInActivePhase)
{
  // Active phase: target inside CPA release floor is EXPECTED (target still
  // near CPA). cpa_release_floor / ship_domain_floor are release criteria and
  // must NOT reject an active hold-only candidate (spec §5.2).
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = false;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Active);
  inputs.m6_release_predicted = true;
  inputs.targets.front().tcpa_s = 180.0;
  // Force cpa_release_floor_clear() == false: cpa - 3*sigma < cpa_release.
  inputs.targets.front().cpa_m = 1900.0;       // 1900 - 150 = 1750 < 1800
  inputs.targets.front().cpa_sigma_m = 50.0;
  inputs.cpa_release_m = 1800.0;

  const auto result = TailBuilder::build(inputs);
  ASSERT_TRUE(result.hold_then_rejoin.has_value()) << result.reject_reason;
  EXPECT_NE(result.reject_reason, "cpa_release_floor");
  EXPECT_NE(result.reject_reason, "ship_domain_floor");
}

TEST(TailBuilderActive, rejectsWhenSClearUnavailable)
{
  // No predicted release AND tcpa_s invalid (<=0) → cannot extrapolate s_clear.
  // Honest degradation (spec §14.3): reject, do NOT fake a hold-to-horizon tail.
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = false;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Active);
  inputs.m6_release_predicted = false;
  inputs.targets.front().tcpa_s = -1.0;  // invalid

  const auto result = TailBuilder::build(inputs);
  EXPECT_FALSE(result.hold_then_rejoin.has_value());
  EXPECT_EQ(result.reject_reason, "active_s_clear_unavailable");
}

TEST(TailBuilderActive, releasePhaseAppendsRejoin)
{
  // Release (past_clear || state∈{Release,Clear}) → hold + rejoin (no regression).
  auto inputs = give_way_starboard_fixture();
  inputs.m6_past_clear = true;
  inputs.m6_encounter_state = static_cast<std::uint8_t>(EncounterState::Release);

  const auto result = TailBuilder::build(inputs);
  ASSERT_TRUE(result.hold_then_rejoin.has_value()) << result.reject_reason;
  const auto& segment = result.hold_then_rejoin.value();
  EXPECT_TRUE(segment_has_label(segment, l3_msgs::msg::AvoidancePlan::MID_MPC_TERMINAL_HOLD));
  EXPECT_TRUE(segment_has_label(segment, l3_msgs::msg::AvoidancePlan::REJOIN_TO_L2));
  // Rejoin terminal waypoint returns to the route (l≈0).
  EXPECT_NEAR(segment.waypoints.back().y_m, 0.0, 1.0e-3);
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
