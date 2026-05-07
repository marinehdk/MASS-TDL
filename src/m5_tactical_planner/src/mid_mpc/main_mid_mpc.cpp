#include "rclcpp/rclcpp.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_node.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<mass_l3::m5::mid_mpc::MidMpcNode>());
  rclcpp::shutdown();
  return 0;
}
