#include "rclcpp/rclcpp.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_node.hpp"

int main(int argc, char* argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<mass_l3::m5::bc_mpc::BcMpcNode>());
  rclcpp::shutdown();
  return 0;
}
