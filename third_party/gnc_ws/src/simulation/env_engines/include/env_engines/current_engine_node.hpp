/**
 * @file current_engine_node.hpp
 * @brief Current load engine for 4-DOF environmental loads.
 *
 * Publishes /env/current_load as a base_link WrenchStamped:
 *   force.x  = surge force (N)
 *   force.y  = sway force (N)
 *   torque.x = roll moment (N*m)
 *   torque.z = yaw moment (N*m)
 *
 * Direction convention defaults to "to": 0 deg is +x, 90 deg is +y.
 * Set current_direction_is_from=true when inputs are oceanographic "from"
 * directions; the node converts them to flow-vector directions internally.
 */

#ifndef ENV_ENGINES_CURRENT_ENGINE_NODE_HPP
#define ENV_ENGINES_CURRENT_ENGINE_NODE_HPP

#include "env_engines/env_engine_base.hpp"
#include "env_engines/hydro_parser.hpp"
#include "geometry_msgs/msg/wrench_stamped.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "ship_interfaces/msg/ocean_currents.hpp"
#include "ship_interfaces/msg/vessel_params.hpp"
#include "std_msgs/msg/float64.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <shared_mutex>
#include <string>

namespace env_engines {

constexpr double DEFAULT_CURR_LPP      = 80.0;
constexpr double DEFAULT_CURR_B        = 15.0;
constexpr double DEFAULT_CURR_T        = 6.0;
constexpr double DEFAULT_CURR_D        = 50.0;
constexpr double DEFAULT_CURR_V_TIDE   = 1.5;
constexpr double DEFAULT_CURR_DIR_TIDE = 90.0;
constexpr double DEFAULT_CURR_V_WIND   = 0.6;
constexpr double DEFAULT_CURR_DIR_WIND = 180.0;
constexpr double DEFAULT_CURR_V_CIRC   = 0.3;
constexpr double DEFAULT_CURR_DIR_CIRC = 120.0;

constexpr double DEFAULT_CURRENT_INPUT_TIMEOUT_S = 3.0;
constexpr bool DEFAULT_CURRENT_DIRECTION_IS_FROM = false;
constexpr double DEFAULT_CURRENT_ROLL_MOMENT_ARM = -1.0; // < 0: use KG - T/3

constexpr double MIN_DEPTH         = 1.0;
constexpr double MIN_DRAFT         = 0.1;
constexpr double MAX_CURRENT_SPEED = 10.0;
constexpr double WIND_DRIFT_DEPTH  = 50.0;
constexpr double MIN_CURRENT_EPS   = 1.0e-6;

struct CurrentComponents {
    double v_tide_surf = DEFAULT_CURR_V_TIDE;
    double dir_tide = DEFAULT_CURR_DIR_TIDE;
    double v_wind_surf = DEFAULT_CURR_V_WIND;
    double dir_wind = DEFAULT_CURR_DIR_WIND;
    double v_circ_surf = DEFAULT_CURR_V_CIRC;
    double dir_circ = DEFAULT_CURR_DIR_CIRC;
};

class CurrentEngineNode : public EnvEngineBase {
public:
    CurrentEngineNode();

private:
    void main_calc_loop();

    bool calculate_depth_averaged_currents(
        const CurrentComponents& current, double draft, double depth,
        double& v_t_avg, double& v_w_avg, double& v_c_avg);

    bool calculate_horizontal_components(
        const CurrentComponents& current,
        double v_t_avg, double v_w_avg, double v_c_avg,
        double& vx, double& vy);

    void calculate_forces(double rel_flow_x, double rel_flow_y,
                          double v_total, double apparent_dir,
                          double draft, double depth, double lpp, double beam,
                          double kg, double water_density, double roll_moment_arm,
                          geometry_msgs::msg::WrenchStamped& msg);

    void ocean_currents_callback(const ship_interfaces::msg::OceanCurrents::SharedPtr msg);
    void vessel_params_callback(const ship_interfaces::msg::VesselParams::SharedPtr msg);
    void heading_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);

    void load_coefficient_csv();
    bool validate_vessel_params();
    bool validate_current_params();
    bool validate_current_params(const CurrentComponents& current, const std::string& source);

    static double normalize_degrees(double angle);
    static void normalize_component(double& speed, double& direction_deg, bool direction_is_from);
    bool is_topic_current_fresh(const rclcpp::Time& now) const;
    CurrentComponents selected_current_locked(const rclcpp::Time& now) const;

    rclcpp::Publisher<geometry_msgs::msg::WrenchStamped>::SharedPtr publisher_;
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::Subscription<ship_interfaces::msg::OceanCurrents>::SharedPtr current_sub_;
    rclcpp::Subscription<ship_interfaces::msg::VesselParams>::SharedPtr vessel_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr heading_sub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;

    double Lpp_ = DEFAULT_CURR_LPP;
    double B_ = DEFAULT_CURR_B;
    double T_ = DEFAULT_CURR_T;
    double d_ = DEFAULT_CURR_D;
    double KG_ = DEFAULT_CURR_T * 0.65;
    double current_roll_moment_arm_ = DEFAULT_CURRENT_ROLL_MOMENT_ARM;

    double ship_heading_ = 0.0;
    double current_u_ = 0.0;
    double current_v_ = 0.0;

    double water_density_ = 1025.0;
    std::string coeffs_csv_path_;
    bool subtract_calm_water_damping_ = true;
    bool current_direction_is_from_ = DEFAULT_CURRENT_DIRECTION_IS_FROM;
    double current_input_timeout_s_ = DEFAULT_CURRENT_INPUT_TIMEOUT_S;

    CurrentComponents param_current_;
    CurrentComponents topic_current_;
    bool current_topic_received_ = false;
    rclcpp::Time last_current_input_time_{0, 0, RCL_ROS_TIME};
    bool coeff_table_loaded_ = false;

    HydroParser hydro_parser_;
    mutable std::shared_mutex params_mutex_;

    uint64_t calc_count_ = 0;
    rclcpp::Time last_log_time_;
};

} // namespace env_engines

#endif // ENV_ENGINES_CURRENT_ENGINE_NODE_HPP
