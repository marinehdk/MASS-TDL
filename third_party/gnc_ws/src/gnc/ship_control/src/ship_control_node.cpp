/**
 * ship_control_node.cpp  —  DP 位置控制器
 *
 * 修复记录（P0/P1）：
 *   [A1/P0] integral_limit 值域修正：不再使用无效的 1e6，改为每步自动从
 *           max_force / ki 推算，并新增 integral_output_limit 参数。
 *   [A2/P0] Anti-windup 重写为 Back-Calculation 法：用饱和差值修正积分，
 *           彻底消除积分 windup，顺序正确。
 *   [C3/P1] dt 改为实测值：(now - last_time_).seconds()，并设合法性保护，
 *           避免系统负载高时积分误差累积。
 *   [B2/P1] 新增 parameter_event 回调：ros2 param set 修改 PID 增益和限幅时
 *           实时生效，无需重启节点；目标点参数修改同步更新。
 *
 * 架构说明：
 *   - 微分项使用速度反馈（-v）而非误差差分，是 DP 控制标准做法（阻尼注入），
 *     可避免目标点阶跃时的微分冲击。kd 物理含义 = 阻尼系数，与传统 kd*de/dt 不同。
 *   - 坐标系：位置误差从世界系(NED)转换到船体系后分别做纵荡/横荡 PID，
 *     输出 /cmd_tau 为船体系广义力，与水动力模型接口一致。
 */

#include "ship_control/ship_control_node.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// 构造函数
// ─────────────────────────────────────────────────────────────────────────────
ShipControllerNode::ShipControllerNode()
    : Node("ship_control_node"),
      odom_received_(false),
      integral_surge_(0.0), integral_sway_(0.0), integral_yaw_(0.0), integral_speed_(0.0),
      prev_error_surge_(0.0), prev_error_sway_(0.0), prev_error_yaw_(0.0), prev_error_speed_(0.0),
      prev_deriv_surge_(0.0), prev_deriv_sway_(0.0), prev_deriv_yaw_(0.0),
      last_odom_time_(this->now())
{
    declare_parameters();
    get_parameters();

    // [B2/P1] 参数事件回调：PID 增益和限幅实时热更新
    param_cb_handle_ = this->add_on_set_parameters_callback(
        std::bind(&ShipControllerNode::on_param_change, this, std::placeholders::_1));
    
    // 打印初始参数值
    RCLCPP_INFO(this->get_logger(),
        "初始参数: Kp_surge=%.0f, Ki_surge=%.0f, Kd_surge=%.0f  |"
        "  Kp_sway=%.0f, Ki_sway=%.0f, Kd_sway=%.0f  |"
        "  Kp_yaw=%.0f, Ki_yaw=%.0f, Kd_yaw=%.0f  |"
        "  Kp_speed=%.0f, Ki_speed=%.0f, Kd_speed=%.0f  |"
        "  max_force_x=%.0f, max_force_y=%.0f, max_torque_z=%.0f  |"
        "  cruise_speed=%.1f m/s, max_thrust_surge=%.0f N  |"
        "  target=(%.1f, %.1f, %.3f), target_speed=%.1f m/s",
        gains_surge_.kp, gains_surge_.ki, gains_surge_.kd,
        gains_sway_.kp, gains_sway_.ki, gains_sway_.kd,
        gains_yaw_.kp, gains_yaw_.ki, gains_yaw_.kd,
        gains_speed_.kp, gains_speed_.ki, gains_speed_.kd,
        max_force_x_, max_force_y_, max_torque_z_,
        cruise_speed_, max_thrust_surge_,
        target_x_, target_y_, target_yaw_, target_speed_);

    control_timer_ = this->create_wall_timer(
        std::chrono::milliseconds(static_cast<int>(control_period_ * 1000)),
        std::bind(&ShipControllerNode::control_loop, this));

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&ShipControllerNode::odom_callback, this, std::placeholders::_1));

    target_sub_ = this->create_subscription<geometry_msgs::msg::PoseStamped>(
        "/target_pose", 10,
        std::bind(&ShipControllerNode::target_callback, this, std::placeholders::_1));

    target_heading_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/control/heading_setpoint", 10,
        std::bind(&ShipControllerNode::heading_callback, this, std::placeholders::_1));

    target_speed_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/control/speed_setpoint", 10,
        std::bind(&ShipControllerNode::target_speed_callback, this, std::placeholders::_1));

    tau_pub_ = this->create_publisher<geometry_msgs::msg::WrenchStamped>("/cmd_tau", 10);

    RCLCPP_INFO(this->get_logger(),
        "DP 控制器已启动，控制周期: %.1f ms", control_period_ * 1000.0);
}

// ─────────────────────────────────────────────────────────────────────────────
// 参数声明
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::declare_parameters()
{
    // PID 增益
    this->declare_parameter<double>("gains.surge.kp", 100.0);
    this->declare_parameter<double>("gains.surge.ki",   1.0);
    this->declare_parameter<double>("gains.surge.kd", 100.0);

    this->declare_parameter<double>("gains.sway.kp",  100.0);
    this->declare_parameter<double>("gains.sway.ki",    1.0);
    this->declare_parameter<double>("gains.sway.kd",  100.0);

    this->declare_parameter<double>("gains.yaw.kp",  1000.0);
    this->declare_parameter<double>("gains.yaw.ki",    10.0);
    this->declare_parameter<double>("gains.yaw.kd",  1000.0);

    // 航速PID增益
    this->declare_parameter<double>("gains.speed.kp",  5000.0);
    this->declare_parameter<double>("gains.speed.ki",    50.0);
    this->declare_parameter<double>("gains.speed.kd",  2000.0);

    // [SOTA] 滑模变结构 (SMC) 鲁棒控制参数
    this->declare_parameter<double>("gains.surge.k_robust", 500000.0); // 纵荡滑模切换增益
    this->declare_parameter<double>("gains.surge.phi",      0.5);    // 纵荡边界层厚度
    this->declare_parameter<double>("gains.sway.k_robust",  500000.0);
    this->declare_parameter<double>("gains.sway.phi",       0.5);
    this->declare_parameter<double>("gains.yaw.k_robust",   5000000.0);
    this->declare_parameter<double>("gains.yaw.phi",        0.1);
    this->declare_parameter<double>("gains.speed.k_robust", 100000.0);
    this->declare_parameter<double>("gains.speed.phi",      0.5);

    // DP 模式专用 PID 增益
    this->declare_parameter<double>("gains.surge_dp.kp", 50000.0);
    this->declare_parameter<double>("gains.surge_dp.ki", 500.0);
    this->declare_parameter<double>("gains.surge_dp.kd", 20000.0);
    this->declare_parameter<double>("gains.surge_dp.k_robust", 2000.0);
    this->declare_parameter<double>("gains.surge_dp.phi", 1.0);
    this->declare_parameter<double>("gains.sway_dp.kp", 100000.0);
    this->declare_parameter<double>("gains.sway_dp.ki", 2000.0);
    this->declare_parameter<double>("gains.sway_dp.kd", 50000.0);
    this->declare_parameter<double>("gains.sway_dp.k_robust", 50000.0);
    this->declare_parameter<double>("gains.sway_dp.phi", 1.0);
    this->declare_parameter<double>("gains.yaw_dp.kp", 10000.0);
    this->declare_parameter<double>("gains.yaw_dp.ki", 500.0);
    this->declare_parameter<double>("gains.yaw_dp.kd", 5000.0);
    this->declare_parameter<double>("gains.yaw_dp.k_robust", 5000.0);
    this->declare_parameter<double>("gains.yaw_dp.phi", 0.5);
    this->declare_parameter<double>("gains.speed_dp.kp", 20000.0);
    this->declare_parameter<double>("gains.speed_dp.ki", 500.0);
    this->declare_parameter<double>("gains.speed_dp.kd", 0.0);
    this->declare_parameter<double>("gains.speed_dp.k_robust", 5000.0);
    this->declare_parameter<double>("gains.speed_dp.phi", 1.0);

    // 输出限幅（推进器能力上界）
    this->declare_parameter<double>("max_force_x",   10000.0);
    this->declare_parameter<double>("max_force_y",   10000.0);
    this->declare_parameter<double>("max_torque_z",  100000.0);
    this->declare_parameter<double>("max_torque_z_dp", 100000.0);
    this->declare_parameter<double>("min_yaw_moment_cruise", 400000.0);
    this->declare_parameter<double>("min_yaw_moment_error_deg", 8.0);
    this->declare_parameter<double>("min_yaw_moment_full_error_deg", 90.0);
    this->declare_parameter<double>("min_yaw_moment_yaw_rate_suppress_deg_s", 1.5);
    this->declare_parameter<double>("max_thrust_surge", 100000.0);  // 最大纵向推力
    this->declare_parameter<double>("max_reverse_surge", 100000.0);  // 【ABS修复】最大倒车推力

    // 航速控制参数
    this->declare_parameter<double>("cruise_speed", 5.0);      // 巡航航速 (m/s)
    this->declare_parameter<double>("speed_deadzone", 0.1);     // 航速死区 (m/s)
    this->declare_parameter<bool>("speed_drag_feedforward.enable", false);
    this->declare_parameter<double>("speed_drag_feedforward.linear_N_per_mps", 0.0);
    this->declare_parameter<double>("speed_drag_feedforward.quadratic_N_per_mps2", 0.0);

    // 坐标系约定映射参数
    this->declare_parameter<double>("coordinate_conventions.yaw_torque_sign", 1.0);  // [修复] NED坐标系，左转为正
    this->declare_parameter<double>("coordinate_conventions.yaw_torque_sign_cruise", 0.0);
    this->declare_parameter<double>("coordinate_conventions.yaw_torque_sign_dp", 0.0);

    // [A1/P0] 移除失效的 integral_limit 参数，改用输出导向的积分限幅：
    //   积分项最大贡献 = max_force / ki，由 calc_integral_limits() 自动推算。
    //   保留此参数名以保持 yaml 兼容，但实际不再直接使用该值限制积分状态。
    this->declare_parameter<double>("integral_limit", 1e6);   // 保留兼容，不直接使用

    this->declare_parameter<double>("control_period",        0.1);
    this->declare_parameter<double>("derivative_cutoff_freq", 5.0);
    
    // 新增参数：控制死区和低通滤波器系数
    this->declare_parameter<double>("deadzone_radius", 0.5);  // 控制死区半径（m）
    this->declare_parameter<double>("lpf_alpha", 0.2);        // 低通滤波器系数

    // 目标点（备用，优先使用 /target_pose topic）
    this->declare_parameter<double>("target.x",   0.0); // [FIX]
    this->declare_parameter<double>("target.y",   0.0); // [FIX]
    this->declare_parameter<double>("target.yaw", 0.0); // [FIX]

    // NDO 物理参数 (名义模型)
    this->declare_parameter<bool>("ndo.enable", true);
    // [M-03 修复] 默认值对齐 ship_config.yaml，消除 YAML 加载失败时的参数偏差
    // 质量矩阵 (M) 对角线成分: mass + added_mass
    this->declare_parameter<double>("ndo.mass_x", 1.1e6);     // 1e6 + 1e5 (X_dot_u)
    this->declare_parameter<double>("ndo.mass_y", 1.7e6);     // 1e6 + 7e5 (Y_dot_v)
    this->declare_parameter<double>("ndo.mass_yaw", 1.9e8);   // 1.6e8 + 3e7 (N_dot_r)
    // 线性阻尼矩阵 (D) 对角线成分: |hydrodynamic linear damping|
    this->declare_parameter<double>("ndo.damping_x", 10000.0);    // |X_u| = 1e4
    this->declare_parameter<double>("ndo.damping_y", 60000.0);    // |Y_v| = 6e4
    this->declare_parameter<double>("ndo.damping_yaw", 1600000.0); // |N_r| = 1.6e6
    // 观测器增益矩阵 (L)
    this->declare_parameter<double>("ndo.gain_x", 0.2);
    this->declare_parameter<double>("ndo.gain_y", 0.2);
    this->declare_parameter<double>("ndo.gain_yaw", 0.1);
}

// ─────────────────────────────────────────────────────────────────────────────
// 参数读取（构造时 + 热更新时调用）
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::get_parameters()
{
    gains_surge_.kp = this->get_parameter("gains.surge.kp").as_double();
    gains_surge_.ki = this->get_parameter("gains.surge.ki").as_double();
    gains_surge_.kd = this->get_parameter("gains.surge.kd").as_double();

    gains_sway_.kp  = this->get_parameter("gains.sway.kp").as_double();
    gains_sway_.ki  = this->get_parameter("gains.sway.ki").as_double();
    gains_sway_.kd  = this->get_parameter("gains.sway.kd").as_double();

    gains_yaw_.kp   = this->get_parameter("gains.yaw.kp").as_double();
    gains_yaw_.ki   = this->get_parameter("gains.yaw.ki").as_double();
    gains_yaw_.kd   = this->get_parameter("gains.yaw.kd").as_double();

    gains_speed_.kp   = this->get_parameter("gains.speed.kp").as_double();
    gains_speed_.ki   = this->get_parameter("gains.speed.ki").as_double();
    gains_speed_.kd   = this->get_parameter("gains.speed.kd").as_double();

    // SMC 参数
    gains_surge_.k_robust = this->get_parameter("gains.surge.k_robust").as_double();
    gains_surge_.phi      = this->get_parameter("gains.surge.phi").as_double();
    gains_sway_.k_robust  = this->get_parameter("gains.sway.k_robust").as_double();
    gains_sway_.phi       = this->get_parameter("gains.sway.phi").as_double();
    gains_yaw_.k_robust   = this->get_parameter("gains.yaw.k_robust").as_double();
    gains_yaw_.phi        = this->get_parameter("gains.yaw.phi").as_double();
    gains_speed_.k_robust = this->get_parameter("gains.speed.k_robust").as_double();
    gains_speed_.phi      = this->get_parameter("gains.speed.phi").as_double();

    // DP 模式专用增益加载
    gains_surge_dp_.kp = this->get_parameter("gains.surge_dp.kp").as_double();
    gains_surge_dp_.ki = this->get_parameter("gains.surge_dp.ki").as_double();
    gains_surge_dp_.kd = this->get_parameter("gains.surge_dp.kd").as_double();
    gains_surge_dp_.k_robust = this->get_parameter("gains.surge_dp.k_robust").as_double();
    gains_surge_dp_.phi = this->get_parameter("gains.surge_dp.phi").as_double();

    gains_sway_dp_.kp = this->get_parameter("gains.sway_dp.kp").as_double();
    gains_sway_dp_.ki = this->get_parameter("gains.sway_dp.ki").as_double();
    gains_sway_dp_.kd = this->get_parameter("gains.sway_dp.kd").as_double();
    gains_sway_dp_.k_robust = this->get_parameter("gains.sway_dp.k_robust").as_double();
    gains_sway_dp_.phi = this->get_parameter("gains.sway_dp.phi").as_double();

    gains_yaw_dp_.kp = this->get_parameter("gains.yaw_dp.kp").as_double();
    gains_yaw_dp_.ki = this->get_parameter("gains.yaw_dp.ki").as_double();
    gains_yaw_dp_.kd = this->get_parameter("gains.yaw_dp.kd").as_double();
    gains_yaw_dp_.k_robust = this->get_parameter("gains.yaw_dp.k_robust").as_double();
    gains_yaw_dp_.phi = this->get_parameter("gains.yaw_dp.phi").as_double();

    gains_speed_dp_.kp = this->get_parameter("gains.speed_dp.kp").as_double();
    gains_speed_dp_.ki = this->get_parameter("gains.speed_dp.ki").as_double();
    gains_speed_dp_.kd = this->get_parameter("gains.speed_dp.kd").as_double();
    gains_speed_dp_.k_robust = this->get_parameter("gains.speed_dp.k_robust").as_double();
    gains_speed_dp_.phi = this->get_parameter("gains.speed_dp.phi").as_double();

    max_force_x_    = this->get_parameter("max_force_x").as_double();
    max_force_y_    = this->get_parameter("max_force_y").as_double();
    max_torque_z_   = this->get_parameter("max_torque_z").as_double();
    max_torque_z_dp_ = this->get_parameter("max_torque_z_dp").as_double();
    min_yaw_moment_cruise_ = this->get_parameter("min_yaw_moment_cruise").as_double();
    min_yaw_moment_error_rad_ = this->get_parameter("min_yaw_moment_error_deg").as_double() * M_PI / 180.0;
    min_yaw_moment_full_error_rad_ = std::max(
        min_yaw_moment_error_rad_ + 1.0 * M_PI / 180.0,
        this->get_parameter("min_yaw_moment_full_error_deg").as_double() * M_PI / 180.0);
    min_yaw_moment_yaw_rate_suppress_rad_s_ = std::max(
        0.0,
        this->get_parameter("min_yaw_moment_yaw_rate_suppress_deg_s").as_double() * M_PI / 180.0);
    max_thrust_surge_ = this->get_parameter("max_thrust_surge").as_double();
    max_reverse_surge_ = this->get_parameter("max_reverse_surge").as_double();
    control_period_ = this->get_parameter("control_period").as_double();
    derivative_cutoff_ = this->get_parameter("derivative_cutoff_freq").as_double();
    
    // 读取新增参数
    deadzone_radius_ = this->get_parameter("deadzone_radius").as_double();
    lpf_alpha_ = this->get_parameter("lpf_alpha").as_double();
    cruise_speed_ = this->get_parameter("cruise_speed").as_double();
    speed_deadzone_ = this->get_parameter("speed_deadzone").as_double();
    speed_drag_feedforward_enable_ = this->get_parameter("speed_drag_feedforward.enable").as_bool();
    speed_drag_linear_ = this->get_parameter("speed_drag_feedforward.linear_N_per_mps").as_double();
    speed_drag_quadratic_ = this->get_parameter("speed_drag_feedforward.quadratic_N_per_mps2").as_double();
    yaw_sign_ = this->get_parameter("coordinate_conventions.yaw_torque_sign").as_double();
    yaw_sign_cruise_ = this->get_parameter("coordinate_conventions.yaw_torque_sign_cruise").as_double();
    yaw_sign_dp_ = this->get_parameter("coordinate_conventions.yaw_torque_sign_dp").as_double();
    if (std::abs(yaw_sign_cruise_) < 1e-9) {
        yaw_sign_cruise_ = yaw_sign_;
    }
    if (std::abs(yaw_sign_dp_) < 1e-9) {
        yaw_sign_dp_ = yaw_sign_;
    }

    target_x_   = this->get_parameter("target.x").as_double();
    target_y_   = this->get_parameter("target.y").as_double();
    target_yaw_ = this->get_parameter("target.yaw").as_double();
    target_speed_ = this->get_parameter("cruise_speed").as_double();  // 默认使用cruise_speed作为目标航速

    // NDO 参数加载
    init_ndo_params();

    // [A1/P0] 推算积分上界：积分项最大输出 = max_force，
    //   因此积分状态上界 = max_force / ki，ki=0 时退化为保护值。
    calc_integral_limits();
}

void ShipControllerNode::init_ndo_params() {
    enable_ndo_ = this->get_parameter("ndo.enable").as_bool();
    
    double mx = this->get_parameter("ndo.mass_x").as_double();
    double my = this->get_parameter("ndo.mass_y").as_double();
    double myaw = this->get_parameter("ndo.mass_yaw").as_double();
    M_ = Eigen::Vector3d(mx, my, myaw).asDiagonal();

    double dx = this->get_parameter("ndo.damping_x").as_double();
    double dy = this->get_parameter("ndo.damping_y").as_double();
    double dyaw = this->get_parameter("ndo.damping_yaw").as_double();
    D_ = Eigen::Vector3d(dx, dy, dyaw).asDiagonal();

    double lx = this->get_parameter("ndo.gain_x").as_double();
    double ly = this->get_parameter("ndo.gain_y").as_double();
    double lyaw = this->get_parameter("ndo.gain_yaw").as_double();
    L_ = Eigen::Vector3d(lx, ly, lyaw).asDiagonal();

    z_.setZero();
    d_hat_.setZero();
    tau_last_.setZero();

    if (enable_ndo_) {
        RCLCPP_INFO(this->get_logger(), "[NDO] 非线性干扰观测器已开启。名义质量对角: (%.1e, %.1e, %.1e)", mx, my, myaw);
    }
}

void ShipControllerNode::update_ndo(const Eigen::Vector3d& nu, const Eigen::Vector3d& tau_cmd, double dt) {
    if (!enable_ndo_) {
        d_hat_.setZero();
        return;
    }
    
    // NDO 无导数数学积分方程 (Chen & Fossen 形式)
    // dz/dt = -L*z + L*(D*\nu - \tau - L*M*\nu)
    Eigen::Vector3d z_dot = -L_ * z_ + L_ * (D_ * nu - tau_cmd - L_ * M_ * nu);
    
    // 欧拉积分更新内部状态
    z_ += z_dot * dt;
    
    // 重构环境干扰力
    d_hat_ = z_ + L_ * M_ * nu;

    // 针对 d_hat 进行安全性限幅，避免极端工况发散
    d_hat_.x() = std::clamp(d_hat_.x(), -max_force_x_ * 0.9, max_force_x_ * 0.9);
    d_hat_.y() = std::clamp(d_hat_.y(), -max_force_y_ * 0.9, max_force_y_ * 0.9);
    d_hat_.z() = std::clamp(d_hat_.z(), -max_torque_z_ * 0.9, max_torque_z_ * 0.9);
}

// [A1/P0] 根据当前增益和输出限幅自动推算积分状态上界
void ShipControllerNode::calc_integral_limits()
{
    // 积分状态上界 = max_output / ki（积分贡献不超过满量程输出）
    // 额外乘以 2.0 提供余量（比例项也会贡献）
    const double safety = 2.0;
    i_lim_surge_ = (gains_surge_.ki > 1e-9)
        ? (max_force_x_ / gains_surge_.ki) * safety : 1e9;
    i_lim_sway_  = (gains_sway_.ki  > 1e-9)
        ? (max_force_y_ / gains_sway_.ki)  * safety : 1e9;
    i_lim_yaw_   = (gains_yaw_.ki   > 1e-9)
        ? (max_torque_z_ / gains_yaw_.ki)  * safety : 1e9;

    // 更新后把当前积分 clamp 到新上界，防止突变
    integral_surge_ = std::clamp(integral_surge_, -i_lim_surge_, i_lim_surge_);
    integral_sway_  = std::clamp(integral_sway_,  -i_lim_sway_,  i_lim_sway_);
    integral_yaw_   = std::clamp(integral_yaw_,   -i_lim_yaw_,   i_lim_yaw_);
}

// ─────────────────────────────────────────────────────────────────────────────
// [B2/P1] 参数热更新回调
// ─────────────────────────────────────────────────────────────────────────────
rcl_interfaces::msg::SetParametersResult
ShipControllerNode::on_param_change(const std::vector<rclcpp::Parameter>& params)
{
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;

    for (const auto& p : params) {
        const auto& name = p.get_name();

        // 验证数值合法性
        if (name.find("gains.") == 0 || name.find("max_") == 0) {
            if (p.as_double() < 0.0) {
                result.successful = false;
                result.reason = name + " 必须为正数";
                return result;
            }
        }
    }

    // 全量重新读取（保证一致性）
    get_parameters();

    RCLCPP_INFO(this->get_logger(),
        "[参数更新] Kp_surge=%.0f, Ki_surge=%.0f, Kd_surge=%.0f  |"
        "  i_lim_surge=%.1f, i_lim_sway=%.1f, i_lim_yaw=%.1f",
        gains_surge_.kp, gains_surge_.ki, gains_surge_.kd,
        i_lim_surge_, i_lim_sway_, i_lim_yaw_);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// 里程计回调
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    // Extract yaw from incoming message for cold-start anchor
    tf2::Quaternion q(
        msg->pose.pose.orientation.x,
        msg->pose.pose.orientation.y,
        msg->pose.pose.orientation.z,
        msg->pose.pose.orientation.w);
    tf2::Matrix3x3 m(q);
    double roll, pitch, msg_yaw;
    m.getRPY(roll, pitch, msg_yaw);

    if (!odom_received_) {
        target_x_ = msg->pose.pose.position.x;
        target_y_ = msg->pose.pose.position.y;
        target_yaw_ = msg_yaw;  // Use yaw from incoming message
        RCLCPP_INFO(this->get_logger(), 
            "[ARCH] Cold-start anchor: target locked at (%.2f, %.2f, %.1f deg)", 
            target_x_, target_y_, target_yaw_ * 180.0 / M_PI);
    }

    current_x_ = msg->pose.pose.position.x;
    current_y_ = msg->pose.pose.position.y;
    current_yaw_ = msg_yaw;

    // twist 是船体系速度（base_link），直接用于速度阻尼项
    current_vx_    = msg->twist.twist.linear.x;
    current_vy_    = msg->twist.twist.linear.y;
    current_omega_ = msg->twist.twist.angular.z;

    odom_received_ = true;
    last_odom_time_ = this->now(); // 更新最后一次收到里程计数据的时间戳
}

// ─────────────────────────────────────────────────────────────────────────────
// 目标点回调
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::target_callback(const geometry_msgs::msg::PoseStamped::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(state_mutex_); // [S-03]
    target_x_ = msg->pose.position.x;
    target_y_ = msg->pose.position.y;
    target_cross_track_error_ = msg->pose.position.z;

    tf2::Quaternion q(
        msg->pose.orientation.x,
        msg->pose.orientation.y,
        msg->pose.orientation.z,
        msg->pose.orientation.w);
    tf2::Matrix3x3 m(q);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);
    
    // [M-11] 无扰动切换（从 AUTOPILOT 转入 DP_POSITION 时），用上一帧下发力矩预置积分进行平滑
    if (active_mode_ != ControlMode::DP_POSITION) {
        if (gains_surge_.ki > 1e-9) integral_surge_ = tau_last_.x() / gains_surge_.ki;
        if (gains_sway_.ki > 1e-9)  integral_sway_  = tau_last_.y() / gains_sway_.ki;
        if (gains_yaw_.ki > 1e-9)   integral_yaw_   = tau_last_.z() / gains_yaw_.ki;
        integral_speed_ = 0.0;
        previous_u_ = std::sqrt(current_vx_ * current_vx_ + current_vy_ * current_vy_); // [M-03] 消除模式切换时的微分脉冲
    }
    
    target_yaw_ = yaw;
    // [修复 Bug-1] target_callback 不应切换 active_mode_！
    // active_mode_ 的切换应由 heading_callback 处理，避免 guidance 每周期发布 target_pose
    // 时不断切换到 DP_POSITION 模式，扰乱 heading_control 模式
    // active_mode_ = ControlMode::DP_POSITION; // 已注释：仅更新目标坐标，不切换模式

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "收到新目标: (%.2f, %.2f), 航向 %.2f rad", target_x_, target_y_, target_yaw_);
}

// ─────────────────────────────────────────────────────────────────────────────
// ALOS 航向制导回调
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::heading_callback(const std_msgs::msg::Float64::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(state_mutex_);

    double incoming_heading = msg->data;

    // [DP修复] speed=0 表示位置保持。DP 一旦进入，不应因目标艏向与当前艏向
    // 拉开而退回巡航，否则终点保持会丢失 yaw 闭环。
    if (target_speed_ < 0.01) {
        target_yaw_ = incoming_heading;
        active_mode_ = ControlMode::DP_POSITION;  // 显式设置为DP模式
        double dist_error = std::hypot(target_x_ - current_x_, target_y_ - current_y_);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "接收DP保持信令: ψ=%.2f° speed=0 | 当前位置(%.1f, %.1f) 目标(%.1f, %.1f) 距离误差%.1fm",
            incoming_heading*180/M_PI, current_x_, current_y_, target_x_, target_y_, dist_error);
        return;
    }

    // [M-11] 从 DP 切换入 AUTOPILOT 时，隔离位置环积分干扰，并将前向推力赋予给速度环积分平滑启动
    if (active_mode_ != ControlMode::HEADING_SPEED_AUTOPILOT) {
        integral_surge_ = 0.0;
        integral_sway_  = 0.0;
        integral_yaw_   = 0.0;
        if (gains_speed_.ki > 1e-9) {
            integral_speed_ = tau_last_.x() / gains_speed_.ki;
        }
        previous_u_ = current_vx_; // [M-03] 巡航速度环使用有符号纵荡速度，倒车不能被当作前进
    }

    target_yaw_ = msg->data;
    active_mode_ = ControlMode::HEADING_SPEED_AUTOPILOT; // 接收到航向信标，切入独立巡航模式

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "接收制导航向信令: %.2f rad, 进入独立巡航自驾模式", target_yaw_);
}

// ─────────────────────────────────────────────────────────────────────────────
// 目标航速回调
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::target_speed_callback(const std_msgs::msg::Float64::SharedPtr msg)
{
    double new_speed = msg->data;

    if (new_speed < 0.0) {
        RCLCPP_WARN(this->get_logger(), "收到负目标航速 %.2f，忽略", new_speed);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        const bool entering_dp_position = new_speed < 0.01 && target_speed_ >= 0.01;
        target_speed_ = new_speed;
        if (target_speed_ < 0.01) {
            active_mode_ = ControlMode::DP_POSITION;
            if (entering_dp_position) {
                integral_surge_ = 0.0;
                integral_sway_ = 0.0;
                integral_yaw_ = 0.0;
                integral_speed_ = 0.0;
                tau_last_.setZero();
                prev_error_surge_ = 0.0;
                prev_error_sway_ = 0.0;
                prev_error_yaw_ = 0.0;
                prev_error_speed_ = 0.0;
                previous_u_ = std::sqrt(current_vx_ * current_vx_ + current_vy_ * current_vy_);
                RCLCPP_WARN(this->get_logger(),
                    "[DP Capture] enter target_speed=0: clear cruise integrators before position hold");
            }
        }
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "收到新目标航速: %.2f m/s", new_speed);
}

// ─────────────────────────────────────────────────────────────────────────────
// 主控制循环
// ─────────────────────────────────────────────────────────────────────────────
void ShipControllerNode::control_loop()
{
    if (!odom_received_) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "等待里程计数据...");
        return;
    }
    
    // 检查里程计数据是否超时（超过500ms）
    rclcpp::Time now = this->now();
    if ((now - last_odom_time_).seconds() > 0.5) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "里程计数据超时，已停止控制输出");
        // 发布零力指令
        geometry_msgs::msg::WrenchStamped tau_msg;
        tau_msg.header.stamp    = now;
        tau_msg.header.frame_id = "base_link";
        tau_msg.wrench.force.x  = 0.0;
        tau_msg.wrench.force.y  = 0.0;
        tau_msg.wrench.torque.z = 0.0;
        tau_pub_->publish(tau_msg);
        return;
    }

    // 读取当前状态与目标指令（加锁保护）
    double x, y, yaw, vx, vy, omega;
    double t_x, t_y, t_yaw, t_speed;
    ControlMode current_mode;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        x     = current_x_;
        y     = current_y_;
        yaw   = current_yaw_;
        vx    = current_vx_;
        vy    = current_vy_;
        omega = current_omega_;

        t_x   = target_x_;
        t_y   = target_y_;
        t_yaw = target_yaw_;
        t_speed = target_speed_;   // [S-03] 纳入锁保护，消除数据竞争
        current_mode = active_mode_;
    }

    // [DP调试] 打印当前模式
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[ControlLoop] current_mode=%s | pos=(%.1f,%.1f) target=(%.1f,%.1f) error=(%.1f,%.1f)",
        current_mode == ControlMode::DP_POSITION ? "DP_POSITION" : "OTHER",
        x, y, t_x, t_y, t_x - x, t_y - y);

    // [架构补全] 如果是外部手动模式，直接切断闭环计算
    // if (current_auth == ControlAuthority::MANUAL_EXTERNAL) {
    //     return;
    // }

    // [C3/P1] dt 使用实测值，第一拍跳过
    if (last_time_.nanoseconds() == 0) {
        last_time_ = now;
        return;
    }
    double dt = (now - last_time_).seconds();
    last_time_ = now;
    // 合法性保护：dt 异常（系统负载尖刺或首帧）时退化为设定周期
    if (dt <= 0.0 || dt > 1.0) {
        dt = control_period_;
    }

    const bool in_dp_mode = (current_mode == ControlMode::DP_POSITION);
    const PIDGains& surge_g = in_dp_mode ? gains_surge_dp_ : gains_surge_;
    const PIDGains& sway_g  = in_dp_mode ? gains_sway_dp_  : gains_sway_;
    const PIDGains& yaw_g   = in_dp_mode ? gains_yaw_dp_   : gains_yaw_;
    const PIDGains& speed_g = in_dp_mode ? gains_speed_dp_ : gains_speed_;

    // ── 误差计算与拓扑隔离 ────────────────────────────────────────────────────────
    double error_surge = 0.0;
    double error_sway  = 0.0;
    double error_x_global = 0.0;
    double error_y_global = 0.0;

    if (current_mode == ControlMode::DP_POSITION) {
        // DP 模式：追踪世界坐标系下的绝对经纬锚点
        error_x_global = t_x - x;
        error_y_global = t_y - y;
        error_surge =  std::cos(yaw) * error_x_global + std::sin(yaw) * error_y_global;
        error_sway  = -std::sin(yaw) * error_x_global + std::cos(yaw) * error_y_global;
    } else {
        error_surge = 0.0;
        error_sway  = -target_cross_track_error_;
        integral_surge_ = 0.0;
    }
    double error_yaw = t_yaw - yaw;

    // 偏航误差归一化到 [-π, π]
    while (error_yaw >  M_PI) error_yaw -= 2.0 * M_PI;
    while (error_yaw < -M_PI) error_yaw += 2.0 * M_PI;

    // 【工业级最短转向路径取模】
    // 利用 atan2 的数学特性，自动将误差强行压平到 [-pi, pi] 之间
    // 例如: cmd=-40°(320°), meas=150° -> error=-190° -> atan2映射为+170°
    // 船会选择右转170°而不是左转190°（最短路径）
    error_yaw = std::atan2(std::sin(error_yaw), std::cos(error_yaw));

    // ── 调试日志：详细显示控制状态 ─────────────────────────────────────────
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[控制调试] 位置: (%.2f, %.2f)→(%.2f, %.2f) | XTE=%.1fm error_sway=%.1fm | 船首向: %.1f° | 目标首向: %.1f° | 偏航误差: %.2f°(%.3f rad)",
        x, y, t_x, t_y, target_cross_track_error_, error_sway,
        yaw * 180.0 / M_PI, t_yaw * 180.0 / M_PI, error_yaw * 180.0 / M_PI, error_yaw);

    // ── 微分项（速度反馈阻尼注入）────────────────────────────────────────────
    // 注意：这里 kd × (-v) 相当于阻尼系数注入，与传统 kd×de/dt 物理含义不同。
    // 优点：目标点阶跃时无微分冲击，稳态时自然收敛。
    // [NaN Firewall] 确保输入数据有效
    if (std::isnan(vx) || std::isnan(vy) || std::isnan(omega)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] 检测到速度/角速度为NaN，使用零微分");
        vx = 0.0; vy = 0.0; omega = 0.0;
    }

    double error_dot_surge = -vx;
    double error_dot_sway  = -vy;
    double error_dot_yaw   = -omega;

    // 低通滤波（使用新的 lpf_alpha 参数）
    error_dot_surge = lpf_alpha_ * error_dot_surge + (1.0 - lpf_alpha_) * prev_deriv_surge_;
    error_dot_sway  = lpf_alpha_ * error_dot_sway  + (1.0 - lpf_alpha_) * prev_deriv_sway_;
    error_dot_yaw   = lpf_alpha_ * error_dot_yaw   + (1.0 - lpf_alpha_) * prev_deriv_yaw_;
    prev_deriv_surge_ = error_dot_surge;
    prev_deriv_sway_  = error_dot_sway;
    prev_deriv_yaw_   = error_dot_yaw;

    // ── 积分更新（积分分离防饱和技术）─────────────────────────────────────────
    // 【破局修复】大角度转向时冻结积分，防止积分饱和导致超调
    const double YAW_INTEGRAL_SEPARATION_THRESHOLD = 20.0 * M_PI / 180.0;  // 20度阈值，平衡超调抑制和稳态误差消除
    if (std::abs(error_yaw) < YAW_INTEGRAL_SEPARATION_THRESHOLD) {
        // 只有当偏航误差<20°时，才允许积分累积（用于消除稳态误差）
        integral_yaw_ += error_yaw * dt;
    } else {
        // 大误差时直接清零积分，防止历史误差累积导致超调
        integral_yaw_ = 0.0;
    }

    // [修复] 恢复纵荡/横荡 I 控制
    // [Anti-Windup Layer1] error_surge<0 && integral>0: 超速衰减
    if (error_surge < 0.0 && integral_surge_ > 0.0) {
        integral_surge_ *= 0.70;
    } else {
        integral_surge_ += error_surge * dt;
    }
    integral_sway_  += error_sway  * dt;

    // 积分状态软限幅（防止在大误差下积分溢出，Anti-windup 的第一道防线）
    integral_surge_ = std::clamp(integral_surge_, -i_lim_surge_, i_lim_surge_);
    integral_sway_  = std::clamp(integral_sway_,  -i_lim_sway_,  i_lim_sway_);
    integral_yaw_   = std::clamp(integral_yaw_,   -i_lim_yaw_,   i_lim_yaw_);

    // ── 控制死区处理 ──────────────────────────────────────────────────────────
    double error_surge_deadzone = (std::abs(error_surge) < deadzone_radius_) ? 0.0 : error_surge;
    double error_sway_deadzone = (std::abs(error_sway) < deadzone_radius_) ? 0.0 : error_sway;
    
    // ── NDO 状态观测更新 ──────────────────────────────────────────────────────
    Eigen::Vector3d nu(vx, vy, omega);

    // [NaN Firewall] NDO更新前检查输入
    if (nu.hasNaN()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] NDO输入包含NaN，跳过NDO更新");
        nu.setZero();
    }

    update_ndo(nu, tau_last_, dt);

    // [NaN Firewall] NDO更新后检查输出
    if (d_hat_.hasNaN()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] NDO输出包含NaN，重置为0");
        d_hat_.setZero();
    }

    // ── [SOTA] 鲁棒滑模控制 (Sliding Mode Control) ───────────────────────
    // 构造滑模面 S = \lambda e + \lambda_{int} \int e + \dot{e}
    // 原PID输出在数学上直接等价于定义了一个非线性滑模面 S
    // ── [Task 2] 航速自适应增益调度 (SMC Gain Scheduling) ─────────────────────
    // 舵效与航速的平方成正比 (T ∝ u²)，因此控制增益应与 1/u² 成正比
    // 高速时增益小（柔和控制，防止超调），低速时增益大（强力控制）
    double u_nom = 5.0;  // 额定工况航速 m/s
    double U_current = std::sqrt(vx * vx + vy * vy);
    if (std::isnan(U_current) || std::isinf(U_current) || U_current < 0.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Alert] U_current异常: sqrt(%.2f^2 + %.2f^2) = %.2f，强制设为0.5",
            vx, vy, U_current);
        U_current = 0.0;
    }
    U_current = std::max(U_current, 0.5);  // 防除零保护

    double alpha = (u_nom * u_nom) / (U_current * U_current);  // 调度因子
    if (std::isnan(alpha) || std::isinf(alpha)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Alert] alpha异常: (%.1f^2)/(%.2f^2) = %.2f，强制设为1.0",
            u_nom, U_current, alpha);
        alpha = 1.0;
    }
    alpha = std::clamp(alpha, 0.2, 2.0);  // [Nomoto] U_low->Kp*2.0, U_high->Kp*0.2

    // 动态增益：Kp 与 1/u² 成正比，Kd 用平方根保持阻尼比一致
    double dyn_Kp_surge = surge_g.kp * alpha;
    double dyn_Kd_surge = surge_g.kd * std::sqrt(alpha);
    double dyn_Kp_sway  = sway_g.kp  * alpha;
    double dyn_Kd_sway  = sway_g.kd  * std::sqrt(alpha);
    double dyn_Kp_yaw   = yaw_g.kp   * alpha;
    double dyn_Kd_yaw   = yaw_g.kd   * std::sqrt(alpha);

    // [调试日志] 每 3 秒打印一次调度因子
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[Gain Sched] U=%.2f m/s alpha=%.3f | Kp_yaw: %.0f→%.0f, Kd_yaw: %.0f→%.0f",
        U_current, alpha, yaw_g.kp, dyn_Kp_yaw, yaw_g.kd, dyn_Kd_yaw);

    // 使用自适应增益构建滑模面 S
    double S_surge = dyn_Kp_surge * error_surge_deadzone
                   + surge_g.ki * integral_surge_
                   + dyn_Kd_surge * error_dot_surge;

    double S_sway  = dyn_Kp_sway  * error_sway_deadzone
                   + sway_g.ki  * integral_sway_
                   + dyn_Kd_sway  * error_dot_sway;

    double S_yaw   = dyn_Kp_yaw   * error_yaw
                   + yaw_g.ki   * integral_yaw_
                   + dyn_Kd_yaw   * error_dot_yaw;

    // sat() 边界层函数：消除传统 Sign(S) 带来的剧烈 Chattering 震颤
    auto sat = [](double s, double phi) {
        if (phi < 1e-6) return (s > 0) ? 1.0 : ((s < 0) ? -1.0 : 0.0);
        return std::clamp(s / phi, -1.0, 1.0);
    };

    // [M-02] 限制巡航模式下的横荡强行 bang-bang 拉锯力
    double active_k_robust_sway = (current_mode == ControlMode::HEADING_SPEED_AUTOPILOT)
                                  ? (sway_g.k_robust * 0.05) // 防止产生超过500k的抗扰指令
                                  : sway_g.k_robust;

    // SMC 控制律 = 等效控制 (S) + 鲁棒切换律 (K * sat(S / \Phi))
    double force_x_smc  = S_surge + surge_g.k_robust * sat(S_surge, surge_g.phi);
    double force_y_smc  = S_sway  + active_k_robust_sway * sat(S_sway, sway_g.phi);
    double torque_z_smc = S_yaw   + yaw_g.k_robust * sat(S_yaw, yaw_g.phi);

    // ── 物理前馈叠加与输出限幅 ─────────────────────────────────────────────────
    //抵消 NDO 估计的环境干扰力: tau_cmd = tau_smc - d_hat
    double force_x_raw  = force_x_smc - d_hat_.x();
    double force_y_raw  = force_y_smc - d_hat_.y();
    
    // [M-06/O-01] 偏航符号约定映射：
    // 在右手法则中，艏向右转(Starboard)通常为负力矩。此处由于控制节点
    // 采用标准 NED (z 轴朝下，右转为正，即顺时针右转应受正力矩)，但外部
    // 推力分配单元与动力学坐标系在偏流角/力矩定义上存在装配惯例差异。
    // 使用 YAML 配置的 yaw_sign_ 将艏向力矩对齐至系统整体环境的约定正方向。
    double active_yaw_sign = in_dp_mode ? yaw_sign_dp_ : yaw_sign_cruise_;
    double torque_z_raw_corrected = active_yaw_sign * torque_z_smc - d_hat_.z();

    // [NaN Firewall] 最终输出检查
    if (std::isnan(force_x_raw) || std::isinf(force_x_raw)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] force_x_raw异常，强制归零");
        force_x_raw = 0.0;
    }
    if (std::isnan(force_y_raw) || std::isinf(force_y_raw)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] force_y_raw异常，强制归零");
        force_y_raw = 0.0;
    }
    if (std::isnan(torque_z_raw_corrected) || std::isinf(torque_z_raw_corrected)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] torque_z_raw异常，强制归零");
        torque_z_raw_corrected = 0.0;
    }

    // ── 【架构级修复】航向优先的动态刹车钳制 ─────────────────────────────────
    // 当偏航误差大于30°时，船舶处于大角度转向的危险期
    // 此时必须限制刹车功率，确保Mz有足够推力
    const double HEADING_ERROR_TURN_THRESHOLD = 30.0 * M_PI / 180.0;  // 30度阈值
    const double MAX_BRAKE_DURING_TURN = -20000.0;  // 大角度转向时最大允许倒车推力
    if (std::abs(error_yaw) > HEADING_ERROR_TURN_THRESHOLD) {
        if (force_x_raw < MAX_BRAKE_DURING_TURN) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[物理防呆] 大角度转向中(err=%.1f°)，抑制刹车: Fx要求 %.0f→%.0f N",
                error_yaw * 180.0 / M_PI, force_x_raw, MAX_BRAKE_DURING_TURN);
            force_x_raw = MAX_BRAKE_DURING_TURN;  // 强制钳制
        }
    }

    double force_x  = std::clamp(force_x_raw,  -max_reverse_surge_,  max_force_x_);  // 【ABS修复】限制倒车推力，防止失舵
    double force_y  = std::clamp(force_y_raw,  -max_force_y_,  max_force_y_);
    double torque_z = std::clamp(torque_z_raw_corrected, -max_torque_z_, max_torque_z_);

    if (!in_dp_mode &&
        min_yaw_moment_cruise_ > 0.0 &&
        std::abs(error_yaw) >= std::max(min_yaw_moment_error_rad_, 0.0)) {
        const double heading_correction_sign = std::copysign(1.0, active_yaw_sign * error_yaw);
        const bool raw_is_damping =
            std::abs(torque_z_raw_corrected) > 1e-6 &&
            torque_z_raw_corrected * heading_correction_sign < 0.0;
        const bool yaw_rate_already_helping =
            (error_yaw * omega > 0.0) &&
            std::abs(omega) >= min_yaw_moment_yaw_rate_suppress_rad_s_;
        const double span = std::max(1.0 * M_PI / 180.0,
            min_yaw_moment_full_error_rad_ - min_yaw_moment_error_rad_);
        const double t_floor = std::clamp(
            (std::abs(error_yaw) - min_yaw_moment_error_rad_) / span,
            0.0, 1.0);
        const double smooth_floor = t_floor * t_floor * (3.0 - 2.0 * t_floor);
        const double floored_mz = std::min(
            min_yaw_moment_cruise_ * smooth_floor,
            max_torque_z_);
        if (floored_mz > 1.0 && std::abs(torque_z) < floored_mz) {
            if (raw_is_damping || yaw_rate_already_helping) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[Yaw Moment Floor Soft] cruise err=%.1fdeg raw=%.0fNm floor=%.0fNm suppressed=%s yaw_rate=%.2fdeg/s",
                    error_yaw * 180.0 / M_PI, torque_z_raw_corrected,
                    floored_mz,
                    raw_is_damping ? "damping" : "yaw_rate",
                    omega * 180.0 / M_PI);
            } else {
                torque_z = heading_correction_sign * floored_mz;
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[Yaw Moment Floor Soft] cruise err=%.1fdeg raw=%.0fNm floor=%.0fNm cmd=%.0fNm yaw_rate=%.2fdeg/s",
                    error_yaw * 180.0 / M_PI, torque_z_raw_corrected,
                    floored_mz, torque_z, omega * 180.0 / M_PI);
            }
        }
    }


    // ── [DNV-Architecture] 航速自适应Mz限幅 ──────────────────────────────
    // 差速已禁用，Mz仅来自舵+侧推。舵效∝u²，侧推提供360kNm基底
    // Mz_max(u) = 360000 + sign(u)*0.5*rho*A*CL_alpha*u²*x_rudder*sin(35°)
    // 简化工业公式: Mz_max = 360000 + 2500 * u² (u in m/s, Nm)
    {
        double u_abs = std::abs(vx);
        double mz_rudder_cap = 360000.0 + 2500.0 * u_abs * u_abs;
        double configured_mz_limit = in_dp_mode ? max_torque_z_dp_ : max_torque_z_;
        double requested_floor = in_dp_mode ? min_yaw_moment_dp_ : min_yaw_moment_cruise_;
        double lower_mz_limit = std::min(requested_floor, configured_mz_limit);
        double mz_limit = std::clamp(mz_rudder_cap, lower_mz_limit, configured_mz_limit);
        torque_z = std::clamp(torque_z, -mz_limit, mz_limit);
    }
    if (in_dp_mode) {
        const double dp_position_error = std::hypot(error_x_global, error_y_global);
        const double dp_speed = std::hypot(vx, vy);
        constexpr double yaw_release_radius = 2.0;
        constexpr double yaw_release_speed = 0.05;
        const bool position_hold_settled =
            dp_position_error <= yaw_release_radius && dp_speed <= yaw_release_speed;
        if (!position_hold_settled) {
            const double old_torque_z = torque_z;
            torque_z = 0.0;
            integral_yaw_ = 0.0;
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[DP XY Priority] pos_err=%.1fm speed=%.2fm/s yaw_mz %.0f->0, hold position; no heading chase",
                dp_position_error, dp_speed, old_torque_z);
        }
    }

    // ── [A1/A2/P0] Anti-windup：Back-Calculation 法 ──────────────────────────
    // 原理：用限幅前后的差值（饱和量）反向修正积分，自动消除 windup。
    if (surge_g.ki > 1e-9) {
        integral_surge_ += (1.0 / surge_g.ki) * (force_x  - force_x_raw);
    }
    if (sway_g.ki  > 1e-9) {
        integral_sway_  += (1.0 / sway_g.ki)  * (force_y  - force_y_raw);
    }
    if (yaw_g.ki   > 1e-9) {
     // [修复历史遗留的 Yaw Anti-windup 符号错误]：由于物理域被翻转，差值为反向
        integral_yaw_   += (1.0 / yaw_g.ki)   * (torque_z - torque_z_raw_corrected);
    }

    // ── [S-03 / M-01 新增与修复] 航速控制回路 ───────────────────────────────────
    // 注意: U_current 已在上面的增益调度块中计算并声明
    if (std::isnan(U_current) || U_current < 0.0) U_current = 0.0;  // [NaN Firewall]
    U_current = std::max(0.0, U_current);  // 强制非负

    // [NaN Firewall] t_speed检查
    if (std::isnan(t_speed) || std::isinf(t_speed)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] t_speed异常，使用零");
        t_speed = 0.0;
    }

    double speed_feedback = (current_mode == ControlMode::HEADING_SPEED_AUTOPILOT)
        ? vx
        : U_current;
    if (std::isnan(speed_feedback) || std::isinf(speed_feedback)) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[NaN Firewall] speed_feedback异常，使用零");
        speed_feedback = 0.0;
    }

    double error_speed = t_speed - speed_feedback;  // [S-03] 巡航用有符号纵荡速度，避免倒车被视为达速

    // [S-03] 修复缺失的速度积分累加 (激活 ki_speed 的调平能力)
    // [Anti-Windup Layer1] error_speed<0 && integral>0: 超速衰减
    if (error_speed < 0.0 && integral_speed_ > 0.0) {
        integral_speed_ *= 0.70;
    } else {
        integral_speed_ += error_speed * dt;
    }

    // 航速误差死区处理
    double error_speed_deadzone = (std::abs(error_speed) < speed_deadzone_) ? 0.0 : error_speed;

    // [M-01] 航速误差微分（真正的误差变化率，消除恒定制动力）
    // [NaN Firewall] 保护除以零
    double dt_safe = std::max(dt, 1e-6);  // 防止 dt=0 导致无穷大
    double error_dot_speed = (std::abs(speed_feedback) < 1e-6 && std::abs(previous_u_) < 1e-6)
        ? 0.0  // 静止时微分设为0，避免噪声
        : -(speed_feedback - previous_u_) / dt_safe;
    previous_u_ = speed_feedback; // 保存供下次微分

    // 航速滑模面 S_speed
    double S_speed = speed_g.kp * error_speed_deadzone
                   + speed_g.ki * integral_speed_
                   + speed_g.kd * error_dot_speed;

    // 航速 SMC 控制律
    double force_speed_raw = S_speed + speed_g.k_robust * sat(S_speed, speed_g.phi);
    if (speed_drag_feedforward_enable_
        && current_mode == ControlMode::HEADING_SPEED_AUTOPILOT
        && t_speed > speed_deadzone_) {
        double drag_ff = speed_drag_linear_ * t_speed
                       + speed_drag_quadratic_ * t_speed * std::abs(t_speed);
        force_speed_raw += drag_ff;
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[SpeedFF] 静水阻力前馈: Uref=%.2f m/s FF=%.0f N | raw=%.0f N",
            t_speed, drag_ff, force_speed_raw);
    }

    // 航速输出限幅
    double force_speed = std::clamp(force_speed_raw, -max_reverse_surge_, max_thrust_surge_);

    // 航速积分Anti-windup
    if (speed_g.ki > 1e-9) {
        integral_speed_ += (1.0 / speed_g.ki) * (force_speed - force_speed_raw);
    }

    // 积分状态限幅
    if (speed_g.ki > 1e-9) {
        double i_lim_speed = max_thrust_surge_ / speed_g.ki * 2.0;
        integral_speed_ = std::clamp(integral_speed_, -i_lim_speed, i_lim_speed);
    }

    // [Anti-Windup Layer2] 速度硬上限：永远对照 max_transit_speed_ (8.0 m/s)
    if (current_mode != ControlMode::DP_POSITION) {
        const double SPEED_CAP   = 8.0;
        const double CAP_TOL     = std::max(1.5, SPEED_CAP * 0.25);
        const double CAP_TRIGGER = SPEED_CAP + CAP_TOL;
        if (U_current > CAP_TRIGGER) {
            double drag_at_cap = speed_drag_linear_ * SPEED_CAP
                               + speed_drag_quadratic_ * SPEED_CAP * SPEED_CAP;
            double excess      = U_current - CAP_TRIGGER;
            double cap_ratio   = std::min(excess / CAP_TOL, 1.0);
            double blended     = force_speed * (1.0 - cap_ratio)
                               + drag_at_cap   * cap_ratio;
            double min_steer   = speed_drag_linear_ * 2.5;
            force_speed        = std::max(blended, min_steer);
            integral_speed_ = std::min(integral_speed_, 0.0);
            integral_surge_ = std::min(integral_surge_, 0.0);
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 500,
                "[SPEED CAP] U=%.2f > cap=%.2f, ratio=%.2f, Fx=%.0f",
                U_current, CAP_TRIGGER, cap_ratio, force_speed);
        }
    }

    // ── 发布广义力指令 ────────────────────────────────────────────────────────
    double force_x_final = 0.0;
    double force_y_final = force_y; // 默认保留 DP 算出的侧向力

    if (current_mode == ControlMode::DP_POSITION) {
        force_x_final = force_x;
    } else {
        force_x_final = force_speed;
        // 真实船舶巡航原则：高速/航迹保持不直接命令横向力 Fy；
        // 横偏修正通过航向偏置 -> 艏摇力矩 -> 舵产生曲率完成。
        force_y_final = 0.0;
    }

    // ── [物理破局] 起步推力优先 ─────────────────────────────────────────────────
    // 物理原理：极低速(<0.5m/s)且航向误差大(>20°)时，转向力矩是徒劳的
    // 解决：强行注入起步推力，优先让船动起来再说
    double heading_error_abs = std::abs(error_yaw);
    if (current_mode != ControlMode::DP_POSITION
        && U_current < 0.5
        && heading_error_abs > (20.0 * M_PI / 180.0)) {
        double starting_thrust_boost = 50000.0;  // 额外50kN起步推力
        force_x_final = std::max(force_x_final, starting_thrust_boost);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[物理破局] 起步推力优先: U=%.2f m/s | heading_err=%.1f° | Fx: %.0f→%.0f",
            U_current, heading_error_abs*180/M_PI, force_x_final, force_x_final + starting_thrust_boost);
    }

    // ── 【架构级修复】航向优先的动态刹车钳制 (ALOS模式) ──────────────────────────
    // 在ALOS巡航模式下，force_x_final来自speed控制器，需要额外钳制
    // 当偏航误差大于30°时，限制刹车功率确保Mz有足够推力
    if (heading_error_abs > HEADING_ERROR_TURN_THRESHOLD) {
        if (force_x_final < MAX_BRAKE_DURING_TURN) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[物理防呆-巡航] 大角度转向中(err=%.1f°)，抑制刹车: Fx %.0f→%.0f N",
                heading_error_abs*180/M_PI, force_x_final, MAX_BRAKE_DURING_TURN);
            force_x_final = MAX_BRAKE_DURING_TURN;
        }
    }

    geometry_msgs::msg::WrenchStamped tau_msg;
    tau_msg.header.stamp    = now;
    tau_msg.header.frame_id = "base_link";
    tau_msg.wrench.force.x  = force_x_final;
    tau_msg.wrench.force.y  = force_y_final;
    tau_msg.wrench.torque.z = torque_z;
    tau_pub_->publish(tau_msg);

    // RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
    //     "指令: Fx=%.1f (NDO:%.1f) Fy=%.1f (NDO:%.1f) Mz=%.1f (NDO:%.1f) |"
    //     " 误差: X=%.2f Y=%.2f Yaw=%.3f | U=%.2f m/s",
    //     force_x_final, d_hat_.x(), force_y, d_hat_.y(), torque_z, d_hat_.z(),
    //     error_surge, error_sway, error_yaw, U_current);

    // 缓存指令用于下一帧 NDO
    tau_last_ << force_x_final, force_y, torque_z;

    // 更新前次误差
    prev_error_surge_ = error_surge;
    prev_error_sway_  = error_sway;
    prev_error_yaw_   = error_yaw;
    prev_error_speed_ = error_speed;
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ShipControllerNode>());
    rclcpp::shutdown();
    return 0;
}
