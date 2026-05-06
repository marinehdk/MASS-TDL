#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule14_head_on.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule14_HeadOnTest, ReciprocalCoursesActive) {
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.bearing_deg = 180.0;    // target bearing 180, own heading 0
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule14_HeadOnTest, BothGiveWayRole) {
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.bearing_deg = 180.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.role, Role::BOTH_GIVE_WAY);
  EXPECT_EQ(result.encounter_type, EncounterType::HEAD_ON);
}

TEST(Rule14_HeadOnTest, StarboardPreferredDirection) {
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.bearing_deg = 180.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule14_HeadOnTest, NotHeadOnWhenBearingDiffers) {
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.bearing_deg = 90.0;     // beam, not head-on
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule14_HeadOnTest, HighConfidence) {
  Rule14_HeadOn rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.bearing_deg = 179.0;    // nearly reciprocal
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_GE(result.confidence, 0.8f);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
