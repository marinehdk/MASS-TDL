#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <thread>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

#include "l3_external_msgs/msg/planned_route.hpp"

namespace mass_l3::m4 {

namespace {
constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr std::uint8_t kRoleBothGiveWay = 2U;
constexpr std::uint8_t kRoleFree = 3U;
using PlannedRouteMsg = l3_external_msgs::msg::PlannedRoute;
}  // namespace

class BehaviorArbiterTest : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }

  void trigger_odd_state(std::shared_ptr<BehaviorArbiterNode> node, const ODDStateMsg::SharedPtr msg) {
    node->on_odd_state(msg);
  }
  void trigger_world_state(std::shared_ptr<BehaviorArbiterNode> node, const WorldStateMsg::SharedPtr msg) {
    node->on_world_state(msg);
  }
  void trigger_mode_cmd(std::shared_ptr<BehaviorArbiterNode> node, const ModeCmdMsg::SharedPtr msg) {
    node->on_mode_cmd(msg);
  }
  void trigger_mission_goal(std::shared_ptr<BehaviorArbiterNode> node, const MissionGoalMsg::SharedPtr msg) {
    node->on_mission_goal(msg);
  }
  void trigger_colregs_constraint(std::shared_ptr<BehaviorArbiterNode> node, const COLREGsConstraintMsg::SharedPtr msg) {
    node->on_colregs_constraint(msg);
  }
  void trigger_arbitration(std::shared_ptr<BehaviorArbiterNode> node) {
    node->arbitration_timer_callback();
  }
  bool get_m3_active_latch(std::shared_ptr<BehaviorArbiterNode> node) {
    return node->m3_active_latch_;
  }
  bool get_fallback_anchor_set(std::shared_ptr<BehaviorArbiterNode> node) {
    return node->fallback_anchor_set_;
  }
  double get_fallback_anchor_hdg(std::shared_ptr<BehaviorArbiterNode> node) {
    return node->fallback_anchor_hdg_;
  }
  void trigger_rule_assessment(std::shared_ptr<BehaviorArbiterNode> node, const l3_msgs::msg::RuleAssessment::SharedPtr msg) {
    node->on_rule_assessment(msg);
  }
  void trigger_planned_route(std::shared_ptr<BehaviorArbiterNode> node, const PlannedRouteMsg::SharedPtr msg) {
    node->on_planned_route(msg);
  }
  float get_colreg_avoidance_weight(std::shared_ptr<BehaviorArbiterNode> node) {
    return node->colreg_avoidance_weight_;
  }
  double get_dictionary_priority_weight(std::shared_ptr<BehaviorArbiterNode> node, BehaviorType type) {
    const auto* desc = node->dictionary_.find(type);
    return desc ? desc->priority_weight : 0.0;
  }
  bool has_behavior(std::shared_ptr<BehaviorArbiterNode> node, BehaviorType type) {
    return node->dictionary_.find(type) != nullptr;
  }
  void add_behavior(std::shared_ptr<BehaviorArbiterNode> node, const BehaviorDescriptor& desc) {
    node->dictionary_.add_behavior(desc);
  }
  std::optional<double> parse_best_util(const std::string& rationale) {
    const std::string key = "best_util=";
    const auto start = rationale.find(key);
    if (start == std::string::npos) {
      return std::nullopt;
    }
    const auto value_start = start + key.size();
    const auto value_end = rationale.find(' ', value_start);
    try {
      return std::stod(rationale.substr(value_start, value_end - value_start));
    } catch (...) {
      return std::nullopt;
    }
  }

  template <typename Predicate>
  void spin_until(rclcpp::executors::SingleThreadedExecutor& executor, Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      executor.spin_some(std::chrono::milliseconds(10));
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }

  std::shared_ptr<BehaviorArbiterNode> make_node_with_immediate_ivp_timeout() {
    rclcpp::NodeOptions options;
    options.parameter_overrides({
        rclcpp::Parameter("m4.arbitration.ivp_timeout_ms", -1),
    });
    return std::make_shared<BehaviorArbiterNode>(options);
  }
};

// Node can be constructed
TEST_F(BehaviorArbiterTest, Construct) {
  auto node = std::make_shared<BehaviorArbiterNode>();
  ASSERT_NE(node, nullptr);
  EXPECT_STREQ(node->get_name(), "behavior_arbiter");
}

TEST_F(BehaviorArbiterTest, FallbackLatchingAndSafetyConcernPublishing) {
  auto node = make_node_with_immediate_ivp_timeout();

  // 1. Initially, M3 active latch is false, anchor is not set
  EXPECT_FALSE(get_m3_active_latch(node));
  EXPECT_FALSE(get_fallback_anchor_set(node));

  // 2. Setup inputs: ODD, WorldState (heading = 120.0 deg)
  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 120.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  // Setup MissionGoal: FSM_ACTIVE + TASK_VALIDITY_VALID (M3 Active and Valid)
  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  // Run callback to process state — first time, M3 is active and valid, latching should trigger
  trigger_arbitration(node);

  // M3 should now be latched as active
  EXPECT_TRUE(get_m3_active_latch(node));
  // Since M3 is active+valid (m3_task_valid is true), the anchor should NOT be set
  EXPECT_FALSE(get_fallback_anchor_set(node));

  // Setup COLREGs constraint requiring 30 deg starboard dev
  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint constraint;
  constraint.constraint_type = "colregs";
  constraint.unit = "deg";
  constraint.numeric_value = 30.0;
  colregs_msg->constraints.push_back(constraint);
  trigger_colregs_constraint(node, colregs_msg);

  // 3. Now make M3 invalid (degraded / infeasible)
  mission_msg->task_validity = 0; // INVALID
  trigger_mission_goal(node, mission_msg);

  // Run callback again — this time IvP is infeasible (no active behaviors) and m3_task_valid is false, so absolute snapshot anchor sets
  trigger_arbitration(node);

  EXPECT_TRUE(get_fallback_anchor_set(node));
  EXPECT_DOUBLE_EQ(get_fallback_anchor_hdg(node), 120.0);

  // 4. Change own ship heading to 150.0 deg.
  // Run callback again — anchor should stay absolute at 120.0 deg
  world_msg->own_ship.heading_deg = 150.0;
  trigger_world_state(node, world_msg);

  trigger_arbitration(node);

  EXPECT_TRUE(get_fallback_anchor_set(node));
  EXPECT_DOUBLE_EQ(get_fallback_anchor_hdg(node), 120.0);

  // 5. Restore M3 task_validity to VALID
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  trigger_arbitration(node);

  // Anchor should now be released (fallback_anchor_set_ becomes false)
  EXPECT_FALSE(get_fallback_anchor_set(node));
}

TEST_F(BehaviorArbiterTest, PortDirectiveFallbackPublishesPortWindowAndConcern) {
  auto node = make_node_with_immediate_ivp_timeout();

  auto observer = std::make_shared<rclcpp::Node>("m4_port_directive_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<l3_msgs::msg::SafetyConcernEvent> last_concern;

  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });
  auto concern_sub = observer->create_subscription<l3_msgs::msg::SafetyConcernEvent>(
      "/l3/safety/concern", rclcpp::QoS(10).reliable(),
      [&](const l3_msgs::msg::SafetyConcernEvent::SharedPtr msg) {
        last_concern = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0 &&
           node->count_subscribers("/l3/safety/concern") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);
  ASSERT_GT(node->count_subscribers("/l3/safety/concern"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(get_m3_active_latch(node));

  last_plan.reset();
  last_concern.reset();

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "PORT";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 25.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  mission_msg->task_validity = 0;
  trigger_mission_goal(node, mission_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() {
    return last_plan.has_value() && last_concern.has_value();
  });

  ASSERT_TRUE(last_plan.has_value());
  ASSERT_TRUE(last_concern.has_value());
  EXPECT_TRUE(get_fallback_anchor_set(node));
  EXPECT_DOUBLE_EQ(get_fallback_anchor_hdg(node), 0.0);
  EXPECT_NEAR(last_plan->heading_min_deg, 320.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 350.0f, 1e-3f);
  EXPECT_NE(last_plan->rationale.find("dev=-25"), std::string::npos);
  EXPECT_EQ(last_concern->suggested_action, "turn_port_absolute");
}

TEST_F(BehaviorArbiterTest, PortDirectiveSolverPublishesTightWrappedWindow) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_port_solver_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.position.latitude = 0.0;
  world_msg->own_ship.position.longitude = 0.0;
  world_msg->own_ship.heading_deg = 10.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  mission_msg->current_target_wp.latitude = 0.0;
  mission_msg->current_target_wp.longitude = 1.0;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "PORT";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 25.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 330.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 0.0f, 1e-3f);
  EXPECT_LT(last_plan->heading_min_deg, 360.0f);
  EXPECT_LT(last_plan->heading_max_deg, 360.0f);
  EXPECT_FALSE(get_fallback_anchor_set(node));
}

TEST_F(BehaviorArbiterTest, StarboardHighDeviationKeepsColregAvoidanceUtility) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_high_dev_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 120.0;
  colregs_msg->constraints.push_back(c);
  odd_msg->stamp = node->now();
  world_msg->stamp = node->now();
  mission_msg->stamp = node->now();
  colregs_msg->stamp = node->now();
  accept_plan_after = node->now();
  trigger_odd_state(node, odd_msg);
  trigger_world_state(node, world_msg);
  trigger_mission_goal(node, mission_msg);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  const auto best_util = parse_best_util(last_plan->rationale);
  ASSERT_TRUE(best_util.has_value()) << last_plan->rationale;
  EXPECT_GT(*best_util, 10.0);
  EXPECT_NEAR(last_plan->heading_min_deg, 105.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 135.0f, 1e-3f);
  EXPECT_FALSE(get_fallback_anchor_set(node));
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveWindowStaysAnchoredDuringOwnTurn) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_anchor_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  last_plan.reset();
  trigger_arbitration(node);
  spin_until(executor, [&]() {
    return last_plan.has_value() &&
        std::abs(last_plan->heading_min_deg - 15.0f) <= 1e-3f &&
        std::abs(last_plan->heading_max_deg - 45.0f) <= 1e-3f;
  });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);

  last_plan.reset();
  world_msg->own_ship.heading_deg = 80.0;
  trigger_world_state(node, world_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() {
    return last_plan.has_value() &&
        std::abs(last_plan->heading_min_deg - 15.0f) <= 1e-3f &&
        std::abs(last_plan->heading_max_deg - 45.0f) <= 1e-3f;
  });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveSurvivesBriefColregsFalseGap) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_colregs_false_gap_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);

  last_plan.reset();
  world_msg->own_ship.heading_deg = 80.0;
  trigger_world_state(node, world_msg);

  auto clear_msg = std::make_shared<COLREGsConstraintMsg>();
  clear_msg->conflict_detected = false;
  clear_msg->primary_preferred_direction = "HOLD";
  trigger_colregs_constraint(node, clear_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveReleasesImmediatelyOnHighConfidenceClear) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_colregs_clean_release_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);

  last_plan.reset();
  world_msg->own_ship.heading_deg = 80.0;
  trigger_world_state(node, world_msg);

  auto clear_msg = std::make_shared<COLREGsConstraintMsg>();
  clear_msg->conflict_detected = false;
  clear_msg->primary_preferred_direction = "HOLD";
  clear_msg->confidence = 0.95f;
  odd_msg->stamp = node->now();
  mission_msg->stamp = node->now();
  world_msg->stamp = node->now();
  clear_msg->stamp = node->now();
  trigger_odd_state(node, odd_msg);
  trigger_mission_goal(node, mission_msg);
  trigger_world_state(node, world_msg);
  trigger_colregs_constraint(node, clear_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT);
  EXPECT_NEAR(last_plan->heading_min_deg, 78.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 82.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveUsesTacticalBufferForBoundaryRange) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_tactical_buffer_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 1400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 120.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 135.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 165.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveRelaxesWhenPredictedCpaIsSafe) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_cpa_relax_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 2000.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveHoldsMaxDeviationBelowCriticalCpa) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_critical_cpa_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 108.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 135.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 165.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, StarboardDirectiveDoesNotCriticalGateBowCrossing) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_starboard_bow_cpa_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 25.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, BowCrossingKeepsCommittedStarboardWindowAfterCpaImproves) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_bow_crossing_commit_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 25.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  active_rule.target_id = 100000001;
  active_rule.role = kRoleGiveWay;
  active_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);

  last_plan.reset();
  world_msg->targets[0].cpa_m = 1600.0;
  trigger_world_state(node, world_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, Rule15CommitmentSurvivesActiveRuleDropDuringTurn) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_rule15_commit_drop_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 25.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);

  last_plan.reset();
  world_msg->targets[0].cpa_m = 1600.0;
  trigger_world_state(node, world_msg);
  colregs_msg->primary_role = kRoleBothGiveWay;
  colregs_msg->active_rules.clear();
  active_rule.rule_id = 14;
  active_rule.role = kRoleBothGiveWay;
  colregs_msg->active_rules.push_back(active_rule);
  colregs_msg->constraints[0].numeric_value = 15.0;
  trigger_colregs_constraint(node, colregs_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);

  last_plan.reset();
  world_msg->targets[0].cpa_m = 1600.0;
  trigger_world_state(node, world_msg);
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->active_rules.clear();
  colregs_msg->constraints[0].numeric_value = 15.0;
  trigger_colregs_constraint(node, colregs_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, HeadOnDoesNotUseBowCrossingCommitment) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_head_on_no_commit_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].rng_m = 900.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].encounter.relative_bearing_deg = 25.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 14;
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 48.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 78.0f, 1e-3f);

  last_plan.reset();
  world_msg->targets[0].cpa_m = 1600.0;
  trigger_world_state(node, world_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NEAR(last_plan->heading_min_deg, 15.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 45.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, ReduceSpeedDirectiveCapsBelowCurrentSpeedWithoutHeadingTurn) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_reduce_speed_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 90.0;
  world_msg->own_ship.sog_kn = 4.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "REDUCE_SPEED";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 25.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_LT(last_plan->speed_max_kn, world_msg->own_ship.sog_kn);
  EXPECT_NEAR(last_plan->heading_min_deg, 70.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 110.0f, 1e-3f);
  EXPECT_FALSE(get_fallback_anchor_set(node));
}

TEST_F(BehaviorArbiterTest, Rule15DangerAddsAuxiliarySpeedCapWhileTurningStarboard) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_rule15_danger_speed_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 10.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 200.0;
  world_msg->targets[0].brg_deg = 0.0;
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 10.0;
  world_msg->targets[0].cpa_m = 100.0;
  world_msg->targets[0].tcpa_s = 40.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_LT(last_plan->speed_max_kn, world_msg->own_ship.sog_kn);
  EXPECT_GT(last_plan->heading_min_deg, 0.0f);
  EXPECT_GT(last_plan->heading_max_deg, last_plan->heading_min_deg);
  EXPECT_TRUE(last_plan->rationale.find("phase=Danger") != std::string::npos ||
              last_plan->rationale.find("phase=Critical") != std::string::npos)
      << last_plan->rationale;
}

TEST_F(BehaviorArbiterTest, HoldDirectiveConflictDoesNotCreateTurnFallback) {
  auto node = make_node_with_immediate_ivp_timeout();

  auto observer = std::make_shared<rclcpp::Node>("m4_hold_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        last_plan = *msg;
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });
  ASSERT_GT(node->count_subscribers("/l3/m4/behavior_plan"), 0u);

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 45.0;
  world_msg->own_ship.sog_kn = 8.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  ASSERT_TRUE(get_m3_active_latch(node));
  last_plan.reset();

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_preferred_direction = "HOLD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 25.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  mission_msg->task_validity = 0;
  trigger_mission_goal(node, mission_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_FALSE(get_fallback_anchor_set(node));
  EXPECT_NEAR(last_plan->heading_min_deg, 315.0f, 1e-3f);
  EXPECT_NEAR(last_plan->heading_max_deg, 135.0f, 1e-3f);
}

TEST_F(BehaviorArbiterTest, RuleAssessmentPriorityWeightBoost) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  // Add the COLREG_AVOID behavior to dictionary if not present
  if (!has_behavior(node, BehaviorType::COLREG_AVOID)) {
    BehaviorDescriptor colreg_rule;
    colreg_rule.type = BehaviorType::COLREG_AVOID;
    colreg_rule.name = "COLREG_AVOID";
    colreg_rule.priority_weight = 0.70;
    colreg_rule.activation_rule = "colregs_conflict_detected";
    colreg_rule.ivp_function_type = "colreg_avoid";
    add_behavior(node, colreg_rule);
  }

  // 1. Initial state: weight is default 0.60
  EXPECT_FLOAT_EQ(get_colreg_avoidance_weight(node), 0.6f);

  // 2. Trigger Rule Assessment with Rule 14 Head-On
  auto rule_msg = std::make_shared<l3_msgs::msg::RuleAssessment>();
  rule_msg->stamp = node->now();
  rule_msg->applicable_rule = "Rule 14";
  rule_msg->expected_action = "turn_starboard";
  rule_msg->confidence = 0.91f;
  
  trigger_rule_assessment(node, rule_msg);

  // Weight should be boosted to 0.85 and updated in the dictionary
  EXPECT_FLOAT_EQ(get_colreg_avoidance_weight(node), 0.85f);
  EXPECT_DOUBLE_EQ(get_dictionary_priority_weight(node, BehaviorType::COLREG_AVOID), 0.85);

  // 3. Trigger Rule Assessment with other rules (or none active)
  rule_msg->applicable_rule = "Rule 15";
  trigger_rule_assessment(node, rule_msg);

  // Weight should return to default 0.70 and updated in the dictionary
  EXPECT_FLOAT_EQ(get_colreg_avoidance_weight(node), 0.70f);
  EXPECT_DOUBLE_EQ(get_dictionary_priority_weight(node, BehaviorType::COLREG_AVOID), 0.70);
}

// Phase 4 Task 4.2: AVOID → RECOVERY → TRANSIT state machine.
// When COLREGs conflict releases but own ship XTE exceeds corridor_half*0.5,
// M4 must enter RECOVERY (gradual return) instead of hard-switching to TRANSIT.
// RECOVERY clears to TRANSIT once XTE converges below the gate AND release_dwell
// (architecture §9.3 CPA recovery confirmation) is satisfied.
TEST_F(BehaviorArbiterTest, AvoidToRecoveryToTransitByXteAndDwell) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_recovery_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) { last_plan = *msg; });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  // ODD zone 1 (ODD-A).
  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  // Planned route: north-south line along lon=0 (poses at lat 0 and lat 0.01).
  // Own ship will be placed 200 m east of this line → XTE ≈ 200 m.
  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  // Own ship: 200 m east of route (lon 0.0018 ≈ 200 m at equator), heading north.
  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east of route
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  // 1. COLREGs conflict active → AVOID.
  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  // Conflict active yields a COLREG-avoidance heading window (starboard bias
  // off the 0° route heading). The behavior enum stays TRANSIT in this M4
  // build (COLREGs acts via heading window, not behavior enum); the RECOVERY
  // gate below keys off the COLREGs turn-release edge, not the behavior enum.
  EXPECT_GT(last_plan->heading_max_deg, 5.0f)
      << "conflict active should bias heading window toward starboard avoidance";

  // 2. COLREGs conflict clears, XTE still ~200 m → RECOVERY (not TRANSIT).
  last_plan.reset();
  auto clear_msg = std::make_shared<COLREGsConstraintMsg>();
  clear_msg->conflict_detected = false;
  clear_msg->primary_preferred_direction = "HOLD";
  clear_msg->confidence = 0.95f;
  trigger_colregs_constraint(node, clear_msg);

  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "XTE beyond corridor_half*0.5 after release should yield RECOVERY";

  // 3. Own ship converges back onto route (XTE ~8 m, below gate).
  //    RECOVERY should persist until release_dwell satisfied, then TRANSIT.
  world_msg->own_ship.position.longitude = 0.00007;  // ~8 m east, within gate

  // Pump arbitration cycles to accumulate release_dwell.
  for (int i = 0; i < 60; ++i) {
    last_plan.reset();
    odd_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    clear_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, clear_msg);
    trigger_arbitration(node);
    spin_until(executor, [&]() { return last_plan.has_value(); });
    if (last_plan.has_value() &&
        last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_TRANSIT) {
      break;
    }
  }
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "XTE restored + release_dwell elapsed should yield TRANSIT";
}

// A1: RECOVERY→TRANSIT release must require heading alignment with the route
// leg, not just XTE convergence. A ship that has converged laterally (XTE<gate)
// but is still pointed off the route line must stay in RECOVERY — otherwise
// TRANSIT inherits a residual heading error and route_return's heading<10°
// acceptance fails (observed: Final Heading Dev -19.6° on rule14-ho).
TEST_F(BehaviorArbiterTest, RecoveryHeldWhenXteConvergedButHeadingMisaligned) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_recovery_heading_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) { last_plan = *msg; });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  // Route: north-south line along lon=0 (route heading 0°).
  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  // Own ship 200 m east, heading north → XTE~200, heading aligned (0°).
  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  // COLREGs conflict active → arm the turn, then release to enter RECOVERY.
  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  last_plan.reset();
  auto clear_msg = std::make_shared<COLREGsConstraintMsg>();
  clear_msg->conflict_detected = false;
  clear_msg->primary_preferred_direction = "HOLD";
  clear_msg->confidence = 0.95f;
  trigger_colregs_constraint(node, clear_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() {
    return last_plan.has_value() &&
        last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_RECOVERY;
  });
  ASSERT_TRUE(last_plan.has_value());
  ASSERT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "XTE beyond gate after release should yield RECOVERY";

  // XTE converges within gate (lon ~8 m → XTE~8 m < 125 m gate) BUT own heading
  // is 20° off the 0° route heading (> 10° alignment threshold).
  world_msg->own_ship.position.longitude = 0.00007;  // ~8 m east, within gate
  world_msg->own_ship.heading_deg = 20.0;            // misaligned, off route leg
  trigger_world_state(node, world_msg);

  for (int i = 0; i < 60; ++i) {
    last_plan.reset();
    trigger_arbitration(node);
    spin_until(executor, [&]() {
      return last_plan.has_value() &&
          last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_RECOVERY;
    });
  }
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "XTE within gate but heading >10° misaligned must hold RECOVERY, not release to TRANSIT";

  // Once heading realigns (5°, within threshold) and dwell elapses → TRANSIT.
  world_msg->own_ship.heading_deg = 5.0;
  trigger_world_state(node, world_msg);
  for (int i = 0; i < 60; ++i) {
    last_plan.reset();
    trigger_arbitration(node);
    spin_until(executor, [&]() { return last_plan.has_value(); });
    if (last_plan.has_value() &&
        last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_TRANSIT) {
      break;
    }
  }
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "XTE within gate + heading aligned + dwell must yield TRANSIT";
}

TEST_F(BehaviorArbiterTest, ActiveColregsHoldDoesNotEnterRecovery) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_active_hold_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) { last_plan = *msg; });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east of route
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto turn_msg = std::make_shared<COLREGsConstraintMsg>();
  turn_msg->conflict_detected = true;
  turn_msg->primary_role = kRoleGiveWay;
  turn_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  turn_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, turn_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  last_plan.reset();
  auto hold_msg = std::make_shared<COLREGsConstraintMsg>();
  hold_msg->conflict_detected = true;
  hold_msg->primary_role = kRoleGiveWay;
  hold_msg->primary_preferred_direction = "HOLD";
  hold_msg->confidence = 1.0f;
  trigger_colregs_constraint(node, hold_msg);
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "RECOVERY may start only after M6 conflict release, not while COLREGs conflict remains active";
}

TEST_F(BehaviorArbiterTest, Rule15ReleaseNearRouteStillWaitsForPastClear) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_rule15_near_route_release_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 1400.0;
  world_msg->targets[0].brg_deg = 30.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 800.0;
  world_msg->targets[0].tcpa_s = 300.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mode_msg = std::make_shared<ModeCmdMsg>();
  mode_msg->stamp = node->now();
  mode_msg->mode = ModeCmdMsg::MODE_NORMAL;
  trigger_mode_cmd(node, mode_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  colregs_msg->colregs_chain_target_id = "100000001";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  active_rule.target_id = 100000001;
  active_rule.role = kRoleGiveWay;
  active_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mode_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mode_cmd(node, mode_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());

  last_plan.reset();
  colregs_msg->conflict_detected = false;
  colregs_msg->phase = "PRESERVE_COURSE";
  colregs_msg->primary_role = kRoleFree;
  colregs_msg->primary_preferred_direction = "HOLD";
  colregs_msg->active_rules.clear();
  colregs_msg->constraints.clear();
  for (int i = 0; i < 5; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
  }

  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "Rule15 M6 release near route must still hold M4 in RECOVERY until "
         "the committed target is past-clear; otherwise route_return can pass "
         "while phase C1 is still red: "
      << last_plan->rationale;
}

TEST_F(BehaviorArbiterTest, Rule15PastClearAcceptsM2ClampedZeroTcpa) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_rule15_zero_tcpa_release_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 1400.0;
  world_msg->targets[0].brg_deg = 30.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 800.0;
  world_msg->targets[0].tcpa_s = 300.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mode_msg = std::make_shared<ModeCmdMsg>();
  mode_msg->stamp = node->now();
  mode_msg->mode = ModeCmdMsg::MODE_NORMAL;
  trigger_mode_cmd(node, mode_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  colregs_msg->colregs_chain_target_id = "100000001";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  active_rule.target_id = 100000001;
  active_rule.role = kRoleGiveWay;
  active_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mode_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mode_cmd(node, mode_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());

  last_plan.reset();
  colregs_msg->conflict_detected = false;
  colregs_msg->phase = "PRESERVE_COURSE";
  colregs_msg->primary_role = kRoleFree;
  colregs_msg->primary_preferred_direction = "HOLD";
  colregs_msg->active_rules.clear();
  colregs_msg->constraints.clear();
  world_msg->targets[0].rng_m = 2200.0;
  world_msg->targets[0].brg_deg = 120.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].cpa_m = 1200.0;
  world_msg->targets[0].tcpa_s = 0.0;
  for (int i = 0; i < 5; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
  }

  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "M2 clamps past-CPA TCPA to zero, so Rule15 residual release cannot "
         "require tcpa_s<0 once the committed target is past beam and astern: "
      << last_plan->rationale;
}

TEST_F(BehaviorArbiterTest, RiskControlledResidualColregsConflictCanEnterRecovery) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_risk_clear_recovery_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east of route
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 200.0;
  world_msg->targets[0].brg_deg = 0.0;
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 10.0;
  world_msg->targets[0].cpa_m = 100.0;
  world_msg->targets[0].tcpa_s = 40.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_TRUE(last_plan->rationale.find("phase=Danger") != std::string::npos ||
              last_plan->rationale.find("phase=Critical") != std::string::npos)
      << last_plan->rationale;

  last_plan.reset();
  world_msg->targets[0].rng_m = 1200.0;
  world_msg->targets[0].brg_deg = 30.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].tcpa_s = 300.0;

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "risk Monitor that is still closing must not release COLREGs into RECOVERY: "
      << last_plan->rationale;
  EXPECT_NE(last_plan->rationale.find("phase=Monitor"), std::string::npos)
      << last_plan->rationale;

  last_plan.reset();
  world_msg->targets[0].rng_m = 1200.0;
  world_msg->targets[0].brg_deg = 270.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].tcpa_s = 300.0;

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "Rule15 give-way residual release must wait until own ship is abaft "
         "the target track; risk Clear alone can still cross ahead: "
      << last_plan->rationale;

  last_plan.reset();
  colregs_msg->active_rules.clear();
  l3_msgs::msg::RuleActive residual_carrier_rule;
  residual_carrier_rule.rule_id = 18;
  residual_carrier_rule.target_id = 100000001;
  residual_carrier_rule.role = kRoleGiveWay;
  residual_carrier_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(residual_carrier_rule);
  world_msg->own_ship.heading_deg = 60.0;
  world_msg->targets[0].rng_m = 3000.0;
  world_msg->targets[0].brg_deg = 91.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 2800.0;
  world_msg->targets[0].tcpa_s = 600.0;

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "Rule15 give-way residual release must also wait until the target is "
         "past own-ship beam; passing astern of the target alone still released "
         "rule15-cs at rel_brg=31deg (true bearing 91deg, own heading 60deg) "
         "after the M6 carrier dropped to Rule18: "
      << last_plan->rationale;

  last_plan.reset();
  colregs_msg->active_rules.clear();
  colregs_msg->active_rules.push_back(active_rule);
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.position.longitude = 0.0018;  // keep recovery XTE gate open
  world_msg->targets[0].rng_m = 342.0;
  world_msg->targets[0].brg_deg = 90.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 10.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].tcpa_s = 300.0;

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "Active Rule15 must not risk-release into RECOVERY while the warning "
         "domain clearance is only a few meters; cs-edge re-entered avoidance "
         "when the margin went negative on the next samples: "
      << last_plan->rationale;

  last_plan.reset();
  colregs_msg->active_rules.clear();
  colregs_msg->active_rules.push_back(active_rule);
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->targets[0].rng_m = 1200.0;
  world_msg->targets[0].brg_deg = 90.0;
  world_msg->targets[0].cpa_m = 400.0;
  world_msg->targets[0].tcpa_s = 300.0;

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  // D1.3 v6: M4 risk math must NOT release to RECOVERY while M6 still reports
  // conflict_detected=true. Previously these asserted BEHAVIOR_RECOVERY, which
  // encoded the rule14-ho oscillation bug — M4 entered RECOVERY ~250s before
  // M6 cleared, the recovery turn rotated own-ship back toward the route/target,
  // M6 kept reporting conflict, and the next cycle re-activated AVOID (double-
  // sided over-steer, steering_reversals=10). M6 is the COLREGs rule authority
  // (AGENTS.md "ODD is the only authority"); risk gates stay as secondary
  // hysteresis but cannot override M6. See
  // 2026-06-25-m4-risk-release-m6-authority-design.md §1.3.
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "risk Monitor that is no longer closing must NOT release to RECOVERY "
         "while M6 conflict_detected=true (rule14-ho oscillation root cause): "
      << last_plan->rationale;
  EXPECT_NE(last_plan->rationale.find("phase=Monitor"), std::string::npos)
      << last_plan->rationale;

  last_plan.reset();
  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "M4 must stay in COLREG_AVOID across residual M6 warning cycles while "
         "conflict_detected=true; risk-released RECOVERY is forbidden until M6 clears: "
      << last_plan->rationale;

  last_plan.reset();
  colregs_msg->conflict_detected = false;
  colregs_msg->phase = "PRESERVE_COURSE";
  colregs_msg->primary_role = kRoleFree;
  colregs_msg->primary_preferred_direction = "HOLD";
  colregs_msg->active_rules.clear();
  colregs_msg->constraints.clear();

  last_plan.reset();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.position.longitude = 0.0;  // back on route
  world_msg->targets[0].rng_m = 1400.0;
  world_msg->targets[0].brg_deg = 30.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 800.0;
  world_msg->targets[0].tcpa_s = 300.0;
  for (int i = 0; i < 5; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
  }

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "M6 PRESERVE_COURSE release must not make M4 forget a committed "
         "Rule15 give-way encounter before C1 past-clear: "
      << last_plan->rationale;

  world_msg->targets.clear();
  for (int i = 0; i < 5; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
  }

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "Rule15 committed RECOVERY should clear once M6 is released, route "
         "tracking is restored, and the committed target is no longer present "
         "in the world model; otherwise M4 stays in RECOVERY forever after "
         "scenario target cleanup: "
      << last_plan->rationale;

  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 2200.0;
  world_msg->targets[0].brg_deg = 120.0;
  world_msg->targets[0].cog_deg = 90.0;
  world_msg->targets[0].sog_kn = 0.0;
  world_msg->targets[0].cpa_m = 1200.0;
  world_msg->targets[0].tcpa_s = -10.0;
  for (int i = 0; i < 5; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
  }

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "Rule15 committed RECOVERY should clear to TRANSIT once the target is "
         "past beam and TCPA is negative: "
      << last_plan->rationale;
}

// D1.2: M4 must not enter RECOVERY while M6 still reports conflict_detected.
// This is the PREMATURE_RECOVERY_BEFORE_RULE_RELEASE defect observed in
// rule14-ho/port/intelligent: M4's risk-controlled release (risk phase Clear
// with large CPA margin) overrode M6 conflict_detected=true, entering RECOVERY
// ~135s before M6 cleared the conflict. Architecture §8.4: COLREGs constraints
// are hard constraints; M4 must not unilaterally end avoidance while M6 holds
// the conflict. Fix D1.3 gates RECOVERY entry on !conflict_detected.
TEST_F(BehaviorArbiterTest, PrematureRecoveryGatedOnM6Conflict) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_premature_recovery_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  // Phase 1: arm M4 avoidance with an active Rule14 head-on conflict.
  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east (XTE>gate)
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 1500.0;
  world_msg->targets[0].brg_deg = 0.0;
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 8.0;
  world_msg->targets[0].cpa_m = 100.0;
  world_msg->targets[0].tcpa_s = 400.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 14;
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  ASSERT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "Phase 1: M4 must be in COLREG_AVOID with active conflict: "
      << last_plan->rationale;

  // Phase 2: risk geometry now reads Clear/Monitor-not-closing (large CPA margin)
  // — this is the rule14-ho premature-release trigger. M6 conflict_detected stays
  // TRUE (target still closing, TCPA>0; M6 release requires past-and-clear).
  // M4 must NOT enter RECOVERY while M6 holds conflict, even if risk phase
  // suggests the encounter is clearing. Architecture §8.4: COLREGs constraints
  // are hard constraints; M4 defers to M6 conflict_detected for duty release.
  last_plan.reset();
  // Mirror rule14-ho release-point geometry (phase3 evidence): M6 conflict still
  // true, CPA large but TCPA still strongly positive (target closing).
  world_msg->targets[0].rng_m = 2490.0;      // large range -> risk Clear/Monitor
  world_msg->targets[0].brg_deg = 9.0;       // near bow (not past beam)
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 8.0;
  world_msg->targets[0].cpa_m = 2490.0;      // CPA >> floor -> risk Clear
  world_msg->targets[0].tcpa_s = 540.0;      // TCPA>0: target still approaching
  // M6 conflict_detected remains TRUE (unchanged) — M6 has not cleared.

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "D1.3: M4 must NOT enter RECOVERY while M6 conflict_detected=true, even "
         "if risk phase reads Clear with large CPA. This is the rule14-ho "
         "PREMATURE_RECOVERY defect (M4 released ~135s before M6 cleared): "
      << last_plan->rationale;
}

TEST_F(BehaviorArbiterTest, LiveM6TurnDirectiveCannotDropToTransitOnActionGateGlitch) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_live_turn_directive_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 1500.0;
  world_msg->targets[0].brg_deg = 45.0;
  world_msg->targets[0].cog_deg = 270.0;
  world_msg->targets[0].sog_kn = 8.0;
  world_msg->targets[0].cpa_m = 200.0;
  world_msg->targets[0].tcpa_s = 300.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->stamp = node->now();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "PRESERVE_COURSE";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  colregs_msg->colregs_chain_target_id = "100000001";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  active_rule.rule_phase = "T_warn";
  active_rule.target_id = 100000001;
  active_rule.role = kRoleGiveWay;
  active_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);
  trigger_colregs_constraint(node, colregs_msg);

  accept_plan_after = node->now();
  trigger_arbitration(node);
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "A live M6 turn directive is a hard COLREG handoff. M4 must not drop "
         "to TRANSIT just because the phase/action gate flickers while "
         "conflict_detected and STARBOARD/PORT direction remain present: "
      << last_plan->rationale;
}

TEST_F(BehaviorArbiterTest, PrematureRecoveryBlockedWhenTargetNotAbaftBeam) {
  // D1.3 v4: the closing_speed gate (D1.3 v3) only blocks release while the
  // target is still closing (TCPA>0). It does NOT cover the observed case where
  // TCPA has crossed zero (target opening, closing_speed<=0, gate satisfied) but
  // the target is still on the bow side (rel_bearing < 90°, not abaft the beam).
  // rule14-ho trace: M4 entered RECOVERY at rel_bearing=9.3° while the target
  // was still forward of the beam; M6 conflict stayed true until the target
  // actually drew abaft. RECOVERY then pushed own-ship back toward the target
  // (XTE grew, warn_margin shrank), re-triggering AVOID -> AVOID<->RECOVERY
  // oscillation (rule14-ho: behavior_toggles 5; rule15-cs-edge: 32
  // steering_reversals + 179° U-turn).
  // Spec (COLREGs_8Probe_TraceEvaluator_Spec_v0.2.md post_pass_clearance):
  // release requires target_abaft==true AND range_increasing. The closing gate
  // alone is insufficient; M4 must also hold AVOID until the primary target is
  // abaft the beam (rel_bearing>=90°).
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_not_abaft_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  // Phase 1: arm M4 avoidance with an active Rule14 head-on conflict.
  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east (XTE>gate)
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 1500.0;
  world_msg->targets[0].brg_deg = 0.0;
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 8.0;
  world_msg->targets[0].cpa_m = 100.0;
  world_msg->targets[0].tcpa_s = 400.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 14;
  active_rule.target_id = 100000001;  // so active_colregs_target_key_ resolves
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  ASSERT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_COLREG_AVOID)
      << "Phase 1: M4 must be in COLREG_AVOID with active conflict: "
      << last_plan->rationale;

  // Phase 2: TCPA has crossed zero (closing_speed<=0, D1.3 v3 gate satisfied) but
  // the target is still on the bow side (rel_bearing ~9° << 90° abaft beam). CPA
  // is large so risk phase reads Clear. M6 has released its conflict_detected
  // (the target is opening and past CPA), but the target has NOT yet drawn abaft
  // the beam — M6's past-and-clear release fires on CPA/range, ahead of the
  // geometric abaft-beam condition. This is the rule14-ho trace path: M4 entered
  // RECOVERY at rel_bearing=9.3° (closing_speed<=0, M6 conflict just cleared),
  // then RECOVERY pushed own-ship back toward the target and re-triggered AVOID
  // (behavior_toggles 5; rule15-cs-edge: 32 steering_reversals + 179° U-turn).
  // M4 must hold AVOID until the primary target is abaft the beam, not merely
  // past CPA.
  last_plan.reset();
  world_msg->targets[0].rng_m = 2490.0;      // large range -> risk Clear/Monitor
  world_msg->targets[0].brg_deg = 9.0;       // bow side: rel_bearing ~9° (NOT abaft)
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 8.0;
  world_msg->targets[0].cpa_m = 2490.0;      // CPA >> floor -> risk Clear
  world_msg->targets[0].tcpa_s = 0.0;        // TCPA<=0: target opening (D1.3 v3 gate OK)
  colregs_msg->conflict_detected = false;    // M6 released: target past CPA, opening
  colregs_msg->confidence = 1.0f;            // high-confidence release (skip low-conf dwell)

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY)
      << "D1.3 v4: M4 must NOT enter RECOVERY while the primary target is still "
         "forward of the beam (rel_bearing<90°) even though TCPA<=0 and M6 has "
         "released. The closing_speed gate alone is insufficient — RECOVERY before "
         "the target is abaft pushes own-ship back toward the target and "
         "re-triggers AVOID (rule14-ho oscillation, rule15-cs-edge 32 "
         "steering_reversals).";
}

TEST_F(BehaviorArbiterTest, SafeSameEncounterRearmAfterReleaseDoesNotReenterAvoidance) {
  auto node = std::make_shared<BehaviorArbiterNode>();

  auto observer = std::make_shared<rclcpp::Node>("m4_same_encounter_rearm_observer");
  std::optional<BehaviorPlanMsg> last_plan;
  std::optional<rclcpp::Time> accept_plan_after;
  auto plan_sub = observer->create_subscription<BehaviorPlanMsg>(
      "/l3/m4/behavior_plan", rclcpp::QoS(10).reliable(),
      [&](const BehaviorPlanMsg::SharedPtr msg) {
        if (!accept_plan_after.has_value() ||
            rclcpp::Time(msg->stamp) >= accept_plan_after.value()) {
          last_plan = *msg;
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(observer);
  spin_until(executor, [&]() {
    return node->count_subscribers("/l3/m4/behavior_plan") > 0;
  });

  auto odd_msg = std::make_shared<ODDStateMsg>();
  odd_msg->stamp = node->now();
  odd_msg->current_zone = 1;
  trigger_odd_state(node, odd_msg);

  auto route_msg = std::make_shared<PlannedRouteMsg>();
  route_msg->stamp = node->now();
  route_msg->schema_version = 112;
  {
    geographic_msgs::msg::GeoPoseStamped p0;
    p0.pose.position.latitude = 0.0;
    p0.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p0);
    geographic_msgs::msg::GeoPoseStamped p1;
    p1.pose.position.latitude = 0.01;
    p1.pose.position.longitude = 0.0;
    route_msg->route.poses.push_back(p1);
  }
  trigger_planned_route(node, route_msg);

  auto world_msg = std::make_shared<WorldStateMsg>();
  world_msg->stamp = node->now();
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->own_ship.sog_kn = 8.0;
  world_msg->own_ship.confidence = 1.0;
  world_msg->own_ship.position.latitude = 0.005;
  world_msg->own_ship.position.longitude = 0.0018;  // ~200 m east of route
  world_msg->targets.resize(1);
  world_msg->targets[0].target_id = 100000001;
  world_msg->targets[0].rng_m = 200.0;
  world_msg->targets[0].brg_deg = 0.0;
  world_msg->targets[0].cog_deg = 180.0;
  world_msg->targets[0].sog_kn = 10.0;
  world_msg->targets[0].cpa_m = 100.0;
  world_msg->targets[0].tcpa_s = 40.0;
  world_msg->targets[0].confidence = 1.0f;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  colregs_msg->colregs_chain_target_id = "100000001";
  l3_msgs::msg::RuleActive active_rule;
  active_rule.rule_id = 15;
  active_rule.rule_phase = "SOUND_WARNING";
  active_rule.target_id = 100000001;
  active_rule.role = kRoleGiveWay;
  active_rule.preferred_direction = "STARBOARD";
  colregs_msg->active_rules.push_back(active_rule);
  l3_msgs::msg::Constraint c;
  c.constraint_type = "colregs";
  c.unit = "deg";
  c.numeric_value = 30.0;
  colregs_msg->constraints.push_back(c);

  auto arbitrate_with_current_inputs = [&]() {
    accept_plan_after = node->now();
    odd_msg->stamp = node->now();
    mission_msg->stamp = node->now();
    world_msg->stamp = node->now();
    colregs_msg->stamp = node->now();
    trigger_odd_state(node, odd_msg);
    trigger_mission_goal(node, mission_msg);
    trigger_world_state(node, world_msg);
    trigger_colregs_constraint(node, colregs_msg);
    trigger_arbitration(node);
  };

  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });
  ASSERT_TRUE(last_plan.has_value());
  EXPECT_NE(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY);

  last_plan.reset();
  colregs_msg->conflict_detected = false;
  colregs_msg->phase = "T_postAvoid";
  colregs_msg->primary_preferred_direction = "HOLD";
  colregs_msg->active_rules.clear();
  colregs_msg->constraints.clear();
  colregs_msg->confidence = 0.95f;
  // D1.3 v4: RECOVERY entry now requires the give-way target to be abaft the
  // beam (rel_bearing>=90°). Mirror T_postAvoid geometry: the target has drawn
  // past the beam and is opening.
  world_msg->targets[0].rng_m = 1200.0;
  world_msg->targets[0].brg_deg = 120.0;  // abaft the beam (rel_bearing 120°)
  world_msg->targets[0].cpa_m = 1200.0;
  world_msg->targets[0].tcpa_s = -10.0;   // past CPA, opening
  arbitrate_with_current_inputs();
  spin_until(executor, [&]() {
    return last_plan.has_value() &&
        last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_RECOVERY;
  });
  ASSERT_TRUE(last_plan.has_value());
  ASSERT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_RECOVERY);

  world_msg->own_ship.position.longitude = 0.0;
  world_msg->own_ship.heading_deg = 0.0;
  world_msg->targets[0].rng_m = 2200.0;
  world_msg->targets[0].brg_deg = 120.0;
  world_msg->targets[0].cpa_m = 1200.0;
  world_msg->targets[0].tcpa_s = -10.0;
  for (int i = 0; i < 10; ++i) {
    last_plan.reset();
    arbitrate_with_current_inputs();
    spin_until(executor, [&]() { return last_plan.has_value(); });
    ASSERT_TRUE(last_plan.has_value());
    if (last_plan->behavior == BehaviorPlanMsg::BEHAVIOR_TRANSIT) {
      break;
    }
  }
  ASSERT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "test setup should restore the route before same-encounter rearm";

  last_plan.reset();
  world_msg->targets.clear();
  colregs_msg->conflict_detected = true;
  colregs_msg->phase = "SOUND_WARNING";
  colregs_msg->primary_role = kRoleGiveWay;
  colregs_msg->primary_preferred_direction = "STARBOARD";
  colregs_msg->colregs_chain_target_id = "100000001";
  colregs_msg->active_rules.push_back(active_rule);
  colregs_msg->constraints.push_back(c);
  arbitrate_with_current_inputs();
  spin_until(executor, [&]() { return last_plan.has_value(); });

  ASSERT_TRUE(last_plan.has_value());
  EXPECT_EQ(last_plan->behavior, BehaviorPlanMsg::BEHAVIOR_TRANSIT)
      << "a released encounter that reappears without current world-model risk "
         "evidence must not re-enter AVOIDANCE; otherwise stale M6/M2 timing "
         "creates AVOID/RECOVERY/TRANSIT churn: "
      << last_plan->rationale;
}

}  // namespace mass_l3::m4
