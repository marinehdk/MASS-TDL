#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule14_head_on.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

// Helper: standard head-on geometry with zero ownship heading.
// Target at absolute bearing 0° (dead ahead), heading 180° (reciprocal).
static TargetGeometricState make_head_on_geo(uint64_t id = 1) {
  TargetGeometricState geo{};
  geo.target_id = id;
  geo.bearing_deg = 0.0;          // absolute: target directly ahead
  geo.target_heading_deg = 180.0; // target heading toward us
  geo.ownship_heading_deg = 0.0;
  return geo;
}

TEST(Rule14_HeadOnTest, ReciprocalCoursesActive) {
  Rule14_HeadOn rule;
  auto geo = make_head_on_geo(1);
  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule14_HeadOnTest, BothGiveWayRole) {
  Rule14_HeadOn rule;
  auto geo = make_head_on_geo(2);
  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(result.encounter_type, EncounterType::HEAD_ON);
}

TEST(Rule14_HeadOnTest, StarboardPreferredDirection) {
  Rule14_HeadOn rule;
  auto geo = make_head_on_geo(3);
  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule14_HeadOnTest, NotHeadOnWhenTargetOnBeam) {
  // Target on starboard beam — not head-on, should trigger Rule 15 (crossing)
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.bearing_deg = 90.0;         // absolute: target on starboard beam
  geo.target_heading_deg = 90.0;  // target heading perpendicular, not reciprocal
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule14_HeadOnTest, NearlyHeadOn_HighConfidence) {
  // 1° off dead ahead, 179° off reciprocal → still head-on
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.bearing_deg = 1.0;           // absolute: nearly dead ahead
  geo.target_heading_deg = 181.0;  // nearly reciprocal
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_GE(result.confidence, 0.8f);
}

// --- Non-zero heading tests (verify C1+C2 fix: bearing_deg is absolute) ---

TEST(Rule14_HeadOnTest, NonZeroHeading_HeadOn) {
  // Own heading 045°, target at absolute bearing 045° (directly ahead),
  // target heading 225° (reciprocal) → should be head-on
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 6;
  geo.bearing_deg = 45.0;          // absolute bearing: target NE, directly ahead
  geo.target_heading_deg = 225.0;  // target heading SW, toward us
  geo.ownship_heading_deg = 45.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::BOTH_GIVE_WAY);
}

TEST(Rule14_HeadOnTest, NonZeroHeading_NotHeadOn) {
  // Own heading 090°, target at absolute bearing 180° (due south),
  // target heading 000° → not head-on (target is on our port beam)
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 7;
  geo.bearing_deg = 180.0;         // absolute: target due south
  geo.target_heading_deg = 0.0;    // target heading north
  geo.ownship_heading_deg = 90.0;  // own heading east

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  // rel_bearing = 180 - 90 = 90 → not dead ahead → not head-on
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
