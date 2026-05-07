// INT-005: Hardware Override → M1 OVERRIDDEN
// INT-006: M5 dual-interface — mid-MPC (AvoidancePlan) + BC-MPC (ReactiveOverrideCmd)
//
// INT-005 Scene: OverrideActiveSignal fires → M1 enters Overridden state.
// INT-006 Scene: M5 mid-MPC publishes AvoidancePlan when fed prerequisites;
//               M5 BC-MPC publishes ReactiveOverrideCmd when fed WorldState
//               with a near-CPA target that triggers the override branch.
//               Mode-switch timing is advisory (non-RT test env).
//
// EnvelopeState::Overridden = 5 (types.hpp; not a constant in ODDState.msg).
// M5 mid-MPC publish topic: /m5/avoidance_plan
// M5 BC-MPC publish topic:  /m5/reactive_override_cmd
//
// Per v1.1.2 §10 + RFC-001 scheme B + §5; MISRA C++:2023 PATH-D (full 179 rules).

#include <chrono>
#include <functional>
#include <memory>

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "l3_msgs/msg/encounter_classification.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"

// ── Constants (no magic numbers) ──────────────────────────────────────────────
// EnvelopeState::Overridden = 5 per types.hpp (not in ODDState.msg constants).
constexpr uint8_t kEnvelopeStateOverridden005    = 5U;

constexpr float  kMockConfidence005              = 0.9F;
constexpr auto   kSpinTimeout005                 = std::chrono::seconds(2);
constexpr auto   kSpinTimeout006                 = std::chrono::seconds(3);
constexpr auto   kSpinStep005                    = std::chrono::milliseconds(100);
constexpr auto   kPublishInterval005             = std::chrono::milliseconds(250);
// Advisory mode-switch timing: < 100ms per RFC-001 (non-RT test env).
constexpr int64_t kModeSwitchAdvisoryMs          = 100;
constexpr double  kOwnShipSog005                 = 15.0;
constexpr double  kOwnShipCog005                 = 90.0;
// Near-CPA distance for BC-MPC activation (< kCpaSafeFallback_m = 1852m).
constexpr double  kNearCpaDist005                = 200.0;
constexpr double  kNearCpaTcpa005                = 20.0;    // 20 s to CPA
constexpr double  kTargetSog005                  = 10.0;
constexpr double  kTargetCog005                  = 270.0;
constexpr double  kTotalDistanceNm005            = 60.0;
constexpr double  kEstimatedDurationS005         = 21600.0;
constexpr float   kBehaviorSpeedMin005           = 0.0F;
constexpr float   kBehaviorSpeedMax005           = 20.0F;
constexpr float   kBehaviorHeadingMin005         = 0.0F;
constexpr float   kBehaviorHeadingMax005         = 360.0F;
constexpr double  kKnotsToMps005                 = 0.514444;

// ── GTest Fixture ─────────────────────────────────────────────────────────────
class Int005006Test : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("int005006_test_node");
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
      executor_->spin_some(kSpinStep005);
    }
    return condition();
  }

  // OverrideActiveSignal: override engaged.
  l3_external_msgs::msg::OverrideActiveSignal make_override_active() const {
    l3_external_msgs::msg::OverrideActiveSignal sig{};
    sig.stamp             = node_->now();
    sig.override_active   = true;
    sig.activation_source = "manual_button";
    return sig;
  }

  // WorldState: own ship only, open water, no targets.
  l3_msgs::msg::WorldState make_world_state_normal() const {
    l3_msgs::msg::WorldState ws{};
    ws.stamp      = node_->now();
    ws.confidence = kMockConfidence005;
    ws.rationale  = "int005006_world_state";
    ws.own_ship.sog_kn      = kOwnShipSog005;
    ws.own_ship.cog_deg     = kOwnShipCog005;
    ws.own_ship.heading_deg = kOwnShipCog005;
    ws.own_ship.nav_mode    = "OPTIMAL";
    ws.zone.zone_type         = "open_water";
    ws.zone.in_tss            = false;
    ws.zone.in_narrow_channel = false;
    return ws;
  }

  // WorldState with a near-CPA target to activate BC-MPC override branch.
  // BC-MPC activates when BcMpcSolution::Status == Override, triggered when
  // CPA < kCpaSafeFallback_m (1852 m) in the solver.
  l3_msgs::msg::WorldState make_world_state_near_cpa() const {
    l3_msgs::msg::WorldState ws = make_world_state_normal();
    ws.rationale = "int005006_near_cpa";
    l3_msgs::msg::TrackedTarget tgt{};
    tgt.stamp             = node_->now();
    tgt.target_id         = 1U;
    tgt.sog_kn            = kTargetSog005;
    tgt.cog_deg           = kTargetCog005;
    tgt.heading_deg       = kTargetCog005;
    tgt.classification    = "vessel";
    tgt.classification_confidence = kMockConfidence005;
    tgt.cpa_m             = kNearCpaDist005;
    tgt.tcpa_s            = kNearCpaTcpa005;
    tgt.confidence        = kMockConfidence005;
    tgt.source_sensor     = "fused";
    tgt.encounter.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON;
    tgt.encounter.relative_bearing_deg = 0.0;
    tgt.encounter.aspect_angle_deg     = 180.0;
    tgt.encounter.is_giveway           = true;
    ws.targets.push_back(tgt);
    return ws;
  }

  // OwnShipState for M5 mid-MPC /m2/own_ship_state subscription.
  l3_msgs::msg::OwnShipState make_own_ship_state() const {
    l3_msgs::msg::OwnShipState s{};
    s.stamp       = node_->now();
    s.sog_kn      = kOwnShipSog005;
    s.cog_deg     = kOwnShipCog005;
    s.heading_deg = kOwnShipCog005;
    s.u_water     = kOwnShipSog005 * kKnotsToMps005;
    s.nav_mode    = "OPTIMAL";
    return s;
  }

  // TrackedTargetArray for M5 mid-MPC /m2/tracked_targets subscription.
  l3_external_msgs::msg::TrackedTargetArray make_tracked_target_array() const {
    l3_external_msgs::msg::TrackedTargetArray arr{};
    arr.stamp      = node_->now();
    arr.confidence = kMockConfidence005;
    arr.rationale  = "int005006_test";
    l3_msgs::msg::TrackedTarget tgt{};
    tgt.stamp             = node_->now();
    tgt.target_id         = 1U;
    tgt.sog_kn            = kTargetSog005;
    tgt.cog_deg           = kTargetCog005;
    tgt.heading_deg       = kTargetCog005;
    tgt.classification    = "vessel";
    tgt.classification_confidence = kMockConfidence005;
    tgt.cpa_m             = kNearCpaDist005;
    tgt.tcpa_s            = kNearCpaTcpa005;
    tgt.confidence        = kMockConfidence005;
    tgt.source_sensor     = "fused";
    tgt.encounter.encounter_type =
        l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_HEAD_ON;
    tgt.encounter.relative_bearing_deg = 0.0;
    tgt.encounter.aspect_angle_deg     = 180.0;
    tgt.encounter.is_giveway           = true;
    arr.targets.push_back(tgt);
    return arr;
  }

  // BehaviorPlan prerequisite for M5 mid-MPC.
  l3_msgs::msg::BehaviorPlan make_behavior_plan() const {
    l3_msgs::msg::BehaviorPlan bp{};
    bp.stamp           = node_->now();
    bp.behavior        = l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID;
    bp.heading_min_deg = kBehaviorHeadingMin005;
    bp.heading_max_deg = kBehaviorHeadingMax005;
    bp.speed_min_kn    = kBehaviorSpeedMin005;
    bp.speed_max_kn    = kBehaviorSpeedMax005;
    bp.confidence      = kMockConfidence005;
    bp.rationale       = "int005006_behavior_plan";
    return bp;
  }

  // COLREGsConstraint prerequisite for M5 mid-MPC /m6/colregs_constraint.
  l3_msgs::msg::COLREGsConstraint make_colregs_constraint() const {
    l3_msgs::msg::COLREGsConstraint c{};
    c.stamp             = node_->now();
    c.phase             = "T_act";
    c.conflict_detected = false;
    c.confidence        = kMockConfidence005;
    c.rationale         = "int005006_colregs";
    l3_msgs::msg::RuleActive rule14{};
    rule14.rule_id         = 14U;
    rule14.rule_phase      = "T_act";
    rule14.target_id       = 1U;
    rule14.rule_confidence = kMockConfidence005;
    rule14.rationale       = "HEAD_ON_giveway";
    c.active_rules.push_back(rule14);
    return c;
  }

  // PlannedRoute prerequisite for M5 mid-MPC /l2/planned_route.
  l3_external_msgs::msg::PlannedRoute make_planned_route() const {
    l3_external_msgs::msg::PlannedRoute r{};
    r.stamp              = node_->now();
    r.route_id           = 1UL;
    r.total_distance_nm  = kTotalDistanceNm005;
    r.estimated_duration_s = kEstimatedDurationS005;
    r.confidence         = kMockConfidence005;
    r.rationale          = "int005006_planned_route";
    return r;
  }

  std::shared_ptr<rclcpp::Node>                              node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

// ── INT-005 Test 1: Hardware Override → M1 enters Overridden state ─────────────
// Publish OverrideActiveSignal(override_active=true) to /l3/override/active.
// Subscribe /l3/m1/odd_state; assert envelope_state == 5 (Overridden) within 2s.
// M1's on_override_signal immediately triggers state machine → Overridden.
TEST_F(Int005006Test, INT005_HardwareOverride_M1_EntersOverridden) {
  bool overridden_received = false;
  l3_msgs::msg::OddState last_odd{};

  auto odd_sub = node_->create_subscription<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      [&overridden_received, &last_odd](
          const l3_msgs::msg::OddState::SharedPtr msg) {
        last_odd = *msg;
        // kEnvelopeStateOverridden005 = 5 per types.hpp EnvelopeState::Overridden.
        if (msg->envelope_state == kEnvelopeStateOverridden005) {
          overridden_received = true;
        }
      });

  // Provide M1 WorldState prerequisite at 4 Hz.
  auto ws_pub = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::QoS(10).reliable());
  auto ws_timer = node_->create_wall_timer(
      kPublishInterval005,
      [ws_pub, this]() { ws_pub->publish(make_world_state_normal()); });

  auto override_pub = node_->create_publisher<
      l3_external_msgs::msg::OverrideActiveSignal>(
      "/l3/override/active", rclcpp::QoS(50).reliable());
  auto override_timer = node_->create_wall_timer(
      kPublishInterval005,
      [override_pub, this]() { override_pub->publish(make_override_active()); });

  bool ok = spin_until([&overridden_received]() { return overridden_received; },
                       kSpinTimeout005);

  odd_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-005: M1 did not reach envelope_state=Overridden(5) within "
      << kSpinTimeout005.count() << "s after OverrideActiveSignal(active=true)";
  EXPECT_EQ(last_odd.envelope_state, kEnvelopeStateOverridden005)
      << "INT-005: M1 envelope_state must be 5 (Overridden per types.hpp)";
}

// ── INT-006 Test 1: M5 mid-MPC publishes AvoidancePlan in normal mode ──────────
// Publish all M5 mid-MPC prerequisites: OwnShipState, TrackedTargetArray,
// BehaviorPlan, COLREGsConstraint, PlannedRoute. Also publish WorldState
// on /m2/world_state (M5 BC-MPC uses /m2/world_state).
// Subscribe /m5/avoidance_plan; assert AvoidancePlan received within 3s.
// M5 mid-MPC runs a 1 Hz solve_timer_.
TEST_F(Int005006Test, INT006_M5_AvoidancePlan_NormalMode) {
  bool received = false;
  l3_msgs::msg::AvoidancePlan last_plan{};

  auto plan_sub = node_->create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      [&received, &last_plan](const l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        last_plan = *msg;
        received  = true;
      });

  // M5 mid-MPC subscribes: /m2/own_ship_state, /m2/tracked_targets,
  // /m4/behavior_plan, /m6/colregs_constraint, /l2/planned_route, /l2/speed_profile.
  auto os_pub = node_->create_publisher<l3_msgs::msg::OwnShipState>(
      "/m2/own_ship_state", 10);
  auto tt_pub = node_->create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
      "/m2/tracked_targets", 10);
  auto bp_pub = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/m4/behavior_plan", 10);
  auto cc_pub = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", 10);
  auto pr_pub = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", 10);

  auto prereq_timer = node_->create_wall_timer(
      kPublishInterval005,
      [os_pub, tt_pub, bp_pub, cc_pub, pr_pub, this]() {
        os_pub->publish(make_own_ship_state());
        tt_pub->publish(make_tracked_target_array());
        bp_pub->publish(make_behavior_plan());
        cc_pub->publish(make_colregs_constraint());
        pr_pub->publish(make_planned_route());
      });

  bool ok = spin_until([&received]() { return received; }, kSpinTimeout006);

  plan_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-006: M5 mid-MPC did not publish AvoidancePlan within "
      << kSpinTimeout006.count() << "s when fed prerequisites on actual topics";
  EXPECT_GT(last_plan.confidence, 0.0F)
      << "INT-006: AvoidancePlan.confidence must be > 0";
}

// ── INT-006 Test 2: M5 BC-MPC publishes ReactiveOverrideCmd ───────────────────
// M5 BC-MPC subscribes /m2/world_state and /m5/avoidance_plan.
// It publishes /m5/reactive_override_cmd when the solver detects Override.
// Trigger: WorldState with near-CPA target (cpa_m < kCpaSafeFallback_m = 1852 m)
//         + AvoidancePlan from mid-MPC (or direct mock).
// Assert: ReactiveOverrideCmd received within 3s.
TEST_F(Int005006Test, INT006_M5_ReactiveOverride_Mode) {
  bool received = false;
  l3_msgs::msg::ReactiveOverrideCmd last_cmd{};

  auto cmd_sub = node_->create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
      "/m5/reactive_override_cmd", 10,
      [&received, &last_cmd](
          const l3_msgs::msg::ReactiveOverrideCmd::SharedPtr msg) {
        last_cmd = *msg;
        received = true;
      });

  // M5 BC-MPC subscribes /m2/world_state (not /l3/m2/world_state).
  auto ws_pub = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/m2/world_state", 10);

  // M5 BC-MPC also subscribes /m5/avoidance_plan (from mid-MPC).
  // Publish a mock AvoidancePlan to allow BC-MPC to proceed.
  auto ap_pub = node_->create_publisher<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10);
  l3_msgs::msg::AvoidancePlan mock_plan{};
  mock_plan.stamp      = node_->now();
  mock_plan.confidence = kMockConfidence005;
  mock_plan.rationale  = "int006_mock_avoidance_plan";

  // Publish near-CPA WorldState at 10 Hz (BC-MPC runs on each on_world_state).
  auto combined_timer = node_->create_wall_timer(
      std::chrono::milliseconds(100),
      [ws_pub, ap_pub, &mock_plan, this]() {
        ws_pub->publish(make_world_state_near_cpa());
        mock_plan.stamp = node_->now();
        ap_pub->publish(mock_plan);
      });

  bool ok = spin_until([&received]() { return received; }, kSpinTimeout006);

  cmd_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-006: M5 BC-MPC did not publish ReactiveOverrideCmd within "
      << kSpinTimeout006.count() << "s after near-CPA WorldState injection";
  EXPECT_GT(last_cmd.confidence, 0.0F)
      << "INT-006: ReactiveOverrideCmd.confidence must be > 0";
}

// ── INT-006 Test 3: Tri-mode timing — advisory in non-RT test environment ──────
// Measure time between publishing COLREGsConstraint trigger and receiving
// AvoidancePlan from M5 mid-MPC. Advisory threshold: < 100ms per RFC-001.
// NOTE: non-RT GTest environment; EXPECT not ASSERT.
// Performance constraint: < 100ms per RFC-001; advisory in test env.
TEST_F(Int005006Test, INT006_TriMode_Timing) {
  bool plan_received = false;
  std::chrono::steady_clock::time_point t_received{};

  auto plan_sub = node_->create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/m5/avoidance_plan", 10,
      [&plan_received, &t_received](
          const l3_msgs::msg::AvoidancePlan::SharedPtr /*msg*/) {
        if (!plan_received) {
          t_received   = std::chrono::steady_clock::now();
          plan_received = true;
        }
      });

  // Publish M5 prerequisites.
  auto os_pub = node_->create_publisher<l3_msgs::msg::OwnShipState>(
      "/m2/own_ship_state", 10);
  auto tt_pub = node_->create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
      "/m2/tracked_targets", 10);
  auto bp_pub = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/m4/behavior_plan", 10);
  auto pr_pub = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", 10);

  auto prereq_timer = node_->create_wall_timer(
      kPublishInterval005,
      [os_pub, tt_pub, bp_pub, pr_pub, this]() {
        os_pub->publish(make_own_ship_state());
        tt_pub->publish(make_tracked_target_array());
        bp_pub->publish(make_behavior_plan());
        pr_pub->publish(make_planned_route());
      });

  // Record t0 immediately before publishing the COLREGsConstraint trigger.
  auto cc_pub = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", 10);
  auto t0 = std::chrono::steady_clock::now();
  cc_pub->publish(make_colregs_constraint());

  // Continuously re-publish trigger at 4 Hz.
  auto cc_timer = node_->create_wall_timer(
      kPublishInterval005,
      [cc_pub, this]() { cc_pub->publish(make_colregs_constraint()); });

  spin_until([&plan_received]() { return plan_received; }, kSpinTimeout006);

  plan_sub.reset();

  ASSERT_TRUE(plan_received)
      << "INT-006 (timing): M5 did not publish AvoidancePlan within "
      << kSpinTimeout006.count() << "s";

  const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      t_received - t0).count();

  // Performance constraint: < 100ms per RFC-001; advisory in test env.
  EXPECT_LE(latency_ms, kModeSwitchAdvisoryMs)
      << "INT-006: Mode-switch latency " << latency_ms << " ms exceeds advisory "
      << kModeSwitchAdvisoryMs << " ms (RFC-001). "
         "Non-RT test environment: this is advisory, not a hard failure.";
}
