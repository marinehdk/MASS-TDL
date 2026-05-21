// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
// SPDX-License-Identifier: Proprietary
// Integration tests — M3 dual-subscription independence + ODD-B + L4 chain
// spec D2.3 §7.3, §7.4, §7.5

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/tor_request.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_external_msgs/msg/tracking_error.hpp"
#include "m3_mission_manager/mission_manager_node.hpp"

#include "../fixtures/voyage_task_fixtures.hpp"
#include "../fixtures/route_fixtures.hpp"

namespace mass_l3::m3 {
namespace {

// -----------------------------------------------------------------------
// Test fixture: spins node + helper pub/sub in background thread
// -----------------------------------------------------------------------
class M3DualSubTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { rclcpp::init(0, nullptr); }
  static void TearDownTestSuite() { rclcpp::shutdown(); }

  void SetUp() override {
    rclcpp::NodeOptions opts;
    opts.parameter_overrides({
        rclcpp::Parameter("l1_watchdog.warning_s",   2.0),
        rclcpp::Parameter("l1_watchdog.timeout_s",   4.0),
        rclcpp::Parameter("odd.degraded_buffer_s",   0.1),
    });
    node_   = std::make_shared<MissionManagerNode>(opts);
    helper_ = std::make_shared<rclcpp::Node>("m3_test_helper");

    voyage_task_pub_ = helper_->create_publisher<l3_external_msgs::msg::VoyageTask>(
        "/l1/voyage_task", rclcpp::QoS(10).reliable().transient_local());
    route_pub_ = helper_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
        "/l2/planned_route", rclcpp::QoS(10).reliable());
    speed_pub_ = helper_->create_publisher<l3_external_msgs::msg::SpeedProfile>(
        "/l2/speed_profile", rclcpp::QoS(10).reliable());
    odd_pub_ = helper_->create_publisher<l3_msgs::msg::ODDState>(
        "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
    world_pub_ = helper_->create_publisher<l3_msgs::msg::WorldState>(
        "/l3/m2/world_state", rclcpp::SensorDataQoS().keep_last(2));
    tracking_pub_ = helper_->create_publisher<l3_external_msgs::msg::TrackingError>(
        "/l4/tracking_error", rclcpp::SensorDataQoS().keep_last(2));

    goal_sub_ = helper_->create_subscription<l3_msgs::msg::MissionGoal>(
        "/l3/m3/mission_goal", rclcpp::QoS(50),
        [this](l3_msgs::msg::MissionGoal::SharedPtr msg) {
          last_goal_ = msg;
        });
    replan_sub_ = helper_->create_subscription<l3_msgs::msg::RouteReplanRequest>(
        "/l3/m3/route_replan_request", rclcpp::QoS(50).transient_local(),
        [this](l3_msgs::msg::RouteReplanRequest::SharedPtr msg) {
          last_replan_ = msg;
        });
    asdr_sub_ = helper_->create_subscription<l3_msgs::msg::ASDRRecord>(
        "/l3/asdr/record", rclcpp::QoS(50).transient_local(),
        [this](l3_msgs::msg::ASDRRecord::SharedPtr msg) {
          last_asdr_ = msg;
          last_asdr_type_ = msg->decision_type;
        });
    tor_sub_ = helper_->create_subscription<l3_msgs::msg::ToRRequest>(
        "/l3/m3/tor_request", rclcpp::QoS(10).transient_local(),
        [this](l3_msgs::msg::ToRRequest::SharedPtr msg) {
          last_tor_ = msg;
        });

    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
    executor_->add_node(helper_);
    spin_thread_ = std::thread([this]() {
      executor_->spin();
    });
  }

  void TearDown() override {
    executor_->cancel();
    if (spin_thread_.joinable()) { spin_thread_.join(); }
    node_.reset();
    helper_.reset();
  }

  // Bring node to ACTIVE state
  void bring_to_active() {
    auto vt = make_valid_voyage_task(1);
    voyage_task_pub_->publish(vt);
    spin_ms(50);

    auto route = make_valid_route(1);
    route_pub_->publish(route);
    spin_ms(50);

    publish_odd(0.9F, l3_msgs::msg::ODDState::ODD_ZONE_A);
    publish_world(1.0);
    spin_ms(100);
  }

  void publish_odd(float conformance, uint8_t zone) {
    l3_msgs::msg::ODDState msg;
    msg.stamp.sec = 1000;
    msg.stamp.nanosec = 0;
    msg.current_zone    = zone;
    msg.conformance_score = conformance;
    msg.envelope_state  = (conformance < 0.3F) ?
        l3_msgs::msg::ODDState::ENVELOPE_OUT : l3_msgs::msg::ODDState::ENVELOPE_IN;
    msg.auto_level      = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
    msg.health          = l3_msgs::msg::ODDState::HEALTH_FULL;
    msg.tmr_s = 60.0F;
    msg.tdl_s = 10.0F;
    msg.confidence = 0.9F;
    odd_pub_->publish(msg);
  }

  void publish_world(double sea_current_kn) {
    l3_msgs::msg::WorldState msg;
    msg.own_ship.current_speed_kn = sea_current_kn;
    msg.own_ship.sog_kn  = 10.0;
    msg.own_ship.cog_deg = 0.0;
    msg.confidence = 1.0F;
    world_pub_->publish(msg);
  }

  void publish_tracking(float xte_nm) {
    l3_external_msgs::msg::TrackingError msg;
    msg.xte_nm = xte_nm;
    msg.along_track_error_nm = 0.0F;
    msg.confidence = 1.0F;
    tracking_pub_->publish(msg);
  }

  void spin_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  }

  bool wait_for(std::function<bool()> pred, int timeout_ms = 3000, int poll_ms = 50) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
      if (pred()) { return true; }
      spin_ms(poll_ms);
    }
    return pred();
  }

  std::shared_ptr<MissionManagerNode>  node_;
  std::shared_ptr<rclcpp::Node>        helper_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
  std::thread spin_thread_;

  rclcpp::Publisher<l3_external_msgs::msg::VoyageTask>::SharedPtr   voyage_task_pub_;
  rclcpp::Publisher<l3_external_msgs::msg::PlannedRoute>::SharedPtr  route_pub_;
  rclcpp::Publisher<l3_external_msgs::msg::SpeedProfile>::SharedPtr  speed_pub_;
  rclcpp::Publisher<l3_msgs::msg::ODDState>::SharedPtr               odd_pub_;
  rclcpp::Publisher<l3_msgs::msg::WorldState>::SharedPtr             world_pub_;
  rclcpp::Publisher<l3_external_msgs::msg::TrackingError>::SharedPtr tracking_pub_;

  rclcpp::Subscription<l3_msgs::msg::MissionGoal>::SharedPtr         goal_sub_;
  rclcpp::Subscription<l3_msgs::msg::RouteReplanRequest>::SharedPtr  replan_sub_;
  rclcpp::Subscription<l3_msgs::msg::ASDRRecord>::SharedPtr          asdr_sub_;
  rclcpp::Subscription<l3_msgs::msg::ToRRequest>::SharedPtr          tor_sub_;

  l3_msgs::msg::MissionGoal::SharedPtr          last_goal_;
  l3_msgs::msg::RouteReplanRequest::SharedPtr   last_replan_;
  l3_msgs::msg::ASDRRecord::SharedPtr           last_asdr_;
  std::string                                   last_asdr_type_;
  l3_msgs::msg::ToRRequest::SharedPtr           last_tor_;
};

// -----------------------------------------------------------------------
// IT-01: normal — ACTIVE; ETA valid; confidence ~1.0; schema_version=120
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT01_NormalActive) {
  bring_to_active();
  ASSERT_TRUE(wait_for([this]{ return last_goal_ != nullptr; }, 3000));
  EXPECT_EQ(last_goal_->schema_version, 120U);
  EXPECT_EQ(last_goal_->current_error_severity, 0U);  // NORMAL
  EXPECT_EQ(last_goal_->l1_watchdog_status, 0U);  // OK
}

// -----------------------------------------------------------------------
// IT-02: L1 dropout > 2s + L2 normal → WARNING, confidence ≤ 0.6
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT02_L1DropoutWarning) {
  bring_to_active();
  ASSERT_TRUE(wait_for(
      [this]{
        return last_goal_ != nullptr &&
               last_goal_->l1_watchdog_status == 1U;  // WARNING
      }, 5000));
  EXPECT_LE(last_goal_->confidence, 0.61F);
  EXPECT_TRUE(wait_for([this]{ return last_asdr_type_ == "l1_dropout_warning"; }, 2000));
}

// -----------------------------------------------------------------------
// IT-03: L1 dropout > 4s → TIMEOUT + ToR issued
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT03_L1DropoutTimeout) {
  bring_to_active();
  ASSERT_TRUE(wait_for(
      [this]{
        return last_goal_ != nullptr &&
               last_goal_->l1_watchdog_status == 2U;  // TIMEOUT
      }, 8000));
  EXPECT_LE(last_goal_->confidence, 0.41F);
  ASSERT_TRUE(wait_for([this]{ return last_tor_ != nullptr; }, 2000));
  EXPECT_EQ(last_tor_->reason, l3_msgs::msg::ToRRequest::REASON_MANUAL_REQUEST);
  EXPECT_FLOAT_EQ(last_tor_->deadline_s, 60.0F);
}

// -----------------------------------------------------------------------
// IT-04: L2 route stale → eta_to_target_s = -1
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT04_L2RouteStale) {
  bring_to_active();
  ASSERT_TRUE(wait_for([this]{ return last_goal_ != nullptr; }, 2000));
  last_goal_.reset();
  spin_ms(4000);  // route becomes stale (threshold=3s)
  publish_odd(0.9F, l3_msgs::msg::ODDState::ODD_ZONE_A);
  publish_world(0.5);
  ASSERT_TRUE(wait_for([this]{ return last_goal_ != nullptr; }, 3000));
  EXPECT_FLOAT_EQ(last_goal_->eta_to_target_s, -1.0F);
}

// -----------------------------------------------------------------------
// IT-05: L1 WARNING + L2 stale → both degradations coexist
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT05_L1WarningPlusL2Stale) {
  bring_to_active();
  spin_ms(4000);
  publish_world(0.5);
  ASSERT_TRUE(wait_for(
      [this]{
        return last_goal_ != nullptr &&
               last_goal_->l1_watchdog_status >= 1U &&
               last_goal_->eta_to_target_s < 0.0F;
      }, 3000));
  EXPECT_LE(last_goal_->confidence, 0.61F);
  EXPECT_FLOAT_EQ(last_goal_->eta_to_target_s, -1.0F);
}

// -----------------------------------------------------------------------
// IT-06: L1 recovery → watchdog resets to OK + ASDR "l1_recovered"
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, IT06_L1Recovery) {
  bring_to_active();
  ASSERT_TRUE(wait_for(
      [this]{ return last_goal_ && last_goal_->l1_watchdog_status == 1U; }, 5000));
  auto vt = make_valid_voyage_task(2);
  voyage_task_pub_->publish(vt);
  ASSERT_TRUE(wait_for(
      [this]{ return last_asdr_type_ == "l1_recovered"; }, 3000));
  ASSERT_TRUE(wait_for(
      [this]{ return last_goal_ && last_goal_->l1_watchdog_status == 0U; }, 2000));
  EXPECT_FLOAT_EQ(last_goal_->confidence, 1.0F);
}

// -----------------------------------------------------------------------
// §7.4 ODD-B live chain: conformance 0.9→0.55 → RouteReplanRequest ODD_EXIT
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, OddB_ReplanChain) {
  bring_to_active();
  last_replan_.reset();

  const auto t_inject = std::chrono::steady_clock::now();
  publish_odd(0.55F, l3_msgs::msg::ODDState::ODD_ZONE_B);

  ASSERT_TRUE(wait_for([this]{ return last_replan_ != nullptr; }, 3000));
  const auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(
      std::chrono::steady_clock::now() - t_inject).count();

  EXPECT_EQ(last_replan_->reason, l3_msgs::msg::RouteReplanRequest::REASON_ODD_EXIT);
  EXPECT_LE(last_replan_->deadline_s, 121.0F);
  EXPECT_LT(elapsed, 2.5);
  EXPECT_EQ(last_replan_->schema_version, 120U);
}

// -----------------------------------------------------------------------
// §7.5 L4 tracking error chain: xte=0.2→0.6 → current_error_severity=2
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, L4TrackingChain_HighSeverity) {
  bring_to_active();
  publish_tracking(0.2F);
  spin_ms(100);

  last_goal_.reset();
  last_asdr_.reset();
  last_asdr_type_.clear();

  const auto t_inject = std::chrono::steady_clock::now();
  publish_tracking(0.6F);

  ASSERT_TRUE(wait_for(
      [this]{ return last_goal_ && last_goal_->current_error_severity == 2U; },
      1000));
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - t_inject).count();
  EXPECT_LT(elapsed_ms, 500L);

  EXPECT_EQ(last_goal_->schema_version, 120U);
  EXPECT_FLOAT_EQ(last_goal_->xte_nm, 0.6F);
  EXPECT_LE(last_goal_->confidence, 0.86F);

  ASSERT_TRUE(wait_for([this]{ return last_asdr_type_ == "current_error_high_alert"; }, 1000));

  // Verify resolution
  last_goal_.reset();
  last_asdr_type_.clear();
  publish_tracking(0.1F);
  ASSERT_TRUE(wait_for(
      [this]{ return last_goal_ && last_goal_->current_error_severity == 0U; }, 1000));
  ASSERT_TRUE(wait_for([this]{ return last_asdr_type_ == "current_error_resolved"; }, 1000));
}

// -----------------------------------------------------------------------
// §7.5 L4 chain with sea_current HIGH (M2 driven)
// -----------------------------------------------------------------------
TEST_F(M3DualSubTest, L4TrackingChain_SeaCurrentHigh) {
  bring_to_active();
  last_goal_.reset();

  publish_world(2.5);  // sea_current > 2.0 kn → HIGH

  ASSERT_TRUE(wait_for(
      [this]{ return last_goal_ && last_goal_->current_error_severity == 2U; }, 2000));
  EXPECT_FLOAT_EQ(last_goal_->sea_current_kn, 2.5F);
  EXPECT_LE(last_goal_->confidence, 0.86F);
}

}  // namespace
}  // namespace mass_l3::m3
