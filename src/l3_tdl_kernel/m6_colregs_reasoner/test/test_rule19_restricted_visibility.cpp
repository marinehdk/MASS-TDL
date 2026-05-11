#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule19_restricted_visibility.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule19_RestrictedVisibilityTest, ActiveInODDD) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.cpa_m = 500.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule19_RestrictedVisibilityTest, InactiveInNonODDD) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.cpa_m = 500.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  for (auto odd : {OddDomain::ODD_A, OddDomain::ODD_B, OddDomain::ODD_C, OddDomain::ODD_UNKNOWN}) {
    auto result = rule.evaluate(geo, odd, params);
    EXPECT_FALSE(result.is_active) << "Should be inactive for domain " << static_cast<int>(odd);
  }
}

TEST(Rule19_RestrictedVisibilityTest, RestrictedVisEncounterType) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.cpa_m = 500.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_EQ(result.encounter_type, EncounterType::RESTRICTED_VIS);
}

TEST(Rule19_RestrictedVisibilityTest, CPABelowThresholdTriggersEvasive) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.cpa_m = 50.0;          // well below reduced CPA threshold
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule19_RestrictedVisibilityTest, CPABeyondThresholdMonitor) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.cpa_m = 1000.0;        // well above reduced CPA threshold
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_EQ(result.preferred_direction, "REDUCE_SPEED");
  EXPECT_EQ(result.role, Role::FREE);
}

TEST(Rule19_RestrictedVisibilityTest, LargerAlterationInRestrictedVis) {
  Rule19_RestrictedVisibility rule;
  TargetGeometricState geo{};
  geo.target_id = 6;
  geo.cpa_m = 50.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_D, params);
  EXPECT_GT(result.min_alteration_deg, 15.0);  // scaled up for restricted vis
}

}  // namespace mass_l3::m6_colregs::rules::colregs
