#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <thread>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

namespace mass_l3::m4 {

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
  world_msg->own_ship.heading_deg = 10.0;
  world_msg->own_ship.sog_kn = 10.0;
  trigger_world_state(node, world_msg);

  auto mission_msg = std::make_shared<MissionGoalMsg>();
  mission_msg->stamp = node->now();
  mission_msg->fsm_state = MissionGoalMsg::FSM_ACTIVE;
  mission_msg->task_validity = MissionGoalMsg::TASK_VALIDITY_VALID;
  trigger_mission_goal(node, mission_msg);

  auto colregs_msg = std::make_shared<COLREGsConstraintMsg>();
  colregs_msg->conflict_detected = true;
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

}  // namespace mass_l3::m4
