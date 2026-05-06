#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule18_responsibilities.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule18_ResponsibilitiesTest, LowerPriorityGivesWay) {
  Rule18_Responsibilities rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.target_ship_type_priority = 3;  // higher priority (more restricted) than own (5)

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
}

TEST(Rule18_ResponsibilitiesTest, SamePriorityNotActive) {
  Rule18_Responsibilities rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.target_ship_type_priority = 5;  // same as own default

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule18_ResponsibilitiesTest, TargetLessPriorityOwnStandOn) {
  Rule18_Responsibilities rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.target_ship_type_priority = 6;  // less restricted (higher number) than own

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::STAND_ON);
}

TEST(Rule18_ResponsibilitiesTest, NUCTopPriority) {
  Rule18_Responsibilities rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.target_ship_type_priority = 0;  // NUC — highest priority

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
}

TEST(Rule18_ResponsibilitiesTest, StarboardDirectionWhenGiveWay) {
  Rule18_Responsibilities rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.target_ship_type_priority = 0;  // NUC

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

}  // namespace mass_l3::m6_colregs::rules::colregs
