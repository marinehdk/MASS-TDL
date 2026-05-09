// INT-001: M2→M6→M5 COLREGs chain integration test
// Scene: own ship 18 kn @ 090°, target 12 kn @ 270° (head-on Rule 14)
// Expected flow: WorldState(HEAD_ON) → M6 → COLREGsConstraint(Rule14) → M5 → AvoidancePlan(stbd)
//
// KNOWN BUG: M6 publishes /l3/m6/colregs_constraint but M5 subscribes /m6/colregs_constraint
// INT001_FullChain_TopicMismatch surfaces this via asserting the gap between the two topics.
//
// Per v1.1.2 §9, §10; MISRA C++:2023 PATH-D (full 179 rules; 40-line limit is PATH-S only).

#include <chrono>
#include <functional>
#include <memory>

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "l3_msgs/msg/encounter_classification.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"

// ── Constants (no magic numbers) ──────────────────────────────────────────────
constexpr float  kMockConfidence          = 0.9F;
constexpr double kOwnShipSogKn            = 18.0;
constexpr double kOwnShipCogDeg           = 90.0;
constexpr double kTargetSogKn             = 12.0;
constexpr double kTargetCogDeg            = 270.0;
constexpr double kHeadOnRelBearingDeg     = 0.0;
constexpr double kHeadOnAspectDeg         = 180.0;
constexpr auto   kSpinTimeout             = std::chrono::seconds(3);
constexpr auto   kMismatchTimeout         = std::chrono::seconds(2);
constexpr auto   kSpinStep                = std::chrono::milliseconds(100);
constexpr float  kBehaviorSpeedMinKn      = 0.0F;
constexpr float  kBehaviorSpeedMaxKn      = 20.0F;
constexpr float  kBehaviorHeadingMinDeg   = 0.0F;
constexpr float  kBehaviorHeadingMaxDeg   = 360.0F;
constexpr double kKnotsToMps              = 0.514444;  // standard IMO/IACS conversion (1852 m/nm ÷ 3600 s/h ≈ 0.5144 m/s)
constexpr double kMockTcpaS               = 120.0;     // within Rule 14 T_act engagement range
constexpr double kHeadOnCpaM              = 0.0;       // collision course geometry for HEAD_ON scenario

// ── GTest Fixture ─────────────────────────────────────────────────────────────
class Int001Test : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("int001_test_node");
    executor_ = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
    executor_->add_node(node_);
  }

  void TearDown() override {
    executor_->cancel();
    node_.reset();
    rclcpp::shutdown();
  }

  // Spin until condition() returns true or timeout elapses.
  bool spin_until(std::function<bool()> condition, std::chrono::seconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!condition() && std::chrono::steady_clock::now() < deadline) {
      executor_->spin_some(kSpinStep);
    }
    return condition();
  }

  // Build a minimal ODDState (ODD_ZONE_A, D3, HEALTH_FULL, ENVELOPE_IN).
  l3_msgs::msg::ODDState make_odd_state() const {
    l3_msgs::msg::ODDState odd{};
    odd.stamp              = node_->now();
    odd.current_zone       = l3_msgs::msg::ODDState::ODD_ZONE_A;
    odd.auto_level         = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
    odd.health             = l3_msgs::msg::ODDState::HEALTH_FULL;
    odd.envelope_state     = l3_msgs::msg::ODDState::ENVELOPE_IN;
    odd.conformance_score  = kMockConfidence;
    odd.confidence         = kMockConfidence;
    odd.rationale          = "int001_test_mock";
    return odd;
  }

  // Build a WorldState with one HEAD_ON encounter classification.
  l3_msgs::msg::WorldState make_head_on_world_state() const {
    l3_msgs::msg::WorldState ws{};
    ws.stamp      = node_->now();
    ws.confidence = kMockConfidence;
    ws.rationale  = "int001_head_on_mock";

    // Own ship: 18 kn @ 090°
    ws.own_ship.sog_kn    = kOwnShipSogKn;
    ws.own_ship.cog_deg   = kOwnShipCogDeg;
    ws.own_ship.heading_deg = kOwnShipCogDeg;
    ws.own_ship.nav_mode  = "OPTIMAL";

    // Zone (minimal defaults — open water)
    ws.zone.zone_type         = "open_water";
    ws.zone.in_tss            = false;
    ws.zone.in_narrow_channel = false;

    // Target: 12 kn @ 270° (head-on)
    l3_msgs::msg::TrackedTarget tgt{};
    tgt.stamp                = node_->now();
    tgt.target_id            = 1U;
    tgt.sog_kn               = kTargetSogKn;
    tgt.cog_deg              = kTargetCogDeg;
    tgt.heading_deg          = kTargetCogDeg;
    tgt.classification       = "vessel";
    tgt.classification_confidence = kMockConfidence;
    tgt.cpa_m                = kHeadOnCpaM;
    tgt.tcpa_s               = kMockTcpaS;
    tgt.confidence           = kMockConfidence;
    tgt.source_sensor        = "fused";

    tgt.encounter.encounter_type     =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON;
    tgt.encounter.relative_bearing_deg = kHeadOnRelBearingDeg;
    tgt.encounter.aspect_angle_deg   = kHeadOnAspectDeg;
    tgt.encounter.is_giveway         = true;

    ws.targets.push_back(tgt);
    return ws;
  }

  // Build a minimal OwnShipState (18 kn @ 090°) for M5's /m2/own_ship_state.
  l3_msgs::msg::OwnShipState make_own_ship_state() const {
    l3_msgs::msg::OwnShipState s{};
    s.stamp        = node_->now();
    s.sog_kn       = kOwnShipSogKn;
    s.cog_deg      = kOwnShipCogDeg;
    s.heading_deg  = kOwnShipCogDeg;
    s.u_water      = kOwnShipSogKn * kKnotsToMps;
    s.nav_mode     = "OPTIMAL";
    return s;
  }

  // Build a TrackedTargetArray with one HEAD_ON target for /m2/tracked_targets.
  l3_external_msgs::msg::TrackedTargetArray make_tracked_target_array() const {
    l3_external_msgs::msg::TrackedTargetArray arr{};
    arr.stamp      = node_->now();
    arr.confidence = kMockConfidence;
    arr.rationale  = "int001_test_mock";

    l3_msgs::msg::TrackedTarget tgt{};
    tgt.stamp             = node_->now();
    tgt.target_id         = 1U;
    tgt.sog_kn            = kTargetSogKn;
    tgt.cog_deg           = kTargetCogDeg;
    tgt.heading_deg       = kTargetCogDeg;
    tgt.classification    = "vessel";
    tgt.classification_confidence = kMockConfidence;
    tgt.cpa_m             = kHeadOnCpaM;
    tgt.tcpa_s            = kMockTcpaS;
    tgt.confidence        = kMockConfidence;
    tgt.source_sensor     = "fused";
    tgt.encounter.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON;
    tgt.encounter.relative_bearing_deg = kHeadOnRelBearingDeg;
    tgt.encounter.aspect_angle_deg     = kHeadOnAspectDeg;
    tgt.encounter.is_giveway           = true;
    arr.targets.push_back(tgt);
    return arr;
  }

  // Build a minimal BehaviorPlan (COLREG_AVOID) for M5 prerequisite.
  l3_msgs::msg::BehaviorPlan make_behavior_plan() const {
    l3_msgs::msg::BehaviorPlan bp{};
    bp.stamp              = node_->now();
    bp.behavior           = l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID;
    bp.heading_min_deg    = kBehaviorHeadingMinDeg;
    bp.heading_max_deg    = kBehaviorHeadingMaxDeg;
    bp.speed_min_kn       = kBehaviorSpeedMinKn;
    bp.speed_max_kn       = kBehaviorSpeedMaxKn;
    bp.confidence         = kMockConfidence;
    bp.rationale          = "int001_test_mock";
    return bp;
  }

  // Build a COLREGsConstraint with Rule 14 active (HEAD_ON give-way phase).
  l3_msgs::msg::COLREGsConstraint make_rule14_constraint() const {
    l3_msgs::msg::COLREGsConstraint c{};
    c.stamp             = node_->now();
    c.phase             = "T_act";
    c.conflict_detected = false;
    c.confidence        = kMockConfidence;
    c.rationale         = "int001_direct_mock";

    l3_msgs::msg::RuleActive rule14{};
    rule14.rule_id         = 14U;
    rule14.rule_phase      = "T_act";
    rule14.target_id       = 1U;
    rule14.rule_confidence = kMockConfidence;
    rule14.rationale       = "HEAD_ON_giveway";
    c.active_rules.push_back(rule14);
    return c;
  }

  // Publish WorldState + ODDState to M2/M1 topics on a 4 Hz timer.
  // Returns the timer handle (keep alive for its lifetime).
  rclcpp::TimerBase::SharedPtr make_world_odd_publisher() {
    auto ws_pub = node_->create_publisher<l3_msgs::msg::WorldState>(
        "/l3/m2/world_state", 10);
    auto odd_pub = node_->create_publisher<l3_msgs::msg::ODDState>(
        "/l3/m1/odd_state", 10);
    return node_->create_wall_timer(
        std::chrono::milliseconds(250),
        [ws_pub, odd_pub, this]() {
          ws_pub->publish(make_head_on_world_state());
          odd_pub->publish(make_odd_state());
        });
  }

  // Publish all four M5 hard prerequisites on a 4 Hz timer.
  // Returns the timer handle (keep alive for its lifetime).
  rclcpp::TimerBase::SharedPtr make_m5_prerequisite_publisher() {
    auto cp = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
        "/m6/colregs_constraint", 10);
    auto op = node_->create_publisher<l3_msgs::msg::OwnShipState>(
        "/m2/own_ship_state", 10);
    auto tp = node_->create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
        "/m2/tracked_targets", 10);
    auto bp = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
        "/m4/behavior_plan", 10);
    return node_->create_wall_timer(
        std::chrono::milliseconds(250),
        [cp, op, tp, bp, this]() {
          cp->publish(make_rule14_constraint());
          op->publish(make_own_ship_state());
          tp->publish(make_tracked_target_array());
          bp->publish(make_behavior_plan());
        });
  }

  std::shared_ptr<rclcpp::Node>                              node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

// ── Test 1: WorldState → M6 → COLREGsConstraint ───────────────────────────────
// Publishes mock WorldState+ODDState; asserts M6 responds with confidence > 0.
TEST_F(Int001Test, INT001_M2ToM6_WorldStateTriggersConstraint) {
  bool received = false;
  l3_msgs::msg::COLREGsConstraint last_constraint{};

  auto constraint_sub =
      node_->create_subscription<l3_msgs::msg::COLREGsConstraint>(
          "/l3/m6/colregs_constraint", 10,
          [&received, &last_constraint](
              const l3_msgs::msg::COLREGsConstraint::SharedPtr msg) {
            last_constraint = *msg;
            received = true;
          });

  // Publish WorldState + ODDState at ~4 Hz until M6 responds or timeout.
  auto publish_timer = make_world_odd_publisher();

  bool ok = spin_until([&received]() { return received; }, kSpinTimeout);

  constraint_sub.reset();  // explicit lifetime: prevent callbacks during assertion

  ASSERT_TRUE(ok)
      << "INT-001: M6 did not publish COLREGsConstraint within "
      << kSpinTimeout.count() << "s of WorldState+ODDState input";
  EXPECT_GT(last_constraint.confidence, 0.0F)
      << "INT-001: COLREGsConstraint.confidence must be > 0";
}

// ── Test 2: Direct COLREGsConstraint → M5 → AvoidancePlan ────────────────────
// Bypasses the M6 topic mismatch: publishes directly to M5's actual sub topic.
// Also publishes BehaviorPlan and OwnShipState prerequisites for M5.
TEST_F(Int001Test, INT001_M5_ReceivesConstraintOnActualTopic) {
  bool received = false;
  l3_msgs::msg::AvoidancePlan last_plan{};

  auto plan_sub = node_->create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      [&received, &last_plan](
          const l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        last_plan = *msg;
        received  = true;
      });

  // Publish all four M5 hard prerequisites at ~4 Hz until M5 responds or timeout.
  auto publish_timer = make_m5_prerequisite_publisher();

  bool ok = spin_until([&received]() { return received; }, kSpinTimeout);

  plan_sub.reset();  // explicit lifetime: prevent callbacks during assertion

  ASSERT_TRUE(ok)
      << "INT-001: M5 did not publish AvoidancePlan within "
      << kSpinTimeout.count() << "s when fed directly on /m6/colregs_constraint";
  EXPECT_GT(last_plan.confidence, 0.0F)
      << "INT-001: AvoidancePlan.confidence must be > 0";
}

// ── Test 3: Full chain exposes M6→M5 topic mismatch ──────────────────────────
// INTEGRATION BUG: M6 publishes /l3/m6/colregs_constraint but M5 subscribes
// /m6/colregs_constraint. Fix: align topic names in RFC-001 or add remapping
// in tools/ci/full_l3_stack.launch.py.
//
// This test asserts that M6's output reaches its own topic but does NOT reach
// M5's input topic — making the mismatch visible as a test assertion rather
// than a silent runtime gap.
TEST_F(Int001Test, INT001_FullChain_TopicMismatch) {
  bool received_on_l3_topic = false;   // M6's publish topic
  bool received_on_m5_topic = false;   // M5's subscribe topic

  auto l3_sub = node_->create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", 10,
      [&received_on_l3_topic](
          const l3_msgs::msg::COLREGsConstraint::SharedPtr /*msg*/) {
        received_on_l3_topic = true;
      });

  auto m5_sub = node_->create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", 10,
      [&received_on_m5_topic](
          const l3_msgs::msg::COLREGsConstraint::SharedPtr /*msg*/) {
        received_on_m5_topic = true;
      });

  // Publish WorldState + ODDState to trigger M6.
  auto publish_timer = make_world_odd_publisher();

  // Give the chain up to 2 s — enough for M6 to react but capped for CI.
  spin_until([&received_on_l3_topic]() { return received_on_l3_topic; },
             kMismatchTimeout);

  l3_sub.reset();   // explicit lifetime: prevent callbacks during assertion
  m5_sub.reset();   // explicit lifetime: prevent callbacks during assertion

  // M6 MUST have published to its own topic.
  EXPECT_TRUE(received_on_l3_topic)
      << "INT-001: M6 did not publish to /l3/m6/colregs_constraint within "
      << kMismatchTimeout.count() << "s";

  // M5 must NOT have received anything via the broken chain.
  // INTEGRATION BUG: M6 publishes /l3/m6/colregs_constraint but M5 subscribes
  // /m6/colregs_constraint. Fix: align topic names in RFC-001 or add
  // topic remapping in tools/ci/full_l3_stack.launch.py.
  EXPECT_FALSE(received_on_m5_topic)
      << "INT-001: /m6/colregs_constraint unexpectedly received a message — "
         "topic mismatch bug may have been fixed; update this test accordingly";
}
