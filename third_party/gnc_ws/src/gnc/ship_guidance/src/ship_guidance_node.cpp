#include "ship_guidance/ship_guidance_node.hpp"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <limits>
#include <cstdint>

// ========== 辅助函数 ========== //


static std::uint64_t compute_path_signature(const nav_msgs::msg::Path& path)
{
    std::uint64_t hash = 1469598103934665603ull;
    auto mix = [&hash](std::int64_t value) {
        std::uint64_t v = static_cast<std::uint64_t>(value);
        for (int i = 0; i < 8; ++i) {
            hash ^= (v & 0xffu);
            hash *= 1099511628211ull;
            v >>= 8;
        }
    };

    mix(static_cast<std::int64_t>(path.poses.size()));
    for (const auto& pose_stamped : path.poses) {
        const auto& pose = pose_stamped.pose;
        mix(static_cast<std::int64_t>(std::llround(pose.position.x * 100.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.position.y * 100.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.position.z * 1000.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.orientation.x * 1000.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.orientation.y * 1000.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.orientation.z * 1000.0)));
        mix(static_cast<std::int64_t>(std::llround(pose.orientation.w * 1000.0)));
    }
    return hash;
}

static std::string normalize_navigation_mode(std::string mode)
{
    for (char& ch : mode) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch == '-' || ch == ' ') {
            ch = '_';
        }
    }
    return mode;
}

static std::string navigation_mode_from_code(double code_value)
{
    const int code = static_cast<int>(std::llround(code_value));
    switch (code) {
        case 1: return "cruise";
        case 2: return "narrow_channel";
        case 3: return "harbor";
        case 4: return "approach";
        case 5: return "dp_hold";
        case 6: return "emergency_avoidance";
        default: return "";
    }
}

static bool is_dp_navigation_mode(const std::string& mode)
{
    const std::string normalized = normalize_navigation_mode(mode);
    return normalized == "dp_hold" ||
           normalized == "dp" ||
           normalized == "station_keeping";
}

static bool is_emergency_avoidance_mode(const std::string& mode)
{
    const std::string normalized = normalize_navigation_mode(mode);
    return normalized == "emergency_avoidance" ||
           normalized == "emergency_avoid" ||
           normalized == "collision_avoidance" ||
           normalized == "avoidance";
}

static double compute_cross_track_error(double x, double y,
                                          double x1, double y1,
                                          double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double seg_len = std::hypot(dx, dy);
    if (seg_len < 1e-6) return 0.0;

    double ex = dx / seg_len;
    double ey = dy / seg_len;
    double vx = x - x1;
    double vy = y - y1;
    return vy * ex - vx * ey;
}

static double compute_along_track(double x, double y,
                                     double x1, double y1,
                                     double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double seg_len = std::hypot(dx, dy);
    if (seg_len < 1e-6) return 0.0;

    double ex = dx / seg_len;
    double ey = dy / seg_len;
    double vx = x - x1;
    double vy = y - y1;
    return vx * ex + vy * ey;
}

static double compute_path_angle(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double alpha = std::atan2(dy, dx);
    if (alpha < 0) alpha += 2 * M_PI;
    return alpha;
}

static double compute_segment_distance(double x, double y,
                                       double x1, double y1,
                                       double x2, double y2)
{
    double dx = x2 - x1;
    double dy = y2 - y1;
    double seg_len_sq = dx * dx + dy * dy;
    if (seg_len_sq < 1e-12) {
        return std::hypot(x - x1, y - y1);
    }

    double t = ((x - x1) * dx + (y - y1) * dy) / seg_len_sq;
    t = std::clamp(t, 0.0, 1.0);
    double proj_x = x1 + t * dx;
    double proj_y = y1 + t * dy;
    return std::hypot(x - proj_x, y - proj_y);
}

static double normalize_angle_pi(double angle)
{
    while (angle > M_PI) angle -= 2 * M_PI;
    while (angle < -M_PI) angle += 2 * M_PI;
    return angle;
}

// ========== 前瞻减速规划辅助函数 ========== //
// 根据弯道大小，规划安全过弯速度和提前减速距离
static void compute_turn_approach_params(double turn_angle_rad, double max_u,
                                         double turn_speed_15deg,
                                         double turn_speed_45deg,
                                         double turn_speed_90deg,
                                         double turn_speed_180deg,
                                         double turn_no_slowdown_angle_deg,
                                         double turn_slow_down_dist_15deg,
                                         double turn_slow_down_dist_45deg,
                                         double turn_slow_down_dist_90deg,
                                         double turn_slow_down_dist_180deg,
                                         double& target_arrival_speed, double& required_slow_down_dist) {
    double angle_deg = std::abs(turn_angle_rad) * 180.0 / M_PI;
    const double no_slowdown_angle = std::clamp(turn_no_slowdown_angle_deg, 0.0, 15.0);
    const double speed_15 = std::clamp(turn_speed_15deg, 0.1, max_u);
    const double speed_45 = std::clamp(turn_speed_45deg, 0.1, max_u);
    const double speed_90 = std::clamp(turn_speed_90deg, 0.1, speed_45);
    const double speed_180 = std::clamp(turn_speed_180deg, 0.1, speed_90);
    const double dist_15 = std::max(turn_slow_down_dist_15deg, 1.0);
    const double dist_45 = std::max(turn_slow_down_dist_45deg, dist_15);
    const double dist_90 = std::max(turn_slow_down_dist_90deg, dist_45);
    const double dist_180 = std::max(turn_slow_down_dist_180deg, dist_90);

    // 基于船长 (Lpp = 45m) 的实船经验数据：
    // 45° -> 提前50m减速，到达速度 1.2m/s
    // 90° -> 提前70m减速，到达速度 0.9m/s
    // 169° -> 提前105m减速，到达速度 0.4m/s

    if (angle_deg <= no_slowdown_angle) {
        target_arrival_speed = max_u;
        required_slow_down_dist = 0.0;
    } else if (angle_deg <= 15.0) {
        double span = std::max(15.0 - no_slowdown_angle, 1.0);
        double t = (angle_deg - no_slowdown_angle) / span;
        target_arrival_speed = max_u - t * (max_u - speed_15);
        required_slow_down_dist = dist_15;
    } else if (angle_deg <= 45.0) {
        // 15° 到 45° 线性插值
        double t = (angle_deg - 15.0) / 30.0;
        target_arrival_speed = speed_15 - t * (speed_15 - speed_45);
        required_slow_down_dist = dist_15 + t * (dist_45 - dist_15);
    } else if (angle_deg <= 90.0) {
        // 45° 到 90°
        double t = (angle_deg - 45.0) / 45.0;
        target_arrival_speed = speed_45 - t * (speed_45 - speed_90);
        required_slow_down_dist = dist_45 + t * (dist_90 - dist_45);
    } else {
        // 90° 到 180°
        double t = std::min((angle_deg - 90.0) / 79.0, 1.0); // 169-90 = 79
        target_arrival_speed = speed_90 - t * (speed_90 - speed_180);
        required_slow_down_dist = dist_90 + t * (dist_180 - dist_90);
    }
}



// ========== 自适应航向速率函数 (方案B) ========== //
static double compute_adaptive_turn_rate(double delta_psi_rad) {
    const double limit_lower_rad = 15.0 * M_PI / 180.0;  // 15°
    const double limit_upper_rad = 45.0 * M_PI / 180.0; // 45°
    const double rate_base_rad   = 10.0 * M_PI / 180.0; // 10°/s
    const double rate_max_rad    = 30.0 * M_PI / 180.0; // 30°/s

    double abs_err = std::abs(delta_psi_rad);

    // 计算归一化参数 t
    double t = (abs_err - limit_lower_rad) / (limit_upper_rad - limit_lower_rad);
    t = std::clamp(t, 0.0, 1.0);

    // Smoothstep (Hermite 插值) 保证速度和加速度的连续性
    double smooth_factor = t * t * (3.0 - 2.0 * t);

    return rate_base_rad + smooth_factor * (rate_max_rad - rate_base_rad);
}

// ========== 类成员函数实现 ========== //

ShipGuidanceNode::ShipGuidanceNode()
    : Node("ship_guidance_node"),
      odom_received_(false),
      path_start_pos_(Waypoint{0.0, 0.0, -1.0, false, false, 0.0, ""}),
      path_from_callback_(false)
{
    this->declare_parameter("lookahead_distance", 50.0);
    this->declare_parameter("guidance_period_s", 0.5);
    this->declare_parameter("capture_radius", 10.0);
    this->declare_parameter("intermediate_capture_radius", 15.0);
    this->declare_parameter("intermediate_overrun_radius", 150.0);
    this->declare_parameter("max_transit_speed", 3.0);
    this->declare_parameter("minimum_steerage_speed", 2.5);
    this->declare_parameter("cruise_min_speed_mps", 3.8);
    this->declare_parameter("use_adaptive_los", false);
    this->declare_parameter("wait_for_route_plan", true);
    this->declare_parameter("gamma_alos", 1.5);
    // [Task 3] 动态前瞻距离参数
    this->declare_parameter("Lpp", 45.0);                    // 船长 (m)
    this->declare_parameter("delta_min_coeff", 1.5);        // 最小前瞻系数
    this->declare_parameter("gamma_lookahead", 0.5);         // 速度前瞻系数
    this->declare_parameter("slow_down_dist", 50.0);
    this->declare_parameter("final_dp_slow_down_dist", 420.0);
    this->declare_parameter("final_dp_stop_decel_mps2", 0.04);
    this->declare_parameter("final_dp_stop_min_lpp", 8.0);
    this->declare_parameter("final_dp_stop_max_lpp", 20.0);
    this->declare_parameter("final_capture_radius", 20.0);
    this->declare_parameter("final_stop_radius", 20.0);
    this->declare_parameter("final_capture_speed", 0.8);
    this->declare_parameter("final_dp_brake_radius", 60.0);
    this->declare_parameter("final_dp_brake_speed", 0.25);
    this->declare_parameter("final_dp_min_closing_speed_mps", 0.5);
    this->declare_parameter("final_handoff_speed", 0.9);
    this->declare_parameter("final_capture_max_cross_track_m", 20.0);
    this->declare_parameter("final_reacquire_cross_track_m", 15.0);
    this->declare_parameter("final_reacquire_min_speed_mps", 2.0);
    this->declare_parameter("terminal_decel_use_position_error", true);
    this->declare_parameter("final_approach_lookahead_m", 0.0);
    this->declare_parameter("final_approach_use_adaptive_los", true);
    this->declare_parameter("dp_weathervane_heading_enabled", true);
    this->declare_parameter("dp_weathervane_min_env_force_n", 500.0);
    this->declare_parameter("dp_weathervane_stale_timeout_s", 2.0);
    this->declare_parameter("dp_weathervane_strict_handoff_enabled", true);
    this->declare_parameter("dp_weathervane_handoff_max_cross_track_m", 35.0);
    this->declare_parameter("dp_weathervane_handoff_speed_mps", 0.9);
    this->declare_parameter("turn_speed_15deg", 8.0);
    this->declare_parameter("turn_speed_45deg", 1.2);
    this->declare_parameter("turn_speed_90deg", 0.9);
    this->declare_parameter("turn_speed_180deg", 0.4);
    this->declare_parameter("turn_no_slowdown_angle_deg", 0.0);
    this->declare_parameter("turn_slow_down_dist_15deg", 20.0);
    this->declare_parameter("turn_slow_down_dist_45deg", 50.0);
    this->declare_parameter("turn_slow_down_dist_90deg", 70.0);
    this->declare_parameter("turn_slow_down_dist_180deg", 105.0);
    this->declare_parameter("turn_arc_smoothing_enabled", true);
    this->declare_parameter("turn_arc_radius_m", 700.0);
    this->declare_parameter("turn_arc_max_tangent_m", 0.0);
    this->declare_parameter("turn_arc_sample_spacing_m", 80.0);
    this->declare_parameter("turn_arc_min_angle_deg", 10.0);
    this->declare_parameter("turn_arc_speed_mps", 4.5);
    this->declare_parameter("turn_arc_pre_decel_dist_m", 350.0);
    this->declare_parameter("turn_arc_shallow_radius_m", 1200.0);
    this->declare_parameter("turn_arc_shallow_angle_deg", 35.0);
    this->declare_parameter("turn_arc_external_route_min_points", 40);
    this->declare_parameter("turn_arc_external_route_smoothing_enabled", true);
    this->declare_parameter("turn_arc_external_route_min_angle_deg", 20.0);
    this->declare_parameter("turn_arc_external_route_min_segment_m", 600.0);
    this->declare_parameter("turn_arc_external_route_radius_m", 450.0);
    this->declare_parameter("turn_arc_external_route_max_tangent_m", 0.0);
    this->declare_parameter("dense_turn_cluster_smoothing_enabled", true);
    this->declare_parameter("dense_turn_cluster_min_angle_deg", 12.0);
    this->declare_parameter("dense_turn_cluster_max_segment_m", 500.0);
    this->declare_parameter("dense_turn_cluster_min_turns", 2);
    this->declare_parameter("dense_turn_cluster_chaikin_iterations", 2);
    this->declare_parameter("dense_turn_cluster_sample_spacing_m", 60.0);
    this->declare_parameter("dense_turn_cluster_speed_mps", 3.8);
    this->declare_parameter("turn_arc_auto_flyby_enabled", true);
    this->declare_parameter("turn_arc_wheel_over_distance_m", 180.0);
    this->declare_parameter("turn_arc_wheel_over_max_segment_ratio", 0.45);
    this->declare_parameter("heading_cmd_rate_limit_deg_s", 360.0);
    this->declare_parameter("homing_threshold_m", 90.0);
    this->declare_parameter("homing_max_approach_angle_deg", 45.0);
    this->declare_parameter("homing_lookahead_m", 0.0);
    this->declare_parameter("turn_recovery_gate_enabled", false);
    this->declare_parameter("turn_recovery_require_cruise_mode", true);
    this->declare_parameter("turn_recovery_max_xte_m", 35.0);
    this->declare_parameter("turn_recovery_max_heading_error_deg", 8.0);
    this->declare_parameter("turn_recovery_max_yaw_rate_deg_s", 0.5);
    this->declare_parameter("turn_recovery_max_cross_track_rate_mps", 0.30);
    this->declare_parameter("turn_recovery_speed_margin_mps", 0.5);
    this->declare_parameter("turn_recovery_speed_ramp_mps2", 0.30);
    this->declare_parameter("turn_segment_max_los_correction_deg", 3.0);
    this->declare_parameter("turn_segment_los_correction_45deg", 7.0);
    this->declare_parameter("turn_segment_speed_gate_enabled", true);
    this->declare_parameter("turn_segment_speed_cap_mps", 3.9);
    this->declare_parameter("turn_segment_release_xte_m", 30.0);
    this->declare_parameter("turn_segment_release_heading_error_deg", 4.0);
    this->declare_parameter("turn_segment_release_cross_track_rate_mps", 0.30);
    this->declare_parameter("turn_segment_release_yaw_rate_deg_s", 0.8);
    this->declare_parameter("external_route_turn_preview_enabled", true);
    this->declare_parameter("external_route_turn_preview_distance_m", 2500.0);
    this->declare_parameter("external_route_turn_hold_distance_m", 4500.0);
    this->declare_parameter("external_route_turn_min_cumulative_angle_deg", 12.0);
    this->declare_parameter("external_route_turn_heading_preview_distance_m", 900.0);
    this->declare_parameter("external_route_turn_centerline_distance_m", 600.0);
    this->declare_parameter("external_route_turn_centerline_release_xte_m", 18.0);
    this->declare_parameter("external_route_turn_speed_cap_mps", 4.2);
    this->declare_parameter("turn_feasibility_preview_enabled", true);
    this->declare_parameter("turn_feasibility_preview_distance_m", 1200.0);
    this->declare_parameter("turn_feasibility_min_cumulative_angle_deg", 10.0);
    this->declare_parameter("turn_feasibility_response_margin_m", 180.0);
    this->declare_parameter("cruise_speed_recovery_enabled", false);
    this->declare_parameter("cruise_base_speed_mps", 6.0);
    this->declare_parameter("cruise_recovery_target_speed_mps", 8.0);
    this->declare_parameter("cruise_recovery_required_stable_s", 30.0);
    this->declare_parameter("cruise_recovery_ramp_mps2", 0.04);
    this->declare_parameter("cruise_recovery_max_xte_m", 5.0);
    this->declare_parameter("cruise_recovery_max_xte_dot_mps", 0.05);
    this->declare_parameter("cruise_recovery_max_yaw_rate_deg_s", 0.3);
    this->declare_parameter("cruise_fallback_xte_m", 15.0);
    this->declare_parameter("cruise_fallback_xte_dot_mps", 0.15);
    this->declare_parameter("cruise_fallback_yaw_rate_deg_s", 1.0);
    this->declare_parameter("wind_cruise_speed_relax_enabled", true);
    this->declare_parameter("wind_cruise_relax_min_cross_force_n", 800.0);
    this->declare_parameter("wind_cruise_mid_speed_mps", 7.2);
    this->declare_parameter("wind_cruise_mid_xte_m", 45.0);
    this->declare_parameter("wind_cruise_mid_xte_dot_mps", 0.55);
    this->declare_parameter("wind_cruise_mid_outward_xte_dot_mps", 0.12);
    this->declare_parameter("wind_cruise_mid_yaw_rate_deg_s", 0.8);
    this->declare_parameter("wind_cruise_full_xte_m", 25.0);
    this->declare_parameter("wind_cruise_full_xte_dot_mps", 0.18);
    this->declare_parameter("wind_cruise_full_outward_xte_dot_mps", 0.03);
    this->declare_parameter("wind_cruise_full_stable_s", 4.0);
    this->declare_parameter("kappa_ilos", 1.0);
    this->declare_parameter("corridor_guidance_enabled", false);
    this->declare_parameter("corridor_half_width_m", 30.0);
    this->declare_parameter("corridor_soft_width_m", 60.0);
    this->declare_parameter("corridor_reacquire_width_m", 90.0);
    this->declare_parameter("corridor_integral_decay", 0.85);
    this->declare_parameter("corridor_beta_decay", 0.92);
    this->declare_parameter("turn_entry_centerline_enabled", true);
    this->declare_parameter("turn_entry_centerline_distance_m", 1200.0);
    this->declare_parameter("turn_entry_centerline_min_angle_deg", 10.0);
    this->declare_parameter("turn_entry_centerline_release_xte_m", 8.0);
    this->declare_parameter("dynamic_path_attach_current_position_enabled", true);
    this->declare_parameter("dynamic_path_attach_min_distance_m", 20.0);
    this->declare_parameter("rejoin_speed_gate_enabled", true);
    this->declare_parameter("rejoin_speed_cap_mps", 3.0);
    this->declare_parameter("rejoin_severe_speed_cap_mps", 1.5);
    this->declare_parameter("rejoin_heading_error_deg", 15.0);
    this->declare_parameter("rejoin_severe_heading_error_deg", 30.0);
    this->declare_parameter("rejoin_cross_track_m", 60.0);
    this->declare_parameter("emergency_avoidance_speed_cap_mps", 3.2);
    this->declare_parameter("emergency_avoidance_wheel_over_distance_m", 120.0);
    this->declare_parameter("emergency_avoidance_switch_max_xte_m", 90.0);
    this->declare_parameter("heading_align_rejoin_enabled", true);
    this->declare_parameter("heading_align_enter_deg", 30.0);
    this->declare_parameter("heading_align_exit_deg", 8.0);
    this->declare_parameter("heading_align_speed_mps", 0.6);
    this->declare_parameter("heading_align_max_speed_mps", 2.0);
    this->declare_parameter("heading_align_reset_xte_m", 45.0);
    this->declare_parameter("far_xte_rejoin_override_enabled", true);
    this->declare_parameter("far_xte_rejoin_threshold_m", 65.0);
    this->declare_parameter("far_xte_rejoin_release_m", 30.0);
    this->declare_parameter("xte_hard_limit_m", 100.0);
    this->declare_parameter("far_xte_rejoin_speed_cap_mps", 3.0);
    this->declare_parameter("far_xte_rejoin_hard_speed_cap_mps", 3.0);
    this->declare_parameter("far_xte_rejoin_lookahead_m", 180.0);
    this->declare_parameter("far_xte_rejoin_max_approach_angle_deg", 18.0);
    this->declare_parameter("xte_rejoin_release_guard_s", 5.0);
    this->declare_parameter("xte_rejoin_release_guard_max_correction_deg", 6.0);
    this->declare_parameter("raw_route_rejoin_enabled", true);
    this->declare_parameter("raw_route_rejoin_threshold_m", 60.0);
    this->declare_parameter("raw_route_rejoin_release_m", 30.0);
    this->declare_parameter("wind_rejoin_enabled", true);
    this->declare_parameter("wind_rejoin_force_ref_n", 3000.0);
    this->declare_parameter("wind_rejoin_min_outward_force_n", 500.0);
    this->declare_parameter("wind_rejoin_lookahead_gain", 1.0);
    this->declare_parameter("wind_rejoin_max_factor", 2.5);
    this->declare_parameter("wind_rejoin_far_threshold_m", 30.0);
    this->declare_parameter("wind_rejoin_raw_threshold_m", 30.0);
    this->declare_parameter("wind_rejoin_release_m", 25.0);
    this->declare_parameter("wind_rejoin_min_lookahead_m", 45.0);
    this->declare_parameter("wind_rejoin_speed_cap_mps", 4.5);
    this->declare_parameter("wind_rejoin_stale_timeout_s", 2.0);
    this->declare_parameter("current_rejoin_enabled", true);
    this->declare_parameter("current_rejoin_force_ref_n", 12000.0);
    this->declare_parameter("current_rejoin_min_outward_force_n", 1200.0);
    this->declare_parameter("current_rejoin_lookahead_gain", 1.6);
    this->declare_parameter("current_rejoin_max_factor", 2.5);
    this->declare_parameter("current_rejoin_far_threshold_m", 28.0);
    this->declare_parameter("current_rejoin_raw_threshold_m", 25.0);
    this->declare_parameter("current_rejoin_release_m", 16.0);
    this->declare_parameter("current_rejoin_min_lookahead_m", 40.0);
    this->declare_parameter("current_rejoin_speed_cap_mps", 6.2);
    this->declare_parameter("current_rejoin_max_approach_angle_deg", 28.0);
    this->declare_parameter("current_rejoin_stale_timeout_s", 2.0);
    this->declare_parameter("wind_crab_enabled", true);
    this->declare_parameter("wind_crab_force_ref_n", 6000.0);
    this->declare_parameter("wind_crab_min_force_n", 500.0);
    this->declare_parameter("wind_crab_max_angle_deg", 6.0);
    this->declare_parameter("wind_crab_min_speed_mps", 3.0);
    this->declare_parameter("wind_crab_base_scale", 0.35);
    this->declare_parameter("wind_crab_target_offset_enabled", true);
    this->declare_parameter("wind_crab_target_offset_max_m", 12.0);
    this->declare_parameter("wind_crab_target_offset_min_m", 3.0);
    this->declare_parameter("wind_crab_target_offset_force_ref_n", 6000.0);
    this->declare_parameter("wind_crab_xte_kp_deg_per_m", 0.12);
    this->declare_parameter("wind_crab_xte_rate_kd_deg_per_mps", 1.5);
    this->declare_parameter("wind_crab_feedback_max_angle_deg", 4.0);
    this->declare_parameter("current_crab_enabled", true);
    this->declare_parameter("current_crab_force_ref_n", 15000.0);
    this->declare_parameter("current_crab_min_force_n", 1000.0);
    this->declare_parameter("current_crab_max_angle_deg", 10.0);
    this->declare_parameter("current_crab_min_speed_mps", 3.0);
    this->declare_parameter("current_crab_base_scale", 0.5);
    this->declare_parameter("current_crab_xte_kp_deg_per_m", 0.18);
    this->declare_parameter("current_crab_xte_rate_kd_deg_per_mps", 2.2);
    this->declare_parameter("current_crab_feedback_max_angle_deg", 7.0);
    this->declare_parameter("current_cog_rejoin_enabled", true);
    this->declare_parameter("current_cog_rejoin_kp", 0.55);
    this->declare_parameter("current_cog_rejoin_max_angle_deg", 7.0);
    this->declare_parameter("current_cog_rejoin_min_speed_mps", 2.0);
    this->declare_parameter("current_force_ff_enabled", true);
    this->declare_parameter("current_force_ff_force_ref_n", 25000.0);
    this->declare_parameter("current_force_ff_max_angle_deg", 2.5);
    this->declare_parameter("current_force_ff_min_speed_mps", 2.0);
    this->declare_parameter("roll_guard_enabled", true);
    this->declare_parameter("roll_guard_limit_deg", 10.0);
    this->declare_parameter("roll_guard_release_deg", 7.0);
    this->declare_parameter("roll_guard_speed_cap_mps", 3.0);
    this->declare_parameter("reset_sideslip_on_waypoint_switch", true);
    this->declare_parameter("wp_switch_modes", std::vector<std::string>{});
    this->declare_parameter("wp_switch_radius_m", std::vector<double>{});
    this->declare_parameter("wp_wheel_over_distance_m", std::vector<double>{});
    this->declare_parameter("wp_switch_max_xte_m", std::vector<double>{});
    this->declare_parameter("wp_missed_after_distance_m", std::vector<double>{});
    this->declare_parameter("wp_switch_max_heading_error_deg", std::vector<double>{});
    this->declare_parameter("wp_switch_max_speed_mps", std::vector<double>{});
    this->declare_parameter("wp_speed_limit_mps", std::vector<double>{});
    this->declare_parameter("wp_lookahead_m", std::vector<double>{});
    this->declare_parameter("wp_rejoin_cross_track_m", std::vector<double>{});
    this->declare_parameter("wp_gate_blocked_speed_mps", std::vector<double>{});
    this->declare_parameter("wp_navigation_modes", std::vector<std::string>{});

    this->declare_parameter("wp_x", std::vector<double>{0.0, 200.0});
    this->declare_parameter("wp_y", std::vector<double>{0.0, 200.0});

    get_parameters();

    current_wp_idx_ = (waypoints_.size() > 1) ? 1 : 0;
    if (wait_for_route_plan_) {
        waypoints_.clear();
        current_wp_idx_ = 0;
        path_from_callback_ = false;
    }
    beta_hat_ = 0.0;
    last_time_ = this->now();
    speed_recovery_last_time_ = this->now();
    cruise_recovery_stable_since_ = this->now();
    cruise_recovery_last_time_ = this->now();
    cruise_speed_cap_mps_ = cruise_base_speed_mps_;

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&ShipGuidanceNode::odom_callback, this, std::placeholders::_1));

    env_load_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/total_load", 10,
        std::bind(&ShipGuidanceNode::env_load_callback, this, std::placeholders::_1));
    current_load_sub_ = this->create_subscription<geometry_msgs::msg::WrenchStamped>(
        "/env/current_load", 10,
        std::bind(&ShipGuidanceNode::current_load_callback, this, std::placeholders::_1));

    auto path_qos = rclcpp::QoS(10).transient_local().reliable();
    path_sub_ = this->create_subscription<nav_msgs::msg::Path>(
        "/ship/waypoints", path_qos,
        std::bind(&ShipGuidanceNode::path_callback, this, std::placeholders::_1));

    // 运行时 reset：orchestrator 经 gnc_bridge 发 /ship/dynamics_reset。
    reset_sub_ = this->create_subscription<ship_interfaces::msg::ShipReset>(
        "/ship/dynamics_reset", 10,
        std::bind(&ShipGuidanceNode::reset_callback, this, std::placeholders::_1));

    heading_setpoint_pub_ = this->create_publisher<std_msgs::msg::Float64>("/control/heading_setpoint", 10);
    target_speed_pub_ = this->create_publisher<std_msgs::msg::Float64>("/control/speed_setpoint", 10);
    target_pose_pub_ = this->create_publisher<geometry_msgs::msg::PoseStamped>("/target_pose", 10);
    smoothed_waypoints_pub_ = this->create_publisher<nav_msgs::msg::Path>("/gnc/smoothed_waypoints", path_qos);

    timer_ = this->create_wall_timer(
        std::chrono::milliseconds(static_cast<int>(guidance_period_s_ * 1000.0)),
        std::bind(&ShipGuidanceNode::control_loop, this));

    RCLCPP_INFO(this->get_logger(), "船舶制导中枢已启动，采用 ILOS 导引律，周期 %.0f ms.", guidance_period_s_ * 1000.0);
}

void ShipGuidanceNode::get_parameters()
{
    guidance_period_s_ = std::clamp(this->get_parameter("guidance_period_s").as_double(), 0.02, 5.0);
    delta_lookahead_ = this->get_parameter("lookahead_distance").as_double();
    capture_radius_  = this->get_parameter("capture_radius").as_double();
    intermediate_capture_radius_ = this->get_parameter("intermediate_capture_radius").as_double();
    intermediate_overrun_radius_ = this->get_parameter("intermediate_overrun_radius").as_double();
    max_speed_       = this->get_parameter("max_transit_speed").as_double();
    minimum_steerage_speed_ = this->get_parameter("minimum_steerage_speed").as_double();
    cruise_min_speed_mps_ = this->get_parameter("cruise_min_speed_mps").as_double();
    use_adaptive_los_= this->get_parameter("use_adaptive_los").as_bool();
    wait_for_route_plan_ = this->get_parameter("wait_for_route_plan").as_bool();
    gamma_alos_      = this->get_parameter("gamma_alos").as_double();
    // [Task 3] 动态前瞻距离参数
    Lpp_             = this->get_parameter("Lpp").as_double();
    delta_min_coeff_ = this->get_parameter("delta_min_coeff").as_double();
    gamma_lookahead_ = this->get_parameter("gamma_lookahead").as_double();
    slow_down_dist_  = this->get_parameter("slow_down_dist").as_double();
    final_dp_slow_down_dist_ = this->get_parameter("final_dp_slow_down_dist").as_double();
    final_dp_stop_decel_mps2_ = std::max(0.01, this->get_parameter("final_dp_stop_decel_mps2").as_double());
    final_dp_stop_min_lpp_ = std::max(1.0, this->get_parameter("final_dp_stop_min_lpp").as_double());
    final_dp_stop_max_lpp_ = std::max(final_dp_stop_min_lpp_, this->get_parameter("final_dp_stop_max_lpp").as_double());
    final_capture_radius_ = this->get_parameter("final_capture_radius").as_double();
    final_stop_radius_ = this->get_parameter("final_stop_radius").as_double();
    final_capture_speed_ = this->get_parameter("final_capture_speed").as_double();
    final_dp_brake_radius_ = this->get_parameter("final_dp_brake_radius").as_double();
    final_dp_brake_speed_ = this->get_parameter("final_dp_brake_speed").as_double();
    final_dp_min_closing_speed_mps_ =
        this->get_parameter("final_dp_min_closing_speed_mps").as_double();
    final_handoff_speed_ = this->get_parameter("final_handoff_speed").as_double();
    final_capture_max_cross_track_m_ = this->get_parameter("final_capture_max_cross_track_m").as_double();
    final_reacquire_cross_track_m_ = this->get_parameter("final_reacquire_cross_track_m").as_double();
    final_reacquire_min_speed_mps_ = this->get_parameter("final_reacquire_min_speed_mps").as_double();
    terminal_decel_use_position_error_ = this->get_parameter("terminal_decel_use_position_error").as_bool();
    final_approach_lookahead_m_ = this->get_parameter("final_approach_lookahead_m").as_double();
    final_approach_use_adaptive_los_ = this->get_parameter("final_approach_use_adaptive_los").as_bool();
    minimum_steerage_speed_ = std::clamp(minimum_steerage_speed_, 0.1, max_speed_);
    cruise_min_speed_mps_ = std::clamp(
        cruise_min_speed_mps_,
        minimum_steerage_speed_,
        std::max(max_speed_, minimum_steerage_speed_));
    dp_weathervane_heading_enabled_ =
        this->get_parameter("dp_weathervane_heading_enabled").as_bool();
    dp_weathervane_min_env_force_n_ = std::max(
        0.0, this->get_parameter("dp_weathervane_min_env_force_n").as_double());
    dp_weathervane_stale_timeout_s_ = std::max(
        0.1, this->get_parameter("dp_weathervane_stale_timeout_s").as_double());
    dp_weathervane_strict_handoff_enabled_ =
        this->get_parameter("dp_weathervane_strict_handoff_enabled").as_bool();
    dp_weathervane_handoff_max_cross_track_m_ = std::max(
        1.0, this->get_parameter("dp_weathervane_handoff_max_cross_track_m").as_double());
    dp_weathervane_handoff_speed_mps_ = std::max(
        0.1, this->get_parameter("dp_weathervane_handoff_speed_mps").as_double());
    final_dp_slow_down_dist_ = std::max(final_dp_slow_down_dist_, final_capture_radius_);
    final_dp_brake_radius_ = std::max(final_dp_brake_radius_, final_stop_radius_);
    final_dp_brake_speed_ = std::clamp(final_dp_brake_speed_, 0.0, std::max(final_capture_speed_, 0.1));
    final_dp_min_closing_speed_mps_ = std::clamp(
        final_dp_min_closing_speed_mps_,
        std::max(final_dp_brake_speed_, 0.1),
        std::max(final_capture_speed_, 0.1));
    turn_speed_15deg_ = this->get_parameter("turn_speed_15deg").as_double();
    turn_speed_45deg_ = this->get_parameter("turn_speed_45deg").as_double();
    turn_speed_90deg_ = this->get_parameter("turn_speed_90deg").as_double();
    turn_speed_180deg_ = this->get_parameter("turn_speed_180deg").as_double();
    turn_no_slowdown_angle_deg_ = this->get_parameter("turn_no_slowdown_angle_deg").as_double();
    turn_slow_down_dist_15deg_ = this->get_parameter("turn_slow_down_dist_15deg").as_double();
    turn_slow_down_dist_45deg_ = this->get_parameter("turn_slow_down_dist_45deg").as_double();
    turn_slow_down_dist_90deg_ = this->get_parameter("turn_slow_down_dist_90deg").as_double();
    turn_slow_down_dist_180deg_ = this->get_parameter("turn_slow_down_dist_180deg").as_double();
    turn_arc_smoothing_enabled_ = this->get_parameter("turn_arc_smoothing_enabled").as_bool();
    turn_arc_radius_m_ = std::max(50.0, this->get_parameter("turn_arc_radius_m").as_double());
    turn_arc_max_tangent_m_ = std::max(0.0, this->get_parameter("turn_arc_max_tangent_m").as_double());
    turn_arc_sample_spacing_m_ = std::clamp(
        this->get_parameter("turn_arc_sample_spacing_m").as_double(), 20.0, 300.0);
    turn_arc_min_angle_deg_ = std::clamp(
        this->get_parameter("turn_arc_min_angle_deg").as_double(), 1.0, 90.0);
    turn_arc_speed_mps_ = std::clamp(
        this->get_parameter("turn_arc_speed_mps").as_double(), 0.5, std::max(max_speed_, 0.5));
    turn_arc_pre_decel_dist_m_ = std::max(0.0, this->get_parameter("turn_arc_pre_decel_dist_m").as_double());
    turn_arc_shallow_radius_m_ = std::max(
        turn_arc_radius_m_, this->get_parameter("turn_arc_shallow_radius_m").as_double());
    turn_arc_shallow_angle_deg_ = std::clamp(
        this->get_parameter("turn_arc_shallow_angle_deg").as_double(),
        turn_arc_min_angle_deg_,
        60.0);
    turn_arc_external_route_min_points_ = std::max(
        0, static_cast<int>(
            this->get_parameter("turn_arc_external_route_min_points").as_int()));
    turn_arc_external_route_smoothing_enabled_ =
        this->get_parameter("turn_arc_external_route_smoothing_enabled").as_bool();
    turn_arc_external_route_min_angle_deg_ = std::clamp(
        this->get_parameter("turn_arc_external_route_min_angle_deg").as_double(),
        turn_arc_min_angle_deg_,
        90.0);
    turn_arc_external_route_min_segment_m_ = std::max(
        Lpp_, this->get_parameter("turn_arc_external_route_min_segment_m").as_double());
    turn_arc_external_route_radius_m_ = std::max(
        50.0, this->get_parameter("turn_arc_external_route_radius_m").as_double());
    turn_arc_external_route_max_tangent_m_ = std::max(
        0.0, this->get_parameter("turn_arc_external_route_max_tangent_m").as_double());
    dense_turn_cluster_smoothing_enabled_ =
        this->get_parameter("dense_turn_cluster_smoothing_enabled").as_bool();
    dense_turn_cluster_min_angle_deg_ = std::clamp(
        this->get_parameter("dense_turn_cluster_min_angle_deg").as_double(),
        1.0,
        90.0);
    dense_turn_cluster_max_segment_m_ = std::max(
        Lpp_, this->get_parameter("dense_turn_cluster_max_segment_m").as_double());
    dense_turn_cluster_min_turns_ = std::max(
        2, static_cast<int>(this->get_parameter("dense_turn_cluster_min_turns").as_int()));
    dense_turn_cluster_chaikin_iterations_ = std::clamp(
        static_cast<int>(this->get_parameter("dense_turn_cluster_chaikin_iterations").as_int()),
        1,
        4);
    dense_turn_cluster_sample_spacing_m_ = std::clamp(
        this->get_parameter("dense_turn_cluster_sample_spacing_m").as_double(),
        20.0,
        200.0);
    dense_turn_cluster_speed_mps_ = std::clamp(
        this->get_parameter("dense_turn_cluster_speed_mps").as_double(),
        0.5,
        std::max(max_speed_, 0.5));
    turn_arc_auto_flyby_enabled_ =
        this->get_parameter("turn_arc_auto_flyby_enabled").as_bool();
    turn_arc_wheel_over_distance_m_ = std::max(
        0.0, this->get_parameter("turn_arc_wheel_over_distance_m").as_double());
    turn_arc_wheel_over_max_segment_ratio_ = std::clamp(
        this->get_parameter("turn_arc_wheel_over_max_segment_ratio").as_double(),
        0.05,
        0.80);
    heading_cmd_rate_limit_deg_s_ = this->get_parameter("heading_cmd_rate_limit_deg_s").as_double();
    homing_threshold_m_ = this->get_parameter("homing_threshold_m").as_double();
    homing_max_approach_angle_deg_ = this->get_parameter("homing_max_approach_angle_deg").as_double();
    homing_lookahead_m_ = this->get_parameter("homing_lookahead_m").as_double();
    turn_recovery_gate_enabled_ = this->get_parameter("turn_recovery_gate_enabled").as_bool();
    turn_recovery_require_cruise_mode_ = this->get_parameter("turn_recovery_require_cruise_mode").as_bool();
    turn_recovery_max_xte_m_ = this->get_parameter("turn_recovery_max_xte_m").as_double();
    turn_recovery_max_heading_error_deg_ = this->get_parameter("turn_recovery_max_heading_error_deg").as_double();
    turn_recovery_max_yaw_rate_deg_s_ = this->get_parameter("turn_recovery_max_yaw_rate_deg_s").as_double();
    turn_recovery_max_cross_track_rate_mps_ = this->get_parameter("turn_recovery_max_cross_track_rate_mps").as_double();
    turn_recovery_speed_margin_mps_ = this->get_parameter("turn_recovery_speed_margin_mps").as_double();
    turn_recovery_speed_ramp_mps2_ = this->get_parameter("turn_recovery_speed_ramp_mps2").as_double();
    turn_segment_max_los_correction_deg_ = std::clamp(
        this->get_parameter("turn_segment_max_los_correction_deg").as_double(),
        0.0,
        30.0);
    turn_segment_los_correction_45deg_ = std::clamp(
        this->get_parameter("turn_segment_los_correction_45deg").as_double(),
        turn_segment_max_los_correction_deg_,
        30.0);
    turn_segment_speed_gate_enabled_ =
        this->get_parameter("turn_segment_speed_gate_enabled").as_bool();
    turn_segment_speed_cap_mps_ = std::clamp(
        this->get_parameter("turn_segment_speed_cap_mps").as_double(),
        0.1,
        std::max(max_speed_, 0.1));
    turn_segment_release_xte_m_ = std::max(
        0.0, this->get_parameter("turn_segment_release_xte_m").as_double());
    turn_segment_release_heading_error_deg_ = std::max(
        0.0, this->get_parameter("turn_segment_release_heading_error_deg").as_double());
    turn_segment_release_cross_track_rate_mps_ = std::max(
        0.0, this->get_parameter("turn_segment_release_cross_track_rate_mps").as_double());
    turn_segment_release_yaw_rate_deg_s_ = std::max(
        0.0, this->get_parameter("turn_segment_release_yaw_rate_deg_s").as_double());
    external_route_turn_preview_enabled_ =
        this->get_parameter("external_route_turn_preview_enabled").as_bool();
    external_route_turn_preview_distance_m_ = std::max(
        Lpp_, this->get_parameter("external_route_turn_preview_distance_m").as_double());
    external_route_turn_hold_distance_m_ = std::max(
        Lpp_, this->get_parameter("external_route_turn_hold_distance_m").as_double());
    external_route_turn_min_cumulative_angle_deg_ = std::clamp(
        this->get_parameter("external_route_turn_min_cumulative_angle_deg").as_double(),
        1.0,
        90.0);
    external_route_turn_heading_preview_distance_m_ = std::max(
        Lpp_, this->get_parameter("external_route_turn_heading_preview_distance_m").as_double());
    external_route_turn_centerline_distance_m_ = std::max(
        Lpp_, this->get_parameter("external_route_turn_centerline_distance_m").as_double());
    external_route_turn_centerline_release_xte_m_ = std::max(
        0.0, this->get_parameter("external_route_turn_centerline_release_xte_m").as_double());
    external_route_turn_speed_cap_mps_ = std::clamp(
        this->get_parameter("external_route_turn_speed_cap_mps").as_double(),
        minimum_steerage_speed_,
        std::max(max_speed_, minimum_steerage_speed_));
    turn_feasibility_preview_enabled_ =
        this->get_parameter("turn_feasibility_preview_enabled").as_bool();
    turn_feasibility_preview_distance_m_ = std::clamp(
        this->get_parameter("turn_feasibility_preview_distance_m").as_double(),
        Lpp_,
        5000.0);
    turn_feasibility_min_cumulative_angle_deg_ = std::clamp(
        this->get_parameter("turn_feasibility_min_cumulative_angle_deg").as_double(),
        1.0,
        90.0);
    turn_feasibility_response_margin_m_ = std::clamp(
        this->get_parameter("turn_feasibility_response_margin_m").as_double(),
        0.0,
        2000.0);
    cruise_speed_recovery_enabled_ = this->get_parameter("cruise_speed_recovery_enabled").as_bool();
    cruise_base_speed_mps_ = this->get_parameter("cruise_base_speed_mps").as_double();
    cruise_recovery_target_speed_mps_ = this->get_parameter("cruise_recovery_target_speed_mps").as_double();
    cruise_recovery_required_stable_s_ = this->get_parameter("cruise_recovery_required_stable_s").as_double();
    cruise_recovery_ramp_mps2_ = this->get_parameter("cruise_recovery_ramp_mps2").as_double();
    cruise_recovery_max_xte_m_ = this->get_parameter("cruise_recovery_max_xte_m").as_double();
    cruise_recovery_max_xte_dot_mps_ = this->get_parameter("cruise_recovery_max_xte_dot_mps").as_double();
    cruise_recovery_max_yaw_rate_deg_s_ = this->get_parameter("cruise_recovery_max_yaw_rate_deg_s").as_double();
    cruise_fallback_xte_m_ = this->get_parameter("cruise_fallback_xte_m").as_double();
    cruise_fallback_xte_dot_mps_ = this->get_parameter("cruise_fallback_xte_dot_mps").as_double();
    cruise_fallback_yaw_rate_deg_s_ = this->get_parameter("cruise_fallback_yaw_rate_deg_s").as_double();
    wind_cruise_speed_relax_enabled_ = this->get_parameter("wind_cruise_speed_relax_enabled").as_bool();
    wind_cruise_relax_min_cross_force_n_ = std::max(
        0.0, this->get_parameter("wind_cruise_relax_min_cross_force_n").as_double());
    wind_cruise_mid_speed_mps_ = this->get_parameter("wind_cruise_mid_speed_mps").as_double();
    wind_cruise_mid_xte_m_ = std::max(
        0.0, this->get_parameter("wind_cruise_mid_xte_m").as_double());
    wind_cruise_mid_xte_dot_mps_ = std::max(
        0.0, this->get_parameter("wind_cruise_mid_xte_dot_mps").as_double());
    wind_cruise_mid_outward_xte_dot_mps_ = std::max(
        0.0, this->get_parameter("wind_cruise_mid_outward_xte_dot_mps").as_double());
    wind_cruise_mid_yaw_rate_deg_s_ = std::max(
        0.0, this->get_parameter("wind_cruise_mid_yaw_rate_deg_s").as_double());
    wind_cruise_full_xte_m_ = std::max(
        0.0, this->get_parameter("wind_cruise_full_xte_m").as_double());
    wind_cruise_full_xte_dot_mps_ = std::max(
        0.0, this->get_parameter("wind_cruise_full_xte_dot_mps").as_double());
    wind_cruise_full_outward_xte_dot_mps_ = std::max(
        0.0, this->get_parameter("wind_cruise_full_outward_xte_dot_mps").as_double());
    wind_cruise_full_stable_s_ = std::max(
        0.0, this->get_parameter("wind_cruise_full_stable_s").as_double());
    cruise_base_speed_mps_ = std::clamp(cruise_base_speed_mps_, 0.1, std::max(max_speed_, 0.1));
    cruise_recovery_target_speed_mps_ = std::clamp(cruise_recovery_target_speed_mps_, cruise_base_speed_mps_, std::max(max_speed_, cruise_base_speed_mps_));
    wind_cruise_mid_speed_mps_ = std::clamp(
        wind_cruise_mid_speed_mps_,
        cruise_base_speed_mps_,
        cruise_recovery_target_speed_mps_);
    if (cruise_speed_cap_mps_ <= 0.0) {
        cruise_speed_cap_mps_ = cruise_base_speed_mps_;
    }
    kappa_ilos_      = this->get_parameter("kappa_ilos").as_double();
    corridor_guidance_enabled_ = this->get_parameter("corridor_guidance_enabled").as_bool();
    corridor_half_width_m_ = std::max(0.0, this->get_parameter("corridor_half_width_m").as_double());
    corridor_soft_width_m_ = std::max(corridor_half_width_m_, this->get_parameter("corridor_soft_width_m").as_double());
    corridor_reacquire_width_m_ = std::max(corridor_soft_width_m_, this->get_parameter("corridor_reacquire_width_m").as_double());
    corridor_integral_decay_ = std::clamp(this->get_parameter("corridor_integral_decay").as_double(), 0.0, 1.0);
    corridor_beta_decay_ = std::clamp(this->get_parameter("corridor_beta_decay").as_double(), 0.0, 1.0);
    turn_entry_centerline_enabled_ = this->get_parameter("turn_entry_centerline_enabled").as_bool();
    turn_entry_centerline_distance_m_ = std::max(
        0.0, this->get_parameter("turn_entry_centerline_distance_m").as_double());
    turn_entry_centerline_min_angle_deg_ = std::clamp(
        this->get_parameter("turn_entry_centerline_min_angle_deg").as_double(),
        turn_arc_min_angle_deg_,
        90.0);
    turn_entry_centerline_release_xte_m_ = std::max(
        0.0, this->get_parameter("turn_entry_centerline_release_xte_m").as_double());
    dynamic_path_attach_current_position_enabled_ = this->get_parameter("dynamic_path_attach_current_position_enabled").as_bool();
    dynamic_path_attach_min_distance_m_ = std::max(0.0, this->get_parameter("dynamic_path_attach_min_distance_m").as_double());
    rejoin_speed_gate_enabled_ = this->get_parameter("rejoin_speed_gate_enabled").as_bool();
    rejoin_speed_cap_mps_ = std::max(0.1, this->get_parameter("rejoin_speed_cap_mps").as_double());
    rejoin_severe_speed_cap_mps_ = std::max(0.1, this->get_parameter("rejoin_severe_speed_cap_mps").as_double());
    rejoin_heading_error_deg_ = std::max(0.1, this->get_parameter("rejoin_heading_error_deg").as_double());
    rejoin_severe_heading_error_deg_ = std::max(rejoin_heading_error_deg_, this->get_parameter("rejoin_severe_heading_error_deg").as_double());
    rejoin_cross_track_m_ = std::max(0.0, this->get_parameter("rejoin_cross_track_m").as_double());
    emergency_avoidance_speed_cap_mps_ = std::clamp(
        this->get_parameter("emergency_avoidance_speed_cap_mps").as_double(),
        0.5,
        std::max(max_speed_, 0.5));
    emergency_avoidance_wheel_over_distance_m_ = std::max(
        0.0, this->get_parameter("emergency_avoidance_wheel_over_distance_m").as_double());
    emergency_avoidance_switch_max_xte_m_ = std::max(
        Lpp_, this->get_parameter("emergency_avoidance_switch_max_xte_m").as_double());
    heading_align_rejoin_enabled_ = this->get_parameter("heading_align_rejoin_enabled").as_bool();
    heading_align_enter_rad_ = std::max(0.0, this->get_parameter("heading_align_enter_deg").as_double()) * M_PI / 180.0;
    heading_align_exit_rad_ = std::clamp(
        this->get_parameter("heading_align_exit_deg").as_double() * M_PI / 180.0,
        0.0,
        heading_align_enter_rad_);
    heading_align_speed_mps_ = std::clamp(
        this->get_parameter("heading_align_speed_mps").as_double(),
        0.05,
        std::max(max_speed_, 0.05));
    heading_align_max_speed_mps_ = std::clamp(
        this->get_parameter("heading_align_max_speed_mps").as_double(),
        heading_align_speed_mps_,
        std::max(max_speed_, heading_align_speed_mps_));
    heading_align_reset_xte_m_ = std::max(0.0, this->get_parameter("heading_align_reset_xte_m").as_double());
    far_xte_rejoin_override_enabled_ =
        this->get_parameter("far_xte_rejoin_override_enabled").as_bool();
    far_xte_rejoin_threshold_m_ = std::max(
        0.0, this->get_parameter("far_xte_rejoin_threshold_m").as_double());
    far_xte_rejoin_release_m_ = std::max(
        0.0, this->get_parameter("far_xte_rejoin_release_m").as_double());
    xte_hard_limit_m_ = std::max(
        far_xte_rejoin_threshold_m_, this->get_parameter("xte_hard_limit_m").as_double());
    far_xte_rejoin_speed_cap_mps_ = std::clamp(
        this->get_parameter("far_xte_rejoin_speed_cap_mps").as_double(),
        0.1,
        std::max(max_speed_, 0.1));
    far_xte_rejoin_hard_speed_cap_mps_ = std::clamp(
        this->get_parameter("far_xte_rejoin_hard_speed_cap_mps").as_double(),
        0.1,
        std::max(max_speed_, 0.1));
    far_xte_rejoin_lookahead_m_ = std::max(
        Lpp_, this->get_parameter("far_xte_rejoin_lookahead_m").as_double());
    far_xte_rejoin_max_approach_angle_deg_ = std::clamp(
        this->get_parameter("far_xte_rejoin_max_approach_angle_deg").as_double(),
        5.0,
        45.0);
    xte_rejoin_release_guard_s_ = std::clamp(
        this->get_parameter("xte_rejoin_release_guard_s").as_double(),
        0.0,
        20.0);
    xte_rejoin_release_guard_max_correction_deg_ = std::clamp(
        this->get_parameter("xte_rejoin_release_guard_max_correction_deg").as_double(),
        0.0,
        30.0);
    raw_route_rejoin_enabled_ =
        this->get_parameter("raw_route_rejoin_enabled").as_bool();
    raw_route_rejoin_threshold_m_ = std::max(
        0.0, this->get_parameter("raw_route_rejoin_threshold_m").as_double());
    raw_route_rejoin_release_m_ = std::max(
        0.0, this->get_parameter("raw_route_rejoin_release_m").as_double());
    raw_route_rejoin_threshold_m_ =
        std::max(raw_route_rejoin_threshold_m_, raw_route_rejoin_release_m_ + 1.0);
    wind_rejoin_enabled_ = this->get_parameter("wind_rejoin_enabled").as_bool();
    wind_rejoin_force_ref_n_ = std::max(
        1.0, this->get_parameter("wind_rejoin_force_ref_n").as_double());
    wind_rejoin_min_outward_force_n_ = std::max(
        0.0, this->get_parameter("wind_rejoin_min_outward_force_n").as_double());
    wind_rejoin_lookahead_gain_ = std::max(
        0.0, this->get_parameter("wind_rejoin_lookahead_gain").as_double());
    wind_rejoin_max_factor_ = std::clamp(
        this->get_parameter("wind_rejoin_max_factor").as_double(),
        0.0,
        10.0);
    wind_rejoin_far_threshold_m_ = std::max(
        0.0, this->get_parameter("wind_rejoin_far_threshold_m").as_double());
    wind_rejoin_raw_threshold_m_ = std::max(
        0.0, this->get_parameter("wind_rejoin_raw_threshold_m").as_double());
    wind_rejoin_release_m_ = std::max(
        0.0, this->get_parameter("wind_rejoin_release_m").as_double());
    wind_rejoin_min_lookahead_m_ = std::clamp(
        this->get_parameter("wind_rejoin_min_lookahead_m").as_double(),
        0.5 * Lpp_,
        std::max(far_xte_rejoin_lookahead_m_, 0.5 * Lpp_));
    wind_rejoin_speed_cap_mps_ = std::clamp(
        this->get_parameter("wind_rejoin_speed_cap_mps").as_double(),
        minimum_steerage_speed_,
        std::max(max_speed_, minimum_steerage_speed_));
    wind_rejoin_stale_timeout_s_ = std::max(
        0.1, this->get_parameter("wind_rejoin_stale_timeout_s").as_double());
    current_rejoin_enabled_ = this->get_parameter("current_rejoin_enabled").as_bool();
    current_rejoin_force_ref_n_ = std::max(
        1.0, this->get_parameter("current_rejoin_force_ref_n").as_double());
    current_rejoin_min_outward_force_n_ = std::max(
        0.0, this->get_parameter("current_rejoin_min_outward_force_n").as_double());
    current_rejoin_lookahead_gain_ = std::max(
        0.0, this->get_parameter("current_rejoin_lookahead_gain").as_double());
    current_rejoin_max_factor_ = std::clamp(
        this->get_parameter("current_rejoin_max_factor").as_double(),
        0.0,
        10.0);
    current_rejoin_far_threshold_m_ = std::max(
        0.0, this->get_parameter("current_rejoin_far_threshold_m").as_double());
    current_rejoin_raw_threshold_m_ = std::max(
        0.0, this->get_parameter("current_rejoin_raw_threshold_m").as_double());
    current_rejoin_release_m_ = std::max(
        0.0, this->get_parameter("current_rejoin_release_m").as_double());
    current_rejoin_min_lookahead_m_ = std::clamp(
        this->get_parameter("current_rejoin_min_lookahead_m").as_double(),
        0.5 * Lpp_,
        std::max(far_xte_rejoin_lookahead_m_, 0.5 * Lpp_));
    current_rejoin_speed_cap_mps_ = std::clamp(
        this->get_parameter("current_rejoin_speed_cap_mps").as_double(),
        minimum_steerage_speed_,
        std::max(max_speed_, minimum_steerage_speed_));
    current_rejoin_max_approach_angle_deg_ = std::clamp(
        this->get_parameter("current_rejoin_max_approach_angle_deg").as_double(),
        5.0, 45.0);
    current_rejoin_stale_timeout_s_ = std::max(
        0.1, this->get_parameter("current_rejoin_stale_timeout_s").as_double());
    wind_crab_enabled_ = this->get_parameter("wind_crab_enabled").as_bool();
    wind_crab_force_ref_n_ = std::max(
        1.0, this->get_parameter("wind_crab_force_ref_n").as_double());
    wind_crab_min_force_n_ = std::max(
        0.0, this->get_parameter("wind_crab_min_force_n").as_double());
    wind_crab_max_angle_deg_ = std::clamp(
        this->get_parameter("wind_crab_max_angle_deg").as_double(),
        0.0,
        15.0);
    wind_crab_min_speed_mps_ = std::max(
        0.0, this->get_parameter("wind_crab_min_speed_mps").as_double());
    wind_crab_base_scale_ = std::clamp(
        this->get_parameter("wind_crab_base_scale").as_double(),
        0.0,
        1.0);
    wind_crab_target_offset_enabled_ =
        this->get_parameter("wind_crab_target_offset_enabled").as_bool();
    wind_crab_target_offset_max_m_ = std::max(
        0.0, this->get_parameter("wind_crab_target_offset_max_m").as_double());
    wind_crab_target_offset_min_m_ = std::clamp(
        this->get_parameter("wind_crab_target_offset_min_m").as_double(),
        0.0,
        wind_crab_target_offset_max_m_);
    wind_crab_target_offset_force_ref_n_ = std::max(
        1.0, this->get_parameter("wind_crab_target_offset_force_ref_n").as_double());
    wind_crab_xte_kp_deg_per_m_ = std::clamp(
        this->get_parameter("wind_crab_xte_kp_deg_per_m").as_double(),
        0.0,
        1.0);
    wind_crab_xte_rate_kd_deg_per_mps_ = std::clamp(
        this->get_parameter("wind_crab_xte_rate_kd_deg_per_mps").as_double(),
        0.0,
        10.0);
    wind_crab_feedback_max_angle_deg_ = std::clamp(
        this->get_parameter("wind_crab_feedback_max_angle_deg").as_double(),
        0.0,
        wind_crab_max_angle_deg_);
    current_crab_enabled_ = this->get_parameter("current_crab_enabled").as_bool();
    current_crab_force_ref_n_ = std::max(
        1.0, this->get_parameter("current_crab_force_ref_n").as_double());
    current_crab_min_force_n_ = std::max(
        0.0, this->get_parameter("current_crab_min_force_n").as_double());
    current_crab_max_angle_deg_ = std::clamp(
        this->get_parameter("current_crab_max_angle_deg").as_double(),
        0.0,
        15.0);
    current_crab_min_speed_mps_ = std::max(
        0.0, this->get_parameter("current_crab_min_speed_mps").as_double());
    current_crab_base_scale_ = std::clamp(
        this->get_parameter("current_crab_base_scale").as_double(),
        0.0,
        1.0);
    current_crab_xte_kp_deg_per_m_ = std::clamp(
        this->get_parameter("current_crab_xte_kp_deg_per_m").as_double(),
        0.0,
        1.0);
    current_crab_xte_rate_kd_deg_per_mps_ = std::clamp(
        this->get_parameter("current_crab_xte_rate_kd_deg_per_mps").as_double(),
        0.0,
        10.0);
    current_crab_feedback_max_angle_deg_ = std::clamp(
        this->get_parameter("current_crab_feedback_max_angle_deg").as_double(),
        0.0,
        current_crab_max_angle_deg_);
    current_cog_rejoin_enabled_ = this->get_parameter("current_cog_rejoin_enabled").as_bool();
    current_cog_rejoin_kp_ = std::clamp(
        this->get_parameter("current_cog_rejoin_kp").as_double(),
        0.0,
        2.0);
    current_cog_rejoin_max_angle_deg_ = std::clamp(
        this->get_parameter("current_cog_rejoin_max_angle_deg").as_double(),
        0.0,
        std::max(0.0, current_rejoin_max_approach_angle_deg_));
    current_cog_rejoin_min_speed_mps_ = std::max(
        0.0, this->get_parameter("current_cog_rejoin_min_speed_mps").as_double());
    current_force_ff_enabled_ = this->get_parameter("current_force_ff_enabled").as_bool();
    current_force_ff_force_ref_n_ = std::max(
        1.0, this->get_parameter("current_force_ff_force_ref_n").as_double());
    current_force_ff_max_angle_deg_ = std::clamp(
        this->get_parameter("current_force_ff_max_angle_deg").as_double(),
        0.0,
        std::max(0.0, current_rejoin_max_approach_angle_deg_));
    current_force_ff_min_speed_mps_ = std::max(
        0.0, this->get_parameter("current_force_ff_min_speed_mps").as_double());
    roll_guard_enabled_ = this->get_parameter("roll_guard_enabled").as_bool();
    roll_guard_limit_deg_ = std::max(0.0, this->get_parameter("roll_guard_limit_deg").as_double());
    roll_guard_release_deg_ = std::clamp(
        this->get_parameter("roll_guard_release_deg").as_double(),
        0.0,
        roll_guard_limit_deg_);
    roll_guard_speed_cap_mps_ = std::clamp(
        this->get_parameter("roll_guard_speed_cap_mps").as_double(),
        0.1,
        std::max(max_speed_, 0.1));
    reset_sideslip_on_waypoint_switch_ = this->get_parameter("reset_sideslip_on_waypoint_switch").as_bool();
    wp_switch_modes_ = this->get_parameter("wp_switch_modes").as_string_array();
    wp_switch_radius_m_ = this->get_parameter("wp_switch_radius_m").as_double_array();
    wp_wheel_over_distance_m_ = this->get_parameter("wp_wheel_over_distance_m").as_double_array();
    wp_switch_max_xte_m_ = this->get_parameter("wp_switch_max_xte_m").as_double_array();
    wp_missed_after_distance_m_ = this->get_parameter("wp_missed_after_distance_m").as_double_array();
    wp_switch_max_heading_error_deg_ = this->get_parameter("wp_switch_max_heading_error_deg").as_double_array();
    wp_switch_max_speed_mps_ = this->get_parameter("wp_switch_max_speed_mps").as_double_array();
    wp_speed_limit_mps_ = this->get_parameter("wp_speed_limit_mps").as_double_array();
    wp_lookahead_m_ = this->get_parameter("wp_lookahead_m").as_double_array();
    wp_rejoin_cross_track_m_ = this->get_parameter("wp_rejoin_cross_track_m").as_double_array();
    wp_gate_blocked_speed_mps_ = this->get_parameter("wp_gate_blocked_speed_mps").as_double_array();
    wp_navigation_modes_ = this->get_parameter("wp_navigation_modes").as_string_array();

    std::vector<double> wp_x = this->get_parameter("wp_x").as_double_array();
    std::vector<double> wp_y = this->get_parameter("wp_y").as_double_array();

    waypoints_.clear();
    for (size_t i = 0; i < wp_x.size() && i < wp_y.size(); ++i) {
        waypoints_.push_back(Waypoint{wp_x[i], wp_y[i], -1.0, false, false, 0.0, ""});
    }
    raw_waypoints_ = waypoints_;
    raw_route_rejoin_active_ = false;
    const size_t raw_waypoint_count = waypoints_.size();
    waypoints_ = smooth_waypoints_for_turns(waypoints_);

    RCLCPP_INFO(this->get_logger(), "[INIT] 加载了 %zu 个航点，圆弧展开后 %zu 个航点",
        raw_waypoint_count, waypoints_.size());
}

std::vector<Waypoint> ShipGuidanceNode::smooth_waypoints_for_turns(const std::vector<Waypoint>& raw_waypoints)
{
    if (!turn_arc_smoothing_enabled_ || raw_waypoints.size() < 3) {
        return raw_waypoints;
    }

    const bool has_emergency_avoidance = std::any_of(
        raw_waypoints.begin(), raw_waypoints.end(),
        [](const Waypoint& wp) { return is_emergency_avoidance_mode(wp.navigation_mode); });
    if (has_emergency_avoidance) {
        RCLCPP_WARN(this->get_logger(),
            "[TURN ARC] bypassed internal smoothing: emergency_avoidance route uses raw planner geometry");
        return raw_waypoints;
    }

    const bool already_smoothed = std::any_of(
        raw_waypoints.begin(), raw_waypoints.end(),
        [](const Waypoint& wp) { return wp.is_arc_point; });
    if (already_smoothed) {
        return raw_waypoints;
    }

    const bool external_planner_route =
        turn_arc_external_route_min_points_ > 0 &&
        raw_waypoints.size() >= static_cast<size_t>(turn_arc_external_route_min_points_);
    if (external_planner_route && !turn_arc_external_route_smoothing_enabled_) {
        RCLCPP_INFO(this->get_logger(),
            "[TURN ARC] bypassed internal smoothing: external planner route has %zu points >= %d and external smoothing disabled",
            raw_waypoints.size(), turn_arc_external_route_min_points_);
        return raw_waypoints;
    }
    if (external_planner_route) {
        RCLCPP_INFO(this->get_logger(),
            "[TURN ARC] external planner route has %zu points >= %d; only sparse ordinary bends are eligible for local arc smoothing angle>=%.1fdeg segment>=%.1fm",
            raw_waypoints.size(), turn_arc_external_route_min_points_,
            turn_arc_external_route_min_angle_deg_, turn_arc_external_route_min_segment_m_);
    }

    auto append_if_far = [](std::vector<Waypoint>& out, const Waypoint& wp) {
        if (out.empty() || std::hypot(wp.x - out.back().x, wp.y - out.back().y) > 1.0) {
            out.push_back(wp);
        } else {
            out.back() = wp;
        }
    };

    struct DenseTurnCluster {
        size_t begin = 0;  // first internal bend index in raw_waypoints
        size_t end = 0;    // last internal bend index in raw_waypoints
        double max_turn_deg = 0.0;
        double min_segment_m = 0.0;
    };

    auto point_distance = [](const Waypoint& p0, const Waypoint& p1) {
        return std::hypot(p1.x - p0.x, p1.y - p0.y);
    };
    auto turn_angle_deg_at = [&](size_t idx) {
        if (idx == 0 || idx + 1 >= raw_waypoints.size()) {
            return 0.0;
        }
        const Waypoint& A = raw_waypoints[idx - 1];
        const Waypoint& B = raw_waypoints[idx];
        const Waypoint& C = raw_waypoints[idx + 1];
        const double ab_x = B.x - A.x;
        const double ab_y = B.y - A.y;
        const double bc_x = C.x - B.x;
        const double bc_y = C.y - B.y;
        const double len_ab = std::hypot(ab_x, ab_y);
        const double len_bc = std::hypot(bc_x, bc_y);
        if (len_ab < 1e-3 || len_bc < 1e-3) {
            return 0.0;
        }
        const double dot = (ab_x * bc_x + ab_y * bc_y) / (len_ab * len_bc);
        return std::acos(std::clamp(dot, -1.0, 1.0)) * 180.0 / M_PI;
    };
    auto ordinary_navigation_mode = [](const Waypoint& wp) {
        return wp.navigation_mode.empty() ||
               wp.navigation_mode == "cruise" ||
               wp.navigation_mode == "open_water_cruise" ||
               wp.navigation_mode == "post_turn_cruise" ||
               wp.navigation_mode == "transit";
    };

    std::vector<DenseTurnCluster> dense_turn_clusters;
    if (dense_turn_cluster_smoothing_enabled_ && raw_waypoints.size() >= 5) {
        const size_t n = raw_waypoints.size();
        std::vector<double> seg_len(n > 1 ? n - 1 : 0, 0.0);
        for (size_t k = 0; k + 1 < n; ++k) {
            seg_len[k] = point_distance(raw_waypoints[k], raw_waypoints[k + 1]);
        }
        std::vector<double> turn_deg(n, 0.0);
        std::vector<bool> significant(n, false);
        std::vector<bool> dense(n, false);
        for (size_t k = 1; k + 1 < n; ++k) {
            const bool protected_navigation_mode =
                is_dp_navigation_mode(raw_waypoints[k - 1].navigation_mode) ||
                is_dp_navigation_mode(raw_waypoints[k].navigation_mode) ||
                is_dp_navigation_mode(raw_waypoints[k + 1].navigation_mode) ||
                is_emergency_avoidance_mode(raw_waypoints[k - 1].navigation_mode) ||
                is_emergency_avoidance_mode(raw_waypoints[k].navigation_mode) ||
                is_emergency_avoidance_mode(raw_waypoints[k + 1].navigation_mode);
            turn_deg[k] = turn_angle_deg_at(k);
            significant[k] = !protected_navigation_mode &&
                ordinary_navigation_mode(raw_waypoints[k]) &&
                turn_deg[k] >= dense_turn_cluster_min_angle_deg_;
        }
        for (size_t k = 1; k + 2 < n; ++k) {
            if (significant[k] && significant[k + 1] &&
                seg_len[k] <= dense_turn_cluster_max_segment_m_) {
                dense[k] = true;
                dense[k + 1] = true;
            }
        }
        size_t k = 1;
        while (k + 1 < n) {
            if (!dense[k]) {
                ++k;
                continue;
            }
            const size_t begin = k;
            double max_turn = turn_deg[k];
            double min_segment = std::numeric_limits<double>::infinity();
            while (k + 1 < n && dense[k]) {
                max_turn = std::max(max_turn, turn_deg[k]);
                if (k > 0) {
                    min_segment = std::min(min_segment, seg_len[k - 1]);
                }
                if (k < seg_len.size()) {
                    min_segment = std::min(min_segment, seg_len[k]);
                }
                ++k;
            }
            const size_t end = k - 1;
            if (end >= begin &&
                static_cast<int>(end - begin + 1) >= dense_turn_cluster_min_turns_) {
                dense_turn_clusters.push_back(DenseTurnCluster{begin, end, max_turn, min_segment});
            }
        }
        for (const auto& cluster : dense_turn_clusters) {
            RCLCPP_WARN(this->get_logger(),
                "[DENSE TURN] cluster raw[%zu..%zu] turns=%zu max=%.1fdeg min_seg=%.1fm <= %.1fm: use cluster smoothing speed=%.2fm/s",
                cluster.begin,
                cluster.end,
                cluster.end - cluster.begin + 1,
                cluster.max_turn_deg,
                cluster.min_segment_m,
                dense_turn_cluster_max_segment_m_,
                dense_turn_cluster_speed_mps_);
        }
    }

    auto dense_cluster_start_index = [&](size_t idx) {
        for (size_t c = 0; c < dense_turn_clusters.size(); ++c) {
            if (dense_turn_clusters[c].begin == idx) {
                return static_cast<int>(c);
            }
        }
        return -1;
    };

    auto make_dense_cluster_points = [&](const DenseTurnCluster& cluster) {
        const size_t first = cluster.begin - 1;
        const size_t last = std::min(cluster.end + 1, raw_waypoints.size() - 1);
        std::vector<Waypoint> curve;
        for (size_t idx = first; idx <= last; ++idx) {
            curve.push_back(raw_waypoints[idx]);
        }
        for (int iter = 0; iter < dense_turn_cluster_chaikin_iterations_ && curve.size() >= 2; ++iter) {
            std::vector<Waypoint> next;
            next.reserve(curve.size() * 2);
            next.push_back(curve.front());
            for (size_t j = 0; j + 1 < curve.size(); ++j) {
                const Waypoint& p0 = curve[j];
                const Waypoint& p1 = curve[j + 1];
                Waypoint q = p0;
                q.x = 0.75 * p0.x + 0.25 * p1.x;
                q.y = 0.75 * p0.y + 0.25 * p1.y;
                Waypoint r = p1;
                r.x = 0.25 * p0.x + 0.75 * p1.x;
                r.y = 0.25 * p0.y + 0.75 * p1.y;
                next.push_back(q);
                next.push_back(r);
            }
            next.push_back(curve.back());
            curve = next;
        }

        double inherited_route_limit = std::numeric_limits<double>::infinity();
        for (size_t idx = first; idx <= last; ++idx) {
            const Waypoint& wp = raw_waypoints[idx];
            if (wp.speed_override_is_route_limit && wp.speed_override > 0.1) {
                inherited_route_limit = std::min(inherited_route_limit, wp.speed_override);
            }
        }
        const double dense_speed = std::isfinite(inherited_route_limit)
            ? std::min(dense_turn_cluster_speed_mps_, inherited_route_limit)
            : dense_turn_cluster_speed_mps_;

        std::vector<Waypoint> out;
        auto push_cluster_point = [&](const Waypoint& base, bool interior) {
            Waypoint wp = base;
            if (interior) {
                wp.speed_override = dense_speed;
                wp.speed_override_is_route_limit = false;
                wp.is_arc_point = true;
                wp.turn_angle_deg = cluster.max_turn_deg;
            }
            append_if_far(out, wp);
        };
        if (!curve.empty()) {
            push_cluster_point(curve.front(), false);
        }
        for (size_t j = 0; j + 1 < curve.size(); ++j) {
            const Waypoint& p0 = curve[j];
            const Waypoint& p1 = curve[j + 1];
            const double len = point_distance(p0, p1);
            const int samples = std::max(1, static_cast<int>(std::ceil(len / dense_turn_cluster_sample_spacing_m_)));
            for (int sample = 1; sample <= samples; ++sample) {
                const double ratio = static_cast<double>(sample) / static_cast<double>(samples);
                Waypoint wp = p1;
                wp.x = p0.x + (p1.x - p0.x) * ratio;
                wp.y = p0.y + (p1.y - p0.y) * ratio;
                const bool interior = !(j + 1 == curve.size() - 1 && sample == samples);
                push_cluster_point(wp, interior);
            }
        }
        if (!out.empty()) {
            out.back() = raw_waypoints[last];
        }
        return out;
    };

    std::vector<Waypoint> smoothed;
    smoothed.reserve(raw_waypoints.size() * 3);
    smoothed.push_back(raw_waypoints.front());

    int inserted_turns = 0;
    int dense_clusters_inserted = 0;
    for (size_t i = 1; i + 1 < raw_waypoints.size(); ++i) {
        const int dense_cluster_idx = dense_cluster_start_index(i);
        if (dense_cluster_idx >= 0) {
            const DenseTurnCluster& cluster = dense_turn_clusters[static_cast<size_t>(dense_cluster_idx)];
            const auto cluster_points = make_dense_cluster_points(cluster);
            for (const auto& wp : cluster_points) {
                append_if_far(smoothed, wp);
            }
            ++dense_clusters_inserted;
            i = cluster.end + 1;
            continue;
        }

        const Waypoint& A = raw_waypoints[i - 1];
        const Waypoint& B = raw_waypoints[i];
        const Waypoint& C = raw_waypoints[i + 1];

        const double ab_x = B.x - A.x;
        const double ab_y = B.y - A.y;
        const double bc_x = C.x - B.x;
        const double bc_y = C.y - B.y;
        const double len_ab = std::hypot(ab_x, ab_y);
        const double len_bc = std::hypot(bc_x, bc_y);
        if (len_ab < 1e-3 || len_bc < 1e-3) {
            append_if_far(smoothed, B);
            continue;
        }

        const double u1x = ab_x / len_ab;
        const double u1y = ab_y / len_ab;
        const double u2x = bc_x / len_bc;
        const double u2y = bc_y / len_bc;
        double cos_theta = std::clamp(u1x * u2x + u1y * u2y, -1.0, 1.0);
        const double theta = std::acos(cos_theta);
        const double turn_deg = theta * 180.0 / M_PI;
        const double required_turn_angle_deg =
            external_planner_route ? turn_arc_external_route_min_angle_deg_ : turn_arc_min_angle_deg_;
        if (turn_deg < required_turn_angle_deg) {
            append_if_far(smoothed, B);
            continue;
        }
        if (external_planner_route) {
            const bool ordinary_navigation_mode =
                B.navigation_mode.empty() ||
                B.navigation_mode == "cruise" ||
                B.navigation_mode == "open_water_cruise" ||
                B.navigation_mode == "post_turn_cruise" ||
                B.navigation_mode == "transit";
            const bool protected_navigation_mode =
                is_dp_navigation_mode(A.navigation_mode) ||
                is_dp_navigation_mode(B.navigation_mode) ||
                is_dp_navigation_mode(C.navigation_mode) ||
                is_emergency_avoidance_mode(A.navigation_mode) ||
                is_emergency_avoidance_mode(B.navigation_mode) ||
                is_emergency_avoidance_mode(C.navigation_mode);
            if (!ordinary_navigation_mode || protected_navigation_mode) {
                RCLCPP_INFO(this->get_logger(),
                    "[TURN ARC] external wp[%zu] %.1fdeg skipped: protected/non-ordinary navigation mode prev=%s current=%s next=%s",
                    i, turn_deg, A.navigation_mode.c_str(), B.navigation_mode.c_str(), C.navigation_mode.c_str());
                append_if_far(smoothed, B);
                continue;
            }
            if (len_ab < turn_arc_external_route_min_segment_m_ ||
                len_bc < turn_arc_external_route_min_segment_m_) {
                RCLCPP_INFO(this->get_logger(),
                    "[TURN ARC] external wp[%zu] %.1fdeg skipped: sparse-segment gate len_ab=%.1fm len_bc=%.1fm min=%.1fm",
                    i, turn_deg, len_ab, len_bc, turn_arc_external_route_min_segment_m_);
                append_if_far(smoothed, B);
                continue;
            }
        }

        double radius = external_planner_route ? turn_arc_external_route_radius_m_ : turn_arc_radius_m_;
        if (turn_deg <= turn_arc_shallow_angle_deg_) {
            radius = external_planner_route ? turn_arc_external_route_radius_m_ : turn_arc_shallow_radius_m_;
        } else if (turn_deg < 45.0 && turn_arc_shallow_radius_m_ > turn_arc_radius_m_) {
            const double denom = std::max(1.0, 45.0 - turn_arc_shallow_angle_deg_);
            const double blend = std::clamp((turn_deg - turn_arc_shallow_angle_deg_) / denom, 0.0, 1.0);
            radius = external_planner_route ? turn_arc_external_route_radius_m_ :
                turn_arc_shallow_radius_m_ +
                blend * (turn_arc_radius_m_ - turn_arc_shallow_radius_m_);
        }

        const double route_limit_candidates[] = {
            A.speed_override_is_route_limit ? A.speed_override : std::numeric_limits<double>::infinity(),
            B.speed_override_is_route_limit ? B.speed_override : std::numeric_limits<double>::infinity(),
            C.speed_override_is_route_limit ? C.speed_override : std::numeric_limits<double>::infinity()
        };
        double inherited_route_limit = std::numeric_limits<double>::infinity();
        for (double candidate : route_limit_candidates) {
            if (std::isfinite(candidate) && candidate > 0.1) {
                inherited_route_limit = std::min(inherited_route_limit, candidate);
            }
        }
        const bool has_inherited_route_limit = std::isfinite(inherited_route_limit);
        const double inserted_speed_override =
            has_inherited_route_limit ? std::min(turn_arc_speed_mps_, inherited_route_limit) : turn_arc_speed_mps_;
        const bool inserted_speed_is_route_limit = false;
        double tangent_dist = radius * std::tan(theta / 2.0);
        const double configured_max_tangent_dist = external_planner_route
            ? turn_arc_external_route_max_tangent_m_
            : turn_arc_max_tangent_m_;
        if (configured_max_tangent_dist > 1.0 && tangent_dist > configured_max_tangent_dist) {
            tangent_dist = configured_max_tangent_dist;
            radius = tangent_dist / std::max(std::tan(theta / 2.0), 1e-6);
        }
        const double max_tangent_dist = std::min(len_ab, len_bc) * 0.75;
        if (tangent_dist > max_tangent_dist) {
            tangent_dist = max_tangent_dist;
            radius = tangent_dist / std::max(std::tan(theta / 2.0), 1e-6);
        }
        if (!std::isfinite(radius) || radius < 10.0 || tangent_dist < 5.0) {
            append_if_far(smoothed, B);
            continue;
        }

        double pre_arc_offset = tangent_dist + turn_arc_pre_decel_dist_m_;
        const double min_prev_gap = std::min(50.0, std::max(1.0, len_ab * 0.10));
        const double max_pre_arc_offset = std::max(tangent_dist, len_ab - min_prev_gap);
        if (pre_arc_offset > max_pre_arc_offset) {
            RCLCPP_WARN(this->get_logger(),
                "[TURN ARC] pre-decel clamped at wp[%zu]: requested_offset=%.1fm max=%.1fm len_ab=%.1fm",
                i, pre_arc_offset, max_pre_arc_offset, len_ab);
            pre_arc_offset = max_pre_arc_offset;
        }

        Waypoint pre_arc;
        pre_arc.x = B.x - pre_arc_offset * u1x;
        pre_arc.y = B.y - pre_arc_offset * u1y;
        pre_arc.speed_override = -1.0;
        pre_arc.speed_override_is_route_limit = false;
        pre_arc.is_arc_point = false;
        pre_arc.turn_angle_deg = turn_deg;
        pre_arc.navigation_mode = B.navigation_mode;

        Waypoint t1;
        t1.x = B.x - tangent_dist * u1x;
        t1.y = B.y - tangent_dist * u1y;
        t1.speed_override = inserted_speed_override;
        t1.speed_override_is_route_limit = inserted_speed_is_route_limit;
        t1.is_arc_point = true;
        t1.turn_angle_deg = turn_deg;
        t1.navigation_mode = B.navigation_mode;

        Waypoint t2;
        t2.x = B.x + tangent_dist * u2x;
        t2.y = B.y + tangent_dist * u2y;
        t2.speed_override = inserted_speed_override;
        t2.speed_override_is_route_limit = inserted_speed_is_route_limit;
        t2.is_arc_point = true;
        t2.turn_angle_deg = turn_deg;
        t2.navigation_mode = B.navigation_mode;

        const double cross = u1x * u2y - u1y * u2x;
        const double turn_sign = (cross >= 0.0) ? 1.0 : -1.0;
        const double center_x = t1.x - turn_sign * u1y * radius;
        const double center_y = t1.y + turn_sign * u1x * radius;
        const double angle_start = std::atan2(t1.y - center_y, t1.x - center_x);
        const double angle_end = std::atan2(t2.y - center_y, t2.x - center_x);
        double angle_diff = normalize_angle_pi(angle_end - angle_start);
        if (turn_sign > 0.0 && angle_diff < 0.0) {
            angle_diff += 2.0 * M_PI;
        } else if (turn_sign < 0.0 && angle_diff > 0.0) {
            angle_diff -= 2.0 * M_PI;
        }

        const double arc_len = std::abs(radius * angle_diff);
        const int samples = std::max(2, static_cast<int>(std::ceil(arc_len / turn_arc_sample_spacing_m_)));

        append_if_far(smoothed, pre_arc);
        append_if_far(smoothed, t1);
        for (int sample = 1; sample < samples; ++sample) {
            const double ratio = static_cast<double>(sample) / samples;
            const double angle = angle_start + angle_diff * ratio;
            Waypoint arc_wp;
            arc_wp.x = center_x + radius * std::cos(angle);
            arc_wp.y = center_y + radius * std::sin(angle);
            arc_wp.speed_override = inserted_speed_override;
            arc_wp.speed_override_is_route_limit = inserted_speed_is_route_limit;
            arc_wp.is_arc_point = true;
            arc_wp.turn_angle_deg = turn_deg;
            arc_wp.navigation_mode = B.navigation_mode;
            append_if_far(smoothed, arc_wp);
        }
        append_if_far(smoothed, t2);
        ++inserted_turns;

        RCLCPP_INFO(this->get_logger(),
            "[TURN ARC] wp[%zu] %.1fdeg R=%.0fm tangent=%.0fm tan_cap=%.0fm samples=%d speed=%.2fm/s mode=TURN_CAP",
            i, turn_deg, radius, tangent_dist, configured_max_tangent_dist, samples,
            inserted_speed_override > 0.1 ? inserted_speed_override : max_speed_);
    }

    append_if_far(smoothed, raw_waypoints.back());
    if (inserted_turns > 0 || dense_clusters_inserted > 0) {
        RCLCPP_INFO(this->get_logger(),
            "[TURN ARC] expanded route: raw=%zu smooth=%zu turns=%d dense_clusters=%d",
            raw_waypoints.size(), smoothed.size(), inserted_turns, dense_clusters_inserted);
    }
    return smoothed;
}

double ShipGuidanceNode::apply_heading_rate_limit(double raw_psi_cmd, double dt)
{
    if (!std::isfinite(raw_psi_cmd)) {
        return current_yaw_;
    }

    double rate_limit_rad_s = heading_cmd_rate_limit_deg_s_ * M_PI / 180.0;
    if (rate_limit_rad_s <= 0.0 || rate_limit_rad_s >= 2.0 * M_PI) {
        psi_cmd_prev_ = raw_psi_cmd;
        init_psi_ = false;
        return raw_psi_cmd;
    }

    if (dt <= 0.0 || dt > 2.0) {
        dt = 0.5;
    }

    if (init_psi_) {
        psi_cmd_prev_ = current_yaw_;
        init_psi_ = false;
    }

    double max_step = rate_limit_rad_s * dt;
    double delta = normalize_angle_pi(raw_psi_cmd - psi_cmd_prev_);
    double limited = normalize_angle_pi(psi_cmd_prev_ + std::clamp(delta, -max_step, max_step));

    if (std::abs(delta) > max_step) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[LOS] Heading command rate-limited: raw=%.1f° limited=%.1f° rate=%.1f°/s",
            raw_psi_cmd * 180.0 / M_PI,
            limited * 180.0 / M_PI,
            heading_cmd_rate_limit_deg_s_);
    }

    psi_cmd_prev_ = limited;
    return limited;
}

void ShipGuidanceNode::odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    current_x_ = msg->pose.pose.position.x;
    current_y_ = msg->pose.pose.position.y;

    // [诊断] 每秒打印一次接收到的位置
    static int debug_counter = 0;
    if (++debug_counter % 100 == 0) {
        RCLCPP_INFO(this->get_logger(),
            "[ODOM_RECV] pos=(%.2f, %.2f) vel=(%.2f, %.2f)",
            current_x_, current_y_, current_u_, current_v_);
    }

    tf2::Quaternion q(
        msg->pose.pose.orientation.x,
        msg->pose.pose.orientation.y,
        msg->pose.pose.orientation.z,
        msg->pose.pose.orientation.w);
    tf2::Matrix3x3 m(q);
    double roll, pitch;
    m.getRPY(roll, pitch, current_yaw_);
    current_roll_ = roll;

    current_u_ = msg->twist.twist.linear.x;
    current_v_ = msg->twist.twist.linear.y;
    current_roll_rate_ = msg->twist.twist.angular.x;
    current_r_ = msg->twist.twist.angular.z;

    odom_received_ = true;
}

void ShipGuidanceNode::env_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg)
{
    if (msg->header.frame_id != "base_link") {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[Wind Rejoin] expected /env/total_load in base_link, got '%s'",
            msg->header.frame_id.c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    current_env_fx_n_ = msg->wrench.force.x;
    current_env_fy_n_ = msg->wrench.force.y;
    current_env_mz_nm_ = msg->wrench.torque.z;
    last_env_load_time_ = this->now();
    env_load_received_ = true;
}

void ShipGuidanceNode::current_load_callback(const geometry_msgs::msg::WrenchStamped::SharedPtr msg)
{
    if (msg->header.frame_id != "base_link") {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[Current Rejoin] expected /env/current_load in base_link, got '%s'",
            msg->header.frame_id.c_str());
        return;
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    current_load_fx_n_ = msg->wrench.force.x;
    current_load_fy_n_ = msg->wrench.force.y;
    current_load_mz_nm_ = msg->wrench.torque.z;
    last_current_load_time_ = this->now();
    current_load_received_ = true;
}

bool ShipGuidanceNode::select_dp_weathervane_heading(double fallback_heading,
                                                     double& selected_heading,
                                                     double& env_force_norm_n,
                                                     double& env_age_s)
{
    selected_heading = normalize_angle_pi(fallback_heading);
    env_force_norm_n = 0.0;
    env_age_s = std::numeric_limits<double>::infinity();

    if (!dp_weathervane_heading_enabled_) {
        return false;
    }

    double env_fx_body = 0.0;
    double env_fy_body = 0.0;
    double yaw_rad = 0.0;
    bool env_valid = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        env_fx_body = current_env_fx_n_;
        env_fy_body = current_env_fy_n_;
        yaw_rad = current_yaw_;
        env_valid = env_load_received_;
        if (env_valid) {
            env_age_s = (this->now() - last_env_load_time_).seconds();
        }
    }

    if (!env_valid || env_age_s > dp_weathervane_stale_timeout_s_) {
        return false;
    }

    const double cos_yaw = std::cos(yaw_rad);
    const double sin_yaw = std::sin(yaw_rad);
    const double env_fx_world = cos_yaw * env_fx_body - sin_yaw * env_fy_body;
    const double env_fy_world = sin_yaw * env_fx_body + cos_yaw * env_fy_body;
    env_force_norm_n = std::hypot(env_fx_world, env_fy_world);
    if (env_force_norm_n < dp_weathervane_min_env_force_n_) {
        return false;
    }

    selected_heading = normalize_angle_pi(std::atan2(-env_fy_world, -env_fx_world));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 运行时 reset：清零 ILOS 积分 + 侧滑估计 + path latch + rejoin/gate 锁存
// ─────────────────────────────────────────────────────────────────────────────
void ShipGuidanceNode::reset_guidance()
{
    // 清零跨 run 累积的积分器/估计/latch，等价于冷启初始化。
    // 由 reset_callback 调用，调用方持 state_mutex_。
    integral_e_ = 0.0;
    prev_e_ = 0.0;
    beta_hat_ = 0.0;
    psi_cmd_prev_ = 0.0;
    init_psi_ = true;
    last_time_ = this->now();
    // path signature latch：防止相同 scenario 重跑时丢弃新 path
    has_last_path_signature_ = false;
    last_path_signature_ = 0;
    last_path_size_ = 0;
    // rejoin/gate latch（path_callback 未清零的残留）
    cruise_recovery_gate_cleared_ = false;
    cruise_recovery_stable_timer_active_ = false;
    corridor_hold_active_ = false;
    turn_segment_speed_gate_active_ = false;
    RCLCPP_INFO(this->get_logger(), "reset_guidance: ILOS integral + path latch + rejoin gates cleared");
}

void ShipGuidanceNode::reset_callback(const ship_interfaces::msg::ShipReset::SharedPtr /*msg*/)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    reset_guidance();
}

void ShipGuidanceNode::path_callback(const nav_msgs::msg::Path::SharedPtr msg)
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (msg->poses.empty()) return;

    const auto incoming_path_signature = compute_path_signature(*msg);
    if (has_last_path_signature_ &&
        last_path_signature_ == incoming_path_signature &&
        last_path_size_ == msg->poses.size()) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "[PATH] ignored duplicate Path poses=%zu signature=%llu",
            msg->poses.size(),
            static_cast<unsigned long long>(incoming_path_signature));
        return;
    }
    has_last_path_signature_ = true;
    last_path_signature_ = incoming_path_signature;
    last_path_size_ = msg->poses.size();

    waypoints_.clear();
    raw_waypoints_.clear();
    for (const auto& pose : msg->poses) {
        Waypoint wp;
        wp.x = pose.pose.position.x;
        wp.y = pose.pose.position.y;
        wp.speed_override = (pose.pose.orientation.z > 0.1) ? pose.pose.orientation.z : -1.0;
        wp.is_arc_point = (pose.pose.orientation.x > 0.5);
        wp.speed_override_is_route_limit =
            wp.speed_override > 0.1 && pose.pose.orientation.y > 0.5;
        wp.navigation_mode = navigation_mode_from_code(pose.pose.position.z);
        waypoints_.push_back(wp);
    }

    path_from_callback_ = true;
    path_start_pos_.x = current_x_;
    path_start_pos_.y = current_y_;
    dp_mode_active_ = false;
    final_dp_latched_ = false;
    final_dp_overrun_braking_active_ = false;
    final_dp_overrun_hold_initialized_ = false;
    final_dp_hold_initialized_ = false;
    dp_hold_x_ = 0.0;
    dp_hold_y_ = 0.0;
    dp_hold_psi_ = 0.0;
    raw_route_rejoin_active_ = false;
    speed_recovery_segment_idx_ = -1;
    speed_recovery_gate_cleared_ = false;
    speed_recovery_speed_cap_ = 0.0;
    speed_recovery_last_time_ = this->now();
    turn_segment_speed_gate_active_ = false;

    // ========== 智能航点索引确定：找到船当前所在的航段 ==========
    // 遍历所有航点，找到第一个船已经越过的航点
    // 船在航点T_i的"外侧"意味着：船在沿航线方向上已经经过了T_i点
    current_wp_idx_ = 0;

    if (waypoints_.size() > 1) {
        for (size_t i = 0; i < waypoints_.size() - 1; ++i) {
            Waypoint wp_prev = waypoints_[i];
            Waypoint wp_next = waypoints_[i + 1];

            double dx = wp_next.x - wp_prev.x;
            double dy = wp_next.y - wp_prev.y;
            double seg_len = std::hypot(dx, dy);

            if (seg_len < 1e-6) continue;  // 跳过退化航段

            // 单位方向向量
            double n_k_x = dx / seg_len;
            double n_k_y = dy / seg_len;

            // 从下一航点指向船舶的向量 (Ship - T_{i+1})
            double dx_wp = current_x_ - wp_next.x;
            double dy_wp = current_y_ - wp_next.y;

            // 沿航线方向的投影进度
            // progress > 0 表示船在T_{i+1}的"外侧"（已越过该航点）
            // progress <= 0 表示船尚未到达T_{i+1}
            double progress = dx_wp * n_k_x + dy_wp * n_k_y;

            if (progress > 0.0) {
                // 船已经越过T_{i+1}，应该从下一个航段开始导航
                current_wp_idx_ = i + 1;
            } else {
                // 船尚未越过T_{i+1}，当前航段是 i -> i+1
                break;
            }
        }
    }

    if (path_from_callback_ && current_wp_idx_ == 0 && waypoints_.size() > 1) {
        double wp0_dist = std::hypot(waypoints_[0].x, waypoints_[0].y);
        const double ORIGIN_SANITY_RADIUS = 5.0;
        if (wp0_dist < ORIGIN_SANITY_RADIUS) {
            const double ship_to_origin = std::hypot(
                path_start_pos_.x - waypoints_[0].x,
                path_start_pos_.y - waypoints_[0].y);
            if (dynamic_path_attach_current_position_enabled_ &&
                ship_to_origin > dynamic_path_attach_min_distance_m_) {
                const double old_x = waypoints_[0].x;
                const double old_y = waypoints_[0].y;
                waypoints_[0].x = path_start_pos_.x;
                waypoints_[0].y = path_start_pos_.y;
                current_wp_idx_ = 1;
                RCLCPP_WARN(this->get_logger(),
                    "[PATH ATTACH] late origin WP0=(%.1f,%.1f) replaced by current ship pos=(%.1f,%.1f); navigate wp[0]->wp[1]=(%.1f,%.1f)",
                    old_x, old_y, path_start_pos_.x, path_start_pos_.y,
                    waypoints_[1].x, waypoints_[1].y);
            } else {
                current_wp_idx_ = 1;
                RCLCPP_INFO(this->get_logger(),
                    "[PATH] WP0=coordinate origin(%.2f,%.2f), skip to wp[1]=(%.1f,%.1f)",
                    waypoints_[0].x, waypoints_[0].y,
                    waypoints_[1].x, waypoints_[1].y);
            }
        }
    }

    raw_waypoints_ = waypoints_;
    const size_t raw_waypoint_count = waypoints_.size();
    waypoints_ = smooth_waypoints_for_turns(waypoints_);
    current_wp_idx_ = std::clamp(current_wp_idx_, 0, static_cast<int>(waypoints_.size()) - 1);
    if (smoothed_waypoints_pub_) {
        nav_msgs::msg::Path smoothed_path;
        smoothed_path.header.stamp = this->now();
        smoothed_path.header.frame_id = "odom";
        smoothed_path.poses.reserve(waypoints_.size());
        for (const auto& waypoint : waypoints_) {
            geometry_msgs::msg::PoseStamped pose;
            pose.header = smoothed_path.header;
            pose.pose.position.x = waypoint.x;
            pose.pose.position.y = waypoint.y;
            pose.pose.position.z = waypoint.turn_angle_deg;
            pose.pose.orientation.z = waypoint.speed_override;
            pose.pose.orientation.w = waypoint.is_arc_point ? 1.0 : 0.0;
            smoothed_path.poses.push_back(pose);
        }
        smoothed_waypoints_pub_->publish(smoothed_path);
    }

    RCLCPP_INFO(this->get_logger(),
        "[PATH] Got %zu waypoints, smoothed to %zu, start from wp[%d]=(%.1f,%.1f)",
        raw_waypoint_count, waypoints_.size(), current_wp_idx_,
        waypoints_[current_wp_idx_].x, waypoints_[current_wp_idx_].y);
}

void ShipGuidanceNode::publish_final_dp_hold(double x, double y, double& psi_cmd, double& u_cmd)
{
    dp_mode_active_ = true;
    final_dp_latched_ = true;
    final_dp_overrun_braking_active_ = true;

    if (!final_dp_hold_initialized_) {
        dp_hold_x_ = x;
        dp_hold_y_ = y;
        double env_force_norm = 0.0;
        double env_age_s = 0.0;
        const bool weather_heading_active = select_dp_weathervane_heading(
            current_yaw_, dp_hold_psi_, env_force_norm, env_age_s);
        final_dp_hold_initialized_ = true;
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
            "[FINAL DP HOLD] fallback initialized at current stop point=(%.1f,%.1f) psi=%.1fdeg weather=%d env=%.0fN age=%.2fs",
            dp_hold_x_, dp_hold_y_, dp_hold_psi_ * 180.0 / M_PI,
            weather_heading_active ? 1 : 0, env_force_norm, env_age_s);
    }

    psi_cmd = dp_hold_psi_;
    u_cmd = 0.0;

    std_msgs::msg::Float64 speed_msg;
    speed_msg.data = 0.0;
    target_speed_pub_->publish(speed_msg);

    std_msgs::msg::Float64 heading_msg;
    heading_msg.data = psi_cmd;
    heading_setpoint_pub_->publish(heading_msg);

    geometry_msgs::msg::PoseStamped pose_msg;
    pose_msg.header.stamp = this->now();
    pose_msg.header.frame_id = "odom";
    pose_msg.pose.position.x = dp_hold_x_;
    pose_msg.pose.position.y = dp_hold_y_;
    pose_msg.pose.position.z = current_cross_track_error_;
    tf2::Quaternion q;
    q.setRPY(0, 0, psi_cmd);
    pose_msg.pose.orientation = tf2::toMsg(q);
    target_pose_pub_->publish(pose_msg);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[FINAL DP HOLD] sticky hold=(%.1f,%.1f) current=(%.1f,%.1f) dist=%.1fm psi=%.1fdeg",
        dp_hold_x_, dp_hold_y_, x, y,
        std::hypot(dp_hold_x_ - x, dp_hold_y_ - y),
        dp_hold_psi_ * 180.0 / M_PI);
}

void ShipGuidanceNode::calculate_los(double x, double y, double& psi_cmd, double& u_cmd)
{
    // ========== 安全边界检查 ==========
    if (waypoints_.empty()) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000, "[LOS] 航线为空!");
        psi_cmd = current_yaw_;
        u_cmd = 0.0;
        return;
    }

    if (final_dp_latched_ || dp_mode_active_) {
        publish_final_dp_hold(x, y, psi_cmd, u_cmd);
        return;
    }

    final_dp_overrun_braking_active_ = false;

    // 索引已超出，直接停车（由外层 control_loop 处理终点逻辑）
    if (current_wp_idx_ >= (int)waypoints_.size()) {
        psi_cmd = current_yaw_;
        u_cmd = 0.0;
        return;
    }

    const bool route_uses_internal_reference = std::any_of(
        wp_switch_modes_.begin(), wp_switch_modes_.end(),
        [](const std::string& mode) { return mode == "internal_reference"; });
    if (route_uses_internal_reference && waypoints_.size() > 1) {
        int best_target_idx = current_wp_idx_;
        double best_distance = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i + 1 < waypoints_.size(); ++i) {
            const double distance = compute_segment_distance(
                x, y,
                waypoints_[i].x, waypoints_[i].y,
                waypoints_[i + 1].x, waypoints_[i + 1].y);
            if (distance < best_distance) {
                best_distance = distance;
                best_target_idx = static_cast<int>(i + 1);
            }
        }
        best_target_idx = std::clamp(best_target_idx, 1, static_cast<int>(waypoints_.size()) - 1);
        if (best_target_idx != current_wp_idx_) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[ROUTE PROJECTION] internal reference target reset: wp[%d] -> wp[%d], nearest_dist=%.1fm",
                current_wp_idx_, best_target_idx, best_distance);
            current_wp_idx_ = best_target_idx;
        }
    }

    // ========== 获取当前航段端点 ==========
    Waypoint wp_prev, wp_next;
    int target_wp_idx = 0;
    double cmd_dt = (this->now() - last_time_).seconds();
    if (cmd_dt <= 0.0 || cmd_dt > 2.0) {
        cmd_dt = 0.5;
    }

    if (waypoints_.size() == 1) {
        // 单航点情况：从锁定的起点出发
        if (!path_from_callback_) {
            path_start_pos_.x = x;
            path_start_pos_.y = y;
            path_from_callback_ = true;
        }
        wp_prev = path_start_pos_;
        wp_next = waypoints_[0];
        target_wp_idx = 0;
    } else if (current_wp_idx_ == 0) {
        // [CaseB] Path来自ROS Topic时，第一段从当前船位出发
        if (path_from_callback_) {
            target_wp_idx = 0;
            wp_prev = path_start_pos_;   // 船当前位置(odometry读取)
            wp_next = waypoints_[0];     // 第一个目标航点
        } else {
            // YAML仿真模式：第一个航点即起点
            wp_prev = waypoints_[0];
            wp_next = waypoints_[1];
            target_wp_idx = 1;
        }
    } else {
        // 后续航段：正常使用航点坐标
        target_wp_idx = current_wp_idx_;
        wp_prev = waypoints_[current_wp_idx_ - 1];
        wp_next = waypoints_[current_wp_idx_];
    }

    // ========== 航段几何计算 ==========
    double alpha_k = compute_path_angle(wp_prev.x, wp_prev.y, wp_next.x, wp_next.y);
    double e = compute_cross_track_error(x, y, wp_prev.x, wp_prev.y, wp_next.x, wp_next.y);
    double s = compute_along_track(x, y, wp_prev.x, wp_prev.y, wp_next.x, wp_next.y);
    double seg_len = std::hypot(wp_next.x - wp_prev.x, wp_next.y - wp_prev.y);
    double dist_to_wp = std::hypot(wp_next.x - x, wp_next.y - y);
    double route_remaining_to_final = std::max(0.0, seg_len - std::clamp(s, 0.0, seg_len));
    for (int i = target_wp_idx; i + 1 < static_cast<int>(waypoints_.size()); ++i) {
        route_remaining_to_final += std::hypot(
            waypoints_[i + 1].x - waypoints_[i].x,
            waypoints_[i + 1].y - waypoints_[i].y);
    }

    struct RawRouteReference {
        bool valid{false};
        int segment_idx{-1};
        double cross_track_error{0.0};
        double abs_cross_track_error{0.0};
        double path_heading{0.0};
        double distance{std::numeric_limits<double>::infinity()};
    };

    RawRouteReference raw_route_ref;
    if (raw_route_rejoin_enabled_ && raw_waypoints_.size() >= 2) {
        for (size_t i = 0; i + 1 < raw_waypoints_.size(); ++i) {
            const auto& a = raw_waypoints_[i];
            const auto& b = raw_waypoints_[i + 1];
            const double raw_seg_len = std::hypot(b.x - a.x, b.y - a.y);
            if (raw_seg_len < 1e-6) {
                continue;
            }
            const double raw_distance = compute_segment_distance(x, y, a.x, a.y, b.x, b.y);
            if (raw_distance < raw_route_ref.distance) {
                raw_route_ref.valid = true;
                raw_route_ref.segment_idx = static_cast<int>(i);
                raw_route_ref.distance = raw_distance;
                raw_route_ref.cross_track_error = compute_cross_track_error(x, y, a.x, a.y, b.x, b.y);
                raw_route_ref.abs_cross_track_error = std::abs(raw_route_ref.cross_track_error);
                raw_route_ref.path_heading = compute_path_angle(a.x, a.y, b.x, b.y);
            }
        }
    }
    current_cross_track_error_ = raw_route_ref.valid ? raw_route_ref.cross_track_error : e;

    // [诊断] 每秒打印一次航线信息
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[LOS DIAG] wp[%d]=(%.1f,%.1f)->(%.1f,%.1f) alpha=%.1f° e=%.1fm s=%.1f/%.1f x=%.1f y=%.1f",
        current_wp_idx_, wp_prev.x, wp_prev.y, wp_next.x, wp_next.y,
        alpha_k*180/M_PI, e, s, seg_len, x, y);

    // ========== 航点切换判断（工业级双重冗余） ==========
    // 使用单位方向向量点积计算 progress（沿航线方向的投影距离）
    double n_k_x = std::cos(alpha_k);
    double n_k_y = std::sin(alpha_k);
    double dx_wp = x - wp_next.x;
    double dy_wp = y - wp_next.y;
    double progress = dx_wp * n_k_x + dy_wp * n_k_y;  // >0 表示已越过航点法平面

    auto lookup_double = [](const std::vector<double>& values, int index, double fallback) {
        if (index >= 0 && index < static_cast<int>(values.size()) && std::isfinite(values[index]) && values[index] > 0.0) {
            return values[index];
        }
        return fallback;
    };
    auto lookup_mode = [](const std::vector<std::string>& values, int index, bool is_last) {
        if (index >= 0 && index < static_cast<int>(values.size()) && !values[index].empty()) {
            return values[index];
        }
        return is_last ? std::string("final_hold") : std::string("overrun");
    };
    auto lookup_string = [](const std::vector<std::string>& values, int index, const std::string& fallback) {
        if (index >= 0 && index < static_cast<int>(values.size()) && !values[index].empty()) {
            return values[index];
        }
        return fallback;
    };
    auto waypoint_speed_limit = [&](int waypoint_idx, double fallback) {
        if (waypoint_idx >= 0 && waypoint_idx < static_cast<int>(waypoints_.size()) &&
            waypoints_[waypoint_idx].speed_override_is_route_limit &&
            std::isfinite(waypoints_[waypoint_idx].speed_override) &&
            waypoints_[waypoint_idx].speed_override > 0.1) {
            return waypoints_[waypoint_idx].speed_override;
        }
        return fallback;
    };
    auto routeplan_segment_speed_cap = [&](int target_index, double fallback) {
        if (target_index < 0 || target_index >= static_cast<int>(waypoints_.size())) {
            return fallback;
        }
        double limit = fallback;
        limit = std::min(limit, waypoint_speed_limit(target_index, fallback));
        if (target_index > 0) {
            limit = std::min(limit, waypoint_speed_limit(target_index - 1, fallback));
        }
        return limit;
    };
    auto effective_segment_speed_limit = [&](int target_index, double fallback) {
        const double configured_limit = lookup_double(wp_speed_limit_mps_, target_index, fallback);
        const double routeplan_limit = routeplan_segment_speed_cap(target_index, fallback);
        return std::min(configured_limit, routeplan_limit);
    };

    bool is_last_wp = (target_wp_idx >= (int)waypoints_.size() - 1);
    auto waypoint_navigation_mode = [&](int idx) {
        if (idx >= 0 && idx < static_cast<int>(waypoints_.size()) &&
            !waypoints_[idx].navigation_mode.empty()) {
            return waypoints_[idx].navigation_mode;
        }
        return lookup_string(wp_navigation_modes_, idx, "");
    };
    const std::string target_navigation_mode = waypoint_navigation_mode(target_wp_idx);
    const std::string previous_navigation_mode = waypoint_navigation_mode(target_wp_idx - 1);
    const bool target_is_emergency_avoidance =
        is_emergency_avoidance_mode(target_navigation_mode);
    const bool previous_is_emergency_avoidance =
        is_emergency_avoidance_mode(previous_navigation_mode);
    const bool emergency_avoidance_active =
        target_is_emergency_avoidance || previous_is_emergency_avoidance;
    const bool final_waypoint_requires_dp =
        !waypoints_.empty() &&
        is_dp_navigation_mode(waypoint_navigation_mode(static_cast<int>(waypoints_.size()) - 1));
    const double current_speed_for_stop = std::hypot(current_u_, current_v_);
    const double terminal_segment_speed_limit =
        effective_segment_speed_limit(target_wp_idx, max_speed_);
    const double terminal_speed_for_stop =
        (is_last_wp && final_waypoint_requires_dp)
            ? std::max(current_speed_for_stop, terminal_segment_speed_limit)
            : current_speed_for_stop;
    const double dynamic_final_stop_dist = std::clamp(
        (terminal_speed_for_stop * terminal_speed_for_stop) /
            (2.0 * std::max(final_dp_stop_decel_mps2_, 0.01)),
        final_dp_stop_min_lpp_ * Lpp_,
        final_dp_stop_max_lpp_ * Lpp_);
    const double terminal_slow_down_dist = std::max(
        (is_last_wp && final_waypoint_requires_dp)
            ? std::max(final_dp_slow_down_dist_, dynamic_final_stop_dist)
            : slow_down_dist_,
        1.0);
    if (is_last_wp && final_waypoint_requires_dp) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[FINAL STOP DIST] U=%.2fm/s ref=%.2fm/s stop_dist=%.1fm base=%.1fm min=%.1fL max=%.1fL terminal=%.1fm",
            current_speed_for_stop, terminal_speed_for_stop,
            dynamic_final_stop_dist, final_dp_slow_down_dist_,
            final_dp_stop_min_lpp_, final_dp_stop_max_lpp_, terminal_slow_down_dist);
    }
    std::string switch_mode = lookup_mode(wp_switch_modes_, target_wp_idx, is_last_wp);
    if (!is_last_wp && target_is_emergency_avoidance) {
        switch_mode = "fly_by";
    }
    double active_capture_radius = lookup_double(
        wp_switch_radius_m_,
        target_wp_idx,
        is_last_wp ? capture_radius_ : intermediate_capture_radius_);
    bool very_close = (dist_to_wp < active_capture_radius);
    bool past_waypoint = (progress > 0.0) && (seg_len > 1e-6);

    if (is_last_wp && dp_mode_active_) {
        psi_cmd = dp_hold_psi_;
        u_cmd = 0.0;

        std_msgs::msg::Float64 speed_msg;
        speed_msg.data = 0.0;
        target_speed_pub_->publish(speed_msg);

        std_msgs::msg::Float64 heading_msg;
        heading_msg.data = psi_cmd;
        heading_setpoint_pub_->publish(heading_msg);

        geometry_msgs::msg::PoseStamped pose_msg;
        pose_msg.header.stamp = this->now();
        pose_msg.header.frame_id = "odom";
        pose_msg.pose.position.x = dp_hold_x_;
        pose_msg.pose.position.y = dp_hold_y_;
        pose_msg.pose.position.z = current_cross_track_error_;
        tf2::Quaternion q;
        q.setRPY(0, 0, psi_cmd);
        pose_msg.pose.orientation = tf2::toMsg(q);
        target_pose_pub_->publish(pose_msg);

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[LOS] Final waypoint DP latched: target=(%.1f,%.1f) current=(%.1f,%.1f) latch=%d",
            dp_hold_x_, dp_hold_y_, x, y, final_dp_latched_ ? 1 : 0);
        return;
    }

    // 航线计划门限：
    // - must_pass/overrun: 进圈或越过航点法平面后切换；
    // - fly_by: 按航线计划给出的 wheel-over 距离提前切换，避免高速船追逐顶点。
    double overrun_radius = is_last_wp
        ? 150.0
        : std::max(intermediate_overrun_radius_, active_capture_radius);
    const double remaining_along = seg_len - s;
    const double configured_wheel_over_distance = lookup_double(
        wp_wheel_over_distance_m_, target_wp_idx, 0.0);
    const bool next_wp_is_generated_turn_entry =
        !is_last_wp &&
        turn_arc_auto_flyby_enabled_ &&
        (wp_next.is_arc_point ||
         (wp_next.turn_angle_deg >= turn_arc_min_angle_deg_ &&
          target_wp_idx + 1 < static_cast<int>(waypoints_.size()) &&
          waypoints_[target_wp_idx + 1].is_arc_point));
    const double arc_auto_wheel_over_distance =
        next_wp_is_generated_turn_entry
            ? std::min(
                turn_arc_wheel_over_distance_m_,
                std::max(active_capture_radius, seg_len * turn_arc_wheel_over_max_segment_ratio_))
            : 0.0;
    const double emergency_wheel_over_distance =
        (!is_last_wp && target_is_emergency_avoidance)
            ? emergency_avoidance_wheel_over_distance_m_
            : 0.0;
    const double wheel_over_distance =
        std::max(
            std::max(configured_wheel_over_distance, arc_auto_wheel_over_distance),
            emergency_wheel_over_distance);
    const double default_switch_max_xte =
        emergency_avoidance_active
            ? emergency_avoidance_switch_max_xte_m_
            : std::max(active_capture_radius, Lpp_);
    const double switch_max_xte = lookup_double(
        wp_switch_max_xte_m_, target_wp_idx, default_switch_max_xte);
    const double missed_after_distance = lookup_double(
        wp_missed_after_distance_m_, target_wp_idx, std::max(active_capture_radius, 0.5 * Lpp_));
    const double switch_max_heading_error_deg = lookup_double(
        wp_switch_max_heading_error_deg_, target_wp_idx, 0.0);
    const double switch_max_speed = lookup_double(
        wp_switch_max_speed_mps_, target_wp_idx, 0.0);
    const double U_current_gate = std::hypot(current_u_, current_v_);
    const bool switch_lateral_ready = (std::abs(e) <= switch_max_xte);
    double switch_heading_error_deg = 0.0;
    bool switch_heading_ready = true;
    if (switch_max_heading_error_deg > 0.0 &&
        target_wp_idx + 1 < static_cast<int>(waypoints_.size())) {
        const Waypoint wp_after = waypoints_[target_wp_idx + 1];
        const double next_leg_angle =
            compute_path_angle(wp_next.x, wp_next.y, wp_after.x, wp_after.y);
        switch_heading_error_deg =
            std::abs(normalize_angle_pi(next_leg_angle - current_yaw_)) * 180.0 / M_PI;
        switch_heading_ready = (switch_heading_error_deg <= switch_max_heading_error_deg);
    }
    const bool switch_speed_ready =
        (switch_max_speed <= 0.0) || (U_current_gate <= switch_max_speed);
    const bool switch_policy_ready = switch_heading_ready && switch_speed_ready;
    const bool fly_by_mode = (switch_mode == "fly_by" || switch_mode == "flyby");
    const bool fly_by_style_mode = fly_by_mode || next_wp_is_generated_turn_entry;
    const bool internal_reference_mode = (switch_mode == "internal_reference");
    const bool overrun_mode =
        (switch_mode == "overrun" || switch_mode == "must_pass" || internal_reference_mode);
    const bool flyby_zone =
        fly_by_style_mode &&
        (seg_len > 1e-6) &&
        (s >= 0.0) &&
        (remaining_along <= std::max(wheel_over_distance, active_capture_radius));
    const bool missed_flyby_zone =
        fly_by_style_mode &&
        (seg_len > 1e-6) &&
        (remaining_along < -missed_after_distance);
    const bool missed_overrun_zone =
        overrun_mode &&
        (seg_len > 1e-6) &&
        (remaining_along < -missed_after_distance);
    const bool internal_reference_progress_ready =
        internal_reference_mode &&
        (past_waypoint || missed_overrun_zone);
    const bool flyby_switch =
        flyby_zone &&
        switch_lateral_ready &&
        switch_policy_ready;
    const bool missed_flyby_switch =
        missed_flyby_zone &&
        switch_lateral_ready &&
        switch_policy_ready;
    const bool missed_overrun_switch =
        missed_overrun_zone &&
        (switch_lateral_ready || internal_reference_mode) &&
        switch_policy_ready;
    bool over_plane = (s >= seg_len) && (dist_to_wp < overrun_radius);
    const bool forced_missed_overrun_switch =
        !is_last_wp && missed_overrun_zone && switch_policy_ready;
    bool forced_switch =
        (seg_len > 1e-6) &&
        ((s > seg_len * 1.5) || forced_missed_overrun_switch);
    const bool switch_geometry_ready =
        very_close || flyby_zone || missed_flyby_zone || missed_overrun_zone ||
        over_plane || internal_reference_progress_ready;
    static rclcpp::Time last_switch{0,0,RCL_ROS_TIME};
    double since_switch = (this->now() - last_switch).seconds();
    bool cooldown_ok =
        (since_switch > 3.0) ||
        internal_reference_progress_ready ||
        forced_switch;
    const bool short_arc_mismatch_switch =
        !is_last_wp &&
        fly_by_style_mode &&
        next_wp_is_generated_turn_entry &&
        (seg_len > 1e-6) &&
        (remaining_along <= std::max(wheel_over_distance, active_capture_radius)) &&
        (std::abs(e) > switch_max_xte) &&
        (std::abs(e) >= std::min(
            std::max(0.75 * seg_len, switch_max_xte + 5.0),
            std::max(far_xte_rejoin_release_m_ + 5.0, far_xte_rejoin_threshold_m_)));
    bool can_switch =
        switch_geometry_ready &&
        (switch_lateral_ready || forced_switch || internal_reference_progress_ready || short_arc_mismatch_switch) &&
        switch_policy_ready &&
        cooldown_ok;
    const bool route_gate_blocked =
        switch_geometry_ready &&
        cooldown_ok &&
        ((!switch_lateral_ready && !short_arc_mismatch_switch) || !switch_policy_ready);
    if (short_arc_mismatch_switch) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[SHORT ARC SKIP] wp[%d] seg=%.1fm rem=%.1fm xte=%.1fm gate=%.1fm wheel=%.1fm -> switch to next segment for rejoin",
            target_wp_idx, seg_len, remaining_along, e, switch_max_xte, wheel_over_distance);
    }

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
        "[ROUTE GATE] wp[%d] mode=%s nav=%s dist=%.1fm rem=%.1fm e=%.1fm gate=%.1fm wheel=%.1fm max_xte=%.1fm lat=%d missed_after=%.1fm hdg_err=%.1f/%.1fdeg speed=%.2f/%.2fmps policy=%d close=%d flyby=%d arc_auto=%d emergency=%d missed=%d missed_over=%d over=%d blocked=%d",
        target_wp_idx, switch_mode.c_str(), target_navigation_mode.c_str(),
        dist_to_wp, remaining_along, e,
        active_capture_radius, wheel_over_distance, switch_max_xte,
        switch_lateral_ready ? 1 : 0,
        missed_after_distance,
        switch_heading_error_deg, switch_max_heading_error_deg,
        U_current_gate, switch_max_speed,
        switch_policy_ready ? 1 : 0,
        very_close ? 1 : 0, flyby_switch ? 1 : 0,
        next_wp_is_generated_turn_entry ? 1 : 0,
        emergency_avoidance_active ? 1 : 0,
        missed_flyby_switch ? 1 : 0,
        missed_overrun_switch ? 1 : 0,
        over_plane ? 1 : 0, route_gate_blocked ? 1 : 0);

    bool final_dp_waiting_for_safe_handoff = false;
    bool final_dp_waiting_lateral_ready = false;
    bool final_dp_terminal_point_capture = false;
    bool final_dp_overrun_stop_active = false;

    if (is_last_wp) {
        // 最终航点：进入捕获圈或越过终点即触发
        const double final_capture_radius = std::max(final_capture_radius_, 1.0);
        const double final_stop_radius = std::clamp(final_stop_radius_, 1.0, final_capture_radius);
        const double final_handoff_speed = std::clamp(final_handoff_speed_, 0.05, max_speed_);
        const double final_capture_max_xte = std::max(final_capture_max_cross_track_m_, 1.0);
        const double final_completion_radius = final_capture_radius;
        double U_current_capture = std::hypot(current_u_, current_v_);
        bool final_close = (dist_to_wp <= final_stop_radius);
        bool final_low_speed = (U_current_capture < final_handoff_speed);
        bool final_lateral_ready = (std::abs(e) < final_capture_max_xte);
        const bool final_stop_circle_ready =
            final_waypoint_requires_dp &&
            (dist_to_wp <= final_stop_radius);
        const bool final_overrun_geometry =
            final_waypoint_requires_dp &&
            (seg_len > 1e-6) &&
            (s >= seg_len || remaining_along < 0.0);
        const bool final_over_plane_capture_ready =
            (seg_len > 1e-6) &&
            (s >= seg_len) &&
            (dist_to_wp <= final_stop_radius);
        const bool final_missed_capture_ready =
            (seg_len > 1e-6) &&
            (remaining_along < -missed_after_distance) &&
            (dist_to_wp <= final_stop_radius);
        const bool final_endpoint_capture_geometry_ready =
            final_stop_circle_ready ||
            final_over_plane_capture_ready ||
            final_missed_capture_ready;
        double final_env_force_norm = 0.0;
        double final_env_age_s = 0.0;
        double final_weather_heading = current_yaw_;
        const bool final_weather_handoff_active =
            final_waypoint_requires_dp &&
            dp_weathervane_strict_handoff_enabled_ &&
            select_dp_weathervane_heading(
                current_yaw_, final_weather_heading,
                final_env_force_norm, final_env_age_s);
        const double final_weather_handoff_xte = std::min(
            final_capture_max_xte,
            std::max(1.0, dp_weathervane_handoff_max_cross_track_m_));
        const double final_weather_handoff_speed = std::clamp(
            dp_weathervane_handoff_speed_mps_,
            0.1,
            std::max(max_speed_, 0.1));
        const bool final_weather_low_speed =
            U_current_capture <= final_weather_handoff_speed;
        const bool final_weather_lateral_ready =
            std::abs(e) <= final_weather_handoff_xte;
        const bool final_weather_safe_handoff =
            !final_weather_handoff_active ||
            (final_weather_low_speed && final_weather_lateral_ready);
        const bool final_endpoint_capture_ready =
            final_endpoint_capture_geometry_ready &&
            final_weather_safe_handoff &&
            final_low_speed &&
            final_lateral_ready;
        const bool final_weather_endpoint_blocked =
            final_endpoint_capture_geometry_ready &&
            final_weather_handoff_active &&
            !final_weather_safe_handoff;
        final_dp_overrun_stop_active =
            final_overrun_geometry &&
            !final_endpoint_capture_ready &&
            final_weather_safe_handoff;
        const bool final_emergency_geometry_ready =
            (seg_len > 1e-6) &&
            !final_endpoint_capture_ready &&
            (s >= seg_len && dist_to_wp <= final_completion_radius);
        const bool final_emergency_stop_ready =
            final_emergency_geometry_ready &&
            final_low_speed &&
            final_lateral_ready;
        const bool final_dp_mode_forced_ready =
            final_waypoint_requires_dp &&
            final_endpoint_capture_ready;
        final_dp_waiting_for_safe_handoff =
            final_waypoint_requires_dp &&
            (final_close ||
             final_emergency_geometry_ready ||
             final_weather_endpoint_blocked ||
             (seg_len > 1e-6 && s >= seg_len) ||
             (remaining_along < -missed_after_distance)) &&
            !final_endpoint_capture_ready &&
            !final_emergency_stop_ready;
        final_dp_waiting_lateral_ready = final_lateral_ready;
        final_dp_terminal_point_capture =
            final_waypoint_requires_dp &&
            !final_dp_overrun_stop_active &&
            !final_endpoint_capture_ready &&
            (dist_to_wp <= final_completion_radius);

        if (final_dp_overrun_stop_active) {
            final_dp_overrun_braking_active_ = true;
            const bool final_overrun_low_speed_handoff =
                final_low_speed &&
                final_lateral_ready &&
                final_weather_safe_handoff;
            if (final_weather_handoff_active || final_overrun_low_speed_handoff) {
                dp_mode_active_ = true;
                final_dp_latched_ = true;
            }
            if (!final_dp_hold_initialized_) {
                dp_hold_x_ = x;
                dp_hold_y_ = y;
                double env_force_norm = 0.0;
                double env_age_s = 0.0;
                const bool weather_heading_active = select_dp_weathervane_heading(
                    current_yaw_, dp_hold_psi_, env_force_norm, env_age_s);
                final_dp_hold_initialized_ = true;
                final_dp_overrun_hold_initialized_ = true;
                RCLCPP_WARN(this->get_logger(),
                    "[FINAL DP WEATHER-VANE] overrun hold psi=%.1fdeg weather=%d env=%.0fN age=%.2fs",
                    dp_hold_psi_ * 180.0 / M_PI,
                    weather_heading_active ? 1 : 0, env_force_norm, env_age_s);
            }

            psi_cmd = dp_hold_psi_;
            u_cmd = 0.0;

            std_msgs::msg::Float64 speed_msg;
            speed_msg.data = 0.0;
            target_speed_pub_->publish(speed_msg);

            std_msgs::msg::Float64 heading_msg;
            heading_msg.data = psi_cmd;
            heading_setpoint_pub_->publish(heading_msg);

            geometry_msgs::msg::PoseStamped pose_msg;
            pose_msg.header.stamp = this->now();
            pose_msg.header.frame_id = "odom";
            pose_msg.pose.position.x = dp_hold_x_;
            pose_msg.pose.position.y = dp_hold_y_;
            pose_msg.pose.position.z = current_cross_track_error_;
            tf2::Quaternion q;
            q.setRPY(0, 0, dp_hold_psi_);
            pose_msg.pose.orientation = tf2::toMsg(q);
            target_pose_pub_->publish(pose_msg);

            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[FINAL DP OVERRUN BRAKE] endpoint_target=1 frozen=%d latch=%d dist=%.2fm e=%.2fm U=%.2fm/s rem=%.1fm hold=(%.1f,%.1f) psi=%.1fdeg",
                final_dp_overrun_hold_initialized_ ? 1 : 0,
                final_dp_latched_ ? 1 : 0,
                dist_to_wp, e, U_current_capture, remaining_along,
                dp_hold_x_, dp_hold_y_, dp_hold_psi_ * 180.0 / M_PI);
            return;
        }

        if (final_dp_waiting_for_safe_handoff) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[FINAL DP GATE] waiting safe handoff: dist=%.2fm e=%.2fm U=%.2fm/s low_speed=%d lateral=%d handoff_limit=%.2fm/s xte_limit=%.2fm rem=%.1fm radius=%.1fm weather=%d weather_safe=%d env=%.0fN age=%.2fs weather_speed=%d weather_lateral=%d weather_speed_limit=%.2fm/s weather_xte_limit=%.2fm",
                dist_to_wp, e, U_current_capture,
                final_low_speed ? 1 : 0,
                final_lateral_ready ? 1 : 0,
                final_handoff_speed, final_capture_max_xte,
                remaining_along, final_completion_radius,
                final_weather_handoff_active ? 1 : 0,
                final_weather_safe_handoff ? 1 : 0,
                final_env_force_norm, final_env_age_s,
                final_weather_low_speed ? 1 : 0,
                final_weather_lateral_ready ? 1 : 0,
                final_weather_handoff_speed,
                final_weather_handoff_xte);
        }

        const bool final_weather_stop_for_handoff =
            final_waypoint_requires_dp &&
            final_weather_handoff_active &&
            !final_weather_safe_handoff &&
            final_weather_lateral_ready &&
            (final_endpoint_capture_geometry_ready || final_overrun_geometry);
        if (final_weather_stop_for_handoff) {
            final_dp_overrun_braking_active_ = true;
            integral_e_ = 0.0;
            beta_hat_ = 0.0;
            psi_cmd = alpha_k;
            u_cmd = 0.0;

            std_msgs::msg::Float64 speed_msg;
            speed_msg.data = 0.0;
            target_speed_pub_->publish(speed_msg);

            std_msgs::msg::Float64 heading_msg;
            heading_msg.data = psi_cmd;
            heading_setpoint_pub_->publish(heading_msg);

            geometry_msgs::msg::PoseStamped pose_msg;
            pose_msg.header.stamp = this->now();
            pose_msg.header.frame_id = "odom";
            pose_msg.pose.position.x = waypoints_.back().x;
            pose_msg.pose.position.y = waypoints_.back().y;
            pose_msg.pose.position.z = current_cross_track_error_;
            tf2::Quaternion q;
            q.setRPY(0, 0, psi_cmd);
            pose_msg.pose.orientation = tf2::toMsg(q);
            target_pose_pub_->publish(pose_msg);

            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[FINAL DP WEATHER BRAKE] stop before weather-vane handoff: dist=%.2fm e=%.2fm U=%.2fm/s rem=%.1fm overrun=%d geo_ready=%d keep_channel=%.1fdeg weather_target=%.1fdeg env=%.0fN age=%.2fs",
                dist_to_wp, e, U_current_capture, remaining_along,
                final_overrun_geometry ? 1 : 0,
                final_endpoint_capture_geometry_ready ? 1 : 0,
                psi_cmd * 180.0 / M_PI,
                final_weather_heading * 180.0 / M_PI,
                final_env_force_norm, final_env_age_s);
            return;
        }

        const bool final_speed_stop_for_handoff =
            final_waypoint_requires_dp &&
            final_endpoint_capture_geometry_ready &&
            final_weather_safe_handoff &&
            !final_endpoint_capture_ready;
        if (final_speed_stop_for_handoff) {
            final_dp_overrun_braking_active_ = true;
            integral_e_ = 0.0;
            beta_hat_ = 0.0;
            psi_cmd = alpha_k;
            u_cmd = 0.0;

            std_msgs::msg::Float64 speed_msg;
            speed_msg.data = 0.0;
            target_speed_pub_->publish(speed_msg);

            std_msgs::msg::Float64 heading_msg;
            heading_msg.data = psi_cmd;
            heading_setpoint_pub_->publish(heading_msg);

            geometry_msgs::msg::PoseStamped pose_msg;
            pose_msg.header.stamp = this->now();
            pose_msg.header.frame_id = "odom";
            pose_msg.pose.position.x = waypoints_.back().x;
            pose_msg.pose.position.y = waypoints_.back().y;
            pose_msg.pose.position.z = current_cross_track_error_;
            tf2::Quaternion q;
            q.setRPY(0, 0, psi_cmd);
            pose_msg.pose.orientation = tf2::toMsg(q);
            target_pose_pub_->publish(pose_msg);

            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                "[FINAL DP SPEED BRAKE] stop before DP handoff: dist=%.2fm e=%.2fm U=%.2fm/s rem=%.1fm low_speed=%d lateral=%d handoff_limit=%.2fm/s xte_limit=%.2fm keep_channel=%.1fdeg",
                dist_to_wp, e, U_current_capture, remaining_along,
                final_low_speed ? 1 : 0,
                final_lateral_ready ? 1 : 0,
                final_handoff_speed, final_capture_max_xte,
                psi_cmd * 180.0 / M_PI);
            return;
        }

        if (final_endpoint_capture_ready || final_emergency_stop_ready || final_dp_mode_forced_ready) {
            dp_mode_active_ = true;
            final_dp_latched_ = true;
            final_dp_overrun_braking_active_ = true;
            dp_hold_x_ = x;
            dp_hold_y_ = y;
            double env_force_norm = 0.0;
            double env_age_s = 0.0;
            const bool weather_heading_active = select_dp_weathervane_heading(
                current_yaw_, dp_hold_psi_, env_force_norm, env_age_s);
            final_dp_hold_initialized_ = true;

            RCLCPP_INFO(this->get_logger(),
                "[LOS] Final waypoint DP handoff: mode=%s nav_mode=%s forced_dp=%d dist=%.2fm e=%.2fm U=%.2fm/s handoff_limit=%.2fm/s xte_limit=%.2fm past=%d over_plane=%d missed=%d rem=%.1fm capture_radius=%.1fm stop_radius=%.1fm hold=(%.1f,%.1f) psi=%.1fdeg weather=%d env=%.0fN age=%.2fs weather_safe=%d",
                "endpoint_capture",
                waypoint_navigation_mode(static_cast<int>(waypoints_.size()) - 1).c_str(),
                final_dp_mode_forced_ready ? 1 : 0,
                dist_to_wp, e, U_current_capture, final_handoff_speed,
                final_capture_max_xte, past_waypoint ? 1 : 0,
                final_over_plane_capture_ready ? 1 : 0, final_missed_capture_ready ? 1 : 0,
                remaining_along, final_completion_radius, final_stop_radius, dp_hold_x_, dp_hold_y_,
                dp_hold_psi_ * 180.0 / M_PI,
                weather_heading_active ? 1 : 0, env_force_norm, env_age_s,
                final_weather_safe_handoff ? 1 : 0);
            RCLCPP_INFO(this->get_logger(), "[LOS] ★ 抵达最终航点捕获圈！切换到 DP 保持模式");

            // 【最终航点DP保持修复】：进入捕获半径后，停车并保持当前位置
            psi_cmd = dp_hold_psi_;  // 保持航段终点切线方向
            u_cmd = 0.0;             // 停车

            // 发布DP保持指令：先发speed=0，再发heading（确保control先收到speed再处理heading）
            std_msgs::msg::Float64 speed_msg;
            speed_msg.data = 0.0;
            target_speed_pub_->publish(speed_msg);

            std_msgs::msg::Float64 heading_msg;
            heading_msg.data = psi_cmd;
            heading_setpoint_pub_->publish(heading_msg);

            // 发布当前位置作为DP保持目标
            geometry_msgs::msg::PoseStamped pose_msg;
            pose_msg.header.stamp = this->now();
            pose_msg.header.frame_id = "odom";
            pose_msg.pose.position.x = dp_hold_x_;
            pose_msg.pose.position.y = dp_hold_y_;
            pose_msg.pose.position.z = current_cross_track_error_;
            tf2::Quaternion q;
            q.setRPY(0, 0, dp_hold_psi_);
            pose_msg.pose.orientation = tf2::toMsg(q);
            target_pose_pub_->publish(pose_msg);
            return;
        }
    } else if (can_switch) {
         // 切换航点
         RCLCPP_INFO(this->get_logger(),
            "[LOS] 切换航点: mode=%s dist=%.2fm radius=%.1fm rem=%.1fm s=%.1f/%.1f [wp%d -> wp%d]",
             switch_mode.c_str(), dist_to_wp, active_capture_radius, remaining_along,
             s, seg_len, current_wp_idx_, current_wp_idx_+1);
         
         last_switch = this->now();
         current_wp_idx_++;
         integral_e_ = 0.0;
         if (reset_sideslip_on_waypoint_switch_) {
             beta_hat_ = 0.0;
         }
         psi_cmd_prev_ = current_yaw_;

         if (current_wp_idx_ >= (int)waypoints_.size()) {
             psi_cmd = current_yaw_;
             u_cmd = 0.0;
             return;
         }
         // Use new segment angle, next cycle handles LOS naturally
         psi_cmd = compute_path_angle(waypoints_[current_wp_idx_-1].x, waypoints_[current_wp_idx_-1].y,
                                       waypoints_[current_wp_idx_].x, waypoints_[current_wp_idx_].y);
         psi_cmd = apply_heading_rate_limit(psi_cmd, cmd_dt);
         // Keep steerage during intermediate waypoint handoff. Leaving u_cmd unset
         // publishes speed=0 for one cycle, which falsely triggers DP_POSITION.
         double U_current_switch = std::hypot(current_u_, current_v_);
         const double next_segment_speed_limit =
             effective_segment_speed_limit(current_wp_idx_, max_speed_);
         u_cmd = std::clamp(
             U_current_switch,
             minimum_steerage_speed_,
             std::max(minimum_steerage_speed_, next_segment_speed_limit));
         return;
     }

    // ========== 混合导引律：Homing + ILOS ==========
    // 航道模式把控制误差从“相对中心线横偏”改为“超出允许航道的横偏”。
    // 船进入航道后保持航段方向，不再为了压中心线而反复越线。
    const bool final_alignment_zone =
        is_last_wp && (route_remaining_to_final < terminal_slow_down_dist || dist_to_wp < terminal_slow_down_dist);
    const bool corridor_base_enabled =
        corridor_guidance_enabled_ && corridor_half_width_m_ > 1e-6 && !final_alignment_zone;
    bool turn_entry_centerline_active = false;
    double distance_to_turn_entry = std::numeric_limits<double>::infinity();
    if (turn_entry_centerline_enabled_ && turn_entry_centerline_distance_m_ > 1.0 &&
        corridor_base_enabled && waypoints_.size() >= 3) {
        auto is_turn_entry_wp = [&](int idx) {
            if (idx < 0 || idx >= static_cast<int>(waypoints_.size())) {
                return false;
            }
            const auto& wp = waypoints_[idx];
            if (wp.is_arc_point) {
                return true;
            }
            if (idx <= 0 || idx + 1 >= static_cast<int>(waypoints_.size())) {
                return false;
            }
            const double a1 = compute_path_angle(
                waypoints_[idx - 1].x, waypoints_[idx - 1].y,
                waypoints_[idx].x, waypoints_[idx].y);
            const double a2 = compute_path_angle(
                waypoints_[idx].x, waypoints_[idx].y,
                waypoints_[idx + 1].x, waypoints_[idx + 1].y);
            const double turn_deg = std::abs(normalize_angle_pi(a2 - a1)) * 180.0 / M_PI;
            return turn_deg >= turn_entry_centerline_min_angle_deg_;
        };

        double lookahead_dist = std::max(0.0, seg_len - std::clamp(s, 0.0, seg_len));
        for (int idx = target_wp_idx; idx < static_cast<int>(waypoints_.size()); ++idx) {
            if (is_turn_entry_wp(idx)) {
                distance_to_turn_entry = lookahead_dist;
                turn_entry_centerline_active =
                    lookahead_dist <= turn_entry_centerline_distance_m_ &&
                    std::abs(e) > turn_entry_centerline_release_xte_m_;
                break;
            }
            if (lookahead_dist > turn_entry_centerline_distance_m_) {
                break;
            }
            if (idx + 1 < static_cast<int>(waypoints_.size())) {
                lookahead_dist += std::hypot(
                    waypoints_[idx + 1].x - waypoints_[idx].x,
                    waypoints_[idx + 1].y - waypoints_[idx].y);
            }
        }
    }
    struct ExternalRouteTurnContext {
        bool enabled{false};
        bool upcoming{false};
        bool recent{false};
        bool centerline_required{false};
        double first_turn_distance_m{std::numeric_limits<double>::infinity()};
        double recent_turn_distance_m{std::numeric_limits<double>::infinity()};
        double upcoming_cumulative_deg{0.0};
        double recent_cumulative_deg{0.0};
        double preview_heading_rad{std::numeric_limits<double>::quiet_NaN()};
    };

    ExternalRouteTurnContext external_turn_context;
    const bool external_dense_route =
        external_route_turn_preview_enabled_ &&
        turn_arc_external_route_min_points_ > 0 &&
        waypoints_.size() >= static_cast<size_t>(turn_arc_external_route_min_points_);
    auto preview_segment_heading = [&](int from_idx, int to_idx) {
        return compute_path_angle(
            waypoints_[from_idx].x, waypoints_[from_idx].y,
            waypoints_[to_idx].x, waypoints_[to_idx].y);
    };
    if (external_dense_route && !is_last_wp && waypoints_.size() >= 3) {
        const double preview_window_m =
            std::max(external_route_turn_preview_distance_m_, Lpp_);
        const double hold_window_m =
            std::max(external_route_turn_hold_distance_m_, Lpp_);
        const double small_turn_threshold_deg =
            std::max(1.0, turn_arc_min_angle_deg_ * 0.25);

        double distance_ahead = std::max(0.0, seg_len - std::clamp(s, 0.0, seg_len));
        double prev_heading = alpha_k;
        for (int idx = target_wp_idx; idx + 1 < static_cast<int>(waypoints_.size()); ++idx) {
            const double next_heading = preview_segment_heading(idx, idx + 1);
            const double delta_deg =
                std::abs(normalize_angle_pi(next_heading - prev_heading)) * 180.0 / M_PI;
            if (delta_deg >= small_turn_threshold_deg) {
                external_turn_context.upcoming_cumulative_deg += delta_deg;
                if (!std::isfinite(external_turn_context.first_turn_distance_m)) {
                    external_turn_context.first_turn_distance_m = distance_ahead;
                    external_turn_context.preview_heading_rad = next_heading;
                }
            }
            distance_ahead += std::hypot(
                waypoints_[idx + 1].x - waypoints_[idx].x,
                waypoints_[idx + 1].y - waypoints_[idx].y);
            prev_heading = next_heading;
            if (distance_ahead > preview_window_m) {
                break;
            }
        }

        double distance_behind = std::max(0.0, std::clamp(s, 0.0, seg_len));
        double next_heading = alpha_k;
        for (int idx = target_wp_idx - 1; idx > 0; --idx) {
            const double previous_heading = preview_segment_heading(idx - 1, idx);
            const double delta_deg =
                std::abs(normalize_angle_pi(next_heading - previous_heading)) * 180.0 / M_PI;
            if (delta_deg >= small_turn_threshold_deg) {
                external_turn_context.recent_cumulative_deg += delta_deg;
                if (!std::isfinite(external_turn_context.recent_turn_distance_m)) {
                    external_turn_context.recent_turn_distance_m = distance_behind;
                }
            }
            distance_behind += std::hypot(
                waypoints_[idx].x - waypoints_[idx - 1].x,
                waypoints_[idx].y - waypoints_[idx - 1].y);
            next_heading = previous_heading;
            if (distance_behind > hold_window_m) {
                break;
            }
        }

        external_turn_context.upcoming =
            external_turn_context.upcoming_cumulative_deg >=
            external_route_turn_min_cumulative_angle_deg_;
        external_turn_context.recent =
            external_turn_context.recent_cumulative_deg >=
            external_route_turn_min_cumulative_angle_deg_;
        external_turn_context.enabled =
            external_turn_context.upcoming || external_turn_context.recent;
        const bool upcoming_centerline_zone =
            external_turn_context.upcoming &&
            external_turn_context.first_turn_distance_m <=
            external_route_turn_centerline_distance_m_;
        external_turn_context.centerline_required =
            (upcoming_centerline_zone || external_turn_context.recent) &&
            std::abs(e) > external_route_turn_centerline_release_xte_m_;
    }

    double command_path_heading = alpha_k;
    if (external_turn_context.upcoming &&
        std::isfinite(external_turn_context.preview_heading_rad) &&
        std::isfinite(external_turn_context.first_turn_distance_m) &&
        external_turn_context.first_turn_distance_m <= external_route_turn_heading_preview_distance_m_) {
        const double preview_ratio = 1.0 - std::clamp(
            external_turn_context.first_turn_distance_m /
                std::max(external_route_turn_heading_preview_distance_m_, 1.0),
            0.0,
            1.0);
        const double smooth_ratio = preview_ratio * preview_ratio * (3.0 - 2.0 * preview_ratio);
        const double preview_delta =
            normalize_angle_pi(external_turn_context.preview_heading_rad - alpha_k);
        command_path_heading = normalize_angle_pi(alpha_k + smooth_ratio * preview_delta);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[EXTERNAL TURN HEADING] alpha=%.1fdeg preview=%.1fdeg cmd=%.1fdeg ratio=%.2f ahead=%.1fm window=%.1fm",
            alpha_k * 180.0 / M_PI,
            external_turn_context.preview_heading_rad * 180.0 / M_PI,
            command_path_heading * 180.0 / M_PI,
            smooth_ratio,
            external_turn_context.first_turn_distance_m,
            external_route_turn_heading_preview_distance_m_);
    }

    const bool corridor_enabled =
        corridor_base_enabled &&
        !turn_entry_centerline_active &&
        !external_turn_context.centerline_required;
    if (turn_entry_centerline_active) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[TURN ENTRY CENTERLINE] corridor=off dist_to_turn=%.1fm xte=%.1fm window=%.1fm release=%.1fm",
            distance_to_turn_entry, e, turn_entry_centerline_distance_m_, turn_entry_centerline_release_xte_m_);
    }
    if (external_turn_context.centerline_required) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[EXTERNAL TURN PREVIEW] corridor=off upcoming=%d recent=%d ahead=%.1fm behind=%.1fm turn=%.1f/%.1fdeg xte=%.1fm centerline_window=%.1fm release=%.1fm",
            external_turn_context.upcoming ? 1 : 0,
            external_turn_context.recent ? 1 : 0,
            external_turn_context.first_turn_distance_m,
            external_turn_context.recent_turn_distance_m,
            external_turn_context.upcoming_cumulative_deg,
            external_turn_context.recent_cumulative_deg,
            e,
            external_route_turn_centerline_distance_m_,
            external_route_turn_centerline_release_xte_m_);
    }
    if (corridor_enabled) {
        const double abs_e = std::abs(e);
        if (corridor_hold_active_) {
            if (abs_e >= corridor_reacquire_width_m_) {
                corridor_hold_active_ = false;
            }
        } else if (abs_e <= corridor_half_width_m_) {
            corridor_hold_active_ = true;
        }
    } else {
        corridor_hold_active_ = false;
    }

    auto corridor_control_error = [&](double raw_e) {
        if (!corridor_enabled) {
            return raw_e;
        }
        const double abs_e = std::abs(raw_e);
        const double excess = std::max(0.0, abs_e - corridor_half_width_m_);
        if (abs_e <= corridor_half_width_m_ || excess <= 1e-6) {
            return 0.0;
        }
        if (abs_e >= corridor_soft_width_m_) {
            return std::copysign(excess, raw_e);
        }
        const double span = std::max(1.0, corridor_soft_width_m_ - corridor_half_width_m_);
        const double t = std::clamp(excess / span, 0.0, 1.0);
        const double smooth = t * t * (3.0 - 2.0 * t);
        return std::copysign(excess * smooth, raw_e);
    };

    const double e_control = corridor_control_error(e);
    current_cross_track_error_ = raw_route_ref.valid ? raw_route_ref.cross_track_error : e;
    double homing_threshold = std::max(homing_threshold_m_, Lpp_);
    if (corridor_enabled) {
        homing_threshold = std::max(homing_threshold, corridor_reacquire_width_m_);
    }
    const double raw_abs_xte_for_rejoin =
        raw_route_ref.valid ? raw_route_ref.abs_cross_track_error : std::abs(e);

    struct CrossForceProjection {
        bool valid{false};
        double age_s{std::numeric_limits<double>::infinity()};
        double cross_force_n{0.0};
        double force_norm_n{0.0};
    };
    auto project_body_force_to_path = [](double fx_body, double fy_body,
                                         double yaw_rad, double path_heading) {
        CrossForceProjection projected;
        const double cos_yaw = std::cos(yaw_rad);
        const double sin_yaw = std::sin(yaw_rad);
        const double fx_world = cos_yaw * fx_body - sin_yaw * fy_body;
        const double fy_world = sin_yaw * fx_body + cos_yaw * fy_body;
        const double path_ex = std::cos(path_heading);
        const double path_ey = std::sin(path_heading);
        projected.valid = true;
        projected.cross_force_n = fy_world * path_ex - fx_world * path_ey;
        projected.force_norm_n = std::hypot(fx_world, fy_world);
        return projected;
    };
    auto current_cross_force_for_heading = [&](double path_heading, double timeout_s) {
        double fx_body = 0.0;
        double fy_body = 0.0;
        double yaw_rad = 0.0;
        double age_s = std::numeric_limits<double>::infinity();
        bool valid = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            fx_body = current_load_fx_n_;
            fy_body = current_load_fy_n_;
            yaw_rad = current_yaw_;
            valid = current_load_received_;
            if (valid) {
                age_s = (this->now() - last_current_load_time_).seconds();
            }
        }
        if (!valid || age_s > timeout_s) {
            return CrossForceProjection{};
        }
        auto projected = project_body_force_to_path(fx_body, fy_body, yaw_rad, path_heading);
        projected.age_s = age_s;
        return projected;
    };
    auto total_env_cross_force_for_heading = [&](double path_heading, double timeout_s) {
        double fx_body = 0.0;
        double fy_body = 0.0;
        double yaw_rad = 0.0;
        double age_s = std::numeric_limits<double>::infinity();
        bool valid = false;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            fx_body = current_env_fx_n_;
            fy_body = current_env_fy_n_;
            yaw_rad = current_yaw_;
            valid = env_load_received_;
            if (valid) {
                age_s = (this->now() - last_env_load_time_).seconds();
            }
        }
        if (!valid || age_s > timeout_s) {
            return CrossForceProjection{};
        }
        auto projected = project_body_force_to_path(fx_body, fy_body, yaw_rad, path_heading);
        projected.age_s = age_s;
        return projected;
    };

    double wind_rejoin_outward_force_n = 0.0;
    double wind_rejoin_factor = 0.0;
    bool wind_rejoin_boost_active = false;
    bool wind_rejoin_load_active = false;
    double current_rejoin_outward_force_n = 0.0;
    double current_rejoin_factor = 0.0;
    bool current_rejoin_boost_active = false;
    double adaptive_rejoin_lookahead_m = std::max(far_xte_rejoin_lookahead_m_, Lpp_);

    const double wind_threshold_ref_error =
        raw_route_ref.valid ? raw_route_ref.cross_track_error : e;
    const double wind_threshold_ref_heading =
        raw_route_ref.valid ? raw_route_ref.path_heading : alpha_k;
    const double wind_threshold_ref_abs_xte =
        raw_route_ref.valid ? raw_route_ref.abs_cross_track_error : std::abs(e);
    const double wind_threshold_ref_release =
        raw_route_ref.valid ? raw_route_rejoin_release_m_ : far_xte_rejoin_release_m_;
    const auto current_rejoin_cross_force = current_cross_force_for_heading(
        wind_threshold_ref_heading, current_rejoin_stale_timeout_s_);
    const bool current_guidance_active =
        current_rejoin_cross_force.valid &&
        current_rejoin_cross_force.force_norm_n >= current_crab_min_force_n_;

    if (current_rejoin_enabled_ && current_guidance_active &&
        wind_threshold_ref_abs_xte > current_rejoin_release_m_ + 1e-6) {
        const double rejoin_sign = std::copysign(1.0, wind_threshold_ref_error);
        current_rejoin_outward_force_n = current_rejoin_cross_force.cross_force_n * rejoin_sign;
        if (current_rejoin_outward_force_n > current_rejoin_min_outward_force_n_) {
            current_rejoin_factor = std::clamp(
                current_rejoin_outward_force_n / current_rejoin_force_ref_n_,
                0.0,
                current_rejoin_max_factor_);
            current_rejoin_boost_active = current_rejoin_factor > 1e-6;
        }
    }

    if (wind_rejoin_enabled_ && !current_guidance_active &&
        wind_threshold_ref_abs_xte > wind_threshold_ref_release + 1e-6) {
        const auto env_rejoin_cross_force = total_env_cross_force_for_heading(
            wind_threshold_ref_heading, wind_rejoin_stale_timeout_s_);
        if (env_rejoin_cross_force.valid) {
            wind_rejoin_load_active =
                env_rejoin_cross_force.force_norm_n >= wind_rejoin_min_outward_force_n_;
            const double rejoin_sign = std::copysign(1.0, wind_threshold_ref_error);
            wind_rejoin_outward_force_n = env_rejoin_cross_force.cross_force_n * rejoin_sign;
            if (wind_rejoin_outward_force_n > wind_rejoin_min_outward_force_n_) {
                wind_rejoin_factor = std::clamp(
                    wind_rejoin_outward_force_n / wind_rejoin_force_ref_n_,
                    0.0,
                    wind_rejoin_max_factor_);
                wind_rejoin_boost_active = wind_rejoin_factor > 1e-6;
            }
        }
    }

    const bool env_rejoin_boost_active = current_rejoin_boost_active || wind_rejoin_boost_active;
    const bool env_rejoin_threshold_active = env_rejoin_boost_active || wind_rejoin_load_active;
    const bool env_rejoin_is_current = current_rejoin_boost_active;
    const double env_rejoin_release_m = env_rejoin_is_current ? current_rejoin_release_m_ : wind_rejoin_release_m_;
    const double env_rejoin_raw_threshold_m = env_rejoin_is_current ? current_rejoin_raw_threshold_m_ : wind_rejoin_raw_threshold_m_;
    const double env_rejoin_far_threshold_m = env_rejoin_is_current ? current_rejoin_far_threshold_m_ : wind_rejoin_far_threshold_m_;
    const double env_rejoin_min_lookahead_m = env_rejoin_is_current ? current_rejoin_min_lookahead_m_ : wind_rejoin_min_lookahead_m_;
    const double env_rejoin_lookahead_gain = env_rejoin_is_current ? current_rejoin_lookahead_gain_ : wind_rejoin_lookahead_gain_;
    const double env_rejoin_factor = env_rejoin_is_current ? current_rejoin_factor : wind_rejoin_factor;
    const double env_rejoin_speed_cap_mps = env_rejoin_is_current ? current_rejoin_speed_cap_mps_ : wind_rejoin_speed_cap_mps_;
    const double env_rejoin_outward_force_n = env_rejoin_is_current ? current_rejoin_outward_force_n : wind_rejoin_outward_force_n;

    const double effective_raw_rejoin_release =
        env_rejoin_threshold_active && env_rejoin_release_m > 0.0
            ? std::min(raw_route_rejoin_release_m_, env_rejoin_release_m)
            : raw_route_rejoin_release_m_;
    const double effective_far_rejoin_release =
        env_rejoin_threshold_active && env_rejoin_release_m > 0.0
            ? std::min(far_xte_rejoin_release_m_, env_rejoin_release_m)
            : far_xte_rejoin_release_m_;
    const double effective_raw_route_rejoin_threshold =
        std::max(
            env_rejoin_threshold_active && env_rejoin_raw_threshold_m > 0.0
                ? std::min(raw_route_rejoin_threshold_m_, env_rejoin_raw_threshold_m)
                : raw_route_rejoin_threshold_m_,
            effective_raw_rejoin_release + 1.0);
    const double far_xte_rejoin_threshold =
        std::max(
            env_rejoin_threshold_active && env_rejoin_far_threshold_m > 0.0
                ? std::min(far_xte_rejoin_threshold_m_, env_rejoin_far_threshold_m)
                : far_xte_rejoin_threshold_m_,
            effective_far_rejoin_release + 1.0);
    const double raw_route_soft_rejoin_enter =
        std::max(effective_raw_rejoin_release + 5.0, corridor_half_width_m_ + 5.0);
    const bool raw_route_soft_rejoin_override =
        raw_route_rejoin_enabled_ && raw_route_ref.valid &&
        raw_abs_xte_for_rejoin >= raw_route_soft_rejoin_enter;

    if (raw_route_rejoin_enabled_ && raw_route_ref.valid) {
        if (raw_route_rejoin_active_) {
            if (raw_abs_xte_for_rejoin <= effective_raw_rejoin_release) {
                raw_route_rejoin_active_ = false;
                xte_rejoin_release_guard_until_sec_ =
                    std::max(xte_rejoin_release_guard_until_sec_,
                        this->now().seconds() + xte_rejoin_release_guard_s_);
                RCLCPP_INFO(this->get_logger(),
                    "[RAW ROUTE RECOVERY] released raw_xte=%.1fm <= %.1fm guard=%.1fs",
                    raw_abs_xte_for_rejoin, effective_raw_rejoin_release,
                    xte_rejoin_release_guard_s_);
            }
        } else if (raw_abs_xte_for_rejoin >= effective_raw_route_rejoin_threshold) {
            raw_route_rejoin_active_ = true;
            RCLCPP_WARN(this->get_logger(),
                "[RAW ROUTE RECOVERY] armed raw_xte=%.1fm enter=%.1fm release=%.1fm raw_seg=%d",
                raw_abs_xte_for_rejoin, effective_raw_route_rejoin_threshold,
                effective_raw_rejoin_release, raw_route_ref.segment_idx);
        }
    } else {
        raw_route_rejoin_active_ = false;
    }
    const bool raw_route_rejoin_override =
        raw_route_rejoin_enabled_ && raw_route_ref.valid && raw_route_rejoin_active_;
    const double abs_xte_for_rejoin =
        (raw_route_rejoin_override || raw_route_soft_rejoin_override) ? raw_abs_xte_for_rejoin : std::abs(e);
    const double rejoin_error =
        (raw_route_rejoin_override || raw_route_soft_rejoin_override) ? raw_route_ref.cross_track_error : e;
    const double rejoin_path_heading =
        (raw_route_rejoin_override || raw_route_soft_rejoin_override) ? raw_route_ref.path_heading : alpha_k;
    const double rejoin_release =
        (raw_route_rejoin_override || raw_route_soft_rejoin_override) ? effective_raw_rejoin_release : effective_far_rejoin_release;
    const bool hard_xte_limit_exceeded =
        xte_hard_limit_m_ > 0.0 &&
        std::max(std::abs(e), raw_abs_xte_for_rejoin) >= xte_hard_limit_m_;
    if (far_xte_rejoin_override_enabled_) {
        if (far_xte_rejoin_active_) {
            if (std::abs(e) <= effective_far_rejoin_release) {
                far_xte_rejoin_active_ = false;
                xte_rejoin_release_guard_until_sec_ =
                    std::max(xte_rejoin_release_guard_until_sec_,
                        this->now().seconds() + xte_rejoin_release_guard_s_);
                RCLCPP_INFO(this->get_logger(),
                    "[XTE RECOVERY] released xte=%.1fm <= %.1fm, resume corridor/course hold guard=%.1fs",
                    std::abs(e), effective_far_rejoin_release, xte_rejoin_release_guard_s_);
            }
        } else if (std::abs(e) >= far_xte_rejoin_threshold || hard_xte_limit_exceeded) {
            far_xte_rejoin_active_ = true;
            RCLCPP_WARN(this->get_logger(),
                "[XTE RECOVERY] armed xte=%.1fm enter=%.1fm release=%.1fm hard=%.1fm",
                std::abs(e), far_xte_rejoin_threshold,
                effective_far_rejoin_release, xte_hard_limit_m_);
        }
    } else {
        far_xte_rejoin_active_ = false;
    }
    const bool xte_guidance_rejoin_override =
        (far_xte_rejoin_override_enabled_ && far_xte_rejoin_active_) ||
        raw_route_rejoin_override ||
        raw_route_soft_rejoin_override;
    const bool xte_speed_cap_rejoin_override =
        (far_xte_rejoin_override_enabled_ && far_xte_rejoin_active_) ||
        raw_route_rejoin_override;
    if (env_rejoin_boost_active && abs_xte_for_rejoin > rejoin_release + 1e-6) {
        adaptive_rejoin_lookahead_m = std::max(
            env_rejoin_min_lookahead_m,
            adaptive_rejoin_lookahead_m / (1.0 + env_rejoin_lookahead_gain * env_rejoin_factor));
    } else {
        wind_rejoin_boost_active = false;
        current_rejoin_boost_active = false;
        wind_rejoin_factor = 0.0;
        current_rejoin_factor = 0.0;
    }

    if (xte_guidance_rejoin_override) {
        const double max_approach_angle =
            std::clamp(
                env_rejoin_is_current
                    ? current_rejoin_max_approach_angle_deg_
                    : (raw_route_soft_rejoin_override && !raw_route_rejoin_override
                        ? std::min(far_xte_rejoin_max_approach_angle_deg_, 10.0)
                        : far_xte_rejoin_max_approach_angle_deg_),
                5.0, 45.0) * M_PI / 180.0;
        const double target_band_error =
            std::copysign(std::max(0.0, abs_xte_for_rejoin - rejoin_release), rejoin_error);
        const double approach_angle = std::clamp(
            std::atan2(-target_band_error, adaptive_rejoin_lookahead_m),
            -max_approach_angle,
            max_approach_angle);
        psi_cmd = normalize_angle_pi(rejoin_path_heading + approach_angle);
        double current_force_ff_correction = 0.0;
        if (env_rejoin_is_current && current_force_ff_enabled_ &&
            current_force_ff_max_angle_deg_ > 1e-6 &&
            std::abs(approach_angle) > 1e-6 &&
            std::hypot(current_u_, current_v_) >= current_force_ff_min_speed_mps_) {
            const double max_ff_rad = current_force_ff_max_angle_deg_ * M_PI / 180.0;
            const double ff_abs = std::clamp(
                env_rejoin_outward_force_n / current_force_ff_force_ref_n_ * max_ff_rad,
                0.0,
                max_ff_rad);
            current_force_ff_correction = std::copysign(ff_abs, approach_angle);
            psi_cmd = normalize_angle_pi(psi_cmd + current_force_ff_correction);
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[CURRENT FORCE FF] outward_force=%.0fN ref=%.0fN ff=%.1fdeg approach=%.1fdeg psi=%.1fdeg",
                env_rejoin_outward_force_n,
                current_force_ff_force_ref_n_,
                current_force_ff_correction * 180.0 / M_PI,
                approach_angle * 180.0 / M_PI,
                psi_cmd * 180.0 / M_PI);
        }
        double current_cog_rejoin_correction = 0.0;
        double current_cog_rejoin_error = 0.0;
        double current_cog_rejoin_heading = 0.0;
        const double current_cog_rejoin_speed = std::hypot(current_u_, current_v_);
        if (env_rejoin_is_current && current_cog_rejoin_enabled_ &&
            current_cog_rejoin_speed >= current_cog_rejoin_min_speed_mps_ &&
            current_cog_rejoin_max_angle_deg_ > 1e-6) {
            const double cos_ship = std::cos(current_yaw_);
            const double sin_ship = std::sin(current_yaw_);
            const double ground_vx = cos_ship * current_u_ - sin_ship * current_v_;
            const double ground_vy = sin_ship * current_u_ + cos_ship * current_v_;
            const double ground_speed = std::hypot(ground_vx, ground_vy);
            if (ground_speed >= current_cog_rejoin_min_speed_mps_) {
                const double desired_cog = normalize_angle_pi(rejoin_path_heading + approach_angle);
                current_cog_rejoin_heading = std::atan2(ground_vy, ground_vx);
                current_cog_rejoin_error = normalize_angle_pi(desired_cog - current_cog_rejoin_heading);
                const double max_correction = current_cog_rejoin_max_angle_deg_ * M_PI / 180.0;
                current_cog_rejoin_correction = std::clamp(
                    current_cog_rejoin_kp_ * current_cog_rejoin_error,
                    -max_correction,
                    max_correction);
                psi_cmd = normalize_angle_pi(psi_cmd + current_cog_rejoin_correction);
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[CURRENT COG REJOIN] cog=%.1fdeg desired=%.1fdeg err=%.1fdeg corr=%.1fdeg psi=%.1fdeg gs=%.2fmps",
                    current_cog_rejoin_heading * 180.0 / M_PI,
                    desired_cog * 180.0 / M_PI,
                    current_cog_rejoin_error * 180.0 / M_PI,
                    current_cog_rejoin_correction * 180.0 / M_PI,
                    psi_cmd * 180.0 / M_PI,
                    ground_speed);
            }
        }
        integral_e_ = 0.0;
        beta_hat_ = 0.0;
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[XTE RECOVERY] xte=%.1fm raw_xte=%.1fm raw=%d soft=%d target_band=%.1fm hard=%d approach=%.1fdeg psi=%.1fdeg",
            e, raw_abs_xte_for_rejoin, raw_route_rejoin_override ? 1 : 0,
            raw_route_soft_rejoin_override ? 1 : 0,
            rejoin_release, hard_xte_limit_exceeded ? 1 : 0,
            approach_angle * 180.0 / M_PI, psi_cmd * 180.0 / M_PI);
        if (env_rejoin_boost_active) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[%s REJOIN] outward_force=%.0fN factor=%.2f lookahead %.1fm->%.1fm speed_cap=%.2fmps",
                env_rejoin_is_current ? "CURRENT" : "WIND",
                env_rejoin_outward_force_n,
                env_rejoin_factor,
                std::max(far_xte_rejoin_lookahead_m_, Lpp_),
                adaptive_rejoin_lookahead_m,
                env_rejoin_speed_cap_mps);
        }
    } else if (std::abs(e) > homing_threshold) {
        // ========== 模式1：Homing（捕获）模式 - 限幅切入 ==========
        // 默认兼容旧的固定 45 度切入；当 homing_lookahead_m > 0 时，
        // 使用 atan2(-e, lookahead) 生成可解释的比例切入角。
        const double max_approach_angle =
            std::clamp(homing_max_approach_angle_deg_, 5.0, 60.0) * M_PI / 180.0;
        const double homing_e = corridor_enabled ? corridor_control_error(e) : e;
        const double homing_abs_e = std::max(std::abs(homing_e), 1e-6);
        double approach_angle = (homing_e > 0.0) ? -max_approach_angle : max_approach_angle;
        if (homing_lookahead_m_ > 1e-6) {
            const double homing_lookahead = std::clamp(homing_lookahead_m_, Lpp_, 20.0 * Lpp_);
            // [Fix-2] 比例式切入角：XTE越大切入角越大，最大不超过 max_approach_angle
            double prop_angle = (homing_abs_e / homing_lookahead) * max_approach_angle;
            approach_angle = std::clamp(
                (homing_e > 0.0) ? -prop_angle : prop_angle,
                -max_approach_angle,
                max_approach_angle);
        }
        
        // 最终指令 = 航线方向 + 切入角（不是直指航点，而是沿航线方向偏移）
        psi_cmd = normalize_angle_pi(command_path_heading + approach_angle);

        // 【关键】清空积分器，防止误差累积导致超调
        integral_e_ = 0.0;

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[LOS] [HOMING 限幅] e=%.1fm ctrl_e=%.1fm threshold=%.1fm lookahead=%.1fm corridor=%d | 切入角: %.1f° | ψ_cmd=%.1f° | wp[%d]",
            std::abs(e), e_control, homing_threshold, homing_lookahead_m_, corridor_enabled ? 1 : 0,
            approach_angle*180/M_PI, psi_cmd*180/M_PI, current_wp_idx_);

    } else {
        // ========== 模式2：ILOS 跟踪模式 ==========
        // 船已靠近航线，开启ILOS进行精确压线和抗流干扰

        double dt = (this->now() - last_time_).seconds();
        if (dt <= 0.0 || dt > 1.0) dt = 0.1;

        // 积分项更新（只在ILOS模式下累加）
        // 【实船级架构修复 1：积分分离 (Integral Separation)】
        // 设定内河道走廊：只有横偏小于 30m（约半个船长），才认为是风流干扰，开始积分
        const double INTEGRAL_CORRIDOR = 30.0;

        if (corridor_enabled && std::abs(e) <= corridor_half_width_m_) {
            integral_e_ *= corridor_integral_decay_;
            beta_hat_ *= corridor_beta_decay_;
        } else if (std::abs(e_control) < INTEGRAL_CORRIDOR) {
            if (e_control * integral_e_ < 0.0) {
                // 如果误差反向了（越过了航线），积分项不能立刻清零，
                // 而是快速衰减，防止指令角突变导致船体甩尾
                integral_e_ *= 0.5;
            } else {
                integral_e_ += e_control * dt;
            }
        } else {
            // 在 30m 开外，船还在切入阶段，强行冻结/清零积分，绝不能让它累积！
            integral_e_ = 0.0;
        }

        // [Task 3] 动态前瞻距离：高速时看得更远，低速时看得更近
        // Δ(U) = Lpp * (Δmin + γ * U)，驾驶员"眼睛原理"
        // 高速（如20节）：Δ ≈ 300m，平滑大转弯
        // 低速（如1节）：Δ ≈ 80m，快速响应路径偏差

        // 【架构级修复：切断速度耦合地雷】
        // 设定绝对不可逾越的最小物理视距，通常为 3 到 5 倍船长
        const double MIN_LOOKAHEAD_DISTANCE = 50.0;  // 船长45m，50m约1.1倍船长

        double U_current = std::sqrt(current_u_ * current_u_ + current_v_ * current_v_);
        U_current = std::max(U_current, 0.1);  // 防除零
        double dynamic_delta = Lpp_ * (delta_min_coeff_ + gamma_lookahead_ * U_current);
        double delta = std::max(dynamic_delta, MIN_LOOKAHEAD_DISTANCE);  // 强制下限钳制！
        delta = std::clamp(delta, 30.0, 500.0);  // 限制在合理范围内
        // [Fix-2] 前瞻距离不超过当前航段60%，保证转弯段修正角足够大
        double max_delta = std::max(Lpp_ * 1.3, std::min(500.0, seg_len * 0.6));
        delta = std::min(delta, max_delta);
        const bool final_approach_active =
            is_last_wp && (route_remaining_to_final < terminal_slow_down_dist || dist_to_wp < terminal_slow_down_dist);
        if (final_approach_active && final_approach_lookahead_m_ > 0.0) {
            const double final_delta = std::clamp(final_approach_lookahead_m_, 30.0, 500.0);
            delta = std::min(delta, final_delta);
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[终端对线] final_lookahead=%.1fm active=1 delta=%.1fm",
                final_delta, delta);
        }
        const double segment_lookahead = lookup_double(wp_lookahead_m_, target_wp_idx, 0.0);
        if (segment_lookahead > 0.0) {
            const double planned_delta = std::clamp(segment_lookahead, 30.0, 500.0);
            delta = planned_delta;
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[航段计划前瞻] wp[%d] lookahead=%.1fm delta=%.1fm",
                target_wp_idx, planned_delta, delta);
        }

        // 【实船级架构修复 2：物理蟹行角钳制 (Anti-Windup)】
        // 真实船舶抗流的极限蟹行角极少超过 15度。
        // 我们反算积分项允许的最大作用力，坚决不准超过这个物理极限！
        double max_crab_angle_rad = 15.0 * M_PI / 180.0;
        double max_int_effect = delta * std::tan(max_crab_angle_rad); // 积分项允许折算的最大虚拟偏差(m)

        // 动态计算积分器的物理天花板
        double max_integral_val = (kappa_ilos_ > 1e-3) ? (max_int_effect / kappa_ilos_) : 0.0;

        // 将积分器死死钳制在物理天花板内（告别了荒谬的 200.0）
        integral_e_ = std::clamp(integral_e_, -max_integral_val, max_integral_val);

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[LOS] [Speed-Adaptive Δ] U=%.2f m/s Δ=%.1f m (Lpp=%.1f * (%.2f + %.2f*%.2f))",
            U_current, delta, Lpp_, delta_min_coeff_, gamma_lookahead_, U_current);

        // 【核心修复】：积分视线法内部的误差抵消项必须加负号！
        // 否则它会产生正反馈，让船不断远离航线
        double chi_ilos = command_path_heading + std::atan2(-(e_control + kappa_ilos_ * integral_e_), delta);
        psi_cmd = normalize_angle_pi(chi_ilos);

        // 漂流角补偿（可选）
        if (use_adaptive_los_ && (!final_approach_active || final_approach_use_adaptive_los_)) {
            psi_cmd -= beta_hat_;
            psi_cmd = normalize_angle_pi(psi_cmd);
            
            // 【架构级修复：Thor I. Fossen 标准 ALOS 漂角观测器（归一化版）】
            // 使用横向误差 e 的归一化投影作为驱动源
            // 物理意义: innovation范围[-1,1]，代表"偏离程度的比例"
            double innovation = e_control / std::sqrt(delta * delta + e_control * e_control + 1e-6);
            beta_hat_ += gamma_alos_ * innovation * dt;
            
            // 工业级防积分饱和限幅 (Sideslip 物理上很少超过 20 度)
            const double MAX_BETA_HAT = 20.0 * M_PI / 180.0;
            beta_hat_ = std::clamp(beta_hat_, -MAX_BETA_HAT, MAX_BETA_HAT);
        } else if (final_approach_active && !final_approach_use_adaptive_los_) {
            beta_hat_ = 0.0;
        }

        const auto current_crab_cross_force = current_cross_force_for_heading(
            command_path_heading, current_rejoin_stale_timeout_s_);
        const bool use_current_crab =
            current_crab_enabled_ &&
            current_crab_cross_force.valid &&
            current_crab_cross_force.force_norm_n >= current_crab_min_force_n_;
        const bool use_wind_crab =
            !use_current_crab &&
            wind_crab_enabled_ && wind_rejoin_enabled_ &&
            !current_guidance_active;
        const double active_crab_min_speed_mps = use_current_crab
            ? current_crab_min_speed_mps_
            : wind_crab_min_speed_mps_;
        const double active_crab_max_angle_deg = use_current_crab
            ? current_crab_max_angle_deg_
            : wind_crab_max_angle_deg_;
        if ((use_current_crab || use_wind_crab) &&
            !final_approach_active &&
            U_current >= active_crab_min_speed_mps &&
            active_crab_max_angle_deg > 1e-6) {
            CrossForceProjection active_crab_force;
            if (use_current_crab) {
                active_crab_force = current_crab_cross_force;
            } else {
                active_crab_force = total_env_cross_force_for_heading(
                    command_path_heading, wind_rejoin_stale_timeout_s_);
            }

            const double active_crab_min_force_n = use_current_crab
                ? current_crab_min_force_n_
                : wind_crab_min_force_n_;
            if (active_crab_force.valid &&
                std::abs(active_crab_force.cross_force_n) >= active_crab_min_force_n) {
                const double env_cross_force_n = active_crab_force.cross_force_n;
                const double active_crab_force_ref_n = use_current_crab
                    ? current_crab_force_ref_n_
                    : wind_crab_force_ref_n_;
                const double active_crab_base_scale = use_current_crab
                    ? current_crab_base_scale_
                    : wind_crab_base_scale_;
                const double active_crab_xte_kp = use_current_crab
                    ? current_crab_xte_kp_deg_per_m_
                    : wind_crab_xte_kp_deg_per_m_;
                const double active_crab_xte_kd = use_current_crab
                    ? current_crab_xte_rate_kd_deg_per_mps_
                    : wind_crab_xte_rate_kd_deg_per_mps_;
                const double active_crab_feedback_max_deg = use_current_crab
                    ? current_crab_feedback_max_angle_deg_
                    : wind_crab_feedback_max_angle_deg_;
                const bool active_target_offset_enabled =
                    !use_current_crab && wind_crab_target_offset_enabled_;

                const double center_band_m =
                    std::max(5.0, 0.25 * std::max(1.0, corridor_half_width_m_));
                const bool near_center = std::abs(e) <= center_band_m;
                const bool disturbance_pushes_outward = (e * env_cross_force_n) > 0.0;
                double target_xte_m = 0.0;
                if (active_target_offset_enabled &&
                    wind_crab_target_offset_max_m_ > 1.0) {
                    const double offset_abs = std::clamp(
                        std::abs(env_cross_force_n) /
                            std::max(1.0, wind_crab_target_offset_force_ref_n_) *
                            wind_crab_target_offset_max_m_,
                        0.0,
                        wind_crab_target_offset_max_m_);
                    if (offset_abs > 1.0e-3) {
                        target_xte_m =
                            -std::copysign(std::max(wind_crab_target_offset_min_m_, offset_abs),
                                env_cross_force_n);
                    }
                }

                const double cos_ship = std::cos(current_yaw_);
                const double sin_ship = std::sin(current_yaw_);
                const double body_u = current_u_;
                const double body_v = current_v_;
                const double ground_vx = cos_ship * body_u - sin_ship * body_v;
                const double ground_vy = sin_ship * body_u + cos_ship * body_v;
                const double path_ex = std::cos(command_path_heading);
                const double path_ey = std::sin(command_path_heading);
                const double xte_rate_mps = ground_vy * path_ex - ground_vx * path_ey;
                const double target_error_m = e - target_xte_m;
                const bool target_bias_needed =
                    active_target_offset_enabled &&
                    std::abs(target_error_m) > 1.0;
                if (near_center || disturbance_pushes_outward || target_bias_needed) {
                    const double max_crab_rad = active_crab_max_angle_deg * M_PI / 180.0;
                    const double raw_crab_rad = std::clamp(
                        -env_cross_force_n / active_crab_force_ref_n * max_crab_rad,
                        -max_crab_rad,
                        max_crab_rad);
                    const double feedback_max_rad =
                        active_crab_feedback_max_deg * M_PI / 180.0;
                    const double feedback_deg =
                        -active_crab_xte_kp * target_error_m -
                        active_crab_xte_kd * xte_rate_mps;
                    const double feedback_rad = std::clamp(
                        feedback_deg * M_PI / 180.0,
                        -feedback_max_rad,
                        feedback_max_rad);
                    const double lane_scale = std::min(
                        1.0,
                        std::abs(target_error_m) / std::max(1.0, corridor_half_width_m_));
                    const double scale =
                        active_crab_base_scale + (1.0 - active_crab_base_scale) * lane_scale;
                    const bool already_too_far_windward =
                        active_target_offset_enabled &&
                        std::abs(target_xte_m) > 1.0e-3 &&
                        (target_error_m * target_xte_m) > 0.0;
                    const bool moving_deeper_windward =
                        std::abs(target_xte_m) > 1.0e-3 &&
                        (xte_rate_mps * target_xte_m) > 0.0 &&
                        std::abs(target_error_m) > 1.0;
                    const bool raw_crab_points_outward_from_center =
                        std::abs(e) > 1.0 &&
                        (e * raw_crab_rad) > 0.0;
                    const double effective_raw_crab_rad =
                        (already_too_far_windward ||
                         moving_deeper_windward ||
                         raw_crab_points_outward_from_center) ? 0.0 : raw_crab_rad;
                    const double crab_rad = std::clamp(
                        (effective_raw_crab_rad + feedback_rad) * scale,
                        -max_crab_rad,
                        max_crab_rad);
                    psi_cmd = normalize_angle_pi(psi_cmd + crab_rad);
                    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                        "[%s CRAB] cross_force=%.0fN target_xte=%.1fm xte=%.1fm xte_dot=%.2fm/s ff=%.1f/%.1fdeg fb=%.1fdeg crab=%.1fdeg psi=%.1fdeg windward=%d/%d outward=%d",
                        use_current_crab ? "CURRENT" : "WIND",
                        env_cross_force_n,
                        target_xte_m,
                        e,
                        xte_rate_mps,
                        raw_crab_rad * 180.0 / M_PI,
                        effective_raw_crab_rad * 180.0 / M_PI,
                        feedback_rad * 180.0 / M_PI,
                        crab_rad * 180.0 / M_PI,
                        psi_cmd * 180.0 / M_PI,
                        already_too_far_windward ? 1 : 0,
                        moving_deeper_windward ? 1 : 0,
                        raw_crab_points_outward_from_center ? 1 : 0);
                }
            }
        }

        if (is_last_wp && dist_to_wp < final_capture_radius_ && std::abs(e) < 2.0) {
            integral_e_ = 0.0;
            beta_hat_ = 0.0;
            psi_cmd = command_path_heading;

            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[LOS] Terminal heading hold: dist=%.1fm e=%.2fm psi=%.1fdeg",
                dist_to_wp, e, psi_cmd * 180.0 / M_PI);
        }

        const bool xte_release_guard_active =
            xte_rejoin_release_guard_s_ > 1e-6 &&
            this->now().seconds() <= xte_rejoin_release_guard_until_sec_ &&
            !dp_mode_active_ &&
            !final_approach_active &&
            !emergency_avoidance_active;
        if (xte_release_guard_active &&
            xte_rejoin_release_guard_max_correction_deg_ > 1e-6) {
            const double max_guard_correction =
                xte_rejoin_release_guard_max_correction_deg_ * M_PI / 180.0;
            const double raw_correction = normalize_angle_pi(psi_cmd - command_path_heading);
            const double capped_correction =
                std::clamp(raw_correction, -max_guard_correction, max_guard_correction);
            if (std::abs(normalize_angle_pi(raw_correction - capped_correction)) > 0.5 * M_PI / 180.0) {
                psi_cmd = normalize_angle_pi(command_path_heading + capped_correction);
                beta_hat_ = 0.0;
                integral_e_ = 0.0;
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 1000,
                    "[XTE RELEASE GUARD] cap psi correction %.1f->%.1fdeg xte=%.1fm path=%.1fdeg guard_left=%.1fs",
                    raw_correction * 180.0 / M_PI,
                    capped_correction * 180.0 / M_PI,
                    e,
                    command_path_heading * 180.0 / M_PI,
                    xte_rejoin_release_guard_until_sec_ - this->now().seconds());
            }
        }

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[LOS] [ILOS] e=%.1fm ctrl_e=%.1fm corridor=%d hold=%d half=%.1fm reacq=%.1fm | ψ=%.1f° | wp[%d]",
            std::abs(e), e_control, corridor_enabled ? 1 : 0, corridor_hold_active_ ? 1 : 0,
            corridor_half_width_m_, corridor_reacquire_width_m_, psi_cmd*180/M_PI, current_wp_idx_);
    }

    // ========== 真正的航海级速度规划 (Anticipation 前瞻规划) ==========
    // 外部规划器经常已经把大弯拆成多个小弦段；单段转角可能只有 3-5°，
    // 但连续小弦本质上仍是弯道。这里用相邻航段的几何转角补充识别，
    // 让现有弯道航向限幅/速度门也作用于这类外部平滑航线。
    double geometric_turn_hint_deg = 0.0;
    auto segment_heading = [&](int from_idx, int to_idx) {
        return compute_path_angle(
            waypoints_[from_idx].x, waypoints_[from_idx].y,
            waypoints_[to_idx].x, waypoints_[to_idx].y);
    };
    if (target_wp_idx > 1 && target_wp_idx < static_cast<int>(waypoints_.size())) {
        const double prev_heading = segment_heading(target_wp_idx - 2, target_wp_idx - 1);
        geometric_turn_hint_deg = std::max(
            geometric_turn_hint_deg,
            std::abs(normalize_angle_pi(alpha_k - prev_heading)) * 180.0 / M_PI);
    }
    if (target_wp_idx + 1 < static_cast<int>(waypoints_.size())) {
        const double next_heading = segment_heading(target_wp_idx, target_wp_idx + 1);
        geometric_turn_hint_deg = std::max(
            geometric_turn_hint_deg,
            std::abs(normalize_angle_pi(next_heading - alpha_k)) * 180.0 / M_PI);
    }
    const bool geometric_turn_segment =
        geometric_turn_hint_deg >= std::max(1.0, turn_arc_min_angle_deg_ * 0.25);
    const bool turn_segment_heading_limited =
        turn_segment_max_los_correction_deg_ > 1e-6 &&
        (wp_prev.is_arc_point || wp_next.is_arc_point || geometric_turn_segment);
    const double turn_segment_angle_hint_deg = std::max(
        geometric_turn_hint_deg, std::max(wp_prev.turn_angle_deg, wp_next.turn_angle_deg));
    if (turn_segment_heading_limited && !xte_guidance_rejoin_override) {
        const double turn_angle_hint_deg = turn_segment_angle_hint_deg;
        double max_correction_deg = turn_segment_max_los_correction_deg_;
        if (turn_angle_hint_deg > turn_arc_min_angle_deg_ + 1e-6) {
            const double blend = std::clamp((turn_angle_hint_deg - 15.0) / 30.0, 0.0, 1.0);
            max_correction_deg =
                turn_segment_max_los_correction_deg_ +
                blend * (turn_segment_los_correction_45deg_ - turn_segment_max_los_correction_deg_);
        }
        const double max_correction = max_correction_deg * M_PI / 180.0;
        const double raw_correction = normalize_angle_pi(psi_cmd - command_path_heading);
        const double capped_correction =
            std::clamp(raw_correction, -max_correction, max_correction);
        if (std::abs(raw_correction - capped_correction) > 1e-3) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[TURN SEGMENT HEADING CAP] turn=%.1fdeg alpha=%.1fdeg raw=%.1fdeg cap=%.1fdeg psi=%.1fdeg",
                turn_angle_hint_deg,
                alpha_k * 180.0 / M_PI,
                raw_correction * 180.0 / M_PI,
                capped_correction * 180.0 / M_PI,
                normalize_angle_pi(alpha_k + capped_correction) * 180.0 / M_PI);
        }
        psi_cmd = normalize_angle_pi(alpha_k + capped_correction);
    } else if (turn_segment_heading_limited && xte_guidance_rejoin_override) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[FAR XTE REJOIN] bypass turn heading cap: xte=%.1fm raw_xte=%.1fm threshold=%.1fm turn_hint=%.1fdeg psi=%.1fdeg",
            std::abs(e), raw_abs_xte_for_rejoin, far_xte_rejoin_threshold, turn_segment_angle_hint_deg,
            psi_cmd * 180.0 / M_PI);
    }

    if (final_dp_terminal_point_capture && !final_dp_overrun_stop_active && !dp_mode_active_) {
        const double point_bearing = std::atan2(wp_next.y - y, wp_next.x - x);
        double env_force_norm = 0.0;
        double env_age_s = 0.0;
        const double final_point_weather_speed = std::clamp(
            dp_weathervane_handoff_speed_mps_,
            0.1,
            std::max(max_speed_, 0.1));
        const double final_point_weather_radius =
            std::max(final_stop_radius_, 1.0) + 0.25 * Lpp_;
        const bool final_point_weather_ready =
            std::abs(e) <= std::max(1.0, dp_weathervane_handoff_max_cross_track_m_) &&
            std::hypot(current_u_, current_v_) <= final_point_weather_speed &&
            dist_to_wp <= final_point_weather_radius;
        const bool weather_heading_active = final_point_weather_ready &&
            select_dp_weathervane_heading(
                alpha_k, psi_cmd, env_force_norm, env_age_s);
        integral_e_ = 0.0;
        beta_hat_ = 0.0;
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[FINAL POINT CAPTURE] target=(%.1f,%.1f) current=(%.1f,%.1f) dist=%.1fm rem=%.1fm e=%.1fm psi=%.1fdeg bearing=%.1fdeg weather=%d ready=%d env=%.0fN age=%.2fs",
            wp_next.x, wp_next.y, x, y, dist_to_wp, remaining_along, e,
            psi_cmd * 180.0 / M_PI,
            point_bearing * 180.0 / M_PI,
            weather_heading_active ? 1 : 0,
            final_point_weather_ready ? 1 : 0,
            env_force_norm, env_age_s);
    }

    double u_cmd_planned = max_speed_;
    bool turn_speed_coupling_active = false;
    double upcoming_turn_angle_deg = 0.0;

    // 如果不是最后一个航点，提前看一眼前方的转弯角度
    // [B3-FIX] 用wp_next(当前段终点)作为转弯枢轴点，而非waypoints_[idx]
    // 原代码idx=0时wp_prev==wp_current导致current_path_angle退化
    if (!is_last_wp) {
        int turn_idx = (current_wp_idx_ == 0) ? 1 : current_wp_idx_;
        if (turn_idx + 1 < (int)waypoints_.size()) {
            Waypoint wp_turn  = waypoints_[turn_idx];       // 转弯点(当前段终点)
            Waypoint wp_after = waypoints_[turn_idx + 1];   // 转弯后下一目标

            double current_path_angle = compute_path_angle(wp_prev.x, wp_prev.y, wp_next.x, wp_next.y);
            double next_path_angle    = compute_path_angle(wp_turn.x, wp_turn.y, wp_after.x, wp_after.y);
            double turn_angle_rad = normalize_angle_pi(next_path_angle - current_path_angle);
            upcoming_turn_angle_deg = std::abs(turn_angle_rad) * 180.0 / M_PI;

            double target_arrival_speed = 0.0;
            double required_slow_down_dist = 0.0;
            compute_turn_approach_params(turn_angle_rad, max_speed_,
                                         turn_speed_15deg_, turn_speed_45deg_,
                                         turn_speed_90deg_, turn_speed_180deg_,
                                         turn_no_slowdown_angle_deg_,
                                         turn_slow_down_dist_15deg_, turn_slow_down_dist_45deg_,
                                         turn_slow_down_dist_90deg_, turn_slow_down_dist_180deg_,
                                         target_arrival_speed, required_slow_down_dist);

            const bool turn_slowdown_required =
                target_arrival_speed < max_speed_ - 1e-3 &&
                required_slow_down_dist > 1.0;
            if (turn_slowdown_required) {
                // [B2-FIX] 减速区必须从捕获圈外开始，否则船在150m处切换后永远不进减速区
                required_slow_down_dist = std::max(required_slow_down_dist, intermediate_capture_radius_ + 20.0);
            }

            if (turn_slowdown_required && dist_to_wp < required_slow_down_dist) {
                turn_speed_coupling_active = true;
                double dist_ratio = dist_to_wp / required_slow_down_dist;
                u_cmd_planned = target_arrival_speed + dist_ratio * (max_speed_ - target_arrival_speed);

                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[前瞻规划] 前方 %.0f° 急弯! 距弯心 %.1fm, 计划收油门: u_cmd = %.2f m/s",
                    std::abs(turn_angle_rad) * 180.0 / M_PI, dist_to_wp, u_cmd_planned);
            }
        }
    }

    if (turn_feasibility_preview_enabled_ && !is_last_wp &&
        !dp_mode_active_ &&
        waypoints_.size() >= 3) {
        const double preview_window_m =
            std::max(turn_feasibility_preview_distance_m_, Lpp_);
        const double min_turn_deg =
            std::max(1.0, turn_feasibility_min_cumulative_angle_deg_);
        const double per_step_turn_deg = std::max(1.0, 0.2 * min_turn_deg);
        double distance_ahead = std::max(0.0, seg_len - std::clamp(s, 0.0, seg_len));
        double prev_heading = alpha_k;
        double cumulative_turn_deg = 0.0;
        double dominant_turn_deg = 0.0;
        double first_turn_distance_m = std::numeric_limits<double>::infinity();
        int first_turn_idx = -1;

        for (int idx = target_wp_idx; idx + 1 < static_cast<int>(waypoints_.size()); ++idx) {
            if (distance_ahead > preview_window_m) {
                break;
            }
            const double next_heading = segment_heading(idx, idx + 1);
            const double delta_deg =
                std::abs(normalize_angle_pi(next_heading - prev_heading)) * 180.0 / M_PI;
            if (delta_deg >= per_step_turn_deg) {
                if (!std::isfinite(first_turn_distance_m)) {
                    first_turn_distance_m = distance_ahead;
                    first_turn_idx = idx;
                }
                cumulative_turn_deg += delta_deg;
                dominant_turn_deg = std::max(dominant_turn_deg, delta_deg);
            }
            distance_ahead += std::hypot(
                waypoints_[idx + 1].x - waypoints_[idx].x,
                waypoints_[idx + 1].y - waypoints_[idx].y);
            prev_heading = next_heading;
        }

        const double preview_turn_deg = std::max(cumulative_turn_deg, dominant_turn_deg);
        if (std::isfinite(first_turn_distance_m) && preview_turn_deg >= min_turn_deg) {
            double target_arrival_speed = max_speed_;
            double required_slow_down_dist = 0.0;
            compute_turn_approach_params(
                preview_turn_deg * M_PI / 180.0,
                max_speed_,
                turn_speed_15deg_, turn_speed_45deg_,
                turn_speed_90deg_, turn_speed_180deg_,
                turn_no_slowdown_angle_deg_,
                turn_slow_down_dist_15deg_, turn_slow_down_dist_45deg_,
                turn_slow_down_dist_90deg_, turn_slow_down_dist_180deg_,
                target_arrival_speed, required_slow_down_dist);

            if (target_arrival_speed < max_speed_ - 1e-3 &&
                required_slow_down_dist > 1.0) {
                const double decel_window_m = std::min(
                    preview_window_m,
                    std::max(required_slow_down_dist, intermediate_capture_radius_ + 20.0) +
                        turn_feasibility_response_margin_m_);
                if (first_turn_distance_m <= decel_window_m) {
                    const double ratio = std::clamp(
                        first_turn_distance_m / std::max(decel_window_m, 1.0),
                        0.0,
                        1.0);
                    const double feasible_speed = std::clamp(
                        target_arrival_speed + ratio * (max_speed_ - target_arrival_speed),
                        minimum_steerage_speed_,
                        std::max(max_speed_, minimum_steerage_speed_));
                    const double old_plan = u_cmd_planned;
                    u_cmd_planned = std::min(u_cmd_planned, feasible_speed);
                    turn_speed_coupling_active = true;
                    upcoming_turn_angle_deg = std::max(upcoming_turn_angle_deg, preview_turn_deg);
                    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                        "[TURN FEASIBILITY PREVIEW] first_wp=%d ahead=%.1fm window=%.1fm turn=%.1fdeg step=%.1fdeg target=%.2fmps plan %.2f->%.2fmps",
                        first_turn_idx,
                        first_turn_distance_m,
                        decel_window_m,
                        preview_turn_deg,
                        per_step_turn_deg,
                        target_arrival_speed,
                        old_plan,
                        u_cmd_planned);
                }
            }
        }
    }

    const double dense_turn_predecel_distance =
        std::max(turn_arc_pre_decel_dist_m_, intermediate_capture_radius_ + 20.0);
    if (!is_last_wp && dense_turn_predecel_distance > 1.0 &&
        target_wp_idx + 1 < static_cast<int>(waypoints_.size())) {
        double distance_ahead = std::max(0.0, seg_len - std::clamp(s, 0.0, seg_len));
        double prev_heading = alpha_k;
        double cumulative_turn_deg = 0.0;
        double first_turn_distance = std::numeric_limits<double>::infinity();
        const double small_turn_threshold_deg = std::max(1.0, turn_arc_min_angle_deg_ * 0.25);

        for (int idx = target_wp_idx; idx + 1 < static_cast<int>(waypoints_.size()); ++idx) {
            const double next_heading = segment_heading(idx, idx + 1);
            const double delta_deg =
                std::abs(normalize_angle_pi(next_heading - prev_heading)) * 180.0 / M_PI;
            if (delta_deg >= small_turn_threshold_deg) {
                if (!std::isfinite(first_turn_distance)) {
                    first_turn_distance = distance_ahead;
                }
                cumulative_turn_deg += delta_deg;
            }
            distance_ahead += std::hypot(
                waypoints_[idx + 1].x - waypoints_[idx].x,
                waypoints_[idx + 1].y - waypoints_[idx].y);
            prev_heading = next_heading;
            if (distance_ahead > dense_turn_predecel_distance) {
                break;
            }
        }

        const double dense_turn_slowdown_threshold_deg =
            turn_arc_min_angle_deg_;
        if (std::isfinite(first_turn_distance) &&
            first_turn_distance <= dense_turn_predecel_distance &&
            cumulative_turn_deg > dense_turn_slowdown_threshold_deg + 1e-6) {
            double target_turn_speed = turn_arc_speed_mps_;
            if (turn_segment_speed_gate_enabled_) {
                target_turn_speed = std::min(target_turn_speed, turn_segment_speed_cap_mps_);
            }
            target_turn_speed = std::clamp(
                target_turn_speed, minimum_steerage_speed_, std::max(max_speed_, minimum_steerage_speed_));
            const double dist_ratio = std::clamp(
                first_turn_distance / dense_turn_predecel_distance, 0.0, 1.0);
            const double dense_turn_speed =
                target_turn_speed + dist_ratio * (max_speed_ - target_turn_speed);
            u_cmd_planned = std::min(u_cmd_planned, dense_turn_speed);
            turn_speed_coupling_active = true;
            upcoming_turn_angle_deg = std::max(upcoming_turn_angle_deg, cumulative_turn_deg);

            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[DENSE TURN PREDECEL] cumulative=%.1fdeg first=%.1fm window=%.1fm target=%.2fmps planned=%.2fmps",
                cumulative_turn_deg, first_turn_distance, dense_turn_predecel_distance,
                target_turn_speed, u_cmd_planned);
        }
    }

    if (external_turn_context.enabled) {
        double external_turn_speed = max_speed_;
        if (external_turn_context.upcoming &&
            std::isfinite(external_turn_context.first_turn_distance_m)) {
            const double preview_window_m =
                std::max(external_route_turn_preview_distance_m_, Lpp_);
            const double distance_ratio = std::clamp(
                external_turn_context.first_turn_distance_m / preview_window_m,
                0.0,
                1.0);
            external_turn_speed =
                external_route_turn_speed_cap_mps_ +
                distance_ratio * (max_speed_ - external_route_turn_speed_cap_mps_);
        }
        if (external_turn_context.recent && external_turn_context.centerline_required) {
            external_turn_speed = std::min(external_turn_speed, external_route_turn_speed_cap_mps_);
        }
        external_turn_speed = std::clamp(
            external_turn_speed,
            minimum_steerage_speed_,
            std::max(max_speed_, minimum_steerage_speed_));
        if (external_turn_speed < max_speed_ - 1e-6) {
            u_cmd_planned = std::min(u_cmd_planned, external_turn_speed);
            turn_speed_coupling_active = true;
            upcoming_turn_angle_deg = std::max(
                upcoming_turn_angle_deg,
                std::max(external_turn_context.upcoming_cumulative_deg,
                         external_turn_context.recent_cumulative_deg));
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[EXTERNAL TURN SPEED] cap planned=%.2fmps upcoming=%d recent=%d ahead=%.1fm behind=%.1fm turn=%.1f/%.1fdeg xte=%.1fm release=%.1fm",
                u_cmd_planned,
                external_turn_context.upcoming ? 1 : 0,
                external_turn_context.recent ? 1 : 0,
                external_turn_context.first_turn_distance_m,
                external_turn_context.recent_turn_distance_m,
                external_turn_context.upcoming_cumulative_deg,
                external_turn_context.recent_cumulative_deg,
                e,
                external_route_turn_centerline_release_xte_m_);
        }
    }

    const double configured_segment_speed_limit =
        lookup_double(wp_speed_limit_mps_, target_wp_idx, max_speed_);
    const double routeplan_segment_speed_limit =
        routeplan_segment_speed_cap(target_wp_idx, max_speed_);
    const double segment_speed_limit =
        std::min(configured_segment_speed_limit, routeplan_segment_speed_limit);
    const bool defer_final_dp_segment_speed_limit =
        is_last_wp &&
        final_waypoint_requires_dp &&
        (dist_to_wp > terminal_slow_down_dist);
    if (segment_speed_limit < max_speed_ - 1e-6 && defer_final_dp_segment_speed_limit) {
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[FINAL DP SPEED DEFER] wp[%d] limit=%.2f ignored until %.1fm; dist=%.1fm planned=%.2f m/s",
            target_wp_idx, segment_speed_limit, terminal_slow_down_dist, dist_to_wp, u_cmd_planned);
    } else if (segment_speed_limit < max_speed_ - 1e-6) {
        u_cmd_planned = std::min(u_cmd_planned, segment_speed_limit);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[航段限速] wp[%d] limit=%.2f m/s planned=%.2f m/s",
            target_wp_idx, segment_speed_limit, u_cmd_planned);
    }
    const double gate_blocked_speed = lookup_double(
        wp_gate_blocked_speed_mps_, target_wp_idx, 0.0);
    const double rejoin_cross_track = lookup_double(
        wp_rejoin_cross_track_m_, target_wp_idx, 0.0);
    const bool route_rejoin_required =
        route_gate_blocked ||
        (rejoin_cross_track > 0.0 && std::abs(e) > rejoin_cross_track) ||
        xte_speed_cap_rejoin_override;

    // 船长化恢复巡航 gate：
    // 只有当下一航段速度显著高于上一航段，并且船已经回到航道、艏向稳定、
    // 艏摇角速度收敛时，才逐步恢复到新航段速度。
    const double previous_segment_speed_limit =
        effective_segment_speed_limit(target_wp_idx - 1, max_speed_);
    const std::string active_navigation_mode = target_navigation_mode;
    const bool recovery_mode_allowed =
        !turn_recovery_require_cruise_mode_ ||
        active_navigation_mode == "cruise" ||
        active_navigation_mode == "open_water_cruise" ||
        active_navigation_mode == "post_turn_cruise";
    const bool speed_recovery_candidate =
        turn_recovery_gate_enabled_ &&
        target_wp_idx > 0 &&
        recovery_mode_allowed &&
        segment_speed_limit > previous_segment_speed_limit + std::max(0.0, turn_recovery_speed_margin_mps_);

    if (speed_recovery_candidate) {
        const rclcpp::Time now = this->now();
        const double current_speed = std::hypot(current_u_, current_v_);
        if (speed_recovery_segment_idx_ != target_wp_idx) {
            speed_recovery_segment_idx_ = target_wp_idx;
            speed_recovery_gate_cleared_ = false;
            speed_recovery_speed_cap_ = std::max(
                minimum_steerage_speed_,
                std::min(std::max(current_speed, previous_segment_speed_limit), segment_speed_limit));
            speed_recovery_last_time_ = now;
            RCLCPP_INFO(this->get_logger(),
                "[SPEED RECOVERY] armed wp[%d] mode=%s prev_limit=%.2f target_limit=%.2f cap=%.2f",
                target_wp_idx, active_navigation_mode.c_str(), previous_segment_speed_limit,
                segment_speed_limit, speed_recovery_speed_cap_);
        }

        const double heading_error_deg =
            std::abs(normalize_angle_pi(psi_cmd - current_yaw_)) * 180.0 / M_PI;
        const double yaw_rate_deg_s = std::abs(current_r_) * 180.0 / M_PI;
        const double ground_vx =
            std::cos(current_yaw_) * current_u_ - std::sin(current_yaw_) * current_v_;
        const double ground_vy =
            std::sin(current_yaw_) * current_u_ + std::cos(current_yaw_) * current_v_;
        const double cross_track_rate_mps =
            ground_vy * std::cos(alpha_k) - ground_vx * std::sin(alpha_k);
        const double recovery_abs_xte = std::max(std::abs(e), raw_abs_xte_for_rejoin);
        const bool lateral_ready = recovery_abs_xte <= std::max(0.1, turn_recovery_max_xte_m_);
        const bool heading_ready = heading_error_deg <= std::max(0.1, turn_recovery_max_heading_error_deg_);
        const bool yaw_rate_ready = yaw_rate_deg_s <= std::max(0.01, turn_recovery_max_yaw_rate_deg_s_);
        const bool cross_track_rate_ready =
            std::abs(cross_track_rate_mps) <= std::max(0.01, turn_recovery_max_cross_track_rate_mps_);
        const bool recovery_gate_ready =
            lateral_ready && heading_ready && yaw_rate_ready && cross_track_rate_ready &&
            !route_rejoin_required;

        if (!speed_recovery_gate_cleared_ && recovery_gate_ready) {
            speed_recovery_gate_cleared_ = true;
            speed_recovery_last_time_ = now;
            RCLCPP_INFO(this->get_logger(),
                "[SPEED RECOVERY] gate cleared wp[%d] e=%.1fm/%.1fm hdg=%.1f/%.1fdeg yaw_rate=%.2f/%.2fdeg_s e_dot=%.2f/%.2fmps",
                target_wp_idx, recovery_abs_xte, turn_recovery_max_xte_m_,
                heading_error_deg, turn_recovery_max_heading_error_deg_,
                yaw_rate_deg_s, turn_recovery_max_yaw_rate_deg_s_,
                cross_track_rate_mps, turn_recovery_max_cross_track_rate_mps_);
        }

        if (!speed_recovery_gate_cleared_) {
            const double hold_cap = std::max(minimum_steerage_speed_, previous_segment_speed_limit);
            u_cmd_planned = std::min(u_cmd_planned, hold_cap);
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[SPEED RECOVERY] holding wp[%d] mode=%s e=%.1f/%.1fm hdg=%.1f/%.1fdeg yaw=%.2f/%.2fdeg_s e_dot=%.2f/%.2fmps rejoin=%d cap=%.2f",
                target_wp_idx, active_navigation_mode.c_str(),
                recovery_abs_xte, turn_recovery_max_xte_m_,
                heading_error_deg, turn_recovery_max_heading_error_deg_,
                yaw_rate_deg_s, turn_recovery_max_yaw_rate_deg_s_,
                cross_track_rate_mps, turn_recovery_max_cross_track_rate_mps_,
                route_rejoin_required ? 1 : 0, hold_cap);
        } else {
            double dt_recovery = (now - speed_recovery_last_time_).seconds();
            if (dt_recovery <= 0.0 || dt_recovery > 2.0) {
                dt_recovery = 0.5;
            }
            const double ramp = std::max(0.0, turn_recovery_speed_ramp_mps2_);
            speed_recovery_speed_cap_ = std::min(
                segment_speed_limit,
                speed_recovery_speed_cap_ + ramp * dt_recovery);
            speed_recovery_last_time_ = now;
            u_cmd_planned = std::min(u_cmd_planned, speed_recovery_speed_cap_);
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[SPEED RECOVERY] ramp wp[%d] cap=%.2f target=%.2f ramp=%.2fmps2",
                target_wp_idx, speed_recovery_speed_cap_, segment_speed_limit, ramp);
        }
    } else if (speed_recovery_segment_idx_ >= 0 && speed_recovery_segment_idx_ != target_wp_idx) {
        speed_recovery_segment_idx_ = -1;
        speed_recovery_gate_cleared_ = false;
        speed_recovery_speed_cap_ = 0.0;
    }

    if (route_rejoin_required && gate_blocked_speed > 0.0) {
        const double rejoin_speed = std::clamp(
            gate_blocked_speed, 0.1, std::max(max_speed_, 0.1));
        u_cmd_planned = std::min(u_cmd_planned, rejoin_speed);
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[ROUTE REJOIN] wp[%d] line-up: e=%.1fm gate=%.1fm rejoin=%.1fm hdg=%.1f/%.1fdeg speed=%.2f/%.2fmps blocked=%d -> cap %.2fm/s",
            target_wp_idx, std::abs(e), switch_max_xte, rejoin_cross_track,
            switch_heading_error_deg, switch_max_heading_error_deg,
            U_current_gate, switch_max_speed, route_gate_blocked ? 1 : 0,
            rejoin_speed);
    }

    // 终端减速点来自航线计划的剩余航程，而不是最后一小段航线的长度。
    // 这样 S-turn、进近航道等多航段任务不会等到最后一个 waypoint 才收油。
    const double terminal_decel_distance = (is_last_wp && terminal_decel_use_position_error_)
        ? std::max(route_remaining_to_final, dist_to_wp)
        : route_remaining_to_final;
    if (terminal_decel_distance < terminal_slow_down_dist) {
        double final_radius = std::max(final_capture_radius_, 1.0);
        double ramp_span = std::max(terminal_slow_down_dist - final_radius, 1.0);
        double dist_ratio = std::clamp((terminal_decel_distance - final_radius) / ramp_span, 0.0, 1.0);
        double capture_speed = std::clamp(final_capture_speed_, 0.1, max_speed_);
        double terminal_speed = capture_speed + dist_ratio * (max_speed_ - capture_speed);
        u_cmd_planned = std::min(u_cmd_planned, terminal_speed);

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[终端减速] dist_metric=%.1fm route_rem=%.1fm wp_dist=%.1fm < %.1fm, 计划速度 %.2f m/s",
            terminal_decel_distance, route_remaining_to_final, dist_to_wp, terminal_slow_down_dist, u_cmd_planned);
    }

    // ========== 计划转弯专用速度-航向耦合 ==========
    // 真实船舶高速直线纠偏不能因为 ILOS 航向偏置而误杀速度；否则舵效随 U^2 暴跌。
    // 仅在前方确有计划转弯、且尚未进入终端对线时应用 turn_penalty。
    if (final_dp_terminal_point_capture) {
        const double capture_speed = std::clamp(final_capture_speed_, 0.1, max_speed_);
        const double terminal_reacquire_speed = std::clamp(
            std::max(capture_speed, std::min(final_reacquire_min_speed_mps_, 2.0)),
            0.1,
            max_speed_);
        const double terminal_cap = final_dp_waiting_lateral_ready
            ? capture_speed
            : terminal_reacquire_speed;
        const double old_u_cmd_planned = u_cmd_planned;
        u_cmd_planned = std::min(u_cmd_planned, terminal_cap);
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[FINAL POINT CAPTURE SPEED] cap %.2f->%.2f m/s lateral=%d capture=%.2f reacquire=%.2f dist=%.1fm e=%.1fm",
            old_u_cmd_planned, u_cmd_planned,
            final_dp_waiting_lateral_ready ? 1 : 0,
            capture_speed, terminal_reacquire_speed, dist_to_wp, e);
    }

    if (is_last_wp && final_waypoint_requires_dp && dist_to_wp <= final_dp_brake_radius_) {
        const bool final_brake_lateral_ready =
            std::abs(e) <= std::max(final_capture_max_cross_track_m_, 1.0);
        const double inner_stop_margin = std::max(1.0, 0.05 * Lpp_);
        const bool outside_inner_stop =
            dist_to_wp > (std::max(final_stop_radius_, 1.0) + inner_stop_margin);
        const double closing_brake_speed = outside_inner_stop
            ? std::max(final_dp_brake_speed_, final_dp_min_closing_speed_mps_)
            : final_dp_brake_speed_;
        const double brake_speed = final_brake_lateral_ready
            ? std::clamp(closing_brake_speed, 0.0, std::max(final_capture_speed_, 0.1))
            : std::clamp(
                final_reacquire_min_speed_mps_ > 0.0 ? final_reacquire_min_speed_mps_ : minimum_steerage_speed_,
                minimum_steerage_speed_,
                max_speed_);
        const double old_u_cmd_planned = u_cmd_planned;
        u_cmd_planned = std::min(u_cmd_planned, brake_speed);

        const double final_channel_weather_speed = std::clamp(
            dp_weathervane_handoff_speed_mps_,
            0.1,
            std::max(max_speed_, 0.1));
        const double final_channel_weather_radius =
            std::max(final_stop_radius_, 1.0) + 0.25 * Lpp_;
        const bool final_channel_weather_ready =
            final_brake_lateral_ready &&
            std::hypot(current_u_, current_v_) <= final_channel_weather_speed &&
            dist_to_wp <= final_channel_weather_radius;
        if (final_channel_weather_ready) {
            double env_force_norm = 0.0;
            double env_age_s = 0.0;
            const bool weather_heading_active = select_dp_weathervane_heading(
                alpha_k, psi_cmd, env_force_norm, env_age_s);
            integral_e_ = 0.0;
            beta_hat_ = 0.0;
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[FINAL DP WEATHER-VANE] channel brake psi=%.1fdeg weather=%d env=%.0fN age=%.2fs",
                psi_cmd * 180.0 / M_PI,
                weather_heading_active ? 1 : 0, env_force_norm, env_age_s);
        } else if (final_brake_lateral_ready) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[FINAL DP WEATHER-VANE] deferred during channel brake: U=%.2fm/s dist=%.1fm limits %.2fm/s %.1fm; keep channel heading %.1fdeg",
                std::hypot(current_u_, current_v_),
                dist_to_wp,
                final_channel_weather_speed,
                final_channel_weather_radius,
                alpha_k * 180.0 / M_PI);
        }

        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[FINAL DP CHANNEL BRAKE] cap %.2f->%.2f m/s dist=%.1fm brake_radius=%.1fm stop=%.1fm e=%.1fm align_channel=%d reacquire=%d closing=%d",
            old_u_cmd_planned, u_cmd_planned, dist_to_wp,
            final_dp_brake_radius_, final_stop_radius_, e,
            final_brake_lateral_ready ? 1 : 0,
            final_brake_lateral_ready ? 0 : 1,
            outside_inner_stop ? 1 : 0);
    }

    double heading_error_abs = std::abs(normalize_angle_pi(psi_cmd - current_yaw_));
    const double k_coupling = 0.5;
    const bool final_speed_coupling_blocked =
        is_last_wp && (route_remaining_to_final < terminal_slow_down_dist || dist_to_wp < terminal_slow_down_dist);
    double turn_penalty = 1.0;
    const double path_heading_error_abs = std::abs(normalize_angle_pi(alpha_k - current_yaw_));
    bool heading_align_rejoin_now = false;
    if (heading_align_rejoin_enabled_ && !xte_guidance_rejoin_override &&
        !dp_mode_active_ && !final_speed_coupling_blocked && !emergency_avoidance_active) {
        const bool enter_alignment = path_heading_error_abs >= heading_align_enter_rad_;
        const bool stay_alignment = heading_align_active_ && path_heading_error_abs > heading_align_exit_rad_;
        heading_align_active_ = enter_alignment || stay_alignment;
        heading_align_rejoin_now = heading_align_active_;
        if (heading_align_rejoin_now) {
            psi_cmd = alpha_k;
            heading_error_abs = path_heading_error_abs;
            if (heading_align_reset_xte_m_ <= 0.0 || std::abs(e) <= heading_align_reset_xte_m_) {
                integral_e_ = 0.0;
                beta_hat_ = 0.0;
            }
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[HEADING ALIGN REJOIN] path_hdg_err=%.1fdeg enter=%.1fdeg exit=%.1fdeg U=%.2fmps xte=%.1fm psi=%.1fdeg",
                path_heading_error_abs * 180.0 / M_PI,
                heading_align_enter_rad_ * 180.0 / M_PI,
                heading_align_exit_rad_ * 180.0 / M_PI,
                std::hypot(current_u_, current_v_),
                std::abs(e), psi_cmd * 180.0 / M_PI);
        }
    } else {
        heading_align_active_ = false;
    }
    if (turn_speed_coupling_active && !final_speed_coupling_blocked) {
        turn_penalty = std::exp(-k_coupling * heading_error_abs);
    }

    u_cmd = std::min(u_cmd_planned, max_speed_ * turn_penalty);

    if (xte_speed_cap_rejoin_override && !dp_mode_active_ && !final_speed_coupling_blocked) {
        double requested_cap = hard_xte_limit_exceeded
            ? far_xte_rejoin_hard_speed_cap_mps_
            : far_xte_rejoin_speed_cap_mps_;
        if (env_rejoin_threshold_active && !hard_xte_limit_exceeded) {
            requested_cap = std::max(requested_cap, env_rejoin_speed_cap_mps);
        }
        const double speed_cap = std::clamp(
            requested_cap,
            minimum_steerage_speed_,
            std::max(max_speed_, minimum_steerage_speed_));
        const double old_u_cmd = u_cmd;
        u_cmd = std::min(u_cmd, speed_cap);
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[XTE RECOVERY] cap speed %.2f->%.2f m/s xte=%.1fm raw_xte=%.1fm enter=%.1fm release=%.1fm hard=%d",
            old_u_cmd, u_cmd, std::abs(e), raw_abs_xte_for_rejoin, far_xte_rejoin_threshold,
            effective_far_rejoin_release, hard_xte_limit_exceeded ? 1 : 0);
    }

    if (roll_guard_enabled_ && roll_guard_limit_deg_ > 0.0) {
        const double roll_deg = std::abs(current_roll_) * 180.0 / M_PI;
        if (roll_guard_active_) {
            if (roll_deg <= roll_guard_release_deg_) {
                roll_guard_active_ = false;
                RCLCPP_INFO(this->get_logger(),
                    "[ROLL GUARD] released roll=%.2fdeg <= %.2fdeg",
                    roll_deg, roll_guard_release_deg_);
            }
        } else if (roll_deg >= roll_guard_limit_deg_) {
            roll_guard_active_ = true;
            RCLCPP_WARN(this->get_logger(),
                "[ROLL GUARD] armed roll=%.2fdeg >= %.2fdeg, reducing speed",
                roll_deg, roll_guard_limit_deg_);
        }

        if (roll_guard_active_ && !dp_mode_active_) {
            const double speed_cap = std::clamp(
                roll_guard_speed_cap_mps_, 0.5, std::max(max_speed_, 0.5));
            const double old_u_cmd = u_cmd;
            u_cmd = std::min(u_cmd, speed_cap);
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[ROLL GUARD] cap speed %.2f->%.2f m/s roll=%.2fdeg limit=%.2fdeg release=%.2fdeg roll_rate=%.2fdeg/s",
                old_u_cmd, u_cmd, roll_deg, roll_guard_limit_deg_, roll_guard_release_deg_,
                std::abs(current_roll_rate_) * 180.0 / M_PI);
        }
    } else {
        roll_guard_active_ = false;
    }

    if (turn_segment_speed_gate_enabled_ && !dp_mode_active_ && !final_speed_coupling_blocked) {
        const double ground_vx =
            std::cos(current_yaw_) * current_u_ - std::sin(current_yaw_) * current_v_;
        const double ground_vy =
            std::sin(current_yaw_) * current_u_ + std::cos(current_yaw_) * current_v_;
        const double cross_track_rate_mps =
            ground_vy * std::cos(alpha_k) - ground_vx * std::sin(alpha_k);
        const double path_heading_error_deg = path_heading_error_abs * 180.0 / M_PI;
        const double yaw_rate_deg_s = std::abs(current_r_) * 180.0 / M_PI;
        const double turn_segment_abs_xte =
            std::max(std::abs(e), raw_abs_xte_for_rejoin);
        const bool turn_segment_stable =
            turn_segment_abs_xte <= turn_segment_release_xte_m_ &&
            path_heading_error_deg <= turn_segment_release_heading_error_deg_ &&
            std::abs(cross_track_rate_mps) <= turn_segment_release_cross_track_rate_mps_ &&
            yaw_rate_deg_s <= turn_segment_release_yaw_rate_deg_s_;
        if (turn_segment_heading_limited && !turn_segment_stable) {
            turn_segment_speed_gate_active_ = true;
        }

        if (turn_segment_speed_gate_active_) {
            if (turn_segment_stable) {
                turn_segment_speed_gate_active_ = false;
                RCLCPP_INFO(this->get_logger(),
                    "[TURN SEGMENT SPEED GATE] released xte=%.1fm raw_xte=%.1fm hdg=%.1fdeg e_dot=%.2fmps yaw_rate=%.2fdeg_s",
                    turn_segment_abs_xte, raw_abs_xte_for_rejoin, path_heading_error_deg,
                    std::abs(cross_track_rate_mps), yaw_rate_deg_s);
            } else {
                const double speed_cap = std::clamp(
                    turn_segment_speed_cap_mps_,
                    minimum_steerage_speed_,
                    std::max(max_speed_, minimum_steerage_speed_));
                const double old_u_cmd = u_cmd;
                u_cmd = std::min(u_cmd, speed_cap);
                RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[TURN SEGMENT SPEED GATE] cap %.2f->%.2f m/s turn=%d xte=%.1f raw_xte=%.1f limit=%.1fm hdg=%.1f/%.1fdeg e_dot=%.2f/%.2fmps yaw_rate=%.2f/%.2fdeg_s",
                    old_u_cmd, u_cmd, turn_segment_heading_limited ? 1 : 0,
                    std::abs(e), raw_abs_xte_for_rejoin, turn_segment_release_xte_m_,
                    path_heading_error_deg, turn_segment_release_heading_error_deg_,
                    std::abs(cross_track_rate_mps), turn_segment_release_cross_track_rate_mps_,
                    yaw_rate_deg_s, turn_segment_release_yaw_rate_deg_s_);
            }
        }
    } else {
        turn_segment_speed_gate_active_ = false;
    }

    // [Fix-C v2] 前向速度预判：只看300m内的速度覆盖点，防止远距离FAP误限速
    {
        double min_upcoming_speed = max_speed_;
        double dist_accumulated = 0.0;
        for (int k = target_wp_idx; k + 1 < (int)waypoints_.size(); ++k) {
            double seg = std::hypot(waypoints_[k+1].x - waypoints_[k].x, waypoints_[k+1].y - waypoints_[k].y);
            dist_accumulated += seg;
            if (dist_accumulated > 300.0) break;
            const bool transient_turn_speed =
                !waypoints_[k].speed_override_is_route_limit &&
                (waypoints_[k].is_arc_point || waypoints_[k].turn_angle_deg > 1.0);
            if (waypoints_[k].speed_override > 0.1 && !transient_turn_speed) {
                min_upcoming_speed = std::min(min_upcoming_speed, waypoints_[k].speed_override);
            }
        }
        if (min_upcoming_speed < max_speed_) {
            u_cmd = std::min(u_cmd, min_upcoming_speed);
        }
    }

    // 重捕获速度门控：当新航线接入、开局偏航或超出软航道时，先低速建立可控曲率，
    // 稳定后再交给 cruise recovery gate 恢复巡航，避免高速带大航向误差进入蛇形。
    if (rejoin_speed_gate_enabled_ && !dp_mode_active_ && !heading_align_rejoin_now) {
        const double rejoin_heading_error_deg = heading_error_abs * 180.0 / M_PI;
        const double rejoin_xte_limit = std::max(
            rejoin_cross_track_m_,
            corridor_guidance_enabled_ ? corridor_soft_width_m_ : 0.0);
        const bool rejoin_heading_required =
            rejoin_heading_error_deg > rejoin_heading_error_deg_;
        const bool rejoin_lateral_required =
            rejoin_xte_limit > 0.0 && std::abs(e) > rejoin_xte_limit;
        const bool rejoin_gate_required =
            (rejoin_heading_required || rejoin_lateral_required) && !final_speed_coupling_blocked;
        if (rejoin_gate_required) {
            const bool severe_heading =
                rejoin_heading_error_deg > rejoin_severe_heading_error_deg_;
            const double requested_cap = severe_heading
                ? std::min(rejoin_speed_cap_mps_, rejoin_severe_speed_cap_mps_)
                : rejoin_speed_cap_mps_;
            const double rejoin_cap = std::clamp(requested_cap, 0.5, std::max(max_speed_, 0.5));
            const double old_u_cmd = u_cmd;
            u_cmd = std::min(u_cmd, rejoin_cap);
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[REJOIN SPEED GATE] cap %.2f->%.2f m/s hdg=%.1f/%.1f/%.1fdeg xte=%.1f/%.1fm severe=%d final=%d",
                old_u_cmd, u_cmd, rejoin_heading_error_deg, rejoin_heading_error_deg_,
                rejoin_severe_heading_error_deg_, std::abs(e), rejoin_xte_limit,
                severe_heading ? 1 : 0, final_speed_coupling_blocked ? 1 : 0);
        }
    }

    // 稳定巡航速度恢复 gate：先用可控的 6m/s 建立航迹，横偏/横偏速度/艏摇角速度
    // 连续稳定后再缓慢恢复到 8m/s；一旦失稳立即退回 6m/s。
    if (cruise_speed_recovery_enabled_) {
        const rclcpp::Time now = this->now();
        double dt_recovery = (now - cruise_recovery_last_time_).seconds();
        if (dt_recovery <= 0.0 || dt_recovery > 2.0) {
            dt_recovery = 0.5;
        }
        cruise_recovery_last_time_ = now;

        const double ground_vx =
            std::cos(current_yaw_) * current_u_ - std::sin(current_yaw_) * current_v_;
        const double ground_vy =
            std::sin(current_yaw_) * current_u_ + std::cos(current_yaw_) * current_v_;
        const double cross_track_rate_mps =
            ground_vy * std::cos(alpha_k) - ground_vx * std::sin(alpha_k);
        const double yaw_rate_deg_s = std::abs(current_r_) * 180.0 / M_PI;
        const double abs_xte = std::max(std::abs(e), raw_abs_xte_for_rejoin);
        const double abs_xte_dot = std::abs(cross_track_rate_mps);
        const double signed_xte_for_rate = raw_route_ref.valid ? raw_route_ref.cross_track_error : e;
        const double outward_xte_rate_mps =
            std::abs(signed_xte_for_rate) > 1.0
                ? std::copysign(1.0, signed_xte_for_rate) * cross_track_rate_mps
                : 0.0;
        double wind_cruise_cross_force_n = 0.0;
        bool wind_cruise_relax_available = false;
        if (wind_cruise_speed_relax_enabled_ && !current_guidance_active) {
            double env_fx_body = 0.0;
            double env_fy_body = 0.0;
            double env_yaw_rad = 0.0;
            double env_age_s = std::numeric_limits<double>::infinity();
            bool env_valid = false;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                env_fx_body = current_env_fx_n_;
                env_fy_body = current_env_fy_n_;
                env_yaw_rad = current_yaw_;
                env_valid = env_load_received_;
                if (env_valid) {
                    env_age_s = (this->now() - last_env_load_time_).seconds();
                }
            }
            if (env_valid && env_age_s <= wind_rejoin_stale_timeout_s_) {
                const double cos_yaw = std::cos(env_yaw_rad);
                const double sin_yaw = std::sin(env_yaw_rad);
                const double env_fx_world = cos_yaw * env_fx_body - sin_yaw * env_fy_body;
                const double env_fy_world = sin_yaw * env_fx_body + cos_yaw * env_fy_body;
                const double path_ex = std::cos(alpha_k);
                const double path_ey = std::sin(alpha_k);
                wind_cruise_cross_force_n = env_fy_world * path_ex - env_fx_world * path_ey;
                wind_cruise_relax_available =
                    std::abs(wind_cruise_cross_force_n) >= wind_cruise_relax_min_cross_force_n_;
            }
        }
        const bool straight_cruise_window =
            !turn_speed_coupling_active && !final_speed_coupling_blocked && !dp_mode_active_;
        const bool corridor_speed_window =
            corridor_guidance_enabled_ && corridor_half_width_m_ > 1e-6 && !final_speed_coupling_blocked;
        const double corridor_allowance_m = corridor_speed_window ? corridor_half_width_m_ : 0.0;
        const double cruise_recovery_xte_m = corridor_speed_window
            ? std::max(0.0, abs_xte - corridor_allowance_m)
            : abs_xte;
        const bool base_stable_now = straight_cruise_window
            && cruise_recovery_xte_m <= cruise_recovery_max_xte_m_
            && abs_xte_dot <= cruise_recovery_max_xte_dot_mps_
            && yaw_rate_deg_s <= cruise_recovery_max_yaw_rate_deg_s_;
        const bool stable_now = base_stable_now
            && (!wind_cruise_relax_available
                || outward_xte_rate_mps <= wind_cruise_full_outward_xte_dot_mps_);
        const bool wind_mid_xte_rate_ready =
            abs_xte_dot <= wind_cruise_mid_xte_dot_mps_ || outward_xte_rate_mps <= 0.0;
        const bool wind_mid_ready = wind_cruise_relax_available
            && straight_cruise_window
            && abs_xte <= wind_cruise_mid_xte_m_
            && wind_mid_xte_rate_ready
            && outward_xte_rate_mps <= wind_cruise_mid_outward_xte_dot_mps_
            && yaw_rate_deg_s <= wind_cruise_mid_yaw_rate_deg_s_;
        const bool wind_full_ready = wind_cruise_relax_available
            && straight_cruise_window
            && cruise_recovery_xte_m <= wind_cruise_full_xte_m_
            && abs_xte_dot <= wind_cruise_full_xte_dot_mps_
            && outward_xte_rate_mps <= wind_cruise_full_outward_xte_dot_mps_
            && yaw_rate_deg_s <= cruise_recovery_max_yaw_rate_deg_s_;
        const bool xte_rate_fallback =
            wind_cruise_relax_available
                ? outward_xte_rate_mps > cruise_fallback_xte_dot_mps_
                : abs_xte_dot > cruise_fallback_xte_dot_mps_;
        const bool fallback_now = !straight_cruise_window
            || (!wind_mid_ready && (
                abs_xte > cruise_fallback_xte_m_
                || xte_rate_fallback
                || yaw_rate_deg_s > cruise_fallback_yaw_rate_deg_s_));

        if (fallback_now) {
            cruise_recovery_gate_cleared_ = false;
            cruise_recovery_stable_timer_active_ = false;
            cruise_speed_cap_mps_ = cruise_base_speed_mps_;
            const bool current_cruise_rejoin_available =
                current_guidance_active &&
                abs_xte > current_rejoin_release_m_ + 1.0 &&
                !final_speed_coupling_blocked &&
                !dp_mode_active_;
            if (current_cruise_rejoin_available) {
                cruise_speed_cap_mps_ = std::max(
                    cruise_speed_cap_mps_,
                    std::min(max_speed_, current_rejoin_speed_cap_mps_));
            }
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                "[CRUISE SPEED GATE] fallback cap=%.2f xte=%.2fm eff_xte=%.2fm lane=%.1fm xte_dot=%.3fm/s outward=%.3fm/s yaw_rate=%.2fdeg/s straight=%d current=%d",
                cruise_speed_cap_mps_, abs_xte, cruise_recovery_xte_m, corridor_allowance_m,
                abs_xte_dot, outward_xte_rate_mps, yaw_rate_deg_s, straight_cruise_window ? 1 : 0,
                current_cruise_rejoin_available ? 1 : 0);
        } else if (stable_now || wind_full_ready) {
            if (!cruise_recovery_stable_timer_active_) {
                cruise_recovery_stable_timer_active_ = true;
                cruise_recovery_stable_since_ = now;
            }
            const double stable_s = (now - cruise_recovery_stable_since_).seconds();
            const double required_stable_s = wind_full_ready
                ? std::min(cruise_recovery_required_stable_s_, wind_cruise_full_stable_s_)
                : cruise_recovery_required_stable_s_;
            if (!cruise_recovery_gate_cleared_ && stable_s >= required_stable_s) {
                cruise_recovery_gate_cleared_ = true;
                RCLCPP_INFO(this->get_logger(),
                    "[CRUISE SPEED GATE] cleared after %.1fs: xte=%.2fm eff_xte=%.2fm lane=%.1fm xte_dot=%.3fm/s outward=%.3fm/s yaw_rate=%.2fdeg/s",
                    stable_s, abs_xte, cruise_recovery_xte_m, corridor_allowance_m,
                    abs_xte_dot, outward_xte_rate_mps, yaw_rate_deg_s);
            }
            if (cruise_recovery_gate_cleared_) {
                cruise_speed_cap_mps_ = std::min(
                    cruise_recovery_target_speed_mps_,
                    cruise_speed_cap_mps_ + std::max(0.0, cruise_recovery_ramp_mps2_) * dt_recovery);
            } else {
                cruise_speed_cap_mps_ = cruise_base_speed_mps_;
            }
        } else if (wind_mid_ready) {
            cruise_recovery_gate_cleared_ = false;
            cruise_recovery_stable_timer_active_ = false;
            cruise_speed_cap_mps_ = std::min(
                wind_cruise_mid_speed_mps_,
                cruise_speed_cap_mps_ + std::max(0.0, cruise_recovery_ramp_mps2_) * dt_recovery);
            cruise_speed_cap_mps_ = std::max(cruise_speed_cap_mps_, cruise_base_speed_mps_);
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[WIND CRUISE MID] cap=%.2f cross_force=%.0fN xte=%.1fm eff_xte=%.1fm xte_dot=%.2fm/s outward=%.2fm/s yaw_rate=%.2fdeg/s",
                cruise_speed_cap_mps_, wind_cruise_cross_force_n, abs_xte,
                cruise_recovery_xte_m, abs_xte_dot, outward_xte_rate_mps, yaw_rate_deg_s);
        } else {
            cruise_recovery_gate_cleared_ = false;
            cruise_recovery_stable_timer_active_ = false;
            cruise_speed_cap_mps_ = cruise_base_speed_mps_;
        }

        u_cmd = std::min(u_cmd, cruise_speed_cap_mps_);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[CRUISE SPEED GATE] u_cmd=%.2f cap=%.2f gate=%d xte=%.2fm eff_xte=%.2fm lane=%.1fm xte_dot=%.3fm/s outward=%.3fm/s yaw_rate=%.2fdeg/s",
            u_cmd, cruise_speed_cap_mps_, cruise_recovery_gate_cleared_ ? 1 : 0,
            abs_xte, cruise_recovery_xte_m, corridor_allowance_m, abs_xte_dot,
            outward_xte_rate_mps, yaw_rate_deg_s);
    }

    // [终点减速] 直接用到终点的欧氏距离，不依赖路线进度计算
    {
        double dist_to_final = std::hypot(current_x_ - waypoints_.back().x,
                                          current_y_ - waypoints_.back().y);
        if ((int)waypoints_.size() >= 2 &&
            current_wp_idx_ >= (int)waypoints_.size() - 2) {
            if (dist_to_final < terminal_slow_down_dist) {
                double ratio = dist_to_final / terminal_slow_down_dist;
                double u_slow = ratio * max_speed_;
                if (std::abs(current_cross_track_error_) > std::max(final_reacquire_cross_track_m_, 0.0)) {
                    u_slow = std::max(u_slow, minimum_steerage_speed_);
                }
                u_cmd = std::min(u_cmd, std::max(u_slow, 0.5));
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[FINAL SLOW] dist=%.1fm ratio=%.2f u=%.2f",
                    dist_to_final, ratio, u_cmd);
            }
            if (dist_to_final < final_capture_radius_) {
                const double terminal_hold_speed = std::clamp(final_capture_speed_, 0.3, max_speed_);
                u_cmd = std::min(u_cmd, terminal_hold_speed);
                RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
                    "[FINAL APPROACH] dist=%.2fm < cap=%.1fm, keep %.2fm/s until DP gate",
                    dist_to_final, final_capture_radius_, u_cmd);
            }
        }
    }

    const double final_reacquire_xte = std::max(final_reacquire_cross_track_m_, 0.0);
    const double final_reacquire_speed = std::clamp(
        final_reacquire_min_speed_mps_ > 0.0 ? final_reacquire_min_speed_mps_ : minimum_steerage_speed_,
        0.1,
        max_speed_);
    const bool final_reacquire_window_active =
        is_last_wp && final_waypoint_requires_dp && (dist_to_wp <= terminal_slow_down_dist);
    if (!heading_align_rejoin_now && is_last_wp && final_reacquire_window_active &&
        dist_to_wp > final_stop_radius_ && std::abs(e) > final_reacquire_xte) {
        u_cmd = std::max(u_cmd, final_reacquire_speed);
        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[终端重捕获] e=%.1fm > %.1fm dist=%.1fm <= %.1fm, 保持舵效速度 %.2f m/s",
            std::abs(e), final_reacquire_xte, dist_to_wp, terminal_slow_down_dist, u_cmd);
    }

    // 【保底舵效】只要离航点还远，速度不能低于舵效阈值
    // 45M FCB 舵在1.5m/s时约10kN侧向力，足够完成转弯
    const bool active_cruise_navigation_mode =
        active_navigation_mode.empty() ||
        active_navigation_mode == "cruise" ||
        active_navigation_mode == "open_water_cruise" ||
        active_navigation_mode == "post_turn_cruise" ||
        active_navigation_mode == "transit";
    const bool final_dp_low_speed_wait =
        final_dp_terminal_point_capture && final_dp_waiting_lateral_ready;
    const bool cruise_speed_floor_allowed =
        active_cruise_navigation_mode &&
        !roll_guard_active_ &&
        !emergency_avoidance_active &&
        !final_dp_low_speed_wait &&
        dist_to_wp > 10.0 &&
        (!is_last_wp || dist_to_wp > terminal_slow_down_dist);
    const double active_min_speed =
        cruise_speed_floor_allowed ? cruise_min_speed_mps_ : minimum_steerage_speed_;
    if (!heading_align_rejoin_now && !final_dp_low_speed_wait &&
        dist_to_wp > 10.0 && (!is_last_wp || dist_to_wp > terminal_slow_down_dist)) {
        const double old_u_cmd = u_cmd;
        u_cmd = std::max(u_cmd, active_min_speed);
        if (u_cmd > old_u_cmd + 1e-6 && cruise_speed_floor_allowed) {
            RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[CRUISE MIN SPEED] floor %.2f->%.2f m/s mode=%s dist=%.1fm final=%d roll_guard=%d",
                old_u_cmd, u_cmd, active_navigation_mode.c_str(), dist_to_wp,
                is_last_wp ? 1 : 0, roll_guard_active_ ? 1 : 0);
        }
    }

    if (heading_align_rejoin_now) {
        const double old_u_cmd = u_cmd;
        u_cmd = std::min(u_cmd, heading_align_speed_mps_);
        if (cruise_speed_floor_allowed) {
            u_cmd = std::max(u_cmd, cruise_min_speed_mps_);
        }
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[HEADING ALIGN SPEED] enforce u_cmd=%.2fmps cap=%.2fmps cruise_floor=%.2fmps applied=%d prev=%.2fmps",
            u_cmd, heading_align_speed_mps_, cruise_min_speed_mps_,
            cruise_speed_floor_allowed ? 1 : 0, old_u_cmd);
    }

    if (emergency_avoidance_active && !dp_mode_active_ && !final_speed_coupling_blocked) {
        const double emergency_cap = std::clamp(
            emergency_avoidance_speed_cap_mps_,
            0.5,
            std::max(max_speed_, 0.5));
        const double old_u_cmd = u_cmd;
        u_cmd = std::min(u_cmd, emergency_cap);
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
            "[EMERGENCY AVOIDANCE] cap %.2f->%.2f m/s nav=%s prev_nav=%s wheel=%.1fm max_xte=%.1fm",
            old_u_cmd, u_cmd, target_navigation_mode.c_str(), previous_navigation_mode.c_str(),
            emergency_avoidance_wheel_over_distance_m_, emergency_avoidance_switch_max_xte_m_);
    }

    psi_cmd = apply_heading_rate_limit(psi_cmd, cmd_dt);

    // 记录状态供下一周期使用
    psi_cmd_prev_ = psi_cmd;
    last_time_ = this->now();

    // ========== 调试日志 ==========
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "[LOS] wp[%d]:(%.0f,%.0f)->(%.0f,%.0f) | e=%.2f s=%.2f/%.2f | ψ=%.1f° u=%.2f | β=%.1f°",
        current_wp_idx_, wp_prev.x, wp_prev.y, wp_next.x, wp_next.y,
        e, s, seg_len, psi_cmd*180/M_PI, u_cmd, beta_hat_*180/M_PI);
}

void ShipGuidanceNode::control_loop()
{
    if (!odom_received_) {
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[GUIDANCE] 等待里程计数据...");
        return;
    }

    double x, y, yaw;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        x = current_x_;
        y = current_y_;
        yaw = current_yaw_;
    }

    // ========== 检查终点模式 (需要同时满足距离和速度条件) ==========
    double psi_cmd = 0.0;  // Declare early for speed check branch
    double u_cmd = 0.0;
    if ((wait_for_route_plan_ && !path_from_callback_) || waypoints_.empty()) {
        std_msgs::msg::Float64 stop_msg;
        stop_msg.data = 0.0;
        target_speed_pub_->publish(stop_msg);

        std_msgs::msg::Float64 heading_msg;
        heading_msg.data = yaw;
        heading_setpoint_pub_->publish(heading_msg);

        RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
            "[GUIDANCE] waiting for external RoutePlan; holding speed=0");
        return;
    }
if (current_wp_idx_ >= (int)waypoints_.size()) {
    Waypoint last_wp = waypoints_.back();
    double dist = std::hypot(last_wp.x - x, last_wp.y - y);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "[GUIDANCE] ★ 航线完成 终点(%.1f,%.1f) 当前(%.1f,%.1f) dist=%.1fm → DP",
        last_wp.x, last_wp.y, x, y, dist);

    geometry_msgs::msg::PoseStamped pose_msg;
    pose_msg.header.stamp = this->now();
    pose_msg.header.frame_id = "odom";
    pose_msg.pose.position.x = last_wp.x;
    pose_msg.pose.position.y = last_wp.y;
    pose_msg.pose.position.z = current_cross_track_error_;
    tf2::Quaternion q;
    double fallback_hold_heading = current_yaw_;
    double env_force_norm = 0.0;
    double env_age_s = 0.0;
    const bool weather_heading_active = select_dp_weathervane_heading(
        current_yaw_, fallback_hold_heading, env_force_norm, env_age_s);
    q.setRPY(0, 0, fallback_hold_heading);
    pose_msg.pose.orientation = tf2::toMsg(q);
    target_pose_pub_->publish(pose_msg);
    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "[GUIDANCE] route completed DP fallback psi=%.1fdeg weather=%d env=%.0fN age=%.2fs",
        fallback_hold_heading * 180.0 / M_PI,
        weather_heading_active ? 1 : 0, env_force_norm, env_age_s);

    std_msgs::msg::Float64 stop_msg;
    stop_msg.data = 0.0;
    target_speed_pub_->publish(stop_msg);
    return;
}

    // ========== 计算 LOS 导引 ==========
    calculate_los(x, y, psi_cmd, u_cmd);
    if (dp_mode_active_ || final_dp_overrun_braking_active_) {
        return;
    }

    // ========== 发布控制设定值 ==========
    std_msgs::msg::Float64 heading_msg;
    heading_msg.data = psi_cmd;

    std_msgs::msg::Float64 speed_msg;
    speed_msg.data = u_cmd;

    target_speed_pub_->publish(speed_msg);
    heading_setpoint_pub_->publish(heading_msg);

    // 【修复】正常航行时也发布 target_pose，让控制节点能显示正确的航点信息
    geometry_msgs::msg::PoseStamped pose_msg;
    pose_msg.header.stamp = this->now();
    pose_msg.header.frame_id = "odom";
    Waypoint wp_next;
    if (waypoints_.size() == 1) {
        wp_next = waypoints_[0];
    } else if (current_wp_idx_ == 0) {
        wp_next = path_from_callback_ ? waypoints_[0] : waypoints_[1];
    } else {
        wp_next = waypoints_[current_wp_idx_];
    }
    pose_msg.pose.position.x = wp_next.x;
    pose_msg.pose.position.y = wp_next.y;
    pose_msg.pose.position.z = current_cross_track_error_;
    tf2::Quaternion q;
    q.setRPY(0, 0, psi_cmd);
    pose_msg.pose.orientation = tf2::toMsg(q);
    target_pose_pub_->publish(pose_msg);

    RCLCPP_INFO_THROTTLE(this->get_logger(), *this->get_clock(), 2000,
        "[GUIDANCE] -> ψ_cmd=%.1f° u=%.2fm/s | idx=%d/%zu | target=(%.1f,%.1f) | XTE=%.1fm",
        psi_cmd*180/M_PI, u_cmd, current_wp_idx_, waypoints_.size(), wp_next.x, wp_next.y, current_cross_track_error_);
}

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ShipGuidanceNode>());
    rclcpp::shutdown();
    return 0;
}
