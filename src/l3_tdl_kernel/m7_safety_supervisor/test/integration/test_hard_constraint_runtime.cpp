#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/tracked_target.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "rclcpp/rclcpp.hpp"
#define private public
#include "m7_safety_supervisor/safety_supervisor_node.hpp"
#undef private

using namespace std::chrono_literals;

namespace {

void spin_for(rclcpp::executors::SingleThreadedExecutor& executor,
              std::chrono::milliseconds duration)
{
  auto const deadline = std::chrono::steady_clock::now() + duration;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(10ms);
    std::this_thread::sleep_for(5ms);
  }
}

bool spin_until(rclcpp::executors::SingleThreadedExecutor& executor,
                std::chrono::milliseconds timeout,
                std::function<bool()> const& predicate)
{
  auto const deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(10ms);
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

l3_msgs::msg::WorldState build_world_with_cpa_penetration()
{
  l3_msgs::msg::WorldState world{};
  world.confidence = 1.0F;
  world.own_ship.confidence = 1.0F;
  world.own_ship.position.latitude = 0.0;
  world.own_ship.position.longitude = 0.0;
  world.own_ship.heading_deg = 0.0;
  world.own_ship.cog_deg = 0.0;
  world.own_ship.sog_kn = 6.0;

  l3_msgs::msg::TrackedTarget target{};
  target.target_id = 42U;
  target.position.latitude = 0.0001;
  target.position.longitude = 0.0;
  target.sog_kn = 6.0;
  target.cog_deg = 180.0;
  target.heading_deg = 180.0;
  target.cpa_m = 50.0;
  target.tcpa_s = 30.0;
  target.classification = "vessel";
  target.classification_confidence = 1.0F;
  target.confidence = 1.0F;
  world.targets.push_back(target);
  return world;
}

l3_msgs::msg::AvoidancePlan build_canonical_avoidance_plan()
{
  l3_msgs::msg::AvoidancePlan plan{};
  plan.schema_version = 114U;
  plan.status = "NORMAL";
  plan.plan_id = "hc-runtime-red";
  plan.command_speed_mps.push_back(3.0);
  plan.latitude.push_back(0.0);
  plan.longitude.push_back(0.0);
  plan.latitude.push_back(0.001);
  plan.longitude.push_back(0.0);
  plan.nlp_solver_status = l3_msgs::msg::AvoidancePlan::NLP_CONVERGED;
  plan.nlp_kkt_residual = 0.0F;
  plan.nlp_tail_gate_failed = false;
  plan.confidence = 1.0F;
  plan.rationale = "test canonical plan";
  return plan;
}

l3_msgs::msg::WorldState build_world_with_safe_measured_rot()
{
  l3_msgs::msg::WorldState world{};
  world.confidence = 1.0F;
  world.own_ship.confidence = 1.0F;
  world.own_ship.position.latitude = 0.0;
  world.own_ship.position.longitude = 0.0;
  world.own_ship.heading_deg = 0.0;
  world.own_ship.cog_deg = 0.0;
  world.own_ship.sog_kn = 6.0;
  world.own_ship.r_dot_deg_s = 0.0;
  return world;
}

l3_msgs::msg::AvoidancePlan build_high_rot_avoidance_plan()
{
  auto plan = build_canonical_avoidance_plan();
  plan.plan_id = "hc-runtime-rot-red";
  plan.command_speed_mps = {12.0};
  plan.latitude = {0.0, 0.0001, 0.0001};
  plan.longitude = {0.0, 0.0, 0.0001};
  return plan;
}

}  // namespace

TEST(HardConstraintRuntime, CpaPenetrationTriggersAlertFromCanonicalAvoidancePlan)
{
  int argc = 0;
  char** argv = nullptr;
  if (!rclcpp::ok()) {
    rclcpp::init(argc, argv);
  }

  auto m7_node = std::make_shared<mass_l3::m7::SafetySupervisorNode>(rclcpp::NodeOptions{});
  auto driver = std::make_shared<rclcpp::Node>("m7_hard_constraint_runtime_test_driver");

  std::atomic_bool saw_hc1_alert{false};
  std::string last_alert_description;
  auto alert_sub = driver->create_subscription<l3_msgs::msg::SafetyAlert>(
      "/l3/m7/safety_alert",
      rclcpp::QoS(rclcpp::KeepLast(10)).reliable().transient_local(),
      [&saw_hc1_alert, &last_alert_description](l3_msgs::msg::SafetyAlert::ConstSharedPtr const msg) {
        if (msg->severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) {
          last_alert_description = msg->description;
        }
        if ((msg->severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) &&
            (msg->description.find("HC-1") != std::string::npos)) {
          saw_hc1_alert.store(true);
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(m7_node);
  executor.add_node(driver);

  m7_node->timer_main_->cancel();
  m7_node->timer_sat_->cancel();
  m7_node->timer_asdr_periodic_->cancel();
  m7_node->timer_heartbeat_->cancel();

  ASSERT_TRUE(spin_until(executor, 2s, [&] {
    return alert_sub->get_publisher_count() > 0U &&
           m7_node->pub_alert_->get_subscription_count() > 0U;
  }));

  m7_node->watchdog_->reset_all();
  auto world = std::make_shared<l3_msgs::msg::WorldState>(build_world_with_cpa_penetration());
  auto colregs = std::make_shared<l3_msgs::msg::COLREGsConstraint>();
  colregs->confidence = 1.0F;
  auto plan = std::make_shared<l3_msgs::msg::AvoidancePlan>(build_canonical_avoidance_plan());

  m7_node->on_world_state(world);
  m7_node->on_colregs_constraint(colregs);
  m7_node->on_avoidance_plan(plan);

  EXPECT_TRUE(spin_until(executor, 2s, [&] { return saw_hc1_alert.load(); }))
      << "M7 must publish a HC-1 safety_alert when on_avoidance_plan runtime policing sees CPA penetration; last alert="
      << last_alert_description;

  executor.remove_node(driver);
  executor.remove_node(m7_node);
  rclcpp::shutdown();
}

TEST(HardConstraintRuntime, CommandedRotViolationTriggersAlertWhenMeasuredRotIsSafe)
{
  int argc = 0;
  char** argv = nullptr;
  if (!rclcpp::ok()) {
    rclcpp::init(argc, argv);
  }

  auto m7_node = std::make_shared<mass_l3::m7::SafetySupervisorNode>(rclcpp::NodeOptions{});
  auto driver = std::make_shared<rclcpp::Node>("m7_hard_constraint_rot_runtime_test_driver");

  std::atomic_bool saw_hc6_alert{false};
  std::string last_alert_description;
  auto alert_sub = driver->create_subscription<l3_msgs::msg::SafetyAlert>(
      "/l3/m7/safety_alert",
      rclcpp::QoS(rclcpp::KeepLast(10)).reliable().transient_local(),
      [&saw_hc6_alert, &last_alert_description](l3_msgs::msg::SafetyAlert::ConstSharedPtr const msg) {
        if (msg->severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) {
          last_alert_description = msg->description;
        }
        if ((msg->severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) &&
            (msg->description.find("HC-6") != std::string::npos)) {
          saw_hc6_alert.store(true);
        }
      });

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(m7_node);
  executor.add_node(driver);

  m7_node->timer_main_->cancel();
  m7_node->timer_sat_->cancel();
  m7_node->timer_asdr_periodic_->cancel();
  m7_node->timer_heartbeat_->cancel();

  ASSERT_TRUE(spin_until(executor, 2s, [&] {
    return alert_sub->get_publisher_count() > 0U &&
           m7_node->pub_alert_->get_subscription_count() > 0U;
  }));

  m7_node->watchdog_->reset_all();
  auto world = std::make_shared<l3_msgs::msg::WorldState>(build_world_with_safe_measured_rot());
  auto colregs = std::make_shared<l3_msgs::msg::COLREGsConstraint>();
  colregs->confidence = 1.0F;
  auto plan = std::make_shared<l3_msgs::msg::AvoidancePlan>(build_high_rot_avoidance_plan());

  m7_node->on_world_state(world);
  m7_node->on_colregs_constraint(colregs);
  m7_node->on_avoidance_plan(plan);

  EXPECT_TRUE(spin_until(executor, 2s, [&] { return saw_hc6_alert.load(); }))
      << "M7 must police commanded avoidance ROT, not measured own-ship ROT; last alert="
      << last_alert_description;

  executor.remove_node(driver);
  executor.remove_node(m7_node);
  rclcpp::shutdown();
}
