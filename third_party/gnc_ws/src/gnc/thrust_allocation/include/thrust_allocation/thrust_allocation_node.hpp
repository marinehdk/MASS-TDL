#ifndef THRUST_ALLOCATION_THRUST_ALLOCATION_NODE_HPP
#define THRUST_ALLOCATION_THRUST_ALLOCATION_NODE_HPP

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include "ship_interfaces/msg/ship_reset.hpp"
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <Eigen/Dense>
#include <Eigen/SVD>
#include <vector>
#include <cmath>
#include <algorithm>
#include <map>
#include <shared_mutex>   // [B3/P1] 读写锁，保护 states_ 和 B_pinv_w_
#include "thrust_allocation/pgd_solver.hpp"

/**
 * @brief 推进器配置结构体
 * 包含 DNV-ST-0111 规范定义的物理参数及自主驾驶安全约束
 */
struct ThrusterConfig {
    std::string name;
    double x, y;              // 安装坐标，船体系 (m)
    bool is_azimuth;          // true = 全回转推进器；false = 固定角推进器
    double angle_fixed;       // 固定推进器的额定角度 (rad)
    double weight;            // 分配权重（越大越少用）
    double weight_delta;      // [SOTA] 推力高频颤动惩罚权重 (越大幅度越平滑)
    double max_thrust_N;      // 最大有效推力 (N)
    double max_power_kW;      // 额定制动功率上限 (kW)
    double diameter;          // 螺旋桨直径 (m)

    // DNV-ST-0111 效率系数
    // eta1：推进器类型系数（表3-1），含量纲 N^(1/3)·kW^(-2/3)·m^(-2/3)
    double eta1;              // 默认 800（方位/轴系推进器）
    double eta2;              // 推进器配置系数（表3-2/3-3），无量纲，默认 1.0
    double eta_m;             // 机械效率（表3-4），默认 0.93（隧道/方位）

    double Kt_0 = 0.45;       // 敞水推力系数（用于转速估算，暂未使用）

    // 自主驾驶级约束
    std::vector<std::pair<double, double>> forbidden_sectors; // 禁止区 [start°, end°]
    double thrust_rate_limit;  // 推力变化率限制 (N/s)，默认 50000 N/s
    double angle_rate_limit;   // 转向角速度限制 (rad/s)，默认 0.5 rad/s ≈ 29°/s

    // 舵机专用参数
    double max_angle;          // 舵机最大角度 (rad)
    bool is_rudder;            // 是否为舵机（特殊处理）
};

/**
 * @brief 推进器实时运行状态
 */
struct ThrusterState {
    double last_thrust_N   = 0.0;   // 上帧推力指令 (N)
    double last_angle_rad  = 0.0;   // 上帧角度指令 (rad)
    bool   is_healthy      = true;  // 健康状态（false 时从分配矩阵中排除）
    double health_score    = 1.0;   // 健康评分 [0.0, 1.0]，1.0=完全健康
};

/**
 * @brief 控制模式枚举 - 实现控制权路由（Control Authority Routing）
 */
enum class ControlMode {
    AUTO = 0,   ///< 自动模式：正常处理 /cmd_tau
    MANUAL = 1  ///< 手动模式：忽略 /cmd_tau，直接执行 /manual_actuator_cmd
};

class AutonomousThrustAllocator : public rclcpp::Node {
public:
    AutonomousThrustAllocator();

private:
    // ── 控制模式 ──────────────────────────────────────────────────────────────
    ControlMode control_mode_ = ControlMode::AUTO;  ///< 当前控制模式

    // ── 推进器配置与状态 ──────────────────────────────────────────────────────
    std::vector<ThrusterConfig> configs_;
    std::vector<ThrusterState>  states_;

    // QP 优化矩阵与约束
    Eigen::MatrixXd H_; // Hessian: B^T Q B + W + W_delta
    Eigen::MatrixXd B_; // 配置矩阵 (3 x N)
    Eigen::MatrixXd Q_; // 误差惩罚权重 (3 x 3)
    Eigen::MatrixXd W_; // 加权矩阵 (N x N)，存储权重对角矩阵
    Eigen::MatrixXd W_delta_; // [SOTA] 磨损惩罚权重矩阵
    std::vector<thrust_allocation::ProjectionConstraint> proj_constraints_;

    double dt_;  // 控制步长 = 1 / update_rate (s)

    // [B3/P1] 读写锁：on_tau_callback 持 shared_lock（读），
    //                  on_health_update 持 unique_lock（写，含矩阵重建）
    mutable std::shared_mutex mtx_;

    // ── ROS 接口 ──────────────────────────────────────────────────────────────
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr   tau_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr   manual_cmd_sub_;  ///< 手动指令（MANUAL模式）
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr   env_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr     health_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr              odom_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64MultiArray>::SharedPtr      propulsion_constraints_sub_;
    rclcpp::Subscription<ship_interfaces::msg::ShipReset>::SharedPtr       reset_sub_;  ///< 运行时 reset
    rclcpp::Publisher<std_msgs::msg::Float64MultiArray>::SharedPtr         cmd_pub_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr      param_callback_handle_;
    
    // ── 环境力前馈 ────────────────────────────────────────────────────────────
    Eigen::Vector3d tau_env_ = Eigen::Vector3d::Zero();  // 缓存的环境力
    double current_yaw_ = 0.0;                           // 缓存的船首向
    double current_speed_ = 0.0;                         // 缓存的纵向速度 m/s (用于舵效计算)
    double current_v_ = 0.0;  // sway velocity
    Eigen::Vector3d tau_des_prev_{0.0, 0.0, 0.0};  // for f_init reset
    double last_rudder_cmd_ = 0.0;                     // 上一帧舵角指令 (rad)，用于Fx阻力预测
    double env_feedforward_weight_ = 0.8;  // 前馈权重（0.0~1.0）
    bool enable_straight_cruise_main_equalization_ = true;
    double main_equalization_yaw_deadband_kNm_ = 30.0;
    double main_equalization_lateral_deadband_kN_ = 0.5;
    double side_thruster_derate_start_speed_mps_ = 2.0;
    double side_thruster_derate_decay_per_mps_ = 0.7;
    double side_thruster_lockout_speed_mps_ = 4.0;
    double side_thruster_emergency_unlock_max_speed_mps_ = 4.0;
    double side_thruster_lateral_deadband_kN_ = 0.5;
    double side_thruster_yaw_emergency_kNm_ = 200.0;
    double propulsion_constraints_stale_timeout_s_ = 3.0;
    bool propulsion_constraints_received_ = false;
    bool policy_side_thruster_allowed_ = false;
    double policy_side_thruster_max_fraction_ = 0.0;
    bool policy_rudder_preferred_ = true;
    bool policy_reverse_allowed_ = false;
    rclcpp::Time last_propulsion_constraints_time_{0, 0, RCL_ROS_TIME};

    // ── 私有方法 ──────────────────────────────────────────────────────────────

    /** 从 ROS 参数服务器加载推进器物理配置 */
    void load_thruster_configs();

    /**
     * 重建推力分配矩阵（仅对 is_healthy == true 的推进器建列）
     * 故障推进器恢复/失效时由 on_health_update 触发，持 unique_lock 期间执行
     */
    void rebuild_allocation_matrices();

    /** 根据航速动态更新权重矩阵（高速时优先用舵，低速时用侧推，极低速大转向时主桨差动） */
    void update_dynamic_weights(double u, const Eigen::Vector3d& tau_des);

    /**
     * [B5/P1] 禁止区安全角映射（含路径穿越检测）
     *
     * @param current_rad  推进器当前实际角度（用于判断旋转路径是否穿越禁止区）
     * @param target_rad   优化解算出的目标角度
     * @param cfg          推进器配置（含禁止区列表）
     * @return             安全目标角度（已绕开禁止区，选最短绕行方向）
     *
     * 注意：签名相比原版新增了 current_rad 参数，调用处需同步更新。
     */
    double map_to_safe_angle(double current_rad,
                             double target_rad,
                             const ThrusterConfig& cfg);

    /** 主推力分配回调：接收 /cmd_tau，发布 /thruster/commands */
    void on_tau_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);

    /** 运行时 reset：清执行器热启动状态 + maneuvering mode（等价于冷启） */
    void reset_allocator();
    void reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg);

    /** 环境力回调：接收 /env/total_load，缓存环境力 */
    void on_env_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);

    /** 里程计回调：接收 /ship/odom_filtered，缓存偏航角 */
    void on_odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);

    /** 推进器健康状态更新：故障/恢复时重建分配矩阵 */
    void on_health_update(const std_msgs::msg::Float64MultiArray::SharedPtr msg);

    /** 推进策略约束：接收 /propulsion/constraints，逐项接入硬约束 */
    void on_propulsion_constraints(const std_msgs::msg::Float64MultiArray::SharedPtr msg);

    /** 手动指令回调（MANUAL模式）：直接执行手动推进器指令 */
    void on_manual_cmd_callback(const std_msgs::msg::Float64MultiArray::SharedPtr msg);

    /** 当前推进策略约束是否新鲜可用 */
    bool has_fresh_propulsion_constraints();

    // ── 迟滞状态机状态标志 ──────────────────────────────────────────────
    /** 记录当前是否处于操纵模式（差动/侧推解禁） */
    bool is_maneuvering_mode_ = false;

    /**
     * 使用 CompleteOrthogonalDecomposition (COD) 计算加权伪逆
     * B_pinv_w = W^(-1) * B^T * (B * W^(-1) * B^T)^(-1)
     * COD 比 SVD 更适合最小二乘问题，数值稳定性更好
     *
     * @param B 配置矩阵 (3 x N)
     * @param W 加权矩阵 (N x N)，对角矩阵
     * @return 加权伪逆矩阵 (N x 3)
     */
    Eigen::MatrixXd compute_weighted_pseudoinverse_cod(const Eigen::MatrixXd& B, const Eigen::MatrixXd& W);
};

#endif // THRUST_ALLOCATION_THRUST_ALLOCATION_NODE_HPP
