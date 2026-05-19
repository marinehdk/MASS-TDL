#include <filesystem>

#include <gtest/gtest.h>

#include <l3_msgs/msg/odd_state.hpp>

#include "m4_behavior_arbiter/behavior_arbiter.hpp"
#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

class BehaviorArbiterTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fixture_yaml_path_ = std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "fixtures" / "behavior_definitions_default.yaml";
    auto err = dict_.load(fixture_yaml_path_.string());
    ASSERT_EQ(err, ErrorCode::Ok) << "Failed to load test fixture";

    IvPHeadingDomain h_domain{1.0};
    IvPSpeedDomain s_domain{0.0, 20.0, 0.5};
    arbiter_ = std::make_unique<BehaviorArbiter>(
        dict_, h_domain, s_domain, std::chrono::milliseconds{100});
  }

  std::filesystem::path fixture_yaml_path_;
  BehaviorDictionary dict_;
  std::unique_ptr<BehaviorArbiter> arbiter_;
};

// A1: Transit selected when healthy inputs, ODD-A, no targets
TEST_F(BehaviorArbiterTest, TransitWhenHealthy) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_FULL);
  inputs.m1_fresh = true;
  inputs.m2_fresh = true;
  inputs.m3_fresh = true;
  inputs.world_state.targets.clear();

  const auto result = arbiter_->run(inputs);

  EXPECT_EQ(result.primary, BehaviorType::Transit);
}

// A2: MrcDrift selected when HEALTH_CRITICAL
TEST_F(BehaviorArbiterTest, MrcDriftOnCritical) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_CRITICAL);
  inputs.m1_fresh = true;

  const auto result = arbiter_->run(inputs);

  EXPECT_EQ(result.primary, BehaviorType::MrcDrift);
}

// A3: Result primary is always in valid BehaviorType range
TEST_F(BehaviorArbiterTest, ResultPrimaryIsValid) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_FULL);

  const auto result = arbiter_->run(inputs);

  EXPECT_GE(static_cast<int>(result.primary), 0);
  EXPECT_LE(static_cast<int>(result.primary), static_cast<int>(kBehaviorCount) - 1);
}

// A4: Heading range is always valid
TEST_F(BehaviorArbiterTest, HeadingRangeValid) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_FULL);

  const auto result = arbiter_->run(inputs);

  EXPECT_GE(result.heading_min_deg, 0.0);
  EXPECT_LE(result.heading_max_deg, 360.0);
  EXPECT_LE(result.heading_min_deg, result.heading_max_deg);
}

// A5: Speed range is always valid
TEST_F(BehaviorArbiterTest, SpeedRangeValid) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_FULL);

  const auto result = arbiter_->run(inputs);

  EXPECT_GE(result.speed_min_kn, 0.0);
  EXPECT_GT(result.speed_max_kn, 0.0);
  EXPECT_LE(result.speed_min_kn, result.speed_max_kn);
}

// A6: Rationale is always non-empty
TEST_F(BehaviorArbiterTest, RationaleNonEmpty) {
  ArbitrationInputs inputs;
  inputs.odd_state.current_zone = static_cast<uint8_t>(0);  // ODD-A
  inputs.odd_state.health =
      static_cast<decltype(inputs.odd_state.health)>(l3_msgs::msg::ODDState::HEALTH_FULL);

  const auto result = arbiter_->run(inputs);

  EXPECT_FALSE(result.rationale.empty());
}

}  // namespace mass_l3::m4
