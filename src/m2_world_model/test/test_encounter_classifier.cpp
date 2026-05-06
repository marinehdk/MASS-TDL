#include <gtest/gtest.h>

#include <cmath>

#include <l3_msgs/msg/encounter_classification.hpp>

#include "m2_world_model/encounter_classifier.hpp"

namespace mass_l3::m2 {
namespace {

// Default config per spec §5.1.3
EncounterClassifier::Config default_config() {
  return {112.5, 247.5, 6.0, 1.0, 926.0};
}

}  // namespace

// ──────────────────────────────────────────────
// Test 1: Head-on classification
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, HeadOnClassification) {
  EncounterClassifier ec(default_config());

  // Own ship heading 000°, target heading 180° (reciprocal)
  // Relative bearing ~0° (target directly ahead), moderate speed, close CPA
  auto result = ec.classify(5.0, 0.0, 180.0, 10.0, 100.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON);
  EXPECT_TRUE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 2: Overtaking — target in stern sector
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, OvertakingFromBehind) {
  EncounterClassifier ec(default_config());

  // Target behind own ship (bearing 180°, stern sector)
  auto result = ec.classify(180.0, 0.0, 0.0, 5.0, 500.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING);
  EXPECT_TRUE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 3: Crossing from port side
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, CrossingFromPort) {
  EncounterClassifier ec(default_config());

  // Target on port side (negative bearing), heading roughly across
  auto result = ec.classify(-45.0, 0.0, 90.0, 10.0, 200.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING);
  // Target on port side → own ship is stand-on
  EXPECT_FALSE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 4: Crossing from starboard side
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, CrossingFromStarboard) {
  EncounterClassifier ec(default_config());

  // Target on starboard side (positive bearing)
  auto result = ec.classify(45.0, 0.0, 270.0, 10.0, 200.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING);
  // Target on starboard → own ship is give-way
  EXPECT_TRUE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 5: Safe pass with large CPA → AMBIGUOUS
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, SafePassLargeCpa) {
  EncounterClassifier ec(default_config());

  // Low relative speed + CPA > 0.5 nm
  auto result = ec.classify(10.0, 0.0, 180.0, 0.5, 1000.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_AMBIGUOUS);
  EXPECT_FALSE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 6: Boundary values — bearing near stern sector edge
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, AmbiguousBoundary) {
  EncounterClassifier ec(default_config());

  // Bearing at 112.5° (lower bound of overtaking sector) → overtaking
  auto result_at_boundary = ec.classify(112.5, 0.0, 0.0, 5.0, 500.0);
  EXPECT_EQ(result_at_boundary.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING);

  // Bearing at 111.0° (just outside overtaking sector) → may be head-on or crossing
  // With heading diff = 10° (> 6°), it should be crossing
  auto result_outside = ec.classify(111.0, 0.0, 10.0, 5.0, 500.0);
  EXPECT_EQ(result_outside.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING);
}

// ──────────────────────────────────────────────
// Test 7: Overtaken by faster target
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, OvertakenByFaster) {
  EncounterClassifier ec(default_config());

  // Target in stern sector, heading same direction
  auto result = ec.classify(170.0, 0.0, 0.0, 8.0, 200.0);

  // Stern sector → overtaking (geometric pre-classification)
  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING);
  // Overtaking vessel is give-way
  EXPECT_TRUE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 8: Bearing normalisation (negative → 0-360 range)
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, BearingNormalization) {
  EncounterClassifier ec(default_config());

  // Bearing -10° (10° port side) should be normalised correctly
  auto result = ec.classify(-10.0, 0.0, 185.0, 10.0, 200.0);

  // Head-on check: heading difference = |0 - 185| → 175° → not head-on (tol=6°)
  // Bearing = -10° (350° in 0-360 range) → not in stern sector
  // → crossing
  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSING);
  EXPECT_NEAR(result.relative_bearing_deg, -10.0, 1e-9);
  EXPECT_FALSE(result.is_giveway);  // port side → stand-on
}

// ──────────────────────────────────────────────
// Test 9: Head-on bearing edge case (bearing 0°)
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, HeadOnBearingEdgeCase) {
  EncounterClassifier ec(default_config());

  // Target directly ahead, reciprocal course
  auto result = ec.classify(0.0, 45.0, 225.0, 10.0, 200.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON);
  EXPECT_TRUE(result.is_giveway);
}

// ──────────────────────────────────────────────
// Test 10: Low speed, no collision — safe pass
// ──────────────────────────────────────────────
TEST(EncounterClassifierTest, LowSpeedNoCollision) {
  EncounterClassifier ec(default_config());

  // Very slow relative speed, CPA far above safe pass threshold
  auto result = ec.classify(90.0, 0.0, 180.0, 0.3, 950.0);

  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_AMBIGUOUS);
  EXPECT_FALSE(result.is_giveway);
}

}  // namespace
}  // namespace mass_l3::m2
