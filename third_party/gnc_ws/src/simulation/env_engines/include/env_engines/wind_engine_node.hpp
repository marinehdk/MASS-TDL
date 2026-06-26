#ifndef ENV_ENGINES_WIND_ENGINE_NODE_HPP
#define ENV_ENGINES_WIND_ENGINE_NODE_HPP

#include "env_engines/env_engine_base.hpp"
#include "geometry_msgs/msg/wrench_stamped.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "ship_interfaces/msg/vessel_params.hpp"
#include "std_msgs/msg/float64.hpp"
#include <nav_msgs/msg/odometry.hpp>
#include <array>
#include <cmath>
#include <map>
#include <random>
#include <shared_mutex>
#include <string>

namespace env_engines {

// 风力频谱组件数（固定大小，避免动态分配）
constexpr size_t WIND_SPECTRUM_COMPONENTS = 50;

// 风力系数表大小 (10度分辨率，0到350度)
constexpr size_t WIND_COEFF_TABLE_SIZE = 36;

// 默认参数值
constexpr double DEFAULT_U10 = 0.0;
constexpr double DEFAULT_Z_CENTER = 15.0;
constexpr double DEFAULT_AF = 450.0;
constexpr double DEFAULT_AL = 1500.0;
constexpr double DEFAULT_LPP = 270.0;
constexpr double DEFAULT_WIND_DIR = 45.0;  // 默认风向
constexpr double DEFAULT_AIR_DENSITY = 1.225;
constexpr double DEFAULT_WIND_ROLL_MOMENT_ARM = -1.0; // < 0: use z_center
constexpr double MIN_WIND_SPEED = 0.1;     // 最小风速，避免除零
constexpr double MAX_WIND_VARIATION = 0.6; // 最大风速波动范围（相对于平均值）
constexpr bool DEFAULT_WIND_DIRECTION_IS_FROM = true;
constexpr double DEFAULT_WIND_INPUT_TIMEOUT_S = 3.0;
constexpr bool DEFAULT_WIND_INPUT_FILTER_ENABLED = true;
constexpr double DEFAULT_WIND_SPEED_FILTER_TAU_S = 6.0;
constexpr double DEFAULT_WIND_SPEED_RATE_LIMIT_MPS_S = 2.0;
constexpr double DEFAULT_WIND_DIRECTION_RATE_LIMIT_DEG_S = 20.0;

/**
 * @brief 风力频谱组件
 */
struct WindComponent {
    double freq = 0.0;
    double amplitude = 0.0;
    double phase = 0.0;
};

/**
 * @brief 风力系数表项
 */
struct WindCoeffEntry {
    double angle = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double cn = 0.0;
};

/**
 * @brief 风力计算引擎节点
 * 
 * 基于 DNV Frøya 频谱计算风力载荷
 * 使用固定大小的数组避免实时线程中的动态内存分配
 */
class WindEngineNode : public EnvEngineBase {
public:
    WindEngineNode();
    
private:
    enum class WindSourceMode {
        Auto,
        U10,
        Anemometer
    };

    enum class EffectiveWindSource {
        None,
        U10,
        Anemometer
    };

    // 初始化风力系数表
    void init_coefficient_tables();
    
    // 初始化随机频谱
    void init_spectrum();
    
    // 插值函数
    std::tuple<double, double, double> interpolate_coeff(double angle, const std::array<WindCoeffEntry, WIND_COEFF_TABLE_SIZE>& table);
    
    // 主计算循环
    void calculate_step();
    
    // 订阅回调函数
    void wind_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg);
    void anemometer_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg);
    void vessel_params_callback(const ship_interfaces::msg::VesselParams::SharedPtr msg);
    void heading_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg); // 船舶首向回调
    
    // 更新风速计算
    void update_wind_speed();

    // Helpers for robust runtime parameter updates.
    static double normalize_degrees(double angle);
    static bool parse_wind_source_mode(const std::string& mode, WindSourceMode& parsed);
    static const char* wind_source_mode_name(WindSourceMode mode);
    static const char* effective_wind_source_name(EffectiveWindSource source);
    static double shortest_angle_delta_degrees(double target, double current);
    bool validate_nonnegative(const std::string& name, double value) const;
    bool is_input_available(bool received, const rclcpp::Time& stamp, const rclcpp::Time& now) const;
    bool select_wind_source_locked();
    bool apply_wind_input_filter_locked(double dt);
    
    // 验证并更新参数
    bool validate_and_update_params();
    
    // 发布器
    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr publisher_;
    
    // 定时器
    rclcpp::TimerBase::SharedPtr timer_;

    // 参数热更新句柄
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_cb_handle_;
    
    // 订阅器
    rclcpp::Subscription<geometry_msgs::msg::Vector3>::SharedPtr wind_sub_;
    rclcpp::Subscription<geometry_msgs::msg::Vector3>::SharedPtr anemometer_sub_;
    rclcpp::Subscription<ship_interfaces::msg::VesselParams>::SharedPtr vessel_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;  // 船舶里程计订阅器

    // 参数
    double u10_ = DEFAULT_U10;
    double z_c_ = DEFAULT_Z_CENTER;
    double Af_ = DEFAULT_AF;
    double Al_ = DEFAULT_AL;
    double Lpp_ = DEFAULT_LPP;
    double wind_roll_moment_arm_ = DEFAULT_WIND_ROLL_MOMENT_ARM;
    double wind_direction_ = DEFAULT_WIND_DIR;  // 风速仪/气象风向：风从该方向来（度，真北顺时针）
    bool wind_direction_is_from_ = DEFAULT_WIND_DIRECTION_IS_FROM;
    double air_density_ = DEFAULT_AIR_DENSITY;
    double wind_input_timeout_s_ = DEFAULT_WIND_INPUT_TIMEOUT_S;
    bool wind_input_filter_enabled_ = DEFAULT_WIND_INPUT_FILTER_ENABLED;
    double wind_speed_filter_tau_s_ = DEFAULT_WIND_SPEED_FILTER_TAU_S;
    double wind_speed_rate_limit_mps_s_ = DEFAULT_WIND_SPEED_RATE_LIMIT_MPS_S;
    double wind_direction_rate_limit_deg_s_ = DEFAULT_WIND_DIRECTION_RATE_LIMIT_DEG_S;
    WindSourceMode wind_source_mode_ = WindSourceMode::Auto;
    EffectiveWindSource effective_wind_source_ = EffectiveWindSource::None;
    double target_u10_ = DEFAULT_U10;
    double target_wind_direction_ = DEFAULT_WIND_DIR;
    bool wind_filter_initialized_ = false;
    double last_spectrum_u10_ = DEFAULT_U10;

    // Wind source arbitration. Explicit U10 wins in auto mode while fresh.
    double explicit_u10_ = DEFAULT_U10;
    double explicit_wind_direction_ = DEFAULT_WIND_DIR;
    bool explicit_u10_received_ = false;
    rclcpp::Time last_explicit_u10_time_{0, 0, RCL_ROS_TIME};

    // 计算得到的参数
    double u_avg_z_ = 0.0;      // 高度修正后的平均风速
    double target_sigma_ = 0.0; // 目标标准差

    // 风速仪参数
    double anemometer_wind_speed_ = 0.0;
    double anemometer_u10_ = DEFAULT_U10;
    double anemometer_wind_direction_ = DEFAULT_WIND_DIR;
    double anemometer_height_ = 0.0;
    bool anemometer_received_ = false;
    bool use_anemometer_ = false;
    rclcpp::Time last_anemometer_time_{0, 0, RCL_ROS_TIME};

    // 船舶首向
    double ship_heading_ = 0.0; // 船舶首向（度）

    // 船舶速度（用于视风计算）
    double current_u_ = 0.0;  // 船体系纵荡速度 (m/s)
    double current_v_ = 0.0;  // 船体系横荡速度 (m/s)
    
    // 风力系数表（固定大小数组，无动态分配）
    std::array<WindCoeffEntry, WIND_COEFF_TABLE_SIZE> coeff_table_;
    
    // 随机频谱组件（固定大小数组）
    std::array<WindComponent, WIND_SPECTRUM_COMPONENTS> components_;
    
    // 随机数生成器
    std::mt19937 rng_;
    std::uniform_real_distribution<double> phase_dist_;
    
    // 仿真时间
    double sim_time_ = 0.0;
    
    // 互斥锁
    mutable std::shared_mutex params_mutex_;
    
    // 统计信息
    uint64_t calc_count_ = 0;
    double min_wind_observed_ = std::numeric_limits<double>::max();
    double max_wind_observed_ = 0.0;
    rclcpp::Time last_log_time_;
    
    // 计算时间统计
    double min_calc_time_ = 1e9;
    double max_calc_time_ = 0.0;
    double avg_calc_time_ = 0.0;
    uint64_t calc_time_count_ = 0;
    
    // 参数变化回调
    rcl_interfaces::msg::SetParametersResult parameter_callback(const std::vector<rclcpp::Parameter>& parameters);
};

} // namespace env_engines

#endif // ENV_ENGINES_WIND_ENGINE_NODE_HPP
