#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule5_lookout.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule5_LookoutTest, AlwaysActive) {
  Rule5_Lookout rule;
  TargetGeometricState geo{};
  geo.target_id = 101;
  geo.bearing_deg = 45.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.rule_id, 5);
}

TEST(Rule5_LookoutTest, NoPreferredDirection) {
  Rule5_Lookout rule;
  TargetGeometricState geo{};
  geo.target_id = 102;
  geo.bearing_deg = 90.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_B, params);
  EXPECT_EQ(result.preferred_direction, "HOLD");
  EXPECT_EQ(result.min_alteration_deg, 0.0);
}

TEST(Rule5_LookoutTest, LowConfidence) {
  Rule5_Lookout rule;
  TargetGeometricState geo{};
  geo.target_id = 103;
  geo.bearing_deg = 180.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_C, params);
  EXPECT_LE(result.confidence, 0.3f);
}

TEST(Rule5_LookoutTest, ActiveAcrossAllOddDomains) {
  Rule5_Lookout rule;
  RuleParameters params{};
  TargetGeometricState geo{};
  geo.target_id = 104;

  for (auto odd : {OddDomain::ODD_A, OddDomain::ODD_B, OddDomain::ODD_C, OddDomain::ODD_D}) {
    auto result = rule.evaluate(geo, odd, params);
    EXPECT_TRUE(result.is_active) << "Failed for ODD domain " << static_cast<int>(odd);
  }
}

TEST(Rule5_LookoutTest, FreeRole) {
  Rule5_Lookout rule;
  TargetGeometricState geo{};
  geo.target_id = 105;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.role, Role::FREE);
  EXPECT_EQ(result.encounter_type, EncounterType::NONE);
}

TEST(Rule5_LookoutTest, IncludesTargetIdInRationale) {
  Rule5_Lookout rule;
  TargetGeometricState geo{};
  geo.target_id = 999;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.rationale.find("999") != std::string::npos);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
