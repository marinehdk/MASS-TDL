/**
 * @file wave_engine_node.hpp
 * @brief Wave load engine with separated wave-frequency and drift loads.
 *
 * Publishes:
 *   /env/wave/raw_load   : first-order + second-order loads, base_link
 *   /env/wave/drift_load : second-order drift loads only, base_link
 *
 * The aggregator should normally consume drift_load, not raw_load, because GNC
 * should not chase wave-frequency forces.
 */

#ifndef ENV_ENGINES_WAVE_ENGINE_NODE_HPP
#define ENV_ENGINES_WAVE_ENGINE_NODE_HPP

#include "env_engines/env_engine_base.hpp"
#include "env_engines/hydro_parser.hpp"
#include "geometry_msgs/msg/vector3.hpp"
#include "geometry_msgs/msg/wrench_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "ship_interfaces/msg/vessel_params.hpp"
#include "std_msgs/msg/float64.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <shared_mutex>
#include <string>
#include <vector>

namespace env_engines {

constexpr double DEFAULT_WAVE_LPP       = 150.0;
constexpr double DEFAULT_WAVE_LOS       = 155.0;
constexpr double DEFAULT_WAVE_B         = 30.0;
constexpr double DEFAULT_WAVE_BOW_ANGLE = 0.6;
constexpr double DEFAULT_WAVE_C_WL_AFT  = 0.95;
constexpr double DEFAULT_WAVE_XLOS      = 77.5;
constexpr double DEFAULT_WAVE_HS        = 3.5;
constexpr double DEFAULT_WAVE_TZ        = 8.5;
constexpr double DEFAULT_WAVE_DIRECTION = M_PI / 4.0;
constexpr double DEFAULT_WAVE_DRAFT     = 10.0;

constexpr double MIN_WAVE_HS        = 0.0;
constexpr double MAX_WAVE_HS        = 30.0;
constexpr double MIN_WAVE_TZ        = 1.0;
constexpr double MAX_WAVE_TZ        = 30.0;
constexpr double MIN_WAVE_DIRECTION = 0.0;
constexpr double MAX_WAVE_DIRECTION = 2.0 * M_PI;
constexpr double MIN_C_WL_AFT       = 0.5;
constexpr double MAX_C_WL_AFT       = 1.5;

constexpr double DEFAULT_WAVE_SPREADING_FACTOR = 0.5;
constexpr double DEFAULT_WAVE_INPUT_TIMEOUT_S = 3.0;
constexpr bool DEFAULT_WAVE_DIRECTION_IS_FROM = false;
constexpr int DEFAULT_WAVE_DIRECTION_COMPONENTS = 9;
constexpr double DEFAULT_WAVE_WATER_DEPTH = 50.0;
constexpr double MIN_WAVE_WATER_DEPTH = 0.5;

constexpr double DEFAULT_INFERRED_DRIFT_SURGE_SCALE = 0.06;
constexpr double DEFAULT_INFERRED_DRIFT_SWAY_SCALE = 0.010;
constexpr double DEFAULT_INFERRED_DRIFT_YAW_LEVER_SCALE = 0.12;
constexpr double DEFAULT_INFERRED_DRIFT_ROLL_LEVER_SCALE = 0.45;

struct VesselParams {
    double Lpp = DEFAULT_WAVE_LPP;
    double Los = DEFAULT_WAVE_LOS;
    double B = DEFAULT_WAVE_B;
    double bow_angle_rad = DEFAULT_WAVE_BOW_ANGLE;
    double C_WL_aft = DEFAULT_WAVE_C_WL_AFT;
    double xLos = DEFAULT_WAVE_XLOS;
    double T = DEFAULT_WAVE_DRAFT;
    double KG = 7.0;
    double GM_T = 1.5;
    double displacement_ton = 50000.0;
};

struct WaveComponent {
    double amplitude = 0.0;
    double omega = 0.0;
    double k = 0.0;
    double phase = 0.0;
};

struct DirectionalSample {
    double offset_rad = 0.0;
    double weight = 1.0;
};

struct EnvParams {
    double Hs = DEFAULT_WAVE_HS;
    double Tz = DEFAULT_WAVE_TZ;
    double direction_rad = DEFAULT_WAVE_DIRECTION;
};

struct WaveLoadsSeparated {
    double Fx_1st = 0.0;
    double Fy_1st = 0.0;
    double Mx_1st = 0.0;
    double Mz_1st = 0.0;

    double Fx_2nd = 0.0;
    double Fy_2nd = 0.0;
    double Mx_2nd = 0.0;
    double Mz_2nd = 0.0;
};

enum class WaveSourceMode {
    Auto,
    Topic,
    Params
};

enum class WaveDriftModel {
    Auto,
    QtfTable,
    Inferred
};

struct InferredDriftCoefficients {
    double Fx_N_per_m2 = 0.0;
    double Fy_N_per_m2 = 0.0;
    double Mx_Nm_per_m2 = 0.0;
    double Mz_Nm_per_m2 = 0.0;
};

class WaveEngineNode : public EnvEngineBase {
public:
    WaveEngineNode();

private:
    void load_qtf_csv();
    void rebuild_wave_spectrum_locked(const EnvParams& env, double water_depth);
    void ensure_spectrum_for_env_locked(const EnvParams& env, double water_depth);
    void update_heave_natural_freq();
    void update_roll_natural_freq();

    double calc_rao_heave(double omega) const;
    double calc_rao_dp(double omega, double cutoff) const;
    double calc_rao_roll(double omega) const;
    double solve_wave_number(double omega, double water_depth) const;
    double encounter_frequency(double omega, double wave_number,
                               double forward_speed, double relative_wave_dir) const;
    InferredDriftCoefficients calc_inferred_drift_coefficients(
        const WaveComponent& component, double relative_wave_dir,
        const VesselParams& vessel) const;

    double directional_spreading(double delta, double spread_factor) const;
    std::vector<DirectionalSample> direction_samples() const;

    void calc_first_order_loads(
        double gamma, double sim_time, const VesselParams& vessel,
        const std::vector<WaveComponent>& components,
        double forward_speed,
        double& Fx_1st, double& Fy_1st, double& Mx_1st, double& Mz_1st) const;

    WaveLoadsSeparated calculateWaveDriftForces(
        const EnvParams& env, const VesselParams& vessel,
        const std::vector<WaveComponent>& components,
        double current_heading_deg, double forward_speed, double sim_time);

    void main_calc_loop();
    void env_params_callback(const geometry_msgs::msg::Vector3::SharedPtr msg);
    void vessel_params_callback(const ship_interfaces::msg::VesselParams::SharedPtr msg);
    void heading_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);

    bool validate_vessel_params();
    bool validate_env_params(const EnvParams& env, const std::string& source);
    rcl_interfaces::msg::SetParametersResult
        parameter_callback(const std::vector<rclcpp::Parameter>& parameters);

    static double normalize_radians(double angle);
    static bool parse_wave_source_mode(const std::string& mode, WaveSourceMode& parsed);
    static const char* wave_source_mode_name(WaveSourceMode mode);
    static bool parse_wave_drift_model(const std::string& mode, WaveDriftModel& parsed);
    static const char* wave_drift_model_name(WaveDriftModel mode);
    bool is_topic_wave_fresh(const rclcpp::Time& now) const;
    EnvParams selected_env_locked(const rclcpp::Time& now) const;

    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr raw_publisher_;
    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr drift_publisher_;

    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Subscription<geometry_msgs::msg::Vector3>::SharedPtr env_sub_;
    rclcpp::Subscription<ship_interfaces::msg::VesselParams>::SharedPtr vessel_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_callback_handle_;

    VesselParams vessel_params_;
    EnvParams param_env_;
    EnvParams topic_env_;
    bool topic_wave_received_ = false;
    rclcpp::Time last_wave_input_time_{0, 0, RCL_ROS_TIME};

    double ship_heading_ = 0.0;
    double ship_u_ = 0.0;
    double ship_v_ = 0.0;
    bool use_fixed_heading_ = false;
    double fixed_heading_ = 0.0;

    double water_density_ = 1025.0;
    double water_depth_ = DEFAULT_WAVE_WATER_DEPTH;
    double gravity_ = 9.81;
    double fk_scale_factor_ = 0.1;
    std::string qtf_csv_path_;
    bool qtf_table_loaded_ = false;
    double displacement_kg_ = 5.0e7;

    double spreading_factor_ = DEFAULT_WAVE_SPREADING_FACTOR;
    int direction_components_ = DEFAULT_WAVE_DIRECTION_COMPONENTS;

    WaveSourceMode wave_source_mode_ = WaveSourceMode::Auto;
    WaveDriftModel wave_drift_model_ = WaveDriftModel::Inferred;
    bool wave_direction_is_from_ = DEFAULT_WAVE_DIRECTION_IS_FROM;
    double wave_input_timeout_s_ = DEFAULT_WAVE_INPUT_TIMEOUT_S;

    double inferred_drift_surge_scale_ = DEFAULT_INFERRED_DRIFT_SURGE_SCALE;
    double inferred_drift_sway_scale_ = DEFAULT_INFERRED_DRIFT_SWAY_SCALE;
    double inferred_drift_yaw_lever_scale_ = DEFAULT_INFERRED_DRIFT_YAW_LEVER_SCALE;
    double inferred_drift_roll_lever_scale_ = DEFAULT_INFERRED_DRIFT_ROLL_LEVER_SCALE;

    double rao_omega_n_heave_ = 0.8;
    double rao_omega_n_roll_ = 0.4;
    double rao_damping_heave_ = 0.10;
    double rao_damping_roll_ = 0.15;
    double rao_cutoff_surge_ = 0.25;
    double rao_cutoff_sway_ = 0.30;
    double rao_cutoff_yaw_ = 0.20;
    double rao_scale_max_ = 3.0;
    double rao_scale_max_roll_ = 5.0;
    double rao_surge_scale_ = 1.0;
    double rao_sway_scale_ = 1.0;
    double rao_roll_scale_ = 1.0;
    double rao_yaw_scale_ = 1.0;

    std::vector<WaveComponent> wave_components_;
    double spectrum_hs_ = -1.0;
    double spectrum_tz_ = -1.0;
    double spectrum_depth_ = -1.0;
    double sim_time_ = 0.0;
    bool spectrum_initialized_ = false;
    int num_wave_components_ = 50;
    double jonswap_gamma_ = 3.3;

    HydroParser hydro_parser_;

    mutable std::shared_mutex params_mutex_;
    WaveLoadsSeparated last_valid_result_;
    rclcpp::Time last_log_time_;
    rclcpp::Time last_update_time_;
    uint64_t calc_count_ = 0;
};

} // namespace env_engines

#endif // ENV_ENGINES_WAVE_ENGINE_NODE_HPP
