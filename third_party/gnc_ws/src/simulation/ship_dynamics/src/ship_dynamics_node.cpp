/*
 * @brief 船舶动力学节点
 * @details 该节点根据输入的推进器推力和环境力，计算船舶的运动状态（位置、速度、姿态）。
 *          它使用基于动量守恒的动力学模型，考虑了船舶的附加质量和水动力阻尼。
 *         字段                      含义                坐标系
 * pose.pose.position.x     北向位置 η[0]，单位 m      世界系（map）
 * pose.pose.position.y     东向位置 η[1]，单位 m      世界系（map）
 * pose.pose.orientation.w/z  航向角四元数 w/z 分量    世界系（map）
 * twist.twist.linear.x     纵荡速度 u，单位 m/s      船体系（base_link）
 * twist.twist.linear.y     横荡速度 v，单位 m/s      船体系（base_link）
 * twist.twist.angular.z    偏航角速度 r，单位 rad/s  船体系（base_link）
 */



#include "ship_dynamics/ship_dynamics_node.hpp"
#include <cmath>
#include <iomanip>
#include <filesystem>
#include <algorithm>
#include <limits>
#include <std_msgs/msg/float64_multi_array.hpp>
#include <tf2/LinearMath/Quaternion.h>


namespace ship_dynamics {

ShipDynamicsNode::ShipDynamicsNode(const rclcpp::NodeOptions& options) 
    : rclcpp::Node("ship_dynamics_node", options) {
    
    // --- 船舶物理参数 ---
    this->declare_parameter("vessel.mass", 1.6e7);               // 排水量 (kg)
    this->declare_parameter("vessel.Lpp", 150.0);                // 船长 (m)
    this->declare_parameter("vessel.B", 25.0);                   // 船宽 (m)
    this->declare_parameter("vessel.d", 10.0);                   // 吃水 (m)
    this->declare_parameter("vessel.moment_of_inertia.Izz", 3.0e10); 
    
    // --- 附加质量 (Added Mass) ---
    this->declare_parameter("vessel.added_mass.X_dot_u", 1.6e6); // 纵荡附加质量 (通常为 mass 的 10%)
    this->declare_parameter("vessel.added_mass.Y_dot_v", 1.2e7); // 横荡附加质量 (通常为 mass 的 70%-100%)
    this->declare_parameter("vessel.added_mass.K_dot_p", 1.0e7); // 横摇附加质量
    this->declare_parameter("vessel.added_mass.N_dot_r", 2.0e10); // 偏航附加质量惯性矩
    this->declare_parameter("vessel.added_mass.Y_dot_r", 1.0e6); // 横荡附加耦合质量
    this->declare_parameter("vessel.added_mass.N_dot_v", 1.0e6); // 偏航附加耦合质量
    
    // --- 水动力阻尼系数 (Hydrodynamic Damping) ---
    // 纵向 (Surge)
    this->declare_parameter("vessel.hydrodynamic.X_u", -5.0e4);
    this->declare_parameter("vessel.hydrodynamic.X_uu", -2.0e5);
    // 横向 (Sway) & 交叉耦合
    this->declare_parameter("vessel.hydrodynamic.Y_v", -3.0e5);
    this->declare_parameter("vessel.hydrodynamic.Y_vv", -1.5e6);
    this->declare_parameter("vessel.hydrodynamic.Y_r", 1.0e6);   // 横荡受偏航影响
    // 横摇 (Roll)
    this->declare_parameter("vessel.hydrodynamic.K_p", -1.0e7);
    this->declare_parameter("vessel.hydrodynamic.K_pp", -5.0e7);
    // 旋转 (Yaw) & 交叉耦合
    this->declare_parameter("vessel.hydrodynamic.N_r", -8.0e9);
    this->declare_parameter("vessel.hydrodynamic.N_rr", -2.5e11);
    this->declare_parameter("vessel.hydrodynamic.N_v", -1.0e7);   // 偏航受横荡影响

    // --- 【新增】多推进器布局配置 ---
    // 声明最多 10 个推进器的参数，确保 YAML 中定义的推进器都能找到对应的参数
    this->declare_parameter("thrusters.num", 2);
    this->declare_parameter("thrusters.thruster_names", std::vector<std::string>{"t0", "t1"}); // [M-06] 显式声明
    // 声明 10 个推进器的参数
    for (int i = 0; i < 10; ++i) {
        std::string prefix = "thrusters.t" + std::to_string(i) + ".";
        this->declare_parameter(prefix + "x", -70.0 + i * 10.0);
        this->declare_parameter(prefix + "y", 0.0);
        this->declare_parameter(prefix + "is_azimuth", false);
        this->declare_parameter(prefix + "angle_default", 0.0);
        this->declare_parameter(prefix + "angle_min", -M_PI);
        this->declare_parameter(prefix + "angle_max", M_PI);
        this->declare_parameter(prefix + "angle_rate_limit", 0.5);   // rad/s
        this->declare_parameter(prefix + "thrust_rate_limit", 50000.0);  // N/s
    }

    // --- 环境与配置 ---
    this->declare_parameter("update_rate", 50.0);
    this->declare_parameter("time_scale", 1.0);
    this->declare_parameter("water_depth", 100.0);               // （实时）水深 (用于潜在的浅水效应扩展)
    this->declare_parameter("log_dir", std::string("/tmp/marine_sim_logs/"));
    
    // --- 速度限制参数 ---
    this->declare_parameter("vessel.limits.max_u", 0.5);         // 最大纵荡速度 (m/s)
    this->declare_parameter("vessel.limits.max_r", 0.2);         // 最大偏航角速度 (rad/s)
    this->declare_parameter("vessel.limits.abnormal_reset_u", 25.0);  // [FIX] raised for high-speed;  // [Fix] 高于 max_u=13.0，避免正常加速误触发 // 异常状态重置阈值 (m/s)

    // --- 初始位置参数 ---
    this->declare_parameter("initial_position.x", 0.0);
    this->declare_parameter("initial_position.y", 0.0);
    this->declare_parameter("initial_position.yaw", 0.0);
    this->declare_parameter("initial_velocity.u", 0.0);
    this->declare_parameter("initial_velocity.v", 0.0);
    this->declare_parameter("initial_velocity.r", 0.0);
    this->declare_parameter("auto_initial_yaw_from_route", true);
    this->declare_parameter("auto_initial_yaw_min_segment_m", 20.0);
    this->declare_parameter("auto_initial_yaw_max_speed_mps", 0.05);
    this->declare_parameter("auto_initial_yaw_max_position_offset_m", 2.0);

    RCLCPP_INFO(this->get_logger(), "自主驾驶级别船舶动力学节点已创建");

    // 初始化时间戳
    last_time_ = this->now();
    start_time_ = this->now();
    last_thruster_cmd_time_ = this->now();
    
    // 初始化节点
    initialize();
}

void ShipDynamicsNode::initialize() {
    RCLCPP_INFO(this->get_logger(), "初始化船舶动力学节点...");

    RCLCPP_INFO(this->get_logger(), "调用 load_parameters()...");
    load_parameters();
    RCLCPP_INFO(this->get_logger(), "load_parameters() 调用完成");
    
    RCLCPP_INFO(this->get_logger(), "调用 initialize_mass_matrix()...");
    initialize_mass_matrix();
    RCLCPP_INFO(this->get_logger(), "initialize_mass_matrix() 调用完成");

    double initial_u = get_param_safe("initial_velocity.u", 0.0);
    double initial_v = get_param_safe("initial_velocity.v", 0.0);
    double initial_r = get_param_safe("initial_velocity.r", 0.0);
    nu_ = {initial_u, initial_v, 0.0, initial_r};
    // 从参数加载初始位置
    // [诊断] 打印所有初始位置相关参数，确认是否正确覆盖
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.x declared=%.2f", 0.0);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.y declared=%.2f", 0.0);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.yaw declared=%.2f", 0.0);
    double initial_x = get_param_safe("initial_position.x", 0.0);
    double initial_y = get_param_safe("initial_position.y", 0.0);
    double initial_yaw = get_param_safe("initial_position.yaw", 0.0);
    initial_x_ = initial_x;
    initial_y_ = initial_y;
    auto_initial_yaw_applied_ = false;
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_velocity.u READ=%.2f", initial_u);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_velocity.v READ=%.2f", initial_v);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_velocity.r READ=%.4f", initial_r);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.x READ=%.2f", initial_x);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.y READ=%.2f", initial_y);
    RCLCPP_INFO(this->get_logger(), "[DIAG] initial_position.yaw READ=%.2f", initial_yaw);
    eta_ = {initial_x, initial_y, 0.0, initial_yaw}; // roll 默认为 0.0
    psi_continuous_ = initial_yaw;
    tau_env_ = {0.0, 0.0, 0.0, 0.0};
    tau_thruster_ = {0.0, 0.0, 0.0, 0.0};
    last_thruster_cmd_time_ = this->now();
    RCLCPP_INFO(this->get_logger(), "初始位置设置为: (%.2f, %.2f), 航向: %.2f rad", initial_x, initial_y, initial_yaw);
    RCLCPP_INFO(this->get_logger(), "初始速度设置为: u=%.2f m/s, v=%.2f m/s, r=%.4f rad/s", initial_u, initial_v, initial_r);

    env_force_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/total_load", 10, 
        std::bind(&ShipDynamicsNode::env_force_callback, this, std::placeholders::_1));



    // 接收推力分配节点发来的电机指令 (例如 [2500.0, 2500.0, 1000.0])
    thruster_cmd_sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
        "/thruster/commands", 10, 
        std::bind(&ShipDynamicsNode::thruster_cmd_callback, this, std::placeholders::_1));
    if (auto_initial_yaw_from_route_) {
        auto route_init_qos = rclcpp::QoS(10).transient_local().reliable();
        waypoint_path_sub_ = this->create_subscription<nav_msgs::msg::Path>(
            "/ship/waypoints", route_init_qos,
            std::bind(&ShipDynamicsNode::waypoint_path_callback, this, std::placeholders::_1));
        initial_route_yaw_sub_ = this->create_subscription<std_msgs::msg::Float64>(
            "/sim/initial_route_yaw", route_init_qos,
            std::bind(&ShipDynamicsNode::initial_route_yaw_callback, this, std::placeholders::_1));
        RCLCPP_INFO(
            this->get_logger(),
            "[AUTO_INITIAL_YAW] enabled: min_segment=%.1fm max_speed=%.3fm/s max_offset=%.1fm topics=/ship/waypoints,/sim/initial_route_yaw",
            auto_initial_yaw_min_segment_m_,
            auto_initial_yaw_max_speed_mps_,
            auto_initial_yaw_max_position_offset_m_);
    }
    // 修正点：这里的 create_publisher 会根据成员变量类型自动返回 LifecyclePublisher
    odom_pub_ = this->create_publisher<nav_msgs::msg::Odometry>("/ship/odometry", 10);
    heading_pub_ = this->create_publisher<std_msgs::msg::Float64>("/ship/heading", 10); // M-07
    
    // 初始化 TF2 变换广播器
    tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);
    
    // 创建水深订阅器，用于接收外部发布的水深信息
    depth_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/env/water_depth", 10, 
        std::bind(&ShipDynamicsNode::water_depth_callback, this, std::placeholders::_1)
    );

    // [FDI] 健康状态发布器 (5Hz)
    health_pub_ = this->create_publisher<std_msgs::msg::Float64MultiArray>("/thruster/health_status", 5);

    // [FDI] 故障注入订阅器
    fault_inject_sub_ = this->create_subscription<std_msgs::msg::Int32>(
        "/inject_fault", 10,
        std::bind(&ShipDynamicsNode::inject_fault_callback, this, std::placeholders::_1));

    // 运行时 reset：orchestrator 经 gnc_bridge 发 /ship/dynamics_reset。
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ShipDynamicsNode::reset_callback, this, std::placeholders::_1));

    initialize_csv_file();

    RCLCPP_INFO(this->get_logger(), "配置完成。质量: %.2e, I_zz: %.2e", v_config_.phys.mass, v_config_.phys.Izz);
    RCLCPP_INFO(this->get_logger(), "推进器数量: %zu", thrusters_.size());

    // 创建定时器
    timer_ = this->create_wall_timer(
        std::chrono::milliseconds(static_cast<int>(1000.0 / update_rate_)),
        std::bind(&ShipDynamicsNode::update_dynamics, this));

    RCLCPP_INFO(this->get_logger(), "船舶动力学节点初始化完成，开始发布里程计数据...");
}

void ShipDynamicsNode::cleanup() {
    RCLCPP_INFO(this->get_logger(), "清理船舶动力学节点...");
    timer_.reset();
    env_force_sub_.reset();
    thruster_cmd_sub_.reset();
    waypoint_path_sub_.reset();
    initial_route_yaw_sub_.reset();
    depth_sub_.reset();
    odom_pub_.reset();
    heading_pub_.reset();
    if (csv_file_.is_open()) csv_file_.close();
    RCLCPP_INFO(this->get_logger(), "船舶动力学节点清理完成");
}

/**
 * 浅水修正系数计算
 * 参考：Sheng & Xu (2014) 的经验公式
 */
void ShipDynamicsNode::calculate_shallow_water_correction(double h, double d, 
                                                        double& f_mass, double& f_drag) {
    double h_d_ratio = h / d;

    // 当水深超过 4 倍吃水时，认为属于深水区，不进行修正
    if (h_d_ratio >= 4.0) {
        f_mass = 1.0;
        f_drag = 1.0;
        return;
    }

    // 极端浅水报警
    if (h_d_ratio < 1.2) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000, 
            "极端浅水警告: 水深/吃水比 = %.2f，存在搁浅风险！", h_d_ratio);
    }

    // 浅水临界保护：水深不能小于吃水（否则搁浅）
    double gamma = d / std::max(h, d + 0.1); 

    // 1. 附加质量修正 (f_mass)
    // 根据 Millward 公式：随着 gamma 增大，修正系数呈非线性上升
    f_mass = 1.0 + 0.41 * gamma + 1.26 * std::pow(gamma, 2) + 3.45 * std::pow(gamma, 3);

    // 2. 阻力修正 (f_drag)
    // 浅水引起的阻力增加系数
    f_drag = 1.0 + 0.5 * std::pow(gamma, 2) + 2.5 * std::pow(gamma, 4);
    
    // 限制最大修正倍数，防止数值爆炸
    f_mass = std::min(f_mass, 3.0);
    f_drag = std::min(f_drag, 5.0);
}



template<typename T>
T ShipDynamicsNode::get_param_safe(const std::string &name, T default_val) {
    try {
        // [修复] 强制获取最新参数值，忽略缓存
        // 先尝试 declare_parameter（如果参数不存在则创建）
        // 然后获取实际值（launch/YAML 覆盖的值优先）
        if (!this->has_parameter(name)) {
            this->declare_parameter<T>(name, default_val);
        }
        auto param = this->get_parameter(name);
        return param.get_value<T>();
    } catch (const std::exception &e) {
        RCLCPP_ERROR(this->get_logger(), "参数 [%s] 加载失败: %s. 使用默认值.", name.c_str(), e.what());
        return default_val;
    }
}

void ShipDynamicsNode::load_parameters() {
    RCLCPP_INFO(this->get_logger(), "开始加载船舶动力学参数...");

    // 1. 物理参数加载
    v_config_.phys.mass        = get_param_safe("vessel.mass", 1.6e7);
    v_config_.phys.Ixx         = get_param_safe("vessel.moment_of_inertia.Ixx", 2.0e7); // 横摇惯量
    v_config_.phys.Izz         = get_param_safe("vessel.moment_of_inertia.Izz", 3e10);
    v_config_.phys.GM          = get_param_safe("vessel.GM", 1.5);                      // 初稳性高
    v_config_.phys.draft       = get_param_safe("vessel.d", 8.0);
    v_config_.phys.water_depth = get_param_safe("water_depth", 50.0);
    v_config_.phys.gravity     = get_param_safe("vessel.gravity", 9.80665);             // 重力加速度

    // 2. 附加质量 (Added Mass)
    v_config_.am.X_dot_u = get_param_safe("vessel.added_mass.X_dot_u", 5e6);
    v_config_.am.Y_dot_v = get_param_safe("vessel.added_mass.Y_dot_v", 3.5e7);
    v_config_.am.K_dot_p = get_param_safe("vessel.added_mass.K_dot_p", 1.0e7);
    v_config_.am.N_dot_r = get_param_safe("vessel.added_mass.N_dot_r", 2e10);
    v_config_.am.Y_dot_r = get_param_safe("vessel.added_mass.Y_dot_r", 1e6);
    v_config_.am.N_dot_v = get_param_safe("vessel.added_mass.N_dot_v", 1e6);

    // 3. 水动力阻尼 (Hydrodynamic) - 自动处理绝对值逻辑
    v_config_.hydro.X_u  = -std::abs(get_param_safe("vessel.hydrodynamic.X_u", -5e4));
    v_config_.hydro.X_uu = -std::abs(get_param_safe("vessel.hydrodynamic.X_uu", -2e5));
    v_config_.hydro.Y_v  = -std::abs(get_param_safe("vessel.hydrodynamic.Y_v", -3e5));
    v_config_.hydro.Y_vv = -std::abs(get_param_safe("vessel.hydrodynamic.Y_vv", -1.5e6));
    v_config_.hydro.K_p  = -std::abs(get_param_safe("vessel.hydrodynamic.K_p", -1.0e7));
    v_config_.hydro.K_pp = -std::abs(get_param_safe("vessel.hydrodynamic.K_pp", -5.0e7));
    v_config_.hydro.N_r  = -std::abs(get_param_safe("vessel.hydrodynamic.N_r", -8e7));
    v_config_.hydro.N_rr = -std::abs(get_param_safe("vessel.hydrodynamic.N_rr", -2.5e9));
    
    // 4. 耦合项
    v_config_.hydro.Y_r = get_param_safe("vessel.hydrodynamic.Y_r", 1e6);
    v_config_.hydro.N_v = get_param_safe("vessel.hydrodynamic.N_v", -1e7);

    // 5. 通用配置
    update_rate_ = get_param_safe("update_rate", 50.0);
    time_scale_ = std::clamp(get_param_safe("time_scale", 1.0), 0.1, 20.0);
    log_dir_     = get_param_safe<std::string>("log_dir", "/tmp/");
    auto_initial_yaw_from_route_ = get_param_safe("auto_initial_yaw_from_route", true);
    auto_initial_yaw_min_segment_m_ = get_param_safe("auto_initial_yaw_min_segment_m", 20.0);
    auto_initial_yaw_max_speed_mps_ = get_param_safe("auto_initial_yaw_max_speed_mps", 0.05);
    auto_initial_yaw_max_position_offset_m_ = get_param_safe("auto_initial_yaw_max_position_offset_m", 2.0);
    
    // 6. 速度限制参数
    v_config_.limits.max_u = get_param_safe("vessel.limits.max_u", 3.0);
    v_config_.limits.max_r = get_param_safe("vessel.limits.max_r", 0.2);
    v_config_.limits.abnormal_reset_u = get_param_safe("vessel.limits.abnormal_reset_u", 10.0);

    // 7. 推进器布局优化加载
    RCLCPP_INFO(this->get_logger(), "开始加载推进器配置...");
    load_thruster_configs(); 
    RCLCPP_INFO(this->get_logger(), "推进器配置加载完成");
    RCLCPP_INFO(this->get_logger(), "推进器数量: %zu", thrusters_.size());

    // [FDI] 初始化故障检测状态
    thruster_healthy_.resize(thrusters_.size(), true);
    thruster_residual_.resize(thrusters_.size(), 0.0);
    residual_threshold_.resize(thrusters_.size(), 0.50);  // [修复] 50% 残差阈值，避免启动时误报
    fault_counter_.resize(thrusters_.size(), 0);
    last_health_pub_time_ = this->now();
    RCLCPP_INFO(this->get_logger(), "[FDI] 故障检测模块已初始化");
    
    // 8. 打印加载的参数
    RCLCPP_INFO(this->get_logger(), "加载的参数: mass=%.2e, Izz=%.2e, max_u=%.2f, max_r=%.2f", 
                v_config_.phys.mass, v_config_.phys.Izz, v_config_.limits.max_u, v_config_.limits.max_r);
}

void ShipDynamicsNode::load_thruster_configs() {
    RCLCPP_INFO(this->get_logger(), "进入 load_thruster_configs() 函数");

    // [M-06] 从已声明的参数中读取推进器名称列表
    std::vector<std::string> thruster_names = this->get_parameter("thrusters.thruster_names").as_string_array();

    int num = static_cast<int>(thruster_names.size());
    RCLCPP_INFO(this->get_logger(), "推进器数量: %d", num);

    thrusters_.clear();
    current_thruster_forces_.assign(num, 0.0);
    current_thruster_angles_.assign(num, 0.0);
    current_thruster_buckets_.assign(num, 0.0);

    for (int i = 0; i < num; ++i) {
        const std::string& name = thruster_names[i];
        std::string prefix = "thrusters." + name + ".";

        ThrusterConfig t;
        t.x = get_param_safe(prefix + "x", 0.0);
        t.y = get_param_safe(prefix + "y", 0.0);
        t.is_azimuth = get_param_safe(prefix + "is_azimuth", true);
        t.angle_default = get_param_safe(prefix + "angle_default", 0.0);
        t.angle_min = get_param_safe(prefix + "angle_min", -M_PI);
        t.angle_max = get_param_safe(prefix + "angle_max", M_PI);
        t.name = name;
        t.efficiency = get_param_safe(prefix + "efficiency", 1.0);
        // DNV Rate Limiting: 加载速率限制参数
        t.cmd_angle = 0.0;
        t.cmd_thrust = 0.0;
        t.actual_angle = t.angle_default;
        t.actual_thrust = 0.0;
        t.angle_rate_limit = get_param_safe(prefix + "angle_rate_limit", 0.5);
        t.thrust_rate_limit = get_param_safe(prefix + "thrust_rate_limit", 50000.0);

        thrusters_.push_back(t);
        RCLCPP_INFO(this->get_logger(), "推进器 %s 加载成功: x=%.2f, y=%.2f, is_azimuth=%s",
            name.c_str(), t.x, t.y, t.is_azimuth ? "true" : "false");
    }
    RCLCPP_INFO(this->get_logger(), "共加载 %d 个推进器", num);
    RCLCPP_INFO(this->get_logger(), "推进器数量: %zu", thrusters_.size());
    RCLCPP_INFO(this->get_logger(), "退出 load_thruster_configs() 函数");
}

void ShipDynamicsNode::initialize_mass_matrix() {
    double f_mass, f_drag;
    calculate_shallow_water_correction(v_config_.phys.water_depth, v_config_.phys.draft, f_mass, f_drag);

    // 浅水环境下的总质量阵：仅修正“附加质量”部分
    double m_x = v_config_.phys.mass + (v_config_.am.X_dot_u * f_mass); 
    double m_y = v_config_.phys.mass + (v_config_.am.Y_dot_v * f_mass); 
    double m_p = v_config_.phys.Ixx + (v_config_.am.K_dot_p * f_mass); 
    double m_yaw = v_config_.phys.Izz + (v_config_.am.N_dot_r * f_mass); 
    double m_yr = v_config_.am.Y_dot_r * f_mass; 
    double m_nv = v_config_.am.N_dot_v * f_mass;

    // 构建满秩耦合 4x4 质量矩阵 (surge, sway, roll, yaw)
    M_ << m_x, 0.0, 0.0, 0.0,
          0.0, m_y, 0.0, m_yr,
          0.0, 0.0, m_p, 0.0,
          0.0, m_nv, 0.0, m_yaw;

    // (S-04) 构建预求逆缓存，包含基础的奇异检测
    double det = M_.determinant();
    if (std::abs(det) < 1e-12) {
        RCLCPP_ERROR(this->get_logger(), "质量矩阵 M_ 发生奇异或接近奇异 (det=%.2e)，系统可能崩溃！", det);
        M_inv_ = Eigen::Matrix4d::Identity(); // 安全退路
    } else {
        M_inv_ = M_.inverse();
    }

    RCLCPP_INFO(this->get_logger(), "浅水修正应用成功 [4-DOF]：质量增强系数 %.2f, 阻尼增强系数 %.2f", f_mass, f_drag);
}

/**
 * 核心计算：compute_nu_dot
 * 实现了 4-DOF 耦合的 MMG 模型，包含科里奥利力、向心力以及横摇复原力矩
 * 使用 Eigen 矩阵运算提高数值稳定性和代码可读性
 */
Eigen::Vector4d ShipDynamicsNode::compute_nu_dot(const Eigen::Vector4d& nu, const Eigen::Vector4d& tau_total, double current_roll) {
    double u = nu(0);
    double v = nu(1);
    double p = nu(2);
    double r = nu(3);

    if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(p) || !std::isfinite(r)) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 1000, "检测到异常状态值: u=%.2f, v=%.2f, p=%.2f, r=%.2f", u, v, p, r);
        return Eigen::Vector4d::Zero();
    }

    Eigen::Vector4d D = Eigen::Vector4d::Zero();
    D(0) = (v_config_.hydro.X_u * u + v_config_.hydro.X_uu * u * std::abs(u)) * f_drag_cached_;
    D(1) = (v_config_.hydro.Y_v * v + v_config_.hydro.Y_vv * v * std::abs(v) + v_config_.hydro.Y_r * r) * f_drag_cached_;
    D(2) = (v_config_.hydro.K_p * p + v_config_.hydro.K_pp * p * std::abs(p)) * f_drag_cached_;
    D(3) = (v_config_.hydro.N_r * r + v_config_.hydro.N_rr * r * std::abs(r) + v_config_.hydro.N_v * v) * f_drag_cached_;

    double m_total_x = v_config_.phys.mass + v_config_.am.X_dot_u * f_mass_cached_;
    double m_total_y = v_config_.phys.mass + v_config_.am.Y_dot_v * f_mass_cached_;

    // 🔪 屠龙手术第二刀：标准3-DOF Coriolis (Fossen, x_g=0)
    // C_std 映射到4-DOF，代码约定 C = -C_std (因nu_dot = M⁻¹*(tau+C*nu+D))
    Eigen::Matrix4d C = Eigen::Matrix4d::Zero();
    C(0, 3) =  m_total_x * v;     // surge-sway-yaw coupling
    C(1, 3) = -m_total_y * u;     // sway-surge-yaw coupling
    C(3, 0) = -m_total_x * v;     // yaw-surge-sway (Munk part 1)
    C(3, 1) =  m_total_y * u;     // yaw-surge-sway (Munk part 2)
    Eigen::Vector4d C_vec = C * nu;

    double tau_restoring_p = -v_config_.phys.mass * v_config_.phys.gravity * v_config_.phys.GM * std::sin(current_roll);

    Eigen::Vector4d tau_net = tau_total + C_vec + D;
    tau_net(2) += tau_restoring_p;

    return M_inv_ * tau_net;
}

/**
 * RK4 积分器：保证高频控制下的数值稳定性 (4-DOF 扩展)
 * 使用 Eigen::Vector4d 进行矩阵运算
 */
Eigen::Vector4d ShipDynamicsNode::runge_kutta4(const Eigen::Vector4d& nu, const Eigen::Vector4d& tau_total, double current_roll, double dt) {
    Eigen::Vector4d k1 = compute_nu_dot(nu, tau_total, current_roll);

    Eigen::Vector4d nu_k2 = nu + k1 * dt * 0.5;
    Eigen::Vector4d k2 = compute_nu_dot(nu_k2, tau_total, current_roll);

    Eigen::Vector4d nu_k3 = nu + k2 * dt * 0.5;
    Eigen::Vector4d k3 = compute_nu_dot(nu_k3, tau_total, current_roll);

    Eigen::Vector4d nu_k4 = nu + k3 * dt;
    Eigen::Vector4d k4 = compute_nu_dot(nu_k4, tau_total, current_roll);

    return nu + (dt / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
}

void ShipDynamicsNode::reset_state() {
    RCLCPP_WARN(this->get_logger(), "重置船舶状态到安全值");
    nu_ = Eigen::Vector4d::Zero();
}

void ShipDynamicsNode::reset_to_origin(double yaw_rad, double u_mps) {
    // 运行时 reset：等价于 initialize() 的位置/速度设置，但位置恒回 origin (0,0)，
    // 航向/速度来自 reset 消息。由 reset_callback 调用，调用方持 data_mutex_。
    eta_ = {0.0, 0.0, 0.0, yaw_rad};
    nu_ = {u_mps, 0.0, 0.0, 0.0};
    psi_continuous_ = yaw_rad;
    initial_x_ = 0.0;
    initial_y_ = 0.0;
    auto_initial_yaw_applied_ = false;
    tau_env_ = {0.0, 0.0, 0.0, 0.0};
    tau_thruster_ = {0.0, 0.0, 0.0, 0.0};
    last_thruster_cmd_time_ = this->now();
    last_time_ = this->now();
    start_time_ = this->now();
    RCLCPP_INFO(this->get_logger(),
                "reset_to_origin: eta=(0,0,%.3frad), u=%.3f m/s", yaw_rad, u_mps);
}

void ShipDynamicsNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr msg) {
    // 收到 reset：位置恒回 origin (0,0)；航向/速度来自消息。
    // 绝对经纬度由 coordinate_transform 的 origin 重设决定（另一个订阅者）。
    const double yaw_rad = msg->heading_deg * M_PI / 180.0;
    const double u_mps = msg->sog_kn * 0.514444;  // kn -> m/s
    std::lock_guard<std::mutex> lock(data_mutex_);
    reset_to_origin(yaw_rad, u_mps);
}

double ShipDynamicsNode::normalize_angle(double angle) const {
    while (angle > M_PI) angle -= 2.0 * M_PI;
    while (angle < -M_PI) angle += 2.0 * M_PI;
    return angle;
}

void ShipDynamicsNode::waypoint_path_callback(const nav_msgs::msg::Path::SharedPtr msg) {
    if (!auto_initial_yaw_from_route_ || auto_initial_yaw_applied_) {
        return;
    }
    apply_auto_initial_yaw_from_path(*msg);
}

void ShipDynamicsNode::initial_route_yaw_callback(const std_msgs::msg::Float64::SharedPtr msg) {
    if (!auto_initial_yaw_from_route_ || auto_initial_yaw_applied_) {
        return;
    }
    if (!std::isfinite(msg->data)) {
        RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 3000,
            "[AUTO_INITIAL_YAW] ignored /sim/initial_route_yaw: non-finite yaw");
        return;
    }
    apply_auto_initial_yaw(msg->data, "/sim/initial_route_yaw", std::numeric_limits<double>::quiet_NaN());
}

bool ShipDynamicsNode::apply_auto_initial_yaw(
    double route_yaw, const std::string& source, double segment_len_m)
{
    if (auto_initial_yaw_applied_) {
        return false;
    }
    if (!std::isfinite(route_yaw)) {
        RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 3000,
            "[AUTO_INITIAL_YAW] ignored %s: non-finite yaw",
            source.c_str());
        return false;
    }
    const double speed = std::hypot(nu_(0), nu_(1));
    const double position_offset = std::hypot(eta_(0) - initial_x_, eta_(1) - initial_y_);
    if (speed > auto_initial_yaw_max_speed_mps_
        || position_offset > auto_initial_yaw_max_position_offset_m_) {
        auto_initial_yaw_applied_ = true;
        RCLCPP_WARN(
            this->get_logger(),
            "[AUTO_INITIAL_YAW] ignored %s permanently: ship already moving/off-start "
            "(speed=%.3fm/s, offset=%.2fm)",
            source.c_str(), speed, position_offset);
        return false;
    }

    route_yaw = normalize_angle(route_yaw);
    const double old_yaw = eta_(3);
    eta_(3) = route_yaw;
    psi_continuous_ = route_yaw;
    nu_(3) = 0.0;
    auto_initial_yaw_applied_ = true;
    if (std::isfinite(segment_len_m)) {
        RCLCPP_INFO(
            this->get_logger(),
            "[AUTO_INITIAL_YAW] applied yaw=%.2fdeg from %s len=%.1fm old_yaw=%.2fdeg",
            route_yaw * 180.0 / M_PI,
            source.c_str(),
            segment_len_m,
            old_yaw * 180.0 / M_PI);
    } else {
        RCLCPP_INFO(
            this->get_logger(),
            "[AUTO_INITIAL_YAW] applied yaw=%.2fdeg from %s old_yaw=%.2fdeg",
            route_yaw * 180.0 / M_PI,
            source.c_str(),
            old_yaw * 180.0 / M_PI);
    }
    return true;
}

bool ShipDynamicsNode::apply_auto_initial_yaw_from_path(const nav_msgs::msg::Path& path) {
    if (path.poses.size() < 2) {
        RCLCPP_WARN_THROTTLE(
            this->get_logger(), *this->get_clock(), 3000,
            "[AUTO_INITIAL_YAW] ignored: route has fewer than 2 waypoints");
        return false;
    }

    for (size_t i = 1; i < path.poses.size(); ++i) {
        const auto& p0 = path.poses[i - 1].pose.position;
        const auto& p1 = path.poses[i].pose.position;
        const double dx_north = p1.x - p0.x;
        const double dy_east = p1.y - p0.y;
        const double segment_len = std::hypot(dx_north, dy_east);
        if (!std::isfinite(dx_north) || !std::isfinite(dy_east) || !std::isfinite(segment_len)) {
            continue;
        }
        if (segment_len < auto_initial_yaw_min_segment_m_) {
            continue;
        }

        const double route_yaw = normalize_angle(std::atan2(dy_east, dx_north));
        return apply_auto_initial_yaw(
            route_yaw,
            "/ship/waypoints segment[" + std::to_string(i - 1) + "->" + std::to_string(i) + "]",
            segment_len);
    }

    RCLCPP_WARN_THROTTLE(
        this->get_logger(), *this->get_clock(), 3000,
        "[AUTO_INITIAL_YAW] ignored: no segment longer than %.1fm",
        auto_initial_yaw_min_segment_m_);
    return false;
}

void ShipDynamicsNode::update_dynamics() {
    static int update_count = 0;
    static rclcpp::Time last_update_log{0, 0, RCL_ROS_TIME};
    update_count++;
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[诊断] update_dynamics 调用计数: %d", update_count);
    if ((this->now() - last_update_log).seconds() > 3.0) {
        RCLCPP_INFO(this->get_logger(), "[诊断] update_dynamics 3秒统计: %d 次", update_count);
    if (time_scale_ > 1.01) {
        RCLCPP_INFO(this->get_logger(), "[FastTest] dynamics time_scale=%.2f, wall loop count=%d/3s", time_scale_, update_count);
    }
        last_update_log = this->now();
        update_count = 0;
    }

    try {
    auto now = this->now();
    double wall_dt = (now - last_time_).seconds();
    if (wall_dt <= 0 || wall_dt > 0.2) wall_dt = 1.0/update_rate_;
    last_time_ = now;
    const double dt = wall_dt * time_scale_;

    // 1. [优化] 线程安全地合成推力
    {
        std::lock_guard<std::mutex> lock(data_mutex_);
        // [MOD-20260327] 增加超时阈值到30秒，避免启动时序问题
        // 检查推力指令是否超时（超过30s，给足启动和恢复时间）
        if ((this->now() - last_thruster_cmd_time_).seconds() > 30.0) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "推力指令超时，已将推进器推力置零");
            tau_thruster_ = {0.0, 0.0, 0.0, 0.0};
        } else {
            // DNV Rate Limiting: 在应用推力前先进行速率限幅
            update_actuator_dynamics(dt);

            // [FDI] 更新推进器健康状态
            update_thruster_health(dt);

            // [FDI] 定期发布健康状态 (5Hz)
            auto now = this->now();
            if ((now - last_health_pub_time_).seconds() >= HEALTH_PUB_INTERVAL) {
                publish_health_status();
                last_health_pub_time_ = now;
            }

            collect_thruster_forces();
        }
    }

    // 2. [优化] 预计算环境修正系数，避免在 compute_nu_dot 的 RK4 循环中重复计算（节省 CPU）
    if (std::abs(v_config_.phys.water_depth - last_water_depth_) > 0.1) {
        calculate_shallow_water_correction(v_config_.phys.water_depth, v_config_.phys.draft, f_mass_cached_, f_drag_cached_);
        last_water_depth_ = v_config_.phys.water_depth;
    }

    // 3. 物理积分
    // [S-02 修复] 线程安全地读取环境力（env_force_callback 在 env_force_mutex_ 下写入）
    Eigen::Vector4d local_tau_env;
    {
        std::shared_lock<std::shared_mutex> lk(env_force_mutex_);
        local_tau_env = tau_env_;
    }
    // 环境力叠加到总力中 (包含了由于 4-DOF 新增的风载荷横倾分力)
    Eigen::Vector4d tau_total = tau_thruster_ + local_tau_env;
    double current_roll = eta_(2);

    nu_ = runge_kutta4(nu_, tau_total, current_roll, dt);

    // 计算线速度
    double linear_speed = std::sqrt(nu_(0)*nu_(0) + nu_(1)*nu_(1));
    
    // 限制线速度
    if (linear_speed > v_config_.limits.max_u) {
        double scale = v_config_.limits.max_u / linear_speed;
        nu_(0) *= scale;
        nu_(1) *= scale;
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000, "线速度超过限制 (%.2f m/s)，已限制到 %.2f m/s", linear_speed, v_config_.limits.max_u);
    }

    // [FIX] Soft yaw rate constraint: elastic restoring instead of hard clip
    const double max_r_soft = 0.3;
    const double k_soft = 2e7;  // [FIX] stiffer restoring  // [Task5] faster recovery from overshoot
    const double Iz_soft = v_config_.phys.Izz + v_config_.am.N_dot_r;
    if (std::abs(nu_(3)) > max_r_soft) {
        double excess = std::abs(nu_(3)) - max_r_soft;
        double r_dot_penalty = k_soft * excess / Iz_soft * dt;
        nu_(3) -= r_dot_penalty * std::copysign(1.0, nu_(3));
    }
    if (std::abs(nu_(3)) > 1.0) {
        nu_(3) = std::clamp(nu_(3), -1.0, 1.0);
    }

    // 检测异常状态 (以偏航 nu_[3] 为判定指标)
    // [Fix 1] 阈值参数化：使用 max_r + 0.2 余量，代替硬编码 0.5
    // [Fix 1] 增加 3 秒防抖，避免连续重置
    static rclcpp::Time last_reset_time{0, 0, RCL_ROS_TIME};
    double reset_threshold = 2.0;  // [FIX] let ship recover naturally  // [FIX] give soft constraint room to work
    if (linear_speed > v_config_.limits.abnormal_reset_u || std::abs(nu_[3]) > reset_threshold) {
        std::string reason = "";
        if (linear_speed > v_config_.limits.abnormal_reset_u) {
            reason = "线速度超过异常阈值 " + std::to_string(v_config_.limits.abnormal_reset_u) + " m/s";
        }
        if (std::abs(nu_[3]) > reset_threshold) {
            if (!reason.empty()) reason += " 和 ";
            reason += "角速度超过异常阈值 " + std::to_string(reset_threshold) + " rad/s";
        }
        // 3秒防抖
        if ((this->now() - last_reset_time).seconds() > 3.0) {
            reset_state();
            last_reset_time = this->now();
            RCLCPP_WARN(this->get_logger(), "检测到异常状态，已重置船舶状态。原因: %s", reason.c_str());
        } else {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "角速度 %.2f 超阈值 %.2f，但防抖未触发（距上次重置 %.1f 秒）",
                std::abs(nu_[3]), reset_threshold, (this->now() - last_reset_time).seconds());
        }
    }

    // 2. 位置更新 (World frame)
    double psi_old = eta_(3);
    double psi = eta_(3);
    double c = std::cos(psi);
    double s = std::sin(psi);

    Eigen::Vector4d nu_world;
    nu_world << c * nu_(0) - s * nu_(1),
                s * nu_(0) + c * nu_(1),
                nu_(2),
                nu_(3);

    eta_ += nu_world * dt;

    while (eta_(2) > M_PI) eta_(2) -= 2.0 * M_PI;
    while (eta_(2) < -M_PI) eta_(2) += 2.0 * M_PI;

    while (eta_(3) > M_PI) eta_(3) -= 2.0 * M_PI;
    while (eta_(3) < -M_PI) eta_(3) += 2.0 * M_PI;

    double delta_psi = eta_(3) - psi_old;
    if (delta_psi > M_PI) delta_psi -= 2.0 * M_PI;
    if (delta_psi < -M_PI) delta_psi += 2.0 * M_PI;
    psi_continuous_ += delta_psi;

    publish_odometry();
    record_to_csv();
    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "[update_dynamics 异常] %s", e.what());
    }
}

void ShipDynamicsNode::env_force_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
    std::unique_lock<std::shared_mutex> lk(env_force_mutex_); 
    tau_env_ = {msg->wrench.force.x, 
                msg->wrench.force.y, 
                msg->wrench.torque.x,  // Roll Force (K)
                msg->wrench.torque.z}; // Yaw Force (N)
}



/**
 * 水深回调函数
 * 接收外部发布的水深信息
 */

/**
 * 单个喷水推进器的受力模型
 * 实现: 喷嘴角限位 + 倒车斗非线性 + 洗流效应
 */
void ShipDynamicsNode::water_depth_callback(const std_msgs::msg::Float64::SharedPtr msg) {
    double new_depth = msg->data;
    
    // 验证水深值的合理性
    if (new_depth < 0.1) {
        RCLCPP_WARN(this->get_logger(), "接收到无效水深值: %.2f m，忽略更新", new_depth);
        return;
    }
    
    // 更新水深值
    v_config_.phys.water_depth = new_depth;
    
    // 当水深变化时，重新计算质量矩阵
    initialize_mass_matrix();
    
    RCLCPP_INFO(this->get_logger(), "更新水深: %.2f m", v_config_.phys.water_depth);
}

void ShipDynamicsNode::publish_odometry() {
    // [诊断] 每100次打印一次当前位置
    static int pub_count = 0;
    if (++pub_count % 100 == 0) {
        RCLCPP_INFO(this->get_logger(), "[ODOM_PUB] pos=(%.2f, %.2f) vel=(%.2f, %.2f)",
            eta_[0], eta_[1], nu_[0], nu_[1]);
    }

    auto m = nav_msgs::msg::Odometry();
    m.header.stamp = this->now();
    m.header.frame_id = "odom";  // [M-01 修复] 统一为 "odom"，与 EKF 输出及制导节点对齐
    m.child_frame_id = "base_link";
    m.pose.pose.position.x = eta_[0]; // 北向位置 (NED: North)
    m.pose.pose.position.y = eta_[1]; // 东向位置 (NED: East)
    
    // 四元数：包含 Roll 和 Yaw
    tf2::Quaternion q;
    q.setRPY(eta_[2], 0.0, eta_[3]); // Roll=eta_[2], Pitch=0, Yaw=eta_[3]
    m.pose.pose.orientation.x = q.x();
    m.pose.pose.orientation.y = q.y();
    m.pose.pose.orientation.z = q.z();
    m.pose.pose.orientation.w = q.w();
    
    m.twist.twist.linear.x = nu_[0]; // 纵荡速度
    m.twist.twist.linear.y = nu_[1]; // 横荡速度
    m.twist.twist.angular.x = nu_[2]; // 横摇角速度 p
    m.twist.twist.angular.z = nu_[3]; // 偏航角速度 r
    
    odom_pub_->publish(m);

    // [M-07] 单独抽取航向（度）发布给环境模块
    std_msgs::msg::Float64 heading_msg;
    heading_msg.data = eta_[3] * 180.0 / M_PI;
    while (heading_msg.data < 0.0) heading_msg.data += 360.0;
    while (heading_msg.data >= 360.0) heading_msg.data -= 360.0;
    heading_pub_->publish(heading_msg);
    
    // 发布 TF2 坐标变换
    geometry_msgs::msg::TransformStamped transform;
    transform.header.stamp = m.header.stamp;
    transform.header.frame_id = "odom";  // [M-01 修复] 与 odometry 消息一致
    transform.child_frame_id = "base_link";
    transform.transform.translation.x = eta_[0];
    transform.transform.translation.y = eta_[1];
    transform.transform.translation.z = 0.0;
    transform.transform.rotation = m.pose.pose.orientation;
    
    tf_broadcaster_->sendTransform(transform);
}

/**
 * 【核心升级】：异构推力合成引擎
 * 处理固定角度侧推与带限位的全回转推进器
 */
void ShipDynamicsNode::collect_thruster_forces() {
    double Fx_total = 0.0, Fy_total = 0.0, Mz_total = 0.0;

    for (size_t i = 0; i < thrusters_.size(); ++i) {
        double T = current_thruster_forces_[i]; // 接收到的推力指令
        // 【新增】：识别舵并应用流体力学升力/阻力模型
        if (thrusters_[i].name == "rudder" || thrusters_[i].name == "r1" || thrusters_[i].name == "r2") {
            // === DNV-Compliant Rudder Hydrodynamic Model ===
            // Reference: ITTC Maneuvering Committee (2017), Fossen (2011)
            // Includes: propeller wake wash, effective AoA, 3D lift/drag with stall
            double delta_cmd = current_thruster_angles_[i];
            double delta = std::clamp(delta_cmd, thrusters_[i].angle_min, thrusters_[i].angle_max);

            double u = nu_(0);
            double v = nu_(1);
            double r = nu_(3);
            double x_r = thrusters_[i].x;

            // --- Propeller Wake Wash (Actuator Disk Theory) ---
            const double rho = 1025.0;
            const double prop_diameter = 1.8;
            const double A_disk = M_PI * std::pow(prop_diameter / 2.0, 2);
            const double k_wash = 0.55;

            double u_wash = 0.0;
            // Find center propeller thrust (thruster with y~0 at stern)
            for (size_t j = 0; j < thrusters_.size(); ++j) {
                const auto& tj = thrusters_[j];
                if (tj.name != "rudder" && !tj.is_azimuth
                    && std::abs(tj.y) < 0.5 && tj.x < 0.0) {
                    double Tc = current_thruster_forces_[j];
                    if (Tc > 100.0) {
                        double u_ind = std::sqrt(2.0 * Tc / (rho * A_disk));
                        u_wash = k_wash * u_ind;
                    }
                    break;
                }
            }

            // Effective inflow at rudder
            double u_eff = u + u_wash;
            u_eff = std::max(u_eff, 0.3);
            double v_local = v + x_r * r;
            double U_R_sq = u_eff * u_eff + v_local * v_local;

            // --- 屠龙手术：摘除自激脑垂体 ---
            // 舵力只服从QP指令，不依赖来流角beta_R
            double alpha_R = delta;
            const double ALPHA_STALL = 35.0 * M_PI / 180.0;
            alpha_R = std::clamp(alpha_R, -ALPHA_STALL, ALPHA_STALL);
            const double A_rudder = 3.5;
            // --- 3D Lift & Drag Coefficients ---
            const double AR_rudder = 2.0;
            const double e_rudder = 0.85;
            const double CD_0 = 0.025;

            // 3D lift-curve slope (Helmbold correction)
            double CL_alpha = 2.0 * M_PI * AR_rudder / (AR_rudder + 2.0);
            double CL = CL_alpha * alpha_R;
            // Induced drag: CD_i = CL^2 / (pi * AR * e)
            double CD = CD_0 + CL * CL / (M_PI * AR_rudder * e_rudder);

            // --- Rudder Normal Force (F_N perp to blade) ---
            double qR = 0.5 * rho * A_rudder * U_R_sq;
            double F_N = qR * CL_alpha * alpha_R;

            // Body-frame projection
            // +delta -> trailing edge to port -> stern pushed to starboard (-Y)
            double Fx_rudder = -std::abs(F_N * std::sin(delta)) - qR * CD;
            double Fy_rudder = -F_N * std::cos(delta);  // stern pushed starboard = -Y

            Fx_total += Fx_rudder;
            Fy_total += Fy_rudder;
            Mz_total += (thrusters_[i].x * Fy_rudder) - (thrusters_[i].y * Fx_rudder);

            continue; // 舵的处理结束，跳过下方常规推进器的代码
        }

        double alpha;

        if (thrusters_[i].is_azimuth) {
            // --- 情况 A：全回转推进器 ---
            // 1. 获取控制指令中的角度
            double alpha_cmd = current_thruster_angles_[i];
            
            // 2. 考虑有效转向角度（限位饱和处理）
            // std::clamp 会将角度限制在 [min, max] 之间
            alpha = std::clamp(alpha_cmd, thrusters_[i].angle_min, thrusters_[i].angle_max);
            
            // 如果指令超限，打印警告（可选，高频运行时建议限流打印）
            if (alpha != alpha_cmd) {
                // RCLCPP_DEBUG(this->get_logger(), "推进器 %zu 角度指令超限，已限位", i);
            }
        } else {
            // --- 情况 B：固定角度推进器 (如首侧推) ---
            // 直接使用配置好的默认角度，无视控制器的角度指令
            alpha = thrusters_[i].angle_default;
        }

        // 3. 矢量分解与力矩合成 (Body-fixed Frame)，应用效率系数
        // 处理负推力的情况，调整角度而不是使用负值推力
        // [Task6] Tunnel thruster crossflow efficiency loss
        // Real tunnel thrusters lose 50-80% effectiveness at high crossflow
        double crossflow_eff = 1.0;
        if (!thrusters_[i].is_azimuth && thrusters_[i].x > 0.0) {  // only bow tunnel thrusters
            double v_body = nu_(1);  // sway velocity at hull
            double v_crossflow = v_body + thrusters_[i].x * nu_(3);  // local crossflow at thruster
            crossflow_eff = 1.0 / (1.0 + 0.05 * v_crossflow * v_crossflow);  // restore k=0.05
        }
        double abs_T = std::abs(T);
        double adjusted_alpha = alpha;
        if (T < 0) {
            adjusted_alpha += M_PI;
        }
        double fx = abs_T * std::cos(adjusted_alpha) * thrusters_[i].efficiency * crossflow_eff;
        double fy = abs_T * std::sin(adjusted_alpha) * thrusters_[i].efficiency * crossflow_eff;

        Fx_total += fx;
        Fy_total += fy;
        Mz_total += (thrusters_[i].x * fy) - (thrusters_[i].y * fx);
    }

    tau_thruster_ = {Fx_total, Fy_total, 0.0, Mz_total}; // 推进器目前在模型中不贡献横摇力矩
}

void ShipDynamicsNode::thruster_cmd_callback(const std_msgs::msg::Float64MultiArray::SharedPtr msg) {
    RCLCPP_INFO(this->get_logger(), "[DEBUG] thruster_cmd received: size=%zu, expected=%zu", msg->data.size(), thrusters_.size() * 2);
    size_t num = thrusters_.size();

    // 支持两种格式:
    // 1. num * 2: [force, angle] - 标准 Azimuth
    // 2. num * 3: [force, angle, bucket] - Waterjet 模式
    size_t stride = (msg->data.size() == num * 3) ? 3 : 2;
    bool has_bucket = (stride == 3);

    if (msg->data.size() == stride * num) {
        std::lock_guard<std::mutex> lock(data_mutex_);

        for (size_t i = 0; i < num; ++i) {
            double force = msg->data[i * stride];
            double angle = msg->data[i * stride + 1];
            double bucket = has_bucket ? msg->data[i * stride + 2] : 0.0;

            if (!std::isfinite(force) || !std::isfinite(angle)) {
                RCLCPP_WARN(this->get_logger(), "收到非法指令 (NaN/Inf)，已忽略该帧。");
                return;
            }

            // DNV Rate Limiting: 存储为指令值，实际值通过 update_actuator_dynamics() 计算
            thrusters_[i].cmd_thrust = force;
            thrusters_[i].cmd_angle = angle;
            current_thruster_forces_[i] = force;
            current_thruster_angles_[i] = angle;
            current_thruster_buckets_[i] = bucket;
        }

        static rclcpp::Time last_log{0, 0, RCL_ROS_TIME};
        if ((this->now() - last_log).seconds() > 1.0) {
            std::string log_str = "[推力指令] ";
            for (size_t i = 0; i < num; ++i) {
                log_str += "T" + std::to_string(i) + "=" + std::to_string(msg->data[i*stride]) + "@" +
                           std::to_string(msg->data[i*stride+1]*180/M_PI) + "°";
                if (has_bucket) {
                    log_str += "B=" + std::to_string(msg->data[i*stride+2]);
                }
                log_str += " ";
            }
            RCLCPP_INFO(this->get_logger(), "%s", log_str.c_str());
            last_log = this->now();
        }

        last_thruster_cmd_time_ = this->now();
    } else {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000, "指令长度不匹配");
    }
}






/**
 * DNV Rate Limiting: 执行器状态机
 * 实现一阶限幅逼近，模拟真实执行器的物理动态响应
 * - 舵角:  slew rate limiting (角度变化率限幅)
 * - 推力:  engine ramp-up/down (推力变化率限幅)
 */
void ShipDynamicsNode::update_actuator_dynamics(double dt) {
    for (auto& thruster : thrusters_) {
        // 1. 舵角速率限幅 (Slew Rate Limiting)
        double angle_error = thruster.cmd_angle - thruster.actual_angle;
        double max_angle_step = thruster.angle_rate_limit * dt;
        if (std::abs(angle_error) > max_angle_step) {
            thruster.actual_angle += std::copysign(max_angle_step, angle_error);
        } else {
            thruster.actual_angle = thruster.cmd_angle;
        }

        // 2. 推力变化率限幅 (Engine Ramp-up/down)
        double thrust_error = thruster.cmd_thrust - thruster.actual_thrust;
        double max_thrust_step = thruster.thrust_rate_limit * dt;
        if (std::abs(thrust_error) > max_thrust_step) {
            thruster.actual_thrust += std::copysign(max_thrust_step, thrust_error);
        } else {
            thruster.actual_thrust = thruster.cmd_thrust;
        }

        // 将限幅后的值同步到 current_thruster_forces_/angles_ 供 collect_thruster_forces 使用
        current_thruster_angles_[&thruster - &thrusters_[0]] = thruster.actual_angle;
        current_thruster_forces_[&thruster - &thrusters_[0]] = thruster.actual_thrust;
    }
}

/**
 * [FDI] 故障检测：监控推进器指令与实际输出的残差
 * 残差超过阈值连续N个周期 → 判定为故障
 */
void ShipDynamicsNode::update_thruster_health(double dt) {
    (void)dt;
    // [修复] 临时禁用 FDI 故障检测，避免启动时误报
    // 所有推进器始终保持健康状态
    for (size_t i = 0; i < thrusters_.size(); ++i) {
        //       if (!thruster_healthy_[i]) continue;  // 已故障的推进器跳过

        // // 计算残差: |cmd - actual| / |cmd|
        // double cmd_abs = std::abs(thrusters_[i].cmd_thrust);
        // double residual = 0.0;
        // if (cmd_abs > 100.0) {  // 推力大于100N时才检测
        //     residual = std::abs(thrusters_[i].cmd_thrust - thrusters_[i].actual_thrust) / cmd_abs;
        // }
        // thruster_residual_[i] = residual;

        // // 残差超过阈值 → 故障计数器累加
        // if (residual > residual_threshold_[i]) {
        //     fault_counter_[i]++;
        //     // 连续10个周期残差超标 → 判定为故障
        //     if (fault_counter_[i] >= 10) {
        //         thruster_healthy_[i] = false;
        //         RCLCPP_WARN(this->get_logger(),
        //             "[FDI] 推进器 %s 故障! 残差=%.2f%% 累计%d周期",
        //             thrusters_[i].name.c_str(), residual * 100, fault_counter_[i]);
        //     }
        // } else {
        //     // 正常 → 计数器归零
        //     fault_counter_[i] = 0;
        // }
        thruster_healthy_[i] = true;
        thruster_residual_[i] = 0.0;
        fault_counter_[i] = 0;
    }
}

/**
 * [FDI] 发布健康状态到 /thruster/health_status
 * 数组顺序: [t1, t2, t3, tb1, tb2, rudder]
 * 1.0 = 健康, 0.0 = 故障
 */
void ShipDynamicsNode::publish_health_status() {
    std_msgs::msg::Float64MultiArray health_msg;
    health_msg.data.resize(thrusters_.size());
    for (size_t i = 0; i < thrusters_.size(); ++i) {
        health_msg.data[i] = thruster_healthy_[i] ? 1.0 : 0.0;
    }
    health_pub_->publish(health_msg);

    // 每秒打印一次详细状态
    static int print_counter = 0;
    if (print_counter++ % 5 == 0) {
        std::string status_str = "[FDI] Health: ";
        for (size_t i = 0; i < thrusters_.size(); ++i) {
            status_str += thrusters_[i].name + "=" +
                (thruster_healthy_[i] ? "OK" : "FAULT") + " ";
        }
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000, "%s",
            status_str.c_str());
    }
}

/**
 * [FDI] 故障注入回调
 * 用于测试时人为制造推进器故障
 * msg->data: 推进器索引 (0=t1, 1=t2, 2=t3, 3=tb1, 4=tb2, 5=rudder)
 * 负值表示恢复该推进器
 */
void ShipDynamicsNode::inject_fault_callback(const std_msgs::msg::Int32::SharedPtr msg) {
    int idx = msg->data;
    RCLCPP_INFO(this->get_logger(), "[FDI DEBUG] Received fault inject: idx=%d, thrusters_.size()=%zu", idx, thrusters_.size());
    if (idx >= 0 && idx < static_cast<int>(thrusters_.size())) {
        if (idx < static_cast<int>(thruster_healthy_.size())) {
            thruster_healthy_[idx] = false;
            thruster_healthy_.resize(thrusters_.size());

            RCLCPP_WARN(this->get_logger(),
                "[FDI] ★ 故障注入: %s (索引=%d) 已被拔出!",
                thrusters_[idx].name.c_str(), idx);

            // 如果之前故障，现在恢复
        } else {
            thruster_healthy_[idx] = true;
            RCLCPP_WARN(this->get_logger(),
                "[FDI] ★ 故障恢复: %s (索引=%d) 已修复",
                thrusters_[idx].name.c_str(), idx);
        }
    } else if (idx < 0 && -idx < static_cast<int>(thrusters_.size())) {
        int recover_idx = -idx;
        thruster_healthy_[recover_idx] = true;
        RCLCPP_WARN(this->get_logger(),
            "[FDI] ★ 故障恢复: %s (索引=%d) 已修复",
            thrusters_[recover_idx].name.c_str(), recover_idx);
    } else {
        RCLCPP_WARN(this->get_logger(), "[FDI] 无效的故障索引: %d", idx);
    }
}

void ShipDynamicsNode::initialize_csv_file() {
    try {
        std::filesystem::create_directories(log_dir_);
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        // 使用线程安全的时间处理
        std::tm tm_buf;
        #ifdef _WIN32
        localtime_s(&tm_buf, &time_t);
        #else
        localtime_r(&time_t, &tm_buf);
        #endif
        std::stringstream ss;
        ss << log_dir_ << "ship_sim_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".csv";
        csv_file_.open(ss.str());
        if (csv_file_.is_open()) {
            csv_file_ << "time,x,y,roll_deg,psi_deg,u,v,p,r\n";
        }
    } catch (const std::exception& e) {
        RCLCPP_ERROR(this->get_logger(), "CSV文件初始化失败: %s", e.what());
    }
}

void ShipDynamicsNode::record_to_csv() {
    // 非阻塞式文件写入，避免IO操作阻塞主循环
    if (csv_file_.is_open()) {
        try {
            double t = (this->now() - start_time_).seconds();
            csv_file_ << std::fixed << std::setprecision(5)
                      << t << "," << eta_[0] << "," << eta_[1] << "," 
                      << eta_[2] * 180.0 / M_PI << "," << psi_continuous_ * 180.0 / M_PI << ","
                      << nu_[0] << "," << nu_[1] << "," << nu_[2] << "," << nu_[3] << "\n";
            // 定期刷新缓冲区，避免数据丢失
            if (++csv_flush_count_ % 10 == 0) {
                csv_file_.flush();
            }
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "CSV写入错误: %s", e.what());
            // 发生错误时关闭文件，避免后续写入继续失败
            csv_file_.close();
        }
    }
}

} // namespace ship_dynamics
