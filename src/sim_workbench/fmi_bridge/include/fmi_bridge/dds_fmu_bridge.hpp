#pragma once
#include <string>
#include <vector>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>
#include "fmi_bridge/libcosim_wrapper.hpp"

namespace fmi_bridge {

struct SignalMapping {
    std::string topic;
    std::string msg_type;
    std::string field;
    std::string fmu_var;
    std::string direction;  // "to_fmu" | "from_fmu"
    double frequency = 50.0;
};

class DdsFmuBridge {
public:
    void load_mapping(const std::string& yaml_path);
    void init_ros2(rclcpp::Node* node, LibcosimWrapper* wrapper);

    void write_inputs_to_fmu(double time);
    void read_outputs_from_fmu(double time);

    const std::vector<SignalMapping>& mappings() const { return mappings_; }

private:
    std::vector<SignalMapping> mappings_;
    rclcpp::Node* node_ = nullptr;
    LibcosimWrapper* wrapper_ = nullptr;

    std::vector<rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr> subscriptions_;
    std::vector<rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr> publishers_;
    std::vector<double> input_buffer_;  // indexed by mapping index
};

}  // namespace fmi_bridge
