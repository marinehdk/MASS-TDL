/// D2.2 Track D — MUST-1 OVERTAKING sector boundary unit tests.

#include <gtest/gtest.h>

#include <memory>

#include <l3_msgs/msg/encounter_classification.hpp>

#include "m2_world_model/encounter_classifier.hpp"

namespace mass_l3::m2 {
namespace {

EncounterClassifier::Config overtaking_sector_config() {
  return {112.5, 247.5, 6.0, 1.0, 926.0};
}

}  // namespace

TEST(Must1OvertakingSectorTest, Boundary112_5) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(112.5, 0.0, 0.0, 10.0, 500.0);
  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 112.5° (lower bound) should be OVERTAKING";
}

TEST(Must1OvertakingSectorTest, Boundary112_4) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(112.4, 0.0, 0.0, 10.0, 500.0);
  EXPECT_NE(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 112.4° (just below lower bound) should NOT be OVERTAKING";
}

TEST(Must1OvertakingSectorTest, Boundary247_5) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(247.5, 0.0, 0.0, 10.0, 500.0);
  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 247.5° (upper bound) should be OVERTAKING";
}

TEST(Must1OvertakingSectorTest, Boundary247_6) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(247.6, 0.0, 0.0, 10.0, 500.0);
  EXPECT_NE(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 247.6° (just above upper bound) should NOT be OVERTAKING";
}

TEST(Must1OvertakingSectorTest, Center180) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(180.0, 0.0, 0.0, 10.0, 500.0);
  EXPECT_EQ(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 180° (dead astern) should be OVERTAKING";
}

TEST(Must1OvertakingSectorTest, Crossing0Deg) {
  auto ec = std::make_shared<EncounterClassifier>(overtaking_sector_config());
  auto result = ec->classify(0.0, 0.0, 0.0, 10.0, 500.0);
  EXPECT_NE(result.encounter_type,
            l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKING)
      << "Bearing 0° (dead ahead) should NOT be OVERTAKING";
}

}  // namespace mass_l3::m2
