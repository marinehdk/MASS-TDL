#ifndef SHIP_GUIDANCE_NODE_HPP
#define SHIP_GUIDANCE_NODE_HPP

#include <rclcpp/rclcpp.hpp>
#include <geometry_msgs/msg/wrench_stamped.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/float64.hpp>
#include <tf2/LinearMath/Quaternion.h>
#include <tf2/LinearMath/Matrix3x3.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>
#include <Eigen/Dense>
#include <vector>
#include <string>
#include <mutex>
#include <cstdint>

struct Waypoint {
    double x;
    double y;
    double speed_override = -1.0;
    bool is_arc_point = false;
    bool speed_override_is_route_limit = false;
    double turn_angle_deg = 0.0;
    std::string navigation_mode;
};

class ShipGuidanceNode : public rclcpp::Node {
public:
    ShipGuidanceNode();
    ~ShipGuidanceNode() = default;

private:
    void get_parameters();
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);
    void env_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    void current_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg);
    void path_callback(const nav_msgs::msg::Path::SharedPtr msg);
    void control_loop();
    void calculate_los(double x, double y, double& psi_d, double& u_d);
    void publish_final_dp_hold(double x, double y, double& psi_d, double& u_d);
    bool select_dp_weathervane_heading(double fallback_heading,
                                       double& selected_heading,
                                       double& env_force_norm_n,
                                       double& env_age_s);
    double apply_heading_rate_limit(double raw_psi_cmd, double dt);
    std::vector<Waypoint> smooth_waypoints_for_turns(const std::vector<Waypoint>& raw_waypoints);

    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr env_load_sub_;
    rclcpp::Subscription<geometry_msgs::msg::WrenchStamped>::SharedPtr current_load_sub_;
    rclcpp::Subscription<nav_msgs::msg::Path>::SharedPtr path_sub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr heading_setpoint_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr target_speed_pub_;
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr target_pose_pub_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr smoothed_waypoints_pub_;
    rclcpp::TimerBase::SharedPtr timer_;
    double guidance_period_s_ = 0.5;

    std::mutex state_mutex_;
    double current_x_, current_y_, current_yaw_, current_u_, current_v_, current_r_;
    double current_roll_{0.0};
    double current_roll_rate_{0.0};
    double current_env_fx_n_{0.0};
    double current_env_fy_n_{0.0};
    double current_env_mz_nm_{0.0};
    rclcpp::Time last_env_load_time_{0, 0, RCL_ROS_TIME};
    bool env_load_received_{false};
    double current_load_fx_n_{0.0};
    double current_load_fy_n_{0.0};
    double current_load_mz_nm_{0.0};
    rclcpp::Time last_current_load_time_{0, 0, RCL_ROS_TIME};
    bool current_load_received_{false};
    bool odom_received_;

    // 导引参数
    double delta_lookahead_;
    double capture_radius_;
    double intermediate_capture_radius_;
    double intermediate_overrun_radius_;
    // [Task 3] 动态前瞻距离参数
    double Lpp_;                    // 船长 (m)
    double delta_min_coeff_;         // 最小前瞻系数
    double gamma_lookahead_;         // 速度前瞻系数
    double max_speed_;
    double minimum_steerage_speed_;
    double cruise_min_speed_mps_ = 3.8;
    double slow_down_dist_;
    double final_dp_slow_down_dist_;
    double final_dp_stop_decel_mps2_;
    double final_dp_stop_min_lpp_;
    double final_dp_stop_max_lpp_;
    double final_capture_radius_;
    double final_stop_radius_;
    double final_capture_speed_;
    double final_dp_brake_radius_;
    double final_dp_brake_speed_;
    double final_dp_min_closing_speed_mps_;
    double final_handoff_speed_;
    double final_capture_max_cross_track_m_;
    double final_reacquire_cross_track_m_;
    double final_reacquire_min_speed_mps_;
    bool terminal_decel_use_position_error_;
    double final_approach_lookahead_m_;
    bool final_approach_use_adaptive_los_;
    bool dp_weathervane_heading_enabled_ = true;
    double dp_weathervane_min_env_force_n_ = 500.0;
    double dp_weathervane_stale_timeout_s_ = 2.0;
    bool dp_weathervane_strict_handoff_enabled_ = true;
    double dp_weathervane_handoff_max_cross_track_m_ = 35.0;
    double dp_weathervane_handoff_speed_mps_ = 0.9;
    double turn_speed_15deg_;
    double turn_speed_45deg_;
    double turn_speed_90deg_;
    double turn_speed_180deg_;
    double turn_no_slowdown_angle_deg_;
    double turn_slow_down_dist_15deg_;
    double turn_slow_down_dist_45deg_;
    double turn_slow_down_dist_90deg_;
    double turn_slow_down_dist_180deg_;
    bool turn_arc_smoothing_enabled_;
    double turn_arc_radius_m_;
    double turn_arc_max_tangent_m_ = 120.0;
    double turn_arc_sample_spacing_m_;
    double turn_arc_min_angle_deg_;
    double turn_arc_speed_mps_;
    double turn_arc_pre_decel_dist_m_;
    double turn_arc_shallow_radius_m_;
    double turn_arc_shallow_angle_deg_;
    int turn_arc_external_route_min_points_ = 40;
    bool turn_arc_external_route_smoothing_enabled_ = true;
    double turn_arc_external_route_min_angle_deg_ = 20.0;
    double turn_arc_external_route_min_segment_m_ = 600.0;
    double turn_arc_external_route_radius_m_ = 450.0;
    double turn_arc_external_route_max_tangent_m_ = 120.0;
    bool dense_turn_cluster_smoothing_enabled_ = true;
    double dense_turn_cluster_min_angle_deg_ = 12.0;
    double dense_turn_cluster_max_segment_m_ = 500.0;
    int dense_turn_cluster_min_turns_ = 2;
    int dense_turn_cluster_chaikin_iterations_ = 2;
    double dense_turn_cluster_sample_spacing_m_ = 60.0;
    double dense_turn_cluster_speed_mps_ = 3.8;
    bool turn_arc_auto_flyby_enabled_;
    double turn_arc_wheel_over_distance_m_;
    double turn_arc_wheel_over_max_segment_ratio_;
    double heading_cmd_rate_limit_deg_s_;
    double homing_threshold_m_;
    double homing_max_approach_angle_deg_;
    double homing_lookahead_m_;
    bool turn_recovery_gate_enabled_;
    bool turn_recovery_require_cruise_mode_;
    double turn_recovery_max_xte_m_;
    double turn_recovery_max_heading_error_deg_;
    double turn_recovery_max_yaw_rate_deg_s_;
    double turn_recovery_max_cross_track_rate_mps_;
    double turn_recovery_speed_margin_mps_;
    double turn_recovery_speed_ramp_mps2_;
    double turn_segment_max_los_correction_deg_;
    double turn_segment_los_correction_45deg_;
    bool turn_segment_speed_gate_enabled_;
    double turn_segment_speed_cap_mps_;
    double turn_segment_release_xte_m_;
    double turn_segment_release_heading_error_deg_;
    double turn_segment_release_cross_track_rate_mps_;
    double turn_segment_release_yaw_rate_deg_s_;
    bool external_route_turn_preview_enabled_ = true;
    double external_route_turn_preview_distance_m_ = 2500.0;
    double external_route_turn_hold_distance_m_ = 4500.0;
    double external_route_turn_min_cumulative_angle_deg_ = 12.0;
    double external_route_turn_heading_preview_distance_m_ = 900.0;
    double external_route_turn_centerline_distance_m_ = 600.0;
    double external_route_turn_centerline_release_xte_m_ = 18.0;
    double external_route_turn_speed_cap_mps_ = 4.2;
    bool turn_feasibility_preview_enabled_ = true;
    double turn_feasibility_preview_distance_m_ = 1200.0;
    double turn_feasibility_min_cumulative_angle_deg_ = 10.0;
    double turn_feasibility_response_margin_m_ = 180.0;
    bool cruise_speed_recovery_enabled_;
    double cruise_base_speed_mps_;
    double cruise_recovery_target_speed_mps_;
    double cruise_recovery_required_stable_s_;
    double cruise_recovery_ramp_mps2_;
    double cruise_recovery_max_xte_m_;
    double cruise_recovery_max_xte_dot_mps_;
    double cruise_recovery_max_yaw_rate_deg_s_;
    double cruise_fallback_xte_m_;
    double cruise_fallback_xte_dot_mps_;
    double cruise_fallback_yaw_rate_deg_s_;
    bool wind_cruise_speed_relax_enabled_;
    double wind_cruise_relax_min_cross_force_n_;
    double wind_cruise_mid_speed_mps_;
    double wind_cruise_mid_xte_m_;
    double wind_cruise_mid_xte_dot_mps_;
    double wind_cruise_mid_outward_xte_dot_mps_;
    double wind_cruise_mid_yaw_rate_deg_s_;
    double wind_cruise_full_xte_m_;
    double wind_cruise_full_xte_dot_mps_;
    double wind_cruise_full_outward_xte_dot_mps_;
    double wind_cruise_full_stable_s_;
    double kappa_ilos_;
    bool corridor_guidance_enabled_;
    double corridor_half_width_m_;
    double corridor_soft_width_m_;
    double corridor_reacquire_width_m_;
    double corridor_integral_decay_;
    double corridor_beta_decay_;
    bool turn_entry_centerline_enabled_;
    double turn_entry_centerline_distance_m_;
    double turn_entry_centerline_min_angle_deg_;
    double turn_entry_centerline_release_xte_m_;
    bool dynamic_path_attach_current_position_enabled_;
    double dynamic_path_attach_min_distance_m_;
    bool rejoin_speed_gate_enabled_;
    double rejoin_speed_cap_mps_;
    double rejoin_severe_speed_cap_mps_;
    double rejoin_heading_error_deg_;
    double rejoin_severe_heading_error_deg_;
    double rejoin_cross_track_m_;
    double emergency_avoidance_speed_cap_mps_;
    double emergency_avoidance_wheel_over_distance_m_;
    double emergency_avoidance_switch_max_xte_m_;
    bool heading_align_rejoin_enabled_;
    double heading_align_enter_rad_;
    double heading_align_exit_rad_;
    double heading_align_speed_mps_;
    double heading_align_max_speed_mps_;
    double heading_align_reset_xte_m_;
    bool heading_align_active_ = false;
    bool far_xte_rejoin_override_enabled_ = true;
    double far_xte_rejoin_threshold_m_ = 120.0;
    double far_xte_rejoin_release_m_ = 30.0;
    double xte_hard_limit_m_ = 100.0;
    double far_xte_rejoin_speed_cap_mps_ = 3.0;
    double far_xte_rejoin_hard_speed_cap_mps_ = 3.0;
    double far_xte_rejoin_lookahead_m_ = 180.0;
    double far_xte_rejoin_max_approach_angle_deg_ = 18.0;
    double xte_rejoin_release_guard_s_ = 5.0;
    double xte_rejoin_release_guard_max_correction_deg_ = 6.0;
    bool far_xte_rejoin_active_ = false;
    double xte_rejoin_release_guard_until_sec_ = 0.0;
    bool raw_route_rejoin_enabled_ = true;
    double raw_route_rejoin_threshold_m_ = 60.0;
    double raw_route_rejoin_release_m_ = 30.0;
    bool raw_route_rejoin_active_ = false;
    bool wind_rejoin_enabled_ = true;
    double wind_rejoin_force_ref_n_ = 3000.0;
    double wind_rejoin_min_outward_force_n_ = 500.0;
    double wind_rejoin_lookahead_gain_ = 1.0;
    double wind_rejoin_max_factor_ = 2.5;
    double wind_rejoin_far_threshold_m_ = 30.0;
    double wind_rejoin_raw_threshold_m_ = 30.0;
    double wind_rejoin_release_m_ = 25.0;
    double wind_rejoin_min_lookahead_m_ = 45.0;
    double wind_rejoin_speed_cap_mps_ = 4.5;
    double wind_rejoin_stale_timeout_s_ = 2.0;
    bool current_rejoin_enabled_ = true;
    double current_rejoin_force_ref_n_ = 12000.0;
    double current_rejoin_min_outward_force_n_ = 1200.0;
    double current_rejoin_lookahead_gain_ = 1.6;
    double current_rejoin_max_factor_ = 2.5;
    double current_rejoin_far_threshold_m_ = 28.0;
    double current_rejoin_raw_threshold_m_ = 25.0;
    double current_rejoin_release_m_ = 16.0;
    double current_rejoin_min_lookahead_m_ = 40.0;
    double current_rejoin_speed_cap_mps_ = 6.2;
    double current_rejoin_max_approach_angle_deg_ = 28.0;
    double current_rejoin_stale_timeout_s_ = 2.0;
    bool wind_crab_enabled_ = true;
    double wind_crab_force_ref_n_ = 6000.0;
    double wind_crab_min_force_n_ = 500.0;
    double wind_crab_max_angle_deg_ = 6.0;
    double wind_crab_min_speed_mps_ = 3.0;
    double wind_crab_base_scale_ = 0.35;
    bool wind_crab_target_offset_enabled_ = true;
    double wind_crab_target_offset_max_m_ = 12.0;
    double wind_crab_target_offset_min_m_ = 3.0;
    double wind_crab_target_offset_force_ref_n_ = 6000.0;
    double wind_crab_xte_kp_deg_per_m_ = 0.12;
    double wind_crab_xte_rate_kd_deg_per_mps_ = 1.5;
    double wind_crab_feedback_max_angle_deg_ = 4.0;
    bool current_crab_enabled_ = true;
    double current_crab_force_ref_n_ = 15000.0;
    double current_crab_min_force_n_ = 1000.0;
    double current_crab_max_angle_deg_ = 10.0;
    double current_crab_min_speed_mps_ = 3.0;
    double current_crab_base_scale_ = 0.5;
    double current_crab_xte_kp_deg_per_m_ = 0.18;
    double current_crab_xte_rate_kd_deg_per_mps_ = 2.2;
    double current_crab_feedback_max_angle_deg_ = 7.0;
    bool current_cog_rejoin_enabled_ = true;
    double current_cog_rejoin_kp_ = 0.55;
    double current_cog_rejoin_max_angle_deg_ = 7.0;
    double current_cog_rejoin_min_speed_mps_ = 2.0;
    bool current_force_ff_enabled_ = true;
    double current_force_ff_force_ref_n_ = 25000.0;
    double current_force_ff_max_angle_deg_ = 2.5;
    double current_force_ff_min_speed_mps_ = 2.0;
    bool roll_guard_enabled_ = true;
    double roll_guard_limit_deg_ = 10.0;
    double roll_guard_release_deg_ = 7.0;
    double roll_guard_speed_cap_mps_ = 3.0;
    bool roll_guard_active_ = false;
    bool reset_sideslip_on_waypoint_switch_;
    std::vector<std::string> wp_switch_modes_;
    std::vector<double> wp_switch_radius_m_;
    std::vector<double> wp_wheel_over_distance_m_;
    std::vector<double> wp_switch_max_xte_m_;
    std::vector<double> wp_missed_after_distance_m_;
    std::vector<double> wp_switch_max_heading_error_deg_;
    std::vector<double> wp_switch_max_speed_mps_;
    std::vector<double> wp_speed_limit_mps_;
    std::vector<double> wp_lookahead_m_;
    std::vector<double> wp_rejoin_cross_track_m_;
    std::vector<double> wp_gate_blocked_speed_mps_;
    std::vector<std::string> wp_navigation_modes_;
    int current_wp_idx_;
    std::vector<Waypoint> waypoints_;
    std::vector<Waypoint> raw_waypoints_;
    Waypoint path_start_pos_;  // 锁定航线起始点（修复半平面投影bug）
    bool path_from_callback_;  // 标记路径是否来自动态path_callback
    bool wait_for_route_plan_ = true;
    bool has_last_path_signature_ = false;
    std::uint64_t last_path_signature_ = 0;
    size_t last_path_size_ = 0;

    // ALOS参数
    bool use_adaptive_los_;
    double gamma_alos_;
    double beta_hat_;
    rclcpp::Time last_time_;

    // [新增] 状态记录器，替代原本致命的 static 变量
    double integral_e_ = 0.0;
    double prev_e_ = 0.0;  // 过零点检测
    double psi_cmd_prev_ = 0.0;
    bool init_psi_ = true;

    // [G-12] 圆弧过渡状态变量
    bool in_arc_mode_ = false;          // 是否在圆弧过渡模式
    double arc_center_x_ = 0.0;         // 圆弧圆心 x
    double arc_center_y_ = 0.0;         // 圆弧圆心 y
    double arc_start_angle_ = 0.0;     // 圆弧入口角度
    double arc_target_angle_ = 0.0;     // 圆弧目标角度
    double arc_radius_ = 0.0;           // 圆弧半径
    int arc_waypoint_idx_ = -1;        // 当前弧线对应的航点索引
    double turn_dir_ = 1.0;             // 转弯方向：+1=逆时针，-1=顺时针
    double min_turn_radius_ = 0.0;       // 最小转弯半径（用于计算圆弧半径）
    bool dp_mode_active_ = false;       // DP保持模式标志，触发后不再发布指令
    int speed_recovery_segment_idx_ = -1;
    bool speed_recovery_gate_cleared_ = false;
    double speed_recovery_speed_cap_ = 0.0;
    rclcpp::Time speed_recovery_last_time_;
    bool cruise_recovery_gate_cleared_ = false;
    bool cruise_recovery_stable_timer_active_ = false;
    bool corridor_hold_active_ = false;
    bool turn_segment_speed_gate_active_ = false;
    double cruise_speed_cap_mps_ = 0.0;
    rclcpp::Time cruise_recovery_stable_since_;
    rclcpp::Time cruise_recovery_last_time_;

    double dp_hold_x_{0.0};
    double dp_hold_y_{0.0};
    double dp_hold_psi_{0.0};
    double current_cross_track_error_{0.0};
    bool final_dp_latched_{false};
    bool final_dp_overrun_braking_active_{false};
    bool final_dp_overrun_hold_initialized_{false};
    bool final_dp_hold_initialized_{false};
};

#endif // SHIP_GUIDANCE_NODE_HPP
