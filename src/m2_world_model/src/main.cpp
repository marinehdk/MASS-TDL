#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <spdlog/spdlog.h>

#include "m2_world_model/world_model_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<mass_l3::m2::WorldModelNode>(rclcpp::NodeOptions{});

  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions{}, 4U);
  executor.add_node(node);

  try {
    executor.spin();
  } catch (const std::exception& e) {
    spdlog::critical("[M2] Unhandled exception in spin(): {}", e.what());
  } catch (...) {
    spdlog::critical("[M2] Unknown unhandled exception in spin()");
  }

  rclcpp::shutdown();
  return 0;
}
