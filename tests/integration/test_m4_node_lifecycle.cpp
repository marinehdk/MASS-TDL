// INT-M4-NODE-LIFECYCLE: M4 BehaviorArbiterNode construction and interface check.
//
// Verifies that a BehaviorArbiterNode can be constructed with sim_time and
// no config_dir, then advertises its expected publisher.
//
// Per v1.1.2 §8, §15.2; MISRA C++:2023 PATH-D (full 179 rules).

#include <chrono>
#include <memory>

#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"

using namespace std::chrono_literals;

TEST(INT_M4_NodeLifecycle, ConstructAndVerifyPublisher) {
  rclcpp::init(0, nullptr);

  rclcpp::NodeOptions options;
  options.use_intra_process_comms(false);
  options.start_parameter_services(false);
  // sim_time: use ROS time only to avoid dependency on wall clock synchronisation
  options.clock_type(RCL_ROS_TIME_ONLY);

  auto node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>(options);
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);

  // Spin briefly for graph discovery and internal initialisation.
  auto deadline = std::chrono::steady_clock::now() + 1s;
  bool publisher_found = false;
  while (std::chrono::steady_clock::now() < deadline) {
    exec.spin_some(100ms);
    if (node->count_publishers("/l3/m4/behavior_plan") > 0) {
      publisher_found = true;
      break;
    }
  }

  EXPECT_TRUE(publisher_found)
      << "M4 BehaviorArbiterNode should advertise a publisher on "
         "/l3/m4/behavior_plan within 1 s of construction";

  exec.remove_node(node);
  node.reset();
  rclcpp::shutdown();
}
