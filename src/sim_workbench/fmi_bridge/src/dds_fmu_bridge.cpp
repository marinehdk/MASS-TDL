// src/dds_fmu_bridge.cpp
#include "fmi_bridge/dds_fmu_bridge.hpp"
#include <yaml-cpp/yaml.h>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/float64.hpp>

namespace fmi_bridge {

void DdsFmuBridge::load_mapping(const std::string& yaml_path) {
    YAML::Node root = YAML::LoadFile(yaml_path);
    mappings_.clear();
    for (const auto& m : root["mappings"]) {
        SignalMapping sm;
        sm.topic     = m["topic"].as<std::string>();
        sm.msg_type  = m["msg_type"] ? m["msg_type"].as<std::string>() : "std_msgs/msg/Float64";
        sm.field     = m["field"] ? m["field"].as<std::string>() : "data";
        sm.fmu_var   = m["fmu_var"].as<std::string>();
        sm.direction = m["direction"].as<std::string>();
        sm.frequency = m["frequency"] ? m["frequency"].as<double>() : 50.0;
        mappings_.push_back(sm);
    }
    input_buffer_.resize(mappings_.size(), 0.0);
}

void DdsFmuBridge::init_ros2(rclcpp::Node* node, LibcosimWrapper* wrapper) {
    node_ = node;
    wrapper_ = wrapper;
    subscriptions_.clear();
    publishers_.clear();

    for (size_t i = 0; i < mappings_.size(); ++i) {
        const auto& m = mappings_[i];
        if (m.direction == "to_fmu") {
            auto sub = node_->create_subscription<std_msgs::msg::Float64>(
                m.topic, 10,
                [this, i](std_msgs::msg::Float64::SharedPtr msg) {
                    this->input_buffer_[i] = msg->data;
                });
            subscriptions_.push_back(sub);
        } else if (m.direction == "from_fmu") {
            auto pub = node_->create_publisher<std_msgs::msg::Float64>(m.topic, 10);
            publishers_.push_back(pub);
        }
    }

    RCLCPP_INFO(node_->get_logger(),
        "DdsFmuBridge: %zu mappings, %zu subs, %zu pubs",
        mappings_.size(), subscriptions_.size(), publishers_.size());
}

void DdsFmuBridge::write_inputs_to_fmu(double time) {
    (void)time;
    if (!wrapper_) return;
    for (size_t i = 0; i < mappings_.size(); ++i) {
        if (mappings_[i].direction == "to_fmu") {
            wrapper_->set_real("own_ship", mappings_[i].fmu_var, input_buffer_[i]);
        }
    }
}

void DdsFmuBridge::read_outputs_from_fmu(double time) {
    (void)time;
    if (!wrapper_) return;
    size_t pub_idx = 0;
    for (size_t i = 0; i < mappings_.size(); ++i) {
        if (mappings_[i].direction == "from_fmu" && pub_idx < publishers_.size()) {
            double val = wrapper_->get_real("own_ship", mappings_[i].fmu_var);
            auto msg = std_msgs::msg::Float64();
            msg.data = val;
            publishers_[pub_idx]->publish(msg);
            ++pub_idx;
        }
    }
}

}  // namespace fmi_bridge
