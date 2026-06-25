#ifndef SHIP_DYNAMICS_SHIP_DYNAMICS_NODE_HPP
#define SHIP_DYNAMICS_SHIP_DYNAMICS_NODE_HPP

#include "rclcpp/rclcpp.hpp"
// #include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "geometry_msgs/msg/wrench_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "nav_msgs/msg/path.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"
#include "std_msgs/msg/int32.hpp"
#include "tf2_ros/transform_broadcaster.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include <array>
#include <cmath>
#include <chrono>
#include <Eigen/Dense>
#include <fstream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <vector>
#include <shared_mutex>
#include <memory>

namespace ship_dynamics {

// 推进器配置结构体  异构推进系统
struct ThrusterConfig {
    std::string name;     // 推进器名称
    double x, y;          // 安装位置
    double angle_default; // 默认角度（对于固定式是固定值，对于全回转是初始值）
    bool is_azimuth;      // 是否为全回转（可变角度）
    double angle_min;     // 最小有效角度 (rad)
    double angle_max;     // 最大有效角度 (rad)
    double efficiency;    // 推进器效率系数
    // DNV Rate Limiting: 执行器状态机
    double cmd_angle;         // 角度指令（来自分配器）
    double cmd_thrust;        // 推力指令（来自分配器）
    double actual_angle;      // 实际角度（经过速率限幅）
    double actual_thrust;     // 实际推力（经过速率限幅）
    double angle_rate_limit;  // 角度速率限幅 (rad/s)
    double thrust_rate_limit; // 推力速率限幅 (N/s)
};

struct VesselConfig {
    struct Physics {
        double mass;
        double Ixx;
        double Izz;
        double GM;
        double draft;
        double water_depth;
        double gravity;
    } phys;

    struct AddedMass {
        double X_dot_u, Y_dot_v, K_dot_p, N_dot_r;
        double Y_dot_r, N_dot_v; // 交叉附加质量
    } am;

    struct Hydro {
        double X_u, X_uu;
        double Y_v, Y_vv;
        double K_p, K_pp; // 横摇阻尼
        double N_r, N_rr;
        double Y_r, N_v; // 交叉耦合
    } hydro;

    struct Limits {
        double max_u;           // 最大纵荡速度 (m/s)
        double max_r;           // 最大偏航角速度 (rad/s)
        double abnormal_reset_u; // 异常状态重置阈值 (m/s)
    } limits;
};


class ShipDynamicsNode : public rclcpp::Node {
public:
    explicit ShipDynamicsNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    // 初始化方法
    void initialize();
    void cleanup();
    
    private:
    // 物理参数变量
    double mass_, I_z_, K_z_;
    double d_u0_, d_u1_, d_v0_, d_v1_, d_r0_, d_r1_, d_r_low_speed_;
    double draft_, water_depth_;  // 吃水和水深
    double X_dot_u_, Y_dot_v_, N_dot_r_;  // 附加质量
    double Y_r_, N_v_;  // 交叉耦合系数
    
    // 仿真参数
    double update_rate_;
    double time_scale_ = 1.0;
    double psi_continuous_;
    std::string log_dir_;
    bool auto_initial_yaw_from_route_ = true;
    bool auto_initial_yaw_applied_ = false;
    double auto_initial_yaw_min_segment_m_ = 20.0;
    double auto_initial_yaw_max_speed_mps_ = 0.05;
    double auto_initial_yaw_max_position_offset_m_ = 2.0;
    double initial_x_ = 0.0;
    double initial_y_ = 0.0;

    VesselConfig v_config_;

    // 状态与受力容器 [4-DOF: surge, sway, roll, yaw]
    Eigen::Vector4d nu_;                // 船体速度 [u, v, p, r]
    Eigen::Vector4d eta_;               // 大地坐标系位姿 [x, y, phi(roll), psi(yaw)]
    Eigen::Vector4d tau_env_;           // 环境力 (风、浪、流)
    Eigen::Vector4d tau_thruster_;      // 推进器推力 (来自DNV分配)
    Eigen::Matrix4d M_;                 // [SOTA] 全秩耦合 4x4 质量矩阵
    Eigen::Matrix4d M_inv_;             //预求逆缓存矩阵 (S-04)
    
    // 推进器配置
    std::mutex data_mutex_;
    std::vector<ThrusterConfig> thrusters_;  // 推进器配置
    std::vector<double> current_thruster_forces_;  // 当前推进器推力
    std::vector<double> current_thruster_angles_;
    std::vector<double> current_thruster_buckets_;  // 倒车斗位置
    rclcpp::Time last_thruster_cmd_time_;  // 最后一次推力指令时间戳

    // [FDI] 故障检测与隔离状态
    std::vector<bool> thruster_healthy_;              // 健康状态标志
    std::vector<double> thruster_residual_;           // 残差 (cmd vs actual)
    std::vector<double> residual_threshold_;           // 残差阈值
    std::vector<int> fault_counter_;                  // 连续故障计数器
    rclcpp::Time last_health_pub_time_;               // 上次健康发布的时间
    static constexpr double HEALTH_PUB_INTERVAL = 0.2; // 健康发布间隔 (5Hz)
    // [优化] 缓存计算中间值，避免 RK4 循环内重复计算
    double f_mass_cached_ = 1.0;
    double f_drag_cached_ = 1.0;
    double last_water_depth_ = -1.0; // [M-08] loop static
    int csv_flush_count_ = 0; // [M-08] loop static

    // ROS接口
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr env_force_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr thruster_cmd_sub_;  // 推进器命令订阅器
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr waypoint_path_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr initial_route_yaw_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr depth_sub_;  // 水深订阅器
    rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr fault_inject_sub_;  // [FDI] 故障注入订阅
    rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr heading_pub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_cmd_sub_;
    bool heading_override_ = false;
    double heading_cmd_ = 0.0;
    rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr health_pub_;  // [FDI] 健康状态发布
    std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Time last_time_, start_time_;
    std::ofstream csv_file_;

    // 核心函数
    void load_parameters();
    void initialize_mass_matrix();
    void calculate_shallow_water_correction(double h, double d, double& f_mass, double& f_drag);
    Eigen::Vector4d compute_nu_dot(const Eigen::Vector4d& nu, const Eigen::Vector4d& tau_total, double current_roll);
    Eigen::Vector4d runge_kutta4(const Eigen::Vector4d& nu, const Eigen::Vector4d& tau_total, double current_roll, double dt);
    void update_dynamics();
    void reset_state();
    double normalize_angle(double angle) const;
    template<typename T>
    T get_param_safe(const std::string &name, T default_val);
    void load_thruster_configs();

    // 推进器相关函数
    void collect_thruster_forces();
    void thruster_cmd_callback(const std_msgs::msg::Float64MultiArray::SharedPtr msg);
    void update_actuator_dynamics(double dt);  // DNV Rate Limiting
    void update_thruster_health(double dt);  // [FDI] 故障检测
    void inject_fault_callback(const std_msgs::msg::Int32::SharedPtr msg);  // [FDI] 故障注入
    void publish_health_status();  // [FDI] 发布健康状态


    // 回调函数
    void env_force_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    void water_depth_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void waypoint_path_callback(const nav_msgs::msg::Path::SharedPtr msg);
    void initial_route_yaw_callback(const std_msgs::msg::Float64::SharedPtr msg);
    bool apply_auto_initial_yaw_from_path(const nav_msgs::msg::Path& path);
    bool apply_auto_initial_yaw(double route_yaw, const std::string& source, double segment_len_m);
    void heading_cmd_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void publish_odometry();


    // CSV记录
    void initialize_csv_file();
    void record_to_csv();

    // 互斥锁   
    mutable std::shared_mutex env_force_mutex_;
};

} // namespace ship_dynamics

#endif // SHIP_DYNAMICS_SHIP_DYNAMICS_NODE_HPP
