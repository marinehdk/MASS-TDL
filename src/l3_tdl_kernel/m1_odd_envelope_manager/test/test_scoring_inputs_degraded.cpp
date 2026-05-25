/// Unit tests for ScoringInputs degraded defaults (DEMO-1 Task 3).
///
/// Verifies that build_scoring_inputs() returns degraded values when
/// no env_state / own_ship / diagnostics data is available, and that
/// real data overrides the degraded defaults correctly.

#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>

#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

namespace {

class ScoringInputsDegradedTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }

  void SetUp() override {
    node_ = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();
  }

  void TearDown() override {
    node_.reset();
  }

  std::shared_ptr<mass_l3::m1::OddEnvelopeManagerNode> node_;
};

}  // namespace

TEST_F(ScoringInputsDegradedTest, NoDataDefaultsToDegraded) {
  auto inputs = node_->build_scoring_inputs();

  EXPECT_DOUBLE_EQ(inputs.visibility_nm, 2.0);
  EXPECT_DOUBLE_EQ(inputs.sea_state_hs, 2.0);
  EXPECT_FALSE(inputs.gnss_quality_good);
  EXPECT_FALSE(inputs.radar_health_ok);
  EXPECT_FALSE(inputs.comm_ok);
  EXPECT_FALSE(inputs.tmr_available);
  EXPECT_DOUBLE_EQ(inputs.comm_delay_s, 999.0);
  EXPECT_TRUE(inputs.any_sensor_critical);
}

TEST_F(ScoringInputsDegradedTest, DiagnosticsOverridesDefaults) {
  auto diag = std::make_shared<diagnostic_msgs::msg::DiagnosticArray>();

  diagnostic_msgs::msg::DiagnosticStatus radar_status;
  radar_status.name = "radar";
  radar_status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
  diag->status.push_back(radar_status);

  diagnostic_msgs::msg::DiagnosticStatus comm_status;
  comm_status.name = "comm";
  comm_status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
  diagnostic_msgs::msg::KeyValue delay_kv;
  delay_kv.key = "delay_s";
  delay_kv.value = "1.5";
  comm_status.values.push_back(delay_kv);
  diag->status.push_back(comm_status);

  diagnostic_msgs::msg::DiagnosticStatus tmr_status;
  tmr_status.name = "tmr";
  tmr_status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
  diag->status.push_back(tmr_status);

  node_->last_diagnostics_ = diag;

  auto inputs = node_->build_scoring_inputs();

  EXPECT_TRUE(inputs.radar_health_ok);
  EXPECT_TRUE(inputs.comm_ok);
  EXPECT_TRUE(inputs.tmr_available);
  EXPECT_DOUBLE_EQ(inputs.comm_delay_s, 1.5);
  EXPECT_FALSE(inputs.any_sensor_critical);
}

TEST_F(ScoringInputsDegradedTest, EnvStateOverridesDefaults) {
  auto env = std::make_shared<l3_external_msgs::msg::EnvironmentState>();
  env->visibility_range_nm = 5.0;
  env->wave_height_m = 1.5;
  node_->last_env_state_ = env;

  auto inputs = node_->build_scoring_inputs();

  EXPECT_DOUBLE_EQ(inputs.visibility_nm, 5.0);
  EXPECT_DOUBLE_EQ(inputs.sea_state_hs, 1.5);
}

TEST_F(ScoringInputsDegradedTest, OwnShipOverridesDefaults) {
  auto own = std::make_shared<l3_external_msgs::msg::FilteredOwnShipState>();
  own->nav_mode = "OPTIMAL";
  node_->last_own_ship_ = own;

  auto inputs = node_->build_scoring_inputs();

  EXPECT_TRUE(inputs.gnss_quality_good);

  own->nav_mode = "DR_LONG";
  node_->last_own_ship_ = own;

  inputs = node_->build_scoring_inputs();

  EXPECT_FALSE(inputs.gnss_quality_good);
}
