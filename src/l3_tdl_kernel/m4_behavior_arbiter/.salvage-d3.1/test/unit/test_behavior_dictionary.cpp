#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

class BehaviorDictionaryTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fixture_yaml_path_ = std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "fixtures" / "behavior_definitions_default.yaml";
  }

  std::filesystem::path fixture_yaml_path_;
  BehaviorDictionary dict_;
};

// Test 1: OpenWaterAllowsTransitAndColregAvoid
TEST_F(BehaviorDictionaryTest, OpenWaterAllowsTransitAndColregAvoid) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));
  ASSERT_TRUE(dict_.is_loaded());

  auto subset = dict_.get_active_subset(0);  // ODD-A = 0

  bool found_transit = false;
  bool found_colreg = false;
  for (const auto& desc : subset) {
    if (desc.type == BehaviorType::Transit) {
      found_transit = true;
    }
    if (desc.type == BehaviorType::ColregAvoid) {
      found_colreg = true;
    }
  }

  EXPECT_TRUE(found_transit) << "Transit should be in ODD-A subset";
  EXPECT_TRUE(found_colreg) << "ColregAvoid should be in ODD-A subset";
}

// Test 2: OpenWaterExcludesBerth
TEST_F(BehaviorDictionaryTest, OpenWaterExcludesBerth) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(0);  // ODD-A = 0

  for (const auto& desc : subset) {
    EXPECT_NE(desc.type, BehaviorType::Berth) << "Berth should not be in ODD-A subset";
  }
}

// Test 3: NarrowChannelAllowsColregAvoid
TEST_F(BehaviorDictionaryTest, NarrowChannelAllowsColregAvoid) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(1);  // ODD-B = 1

  bool found_colreg = false;
  for (const auto& desc : subset) {
    if (desc.type == BehaviorType::ColregAvoid) {
      found_colreg = true;
    }
  }

  EXPECT_TRUE(found_colreg) << "ColregAvoid should be in ODD-B subset";
}

// Test 4: NarrowChannelExcludesDpHold
TEST_F(BehaviorDictionaryTest, NarrowChannelExcludesDpHold) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(1);  // ODD-B = 1

  for (const auto& desc : subset) {
    EXPECT_NE(desc.type, BehaviorType::DpHold) << "DpHold should not be in ODD-B subset";
  }
}

// Test 5: PortAllowsDpHoldAndBerth
TEST_F(BehaviorDictionaryTest, PortAllowsDpHoldAndBerth) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(2);  // ODD-C = 2

  bool found_dp = false;
  bool found_berth = false;
  for (const auto& desc : subset) {
    if (desc.type == BehaviorType::DpHold) {
      found_dp = true;
    }
    if (desc.type == BehaviorType::Berth) {
      found_berth = true;
    }
  }

  EXPECT_TRUE(found_dp) << "DpHold should be in ODD-C subset";
  EXPECT_TRUE(found_berth) << "Berth should be in ODD-C subset";
}

// Test 6: PortExcludesTransit
TEST_F(BehaviorDictionaryTest, PortExcludesTransit) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(2);  // ODD-C = 2

  for (const auto& desc : subset) {
    EXPECT_NE(desc.type, BehaviorType::Transit) << "Transit should not be in ODD-C subset";
  }
}

// Test 7: RestrictedVisAllowsColregAvoid
TEST_F(BehaviorDictionaryTest, RestrictedVisAllowsColregAvoid) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(3);  // ODD-D = 3

  bool found_colreg = false;
  for (const auto& desc : subset) {
    if (desc.type == BehaviorType::ColregAvoid) {
      found_colreg = true;
    }
  }

  EXPECT_TRUE(found_colreg) << "ColregAvoid should be in ODD-D subset";
}

// Test 8: RestrictedVisExcludesTransit
TEST_F(BehaviorDictionaryTest, RestrictedVisExcludesTransit) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset = dict_.get_active_subset(3);  // ODD-D = 3

  for (const auto& desc : subset) {
    EXPECT_NE(desc.type, BehaviorType::Transit) << "Transit should not be in ODD-D subset";
  }
}

// Test 9: MrcBehaviorsInAllZones
TEST_F(BehaviorDictionaryTest, MrcBehaviorsInAllZones) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  for (uint8_t zone = 0; zone < 4; ++zone) {
    auto subset = dict_.get_active_subset(zone);

    bool found_drift = false;
    bool found_anchor = false;
    bool found_heaveto = false;

    for (const auto& desc : subset) {
      if (desc.type == BehaviorType::MrcDrift) {
        found_drift = true;
      }
      if (desc.type == BehaviorType::MrcAnchor) {
        found_anchor = true;
      }
      if (desc.type == BehaviorType::MrcHeaveTo) {
        found_heaveto = true;
      }
    }

    EXPECT_TRUE(found_drift) << "MrcDrift should be in zone " << static_cast<int>(zone);
    EXPECT_TRUE(found_anchor) << "MrcAnchor should be in zone " << static_cast<int>(zone);
    EXPECT_TRUE(found_heaveto) << "MrcHeaveTo should be in zone " << static_cast<int>(zone);
  }
}

// Test 10: GetByTypeReturnsCorrectDescriptor
TEST_F(BehaviorDictionaryTest, GetByTypeReturnsCorrectDescriptor) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  const auto& transit_desc = dict_.get(BehaviorType::Transit);
  EXPECT_EQ(transit_desc.code_name, "TRANSIT");
}

// Test 11: LoadFailsOnNonexistentFile
TEST_F(BehaviorDictionaryTest, LoadFailsOnNonexistentFile) {
  auto result = dict_.load("nonexistent_file_should_not_exist_12345.yaml");
  EXPECT_EQ(result, ErrorCode::YamlLoadFailed);
  EXPECT_FALSE(dict_.is_loaded());
}

// Test 12: GetActiveSubsetReturnsMrcInAllZones
TEST_F(BehaviorDictionaryTest, GetActiveSubsetReturnsMrcInAllZones) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  auto subset_a = dict_.get_active_subset(0);
  auto subset_b = dict_.get_active_subset(1);
  auto subset_c = dict_.get_active_subset(2);
  auto subset_d = dict_.get_active_subset(3);

  EXPECT_GE(subset_a.size(), 3) << "ODD-A should have at least Transit+ColregAvoid+MRC(3)";
  EXPECT_GE(subset_b.size(), 3) << "ODD-B should have at least Transit+ColregAvoid+MRC(3)";
  EXPECT_GE(subset_c.size(), 3) << "ODD-C should have at least DpHold+Berth+MRC(3)";
  EXPECT_GE(subset_d.size(), 4) << "ODD-D should have at least ColregAvoid+MRC(3)";
}

// Test 13: DefaultWeightIsPositive
TEST_F(BehaviorDictionaryTest, DefaultWeightIsPositive) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  const auto& colreg_desc = dict_.get(BehaviorType::ColregAvoid);
  EXPECT_GT(colreg_desc.default_weight, 0.0);
}

// Test 14: AllSevenBehaviorsHaveDescriptors
TEST_F(BehaviorDictionaryTest, AllSevenBehaviorsHaveDescriptors) {
  ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

  const auto& t0 = dict_.get(BehaviorType::Transit);
  const auto& t1 = dict_.get(BehaviorType::ColregAvoid);
  const auto& t2 = dict_.get(BehaviorType::DpHold);
  const auto& t3 = dict_.get(BehaviorType::Berth);
  const auto& t4 = dict_.get(BehaviorType::MrcDrift);
  const auto& t5 = dict_.get(BehaviorType::MrcAnchor);
  const auto& t6 = dict_.get(BehaviorType::MrcHeaveTo);

  EXPECT_EQ(t0.type, BehaviorType::Transit);
  EXPECT_EQ(t1.type, BehaviorType::ColregAvoid);
  EXPECT_EQ(t2.type, BehaviorType::DpHold);
  EXPECT_EQ(t3.type, BehaviorType::Berth);
  EXPECT_EQ(t4.type, BehaviorType::MrcDrift);
  EXPECT_EQ(t5.type, BehaviorType::MrcAnchor);
  EXPECT_EQ(t6.type, BehaviorType::MrcHeaveTo);
}

}  // namespace mass_l3::m4
