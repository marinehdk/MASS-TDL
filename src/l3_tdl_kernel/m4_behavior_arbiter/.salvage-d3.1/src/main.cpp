#include <rclcpp/rclcpp.hpp>

#include "m4_behavior_arbiter/behavior_arbiter_node.hpp"

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m4::BehaviorArbiterNode>();
  rclcpp::spin(node);
  rclcpp::shutdown();
  return 0;
}
