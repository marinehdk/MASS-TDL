// src/dds_fmu_node.cpp
#include <rclcpp/rclcpp.hpp>
#include "fmi_bridge/dds_fmu_bridge.hpp"
#include "fmi_bridge/libcosim_wrapper.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("dds_fmu_node");

    node->declare_parameter<std::string>("fmu_path", "fcb_mmg_4dof.fmu");
    node->declare_parameter<double>("step_size", 0.02);
    node->declare_parameter<double>("publish_hz", 50.0);

    std::string fmu_path = node->get_parameter("fmu_path").as_string();
    double step_size = node->get_parameter("step_size").as_double();
    double publish_hz = node->get_parameter("publish_hz").as_double();

    auto bridge = std::make_unique<fmi_bridge::DdsFmuBridge>();
    bridge->load_mapping("config/dds_fmu_mapping.yaml");

    auto wrapper = std::make_unique<fmi_bridge::LibcosimWrapper>(step_size);
    wrapper->load_fmu(fmu_path, "own_ship");

    auto var_names = wrapper->get_variable_names();
    RCLCPP_INFO(node->get_logger(),
        "FMU loaded: %s (%zu variables)", fmu_path.c_str(), var_names.size());

    bridge->init_ros2(node.get(), wrapper.get());

    double sim_time = 0.0;
    rclcpp::Rate rate(publish_hz);

    RCLCPP_INFO(node->get_logger(),
        "dds_fmu_node running at %.1f Hz, step_size=%.3fs", publish_hz, step_size);

    while (rclcpp::ok()) {
        // 1. Write latest DDS inputs to FMU
        bridge->write_inputs_to_fmu(sim_time);

        // 2. Step the FMU (advance by 1/publish_hz seconds)
        wrapper->run_for(1.0 / publish_hz);

        // 3. Read FMU outputs and publish to DDS
        bridge->read_outputs_from_fmu(sim_time);

        sim_time += step_size;
        rclcpp::spin_some(node);
        rate.sleep();
    }

    rclcpp::shutdown();
    return 0;
}
