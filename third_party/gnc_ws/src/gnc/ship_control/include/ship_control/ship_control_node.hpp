#ifndef SHIP_CONTROL_NODE_HPP
#define SHIP_CONTROL_NODE_HPP

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/float64.hpp>
#include <rcl_interfaces/msg/set_parameters_result.hpp>  // [B2/P1] 参数热更新回调返回类型
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <Eigen/Dense>
#include <cmath>
#include <mutex>
#include <vector>

class ShipControllerNode : public rclcpp::Node
{
public:
    ShipControllerNode();
    ~ShipControllerNode() = default;

private:
    // ── 控制环路模式判定 ──────────────────────────────────────────────────────
    enum class ControlMode {
        DP_POSITION,              // DP 位置保持模式
        HEADING_SPEED_AUTOPILOT   // 航向与航速自驾模式 (解耦脱离 DP)
    };
    ControlMode active_mode_ = ControlMode::DP_POSITION;

    // ── PID 增益 ──────────────────────────────────────────────────────────────
    struct PIDGains { // 用作基础滑模面 S 构建
        double kp, ki, kd;
        // [SOTA] 滑模变结构控制参数
        double k_robust; // 鲁棒切换增益
        double phi;      // 边界层厚度 (减轻抖振)
    } gains_surge_, gains_sway_, gains_yaw_, gains_speed_;
    PIDGains gains_surge_dp_, gains_sway_dp_, gains_yaw_dp_, gains_speed_dp_;  // DP 模式专用增益

    // ── 输出限幅 ──────────────────────────────────────────────────────────────
    double max_force_x_;        // 纵向最大力 (N)
    double max_force_y_;        // 横向最大力 (N)
    double max_torque_z_;       // 偏航最大力矩 (N·m)
    double max_torque_z_dp_;    // DP 模式偏航最大力矩 (N·m)
    double min_yaw_moment_cruise_; // 巡航模式转艏时最小艏摇力矩 (N·m)
    double min_yaw_moment_dp_;     // DP 模式最小艏摇力矩，默认 0 防止原地打转
    double min_yaw_moment_error_rad_; // 启用最小巡航艏摇力矩的艏向误差门限 (rad)
    double min_yaw_moment_full_error_rad_; // 软地板达到最大值的艏向误差 (rad)
    double min_yaw_moment_yaw_rate_suppress_rad_s_; // 已有足够艏摇角速度时抑制地板 (rad/s)
    double max_thrust_surge_;   // 最大纵向推力 (N) - 航速控制用
    double max_reverse_surge_;  // 【ABS修复】最大倒车推力 (N) - 限制倒车防止失舵

    // ── 积分限幅 [A1/P0] ──────────────────────────────────────────────────────
    // integral_limit_ 保留仅用于 yaml 兼容，实际限幅由 calc_integral_limits() 推算
    double integral_limit_;     // 保留兼容，不直接参与积分截断
    double i_lim_surge_;        // 纵荡积分状态上界（由 max_force_x / ki 推算）
    double i_lim_sway_;         // 横荡积分状态上界
    double i_lim_yaw_;          // 偏航积分状态上界

    // ── 控制参数 ──────────────────────────────────────────────────────────────
    double control_period_;     // 控制周期 (s)
    double derivative_cutoff_;  // 微分低通滤波截止频率 (Hz)
    double deadzone_radius_;    // 控制死区半径 (m)
    double arrival_radius_;     // 到达停车死区 (m)
    double speed_at_arrival_;   // 到达时目标速度 (m/s)
    double lpf_alpha_;          // 低通滤波器系数
    double speed_deadzone_;     // 航速死区 (m/s)
    double cruise_speed_;       // 巡航航速 (m/s)
    bool speed_drag_feedforward_enable_ = false;
    double speed_drag_linear_ = 0.0;      // C-level calm-water surge drag coefficient (N per m/s)
    double speed_drag_quadratic_ = 0.0;   // C-level calm-water surge drag coefficient (N per m^2/s^2)
    
    // ── 坐标系约定 ──────────────────────────────────────────────────────────────
    double yaw_sign_;           // 偏航力矩符号约定映射
    double yaw_sign_cruise_;    // 巡航/航迹跟踪偏航力矩符号
    double yaw_sign_dp_;        // DP/终点保持偏航力矩符号

    // ── 目标位姿 ──────────────────────────────────────────────────────────────
    double target_x_, target_y_, target_yaw_;
    double target_speed_;
    double target_cross_track_error_{0.0};

    // ── NDO 干扰观测器 (Nonlinear Disturbance Observer) ──────────────────────
    bool enable_ndo_ = true;
    Eigen::Matrix3d M_;         // 船舶名义质量矩阵 (含附加质量)
    Eigen::Matrix3d D_;         // 名义线性阻尼矩阵
    Eigen::Matrix3d L_;         // NDO 增益矩阵 (控制观测带宽)
    Eigen::Vector3d z_;         // 观测器内部积分状态
    Eigen::Vector3d d_hat_;     // 估计的低频环境干扰力 (x, y, yaw)
    Eigen::Vector3d tau_last_;  // 上一帧下发的指令力矩

    // ── 当前状态（odom_callback 写入，control_loop 读取）─────────────────────
    mutable std::mutex state_mutex_;
    double current_x_, current_y_, current_yaw_;
    double current_vx_ = 0.0, current_vy_ = 0.0, current_omega_ = 0.0;
    bool odom_received_;

    // ── PID 状态 ──────────────────────────────────────────────────────────────
    double integral_surge_, integral_sway_, integral_yaw_, integral_speed_;
    double prev_error_surge_, prev_error_sway_, prev_error_yaw_;
    double prev_error_speed_;
    double prev_deriv_surge_,  prev_deriv_sway_,  prev_deriv_yaw_;
    double previous_u_ = 0.0;
    double last_omega_ = 0.0;  // [Munk v16] 上一帧偏航角速度
    rclcpp::Time last_time_;
    rclcpp::Time last_odom_time_; // 最后一次收到里程计数据的时间戳
    bool target_reached_ = false; // 目标是否已到达

    // ── ROS 接口 ──────────────────────────────────────────────────────────────
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr         odom_sub_;
    rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr target_sub_;          // DP 坐标锚点
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr          target_heading_sub_;  // ALOS 航向制导
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr          target_speed_sub_;    // ALOS 速度制导
    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr  tau_pub_;
    rclcpp::Subscription<rcl_interfaces::msg::ParameterEvent>::SharedPtr param_sub_;  // [架构补全] 参数事件订阅
    rclcpp::TimerBase::SharedPtr control_timer_;

    // [B2/P1] 参数热更新句柄
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;

    // ── 私有方法 ──────────────────────────────────────────────────────────────
    void declare_parameters();
    void get_parameters();

    // [A1/P0] 根据当前增益和输出限幅推算积分状态上界
    void calc_integral_limits();

    // [B2/P1] 参数变化回调（ros2 param set 热更新 PID 增益/限幅）
    rcl_interfaces::msg::SetParametersResult
    on_param_change(const std::vector<rclcpp::Parameter>& params);

    // NDO 相关方法
    void init_ndo_params();
    void update_ndo(const Eigen::Vector3d& nu, const Eigen::Vector3d& tau_cmd, double dt);

    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);
    void target_callback(const geometry_msgs::msg::PoseStamped::SharedPtr msg);
    void heading_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void target_speed_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void control_loop();
};

#endif // SHIP_CONTROL_NODE_HPP
