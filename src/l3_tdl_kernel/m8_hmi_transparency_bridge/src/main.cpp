#include "rclcpp/rclcpp.hpp"
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

int main(int argc, char** argv)
{
  rclcpp::init(argc, argv);
  rclcpp::NodeOptions opts;
  auto node = std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>(opts);
  rclcpp::executors::MultiThreadedExecutor exec;
  exec.add_node(node);
  exec.spin();
  rclcpp::shutdown();
  return 0;
}
