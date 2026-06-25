/**
 * @file sim_clock_node.cpp
 * @brief 仿真时钟节点 -- 支持可变倍速，使用 steady_clock 避免循环依赖
 *
 * 修复记录：
 *   [Fix 1] 使用 std::chrono::steady_clock 独立测量墙钟时间，
 *           消除 use_sim_time=true 时 this->now() 读取 /clock 的循环依赖。
 *   [Fix 2] 发布频率从 100Hz 降至 50Hz，匹配物理引擎更新率，减少 CPU 开销。
 */

#include <rclcpp/rclcpp.hpp>
#include <rosgraph_msgs/msg/clock.hpp>
#include <chrono>

class SimClockNode : public rclcpp::Node {
public:
    SimClockNode()
        : Node("sim_clock_node")
    {
        this->declare_parameter("time_scale", 10.0);
        time_scale_ = this->get_parameter("time_scale").as_double();

        pub_ = this->create_publisher<rosgraph_msgs::msg::Clock>("/clock", 10);

        // 50 Hz -- 与动力学节点的 update_rate 对齐，避免无效 tick
        timer_ = this->create_wall_timer(
            std::chrono::milliseconds(20),
            std::bind(&SimClockNode::publish_clock, this));

        // 使用独立于 ROS 时钟的 steady_clock 记录墙钟起点
        start_wall_ = std::chrono::steady_clock::now();
        sim_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);

        RCLCPP_INFO(this->get_logger(),
            "[SimClock] 仿真时钟启动 | 倍速=%.1f | 发布频率=50Hz", time_scale_);
    }

private:
    void publish_clock() {
        auto now = std::chrono::steady_clock::now();
        double dt_wall = std::chrono::duration<double>(now - start_wall_).count();
        sim_time_ = rclcpp::Time(
            static_cast<int64_t>(dt_wall * time_scale_ * 1e9),
            RCL_ROS_TIME
        );

        auto msg = rosgraph_msgs::msg::Clock();
        msg.clock = sim_time_;
        pub_->publish(msg);
    }

    rclcpp::Publisher<rosgraph_msgs::msg::Clock>::SharedPtr pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    std::chrono::steady_clock::time_point start_wall_;
    rclcpp::Time sim_time_;
    double time_scale_;
};

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<SimClockNode>());
    rclcpp::shutdown();
    return 0;
}
