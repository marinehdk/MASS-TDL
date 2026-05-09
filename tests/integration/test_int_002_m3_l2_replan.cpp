// INT-002: M3 Mission Manager → L2 replan (RFC-006)
// Scene: M3 is active, encounters a situation requiring route replanning.
// Expected flow: VoyageTask + ODDState(EDGE/CRITICAL) → M3 triggers
//                RouteReplanRequest → L2 replies with ReplanResponse(SUCCESS)
//                → M3 publishes new MissionGoal. All 4 status values exercised.
//
// Per v1.1.2 §7 + RFC-006; MISRA C++:2023 PATH-D (full 179 rules).

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include "rclcpp/rclcpp.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/time_window.hpp"

// ── Constants (no magic numbers) ──────────────────────────────────────────────
constexpr float  kMockConfidence002       = 0.9F;
constexpr float  kLowConfidence002        = 0.25F;   // < 0.3 critical threshold
constexpr auto   kSpinTimeout002          = std::chrono::seconds(3);
constexpr auto   kSpinStep002             = std::chrono::milliseconds(100);
constexpr auto   kPublishInterval002      = std::chrono::milliseconds(250);
constexpr auto   kWarmupDuration002       = std::chrono::seconds(1);
constexpr double kOwnShipSog002           = 15.0;
constexpr double kOwnShipCog002           = 90.0;
constexpr double kDepartureLat002         = 30.0;
constexpr double kDepartureLon002         = 122.0;
constexpr double kDestinationLat002       = 31.0;
constexpr double kDestinationLon002       = 122.0;
constexpr double kEtaWindowEarliest002    = 3600.0;   // 1 hour
constexpr double kEtaWindowLatest002      = 10800.0;  // 3 hours
constexpr double kOwnShipPositionLat002   = 30.5;
constexpr double kOwnShipPositionLon002   = 122.0;
constexpr double kTotalDistanceNm002      = 60.0;
constexpr double kEstimatedDurationS002   = 21600.0;

// ── GTest Fixture ─────────────────────────────────────────────────────────────
class Int002Test : public ::testing::Test {
 protected:
  void SetUp() override {
    if (!rclcpp::ok()) {
      rclcpp::init(0, nullptr);
    }
    node_ = std::make_shared<rclcpp::Node>("int002_test_node");
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
      executor_->spin_some(kSpinStep002);
    }
    return condition();
  }

  // Build a minimal VoyageTask.
  l3_external_msgs::msg::VoyageTask make_voyage_task() const {
    l3_external_msgs::msg::VoyageTask t{};
    t.stamp = node_->now();
    t.task_id = 42UL;
    t.departure.latitude   = kDepartureLat002;
    t.departure.longitude  = kDepartureLon002;
    t.destination.latitude  = kDestinationLat002;
    t.destination.longitude = kDestinationLon002;
    t.eta_window.earliest.sec = static_cast<int32_t>(kEtaWindowEarliest002);
    t.eta_window.latest.sec   = static_cast<int32_t>(kEtaWindowLatest002);
    t.optimization_priority = "fuel_optimal";
    t.confidence  = kMockConfidence002;
    t.rationale   = "int002_test_voyage_task";
    return t;
  }

  // Build a PlannedRoute to advance M3 AwaitingRoute → Active.
  l3_external_msgs::msg::PlannedRoute make_planned_route() const {
    l3_external_msgs::msg::PlannedRoute r{};
    r.stamp              = node_->now();
    r.route_id           = 1UL;
    r.total_distance_nm  = kTotalDistanceNm002;
    r.estimated_duration_s = kEstimatedDurationS002;
    r.confidence         = kMockConfidence002;
    r.rationale          = "int002_test_planned_route";
    return r;
  }

  // Normal ODDState (conformance high → M3 stays Active, no replan).
  l3_msgs::msg::ODDState make_odd_state_normal() const {
    l3_msgs::msg::ODDState odd{};
    odd.stamp             = node_->now();
    odd.current_zone      = l3_msgs::msg::ODDState::ODD_ZONE_A;
    odd.auto_level        = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
    odd.health            = l3_msgs::msg::ODDState::HEALTH_FULL;
    odd.envelope_state    = l3_msgs::msg::ODDState::ENVELOPE_IN;
    odd.conformance_score = kMockConfidence002;
    odd.confidence        = kMockConfidence002;
    odd.rationale         = "int002_normal";
    return odd;
  }

  // Critical ODDState (conformance_score < 0.3 critical threshold in M3 config)
  // → ReplanRequestTrigger fires with reason ODD_EXIT.
  l3_msgs::msg::ODDState make_odd_state_critical() const {
    l3_msgs::msg::ODDState odd{};
    odd.stamp             = node_->now();
    odd.current_zone      = l3_msgs::msg::ODDState::ODD_ZONE_A;
    odd.auto_level        = l3_msgs::msg::ODDState::AUTO_LEVEL_D3;
    odd.health            = l3_msgs::msg::ODDState::HEALTH_CRITICAL;
    odd.envelope_state    = l3_msgs::msg::ODDState::ENVELOPE_EDGE;
    odd.conformance_score = kLowConfidence002;
    odd.confidence        = kLowConfidence002;
    odd.rationale         = "int002_critical";
    return odd;
  }

  // WorldState: own ship at 15 kn, open water, no targets.
  l3_msgs::msg::WorldState make_world_state() const {
    l3_msgs::msg::WorldState ws{};
    ws.stamp      = node_->now();
    ws.confidence = kMockConfidence002;
    ws.rationale  = "int002_world_state";
    ws.own_ship.sog_kn      = kOwnShipSog002;
    ws.own_ship.cog_deg     = kOwnShipCog002;
    ws.own_ship.heading_deg = kOwnShipCog002;
    ws.own_ship.nav_mode    = "OPTIMAL";
    ws.own_ship.position.latitude  = kOwnShipPositionLat002;
    ws.own_ship.position.longitude = kOwnShipPositionLon002;
    ws.zone.zone_type         = "open_water";
    ws.zone.in_tss            = false;
    ws.zone.in_narrow_channel = false;
    return ws;
  }

  // ReplanResponse with the given status enum value.
  l3_external_msgs::msg::ReplanResponse make_replan_response(uint8_t status) const {
    l3_external_msgs::msg::ReplanResponse r{};
    r.stamp      = node_->now();
    r.status     = status;
    r.confidence = kMockConfidence002;
    r.rationale  = "int002_replan_response";
    return r;
  }

  std::shared_ptr<rclcpp::Node>                              node_;
  std::shared_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

// ── Test 1: M3 publishes RouteReplanRequest on critical ODDState ───────────────
// Initialise M3 with VoyageTask + PlannedRoute (Active), then inject a critical
// ODDState. Assert RouteReplanRequest received within 3s with confidence > 0.
TEST_F(Int002Test, INT002_M3_PublishesReplanRequest) {
  bool received = false;
  l3_msgs::msg::RouteReplanRequest last_request{};

  auto replan_sub =
      node_->create_subscription<l3_msgs::msg::RouteReplanRequest>(
          "/l3/m3/route_replan_request",
          rclcpp::QoS(50).reliable().transient_local(),
          [&received, &last_request](
              const l3_msgs::msg::RouteReplanRequest::SharedPtr msg) {
            last_request = *msg;
            received = true;
          });

  // Publishers for M3 prerequisites.
  auto vt_pub  = node_->create_publisher<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task", rclcpp::QoS(50).reliable().transient_local());
  auto pr_pub  = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", rclcpp::QoS(5).reliable());
  auto odd_pub = node_->create_publisher<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
  auto ws_pub  = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SystemDefaultsQoS().keep_last(2));

  // Phase 1: publish normal inputs at 4 Hz to activate M3.
  auto init_timer = node_->create_wall_timer(
      kPublishInterval002,
      [vt_pub, pr_pub, odd_pub, ws_pub, this]() {
        vt_pub->publish(make_voyage_task());
        pr_pub->publish(make_planned_route());
        odd_pub->publish(make_odd_state_normal());
        ws_pub->publish(make_world_state());
      });

  // Warmup: let M3 reach Active.
  spin_until([]() { return false; }, kWarmupDuration002);

  // Phase 2: switch to critical ODDState at 4 Hz to trigger replan.
  auto trigger_timer = node_->create_wall_timer(
      kPublishInterval002,
      [odd_pub, ws_pub, this]() {
        odd_pub->publish(make_odd_state_critical());
        ws_pub->publish(make_world_state());
      });

  bool ok = spin_until([&received]() { return received; }, kSpinTimeout002);

  replan_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-002: M3 did not publish RouteReplanRequest within "
      << kSpinTimeout002.count() << "s after critical ODDState injection";
  EXPECT_GT(last_request.confidence, 0.0F)
      << "INT-002: RouteReplanRequest.confidence must be > 0";
}

// ── Test 2: M3 publishes MissionGoal after ReplanResponse(SUCCESS) ─────────────
// Bring M3 to Active state; publish ReplanResponse(SUCCESS); confirm M3
// continues publishing MissionGoal to /l3/m3/mission_goal within 3s.
// M3's mission_goal timer fires at 0.5 Hz when state == Active.
TEST_F(Int002Test, INT002_M3_HandlesReplanSuccess) {
  bool mission_goal_received = false;
  l3_msgs::msg::MissionGoal last_goal{};

  auto goal_sub =
      node_->create_subscription<l3_msgs::msg::MissionGoal>(
          "/l3/m3/mission_goal",
          rclcpp::QoS(5).reliable(),  // depth 5: mission_goal rate ≤ 0.5 Hz; depth-5 sufficient within 3s spin window
          [&mission_goal_received, &last_goal](
              const l3_msgs::msg::MissionGoal::SharedPtr msg) {
            last_goal = *msg;
            mission_goal_received = true;
          });

  auto vt_pub  = node_->create_publisher<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task", rclcpp::QoS(50).reliable().transient_local());
  auto pr_pub  = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", rclcpp::QoS(5).reliable());
  auto odd_pub = node_->create_publisher<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
  auto ws_pub  = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SystemDefaultsQoS().keep_last(2));
  auto resp_pub = node_->create_publisher<l3_external_msgs::msg::ReplanResponse>(
      "/l2/replan_response", rclcpp::QoS(50).reliable().transient_local());

  // Publish all M3 prerequisites + SUCCESS response at 4 Hz.
  auto init_timer = node_->create_wall_timer(
      kPublishInterval002,
      [vt_pub, pr_pub, odd_pub, ws_pub, resp_pub, this]() {
        vt_pub->publish(make_voyage_task());
        pr_pub->publish(make_planned_route());
        odd_pub->publish(make_odd_state_normal());
        ws_pub->publish(make_world_state());
        resp_pub->publish(make_replan_response(
            l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS));
      });

  bool ok = spin_until([&mission_goal_received]() { return mission_goal_received; },
                       kSpinTimeout002);

  goal_sub.reset();

  ASSERT_TRUE(ok)
      << "INT-002: M3 did not publish MissionGoal within "
      << kSpinTimeout002.count() << "s after ReplanResponse(SUCCESS)";
  EXPECT_GE(last_goal.confidence, 0.0F)
      << "INT-002: MissionGoal.confidence must be >= 0";
}

// ── Test 3: All 4 ReplanResponse statuses produce ASDR output ─────────────────
// For each of the 4 ReplanResponse status enum values, publish the response and
// verify M3 does not crash (ASDR output confirms M3 processed the response).
// Statuses: STATUS_SUCCESS(0), STATUS_FAILED_TIMEOUT(1),
//           STATUS_FAILED_INFEASIBLE(2), STATUS_FAILED_NO_RESOURCES(3).
TEST_F(Int002Test, INT002_M3_HandlesAllReplanStatuses) {
  const std::vector<uint8_t> statuses = {
      l3_external_msgs::msg::ReplanResponse::STATUS_SUCCESS,
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_TIMEOUT,
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_INFEASIBLE,
      l3_external_msgs::msg::ReplanResponse::STATUS_FAILED_NO_RESOURCES,
  };

  // Bring M3 into Active state.
  auto vt_pub  = node_->create_publisher<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task", rclcpp::QoS(50).reliable().transient_local());
  auto pr_pub  = node_->create_publisher<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route", rclcpp::QoS(5).reliable());
  auto odd_pub = node_->create_publisher<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local());
  auto ws_pub  = node_->create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SystemDefaultsQoS().keep_last(2));
  auto resp_pub = node_->create_publisher<l3_external_msgs::msg::ReplanResponse>(
      "/l2/replan_response", rclcpp::QoS(50).reliable().transient_local());

  auto init_timer = node_->create_wall_timer(
      kPublishInterval002,
      [vt_pub, pr_pub, odd_pub, ws_pub, this]() {
        vt_pub->publish(make_voyage_task());
        pr_pub->publish(make_planned_route());
        odd_pub->publish(make_odd_state_normal());
        ws_pub->publish(make_world_state());
      });

  // Warmup: let M3 initialise into Active.
  spin_until([]() { return false; }, kWarmupDuration002);

  for (uint8_t status : statuses) {
    bool asdr_received = false;

    auto asdr_sub = node_->create_subscription<l3_msgs::msg::ASDRRecord>(
        "/l3/asdr/record",
        rclcpp::QoS(50).reliable().transient_local(),
        [&asdr_received](const l3_msgs::msg::ASDRRecord::SharedPtr /*msg*/) {
          asdr_received = true;
        });

    // Inject the replan response; M3 logs ASDR for every outcome.
    resp_pub->publish(make_replan_response(status));

    bool ok = spin_until([&asdr_received]() { return asdr_received; },
                         kSpinTimeout002);

    asdr_sub.reset();

    EXPECT_TRUE(ok)
        << "INT-002: M3 did not publish ASDR record within "
        << kSpinTimeout002.count() << "s for ReplanResponse status="
        << static_cast<unsigned>(status);
  }
}
