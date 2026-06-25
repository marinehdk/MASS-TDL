/**
 * @file force_aggregator_node.cpp
 * @brief 合力聚合节点实现，用于合成风、浪、流的载荷
 *
 * 修改记录 (2026-04-14):
 *   [DEL] 删除 10kN/100kN·m 限幅逻辑 - 环境力作为已知外部输入，其幅值由
 *         物理模型决定，限幅会导致合力不守恒，破坏能量平衡
 */

#include "env_engines/force_aggregator_node.hpp"
#include <algorithm>
#include <string>
#include <cmath>

namespace env_engines {

ForceAggregatorNode::ForceAggregatorNode() : Node("force_aggregator_node") {
    this->declare_parameter("update_rate", 50.0);
    this->declare_parameter("wave_load_topic", std::string("/env/wave/drift_load"));
    this->declare_parameter("data_timeout_s", 1.0);
    double update_rate = this->get_parameter("update_rate").as_double();
    wave_load_topic_ = this->get_parameter("wave_load_topic").as_string();
    data_timeout_s_ = std::max(0.1, this->get_parameter("data_timeout_s").as_double());

    wave_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        wave_load_topic_, 10,
        std::bind(&ForceAggregatorNode::wave_load_callback, this, std::placeholders::_1));

    current_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/current_load", 10,
        std::bind(&ForceAggregatorNode::current_load_callback, this, std::placeholders::_1));

    wind_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/wind_load", 10,
        std::bind(&ForceAggregatorNode::wind_load_callback, this, std::placeholders::_1));

    const auto timer_period = std::chrono::milliseconds(static_cast<int>(1000.0 / update_rate));
    timer_ = this->create_wall_timer(timer_period,
        std::bind(&ForceAggregatorNode::publish_total_load, this));

    publisher_ = this->create_publisher<geometry_msgs::msg::WrenchStamped>("/env/total_load", 10);

    last_wave_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
    last_current_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);
    last_wind_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);

    last_log_time_ = this->now();
    publish_count_ = 0;

    RCLCPP_INFO(this->get_logger(), "[ForceAggregator] 合力聚合节点启动 (参数化波浪通道)");
    RCLCPP_INFO(this->get_logger(), "[ForceAggregator] 目标发布频率: %.2f Hz", update_rate);
    RCLCPP_INFO(this->get_logger(), "[ForceAggregator] 波浪载荷源: %s", wave_load_topic_.c_str());
    RCLCPP_INFO(this->get_logger(), "[ForceAggregator] data timeout: %.2f s", data_timeout_s_);
}

bool ForceAggregatorNode::check_frame_id(const std::string& source, const std::string& frame_id) {
    if (frame_id != "base_link") {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[%s] 坐标系错误: 期望 base_link, 收到 %s。将忽略该载荷！",
            source.c_str(), frame_id.c_str());
        return false;
    }
    return true;
}

bool ForceAggregatorNode::is_finite_wrench(
    const std::string& source, const geometry_msgs::msg::Wrench& wrench) {
    const bool valid =
        std::isfinite(wrench.force.x) &&
        std::isfinite(wrench.force.y) &&
        std::isfinite(wrench.force.z) &&
        std::isfinite(wrench.torque.x) &&
        std::isfinite(wrench.torque.y) &&
        std::isfinite(wrench.torque.z);
    if (!valid) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[%s] received non-finite load; ignoring this sample", source.c_str());
    }
    return valid;
}

void ForceAggregatorNode::wave_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
    if (!check_frame_id("Wave", msg->header.frame_id)) return;
    if (!is_finite_wrench("Wave", msg->wrench)) return;
    std::lock_guard<std::mutex> lock(data_mutex_);
    wave_load_ = *msg;
    last_wave_time_ = this->now();
    wave_received_.store(true, std::memory_order_release);
}

void ForceAggregatorNode::current_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
    if (!check_frame_id("Current", msg->header.frame_id)) return;
    if (!is_finite_wrench("Current", msg->wrench)) return;
    std::lock_guard<std::mutex> lock(data_mutex_);
    current_load_ = *msg;
    last_current_time_ = this->now();
    current_received_.store(true, std::memory_order_release);
}

void ForceAggregatorNode::wind_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
    if (!check_frame_id("Wind", msg->header.frame_id)) return;
    if (!is_finite_wrench("Wind", msg->wrench)) return;
    std::lock_guard<std::mutex> lock(data_mutex_);
    wind_load_ = *msg;
    last_wind_time_ = this->now();
    wind_received_.store(true, std::memory_order_release);
}

bool ForceAggregatorNode::all_forces_received() const {
    return wave_received_.load(std::memory_order_acquire) &&
           current_received_.load(std::memory_order_acquire) &&
           wind_received_.load(std::memory_order_acquire);
}

bool ForceAggregatorNode::is_data_timeout(const rclcpp::Time& last_time, double timeout) const {
    if (last_time.nanoseconds() == 0) return true;
    return (this->now() - last_time).seconds() > timeout;
}

geometry_msgs::msg::WrenchStamped ForceAggregatorNode::calculate_total_load() {
    std::lock_guard<std::mutex> lock(data_mutex_);

    auto total_msg = geometry_msgs::msg::WrenchStamped();
    total_msg.header.stamp = this->now();
    total_msg.header.frame_id = "base_link";

        // 直接使用最新数据，不做限幅处理
    // 环境力作为已知外部输入，其幅值由物理模型决定，限幅会导致合力不守恒
    geometry_msgs::msg::Wrench w_wave;
    geometry_msgs::msg::Wrench w_curr;
    geometry_msgs::msg::Wrench w_wind;

    if (wave_received_.load(std::memory_order_acquire) &&
        !is_data_timeout(last_wave_time_, data_timeout_s_)) {
        w_wave = wave_load_.wrench;
    }
    if (current_received_.load(std::memory_order_acquire) &&
        !is_data_timeout(last_current_time_, data_timeout_s_)) {
        w_curr = current_load_.wrench;
    }
    if (wind_received_.load(std::memory_order_acquire) &&
        !is_data_timeout(last_wind_time_, data_timeout_s_)) {
        w_wind = wind_load_.wrench;
    }

    total_msg.wrench.force.x = w_wave.force.x + w_curr.force.x + w_wind.force.x;
    total_msg.wrench.force.y = w_wave.force.y + w_curr.force.y + w_wind.force.y;
    total_msg.wrench.force.z = w_wave.force.z + w_curr.force.z + w_wind.force.z;
    total_msg.wrench.torque.x = w_wave.torque.x + w_curr.torque.x + w_wind.torque.x;
    total_msg.wrench.torque.y = w_wave.torque.y + w_curr.torque.y + w_wind.torque.y;
    total_msg.wrench.torque.z = w_wave.torque.z + w_curr.torque.z + w_wind.torque.z;

    return total_msg;
}

void ForceAggregatorNode::publish_total_load() {
    if (!wave_received_.load() && !current_received_.load() && !wind_received_.load()) {
        if (first_wait_) {
            RCLCPP_INFO(this->get_logger(),
                "[ForceAggregator] 等待数据: 波浪[%s] 海流[%s] 风力[%s]",
                wave_received_.load() ? "✓" : "○",
                current_received_.load() ? "✓" : "○",
                wind_received_.load() ? "✓" : "○");
            first_wait_ = false;
        }
        return;
    }

    auto total_msg = calculate_total_load();
    publisher_->publish(total_msg);
    publish_count_++;

    auto now = this->now();
    if ((now - last_log_time_).seconds() >= 1.0) {
        if (is_data_timeout(last_wave_time_, data_timeout_s_) && wave_received_.load()) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                "[ForceAggregator] wave data timeout (timeout=%.2fs)", data_timeout_s_);
        }
        if (is_data_timeout(last_current_time_, data_timeout_s_) && current_received_.load()) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                "[ForceAggregator] current data timeout (timeout=%.2fs)", data_timeout_s_);
        }
        if (is_data_timeout(last_wind_time_, data_timeout_s_) && wind_received_.load()) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                "[ForceAggregator] wind data timeout (timeout=%.2fs)", data_timeout_s_);
        }

        RCLCPP_INFO(this->get_logger(),
            "[ForceAggregator] 总载荷 | Fx: %.1f N | Fy: %.1f N | Mz: %.1f N·m",
            total_msg.wrench.force.x, total_msg.wrench.force.y, total_msg.wrench.torque.z);
        last_log_time_ = now;
    }
}

} // namespace env_engines

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<env_engines::ForceAggregatorNode>());
    rclcpp::shutdown();
    return 0;
}
