#include <gtest/gtest.h>
#include "m6_colregs_reasoner/rules/colregs/rule15_crossing.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

TEST(Rule15_CrossingTest, StarboardCrossingGiveWay) {
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 1;
  geo.bearing_deg = 45.0;     // starboard side
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
  EXPECT_EQ(result.encounter_type, EncounterType::CROSSING);
}

TEST(Rule15_CrossingTest, PortCrossingStandOn) {
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 2;
  geo.bearing_deg = 270.0;    // port side (relative bearing 270)
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::STAND_ON);
  EXPECT_EQ(result.encounter_type, EncounterType::CROSSING);
}

TEST(Rule15_CrossingTest, AheadNotCrossing) {
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 3;
  geo.bearing_deg = 0.0;      // directly ahead, not crossing
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule15_CrossingTest, StarboardBoundary010) {
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 4;
  geo.bearing_deg = 10.0;     // ahead-starboard, but below 22.5
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule15_CrossingTest, StarboardBoundaryAbove112p5) {
  // bearing 113° is just above the 112.5° upper bound → NOT in crossing sector
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 5;
  geo.bearing_deg = 113.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_FALSE(result.is_active);
}

TEST(Rule15_CrossingTest, PreferredDirectionStarboard) {
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 6;
  geo.bearing_deg = 60.0;
  geo.ownship_heading_deg = 0.0;

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_EQ(result.preferred_direction, "STARBOARD");
}

// --- Non-zero heading tests (verify bearing_deg is absolute) ---

TEST(Rule15_CrossingTest, NonZeroHeading_StarboardGiveWay) {
  // Own heading 090°, target at absolute bearing 180° (due south).
  // rel_bearing = 180 - 90 = 90° → in starboard crossing sector [22.5, 112.5] → give-way
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 7;
  geo.bearing_deg = 180.0;        // absolute: south
  geo.ownship_heading_deg = 90.0; // heading east

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::GIVE_WAY);
}

TEST(Rule15_CrossingTest, NonZeroHeading_PortStandOn) {
  // Own heading 090°, target at absolute bearing 000° (due north).
  // rel_bearing = 0 - 90 = -90 → 270° → in port crossing sector [247.5, 337.5] → stand-on
  Rule15_Crossing rule;
  TargetGeometricState geo{};
  geo.target_id = 8;
  geo.bearing_deg = 0.0;          // absolute: north
  geo.ownship_heading_deg = 90.0; // heading east

  RuleParameters params{};
  params.min_alteration_deg = 15.0;

  auto result = rule.evaluate(geo, OddDomain::ODD_A, params);
  EXPECT_TRUE(result.is_active);
  EXPECT_EQ(result.role, Role::STAND_ON);
}

}  // namespace mass_l3::m6_colregs::rules::colregs
