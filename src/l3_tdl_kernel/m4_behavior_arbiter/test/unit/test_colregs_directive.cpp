#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "m4_behavior_arbiter/colregs_directive.hpp"

namespace mass_l3::m4 {
namespace {

constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr std::uint8_t kRoleStandOn = 0U;

l3_msgs::msg::COLREGsConstraint make_msg(
    const std::string& direction,
    double min_deg,
    std::uint8_t primary_role = kRoleGiveWay,
    const std::string& phase = "PRESERVE_COURSE") {
  l3_msgs::msg::COLREGsConstraint msg;
  msg.schema_version = 114U;
  msg.conflict_detected = true;
  msg.phase = phase;
  msg.primary_role = primary_role;
  msg.primary_preferred_direction = direction;
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = min_deg;
  msg.constraints.push_back(c);
  return msg;
}

TEST(ColregsDirective, StarboardBuildsPositiveWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Starboard);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 30.0);

  const auto required = required_deviation_deg(directive, 3704.0);
  EXPECT_DOUBLE_EQ(required, 30.0);

  const auto window = directive_heading_window(0.0, directive, required);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 15.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 45.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, required);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 30.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 180.0);
}

TEST(ColregsDirective, PortBuildsNegativeWindowAndAllowedRange) {
  const auto directive = extract_colregs_directive(make_msg("PORT", 25.0));
  EXPECT_EQ(directive.direction, ColregsDirection::Port);

  const auto window = directive_heading_window(0.0, directive, 25.0);
  ASSERT_TRUE(window.has_value());
  EXPECT_DOUBLE_EQ(window->heading_min_deg, 320.0);
  EXPECT_DOUBLE_EQ(window->heading_max_deg, 350.0);

  const auto ranges = directive_allowed_ranges(0.0, directive, 25.0);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 180.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 335.0);
}

TEST(ColregsDirective, StarboardSplitWrappedAllowedRangeForLinearSolver) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  const auto ranges = directive_allowed_ranges(200.0, directive, 30.0);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 230.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 360.0);
  EXPECT_DOUBLE_EQ(ranges[1].first, 0.0);
  EXPECT_DOUBLE_EQ(ranges[1].second, 20.0);
}

TEST(ColregsDirective, PortSplitWrappedAllowedRangeForLinearSolver) {
  const auto directive = extract_colregs_directive(make_msg("PORT", 25.0));

  const auto ranges = directive_allowed_ranges(75.0, directive, 25.0);
  ASSERT_EQ(ranges.size(), 2u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 255.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 360.0);
  EXPECT_DOUBLE_EQ(ranges[1].first, 0.0);
  EXPECT_DOUBLE_EQ(ranges[1].second, 50.0);
}

TEST(ColregsDirective, StarboardKeepsDirectRangeWhenBothEndpointsWrap) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 150.0));

  const auto ranges = directive_allowed_ranges(230.0, directive, 150.0);
  ASSERT_EQ(ranges.size(), 1u);
  EXPECT_DOUBLE_EQ(ranges[0].first, 20.0);
  EXPECT_DOUBLE_EQ(ranges[0].second, 50.0);
}

TEST(ColregsDirective, NonQuarteringBowCapsDeviationAtSeventyFiveDegrees) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, false), 75.0);
}

TEST(ColregsDirective, QuarteringPreservesWideDeviationEnvelope) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 30.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, true), 150.0);
}

TEST(ColregsDirective, ExplicitHighMinimumAlterationOverridesBowCap) {
  const auto directive = extract_colregs_directive(make_msg("STARBOARD", 95.0));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, false), 95.0);
}

TEST(ColregsDirective, StandOnIndependentActionDoesNotUseQuarteringWideEnvelope) {
  const auto directive = extract_colregs_directive(
      make_msg("STARBOARD", 15.0, kRoleStandOn, "INDEPENDENT_ACTION"));

  EXPECT_DOUBLE_EQ(effective_colregs_max_deviation_deg(directive, true), 60.0);
}

TEST(ColregsDirective, ReduceSpeedAndHoldDoNotCreateHeadingWindow) {
  const auto reduce = extract_colregs_directive(make_msg("REDUCE_SPEED", 15.0));
  EXPECT_EQ(reduce.direction, ColregsDirection::ReduceSpeed);
  EXPECT_FALSE(directive_heading_window(90.0, reduce, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, reduce, 15.0).empty());

  const auto hold = extract_colregs_directive(make_msg("HOLD", 15.0));
  EXPECT_EQ(hold.direction, ColregsDirection::Hold);
  EXPECT_FALSE(directive_heading_window(90.0, hold, 15.0).has_value());
  EXPECT_TRUE(directive_allowed_ranges(90.0, hold, 15.0).empty());
}

TEST(ColregsDirective, NoConflictProducesInactiveHold) {
  auto msg = make_msg("STARBOARD", 30.0);
  msg.conflict_detected = false;

  const auto directive = extract_colregs_directive(msg);
  EXPECT_FALSE(directive.conflict_active);
  EXPECT_EQ(directive.direction, ColregsDirection::Hold);
  EXPECT_DOUBLE_EQ(directive.min_alteration_deg, 0.0);
}

}  // namespace
}  // namespace mass_l3::m4
