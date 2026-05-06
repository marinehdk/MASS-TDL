#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule17_stand_on.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule17_StandOnTest, InactiveWhenCPAVerySafe) {
  Rule17_StandOn rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.cpa_m = 5000.0;
  geo.tcpa_s = 1000.0;
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule17_StandOnTest, PreserveCoursePhase) {
  Rule17_StandOn rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.cpa_m = 100.0;
  geo.tcpa_s = 400.0;       // > t_standOn_s
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.phase, TimingPhase::PRESERVE_COURSE);
  EXPECT_EQ(result.preferred_direction, "HOLD");
  EXPECT_EQ(result.role, Role::STAND_ON);
}

TEST(Rule17_StandOnTest, SoundWarningPhase) {
  Rule17_StandOn rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.cpa_m = 100.0;
  geo.tcpa_s = 200.0;       // between t_act_s and t_standOn_s
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.phase, TimingPhase::SOUND_WARNING);
}

TEST(Rule17_StandOnTest, IndependentActionPhase) {
  Rule17_StandOn rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.cpa_m = 100.0;
  geo.tcpa_s = 80.0;        // between t_emergency_s and t_act_s
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.phase, TimingPhase::INDEPENDENT_ACTION);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

TEST(Rule17_StandOnTest, CriticalActionPhase) {
  Rule17_StandOn rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.cpa_m = 50.0;
  geo.tcpa_s = 30.0;        // < t_emergency_s
  geo.bearing_deg = 45.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.cpa_safe_m = 200.0;
  params.t_standOn_s = 300.0;
  params.t_act_s = 120.0;
  params.t_emergency_s = 60.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.phase, TimingPhase::CRITICAL_ACTION);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
  EXPECT_GE(result.confidence, 0.85f);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
