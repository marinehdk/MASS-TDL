#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "m2_world_model/world_model_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m2::WorldModelNode>();
  rclcpp::executors::MultiThreadedExecutor exec(rclcpp::ExecutorOptions{}, 4);
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
