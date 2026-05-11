#pragma once
#include <string>
#include <vector>
#include <rclcpp/rclcpp.hpp>
namespace fmi_bridge {
struct SignalMapping { std::string topic; std::string fmu_var; std::string direction; };
class DdsFmuBridge {
public:
    void load_mapping(const std::string& yaml_path);
    void init_ros2(rclcpp::Node* node);
    void write_inputs_to_fmu(double time);
    void read_outputs_from_fmu(double time);
private:
    std::vector<SignalMapping> mappings_;
    rclcpp::Node* node_ = nullptr;
};
}
