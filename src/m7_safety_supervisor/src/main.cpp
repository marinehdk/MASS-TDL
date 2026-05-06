#include "rclcpp/rclcpp.hpp"
#include "m7_safety_supervisor/safety_supervisor_node.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<mass_l3::m7::SafetySupervisorNode>(opts);
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
