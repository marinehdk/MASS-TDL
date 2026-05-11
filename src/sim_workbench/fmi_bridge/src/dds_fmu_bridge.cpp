#include "fmi_bridge/dds_fmu_bridge.hpp"

namespace fmi_bridge {

void DdsFmuBridge::load_mapping(const std::string& yaml_path) {
    (void)yaml_path;
    // TODO(D1.3c): parse YAML signal mapping table
}

void DdsFmuBridge::init_ros2(rclcpp::Node* node) {
    node_ = node;
    // TODO(D1.3c): create pub/sub pairs per SignalMapping
}

void DdsFmuBridge::write_inputs_to_fmu(double time) {
    (void)time;
    // TODO(D1.3c): read latest ROS msgs, set FMU inputs
}

void DdsFmuBridge::read_outputs_from_fmu(double time) {
    (void)time;
    // TODO(D1.3c): read FMU outputs, publish ROS msgs
}

} // namespace fmi_bridge
