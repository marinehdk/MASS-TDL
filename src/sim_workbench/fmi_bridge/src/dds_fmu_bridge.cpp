// src/dds_fmu_bridge.cpp
#include "fmi_bridge/dds_fmu_bridge.hpp"
#include <yaml-cpp/yaml.h>
#include <rclcpp/rclcpp.hpp>

namespace fmi_bridge {

void DdsFmuBridge::load_mapping(const std::string& yaml_path) {
    YAML::Node root = YAML::LoadFile(yaml_path);
    mappings_.clear();
    for (const auto& m : root["mappings"]) {
        SignalMapping sm;
        sm.topic = m["topic"].as<std::string>();
        sm.fmu_var = m["fmu_var"].as<std::string>();
        sm.direction = m["direction"].as<std::string>();
        mappings_.push_back(sm);
    }
}

void DdsFmuBridge::init_ros2(rclcpp::Node* node) {
    node_ = node;
    RCLCPP_INFO(node_->get_logger(),
        "DdsFmuBridge initialized with %zu mappings", mappings_.size());
}

void DdsFmuBridge::write_inputs_to_fmu(double time) {
    (void)time;
    // Phase 2: read from DDS subscriptions, call libcosim set_real()
}

void DdsFmuBridge::read_outputs_from_fmu(double time) {
    (void)time;
    // Phase 2: call libcosim get_real(), publish to DDS topics
}

}  // namespace fmi_bridge
