#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m4_behavior_arbiter/behavior_activation.hpp"
#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

class BehaviorActivationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    fixture_yaml_path_ = std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "fixtures" / "behavior_definitions_default.yaml";

    // Load dictionary for tests
    ASSERT_EQ(ErrorCode::Ok, dict_.load(fixture_yaml_path_.string()));

    // Create condition checker
    condition_ = std::make_unique<BehaviorActivationCondition>(dict_);
  }

  std::filesystem::path fixture_yaml_path_;
  BehaviorDictionary dict_;
  std::unique_ptr<BehaviorActivationCondition> condition_;

  ArbitrationInputs make_default_inputs() const {
    ArbitrationInputs inputs;
    inputs.odd_state.current_zone = 0;  // ODD-A
    inputs.odd_state.auto_level = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
    inputs.odd_state.health = l3_msgs::msg::ODDState::HEALTH_FULL;
    inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_NORMAL;
    inputs.m1_fresh = true;
    inputs.m2_fresh = true;
    inputs.m3_fresh = true;
    inputs.m6_fresh = true;
    inputs.cpa_safe_m = 1852.0;  // 1.0 nm in meters
    return inputs;
  }
};

// Test 1: TransitActiveInOddANoTargets
TEST_F(BehaviorActivationTest, TransitActiveInOddANoTargets) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 0;  // ODD-A
  inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_NORMAL;
  inputs.world_state.targets.clear();  // No targets

  EXPECT_TRUE(condition_->is_active(BehaviorType::Transit, inputs));
}

// Test 2: TransitInactiveWhenTargetInsideCpa
TEST_F(BehaviorActivationTest, TransitInactiveWhenTargetInsideCpa) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 0;  // ODD-A
  inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_NORMAL;

  // Add a target with CPA below safe threshold
  l3_msgs::msg::TrackedTarget target;
  target.target_id = 1;
  target.cpa_m = 500.0;  // Below 1852.0 m safe threshold
  inputs.world_state.targets.push_back(target);

  EXPECT_FALSE(condition_->is_active(BehaviorType::Transit, inputs));
}

// Test 3: ColregAvoidActiveWhenTargetClose
TEST_F(BehaviorActivationTest, ColregAvoidActiveWhenTargetClose) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 0;  // ODD-A
  inputs.m6_fresh = true;

  // Add a target with CPA below safe threshold
  l3_msgs::msg::TrackedTarget target;
  target.target_id = 1;
  target.cpa_m = 500.0;  // Below 1852.0 m safe threshold
  inputs.world_state.targets.push_back(target);

  EXPECT_TRUE(condition_->is_active(BehaviorType::ColregAvoid, inputs));
}

// Test 4: ColregAvoidInactiveWithoutM6Fresh
TEST_F(BehaviorActivationTest, ColregAvoidInactiveWithoutM6Fresh) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 0;  // ODD-A
  inputs.m6_fresh = false;  // M6 constraint not fresh

  // Add a target with CPA below safe threshold
  l3_msgs::msg::TrackedTarget target;
  target.target_id = 1;
  target.cpa_m = 500.0;
  inputs.world_state.targets.push_back(target);

  EXPECT_FALSE(condition_->is_active(BehaviorType::ColregAvoid, inputs));
}

// Test 5: DpHoldActiveInOddC
TEST_F(BehaviorActivationTest, DpHoldActiveInOddC) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 2;  // ODD-C
  inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_LIMITED;  // DP indicator

  EXPECT_TRUE(condition_->is_active(BehaviorType::DpHold, inputs));
}

// Test 6: DpHoldInactiveInOddA
TEST_F(BehaviorActivationTest, DpHoldInactiveInOddA) {
  auto inputs = make_default_inputs();
  inputs.odd_state.current_zone = 0;  // ODD-A, not ODD-C
  inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_LIMITED;

  EXPECT_FALSE(condition_->is_active(BehaviorType::DpHold, inputs));
}

// Test 7: MrcDriftActiveOnEmergencyMode
TEST_F(BehaviorActivationTest, MrcDriftActiveOnEmergencyMode) {
  auto inputs = make_default_inputs();
  inputs.mode_cmd.mode = l3_msgs::msg::ModeCmd::MODE_EMERGENCY;

  EXPECT_TRUE(condition_->is_active(BehaviorType::MrcDrift, inputs));
}

// Test 8: ComputeActiveSetContainsMrcWhenEnvelopeMrcActive
TEST_F(BehaviorActivationTest, ComputeActiveSetContainsMrcWhenEnvelopeMrcActive) {
  auto inputs = make_default_inputs();
  inputs.odd_state.envelope_state = l3_msgs::msg::ODDState::ENVELOPE_MRC_ACTIVE;

  auto active_set = condition_->compute_active_set(inputs);

  bool found_mrc = false;
  for (const auto& behavior : active_set) {
    if (behavior == BehaviorType::MrcDrift || behavior == BehaviorType::MrcAnchor ||
        behavior == BehaviorType::MrcHeaveTo) {
      found_mrc = true;
      break;
    }
  }

  EXPECT_TRUE(found_mrc) << "At least one MRC behavior should be in active set";
}

}  // namespace mass_l3::m4
