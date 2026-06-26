#ifndef ENV_ENGINES_FORCE_AGGREGATOR_NODE_HPP
#define ENV_ENGINES_FORCE_AGGREGATOR_NODE_HPP

#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/wrench.hpp"
#include "geometry_msgs/msg/wrench_stamped.hpp"
#include <mutex>
#include <atomic>
#include <string>

namespace env_engines {

/**
 * @brief 环境载荷合力聚合节点
 * 
 * 订阅风、浪、流三个载荷节点的输出，计算总环境载荷
 * 使用互斥锁保证线程安全
 */
class ForceAggregatorNode : public rclcpp::Node {
public:
    ForceAggregatorNode();
    
private:
    // 订阅回调函数
    void wave_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    void current_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    void wind_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    
    // 发布合力
    void publish_total_load();
    
    // 检查是否所有力都已收到
    bool all_forces_received() const;

    // 校验坐标系是否为 ship_link 或 base_link// 辅助函数：校验坐标系
    bool check_frame_id(const std::string& source, const std::string& frame_id);
    bool is_finite_wrench(const std::string& source, const geometry_msgs::msg::Wrench& wrench);
    
    // 检查数据是否超时
    bool is_data_timeout(const rclcpp::Time& last_time, double timeout) const;
    
    // 计算合力（线程安全）
    geometry_msgs::msg::WrenchStamped calculate_total_load();
    
    // 订阅器
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr wave_sub_;
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr current_sub_;
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr wind_sub_;
    
    // 发布器
    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr publisher_;
    
    // 定时器
    rclcpp::TimerBase::SharedPtr timer_;
    
    // 数据保护互斥锁
    mutable std::mutex data_mutex_;
    
    // 存储各力的最新值（受mutex保护）
    geometry_msgs::msg::WrenchStamped wave_load_;
    geometry_msgs::msg::WrenchStamped current_load_;
    geometry_msgs::msg::WrenchStamped wind_load_;
    
    // 原子标志，标记是否收到各力的数据
    std::atomic<bool> wave_received_{false};
    std::atomic<bool> current_received_{false};
    std::atomic<bool> wind_received_{false};
    
    // 统计信息
    uint64_t publish_count_ = 0;
    rclcpp::Time last_log_time_;
    
    // 各数据源的最后更新时间
    rclcpp::Time last_wave_time_;
    rclcpp::Time last_current_time_;
    rclcpp::Time last_wind_time_;
    double data_timeout_s_ = 1.0;
    bool first_wait_ = true; // [M-08] Removed loop static
    std::string wave_load_topic_;
};

} // namespace env_engines

#endif // ENV_ENGINES_FORCE_AGGREGATOR_NODE_HPP
