#include "m7_safety_supervisor/safety_supervisor_node.hpp"

#include <memory>

#include "rclcpp/executors/multi_threaded_executor.hpp"
#include "rclcpp/node_options.hpp"
#include "rclcpp/utilities.hpp"

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions const kOpts;
  auto node = std::make_shared<mass_l3::m7::SafetySupervisorNode>(kOpts);
  rclcpp::executors::MultiThreadedExecutor executor;
  executor.add_node(node);
  executor.spin();
  rclcpp::shutdown();
  return 0;
}
