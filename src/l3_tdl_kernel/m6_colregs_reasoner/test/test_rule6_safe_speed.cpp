#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule6_safe_speed.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule6_SafeSpeedTest, SpeedWithinLimitsNotActive) {
  Rule6_SafeSpeed rule;
  TargetGeometricState geo{};
  geo.ownship_speed_kn = 8.0;
  geo.bearing_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule6_SafeSpeedTest, SpeedExceedsOpenWaterLimit) {
  Rule6_SafeSpeed rule;
  TargetGeometricState geo{};
  geo.ownship_speed_kn = 25.0;
  geo.bearing_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.preferred_direction, "REDUCE_SPEED");
}

TEST(Rule6_SafeSpeedTest, RestrictedVisibilityLowerLimit) {
  Rule6_SafeSpeed rule;
  TargetGeometricState geo{};
  geo.ownship_speed_kn = 12.0;  // Moderate speed — should trigger in ODD-D (max safe = 10)
  geo.bearing_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.preferred_direction, "REDUCE_SPEED");
}

TEST(Rule6_SafeSpeedTest, ODDUnknownConservative) {
  Rule6_SafeSpeed rule;
  TargetGeometricState geo{};
  geo.ownship_speed_kn = 20.0;  // Above conservative limit (20*0.8=16)
  geo.bearing_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_UNKNOWN, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule6_SafeSpeedTest, LowSpeedAlwaysSafe) {
  Rule6_SafeSpeed rule;
  TargetGeometricState geo{};
  geo.ownship_speed_kn = 3.0;
  geo.bearing_deg = 0.0;

  RuleParameters params{};
  for (auto odd : {OddDomain::ODD_A, OddDomain::ODD_B, OddDomain::ODD_C, OddDomain::ODD_D}) {
    auto result = rule.evaluate(geo, odd, params);
    EXPECT_FALSE(result.is_active) << "Should be safe at 3 kn for domain " << static_cast<int>(odd);
  }
}

TEST(Rule6_SafeSpeedTest, CorrectRuleId) {
  Rule6_SafeSpeed rule;
  EXPECT_EQ(rule.rule_id(), 6);
  EXPECT_EQ(rule.rule_name(), "Rule6_SafeSpeed");
}

}  // namespace mass_l3::m6_colregs::rules::colregs
