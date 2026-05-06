// main.cpp — M1 ODD/Envelope Manager ROS2 node entry point.
#include <memory>

#include <rclcpp/rclcpp.hpp>

#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m1::OddEnvelopeManagerNode>();
  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
