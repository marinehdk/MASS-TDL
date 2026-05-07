#include "rclcpp/rclcpp.hpp"
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<mass_l3::m8::HmiTransparencyBridgeNode>());
  rclcpp::shutdown();
  return 0;
}
