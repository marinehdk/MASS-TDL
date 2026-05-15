#include <gtest/gtest.h>

#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4::test {

// Load valid YAML definition and verify 3 behaviours are registered.
TEST(BehaviorDictionaryTest, LoadValidYaml) {
  BehaviorDictionary dict;
  ASSERT_TRUE(dict.load(TEST_CONFIG_DIR "/behavior_definitions.yaml"));
  EXPECT_EQ(dict.size(), 3U);
}

// Find an existing behaviour type returns a valid descriptor with
// the expected name and priority_weight.
TEST(BehaviorDictionaryTest, FindExistingType) {
  BehaviorDictionary dict;
  ASSERT_TRUE(dict.load(TEST_CONFIG_DIR "/behavior_definitions.yaml"));

  const BehaviorDescriptor* desc = dict.find(BehaviorType::TRANSIT);
  ASSERT_NE(desc, nullptr);
  EXPECT_EQ(desc->type, BehaviorType::TRANSIT);
  EXPECT_EQ(desc->name, "Keep Lane / Route Following");
  EXPECT_DOUBLE_EQ(desc->priority_weight, 1.0);
}

// Querying a type that is not in the loaded dictionary returns nullptr.
TEST(BehaviorDictionaryTest, FindMissingTypeReturnsNull) {
  BehaviorDictionary dict;
  // Empty dictionary (no YAML loaded) — any find must return nullptr.
  const BehaviorDescriptor* desc = dict.find(BehaviorType::BERTH);
  EXPECT_EQ(desc, nullptr);
}

}  // namespace mass_l3::m4
