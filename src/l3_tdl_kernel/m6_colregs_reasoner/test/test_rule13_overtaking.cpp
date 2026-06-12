#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule13_OvertakingTest, TargetAsternSameCourseFasterActive) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.bearing_deg = 180.0;    // target directly astern, overtaking own ship
  geo.target_heading_deg = 0.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 7.0;
  geo.target_speed_kn = 14.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule13_OvertakingTest, TargetAheadNotOvertaking) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.bearing_deg = 0.0;      // directly ahead
  geo.target_heading_deg = 180.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 12.0;
  geo.target_speed_kn = 12.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule13_OvertakingTest, TargetAheadSameCourseSlowerIsOwnOvertakingGiveWay) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.bearing_deg = 2.8;  // target ahead, own ship coming up from target's stern
  geo.target_heading_deg = 0.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 14.0;
  geo.target_speed_kn = 7.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
  EXPECT_EQ(result.encounter_type, EncounterType::OVERTAKING);
  EXPECT_GE(result.min_alteration_deg, 65.0);
}

TEST(Rule13_OvertakingTest, BeingOvertakenStandOn) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.bearing_deg = 180.0;
  geo.target_heading_deg = 0.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 7.0;
  geo.target_speed_kn = 14.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::STAND_ON);
  EXPECT_EQ(result.encounter_type, EncounterType::OVERTAKING);
}

TEST(Rule13_OvertakingTest, Bearing135InOvertakingSector) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.bearing_deg = 135.0;    // 135 deg relative = overtaking sector boundary
  geo.target_heading_deg = 0.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 7.0;
  geo.target_speed_kn = 14.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule13_OvertakingTest, BearingNotInSector) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 6;
  geo.bearing_deg = 90.0;     // beam, not in overtaking sector
  geo.target_heading_deg = 0.0;
  geo.ownship_heading_deg = 0.0;
  geo.ownship_speed_kn = 12.0;
  geo.target_speed_kn = 12.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

// --- Non-zero heading test (verify absolute bearing fix) ---

TEST(Rule13_OvertakingTest, NonZeroHeading_InOvertakingSector) {
  // Own heading 090° (east), target at absolute bearing 270° (due west = directly astern).
  // rel_bearing = 270 - 90 = 180° → in overtaking sector [112.5, 247.5] → active
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 7;
  geo.bearing_deg = 270.0;         // absolute: target directly astern
  geo.target_heading_deg = 90.0;   // target heading same direction (east)
  geo.ownship_heading_deg = 90.0;
  geo.ownship_speed_kn = 7.0;
  geo.target_speed_kn = 14.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
}

TEST(Rule13_OvertakingTest, NonZeroHeading_NotInSector) {
  // Own heading 090° (east), target at absolute bearing 090° (directly ahead).
  // rel_bearing = 0° → NOT in overtaking sector
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 8;
  geo.bearing_deg = 90.0;          // absolute: target due east (ahead)
  geo.target_heading_deg = 270.0;
  geo.ownship_heading_deg = 90.0;
  geo.ownship_speed_kn = 14.0;
  geo.target_speed_kn = 7.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule13_OvertakingTest, NonZeroHeading_TargetAheadSameCourseSlowerIsGiveWay) {
  Rule13_Overtaking rule;
  TargetGeometricState geo{};
  geo.target_id = 9;
  geo.bearing_deg = 90.0;          // own heading east, target ahead
  geo.target_heading_deg = 90.0;
  geo.ownship_heading_deg = 90.0;
  geo.ownship_speed_kn = 14.0;
  geo.target_speed_kn = 7.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
  EXPECT_EQ(result.encounter_type, EncounterType::OVERTAKING);
  EXPECT_GE(result.min_alteration_deg, 65.0);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
