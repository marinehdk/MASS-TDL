#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  auto node = std::make_shared<mass_l3::m6_colregs::ColregsReasonerNode>();
  rclcpp::executors::SingleThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
