/**
 * @file main.cpp
 * @brief 船舶动力学节点主入口
 */

#include "ship_dynamics/ship_dynamics_node.hpp"
#include <rclcpp/rclcpp.hpp>

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    
    auto node = std::make_shared<ship_dynamics::ShipDynamicsNode>();
    
    rclcpp::spin(node->get_node_base_interface());
    
    rclcpp::shutdown();
    return 0;
}