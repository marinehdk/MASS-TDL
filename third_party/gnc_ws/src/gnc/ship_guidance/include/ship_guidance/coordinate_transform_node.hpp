#ifndef COORDINATE_TRANSFORM_NODE_HPP
#define COORDINATE_TRANSFORM_NODE_HPP

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/path.hpp>
#include <geometry_msgs/msg/point.hpp>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <std_msgs/msg/float64.hpp>
#include <ship_interfaces/msg/route_plan.hpp>
#include <nav_msgs/msg/odometry.hpp>
#include <ship_interfaces/msg/geo_position.hpp>
#include <ship_interfaces/msg/route_plan_status.hpp>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

struct NedPoint {
    double x;
    double y;
    double speed_override = -1.0;
    bool is_arc_point = false;
    bool speed_override_is_route_limit = false;
    int navigation_mode_code = 0;
};

class CoordinateTransformNode : public rclcpp::Node {
public:
    CoordinateTransformNode();
    ~CoordinateTransformNode() = default;

private:
    void route_callback(const ship_interfaces::msg::RoutePlan::SharedPtr msg);
    void odom_callback(const nav_msgs::msg::Odometry::SharedPtr msg);
    void target_pose_callback(const geometry_msgs::msg::PoseStamped::SharedPtr msg);
    void speed_setpoint_callback(const std_msgs::msg::Float64::SharedPtr msg);
    void publish_route_status(
        const ship_interfaces::msg::RoutePlan& route,
        bool accepted,
        const std::string& status,
        const std::string& reason,
        size_t waypoint_count = 0,
        size_t internal_waypoint_count = 0,
        double total_distance_m = 0.0,
        bool speed_limit_valid = true,
        bool navigation_mode_valid = true,
        double current_along_track_m = std::numeric_limits<double>::quiet_NaN(),
        double first_changed_distance_ahead_m = std::numeric_limits<double>::quiet_NaN(),
        double max_lateral_delta_m = std::numeric_limits<double>::quiet_NaN());
    void wgs84_to_local(double lat, double lon,
                        double lat0, double lon0,
                        double& x, double& y);
    double compute_cross_track_error(double x, double y, int& segment_idx) const;
    double compute_cross_track_error(
        double x, double y, const std::vector<NedPoint>& path, int& segment_idx) const;
    double compute_along_track_progress(double x, double y, const std::vector<NedPoint>& path) const;
    double compute_max_lateral_delta(const std::vector<NedPoint>& candidate,
                                     const std::vector<NedPoint>& reference) const;
    int first_geometry_change_index(const std::vector<NedPoint>& candidate,
                                    const std::vector<NedPoint>& reference) const;
    bool has_reverse_segment(const std::vector<NedPoint>& path) const;
    bool compute_initial_route_yaw(const nav_msgs::msg::Path& path, double& yaw_rad, double& segment_len_m) const;
    void init_feedback_log();
    void write_feedback_log(const ship_interfaces::msg::GeoPosition& msg);
    std::vector<NedPoint> smooth_path(const std::vector<NedPoint>& raw_pts);
    std::vector<NedPoint> insert_arc(const NedPoint& A, const NedPoint& B, const NedPoint& C, double radius, double spacing);
    std::vector<NedPoint> insert_fap(const std::vector<NedPoint>& pts, double fap_dist, double fap_speed);

    rclcpp::Subscription<ship_interfaces::msg::RoutePlan>::SharedPtr route_sub_;
    rclcpp::Publisher<nav_msgs::msg::Path>::SharedPtr path_pub_;
    rclcpp::Publisher<geometry_msgs::msg::Point>::SharedPtr origin_pub_;
    rclcpp::Publisher<std_msgs::msg::Float64>::SharedPtr initial_route_yaw_pub_;
    rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_sub_;
    rclcpp::Subscription<geometry_msgs::msg::PoseStamped>::SharedPtr target_pose_sub_;
    rclcpp::Subscription<std_msgs::msg::Float64>::SharedPtr speed_setpoint_sub_;
    rclcpp::Publisher<ship_interfaces::msg::GeoPosition>::SharedPtr geo_pos_pub_;
    rclcpp::Publisher<ship_interfaces::msg::RoutePlanStatus>::SharedPtr route_status_pub_;

    double earth_a_;
    double earth_e2_;
    std::string projection_mode_;
    double planner_meters_per_degree_ = 111320.0;
    std::string input_topic_;
    std::string output_topic_;
    std::string origin_topic_;

    bool enable_arc_smoothing_ = false;
    double arc_radius_m_ = 100.0;
    double shallow_arc_radius_m_ = 100.0;
    double shallow_arc_angle_deg_ = 35.0;
    double arc_sample_spacing_m_ = 15.0;
    double min_turn_angle_deg_ = 8.0;
    double arc_speed_mps_ = 4.0;
    int arc_smoothing_external_route_min_points_ = 40;
    double arc_smoothing_max_lateral_delta_m_ = 120.0;
    bool enable_fap_ = false;
    double fap_distance_m_ = 120.0;
    double fap_speed_mps_ = 1.0;
    double pre_arc_decel_dist_m_ = 150.0;

    bool enable_route_update_guard_ = true;
    double min_route_update_interval_s_ = 10.0;
    double min_future_update_distance_m_ = 500.0;
    double max_dynamic_lateral_delta_m_ = 100.0;
    bool emergency_avoidance_relax_update_guard_ = true;
    double emergency_avoidance_min_future_update_distance_m_ = 150.0;
    double emergency_avoidance_max_dynamic_lateral_delta_m_ = 500.0;
    bool reject_reverse_segments_ = true;

    bool origin_published_;
    bool origin_locked_ = false;
    double lat0_rad_ = 0.0;
    double M0_ = 0.0;
    double N0_ = 0.0;
    double origin_lat_;
    double origin_lon_;

    std::vector<double> last_lats_;
    std::vector<double> last_lons_;
    std::vector<double> last_speed_limits_;
    std::vector<std::string> last_navigation_modes_;
    std::string last_route_id_;
    std::string last_route_type_;
    std::vector<NedPoint> last_local_path_;
    std::vector<NedPoint> last_feedback_path_;
    bool has_last_route_ = false;
    rclcpp::Time last_accepted_route_time_;
    bool has_odom_ = false;
    double current_x_ = 0.0;
    double current_y_ = 0.0;

    bool feedback_log_enabled_ = false;
    double feedback_log_period_s_ = 0.5;
    std::string feedback_log_dir_;
    std::string feedback_log_path_;
    std::ofstream feedback_log_file_;
    rclcpp::Time last_feedback_log_time_;
    double smoothed_cross_track_error_m_ = std::numeric_limits<double>::quiet_NaN();
    double smoothed_cross_track_error_abs_m_ = std::numeric_limits<double>::quiet_NaN();
    int smoothed_cross_track_segment_idx_ = -1;
    double xte_reference_mismatch_warn_m_ = 80.0;
    double roll_limit_deg_ = 10.0;
    double guidance_cross_track_error_m_ = std::numeric_limits<double>::quiet_NaN();
    double guidance_cross_track_error_abs_m_ = std::numeric_limits<double>::quiet_NaN();
    bool guidance_cross_track_valid_ = false;
    double last_target_pose_x_ = std::numeric_limits<double>::quiet_NaN();
    double last_target_pose_y_ = std::numeric_limits<double>::quiet_NaN();
    bool has_target_pose_ = false;
    double last_speed_setpoint_mps_ = std::numeric_limits<double>::quiet_NaN();
    bool has_speed_setpoint_ = false;
    bool feedback_dp_latched_ = false;
};

#endif // COORDINATE_TRANSFORM_NODE_HPP
