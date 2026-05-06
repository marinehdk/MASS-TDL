#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule16_give_way.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule16_GiveWayTest, ActiveWhenCPAClose) {
  Rule16_GiveWay rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.cpa_m = 50.0;
  geo.tcpa_s = 60.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
}

TEST(Rule16_GiveWayTest, InactiveWhenCPASafe) {
  Rule16_GiveWay rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.cpa_m = 2000.0;
  geo.tcpa_s = 600.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule16_GiveWayTest, PreferredDirectionSTARBOARD) {
  Rule16_GiveWay rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.cpa_m = 100.0;
  geo.tcpa_s = 90.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule16_GiveWayTest, UsesMinAlterationFromParams) {
  Rule16_GiveWay rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.cpa_m = 80.0;
  geo.tcpa_s = 60.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 25.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_DOUBLE_EQ(result.min_alteration_deg, 25.0);
}

TEST(Rule16_GiveWayTest, CorrectRuleId) {
  Rule16_GiveWay rule;
  EXPECT_EQ(rule.rule_id(), 16);
  EXPECT_EQ(rule.rule_name(), "Rule16_GiveWay");
}

TEST(Rule16_GiveWayTest, SoundWarningPhaseWhenActive) {
  Rule16_GiveWay rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.cpa_m = 100.0;
  geo.tcpa_s = 90.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.phase, TimingPhase::SOUND_WARNING);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
