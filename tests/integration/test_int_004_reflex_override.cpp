// INT-004: Reflex Arc → M1 OVERRIDDEN (<50ms advisory)
// Scene: Y-axis Reflex Arc fires during emergency, requiring L3 to freeze.
//
// EnvelopeState::Overridden = 5 (per types.hpp).
// ODDState.msg does not define ENVELOPE_OVERRIDDEN constant; raw value 5 is
// used in assertions with a comment referencing the source.
//
// Test cases:
//   INT004_ReflexArc_M1_EntersOverridden  — l3_freeze_required=true → M1
//                                            envelope_state == 5 (Overridden)
//   INT004_ReflexArc_Latency_Under50ms    — advisory latency check; non-RT env
//   INT004_ReflexArc_FreezeNotRequired    — l3_freeze_required=false → M1 does
//                                            NOT enter OVERRIDDEN within 1s
//
// Per v1.1.2 §5 + RFC-005; MISRA C++:2023 PATH-D (full 179 rules).

#include <chrono>
#include <functional>
#include <memory>

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"

// ── Constants (no magic numbers) ──────────────────────────────────────────────
// EnvelopeState::Overridden = 5 (see types.hpp; not in ODDState.msg constants).
constexpr uint8_t kEnvelopeStateOverridden = 5U;

constexpr float  kMockConfidence004      = 0.9F;
constexpr auto   kSpinTimeout004         = std::chrono::seconds(3);
constexpr auto   kNoOverrideWait004      = std::chrono::seconds(1);
constexpr auto   kSpinStep004            = std::chrono::milliseconds(10);
constexpr auto   kPublishInterval004     = std::chrono::milliseconds(100);
// Advisory latency threshold per v1.1.2 RFC-005 Reflex Arc timing requirement.
// This is NOT a strict ASSERT in a non-RT test environment.
constexpr int64_t kReflexLatencyAdvisoryMs = 50;
constexpr double  kOwnShipSog004           = 10.0;
constexpr double  kOwnShipCog004           = 270.0;

// ── GTest Fixture ─────────────────────────────────────────────────────────────
class Int004Test : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("int004_test_node");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
  }

  void TearDown() override {
    executor_->cancel();
    node_.reset();
    rclcpp::shutdown();
  }

  bool spin_until(std::function<bool()> condition, std::chrono::seconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
      executor_->spin_some(kSpinStep004);
    }
    return condition();
  }

  // ReflexActivationNotification with the given freeze flag.
  l3_external_msgs::msg::ReflexActivationNotification
  make_reflex(bool freeze_required) const {
    l3_external_msgs::msg::ReflexActivationNotification r{};
    r.activation_time  = node_->now();
    r.reason           = "cpa_emergency";
    r.l3_freeze_required = freeze_required;
    return r;
  }

  // Minimal WorldState for M1 prerequisite subscription.
  l3_msgs::msg::WorldState make_world_state() const {
    l3_msgs::msg::WorldState ws{};
    ws.stamp      = node_->now();
    ws.confidence = kMockConfidence004;
    ws.rationale  = "int004_world_state";
    ws.own_ship.sog_kn      = kOwnShipSog004;
    ws.own_ship.cog_deg     = kOwnShipCog004;
    ws.own_ship.heading_deg = kOwnShipCog004;
    ws.own_ship.nav_mode    = "OPTIMAL";
    ws.zone.zone_type         = "open_water";
    ws.zone.in_tss            = false;
    ws.zone.in_narrow_channel = false;
    return ws;
  }

  // Publish M1 prerequisites (WorldState) at 10 Hz to keep M1 running.
  // Returns timer handle (keep alive).
  rclcpp::TimerBase::SharedPtr make_m1_prerequisite_timer() {
    auto ws_pub = node_->create_publisher<l3_msgs::msg::WorldState>(
        "/l3/m2/world_state", rclcpp::QoS(10).reliable());
    return node_->create_wall_timer(
        kPublishInterval004,
        [ws_pub, this]() { ws_pub->publish(make_world_state()); });
  }

  std::shared_ptr<rclcpp::Node>                              node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

// ── Test 1: l3_freeze_required=true → M1 enters Overridden state ──────────────
// Publish ReflexActivationNotification with l3_freeze_required=true.
// Subscribe /l3/m1/odd_state; assert envelope_state == 5 (Overridden)
// received within 3s.
// M1's on_reflex_activation sets freeze flag → next on_odd_state_publish_tick
// (1 Hz) or event-driven publish reflects the Overridden state.
TEST_F(Int004Test, INT004_ReflexArc_M1_EntersOverridden) {
  bool overridden_received = false;
  l3_msgs::msg::OddState last_odd{};

  auto odd_sub = node_->create_subscription<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      [&overridden_received, &last_odd](
          const l3_msgs::msg::OddState::SharedPtr msg) {
        last_odd = *msg;
        // kEnvelopeStateOverridden = 5 per types.hpp EnvelopeState::Overridden.
        if (msg->envelope_state == kEnvelopeStateOverridden) {
          overridden_received = true;
        }
      });

  auto prereq_timer = make_m1_prerequisite_timer();

  auto reflex_pub = node_->create_publisher<
      l3_external_msgs::msg::ReflexActivationNotification>(
      "/l3/reflex/activation", rclcpp::QoS(10).reliable());
  auto reflex_timer = node_->create_wall_timer(
      kPublishInterval004,
      [reflex_pub, this]() { reflex_pub->publish(make_reflex(true)); });

  bool ok = spin_until([&overridden_received]() { return overridden_received; },
                       kSpinTimeout004);

  odd_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-004: M1 did not reach envelope_state=Overridden(5) within "
      << kSpinTimeout004.count() << "s after ReflexActivationNotification(freeze=true)";
  EXPECT_EQ(last_odd.envelope_state, kEnvelopeStateOverridden)
      << "INT-004: envelope_state must be 5 (Overridden per types.hpp)";
}

// ── Test 2: Advisory latency check for Reflex Arc → OVERRIDDEN ────────────────
// Record t0 before publishing; measure latency to first Overridden ODDState.
// Advisory threshold: 50 ms per v1.1.2 RFC-005.
// NOTE: timing in a non-RT GTest environment is advisory only — EXPECT not ASSERT.
TEST_F(Int004Test, INT004_ReflexArc_Latency_Under50ms) {
  bool overridden_received = false;
  std::chrono::steady_clock::time_point t_received{};

  auto odd_sub = node_->create_subscription<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      [&overridden_received, &t_received](
          const l3_msgs::msg::OddState::SharedPtr msg) {
        // kEnvelopeStateOverridden = 5 per types.hpp.
        if (!overridden_received &&
            msg->envelope_state == kEnvelopeStateOverridden) {
          t_received         = std::chrono::steady_clock::now();
          overridden_received = true;
        }
      });

  auto prereq_timer = make_m1_prerequisite_timer();

  auto reflex_pub = node_->create_publisher<
      l3_external_msgs::msg::ReflexActivationNotification>(
      "/l3/reflex/activation", rclcpp::QoS(10).reliable());

  // Record t0 immediately before the first publish.
  auto t0 = std::chrono::steady_clock::now();
  reflex_pub->publish(make_reflex(true));

  spin_until([&overridden_received]() { return overridden_received; },
             kSpinTimeout004);

  odd_sub.reset();

  ASSERT_TRUE(overridden_received)
      << "INT-004 (latency): M1 did not reach Overridden(5) within "
      << kSpinTimeout004.count() << "s";

  const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      t_received - t0).count();

  // Warning if > 50ms: see v1.1.2 RFC-005 Reflex Arc timing requirement.
  // This is advisory in a non-RT test environment; use EXPECT not ASSERT.
  EXPECT_LE(latency_ms, kReflexLatencyAdvisoryMs)
      << "INT-004: Reflex Arc latency " << latency_ms << " ms exceeds advisory "
      << kReflexLatencyAdvisoryMs << " ms (RFC-005). "
         "Non-RT test environment: this is advisory, not a hard failure.";
}

// ── Test 3: l3_freeze_required=false → M1 does NOT enter Overridden ───────────
// Publish ReflexActivationNotification with l3_freeze_required=false.
// Spin for 1s and confirm M1 envelope_state remains != Overridden(5).
// M1's on_reflex_activation sets reflex_freeze_required_=false; the state
// machine does not transition to Overridden on this path.
TEST_F(Int004Test, INT004_ReflexArc_FreezeNotRequired) {
  bool overridden_received = false;
  l3_msgs::msg::OddState last_odd{};

  auto odd_sub = node_->create_subscription<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      [&overridden_received, &last_odd](
          const l3_msgs::msg::OddState::SharedPtr msg) {
        last_odd = *msg;
        // kEnvelopeStateOverridden = 5 per types.hpp.
        if (msg->envelope_state == kEnvelopeStateOverridden) {
          overridden_received = true;
        }
      });

  auto prereq_timer = make_m1_prerequisite_timer();

  auto reflex_pub = node_->create_publisher<
      l3_external_msgs::msg::ReflexActivationNotification>(
      "/l3/reflex/activation", rclcpp::QoS(10).reliable());
  auto reflex_timer = node_->create_wall_timer(
      kPublishInterval004,
      // l3_freeze_required = false → M1 must not enter Overridden.
      [reflex_pub, this]() { reflex_pub->publish(make_reflex(false)); });

  // Wait 1s; during this time M1 publishes periodic ODDState at 1 Hz.
  spin_until([]() { return false; }, kNoOverrideWait004);

  odd_sub.reset();

  EXPECT_FALSE(overridden_received)
      << "INT-004: M1 entered Overridden(5) despite l3_freeze_required=false";
}
