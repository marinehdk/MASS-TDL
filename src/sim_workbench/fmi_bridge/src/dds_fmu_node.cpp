// src/dds_fmu_node.cpp
#include <rclcpp/rclcpp.hpp>
#include "fmi_bridge/dds_fmu_bridge.hpp"
#include "fmi_bridge/libcosim_wrapper.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("dds_fmu_node");

    auto bridge = std::make_unique<fmi_bridge::DdsFmuBridge>();
    bridge->load_mapping("config/dds_fmu_mapping.yaml");
    bridge->init_ros2(node.get());

    auto wrapper = std::make_unique<fmi_bridge::LibcosimWrapper>(0.02);
    wrapper->load_fmu("fcb_mmg_4dof.fmu", "own_ship");

    RCLCPP_INFO(node->get_logger(), "dds_fmu_node started — fmi_bridge D1.3c Phase 1");

    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
