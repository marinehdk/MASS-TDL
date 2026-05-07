// INT-007: All modules → ASDR full coverage
// Scene: Subscribe /l3/asdr/record; publish minimal stimulus to trigger each
//        module. Collect records for 5s; assert ≥1 record per module.
//        Verify SHA-256 signature: 32 bytes, not all zeros.
//
// Source module strings (from source code):
//   M1: "M1_ODD_Manager"           (odd_envelope_manager_node.cpp)
//   M2: "M2_World_Model"           (world_model_node.cpp)
//   M3: "M3_Mission_Manager"       (mission_manager_node.cpp)
//   M5: "M5_Tactical_Planner"      (mid_mpc_node.cpp); "M5_BC_MPC" (bc_mpc_node.cpp)
//   M6: "M6_COLREGs_Reasoner"      (colregs_reasoner_node.cpp)
//   M7: "M7_Safety_Supervisor"     (alert_generator.cpp)
//   M8: "M8_HMI_Transparency_Bridge" (asdr_logger.cpp)
//   NOTE: M4 BehaviorArbiter is not yet implemented (stub only); no M4 ASDR expected.
//
// ASDRRecord.msg: uint8[] signature — dynamic array, SHA-256 = 32 bytes.
//
// Per v1.1.2 §15 + RFC-004; MISRA C++:2023 PATH-D (full 179 rules).

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/own_ship_state.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"

// ── Constants (no magic numbers) ──────────────────────────────────────────────
constexpr float  kMockConfidence007       = 0.9F;
// Collection window: 5s to allow all modules to emit at least one ASDR record.
constexpr auto   kCollectTimeout007       = std::chrono::seconds(5);
constexpr auto   kSpinStep007             = std::chrono::milliseconds(100);
constexpr auto   kPublishInterval007      = std::chrono::milliseconds(250);
// SHA-256 signature length in bytes.
constexpr std::size_t kSha256Bytes        = 32U;
// Modules with known ASDR implementations (M4 is stub-only, excluded).
// Values match source_module strings in the respective node source files.
const std::vector<std::string> kExpectedModules = {
    "M1_ODD_Manager",
    "M2_World_Model",
    "M3_Mission_Manager",
    "M5_Tactical_Planner",
    "M6_COLREGs_Reasoner",
    "M7_Safety_Supervisor",
    "M8_HMI_Transparency_Bridge",
};
constexpr double kOwnShipSog007          = 15.0;
constexpr double kOwnShipCog007          = 90.0;
constexpr double kDepartureLat007        = 30.0;
constexpr double kDepartureLon007        = 122.0;
constexpr double kDestinationLat007      = 31.0;
constexpr double kDestinationLon007      = 122.0;
constexpr double kEtaEarliest007         = 3600.0;
constexpr double kEtaLatest007           = 10800.0;
constexpr double kTotalDistanceNm007     = 60.0;
constexpr double kEstimatedDurationS007  = 21600.0;
constexpr float  kBehaviorSpeedMin007    = 0.0F;
constexpr float  kBehaviorSpeedMax007    = 20.0F;
constexpr float  kBehaviorHeadingMin007  = 0.0F;
constexpr float  kBehaviorHeadingMax007  = 360.0F;
constexpr double kKnotsToMps007          = 0.514444;

// ── GTest Fixture ─────────────────────────────────────────────────────────────
class Int007Test : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("int007_test_node");
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
      executor_->spin_some(kSpinStep007);
    }
    return condition();
  }

  // ODDState: healthy, ODD_ZONE_A, D3, ENVELOPE_IN.
  l3_msgs::msg::OddState make_odd_state() const {
    l3_msgs::msg::OddState odd{};
    odd.stamp             = node_->now();
    odd.current_zone      = l3_msgs::msg::OddState::ODD_ZONE_A;
    odd.auto_level        = l3_msgs::msg::OddState::AUTO_LEVEL_D3;
    odd.health            = l3_msgs::msg::OddState::HEALTH_FULL;
    odd.envelope_state    = l3_msgs::msg::OddState::ENVELOPE_IN;
    odd.conformance_score = kMockConfidence007;
    odd.confidence        = kMockConfidence007;
    odd.rationale         = "int007_odd";
    return odd;
  }

  // WorldState: own ship, open water, no targets.
  l3_msgs::msg::WorldState make_world_state() const {
    l3_msgs::msg::WorldState ws{};
    ws.stamp      = node_->now();
    ws.confidence = kMockConfidence007;
    ws.rationale  = "int007_world_state";
    ws.own_ship.sog_kn      = kOwnShipSog007;
    ws.own_ship.cog_deg     = kOwnShipCog007;
    ws.own_ship.heading_deg = kOwnShipCog007;
    ws.own_ship.nav_mode    = "OPTIMAL";
    ws.own_ship.position.latitude  = 30.5;
    ws.own_ship.position.longitude = 122.0;
    ws.zone.zone_type         = "open_water";
    ws.zone.in_tss            = false;
    ws.zone.in_narrow_channel = false;
    return ws;
  }

  // VoyageTask: minimal valid task.
  l3_external_msgs::msg::VoyageTask make_voyage_task() const {
    l3_external_msgs::msg::VoyageTask t{};
    t.stamp = node_->now();
    t.task_id = 7UL;
    t.departure.latitude   = kDepartureLat007;
    t.departure.longitude  = kDepartureLon007;
    t.destination.latitude  = kDestinationLat007;
    t.destination.longitude = kDestinationLon007;
    t.eta_window.earliest_epoch_s = kEtaEarliest007;
    t.eta_window.latest_epoch_s   = kEtaLatest007;
    t.optimization_priority = "fuel_optimal";
    t.confidence  = kMockConfidence007;
    t.rationale   = "int007_voyage_task";
    return t;
  }

  // PlannedRoute: minimal valid route.
  l3_external_msgs::msg::PlannedRoute make_planned_route() const {
    l3_external_msgs::msg::PlannedRoute r{};
    r.stamp              = node_->now();
    r.route_id           = 1UL;
    r.total_distance_nm  = kTotalDistanceNm007;
    r.estimated_duration_s = kEstimatedDurationS007;
    r.confidence         = kMockConfidence007;
    r.rationale          = "int007_route";
    return r;
  }

  // BehaviorPlan for M5/M7 prerequisites.
  l3_msgs::msg::BehaviorPlan make_behavior_plan() const {
    l3_msgs::msg::BehaviorPlan bp{};
    bp.stamp           = node_->now();
    bp.behavior        = l3_msgs::msg::BehaviorPlan::BEHAVIOR_COLREG_AVOID;
    bp.heading_min_deg = kBehaviorHeadingMin007;
    bp.heading_max_deg = kBehaviorHeadingMax007;
    bp.speed_min_kn    = kBehaviorSpeedMin007;
    bp.speed_max_kn    = kBehaviorSpeedMax007;
    bp.confidence      = kMockConfidence007;
    bp.rationale       = "int007_behavior_plan";
    return bp;
  }

  // COLREGsConstraint for M5/M7 prerequisites.
  l3_msgs::msg::COLREGsConstraint make_colregs_constraint() const {
    l3_msgs::msg::COLREGsConstraint c{};
    c.stamp             = node_->now();
    c.phase             = "nominal";
    c.conflict_detected = false;
    c.confidence        = kMockConfidence007;
    c.rationale         = "int007_colregs";
    return c;
  }

  // OwnShipState for M5 mid-MPC.
  l3_msgs::msg::OwnShipState make_own_ship_state() const {
    l3_msgs::msg::OwnShipState s{};
    s.stamp       = node_->now();
    s.sog_kn      = kOwnShipSog007;
    s.cog_deg     = kOwnShipCog007;
    s.heading_deg = kOwnShipCog007;
    s.u_water     = kOwnShipSog007 * kKnotsToMps007;
    s.nav_mode    = "OPTIMAL";
    return s;
  }

  // SATData for M8 trigger (M8's on_sat_data feeds SatAggregator).
  l3_msgs::msg::SATData make_sat_data() const {
    l3_msgs::msg::SATData sat{};
    sat.stamp         = node_->now();
    sat.source_module = "M7_Safety_Supervisor";
    sat.sat1.state_summary = "TRANSIT @ D3, ODD-A";
    sat.sat2.trigger_reason   = "periodic";
    sat.sat2.reasoning_chain  = "nominal";
    sat.sat2.system_confidence = kMockConfidence007;
    sat.sat3.forecast_horizon_s   = 30.0;
    sat.sat3.predicted_state      = "TRANSIT";
    sat.sat3.prediction_uncertainty = 0.1F;
    sat.sat3.tdl_s = 60.0F;
    sat.sat3.tmr_s = 60.0F;
    return sat;
  }

  std::shared_ptr<rclcpp::Node>                              node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

// ── Test 1: All implemented modules publish ≥1 ASDR record within 5s ──────────
// Subscribe /l3/asdr/record; publish minimal stimulus to all modules.
// Collect for 5s; assert ≥1 record per expected module (M4 excluded: stub only).
TEST_F(Int007Test, INT007_AllModules_PublishASDR) {
  std::set<std::string> modules_seen{};

  auto asdr_sub = node_->create_subscription<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record",
      rclcpp::QoS(50).reliable().transient_local(),
      [&modules_seen](const l3_msgs::msg::ASDRRecord::SharedPtr msg) {
        modules_seen.insert(msg->source_module);
      });

  // Publishers for all modules' prerequisite inputs, published at 4 Hz.
  // M1: /l3/m2/world_state (world state), /fusion/own_ship_state, /fusion/environment_state
  // M2: /fusion/own_ship_state (filtered), /fusion/environment_state, AIS etc.
  // M3: /l1/voyage_task, /l2/planned_route, /l3/m1/odd_state, /l3/m2/world_state
  // M5: /m2/own_ship_state, /m2/tracked_targets, /m4/behavior_plan,
  //     /m6/colregs_constraint, /l2/planned_route
  // M6: /l3/m2/world_state, /l3/m1/odd_state
  // M7: /l3/m2/world_state, /l3/m4/behavior_plan, /l3/m6/colregs_constraint
  // M8: /l3/sat/data, /l3/m1/odd_state, /l3/m2/world_state

  auto odd_pub  = node_->create_publisher<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
  auto ws_pub   = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SystemDefaultsQoS().keep_last(2));
  auto vt_pub   = node_->create_publisher<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task", rclcpp::QoS(50).reliable().transient_local());
  auto pr_pub   = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", rclcpp::QoS(5).reliable());
  auto bp_pub_l3 = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", rclcpp::QoS(5).reliable());
  auto cc_pub_l3 = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", rclcpp::QoS(5).reliable());
  // M5 mid-MPC topics (no /l3/ prefix).
  auto os_pub_m5 = node_->create_publisher<l3_msgs::msg::OwnShipState>(
      "/m2/own_ship_state", 10);
  auto tt_pub_m5 = node_->create_publisher<l3_external_msgs::msg::TrackedTargetArray>(
      "/m2/tracked_targets", 10);
  auto bp_pub_m5 = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/m4/behavior_plan", 10);
  auto cc_pub_m5 = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
      "/m6/colregs_constraint", 10);
  // M8: /l3/sat/data.
  auto sat_pub = node_->create_publisher<l3_msgs::msg::SATData>(
      "/l3/sat/data", rclcpp::SystemDefaultsQoS().keep_last(2));

  auto publish_timer = node_->create_wall_timer(
      kPublishInterval007,
      [odd_pub, ws_pub, vt_pub, pr_pub, bp_pub_l3, cc_pub_l3,
       os_pub_m5, tt_pub_m5, bp_pub_m5, cc_pub_m5, sat_pub, this]() {
        odd_pub->publish(make_odd_state());
        ws_pub->publish(make_world_state());
        vt_pub->publish(make_voyage_task());
        pr_pub->publish(make_planned_route());
        bp_pub_l3->publish(make_behavior_plan());
        cc_pub_l3->publish(make_colregs_constraint());
        os_pub_m5->publish(make_own_ship_state());
        l3_external_msgs::msg::TrackedTargetArray arr{};
        arr.stamp = node_->now();
        arr.confidence = kMockConfidence007;
        tt_pub_m5->publish(arr);
        bp_pub_m5->publish(make_behavior_plan());
        cc_pub_m5->publish(make_colregs_constraint());
        sat_pub->publish(make_sat_data());
      });

  // Collect for up to 5s; stop early if all expected modules have reported.
  spin_until(
      [&modules_seen]() {
        for (const auto& m : kExpectedModules) {
          // M5 may appear as either "M5_Tactical_Planner" or "M5_BC_MPC".
          bool m5_found = (modules_seen.count("M5_Tactical_Planner") > 0U) ||
                          (modules_seen.count("M5_BC_MPC") > 0U);
          if (m == "M5_Tactical_Planner") {
            if (!m5_found) { return false; }
          } else {
            if (modules_seen.count(m) == 0U) { return false; }
          }
        }
        return true;
      },
      kCollectTimeout007);

  asdr_sub.reset();

  for (const auto& expected : kExpectedModules) {
    if (expected == "M5_Tactical_Planner") {
      const bool m5_found = (modules_seen.count("M5_Tactical_Planner") > 0U) ||
                            (modules_seen.count("M5_BC_MPC") > 0U);
      EXPECT_TRUE(m5_found)
          << "INT-007: No ASDR record from M5 (expected M5_Tactical_Planner "
             "or M5_BC_MPC) within "
          << kCollectTimeout007.count() << "s";
    } else {
      EXPECT_GT(modules_seen.count(expected), 0U)
          << "INT-007: No ASDR record from module '" << expected << "' within "
          << kCollectTimeout007.count() << "s";
    }
  }
}

// ── Test 2: ASDR SHA-256 signature is 32 bytes and non-zero ───────────────────
// For each received ASDRRecord that has a non-empty signature, assert:
//   (a) signature.size() == 32 (SHA-256 is 32 bytes)
//   (b) signature is not all zeros (actual hash was computed)
// ASDRRecord.msg: uint8[] signature (dynamic array, not uint8[32]).
TEST_F(Int007Test, INT007_ASDR_SHA256_Signature_Valid) {
  std::vector<l3_msgs::msg::ASDRRecord> records_with_sig{};

  auto asdr_sub = node_->create_subscription<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record",
      rclcpp::QoS(50).reliable().transient_local(),
      [&records_with_sig](const l3_msgs::msg::ASDRRecord::SharedPtr msg) {
        if (!msg->signature.empty()) {
          records_with_sig.push_back(*msg);
        }
      });

  // Publish stimulus to trigger all modules.
  auto odd_pub  = node_->create_publisher<l3_msgs::msg::OddState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
  auto ws_pub   = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SystemDefaultsQoS().keep_last(2));
  auto vt_pub   = node_->create_publisher<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task", rclcpp::QoS(50).reliable().transient_local());
  auto pr_pub   = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", rclcpp::QoS(5).reliable());
  auto bp_pub   = node_->create_publisher<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", rclcpp::QoS(5).reliable());
  auto cc_pub   = node_->create_publisher<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", rclcpp::QoS(5).reliable());
  auto sat_pub  = node_->create_publisher<l3_msgs::msg::SATData>(
      "/l3/sat/data", rclcpp::SystemDefaultsQoS().keep_last(2));

  auto publish_timer = node_->create_wall_timer(
      kPublishInterval007,
      [odd_pub, ws_pub, vt_pub, pr_pub, bp_pub, cc_pub, sat_pub, this]() {
        odd_pub->publish(make_odd_state());
        ws_pub->publish(make_world_state());
        vt_pub->publish(make_voyage_task());
        pr_pub->publish(make_planned_route());
        bp_pub->publish(make_behavior_plan());
        cc_pub->publish(make_colregs_constraint());
        sat_pub->publish(make_sat_data());
      });

  // Collect for 5s.
  spin_until([]() { return false; }, kCollectTimeout007);

  asdr_sub.reset();

  // Verify every signed record.
  ASSERT_FALSE(records_with_sig.empty())
      << "INT-007: No ASDRRecord with non-empty signature received within "
      << kCollectTimeout007.count() << "s";

  for (const auto& rec : records_with_sig) {
    EXPECT_EQ(rec.signature.size(), kSha256Bytes)
        << "INT-007: ASDRRecord from '" << rec.source_module
        << "' has signature.size()=" << rec.signature.size()
        << "; expected " << kSha256Bytes << " (SHA-256)";

    const bool all_zero = std::all_of(
        rec.signature.begin(), rec.signature.end(),
        [](uint8_t b) { return b == 0U; });
    EXPECT_FALSE(all_zero)
        << "INT-007: ASDRRecord from '" << rec.source_module
        << "' has all-zero signature — SHA-256 not computed";
  }
}
