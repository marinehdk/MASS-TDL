/**
 * thrust_allocation_node.cpp  —  加权伪逆推力分配器
 *
 * 修复记录（P0/P1）：
 *   [A3/P0] Vector Preserving Scaling 缺少 abs()：
 *           原代码仅检测正推力超限，负推力超限不触发缩放，
 *           修复为 max(|target_i| / max_i)。
 *   [B3/P1] 线程安全：添加 std::shared_mutex mtx_，
 *           on_tau_callback 用 shared_lock（读），
 *           on_health_update 用 unique_lock（写，持锁期间重建矩阵）。
 *   [B4/P1] DNV 功率公式物理含义修正：
 *           注释明确标注计算的是制动功率 P_B（非轴功率），
 *           变量改名为 p_brake_kW，量纲说明补全。
 *   [B5/P1] 禁止区穿越路径处理：
 *           在角速率步进前检测路径是否穿越禁止区，
 *           若穿越则取绕行边界（选最短绕行方向），避免振荡。
 *
 * 架构说明：
 *   加权伪逆公式：B_pinv_w = W⁻¹·Bᵀ·(B·W⁻¹·Bᵀ)⁻¹
 *   其中 W 对角矩阵存储权重，W_inv 存储 1/weight（直接构造逆矩阵）。
 *   全回转推进器分解为 x/y 两列（Perez & Fossen 2007），
 *   f = [fx₁,fy₁, fx₂,fy₂, ...] → T = ‖[fx,fy]‖, α = atan2(fy,fx)。
 */

#include "thrust_allocation/thrust_allocation_node.hpp"
#include <limits>
#include <shared_mutex>   // [B3/P1] shared_mutex

// ─────────────────────────────────────────────────────────────────────────────
// 构造函数
// ─────────────────────────────────────────────────────────────────────────────
AutonomousThrustAllocator::AutonomousThrustAllocator()
    : Node("thrust_allocation_node")
{
    this->declare_parameter("update_rate", 50.0);
    this->declare_parameter("thruster_names", std::vector<std::string>{});
    this->declare_parameter("env_feedforward_weight", 0.8);
    this->declare_parameter("control_mode", "auto");  // "auto" or "manual" 
    this->declare_parameter("enable_straight_cruise_main_equalization", true);
    this->declare_parameter("main_equalization_yaw_deadband_kNm", 30.0);
    this->declare_parameter("main_equalization_lateral_deadband_kN", 0.5);
    this->declare_parameter("side_thruster_derate_start_speed_mps", 2.0);
    this->declare_parameter("side_thruster_derate_decay_per_mps", 0.7);
    this->declare_parameter("side_thruster_lockout_speed_mps", 4.0);
    this->declare_parameter("side_thruster_emergency_unlock_max_speed_mps", 4.0);
    this->declare_parameter("side_thruster_lateral_deadband_kN", 0.5);
    this->declare_parameter("side_thruster_yaw_emergency_kNm", 200.0);
    this->declare_parameter("propulsion_constraints_stale_timeout_s", 3.0);

    // [FIX] Pre-declare all possible thruster parameters to avoid validation errors
    // This is necessary because rclcpp validates parameters against declared ones
    std::vector<std::string> all_thruster_names = {"t1", "t2", "t3", "tb1", "tb2", "r1", "r2"};
    for (const auto& n : all_thruster_names) {
        this->declare_parameter(n + ".x", 0.0);
        this->declare_parameter(n + ".y", 0.0);
        this->declare_parameter(n + ".is_azimuth", false);
        this->declare_parameter(n + ".angle_default", 0.0);
        this->declare_parameter(n + ".max_thrust_kN", 100.0);
        this->declare_parameter(n + ".max_power_kW", 1000.0);
        this->declare_parameter(n + ".weight", 1.0);
        this->declare_parameter(n + ".weight_delta", 100.0);
        this->declare_parameter(n + ".thrust_rate_limit", 50000.0);
        this->declare_parameter(n + ".angle_rate_limit", 0.5);
        this->declare_parameter(n + ".diameter", 2.0);
        this->declare_parameter(n + ".eta1", 800.0);
        this->declare_parameter(n + ".eta2", 1.0);
        this->declare_parameter(n + ".eta_m", 0.93);
        // Don't pre-declare forbidden_sectors - handled separately in load_thruster_configs
        this->declare_parameter(n + ".is_rudder", false);
        this->declare_parameter(n + ".max_angle", 0.61);
        this->declare_parameter(n + ".angle_min", 0.0);
        this->declare_parameter(n + ".angle_max", 0.0);
    }

    dt_ = 1.0 / this->get_parameter("update_rate").as_double();
    env_feedforward_weight_ = this->get_parameter("env_feedforward_weight").as_double();
    enable_straight_cruise_main_equalization_ =
        this->get_parameter("enable_straight_cruise_main_equalization").as_bool();
    main_equalization_yaw_deadband_kNm_ =
        this->get_parameter("main_equalization_yaw_deadband_kNm").as_double();
    main_equalization_lateral_deadband_kN_ =
        this->get_parameter("main_equalization_lateral_deadband_kN").as_double();
    side_thruster_derate_start_speed_mps_ =
        this->get_parameter("side_thruster_derate_start_speed_mps").as_double();
    side_thruster_derate_decay_per_mps_ =
        this->get_parameter("side_thruster_derate_decay_per_mps").as_double();
    side_thruster_lockout_speed_mps_ =
        this->get_parameter("side_thruster_lockout_speed_mps").as_double();
    side_thruster_emergency_unlock_max_speed_mps_ =
        this->get_parameter("side_thruster_emergency_unlock_max_speed_mps").as_double();
    side_thruster_lateral_deadband_kN_ =
        this->get_parameter("side_thruster_lateral_deadband_kN").as_double();
    side_thruster_yaw_emergency_kNm_ =
        this->get_parameter("side_thruster_yaw_emergency_kNm").as_double();
    propulsion_constraints_stale_timeout_s_ =
        this->get_parameter("propulsion_constraints_stale_timeout_s").as_double();

    // 控制模式加载
    auto mode_str = this->get_parameter("control_mode").as_string();
    if (mode_str == "manual") {
        control_mode_ = ControlMode::MANUAL;
        RCLCPP_WARN(this->get_logger(), "[模式] 推进器分配器切换至 MANUAL (开环手动) 模式");
    } else {
        control_mode_ = ControlMode::AUTO;
        RCLCPP_INFO(this->get_logger(), "[模式] 推进器分配器运行于 AUTO (自动) 模式");
    }

    // 【动态参数切换】注册参数变更回调，支持运行时动态切换MANUAL/AUTO模式
    auto param_callback = [this](const std::vector<rclcpp::Parameter> &params) -> rcl_interfaces::msg::SetParametersResult {
        rcl_interfaces::msg::SetParametersResult result;
        result.successful = true;
        for (const auto &p : params) {
            if (p.get_name() == "control_mode") {
                if (p.as_string() == "manual") {
                    control_mode_ = ControlMode::MANUAL;
                    RCLCPP_WARN(this->get_logger(), "[模式] 动态切换至 MANUAL 模式");
                } else {
                    control_mode_ = ControlMode::AUTO;
                    RCLCPP_INFO(this->get_logger(), "[模式] 动态切换至 AUTO 模式");
                }
            } else if (p.get_name() == "env_feedforward_weight") {
                const double weight = p.as_double();
                if (!std::isfinite(weight) || weight < 0.0 || weight > 1.0) {
                    result.successful = false;
                    result.reason = "env_feedforward_weight must be in [0.0, 1.0]";
                    return result;
                }
                {
                    std::unique_lock<std::shared_mutex> lk(mtx_);
                    env_feedforward_weight_ = weight;
                }
                RCLCPP_INFO(this->get_logger(),
                    "[EnvFeedforward] dynamic weight updated to %.2f", env_feedforward_weight_);
            }
        }
        return result;
    };
    param_callback_handle_ = this->add_on_set_parameters_callback(param_callback);

    load_thruster_configs();
    rebuild_allocation_matrices();

    tau_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/cmd_tau", 10,
        std::bind(&AutonomousThrustAllocator::on_tau_callback, this,
                  std::placeholders::_1));

    // [架构补全] 手动指令订阅 - MANUAL模式下直接执行
    manual_cmd_sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
        "/manual_actuator_cmd", 10,
        std::bind(&AutonomousThrustAllocator::on_manual_cmd_callback, this,
                  std::placeholders::_1));

    env_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/total_load", 10,
        std::bind(&AutonomousThrustAllocator::on_env_callback, this,
                  std::placeholders::_1));

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&AutonomousThrustAllocator::on_odom_callback, this,
                  std::placeholders::_1));

    health_sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
        "/thruster/health_status", 10,
        std::bind(&AutonomousThrustAllocator::on_health_update, this,
                  std::placeholders::_1));

    propulsion_constraints_sub_ = this->create_subscription<std_msgs::msg::Float64MultiArray>(
        "/propulsion/constraints", 10,
        std::bind(&AutonomousThrustAllocator::on_propulsion_constraints, this,
                  std::placeholders::_1));

    // 运行时 reset：orchestrator 经 gnc_bridge 发 /ship/dynamics_reset。
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&AutonomousThrustAllocator::reset_callback, this, std::placeholders::_1));

    cmd_pub_ = this->create_publisher<std_msgs::msg::Float64MultiArray>(
        "/thruster/commands", 10);

    RCLCPP_INFO(this->get_logger(),
        "自主驾驶级推力分配器已就绪。集成 DNV 规范及动力学约束。");
    RCLCPP_INFO(this->get_logger(),
        "[诊断] 推进器配置加载完成，共 %zu 个推进器", configs_.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// 推进器配置加载
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::load_thruster_configs()
{
    auto names = this->get_parameter("thruster_names").as_string_array();
    for (const auto& name : names) {
        ThrusterConfig cfg;
        cfg.name             = name;
        cfg.x                = this->get_parameter(name + ".x").as_double();
        cfg.y                = this->get_parameter(name + ".y").as_double();
        cfg.is_azimuth       = this->get_parameter(name + ".is_azimuth").as_bool();
        cfg.angle_fixed      = this->get_parameter(name + ".angle_default").as_double();
        cfg.weight           = this->get_parameter(name + ".weight").as_double();
        cfg.weight_delta     = this->get_parameter(name + ".weight_delta").as_double();
        cfg.max_thrust_N     = this->get_parameter(name + ".max_thrust_kN").as_double() * 1000.0;
        cfg.max_power_kW     = this->get_parameter(name + ".max_power_kW").as_double();
        cfg.diameter         = this->get_parameter(name + ".diameter").as_double();
        cfg.eta1             = this->get_parameter(name + ".eta1").as_double();
        cfg.eta2             = this->get_parameter(name + ".eta2").as_double();
        cfg.eta_m            = this->get_parameter(name + ".eta_m").as_double();
        cfg.thrust_rate_limit= this->get_parameter(name + ".thrust_rate_limit").as_double();
        cfg.angle_rate_limit = this->get_parameter(name + ".angle_rate_limit").as_double();
        // 舵机参数加载
        this->get_parameter_or(name + ".is_rudder", cfg.is_rudder, false);
        if (cfg.is_rudder || name == "rudder") {
            cfg.is_rudder = true;
            this->get_parameter_or(name + ".max_angle", cfg.max_angle, 0.61);
            this->get_parameter_or(name + ".angle_default", cfg.angle_fixed, 0.0);
            double max_thrust_kN = 50.0;
            this->get_parameter_or(name + ".max_thrust_kN", max_thrust_kN, 50.0);
            cfg.max_thrust_N = max_thrust_kN * 1000.0;
        }

        // 禁止区: 不加载，让 forbidden_sectors 保持为空
        // 如果后续需要启用禁止区功能，需要在YAML中配置并在代码中安全加载

        // 【核心修复 1】：强制纠正侧推的安装角
        // 如果是侧推(tb开头)，且角度为0，说明YAML漏配了，强制修正为 90度 (PI/2)
        if (cfg.name.find("tb") != std::string::npos && std::abs(cfg.angle_fixed) < 0.01) {
            RCLCPP_WARN(this->get_logger(), "侧推 %s 安装角为0，这会把它变成主桨！已强制修正为 90度 (1.57 rad)", cfg.name.c_str());
            cfg.angle_fixed = M_PI / 2.0;
        }

        configs_.push_back(cfg);
        ThrusterState initial_state;
        initial_state.last_angle_rad = cfg.angle_fixed;
        states_.push_back(initial_state);
    }
}
// ─────────────────────────────────────────────────────────────────────────────
// 重建推力分配矩阵（健康推进器子集 + 动态权重）
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::rebuild_allocation_matrices()
{
    int active_cols = 0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (states_[i].is_healthy) {
            active_cols += (configs_[i].is_azimuth ? 2 : 1);
        }
    }

    // if (active_cols == 0) {
    //     RCLCPP_ERROR(this->get_logger(), "所有推进器失效！无法构建分配矩阵。");
    //     return;
    // }

    B_.resize(3, active_cols);
    W_.resize(active_cols, active_cols);
    W_delta_.resize(active_cols, active_cols);
    W_.setZero();
    W_delta_.setZero();
    proj_constraints_.clear();

    const double INF_WEIGHT = 1e10;

    int col = 0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (!states_[i].is_healthy) continue;

        const auto& cfg = configs_[i];
        const auto& state = states_[i];

        double health_factor = 1.0;
        if (!state.is_healthy) {
            health_factor = INF_WEIGHT;
        } else {
            health_factor = 1.0 / std::max(state.health_score, 0.01);
        }

        double effective_weight = cfg.weight * health_factor;
        double effective_delta = cfg.weight_delta * health_factor;

        if (cfg.is_azimuth) {
            B_.col(col) << 1.0, 0.0, -cfg.y;
            W_(col, col) = effective_weight;
            W_delta_(col, col) = effective_delta;

            B_.col(col + 1) << 0.0, 1.0, cfg.x;
            W_(col + 1, col + 1) = effective_weight;
            W_delta_(col + 1, col + 1) = effective_delta;

            thrust_allocation::ProjectionConstraint cons;
            cons.type = 1;
            cons.idx_start = col;
            cons.min_bound = 0.0;
            cons.max_bound = cfg.max_thrust_N * 1e-3;  // [修复] 转换为 kN 与优化器单位对齐
            proj_constraints_.push_back(cons);

            col += 2;
        } else if (cfg.is_rudder || cfg.name == "rudder") {
            // Rudder decision variable is angle [rad]. update_dynamic_weights()
            // replaces this placeholder with force-per-radian terms in kN/rad.
            B_.col(col) << 0.0, 0.0, 0.0;
            W_(col, col) = effective_weight;
            W_delta_(col, col) = effective_delta;

            thrust_allocation::ProjectionConstraint cons;
            cons.type = 0;
            cons.idx_start = col;
            // Rudder variable is angle [rad], not force.
            cons.min_bound = -cfg.max_angle;
            cons.max_bound =  cfg.max_angle;
            proj_constraints_.push_back(cons);

            col++;
        } else {
            double c = std::cos(cfg.angle_fixed);
            double s = std::sin(cfg.angle_fixed);
            // 【差动恢复】主桨B_Mz恢复非零, 差动由 weight_delta 惩罚控制
            bool is_main_prop = (!cfg.is_azimuth && cfg.name.find("tb") == std::string::npos && !cfg.is_rudder);
            double mz_term = cfg.x * s - cfg.y * c;  // t1=-(-18.094*0-(-3)*1)=3, t3=-(18.094*0-3*1)=-3
            B_.col(col) << c, s, mz_term;
            W_(col, col) = effective_weight;
            W_delta_(col, col) = effective_delta;

            thrust_allocation::ProjectionConstraint cons;
            cons.type = 0;
            cons.idx_start = col;
            // 主桨默认禁止倒车（船长原则：高速倒车危及螺旋桨、舵效和机械安全）
            // update_dynamic_weights 在收到明确倒车指令(Fx<0)时会解禁 min_bound
            // 侧推(tb*)和舵不受此限制，保持双向
            cons.min_bound = is_main_prop ? 0.0 : -cfg.max_thrust_N * 1e-3;
            cons.max_bound =  cfg.max_thrust_N * 1e-3;
            proj_constraints_.push_back(cons);

            col++;
        }
    }

    // ── Q矩阵：物理归一化 ──────────────────────────────────────────────────────
    // 问题：标量Q=1e4*I 时，侧推(x=18m)的 Hessian 对角元
    //   H_tb = B_Fy²*Q_fy + B_Mz²*Q_mz = 1²*1e4 + 18²*1e4 = 3,250,000
    //   Mz项比Fy项大324倍，求解器认为用侧推"代价极高"，输出只有297N而非10kN
    //
    // 修复：Q_mz = Q_fx / (Lpp/2)²，以半船长作为特征长度归一化
    //   等价含义：1 N·m 力矩误差 = 1/(22.5) N 力误差
    //   修复后 H_tb = 1²*1e4 + 18²*19.75 = 10000+6400 = 16400（量级正常）
    {
        const double Q_force  = 1e4;
        const double Q_moment = 200.0;  // [舵效增强] 10x 原值(19.75),让QP更重视Mz跟踪
        Q_ = Eigen::MatrixXd::Zero(3, 3);
        Q_(0, 0) = Q_force;   // Fx 误差权重（严格保持航速）
        Q_(1, 1) = 1000.0;   // [Surgery] 蟹行权重1000
        Q_(2, 2) = Q_moment;  // Mz 误差权重（物理归一化）
    }

    H_ = B_.transpose() * Q_ * B_ + W_ + W_delta_;
}

// ─────────────────────────────────────────────────────────────────────────────
// 动态权重更新（根据航速智能分配 + 差动转向优化）
// 高速: 舵廉价(优先用), 侧推禁用
// 低速: 侧推可用, 舵效弱
// 极度低速+大转向需求: 左右主桨差动解禁
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::update_dynamic_weights(double u, const Eigen::Vector3d& tau_des) {
    // 【Phase 4 DNV合规重构】物理一致性与鲁棒性修复
    // 执行顺序：物理参数 -> 动力学关联 -> 鲁棒性处理
    
    double u_abs = std::abs(u);
    
    // ── Step 1: Sigmoid 舵效系数（替代硬限幅 0.5 m/s）─────────────────────
    // sigma(u) = 1 / (1 + exp(-k*(u - u_transition)))
    // u_transition = 1.0 m/s, k = 5.0
    const double u_trans = 1.0;
    const double k_sig = 5.0;
    double sigma = 1.0 / (1.0 + std::exp(-k_sig * (u_abs - u_trans)));
    
    // ── Step 2: 查找舵在B矩阵中的列索引 ─────────────────────────────────
    int rudder_col = -1;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if ((configs_[i].name == "rudder" || configs_[i].is_rudder) && states_[i].is_healthy) {
            rudder_col = 0;
            for (size_t j = 0; j < i; ++j) {
                if (states_[j].is_healthy) {
                    rudder_col += (configs_[j].is_azimuth ? 2 : 1);
                }
            }
            break;
        }
    }
    
    // ── Step 3: 更新所有舵的B矩阵列（双舵协同）────────────────────────────────
    {
        const double rho = 1025.0;
        const double A_rudder = 3.5;
        const double CL_alpha = 2.8;

        double lift_per_rad_kN = 0.5 * rho * A_rudder * CL_alpha * u_abs * u_abs * 1e-3;
        double rudder_force_per_rad = lift_per_rad_kN * sigma;

        // 遍历所有舵，逐个更新B矩阵列（修复原代码只更新第一个舵即break的bug）
        for (size_t ri = 0; ri < configs_.size(); ++ri) {
            if ((!configs_[ri].is_rudder && configs_[ri].name != "rudder") || !states_[ri].is_healthy) continue;

            int r_col = 0;
            for (size_t j = 0; j < ri; ++j) {
                if (states_[j].is_healthy) r_col += (configs_[j].is_azimuth ? 2 : 1);
            }
            if (r_col >= B_.cols()) continue;

            // Variable is delta [rad]. B maps delta to approximate generalized
            // force in [kN, kN, kNm], matching the dynamics rudder convention:
            // positive delta produces negative sway force at the stern.
            B_(0, r_col) = 0.0;
            B_(1, r_col) = -rudder_force_per_rad;
            B_(2, r_col) = -configs_[ri].x * rudder_force_per_rad;
            for (auto& cons : proj_constraints_) {
                if (cons.idx_start == r_col) {
                    cons.max_bound = +configs_[ri].max_angle;
                    cons.min_bound = -configs_[ri].max_angle;
                    break;
                }
            }
        }
    }
    
    // ── Step 4: 动态权重矩阵 W（引入 迟滞状态机 Hysteresis）────────────────
    // 设定迟滞区间的上下限
    const double MANEUVER_ENTER_SPEED = 0.3; // 进入差动模式的严格低速门槛
    const double MANEUVER_EXIT_SPEED  = 1.5;  // [FIXED] 1.5m/s(3kn)舵效已充足，退出差动

    // 状态机更新（迟滞逻辑）
    if (!is_maneuvering_mode_ && u_abs < MANEUVER_ENTER_SPEED) {
        is_maneuvering_mode_ = true;
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[状态机] 速度跌破 %.1f, 锁定进入 Maneuvering Mode (差动解禁)!", MANEUVER_ENTER_SPEED);
    } else if (is_maneuvering_mode_ && u_abs > MANEUVER_EXIT_SPEED) {
        is_maneuvering_mode_ = false;
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[状态机] 速度突破 %.1f, 退出 Maneuvering Mode (切回巡航)!", MANEUVER_EXIT_SPEED);
    }

    for (size_t i = 0; i < configs_.size(); ++i) {
        auto& cfg = configs_[i];
        if (!states_[i].is_healthy) continue;

        int col = 0;
        for (size_t j = 0; j < i; ++j) {
            if (states_[j].is_healthy) {
                col += (configs_[j].is_azimuth ? 2 : 1);
            }
        }

        double effective_weight = cfg.weight;
        double effective_delta = cfg.weight_delta;

        // ====================================================================
        // 【核心修复 2 & 3】：基于物理坐标解耦推力分配，彻底抛弃名称硬编码
        // ====================================================================
        // 自动身份识别：
        bool is_bow_thruster = (cfg.name.find("tb") != std::string::npos);
        bool is_main_prop = (!is_bow_thruster && !cfg.is_rudder);
        bool is_center_prop = is_main_prop && (std::abs(cfg.y) < 0.5); // Y轴靠近中心线的绝对是中桨
        bool is_wing_prop = is_main_prop && (std::abs(cfg.y) >= 0.5);  // Y轴偏离中心的绝对是左右两侧主桨

        // 1. 侧推调度 (Bow Thrusters)
        // 【TC-03 根因修复】：原逻辑把侧推激活绑定到 is_maneuvering_mode_，
        // 导致非低速时纯横向需求（Fy≠0）侧推被打到 weight=1000 完全关闭，
        // 合力只有 297N（溢出到主桨的微小 Fy 分量）。
        //
        // 正确策略：
        //   - 低速横移/DP/靠泊：侧推是主要横向执行器
        //   - 高速直航/巡航转弯：侧推锁定，艏向主要交给舵和航向制导
        //   - 应急大艏摇：允许突破速度锁定，但必须可观测
        if (is_bow_thruster) {
            double Fy_demand = std::abs(tau_des.y());  // kN单位
            double Mz_demand_kNm = std::abs(tau_des.z());  // kNm
            bool needs_lateral   = (Fy_demand > side_thruster_lateral_deadband_kN_);
            bool needs_rotation  = (Mz_demand_kNm > 10.0);  // > 10 kNm 力矩需求
            double current_speed = std::sqrt(u * u + current_v_ * current_v_);
            bool side_thruster_effective = (current_speed < side_thruster_lockout_speed_mps_);
            bool emergency_yaw = (Mz_demand_kNm >= side_thruster_yaw_emergency_kNm_);
            bool emergency_speed_ok = (current_speed < side_thruster_emergency_unlock_max_speed_mps_);
            bool emergency_unlock = emergency_yaw && emergency_speed_ok;
            bool policy_constraints_fresh = has_fresh_propulsion_constraints();
            bool policy_side_locked = policy_constraints_fresh && !policy_side_thruster_allowed_;
            double policy_side_fraction = policy_constraints_fresh
                ? std::clamp(policy_side_thruster_max_fraction_, 0.0, 1.0)
                : 0.0;
            bool policy_side_limited = policy_constraints_fresh
                && policy_side_thruster_allowed_
                && policy_side_fraction < 0.999;

            // 场景策略：
            //   - 直航/高速巡航: 侧推锁定为0，避免隧道侧推在来流中低效工作并扰动航向。
            //   - 低速/DP/靠泊: 侧推作为横移和艏摇执行器开放。
            //   - 应急艏摇: 只有在速度仍处于可控范围内时，允许短时突破常规锁定。
            if (policy_side_locked) {
                effective_weight = 10000.0;
                effective_delta  = 10000.0;
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[SIDE_DEBUG] LOCKED by policy: name=%s speed=%.2f Fy=%.1f Mz=%.1f",
                    cfg.name.c_str(), current_speed, tau_des.y(), tau_des.z());
            } else if ((needs_lateral || needs_rotation) && (side_thruster_effective || emergency_unlock)) {
                effective_weight = 0.5;
                effective_delta  = 0.5;
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[SIDE_DEBUG] UNLOCKED: name=%s needs_lat=%d needs_rot=%d effective=%d emergency=%d speed=%.2f lockout=%.2f",
                    cfg.name.c_str(), needs_lateral, needs_rotation, side_thruster_effective,
                    emergency_unlock, current_speed, side_thruster_lockout_speed_mps_);
            } else {
                effective_weight = 1000.0;
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[SIDE_DEBUG] HIGH_SPEED_BLOCKED: name=%s needs_lat=%d needs_rot=%d effective=%d emergency=%d speed=%.2f lockout=%.2f",
                    cfg.name.c_str(), needs_lateral, needs_rotation, side_thruster_effective,
                    emergency_unlock, current_speed, side_thruster_lockout_speed_mps_);
            }

            double base_max = cfg.max_thrust_N * 1e-3;
            double available = base_max;
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[SIDE_DEBUG] AVAIL_CALC: name=%s base_max=%.1f speed=%.2f derate_start=%.2f decay=%.2f lockout=%.2f effective=%d emergency=%d policy_locked=%d",
                cfg.name.c_str(), base_max, current_speed,
                side_thruster_derate_start_speed_mps_, side_thruster_derate_decay_per_mps_,
                side_thruster_lockout_speed_mps_, side_thruster_effective, emergency_unlock,
                policy_side_locked);
            double policy_cap = policy_constraints_fresh
                ? base_max * policy_side_fraction
                : 0.0;
            const double derate_start = std::max(0.0,
                std::min(side_thruster_derate_start_speed_mps_, side_thruster_lockout_speed_mps_));
            const double derate_decay = std::max(0.0, side_thruster_derate_decay_per_mps_);
            if (policy_side_locked) {
                available = 0.0;
            } else if (!side_thruster_effective && !emergency_unlock) {
                available = 0.0;
            } else if (current_speed > derate_start) {
                available = base_max * std::exp(-derate_decay * (current_speed - derate_start));
            }
            if (!policy_side_locked && policy_constraints_fresh) {
                available = std::min(available, policy_cap);
            }
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[SIDE_DEBUG] AVAIL_FINAL: name=%s available=%.3f base_max=%.1f weight=%.1f",
                cfg.name.c_str(), available, base_max, effective_weight);
            for (auto& cons : proj_constraints_) {
                if (cons.idx_start == col) {
                    cons.max_bound =  available;
                    cons.min_bound = -available;
                    break;
                }
            }

            if (policy_side_locked && (needs_lateral || needs_rotation)) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[PropulsionPolicyEnforce] 侧推被 /propulsion/constraints 硬锁定: V=%.2fm/s Fy=%.1fkN Mz=%.1fkNm",
                    current_speed, tau_des.y(), tau_des.z());
            } else if (available < 1e-6 && (needs_lateral || needs_rotation)) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[ThrustAllocPolicy] 侧推锁定: mode=%s V=%.2fm/s lockout=%.2fm/s emergency_max=%.2fm/s Fy=%.1fkN Mz=%.1fkNm",
                    (emergency_yaw && !emergency_speed_ok) ? "emergency_speed_blocked" : "cruise_speed_lockout",
                    current_speed, side_thruster_lockout_speed_mps_,
                    side_thruster_emergency_unlock_max_speed_mps_, tau_des.y(), tau_des.z());
            } else if (emergency_unlock && available > 1e-6) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[ThrustAllocPolicy] 侧推应急降额解锁: V=%.2fm/s available=%.1f/%.1fkN Mz=%.1fkNm",
                    current_speed, available, base_max, tau_des.z());
            } else if (policy_side_limited && available > 1e-6 && (needs_lateral || needs_rotation)) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                    "[PropulsionPolicyEnforce] side thruster fraction cap: rudder_preferred=%d fraction=%.2f available=%.1f/%.1fkN V=%.2fm/s",
                    policy_rudder_preferred_ ? 1 : 0,
                    policy_side_fraction, available, base_max, current_speed);
            } else if (available > 1e-6 && available < base_max * 0.99 && (needs_lateral || needs_rotation)) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                    "[ThrustAllocPolicy] 侧推速度降额: V=%.2fm/s available=%.1f/%.1fkN",
                    current_speed, available, base_max);
            }

        }

        // 2. 左右主桨调度 (Wing Propellers)
        // [Route S] 三层豁免：日常禁倒车 → ESP差动 → 极限全速倒车
        // [Handover Shock Fix] 航行态(u>1.0) 主桨永不倒车
        if (is_wing_prop) {
            double Fx_signed = tau_des.x();
            bool need_reverse = (Fx_signed < -0.5);
            double v_abs = std::abs(current_v_);
            bool is_extreme_emergency = (v_abs > 1.5 && u > 1.0);
            bool safe_reverse_braking = need_reverse
                && std::abs(tau_des.z()) < 80.0
                && v_abs < 0.5;
            bool policy_reverse_locked = has_fresh_propulsion_constraints() && !policy_reverse_allowed_;

            // 航行态硬锁: u>1.0时主桨永不倒车(防Munk)
            if (policy_reverse_locked) {
                effective_weight = cfg.weight;
                effective_delta = cfg.weight_delta;
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = 0.0; break; }
                }
                if (false && !is_maneuvering_mode_) {
                    B_(2, col) = 0.0;
                }
                if (need_reverse || is_extreme_emergency) {
                    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                        "[PropulsionPolicyEnforce] 主桨倒车被 /propulsion/constraints 硬锁定: Fx=%.1fkN Mz=%.1fkNm u=%.2fm/s v=%.2fm/s",
                        Fx_signed, tau_des.z(), u, v_abs);
                }
            } else if (is_extreme_emergency) {
                // 极限救车：解封倒车+差动+B矩阵全开
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = -cfg.max_thrust_N * 1e-3; break; }
                }
                effective_weight = cfg.weight;
                effective_delta = 1.0;
                B_(2, col) = (cfg.x * std::sin(cfg.angle_fixed) - cfg.y * std::cos(cfg.angle_fixed));
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                    "[DNV exemption] emergency reverse and differential thrust enabled, v=%.1f", v_abs);
            } else if (u > 1.0 && !safe_reverse_braking) {
                effective_weight = cfg.weight;
                effective_delta = cfg.weight_delta;
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = 0.0; break; }
                }
                if (false && !is_maneuvering_mode_) {
                    B_(2, col) = 0.0;
                }
            } else if (need_reverse) {
                effective_weight = cfg.weight;
                effective_delta = 1.0;
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = -cfg.max_thrust_N * 1e-3; break; }
                }
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                    "[ThrustAlloc] controlled astern braking enabled: Fx=%.1fkN Mz=%.1fkNm v=%.2fm/s",
                    Fx_signed, tau_des.z(), v_abs);
            } else if (v_abs > 1.0) {
                // [ESP] 差动解封，但保留min_bound=0
                effective_weight = cfg.weight;
                effective_delta = 1.0;
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = 0.0; break; }
                }
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[ESP] 侧滑%.1fm/s 差动全开", v_abs);
            } else {
                effective_weight = cfg.weight;
                effective_delta = cfg.weight_delta * 5.0;  // 差动惩罚5倍, QP不得已时才用
                for (auto& cons : proj_constraints_) {
                    if (cons.idx_start == col) { cons.min_bound = 0.0; break; }
                }
                // B_(2, col) 由 rebuild 计算保留，weight_delta*5 抑制差动
            }
        }

        // 3. 中主桨调度 (Center Propeller)
        if (is_center_prop) {
            effective_weight = cfg.weight;
            effective_delta = cfg.weight_delta;
            // 中主桨永远不准倒车，为舵提供洗流
            for (auto& cons : proj_constraints_) {
                if (cons.idx_start == col) { cons.min_bound = 0.0; break; }
            }
        }

        // ====================================================================
        // 【基于迟滞状态标志】：尾舵的互斥锁定
        // ====================================================================
        if (cfg.name == "rudder" || cfg.is_rudder) {
            // 【物理驱动】舵效只取决于来流速度，不取决于"模式"标签
            // 用 Sigmoid 平滑过渡: 低速封杀 → 高速接管
            // sigma(u) = 1/(1+exp(-k*(u - u_trans))): u=1.0→W≈10000, u=2.0→W≈0.1
            const double u_rudder_on  = 1.0;   // 舵开始有效的流速 (m/s)
            const double u_rudder_full = 2.0;   // 舵完全接管的流速 (m/s)
            const double k_rudder = 8.0;         // Sigmoid 陡峭度

            if (u_abs < u_rudder_on) {
                // 极低速: 舵完全无效，封杀
                effective_weight = 10000.0;
                effective_delta  = 10000.0;
            } else if (u_abs > u_rudder_full) {
                // 有充足流速: 舵是绝对主力，权重极低
                effective_weight = 0.1;
                effective_delta  = 0.1;
            } else {
                // 过渡区: Sigmoid 平滑从封杀→接管
                double sigmoid = 1.0 / (1.0 + std::exp(-k_rudder * (u_abs - 1.5)));
                effective_weight = 10000.0 + (0.1 - 10000.0) * sigmoid;
                effective_delta  = effective_weight;
            }
        }

        // 【修复】删除 t3 名称硬编码。
        // 根因：YAML中 t3 是右主桨(y=3.0, angle=0°)，t2 才是中桨(y=0)。
        // 名称硬编码错误地把右主桨当中桨，覆盖了 is_wing_prop 的差动权重(0.05→2.0)，
        // 导致右主桨差动失效，且侧推无法承担横向力。
        // 所有推进器身份识别统一由 YAML 坐标（y 值）判断，不再依赖名称。

        if (!cfg.is_azimuth) {
            W_(col, col) = effective_weight;
            W_delta_(col, col) = effective_delta;
        } else {
            W_(col, col) = effective_weight;
            W_(col + 1, col + 1) = effective_weight;
            W_delta_(col, col) = effective_delta;
            W_delta_(col + 1, col + 1) = effective_delta;
        }
    }

    H_ = B_.transpose() * Q_ * B_ + W_ + W_delta_;
}

// ─────────────────────────────────────────────────────────────────────────────
// [B5/P1] 禁止区安全角映射（含穿越路径检测）
// ─────────────────────────────────────────────────────────────────────────────
double AutonomousThrustAllocator::map_to_safe_angle(
    double current_rad,
    double target_rad,
    const ThrusterConfig& cfg)
{
    if (cfg.forbidden_sectors.empty()) return target_rad;

    auto to_deg_360 = [](double rad) -> double {
        double d = rad * 180.0 / M_PI;
        while (d <    0.0) d += 360.0;
        while (d >= 360.0) d -= 360.0;
        return d;
    };

    auto path_len = [](double from, double to) -> double {
        double d = std::abs(to - from);
        return std::min(d, 360.0 - d);
    };

    auto in_forbidden = [&](double deg) -> int {
        for (int k = 0; k < (int)cfg.forbidden_sectors.size(); ++k) {
            double s = cfg.forbidden_sectors[k].first;
            double e = cfg.forbidden_sectors[k].second;
            if (s <= e) {
                if (deg >= s && deg <= e) return k;
            } else {
                if (deg >= s || deg <= e) return k;
            }
        }
        return -1;
    };

    auto path_crosses_sector = [&](double from, double to, double s, double e) -> bool {
        double diff = to - from;
        while (diff >  180.0) diff -= 360.0;
        while (diff < -180.0) diff += 360.0;
        double end_path = from + diff;

        if (s <= e) {
            if (diff >= 0) {
                return (s >= from && s <= end_path) || (e >= from && e <= end_path) || (s <= from && e >= end_path);
            } else {
                return (s >= end_path && s <= from) || (e >= end_path && e <= from) || (s <= end_path && e >= from);
            }
        } else {
            return (s >= from || s <= end_path) || (e >= from || e <= end_path);
        }
    };

    double cur_deg = to_deg_360(current_rad);
    double target_deg = to_deg_360(target_rad);

    int forbidden_idx = in_forbidden(target_deg);
    if (forbidden_idx >= 0) {
        const auto& sec = cfg.forbidden_sectors[forbidden_idx];
        double dist_start = path_len(target_deg, sec.first);
        double dist_end = path_len(target_deg, sec.second);
        target_deg = (dist_start < dist_end) ? sec.first : sec.second;
    }

    for (const auto& sec : cfg.forbidden_sectors) {
        if (path_crosses_sector(cur_deg, target_deg, sec.first, sec.second)) {
            double to_start = sec.first;
            double to_end = sec.second;

            double len_start = path_len(cur_deg, to_start);
            double len_end = path_len(cur_deg, to_end);

            double len_target_to_start = path_len(target_deg, to_start);
            double len_target_to_end = path_len(target_deg, to_end);

            double total_start = len_start + len_target_to_start;
            double total_end = len_end + len_target_to_end;

            target_deg = (total_start <= total_end) ? to_start : to_end;
            break;
        }
    }

    return target_deg * M_PI / 180.0;
}

// ─────────────────────────────────────────────────────────────────────────────
// 运行时 reset：清执行器热启动状态 + maneuvering mode（等价于冷启初始化）
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::reset_allocator()
{
    // 由 reset_callback 调用，调用方持 unique_lock(mtx_)。
    // 清跨 run 残留的执行器热启动量 + 模式 latch。
    for (auto& s : states_) {
        s.last_thrust_N = 0.0;
        // last_angle_rad 保留构造初始值（固定推进器安装角）；
        // 全回转推进器清零即回正。保守清零避免旧角度污染角速率限制基准。
        s.last_angle_rad = 0.0;
    }
    tau_des_prev_ = Eigen::Vector3d::Zero();
    tau_env_ = Eigen::Vector3d::Zero();
    last_rudder_cmd_ = 0.0;
    is_maneuvering_mode_ = false;
    RCLCPP_INFO(this->get_logger(), "reset_allocator: thruster hot-start state cleared");
}

void AutonomousThrustAllocator::reset_callback(
    const ship_interfaces::msg::ShipReset::SharedPtr /*msg*/)
{
    std::unique_lock<std::shared_mutex> lk(mtx_);
    reset_allocator();
}

// ─────────────────────────────────────────────────────────────────────────────
// 主回调：接收力矩需求，计算推进器指令
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_tau_callback(
    const geometry_msgs::msg::WrenchStamped::SharedPtr msg)
{
    static int callback_count = 0;
    static rclcpp::Time last_log_time{0, 0, RCL_ROS_TIME};
    callback_count++;

    // [架构补全] MANUAL模式下忽略自动力矩指令
    if (control_mode_ == ControlMode::MANUAL) {
        if (callback_count % 500 == 1) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
                "[模式] MANUAL模式：忽略 /cmd_tau，自动力矩指令被屏蔽");
        }
        return;  // MANUAL模式不处理自动指令
    }

    Eigen::Vector3d tau_input(msg->wrench.force.x * 1e-3,
                                   msg->wrench.force.y * 1e-3,
                                   msg->wrench.torque.z * 1e-3);  // 统一到kN/kNm单位系


        RCLCPP_INFO(this->get_logger(), "[诊断] on_tau_callback 被调用 %d 次, tau_des=(%.1f, %.1f, %.1f)",
            callback_count, msg->wrench.force.x, msg->wrench.force.y, msg->wrench.torque.z);

    if ((this->now() - last_log_time).seconds() > 5.0) {
        RCLCPP_INFO(this->get_logger(), "[诊断] on_tau_callback 被调用 %d 次, tau_des=(%.1f, %.1f, %.1f)",
            callback_count, msg->wrench.force.x, msg->wrench.force.y, msg->wrench.torque.z);
        last_log_time = this->now();
    }

    // [M-02 修复] 改为 unique_lock：本回调会写入 states_[i].last_thrust_N / last_angle_rad，
    //              shared_lock 下写入是未定义行为，在多线程 executor 下与 on_health_update 构成数据竞争。
    std::unique_lock<std::shared_mutex> lk(mtx_);

    // 【Standalone测试模式】：如果没有收到health状态但有配置，加载默认healthy状态
    if (H_.size() == 0 && configs_.size() > 0) {
        RCLCPP_WARN(this->get_logger(), "[Standalone模式] 未收到health状态，使用默认healthy状态初始化");
        for (size_t i = 0; i < states_.size(); ++i) {
            states_[i].is_healthy = true;
        }
        rebuild_allocation_matrices();
    }

    if (H_.size() == 0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "分配矩阵未初始化，跳过本帧。");
        return;
    }

    Eigen::Vector3d tau_des = tau_input;

    // [NaN Firewall] 拦截输入期望力的 NaN
    if (std::isnan(tau_des.x()) || std::isnan(tau_des.y()) || std::isnan(tau_des.z())) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 500,
            "🔥 [NaN Firewall] 接收到的期望力矩包含 NaN！强制归零保护。");
        tau_des.setZero();
    }
    
    // [NaN Firewall] 拦截环境前馈力的 NaN
    if (std::isnan(tau_env_.x()) || std::isnan(tau_env_.y()) || std::isnan(tau_env_.z())) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 500,
            "🔥 [NaN Firewall] 环境前馈力包含 NaN！忽略前馈。");
        tau_env_.setZero();
    }
    
    // [Munk死锁保护] 大倒车+大偏航 → 完全切断倒车指令
    // 当分配器同时面对 Fx<-50kN 和 Mz>100kNm 时，系统已进入Munk死锁循环。
    // 倒车指令不解除死锁，反而会触发Level降级杀死Fy→侧推被关→Mz不足。
    // 完全切断倒车，释放推进器资源去满足Mz，打破死锁。
    if (tau_des.x() < -50.0 && std::abs(tau_des.z()) > 100.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[Munk死锁保护] 异常倒车+大Mz, 抑制倒车: %.0f→0", 
            tau_des.x() * 1e3);
        tau_des.x() = 0.0;
    }
    
    // /env/total_load is already a base_link load. tau_env_ is stored in
    // allocator units (kN/kNm), so compensate it directly without yaw rotation.
    tau_des -= tau_env_ * env_feedforward_weight_;

    // [Phase 3] 动态权重更新：高速时优先用舵，低速时用侧推，极低速大转向时主桨差动
    update_dynamic_weights(current_speed_, tau_des);

    // [FDI Fix] Debug: 打印各推进器权重 (使用动态索引避免越界)
    if (W_.cols() >= 6) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "[DynWeight] V=%.1fkn W_tb1=%.1f W_tb2=%.1f W_rudder=%.1f W_t1=%.1f",
            current_speed_ * 1.94384,
            W_(3,3), W_(4,4), W_(5,5), W_(0,0));
    } else if (W_.cols() >= 5) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "[DynWeight] V=%.1fkn W_tb1=%.1f W_tb2=%.1f W_rudder=%.1f",
            current_speed_ * 1.94384,
            W_(3,3), W_(4,4), W_(0,0));
    }

    // ── Step 1：构建 QP 梯度项并求解 ─────────────────────────────────────────
    // 首先构造 PGD 热启动初值 (利用上一帧的指令作为基础)
    Eigen::VectorXd f_init(B_.cols());
    // [FIX] Reset f_init when tau_des changes sharply
    double tau_change = (tau_des - tau_des_prev_).norm();
    if (tau_change > 50.0) f_init.setZero();
    tau_des_prev_ = tau_des;
    int col_idx = 0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (!states_[i].is_healthy) continue;
        if (configs_[i].is_azimuth) {
            f_init(col_idx)     = states_[i].last_thrust_N * 1e-3 * std::cos(states_[i].last_angle_rad);  // [修复] N→kN
            f_init(col_idx + 1) = states_[i].last_thrust_N * 1e-3 * std::sin(states_[i].last_angle_rad);  // [修复] N→kN
            col_idx += 2;
        } else {
            if (configs_[i].is_rudder) {
                // Rudder decision variable is angle [rad], not force.
                // Using last_thrust_N (=0 for rudders) pulls the optimizer back
                // toward zero every frame and makes real steering look like 0.x deg.
                f_init(col_idx) = states_[i].last_angle_rad;
            } else {
                f_init(col_idx) = states_[i].last_thrust_N * 1e-3;  // [修复] N→kN
            }
            col_idx += 1;
        }
    }

    // [SOTA] 将推进器磨损惩罚放入线性项 c: 
    // J = ... + (f - f_init)^T W_delta (f - f_init)
    // 展开并丢弃常数点得线性项 c = -B^T * Q * tau_des - W_delta * f_init
    Eigen::VectorXd c = -B_.transpose() * Q_ * tau_des - W_delta_ * f_init;

    // 执行 PGD
    Eigen::VectorXd f_qp = thrust_allocation::PGDSolver::solve(H_, c, proj_constraints_, f_init);

    // [NaN Firewall] 拦截非法的 NaN 解，防止其扩散到控制系统和仿真器
    bool is_nan_solution = false;
    for (int i = 0; i < f_qp.size(); ++i) {
        if (std::isnan(f_qp(i))) {
            is_nan_solution = true;
            break;
        }
    }

    if (is_nan_solution) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 500,
            "🔥 [NaN Firewall] PGD 求解器输出 NaN！拦截非法指令，切断推力输出！");
        f_qp.setZero();
    }

    // ── Fallback: 如果 PGD 解算出的合力误差过大，使用加权伪逆作为备用方案 ───
    double achieved_fx_check = 0.0, achieved_fy_check = 0.0, achieved_mz_check = 0.0;
    int col_check = 0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (!states_[i].is_healthy) continue;
        double fx_i, fy_i;
        if (configs_[i].is_azimuth) {
            fx_i = f_qp(col_check);
            fy_i = f_qp(col_check + 1);
            double angle = std::atan2(fy_i, fx_i);
            double thrust_mag = std::sqrt(fx_i * fx_i + fy_i * fy_i);
            achieved_fx_check += thrust_mag * std::cos(angle);
            achieved_fy_check += thrust_mag * std::sin(angle);
            achieved_mz_check += thrust_mag * (configs_[i].x * std::sin(angle) - configs_[i].y * std::cos(angle));
            col_check += 2;
        } else {
            double f_i = f_qp(col_check);
            double angle = configs_[i].angle_fixed;
            achieved_fx_check += f_i * std::cos(angle);
            achieved_fy_check += f_i * std::sin(angle);
            achieved_mz_check += f_i * (configs_[i].x * std::sin(angle) - configs_[i].y * std::cos(angle));
            col_check += 1;
        }
    }
    Eigen::Vector3d tau_achieved_check(achieved_fx_check, achieved_fy_check, achieved_mz_check);
    double allocation_error = (tau_achieved_check - tau_des).norm() / (tau_des.norm() + 1e-6);

    // ── 优先级降级求解：饱和时保 Fx+Mz，牺牲 Fy ─────────────────────────────
    // 物理背景：固定轴推进船 Fy 和 Mz 方向耦合，DP大力矩时两者不可兼得
    // 策略：误差>30% → Level2降低Fy权重 → 误差仍>30% → Level3放弃Fy
    const double SAT_THRESH = 0.30;
    if (allocation_error > SAT_THRESH && tau_des.norm() > 1.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "⚠️ [降级] 合力误差=%.0f%%，启动优先级降级求解", allocation_error * 100.0);

        // Level-2：Mz+Fx 优先，Fy 权重降至 1%
        Eigen::MatrixXd Q_l2 = Q_;
        Q_l2(1, 1) *= 0.01;
        Eigen::MatrixXd H_l2 = B_.transpose() * Q_l2 * B_ + W_ + W_delta_;
        Eigen::VectorXd c_l2 = -B_.transpose() * Q_l2 * tau_des - W_delta_ * f_init;
        Eigen::VectorXd f_l2 = thrust_allocation::PGDSolver::solve(H_l2, c_l2, proj_constraints_, f_init);

        // 重新计算合力误差
        double fx2=0,fy2=0,mz2=0; int cc2=0;
        for (size_t i=0;i<configs_.size();++i) {
            if (!states_[i].is_healthy) continue;
            double fi=f_l2(cc2), ai=configs_[i].angle_fixed;
            if (configs_[i].is_azimuth) { ai=std::atan2(f_l2(cc2+1),f_l2(cc2)); fi=std::hypot(f_l2(cc2),f_l2(cc2+1)); cc2+=2; } else cc2++;
            fx2+=fi*std::cos(ai); fy2+=fi*std::sin(ai);
            mz2+=fi*(configs_[i].x*std::sin(ai)-configs_[i].y*std::cos(ai));
        }
        double err2=(Eigen::Vector3d(fx2,fy2,mz2)-tau_des).norm()/(tau_des.norm()+1e-6);

        if (err2 < allocation_error) {
            f_qp = f_l2; tau_achieved_check = {fx2,fy2,mz2}; allocation_error = err2;
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "✅ [Level-2 Mz优先] 降级后误差=%.0f%%（Fy被牺牲）", err2*100.0);
        }

        // Level-3：完全放弃 Fy
        if (allocation_error > SAT_THRESH) {
            Eigen::MatrixXd Q_l3 = Q_; Q_l3(1,1) = 0.0;
            Eigen::MatrixXd H_l3 = B_.transpose() * Q_l3 * B_ + W_ + W_delta_;
            Eigen::VectorXd c_l3 = -B_.transpose() * Q_l3 * tau_des - W_delta_ * f_init;
            Eigen::VectorXd f_l3 = thrust_allocation::PGDSolver::solve(H_l3, c_l3, proj_constraints_, f_init);
            double fx3=0,fy3=0,mz3=0; int cc3=0;
            for (size_t i=0;i<configs_.size();++i) {
                if (!states_[i].is_healthy) continue;
                double fi=f_l3(cc3), ai=configs_[i].angle_fixed;
                if (configs_[i].is_azimuth) { ai=std::atan2(f_l3(cc3+1),f_l3(cc3)); fi=std::hypot(f_l3(cc3),f_l3(cc3+1)); cc3+=2; } else cc3++;
                fx3+=fi*std::cos(ai); fy3+=fi*std::sin(ai);
                mz3+=fi*(configs_[i].x*std::sin(ai)-configs_[i].y*std::cos(ai));
            }
            double err3=(Eigen::Vector3d(fx3,fy3,mz3)-tau_des).norm()/(tau_des.norm()+1e-6);
            if (err3 < allocation_error) {
                f_qp = f_l3; allocation_error = err3;
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                    "🚨 [Level-3 Fx+Mz] Fy完全放弃，误差=%.0f%%", err3*100.0);
            }
        }
    }

    // ── Step 2：推进器指令解算 (禁止区与速率约束补充) ──────────────────────
    std::vector<double> target_thrusts(configs_.size(), 0.0);
    std::vector<double> target_angles(configs_.size(), 0.0);
    col_idx = 0;

    for (size_t i = 0; i < configs_.size(); ++i) {
        const auto& cfg = configs_[i];
        auto& state     = states_[i];
        if (!state.is_healthy) continue;

        double desired_f, desired_a;
        if (cfg.is_azimuth) {
            double fx = f_qp(col_idx) * 1000.0;  // [修复] kN→N
            double fy = f_qp(col_idx + 1) * 1000.0;  // [修复] kN→N
            desired_f = std::sqrt(fx*fx + fy*fy);
            desired_a = std::atan2(fy, fx);
            if (fx < 0) {
                desired_f = -desired_f;
                desired_a += M_PI;
            }
            col_idx  += 2;
        } else {
            if (cfg.is_rudder) {
                desired_a = f_qp(col_idx);  // 舵角是rad，保持不变
                desired_f = 0.0;        // 舵不产生纵向力
            } else {
                desired_f = f_qp(col_idx) * 1000.0;  // [修复] kN→N
                desired_a = cfg.angle_fixed;
            }
            col_idx  += 1;
        }

        // 禁止区映射
        double safe_a = map_to_safe_angle(state.last_angle_rad, desired_a, cfg);

        // 角速率限制：全回转和舵需要速率限制，固定推进器直接用安装角
        if (cfg.is_azimuth || cfg.is_rudder) {
            double a_diff = safe_a - state.last_angle_rad;
            while (a_diff >  M_PI) a_diff -= 2.0 * M_PI;
            while (a_diff < -M_PI) a_diff += 2.0 * M_PI;
            target_angles[i] = state.last_angle_rad
                + std::clamp(a_diff,
                             -cfg.angle_rate_limit * dt_,
                              cfg.angle_rate_limit * dt_);
        } else {
            target_angles[i] = cfg.angle_fixed;
        }

        // [修复] 移除推力变化率限制，由动力学节点统一执行物理限幅
        // 避免双重限幅导致 FDI 故障误报
        target_thrusts[i] = desired_f;
    }

    // Treat twin rudders as one linked steering actuator in cruise allocation.
    // The two physical rudders have nearly identical sway/yaw effectiveness columns;
    // solving them as fully independent variables can create ill-conditioned or
    // visually inconsistent steering. Average their rate-limited commands and send
    // the same angle to both, preserving smoothness while matching real twin-rudder use.
    double rudder_angle_sum = 0.0;
    int rudder_count = 0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (states_[i].is_healthy && configs_[i].is_rudder) {
            rudder_angle_sum += target_angles[i];
            rudder_count += 1;
        }
    }
    if (rudder_count >= 2) {
        const double paired_rudder_angle = rudder_angle_sum / static_cast<double>(rudder_count);
        for (size_t i = 0; i < configs_.size(); ++i) {
            if (states_[i].is_healthy && configs_[i].is_rudder) {
                target_angles[i] = std::clamp(
                    paired_rudder_angle,
                    -configs_[i].max_angle,
                    configs_[i].max_angle);
            }
        }
    }

    const bool low_speed_lateral_hold =
        current_speed_ < 1.0 &&
        std::abs(tau_des.y()) > 1.0 &&
        std::abs(tau_des.z()) < 5.0;
    if (low_speed_lateral_hold) {
        int side_count = 0;
        double min_side_x = std::numeric_limits<double>::infinity();
        double max_side_x = -std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < configs_.size(); ++k) {
            const auto& ck = configs_[k];
            if (states_[k].is_healthy && ck.name.find("tb") != std::string::npos) {
                side_count++;
                min_side_x = std::min(min_side_x, ck.x);
                max_side_x = std::max(max_side_x, ck.x);
            }
        }
        if (side_count > 0) {
            const bool side_thrusters_straddle_cg = (min_side_x < -0.5 && max_side_x > 0.5);
            if (!side_thrusters_straddle_cg) {
                for (size_t k = 0; k < configs_.size(); ++k) {
                    const auto& ck = configs_[k];
                    if (!states_[k].is_healthy) {
                        continue;
                    }
                    const bool is_side_thruster = (ck.name.find("tb") != std::string::npos);
                    const bool is_main_prop = (!ck.is_azimuth && !is_side_thruster && !ck.is_rudder);
                    if (is_side_thruster) {
                        target_thrusts[k] = 0.0;
                        target_angles[k] = ck.angle_fixed;
                    } else if (is_main_prop && tau_des.x() < 0.0) {
                        target_thrusts[k] = 0.0;
                    }
                }
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                    "[DP Lateral Hold] pure Fy disabled: side thrusters do not straddle CG "
                    "(x_range=%.1f..%.1fm). Avoid bow-thruster-induced yaw; Fy=%.1fkN Mz=%.1fkNm",
                    min_side_x, max_side_x, tau_des.y(), tau_des.z());
            } else {
            const double per_side_thrust_N = (tau_des.y() * 1000.0) / static_cast<double>(side_count);
            for (size_t k = 0; k < configs_.size(); ++k) {
                const auto& ck = configs_[k];
                if (!states_[k].is_healthy) {
                    continue;
                }
                const bool is_side_thruster = (ck.name.find("tb") != std::string::npos);
                const bool is_main_prop = (!ck.is_azimuth && !is_side_thruster && !ck.is_rudder);
                if (is_side_thruster) {
                    target_thrusts[k] = per_side_thrust_N;
                    target_angles[k] = ck.angle_fixed;
                } else if (is_main_prop && tau_des.x() < 0.0) {
                    target_thrusts[k] = 0.0;
                }
            }
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[DP Lateral Hold] low-speed Fy priority: Fy=%.1fkN Mz=%.1fkNm side_count=%d per_side=%.0fN",
                tau_des.y(), tau_des.z(), side_count, per_side_thrust_N);
            }
        }
    }

    // ── 场景策略：直航/小艏摇需求下三主桨均衡 ───────────────────────────────
    // 45m FCB 的 3 固定主桨在高速直航时应共同承担纵向推力；小航向误差由双舵处理。
    // 只有在低速机动、明显艏摇需求或横移需求时，才允许差动推力成为主要控制手段。
    if (enable_straight_cruise_main_equalization_ && !is_maneuvering_mode_
        && tau_des.x() > 0.5
        && std::abs(tau_des.y()) <= main_equalization_lateral_deadband_kN_
        && std::abs(tau_des.z()) <= main_equalization_yaw_deadband_kNm_) {
        int main_count = 0;
        double main_sum = 0.0;
        for (size_t k = 0; k < configs_.size(); ++k) {
            const auto& ck = configs_[k];
            bool is_main = (!ck.is_azimuth && ck.name.find("tb") == std::string::npos && !ck.is_rudder);
            if (is_main && states_[k].is_healthy) {
                main_sum += target_thrusts[k];
                main_count++;
            }
        }
        if (main_count > 1) {
            double avg_thrust = (tau_des.x() * 1000.0) / static_cast<double>(main_count);
            for (size_t k = 0; k < configs_.size(); ++k) {
                const auto& ck = configs_[k];
                bool is_main = (!ck.is_azimuth && ck.name.find("tb") == std::string::npos && !ck.is_rudder);
                if (is_main && states_[k].is_healthy) {
                    target_thrusts[k] = avg_thrust;
                }
            }
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[ThrustAllocPolicy] 直航主桨均衡: qp_sum=%.0fN direct_avg=%.0fN Fx=%.1fkN Mz=%.1fkNm",
                main_sum, avg_thrust, tau_des.x(), tau_des.z());
        }
    }

    // ── Step 2.5：【修复后】中主桨洗流保护 - 仅在需要舵效时激活 ─────────────
    // 【BUG根因1修复】：原逻辑无条件强制 5000N，导致纯旋转(Mz≠0, Fx=0)时
    // 中桨被强制输出 5000N，产生 Fx=5000N 的合力误差。
    // 修复策略：洗流保护仅在"高速巡航 AND 舵是主要转向执行器"时生效。
    // 低速差动转向模式下，舵效接近零，此保护无物理意义，必须关闭。
    for (size_t i = 0; i < configs_.size(); ++i) {
        const auto& cfg = configs_[i];

        bool is_main_prop = (cfg.name.find("tb") == std::string::npos && !cfg.is_rudder);
        bool is_center_prop = is_main_prop && (std::abs(cfg.y) < 0.5);

        if (is_center_prop && states_[i].is_healthy) {
            // 洗流保护激活条件：
            //   1. 非差动机动模式（高速巡航，舵有效）
            //   2. 存在纵向力需求（Fx != 0），表明船舶需要前进/后退推力
            //   3. 同时纯力矩需求不占主导（避免纯旋转时干扰）
            double Fx_demand = std::abs(tau_des.x());  // 单位 kN
            double Mz_demand_kNm = std::abs(tau_des.z());  // 单位 kNm
            double u_actual = current_speed_;
            bool is_cruise_with_rudder = !is_maneuvering_mode_;
            bool has_ship_speed        = (u_actual > 1.0);          // 用船速替代指令
            bool wants_forward_wash    = (tau_des.x() > 5.0);
            bool is_pure_rotation      = (Mz_demand_kNm > 1.0) && (Fx_demand < 0.5) && (u_actual < 2.0);

            if (is_cruise_with_rudder && has_ship_speed && wants_forward_wash && !is_pure_rotation) {
                const double MIN_CENTER_THRUST = 8000.0;  // N
                if (target_thrusts[i] < MIN_CENTER_THRUST) {
                    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                        "[ThrustAlloc] 中主桨洗流保护(巡航舵效): %.0fN -> %.0fN",
                        target_thrusts[i], MIN_CENTER_THRUST);
                    target_thrusts[i] = MIN_CENTER_THRUST;
                }
            } else {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[ThrustAlloc] 中主桨洗流保护已跳过 (纯旋转/低速差动模式), 中桨推力=%.0fN",
                    target_thrusts[i]);
            }
            break;
        }
    }

    // ── Step 3：最终输出 + DNV 制动功率估算 ───────────────────────────────────
    std_msgs::msg::Float64MultiArray out_msg;
    double total_p_brake_kW = 0.0;

    for (size_t i = 0; i < configs_.size(); ++i) {
        const auto& cfg = configs_[i];
        auto& state     = states_[i];

        // PGD 已经保证了约束，直接使用
        double final_f = target_thrusts[i];
        double final_a = target_angles[i];
        bool is_side_thruster = (cfg.name.find("tb") != std::string::npos);
        bool is_main_prop = (!cfg.is_azimuth && !is_side_thruster && !cfg.is_rudder);

        if (has_fresh_propulsion_constraints() && !policy_side_thruster_allowed_ && is_side_thruster
            && std::abs(final_f) > 1.0) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[PropulsionPolicyEnforce] 侧推最终输出兜底归零: %s %.0fN",
                cfg.name.c_str(), final_f);
            final_f = 0.0;
        }

        if (has_fresh_propulsion_constraints() && policy_side_thruster_allowed_ && is_side_thruster) {
            double side_limit = cfg.max_thrust_N * std::clamp(policy_side_thruster_max_fraction_, 0.0, 1.0);
            if (std::abs(final_f) > side_limit) {
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[PropulsionPolicyEnforce] side thruster output fraction cap: %s %.0fN -> %.0fN fraction=%.2f",
                    cfg.name.c_str(), final_f, std::copysign(side_limit, final_f),
                    policy_side_thruster_max_fraction_);
                final_f = std::copysign(side_limit, final_f);
            }
        }

        if (has_fresh_propulsion_constraints() && !policy_reverse_allowed_ && is_main_prop
            && final_f < 0.0) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[PropulsionPolicyEnforce] 未授权倒车最终输出兜底归零: %s %.0fN",
                cfg.name.c_str(), final_f);
            final_f = 0.0;
        }

        // [NaN Firewall] 最终兜底检查
        if (std::isnan(final_f) || std::isnan(final_a)) {
            RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 500,
                "🔥 [NaN Firewall] 异常状态！计算出 NaN 推力 (%f, %f)，强制切断！", final_f, final_a);
            final_f = 0.0;
            final_a = state.last_angle_rad; // 保持上一次角度
        }

        // [B4/P1] DNV-ST-0111 制动功率估算与功率限幅（优化版）
        double beta_t = 0.9;
        double p_brake_kW = 0.0;

        if (!cfg.is_rudder && cfg.max_thrust_N > 0.0 &&
            std::abs(final_f) > cfg.max_thrust_N) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[ThrustLimit] %s thrust %.0fN exceeds max_thrust %.0fN, clipping.",
                cfg.name.c_str(), final_f, cfg.max_thrust_N);
            final_f = std::copysign(cfg.max_thrust_N, final_f);
        }

        double eta_product = cfg.eta1 * cfg.eta2;
        if (!cfg.is_rudder && std::abs(final_f) > 1.0 &&
            cfg.max_power_kW > 0.0 && eta_product > 1e-9 &&
            cfg.eta_m > 1e-9 && cfg.diameter > 1e-9) {
            double p_shaft_limit_kW = cfg.max_power_kW * cfg.eta_m;
            double t_nominal_limit_kN =
                eta_product * std::pow(p_shaft_limit_kW * cfg.diameter, 2.0 / 3.0);
            double max_force_by_power_N = beta_t * t_nominal_limit_kN * 1000.0;

            if (std::abs(final_f) > max_force_by_power_N) {
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[PowerLimit] %s thrust %.0fN exceeds %.0fN from %.0fkW, clipping.",
                    cfg.name.c_str(), final_f, max_force_by_power_N, cfg.max_power_kW);
                final_f = std::copysign(max_force_by_power_N, final_f);
            }
        }

        target_thrusts[i] = final_f;
        target_angles[i] = final_a;

        double t_nominal_kN = std::abs(final_f) / (beta_t * 1000.0);
        if (t_nominal_kN > 0.01 && eta_product > 1e-9 &&
            cfg.eta_m > 1e-9 && cfg.diameter > 1e-9) {
            double thrust_factor = t_nominal_kN / eta_product;
            double p_shaft_kW = std::pow(thrust_factor, 1.5) / cfg.diameter;
            p_brake_kW = p_shaft_kW / cfg.eta_m;
        }
        total_p_brake_kW += p_brake_kW;

        // 更新状态（供下次循环使用）
        state.last_thrust_N   = final_f;
        state.last_angle_rad  = final_a;

        // 【Phase 4】更新上一帧舵角指令（用于Fx阻力预测）
        if (cfg.is_rudder) {
            last_rudder_cmd_ = final_a;
        }

        // 封装推力指令：[T₀, α₀, T₁, α₁, ...]
        out_msg.data.push_back(final_f);
        out_msg.data.push_back(final_a);
    }

    cmd_pub_->publish(out_msg);

    // [诊断] 显示实际输出推力（所有执行器）
    if (configs_.size() >= 2) {
        double rudder_angle = (configs_.size() > 5 && configs_[5].is_rudder) ? target_angles[5] * 180 / M_PI : 0;
        double tb1_thrust = (configs_.size() > 3) ? target_thrusts[3] : 0;
        double tb2_thrust = (configs_.size() > 4) ? target_thrusts[4] : 0;

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[推力分配] 主桨:L=%.1f C=%.1f R=%.1f | 侧推:F1=%.1f F2=%.1f | 舵角=%.1f° | V=%.1fkn",
            target_thrusts[0], target_thrusts[1], target_thrusts[2],
            tb1_thrust, tb2_thrust, rudder_angle,
            current_speed_ * 1.94384);
    }

    // 饱和诊断 (通过解算的推力评估)
    double achieved_fx = 0.0, achieved_fy = 0.0, achieved_mz = 0.0;
    for (size_t i = 0; i < configs_.size(); ++i) {
        if (!states_[i].is_healthy) continue;
        double f = states_[i].last_thrust_N;
        double a = states_[i].last_angle_rad;
        double c = std::cos(a);
        double s = std::sin(a);
        achieved_fx += f * c;
        achieved_fy += f * s;
        achieved_mz += f * (configs_[i].x * s - configs_[i].y * c);
    }

    // 若需求力矩和实际输出力矩误差较大，表示推力已经饱和
    Eigen::Vector3d tau_achieved(achieved_fx, achieved_fy, achieved_mz);
    double actual_error = (tau_achieved - tau_des).norm() / (tau_des.norm() + 1e-6);
    if (actual_error > 0.05 && tau_des.norm() > 1000.0) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "⚠️ 合力误差=%.1f%% (期望推力可能超出推进器能力)", actual_error * 100.0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 推进策略约束回调：接收 /propulsion/constraints
// 数组协议：
// [side_allowed, side_fraction, main_symmetry, differential_allowed,
//  rudder_preferred, reverse_allowed, actuator_ratio]
// 当前最小 enforcement 启用三个低争议约束：
//   1) side_allowed=false 时侧推硬锁定
//   2) side_fraction 对侧推做比例限额
//   3) reverse_allowed=false 时主桨禁止倒车
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_propulsion_constraints(
    const std_msgs::msg::Float64MultiArray::SharedPtr msg)
{
    if (msg->data.size() < 6) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[PropulsionPolicyEnforce] /propulsion/constraints size=%zu < 6，忽略本帧",
            msg->data.size());
        return;
    }

    std::unique_lock<std::shared_mutex> lk(mtx_);
    policy_side_thruster_allowed_ = (msg->data[0] >= 0.5);
    policy_side_thruster_max_fraction_ = std::clamp(msg->data[1], 0.0, 1.0);
    policy_rudder_preferred_ = (msg->data[4] >= 0.5);
    policy_reverse_allowed_ = (msg->data[5] >= 0.5);
    propulsion_constraints_received_ = true;
    last_propulsion_constraints_time_ = this->now();

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "[PropulsionPolicyEnforce] constraints: side_allowed=%d side_fraction=%.2f rudder_preferred=%d reverse_allowed=%d",
        policy_side_thruster_allowed_ ? 1 : 0,
        policy_side_thruster_max_fraction_,
        policy_rudder_preferred_ ? 1 : 0,
        policy_reverse_allowed_ ? 1 : 0);
}

bool AutonomousThrustAllocator::has_fresh_propulsion_constraints()
{
    if (!propulsion_constraints_received_) {
        // No constraints publisher: do not enforce policy defaults, let speed/lockout logic handle side thrusters.
        return false;
    }
    return (this->now() - last_propulsion_constraints_time_).seconds()
        <= propulsion_constraints_stale_timeout_s_;
}

// ─────────────────────────────────────────────────────────────────────────────
// 环境力回调：接收 /env/total_load，缓存环境力
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_env_callback(
    const geometry_msgs::msg::WrenchStamped::SharedPtr msg) {
    if (msg->header.frame_id != "base_link") {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[EnvFeedforward] expected base_link load, got '%s'; ignoring sample",
            msg->header.frame_id.c_str());
        return;
    }

    // 使用独占锁，因为我们正在修改 tau_env_ 变量
    std::unique_lock<std::shared_mutex> lk(mtx_);
    
    // Cache base_link environmental load in allocator units (kN/kNm).
    tau_env_ << msg->wrench.force.x * 1e-3,
                msg->wrench.force.y * 1e-3,
                msg->wrench.torque.z * 1e-3;
    
    // [NaN Firewall] 防止环境引擎发送 NaN
    if (std::isnan(tau_env_.x()) || std::isnan(tau_env_.y()) || std::isnan(tau_env_.z())) {
        RCLCPP_ERROR_THROTTLE(this->get_logger(), *this->get_clock(), 500,
            "🔥 [NaN Firewall] 环境节点发送了 NaN 载荷！已拦截。");
        tau_env_.setZero();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 里程计回调：缓存当前偏航角
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_odom_callback(
    const nav_msgs::msg::Odometry::SharedPtr msg) {
    tf2::Quaternion q(
        msg->pose.pose.orientation.x,
        msg->pose.pose.orientation.y,
        msg->pose.pose.orientation.z,
        msg->pose.pose.orientation.w);
    tf2::Matrix3x3 m(q);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);
    
    // 提取纵向速度（body frame）
    double vx = msg->twist.twist.linear.x;
    double vy = msg->twist.twist.linear.y;
    double speed = std::sqrt(vx*vx + vy*vy);
    
    std::unique_lock<std::shared_mutex> lk(mtx_);
    current_yaw_ = yaw;
    current_speed_ = speed;
    current_v_ = vy;
}

// ─────────────────────────────────────────────────────────────────────────────
// [架构补全] 手动指令回调 (MANUAL模式)
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_manual_cmd_callback(
    const std_msgs::msg::Float64MultiArray::SharedPtr msg)
{
    if (control_mode_ != ControlMode::MANUAL) {
        return;  // 仅在MANUAL模式下处理
    }

    std::unique_lock<std::shared_mutex> lk(mtx_);

    // [架构防火墙] 定长双槽位协议 - 每个推进器固定占用 [thrust, angle] 两个槽位
    size_t expected_size = configs_.size() * 2;
    if (msg->data.size() < expected_size) {
        RCLCPP_ERROR(this->get_logger(),
            "[架构防火墙] 手动指令格式错误！期望 size=%zu, 收到 size=%zu",
            expected_size, msg->data.size());
        return;
    }

    std::vector<double> target_thrusts(configs_.size(), 0.0);
    std::vector<double> target_angles(configs_.size(), 0.0);

    // 定长槽位解析 - 无论推进器类型，始终从固定位置读取
    for (size_t i = 0; i < configs_.size(); ++i) {
        double cmd_thrust = msg->data[i * 2];       // 固定从偶数位读推力
        double cmd_angle  = msg->data[i * 2 + 1];   // 固定从奇数位读角度

        if (configs_[i].is_rudder) {
            // 舵：忽略推力槽位，只读取角度槽位
            target_thrusts[i] = 0.0;
            target_angles[i] = std::clamp(cmd_angle,
                                          -configs_[i].max_angle,
                                          configs_[i].max_angle);
        }
        else if (configs_[i].is_azimuth) {
            // 全回转：两个槽位都读取
            target_thrusts[i] = cmd_thrust * 1000.0; // kN 转 N
            target_angles[i] = cmd_angle;
        }
        else {
            // 固定轴推力器 (主桨 t1/t2/t3, 侧推 tb1/tb2)
            // 忽略传入的角度槽位，强制使用 yaml 里的固定安装角
            target_thrusts[i] = cmd_thrust * 1000.0; // kN 转 N
            target_angles[i] = configs_[i].angle_fixed;
        }
    }

    // 发布手动指令
    std_msgs::msg::Float64MultiArray out_msg;
    out_msg.data.resize(2 * configs_.size()); // [force, angle] per thruster
    for (size_t i = 0; i < configs_.size(); ++i) {
        out_msg.data[2*i]   = target_thrusts[i];
        out_msg.data[2*i+1] = target_angles[i];
    }
    cmd_pub_->publish(out_msg);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
        "[MANUAL] 收到开环指令解析成功，正在下发到底层...");
}

// ─────────────────────────────────────────────────────────────────────────────
// 健康状态更新回调
// ─────────────────────────────────────────────────────────────────────────────
void AutonomousThrustAllocator::on_health_update(
    const std_msgs::msg::Float64MultiArray::SharedPtr msg) {
    // [B3/P1] unique_lock：写操作，阻塞所有并发读
    std::unique_lock<std::shared_mutex> lk(mtx_);

    // Debug: print raw health values
    RCLCPP_INFO(this->get_logger(), "[DEBUG] on_health_update received %zu values:", msg->data.size());
    std::string health_str;
    for (size_t i = 0; i < msg->data.size() && i < states_.size(); ++i) {
        health_str += std::to_string(msg->data[i]) + " ";
    }
    RCLCPP_INFO(this->get_logger(), "[DEBUG] health values: %s", health_str.c_str());

    bool changed = false;
    for (size_t i = 0; i < msg->data.size() && i < states_.size(); ++i) {
        bool current_health = (msg->data[i] > 0.5);
        if (states_[i].is_healthy != current_health) {
            states_[i].is_healthy = current_health;
            changed = true;
            RCLCPP_WARN(this->get_logger(),
                "推进器 [%s] 状态变更 -> %s",
                configs_[i].name.c_str(),
                current_health ? "正常" : "故障");
        }
    }

    // 持锁期间重建矩阵，保证 tau_callback 读到的矩阵始终一致
    if (changed) {
        rebuild_allocation_matrices();
        RCLCPP_INFO(this->get_logger(), "推力分配矩阵已重建（%zu 个健康推进器）",
            std::count_if(states_.begin(), states_.end(),
                          [](const ThrusterState& s){ return s.is_healthy; }));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// main
// ─────────────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<AutonomousThrustAllocator>());
    rclcpp::shutdown();
    return 0;
}
