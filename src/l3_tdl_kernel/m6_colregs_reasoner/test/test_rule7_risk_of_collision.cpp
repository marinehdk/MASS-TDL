#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule7_risk_of_collision.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule7_RiskOfCollisionTest, RiskDetectedByCPA) {
  Rule7_RiskOfCollision rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.cpa_m = 50.0;
  geo.tcpa_s = 120.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.rule_id, 7);
}

TEST(Rule7_RiskOfCollisionTest, NoRiskWhenCPASafe) {
  Rule7_RiskOfCollision rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.cpa_m = 1000.0;
  geo.tcpa_s = 500.0;
  geo.bearing_deg = 90.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule7_RiskOfCollisionTest, RiskDetectedByBearingTrend) {
  Rule7_RiskOfCollision rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.cpa_m = 500.0;   // CPA safe, but bearing risk may trigger
  geo.tcpa_s = 60.0;    // Close TCPA
  geo.bearing_deg = 0.0;  // Directly ahead (constant bearing)
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule7_RiskOfCollisionTest, ConfidenceHigherForTighterCPA) {
  Rule7_RiskOfCollision rule;
  RuleParameters params{};
  params.cpa_safe_m = 500.0;

  TargetGeometricState close_geo{};
  close_geo.target_id = 4;
  close_geo.cpa_m = 10.0;
  close_geo.tcpa_s = 30.0;
  close_geo.bearing_deg = 30.0;
  close_geo.ownship_heading_deg = 0.0;

  TargetGeometricState far_geo{};
  far_geo.target_id = 5;
  far_geo.cpa_m = 400.0;
  far_geo.tcpa_s = 120.0;
  far_geo.bearing_deg = 60.0;
  far_geo.ownship_heading_deg = 0.0;

  auto close_result = rule.evaluate(close_geo, OddDomain::ODD_A, params);
  auto far_result = rule.evaluate(far_geo, OddDomain::ODD_A, params);

  EXPECT_GT(close_result.confidence, far_result.confidence);
}

TEST(Rule7_RiskOfCollisionTest, NegativeTCPANoRisk) {
  Rule7_RiskOfCollision rule;
  TargetGeometricState geo{};
  geo.target_id = 6;
  geo.cpa_m = 1000.0;
  geo.tcpa_s = -1.0;   // negative TCPA — target moving away
  geo.bearing_deg = 90.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
