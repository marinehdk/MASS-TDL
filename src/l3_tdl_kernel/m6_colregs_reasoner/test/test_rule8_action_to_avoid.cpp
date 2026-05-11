#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule8_action_to_avoid.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule8_ActionToAvoidTest, ActiveWhenCPABelowThreshold) {
  Rule8_ActionToAvoid rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.cpa_m = 50.0;
  geo.tcpa_s = 100.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule8_ActionToAvoidTest, InactiveWhenCPASafe) {
  Rule8_ActionToAvoid rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.cpa_m = 1000.0;
  geo.tcpa_s = 500.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule8_ActionToAvoidTest, StarboardDirectionPreferred) {
  Rule8_ActionToAvoid rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.cpa_m = 30.0;
  geo.tcpa_s = 60.0;
  geo.bearing_deg = 45.0;   // starboard side
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule8_ActionToAvoidTest, UsesMinAlterationFromParams) {
  Rule8_ActionToAvoid rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.cpa_m = 50.0;
  geo.tcpa_s = 100.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 30.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_DOUBLE_EQ(result.min_alteration_deg, 30.0);
}

TEST(Rule8_ActionToAvoidTest, ValidForMultipleOddDomains) {
  Rule8_ActionToAvoid rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.cpa_m = 80.0;
  geo.tcpa_s = 90.0;
  geo.bearing_deg = 90.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  for (auto odd : {OddDomain::ODD_A, OddDomain::ODD_B, OddDomain::ODD_C, OddDomain::ODD_D}) {
    auto result = rule.evaluate(geo, odd, params);
    EXPECT_TRUE(result.is_active) << "Failed for domain " << static_cast<int>(odd);
  }
}

}  // namespace mass_l3::m6_colregs::rules::colregs
