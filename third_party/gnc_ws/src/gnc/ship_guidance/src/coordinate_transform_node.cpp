#include "ship_guidance/coordinate_transform_node.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <algorithm>
#include <cctype>
#include <limits>

namespace {

std::string wall_time_string()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

std::string file_time_string()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

std::string csv_escape(const std::string& value)
{
    if (value.find_first_of(",\"\n\r") == std::string::npos) {
        return value;
    }
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char ch : value) {
        if (ch == '"') {
            escaped.push_back('"');
        }
        escaped.push_back(ch);
    }
    escaped.push_back('"');
    return escaped;
}

std::string normalize_navigation_mode(std::string mode)
{
    for (char& ch : mode) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (ch == '-' || ch == ' ') {
            ch = '_';
        }
    }
    return mode;
}

int navigation_mode_code(const std::string& raw_mode)
{
    const std::string mode = normalize_navigation_mode(raw_mode);
    if (mode.empty()) return 0;
    if (mode == "cruise" || mode == "open_water_cruise" || mode == "post_turn_cruise") return 1;
    if (mode == "narrow_channel") return 2;
    if (mode == "harbor") return 3;
    if (mode == "approach") return 4;
    if (mode == "dp_hold" || mode == "dp" || mode == "station_keeping") return 5;
    if (mode == "emergency_avoidance" ||
        mode == "emergency_avoid" ||
        mode == "collision_avoidance" ||
        mode == "avoidance") return 6;
    return 0;
}

bool vincenty_inverse(
    double lat1_deg, double lon1_deg,
    double lat2_deg, double lon2_deg,
    double semi_major_axis, double first_eccentricity_sq,
    double& distance_m, double& azimuth_rad)
{
    constexpr double deg_to_rad = M_PI / 180.0;
    constexpr double convergence_tol = 1e-12;
    constexpr int max_iterations = 100;

    const double phi1 = lat1_deg * deg_to_rad;
    const double phi2 = lat2_deg * deg_to_rad;
    double L = (lon2_deg - lon1_deg) * deg_to_rad;
    if (L > M_PI || L < -M_PI) {
        L = std::remainder(L, 2.0 * M_PI);
    }

    if (std::abs(phi2 - phi1) < 1e-15 && std::abs(L) < 1e-15) {
        distance_m = 0.0;
        azimuth_rad = 0.0;
        return true;
    }

    const double flattening = 1.0 - std::sqrt(std::max(0.0, 1.0 - first_eccentricity_sq));
    if (!std::isfinite(flattening) || flattening <= 0.0 || flattening >= 1.0 || semi_major_axis <= 0.0) {
        return false;
    }

    const double semi_minor_axis = semi_major_axis * (1.0 - flattening);
    const double U1 = std::atan((1.0 - flattening) * std::tan(phi1));
    const double U2 = std::atan((1.0 - flattening) * std::tan(phi2));
    const double sinU1 = std::sin(U1);
    const double cosU1 = std::cos(U1);
    const double sinU2 = std::sin(U2);
    const double cosU2 = std::cos(U2);

    double lambda = L;
    double sinSigma = 0.0;
    double cosSigma = 0.0;
    double sigma = 0.0;
    double sinAlpha = 0.0;
    double cosSqAlpha = 0.0;
    double cos2SigmaM = 0.0;
    bool converged = false;

    for (int iter = 0; iter < max_iterations; ++iter) {
        const double sinLambda = std::sin(lambda);
        const double cosLambda = std::cos(lambda);
        const double term1 = cosU2 * sinLambda;
        const double term2 = cosU1 * sinU2 - sinU1 * cosU2 * cosLambda;
        sinSigma = std::hypot(term1, term2);
        if (sinSigma == 0.0) {
            distance_m = 0.0;
            azimuth_rad = 0.0;
            return true;
        }
        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = std::atan2(sinSigma, cosSigma);
        sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cosSqAlpha = 1.0 - sinAlpha * sinAlpha;
        if (cosSqAlpha > 1e-15) {
            cos2SigmaM = cosSigma - 2.0 * sinU1 * sinU2 / cosSqAlpha;
        } else {
            cos2SigmaM = 0.0;
        }
        const double C = flattening / 16.0 * cosSqAlpha * (4.0 + flattening * (4.0 - 3.0 * cosSqAlpha));
        const double previous_lambda = lambda;
        lambda = L + (1.0 - C) * flattening * sinAlpha *
            (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma *
            (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));
        if (std::abs(lambda - previous_lambda) <= convergence_tol) {
            converged = true;
            break;
        }
    }

    if (!converged) {
        return false;
    }

    const double uSq = cosSqAlpha * (semi_major_axis * semi_major_axis - semi_minor_axis * semi_minor_axis) /
        (semi_minor_axis * semi_minor_axis);
    const double A = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
    const double B = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));
    const double deltaSigma = B * sinSigma * (cos2SigmaM + B / 4.0 *
        (cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM) -
         B / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma) *
         (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));

    distance_m = semi_minor_axis * A * (sigma - deltaSigma);
    azimuth_rad = std::atan2(
        cosU2 * std::sin(lambda),
        cosU1 * sinU2 - sinU1 * cosU2 * std::cos(lambda));
    return std::isfinite(distance_m) && std::isfinite(azimuth_rad);
}

}  // namespace

CoordinateTransformNode::CoordinateTransformNode()
    : Node("coordinate_transform_node"),
      origin_published_(false),
      origin_lat_(0.0),
      origin_lon_(0.0)
{
    this->declare_parameter("earth_semi_major_axis", 6378137.0);
    this->declare_parameter("earth_first_eccentricity_sq", 0.00669437999014);
    this->declare_parameter("projection_mode", "ellipsoid");
    this->declare_parameter("planner_meters_per_degree", 111320.0);
    this->declare_parameter("input_topic", "/route_planning/route_plan");
    this->declare_parameter("output_topic", "/ship/waypoints");
    this->declare_parameter("origin_topic", "/route_planning/origin_latlon");
    this->declare_parameter("enable_arc_smoothing", false);
    this->declare_parameter("arc_radius_m", 100.0);
    this->declare_parameter("shallow_arc_radius_m", 1200.0);
    this->declare_parameter("shallow_arc_angle_deg", 35.0);
    this->declare_parameter("arc_sample_spacing_m", 15.0);
    this->declare_parameter("min_turn_angle_deg", 8.0);
    this->declare_parameter("arc_speed_mps", 4.0);
    this->declare_parameter("arc_smoothing_external_route_min_points", 40);
    this->declare_parameter("arc_smoothing_max_lateral_delta_m", 120.0);
    this->declare_parameter("enable_fap", false);
    this->declare_parameter("fap_distance_m", 120.0);
    this->declare_parameter("pre_arc_decel_dist_m", 150.0);
    this->declare_parameter("fap_speed_mps", 1.0);
    this->declare_parameter("enable_route_update_guard", true);
    this->declare_parameter("min_route_update_interval_s", 10.0);
    this->declare_parameter("min_future_update_distance_m", 500.0);
    this->declare_parameter("max_dynamic_lateral_delta_m", 100.0);
    this->declare_parameter("emergency_avoidance_relax_update_guard", true);
    this->declare_parameter("emergency_avoidance_min_future_update_distance_m", 150.0);
    this->declare_parameter("emergency_avoidance_max_dynamic_lateral_delta_m", 500.0);
    this->declare_parameter("reject_reverse_segments", true);
    this->declare_parameter("feedback_log_enabled", true);
    this->declare_parameter("feedback_log_dir", "/tmp/ship_feedback_logs");
    this->declare_parameter("feedback_log_period_s", 0.5);
    this->declare_parameter("xte_reference_mismatch_warn_m", 80.0);
    this->declare_parameter("roll_limit_deg", 10.0);

    earth_a_ = this->get_parameter("earth_semi_major_axis").as_double();
    earth_e2_ = this->get_parameter("earth_first_eccentricity_sq").as_double();
    projection_mode_ = this->get_parameter("projection_mode").as_string();
    std::transform(projection_mode_.begin(), projection_mode_.end(), projection_mode_.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    planner_meters_per_degree_ = std::max(1.0, this->get_parameter("planner_meters_per_degree").as_double());
    if (projection_mode_ != "ellipsoid" &&
        projection_mode_ != "planner_equirectangular" &&
        projection_mode_ != "equirectangular" &&
        projection_mode_ != "geod_inverse" &&
        projection_mode_ != "vincenty") {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] unknown projection_mode='%s', fallback to ellipsoid",
            projection_mode_.c_str());
        projection_mode_ = "ellipsoid";
    }
    input_topic_ = this->get_parameter("input_topic").as_string();
    output_topic_ = this->get_parameter("output_topic").as_string();
    origin_topic_ = this->get_parameter("origin_topic").as_string();

    enable_arc_smoothing_ = this->get_parameter("enable_arc_smoothing").as_bool();
    arc_radius_m_         = this->get_parameter("arc_radius_m").as_double();
    min_turn_angle_deg_   = this->get_parameter("min_turn_angle_deg").as_double();
    shallow_arc_radius_m_ = std::max(
        arc_radius_m_, this->get_parameter("shallow_arc_radius_m").as_double());
    shallow_arc_angle_deg_ = std::clamp(
        this->get_parameter("shallow_arc_angle_deg").as_double(),
        min_turn_angle_deg_,
        60.0);
    arc_sample_spacing_m_ = this->get_parameter("arc_sample_spacing_m").as_double();
    arc_speed_mps_        = this->get_parameter("arc_speed_mps").as_double();
    arc_smoothing_external_route_min_points_ = std::max(
        0, static_cast<int>(
            this->get_parameter("arc_smoothing_external_route_min_points").as_int()));
    arc_smoothing_max_lateral_delta_m_ = std::max(
        0.0, this->get_parameter("arc_smoothing_max_lateral_delta_m").as_double());
    enable_fap_           = this->get_parameter("enable_fap").as_bool();
    fap_distance_m_       = this->get_parameter("fap_distance_m").as_double();
    pre_arc_decel_dist_m_ = std::max(
        0.0, this->get_parameter("pre_arc_decel_dist_m").as_double());
    fap_speed_mps_        = this->get_parameter("fap_speed_mps").as_double();
    enable_route_update_guard_ =
        this->get_parameter("enable_route_update_guard").as_bool();
    min_route_update_interval_s_ = std::max(
        0.0, this->get_parameter("min_route_update_interval_s").as_double());
    min_future_update_distance_m_ = std::max(
        0.0, this->get_parameter("min_future_update_distance_m").as_double());
    max_dynamic_lateral_delta_m_ = std::max(
        0.0, this->get_parameter("max_dynamic_lateral_delta_m").as_double());
    emergency_avoidance_relax_update_guard_ =
        this->get_parameter("emergency_avoidance_relax_update_guard").as_bool();
    emergency_avoidance_min_future_update_distance_m_ = std::max(
        0.0, this->get_parameter("emergency_avoidance_min_future_update_distance_m").as_double());
    emergency_avoidance_max_dynamic_lateral_delta_m_ = std::max(
        max_dynamic_lateral_delta_m_,
        this->get_parameter("emergency_avoidance_max_dynamic_lateral_delta_m").as_double());
    reject_reverse_segments_ =
        this->get_parameter("reject_reverse_segments").as_bool();
    feedback_log_enabled_ = this->get_parameter("feedback_log_enabled").as_bool();
    feedback_log_dir_ = this->get_parameter("feedback_log_dir").as_string();
    feedback_log_period_s_ = std::max(
        0.0, this->get_parameter("feedback_log_period_s").as_double());
    xte_reference_mismatch_warn_m_ = std::max(
        0.0, this->get_parameter("xte_reference_mismatch_warn_m").as_double());
    roll_limit_deg_ = std::max(0.0, this->get_parameter("roll_limit_deg").as_double());
    last_feedback_log_time_ = rclcpp::Time(0, 0, RCL_ROS_TIME);

    route_sub_ = this->create_subscription<ship_interfaces::msg::RoutePlan>(
        input_topic_, 10,
        std::bind(&CoordinateTransformNode::route_callback, this, std::placeholders::_1));

    auto transient_local_qos = rclcpp::QoS(10).transient_local().reliable();
    path_pub_ = this->create_publisher<nav_msgs::msg::Path>(output_topic_, transient_local_qos);
    origin_pub_ = this->create_publisher<geometry_msgs::msg::Point>(origin_topic_, 10);
    initial_route_yaw_pub_ = this->create_publisher<std_msgs::msg::Float64>(
        "/sim/initial_route_yaw", transient_local_qos);

    odom_sub_ = this->create_subscription<nav_msgs::msg::Odometry>(
        "/ship/odometry", 10,
        std::bind(&CoordinateTransformNode::odom_callback, this, std::placeholders::_1));
    target_pose_sub_ = this->create_subscription<geometry_msgs::msg::PoseStamped>(
        "/target_pose", 10,
        std::bind(&CoordinateTransformNode::target_pose_callback, this, std::placeholders::_1));
    speed_setpoint_sub_ = this->create_subscription<std_msgs::msg::Float64>(
        "/control/speed_setpoint", 10,
        std::bind(&CoordinateTransformNode::speed_setpoint_callback, this, std::placeholders::_1));

    geo_pos_pub_ = this->create_publisher<ship_interfaces::msg::GeoPosition>(
        "/ship/geo_position", 10);
    route_status_pub_ = this->create_publisher<ship_interfaces::msg::RoutePlanStatus>(
        "/route_planning/route_plan_status", 10);

    init_feedback_log();

    RCLCPP_INFO(this->get_logger(),
        "[CoordTransform] started sub=%s pub=%s origin=%s projection=%s a=%.1f e2=%.12f planner_m_per_deg=%.3f",
        input_topic_.c_str(), output_topic_.c_str(), origin_topic_.c_str(),
        projection_mode_.c_str(), earth_a_, earth_e2_, planner_meters_per_degree_);
}

void CoordinateTransformNode::wgs84_to_local(
    double lat, double lon,
    double lat0, double lon0,
    double& x, double& y)
{
    if (projection_mode_ == "geod_inverse" || projection_mode_ == "vincenty") {
        double distance_m = 0.0;
        double azimuth_rad = 0.0;
        if (vincenty_inverse(lat0, lon0, lat, lon, earth_a_, earth_e2_, distance_m, azimuth_rad)) {
            x = distance_m * std::cos(azimuth_rad);  // NED: x = North
            y = distance_m * std::sin(azimuth_rad);  // NED: y = East
            return;
        }
        RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
            "[CoordTransform] geod_inverse failed, fallback to local linear projection");
    }

    double lat0_rad = lat0 * M_PI / 180.0;
    double sin_lat0 = std::sin(lat0_rad);
    double cos_lat0 = std::cos(lat0_rad);

    double N0 = earth_a_ / std::sqrt(1.0 - earth_e2_ * sin_lat0 * sin_lat0);
    double M0 = earth_a_ * (1.0 - earth_e2_) /
                std::pow(1.0 - earth_e2_ * sin_lat0 * sin_lat0, 1.5);
    if (projection_mode_ == "planner_equirectangular" ||
        projection_mode_ == "equirectangular") {
        const double meters_per_radian = planner_meters_per_degree_ * 180.0 / M_PI;
        M0 = meters_per_radian;
        N0 = meters_per_radian;
    }

    double dlon_rad = (lon - lon0) * M_PI / 180.0;
    double dlat_rad = (lat - lat0) * M_PI / 180.0;

    x = dlat_rad * M0;              // NED: x = North
    y = dlon_rad * N0 * cos_lat0;  // NED: y = East
}

void CoordinateTransformNode::publish_route_status(
    const ship_interfaces::msg::RoutePlan& route,
    bool accepted,
    const std::string& status,
    const std::string& reason,
    size_t waypoint_count,
    size_t internal_waypoint_count,
    double total_distance_m,
    bool speed_limit_valid,
    bool navigation_mode_valid,
    double current_along_track_m,
    double first_changed_distance_ahead_m,
    double max_lateral_delta_m)
{
    ship_interfaces::msg::RoutePlanStatus out;
    out.header.stamp = this->now();
    out.header.frame_id = "route_plan_status";
    out.route_id = route.route_id;
    out.route_type = route.route_type;
    out.accepted = accepted;
    out.status = status;
    out.reason = reason;
    out.waypoint_count = static_cast<uint32_t>(waypoint_count);
    out.internal_waypoint_count = static_cast<uint32_t>(internal_waypoint_count);
    out.total_distance_m = total_distance_m;
    out.speed_limit_valid = speed_limit_valid;
    out.navigation_mode_valid = navigation_mode_valid;
    out.current_along_track_m = current_along_track_m;
    out.first_changed_distance_ahead_m = first_changed_distance_ahead_m;
    out.max_lateral_delta_m = max_lateral_delta_m;
    route_status_pub_->publish(out);
}

double CoordinateTransformNode::compute_along_track_progress(
    double x, double y, const std::vector<NedPoint>& path) const
{
    if (path.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double best_distance = std::numeric_limits<double>::infinity();
    double best_along = 0.0;
    double accumulated = 0.0;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const double dx = path[i + 1].x - path[i].x;
        const double dy = path[i + 1].y - path[i].y;
        const double seg_len_sq = dx * dx + dy * dy;
        if (seg_len_sq < 1e-9) {
            continue;
        }
        const double seg_len = std::sqrt(seg_len_sq);
        const double t = std::clamp(
            ((x - path[i].x) * dx + (y - path[i].y) * dy) / seg_len_sq,
            0.0, 1.0);
        const double px = path[i].x + t * dx;
        const double py = path[i].y + t * dy;
        const double dist = std::hypot(x - px, y - py);
        if (dist < best_distance) {
            best_distance = dist;
            best_along = accumulated + t * seg_len;
        }
        accumulated += seg_len;
    }
    return best_along;
}

bool CoordinateTransformNode::compute_initial_route_yaw(
    const nav_msgs::msg::Path& path, double& yaw_rad, double& segment_len_m) const
{
    if (path.poses.size() < 2) {
        return false;
    }

    for (size_t i = 1; i < path.poses.size(); ++i) {
        const auto& p0 = path.poses[i - 1].pose.position;
        const auto& p1 = path.poses[i].pose.position;
        const double dx_north = p1.x - p0.x;
        const double dy_east = p1.y - p0.y;
        segment_len_m = std::hypot(dx_north, dy_east);
        if (!std::isfinite(dx_north) || !std::isfinite(dy_east)
            || !std::isfinite(segment_len_m) || segment_len_m < 1.0) {
            continue;
        }
        yaw_rad = std::atan2(dy_east, dx_north);
        while (yaw_rad > M_PI) yaw_rad -= 2.0 * M_PI;
        while (yaw_rad < -M_PI) yaw_rad += 2.0 * M_PI;
        return true;
    }

    return false;
}

double CoordinateTransformNode::compute_max_lateral_delta(
    const std::vector<NedPoint>& candidate,
    const std::vector<NedPoint>& reference) const
{
    if (candidate.empty() || reference.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double max_lateral = 0.0;
    for (const auto& p : candidate) {
        double best = std::numeric_limits<double>::infinity();
        for (size_t i = 0; i + 1 < reference.size(); ++i) {
            const double dx = reference[i + 1].x - reference[i].x;
            const double dy = reference[i + 1].y - reference[i].y;
            const double seg_len = std::hypot(dx, dy);
            if (seg_len < 1e-6) {
                continue;
            }
            const double lateral = std::abs(dx * (p.y - reference[i].y) -
                                            dy * (p.x - reference[i].x)) / seg_len;
            best = std::min(best, lateral);
        }
        if (std::isfinite(best)) {
            max_lateral = std::max(max_lateral, best);
        }
    }
    return max_lateral;
}

int CoordinateTransformNode::first_geometry_change_index(
    const std::vector<NedPoint>& candidate,
    const std::vector<NedPoint>& reference) const
{
    const size_t n = std::min(candidate.size(), reference.size());
    for (size_t i = 0; i < n; ++i) {
        if (std::hypot(candidate[i].x - reference[i].x,
                       candidate[i].y - reference[i].y) > 1.0) {
            return static_cast<int>(i);
        }
    }
    if (candidate.size() != reference.size()) {
        return static_cast<int>(n);
    }
    return -1;
}

bool CoordinateTransformNode::has_reverse_segment(const std::vector<NedPoint>& path) const
{
    if (path.size() < 3) {
        return false;
    }
    constexpr double kReverseCosThreshold = -0.8660254037844386;  // 150 deg
    for (size_t i = 1; i + 1 < path.size(); ++i) {
        const double ax = path[i].x - path[i - 1].x;
        const double ay = path[i].y - path[i - 1].y;
        const double bx = path[i + 1].x - path[i].x;
        const double by = path[i + 1].y - path[i].y;
        const double al = std::hypot(ax, ay);
        const double bl = std::hypot(bx, by);
        if (al < 1e-6 || bl < 1e-6) {
            continue;
        }
        const double cos_turn = (ax * bx + ay * by) / (al * bl);
        if (cos_turn < kReverseCosThreshold) {
            return true;
        }
    }
    return false;
}

void CoordinateTransformNode::route_callback(
    const ship_interfaces::msg::RoutePlan::SharedPtr msg)
{
    const auto& lats = msg->latitude;
    const auto& lons = msg->longitude;

    auto reject_route = [&](const std::string& reason) {
        publish_route_status(*msg, false, "REJECTED", reason,
            lats.size(), 0, 0.0, false, false);
    };

    if (lats.empty() || lons.empty()) {
        RCLCPP_WARN(this->get_logger(), "[CoordTransform] rejected empty RoutePlan");
        reject_route("empty latitude/longitude arrays");
        return;
    }

    if (lats.size() != lons.size()) {
        RCLCPP_ERROR(this->get_logger(),
            "[CoordTransform] rejected RoutePlan: latitude(%zu) longitude(%zu) length mismatch",
            lats.size(), lons.size());
        reject_route("latitude/longitude length mismatch");
        return;
    }

    if (lats.size() < 2) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] rejected RoutePlan: at least two waypoints are required");
        reject_route("at least two waypoints are required");
        return;
    }

    for (size_t i = 0; i < lats.size(); ++i) {
        if (!std::isfinite(lats[i]) || !std::isfinite(lons[i]) ||
            std::abs(lats[i]) > 90.0 || std::abs(lons[i]) > 180.0) {
            RCLCPP_ERROR(this->get_logger(),
                "[CoordTransform] rejected RoutePlan: invalid WGS84 waypoint[%zu] lat=%.8f lon=%.8f",
                i, lats[i], lons[i]);
            reject_route("invalid WGS84 latitude/longitude");
            return;
        }
    }

    const bool has_speed_limits =
        !msg->speed_limit_mps.empty() &&
        msg->speed_limit_mps.size() == lats.size();
    const bool speed_limit_valid = msg->speed_limit_mps.empty() || has_speed_limits;
    if (!speed_limit_valid) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] speed_limit_mps(%zu) length mismatch with waypoint count(%zu); field ignored",
            msg->speed_limit_mps.size(), lats.size());
    }

    const bool navigation_mode_valid =
        msg->navigation_mode.empty() || msg->navigation_mode.size() == lats.size();
    if (!navigation_mode_valid) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] navigation_mode(%zu) length mismatch with waypoint count(%zu); field ignored",
            msg->navigation_mode.size(), lats.size());
    }

    if (has_last_route_ &&
        last_lats_.size() == lats.size() &&
        last_lons_.size() == lons.size()) {
        bool identical =
            last_speed_limits_.size() == msg->speed_limit_mps.size() &&
            last_navigation_modes_.size() == msg->navigation_mode.size() &&
            last_route_id_ == msg->route_id &&
            last_route_type_ == msg->route_type;
        for (size_t i = 0; i < lats.size(); ++i) {
            if (std::abs(lats[i] - last_lats_[i]) > 1e-12 ||
                std::abs(lons[i] - last_lons_[i]) > 1e-12) {
                identical = false;
                break;
            }
        }
        if (identical) {
            for (size_t i = 0; i < msg->speed_limit_mps.size(); ++i) {
                if (std::abs(msg->speed_limit_mps[i] - last_speed_limits_[i]) > 1e-6) {
                    identical = false;
                    break;
                }
            }
        }
        if (identical) {
            for (size_t i = 0; i < msg->navigation_mode.size(); ++i) {
                if (msg->navigation_mode[i] != last_navigation_modes_[i]) {
                    identical = false;
                    break;
                }
            }
        }
        if (identical) {
            RCLCPP_INFO(this->get_logger(),
                "[CoordTransform] ignored duplicate RoutePlan route_id='%s' waypoints=%zu",
                msg->route_id.c_str(), lats.size());
            publish_route_status(*msg, true, "IGNORED_DUPLICATE",
                "route geometry and metadata are identical to the active route",
                lats.size(), last_local_path_.size(), 0.0,
                speed_limit_valid, navigation_mode_valid);
            return;
        }
    }

    const bool first_route = !origin_published_;
    const double lat0 = first_route ? lats[0] : origin_lat_;
    const double lon0 = first_route ? lons[0] : origin_lon_;

    std::vector<NedPoint> raw_pts;
    raw_pts.reserve(lats.size());
    for (size_t i = 0; i < lats.size(); ++i) {
        double x = 0.0, y = 0.0;
        wgs84_to_local(lats[i], lons[i], lat0, lon0, x, y);
        NedPoint np;
        np.x = x;
        np.y = y;
        np.speed_override = -1.0;
        if (has_speed_limits &&
            std::isfinite(msg->speed_limit_mps[i]) &&
            msg->speed_limit_mps[i] > 0.0) {
            np.speed_override = msg->speed_limit_mps[i];
            np.speed_override_is_route_limit = true;
        }
        np.is_arc_point = false;
        if (navigation_mode_valid && !msg->navigation_mode.empty()) {
            np.navigation_mode_code = navigation_mode_code(msg->navigation_mode[i]);
        }
        raw_pts.push_back(np);
    }
    const bool has_emergency_avoidance = std::any_of(
        raw_pts.begin(), raw_pts.end(),
        [](const NedPoint& pt) { return pt.navigation_mode_code == 6; });
    const double active_min_future_update_distance =
        (has_emergency_avoidance && emergency_avoidance_relax_update_guard_)
            ? emergency_avoidance_min_future_update_distance_m_
            : min_future_update_distance_m_;
    const double active_max_dynamic_lateral_delta =
        (has_emergency_avoidance && emergency_avoidance_relax_update_guard_)
            ? emergency_avoidance_max_dynamic_lateral_delta_m_
            : max_dynamic_lateral_delta_m_;
    if (has_emergency_avoidance) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] emergency_avoidance route detected: raw geometry preserved, guard future>=%.1fm lateral<=%.1fm",
            active_min_future_update_distance, active_max_dynamic_lateral_delta);
    }

    double current_along_track = std::numeric_limits<double>::quiet_NaN();
    double first_changed_distance_ahead = std::numeric_limits<double>::quiet_NaN();
    double max_lateral_delta = std::numeric_limits<double>::quiet_NaN();

    if (reject_reverse_segments_ && has_reverse_segment(raw_pts)) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] rejected RoutePlan route_id='%s': reverse segment detected",
            msg->route_id.c_str());
        publish_route_status(*msg, false, "REJECTED",
            "reverse segment detected; split the turn or publish a smoother future route",
            lats.size(), 0, 0.0, speed_limit_valid, navigation_mode_valid,
            current_along_track, first_changed_distance_ahead, max_lateral_delta);
        return;
    }

    if (enable_route_update_guard_ && has_last_route_ && last_feedback_path_.size() >= 2) {
        const int first_changed_idx = first_geometry_change_index(raw_pts, last_feedback_path_);
        if (first_changed_idx >= 0) {
            const auto now = this->now();
            if (last_accepted_route_time_.nanoseconds() > 0) {
                const double elapsed_s = (now - last_accepted_route_time_).seconds();
                if (elapsed_s < min_route_update_interval_s_) {
                    RCLCPP_WARN(this->get_logger(),
                        "[CoordTransform] rejected RoutePlan route_id='%s': update interval %.1fs < %.1fs",
                        msg->route_id.c_str(), elapsed_s, min_route_update_interval_s_);
                    publish_route_status(*msg, false, "REJECTED",
                        "route update too frequent",
                        lats.size(), 0, 0.0, speed_limit_valid, navigation_mode_valid);
                    return;
                }
            }

            max_lateral_delta = compute_max_lateral_delta(raw_pts, last_feedback_path_);
            if (std::isfinite(max_lateral_delta) &&
                max_lateral_delta > active_max_dynamic_lateral_delta) {
                RCLCPP_WARN(this->get_logger(),
                    "[CoordTransform] rejected RoutePlan route_id='%s': max lateral delta %.1fm > %.1fm",
                    msg->route_id.c_str(), max_lateral_delta, active_max_dynamic_lateral_delta);
                publish_route_status(*msg, false, "REJECTED",
                    "dynamic route lateral offset exceeds limit; split the avoidance maneuver",
                    lats.size(), 0, 0.0, speed_limit_valid, navigation_mode_valid,
                    current_along_track, first_changed_distance_ahead, max_lateral_delta);
                return;
            }

            if (has_odom_ && first_changed_idx < static_cast<int>(raw_pts.size())) {
                current_along_track = compute_along_track_progress(
                    current_x_, current_y_, last_feedback_path_);
                const double changed_along_track = compute_along_track_progress(
                    raw_pts[first_changed_idx].x,
                    raw_pts[first_changed_idx].y,
                    last_feedback_path_);
                first_changed_distance_ahead = changed_along_track - current_along_track;
                if (std::isfinite(first_changed_distance_ahead) &&
                    first_changed_distance_ahead < active_min_future_update_distance) {
                    RCLCPP_WARN(this->get_logger(),
                        "[CoordTransform] rejected RoutePlan route_id='%s': first changed point %.1fm ahead < %.1fm",
                        msg->route_id.c_str(), first_changed_distance_ahead,
                        active_min_future_update_distance);
                    publish_route_status(*msg, false, "REJECTED",
                        "first changed waypoint is too close to current ship position",
                        lats.size(), 0, 0.0, speed_limit_valid, navigation_mode_valid,
                        current_along_track, first_changed_distance_ahead, max_lateral_delta);
                    return;
                }
            }
        }
    }

    std::vector<NedPoint> ned_pts = raw_pts;
    const bool external_planner_route =
        arc_smoothing_external_route_min_points_ > 0 &&
        raw_pts.size() >= static_cast<size_t>(arc_smoothing_external_route_min_points_);
    if (enable_arc_smoothing_ && ned_pts.size() >= 3) {
        if (has_emergency_avoidance) {
            RCLCPP_WARN(this->get_logger(),
                "[CoordTransform] arc smoothing bypassed for emergency_avoidance route_id='%s'",
                msg->route_id.c_str());
        } else if (external_planner_route) {
            RCLCPP_INFO(this->get_logger(),
                "[CoordTransform] arc smoothing bypassed for route_id='%s': external planner route has %zu points >= %d; using raw RoutePlan geometry",
                msg->route_id.c_str(), raw_pts.size(), arc_smoothing_external_route_min_points_);
        } else {
            auto smoothed_pts = smooth_path(ned_pts);
            const double smoothing_lateral_delta =
                compute_max_lateral_delta(smoothed_pts, raw_pts);
            if (has_reverse_segment(smoothed_pts)) {
                RCLCPP_WARN(this->get_logger(),
                    "[CoordTransform] arc smoothing rejected for route_id='%s': generated reverse segment, using raw RoutePlan waypoints",
                    msg->route_id.c_str());
            } else if (arc_smoothing_max_lateral_delta_m_ > 0.0 &&
                       std::isfinite(smoothing_lateral_delta) &&
                       smoothing_lateral_delta > arc_smoothing_max_lateral_delta_m_) {
                RCLCPP_WARN(this->get_logger(),
                    "[CoordTransform] arc smoothing rejected for route_id='%s': smoothed path deviates %.1fm > %.1fm from raw route, using raw RoutePlan waypoints",
                    msg->route_id.c_str(), smoothing_lateral_delta,
                    arc_smoothing_max_lateral_delta_m_);
            } else {
                ned_pts = std::move(smoothed_pts);
            }
        }
    }

    if (enable_fap_ && ned_pts.size() >= 2 && !has_emergency_avoidance) {
        ned_pts = insert_fap(ned_pts, fap_distance_m_, fap_speed_mps_);
    } else if (enable_fap_ && has_emergency_avoidance) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] FAP insertion bypassed for emergency_avoidance route_id='%s'",
            msg->route_id.c_str());
    }

    nav_msgs::msg::Path path_msg;
    path_msg.header = msg->header;
    path_msg.header.frame_id = "odom";

    for (auto& np : ned_pts) {
        geometry_msgs::msg::PoseStamped pose;
        pose.header = path_msg.header;
        pose.pose.position.x = np.x;
        pose.pose.position.y = np.y;
        pose.pose.position.z = static_cast<double>(np.navigation_mode_code);
        pose.pose.orientation.x = np.is_arc_point ? 1.0 : 0.0;
        pose.pose.orientation.y = np.speed_override_is_route_limit ? 1.0 : 0.0;
        pose.pose.orientation.z = (np.speed_override > 0) ? np.speed_override : 0.0;
        pose.pose.orientation.w = 1.0;
        path_msg.poses.push_back(pose);
    }

    double total_dist = 0.0;
    for (size_t i = 1; i < path_msg.poses.size(); ++i) {
        double dx = path_msg.poses[i].pose.position.x -
                    path_msg.poses[i - 1].pose.position.x;
        double dy = path_msg.poses[i].pose.position.y -
                    path_msg.poses[i - 1].pose.position.y;
        total_dist += std::hypot(dx, dy);
    }

    if (first_route) {
        origin_lat_ = lat0;
        origin_lon_ = lon0;
        origin_published_ = true;
        lat0_rad_ = origin_lat_ * M_PI / 180.0;
        double sin_lat0 = std::sin(lat0_rad_);
        N0_ = earth_a_ / std::sqrt(1.0 - earth_e2_ * sin_lat0 * sin_lat0);
        M0_ = earth_a_ * (1.0 - earth_e2_) /
              std::pow(1.0 - earth_e2_ * sin_lat0 * sin_lat0, 1.5);
        if (projection_mode_ == "planner_equirectangular" ||
            projection_mode_ == "equirectangular") {
            const double meters_per_radian = planner_meters_per_degree_ * 180.0 / M_PI;
            M0_ = meters_per_radian;
            N0_ = meters_per_radian;
        }
        origin_locked_ = true;

        geometry_msgs::msg::Point origin_msg;
        origin_msg.x = origin_lat_;
        origin_msg.y = origin_lon_;
        origin_msg.z = 0.0;
        origin_pub_->publish(origin_msg);
    }

    last_lats_ = lats;
    last_lons_ = lons;
    last_speed_limits_ = msg->speed_limit_mps;
    last_navigation_modes_ = msg->navigation_mode;
    last_route_id_ = msg->route_id;
    last_route_type_ = msg->route_type;
    last_local_path_ = ned_pts;
    last_feedback_path_ = raw_pts;
    has_last_route_ = true;
    last_accepted_route_time_ = this->now();
    feedback_dp_latched_ = false;

    path_pub_->publish(path_msg);

    double initial_yaw_rad = 0.0;
    double initial_yaw_segment_len_m = 0.0;
    if (compute_initial_route_yaw(path_msg, initial_yaw_rad, initial_yaw_segment_len_m)) {
        std_msgs::msg::Float64 yaw_msg;
        yaw_msg.data = initial_yaw_rad;
        initial_route_yaw_pub_->publish(yaw_msg);
        RCLCPP_INFO(this->get_logger(),
            "[InitialRouteYaw] published yaw=%.2fdeg segment_len=%.1fm topic=/sim/initial_route_yaw",
            initial_yaw_rad * 180.0 / M_PI, initial_yaw_segment_len_m);
    } else {
        RCLCPP_WARN(this->get_logger(),
            "[InitialRouteYaw] not published: no valid route segment");
    }

    const std::string status = (speed_limit_valid && navigation_mode_valid)
        ? "ACCEPTED"
        : "ACCEPTED_WITH_WARNINGS";
    const std::string reason = (speed_limit_valid && navigation_mode_valid)
        ? "route accepted and published"
        : "route accepted; invalid optional array was ignored";
    publish_route_status(*msg, true, status, reason,
        raw_pts.size(), path_msg.poses.size(), total_dist,
        speed_limit_valid, navigation_mode_valid,
        current_along_track, first_changed_distance_ahead, max_lateral_delta);

    RCLCPP_INFO(this->get_logger(),
        "[CoordTransform] accepted RoutePlan route_id='%s' raw=%zu internal=%zu total=%.1fm status=%s",
        msg->route_id.c_str(), raw_pts.size(), path_msg.poses.size(), total_dist, status.c_str());
}
double CoordinateTransformNode::compute_cross_track_error(
    double x, double y, int& segment_idx) const
{
    return compute_cross_track_error(x, y, last_feedback_path_, segment_idx);
}

double CoordinateTransformNode::compute_cross_track_error(
    double x, double y, const std::vector<NedPoint>& path, int& segment_idx) const
{
    segment_idx = -1;
    if (path.size() < 2) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    double best_abs_error = std::numeric_limits<double>::infinity();
    double best_signed_error = std::numeric_limits<double>::quiet_NaN();

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        const auto& a = path[i];
        const auto& b = path[i + 1];
        const double dx = b.x - a.x;
        const double dy = b.y - a.y;
        const double seg_len = std::hypot(dx, dy);
        if (seg_len < 1e-6) {
            continue;
        }

        const double ex = dx / seg_len;
        const double ey = dy / seg_len;
        const double vx = x - a.x;
        const double vy = y - a.y;
        const double along = std::clamp(vx * ex + vy * ey, 0.0, seg_len);
        const double proj_x = a.x + along * ex;
        const double proj_y = a.y + along * ey;
        const double dist = std::hypot(x - proj_x, y - proj_y);
        if (dist < best_abs_error) {
            best_abs_error = dist;
            best_signed_error = vy * ex - vx * ey;
            segment_idx = static_cast<int>(i);
        }
    }

    return best_signed_error;
}

std::vector<NedPoint> CoordinateTransformNode::insert_arc(
    const NedPoint& A, const NedPoint& B, const NedPoint& C,
    double R, double spacing)
{
    double dab_x = B.x - A.x, dab_y = B.y - A.y;
    double dbc_x = C.x - B.x, dbc_y = C.y - B.y;
    double len_ab = std::hypot(dab_x, dab_y);
    double len_bc = std::hypot(dbc_x, dbc_y);
    if (len_ab < 1e-3 || len_bc < 1e-3) return {B};

    double u1x = dab_x/len_ab, u1y = dab_y/len_ab;
    double u2x = dbc_x/len_bc, u2y = dbc_y/len_bc;
    double cos_theta = u1x*u2x + u1y*u2y;
    cos_theta = std::clamp(cos_theta, -1.0, 1.0);
    double theta = std::acos(cos_theta);
    if (theta < 1e-4) return {B};

    double d = R * std::tan(theta / 2.0);
    if (d > len_ab * 0.9 || d > len_bc * 0.9) {
        d = std::min(len_ab, len_bc) * 0.8;
        R = d / std::tan(theta / 2.0);
    }

    NedPoint T1 = {B.x - d*u1x, B.y - d*u1y, arc_speed_mps_, true};
    NedPoint T2 = {B.x + d*u2x, B.y + d*u2y, arc_speed_mps_, true};
    T1.navigation_mode_code = B.navigation_mode_code;
    T2.navigation_mode_code = B.navigation_mode_code;

    double cross = u1x*u2y - u1y*u2x;
    double perp_sign = (cross > 0) ? 1.0 : -1.0;
    double perp_x = -perp_sign * u1y;
    double perp_y =  perp_sign * u1x;
    double cx = T1.x + R * perp_x;
    double cy = T1.y + R * perp_y;

    double arc_len = R * theta;
    int N = std::max(2, (int)std::ceil(arc_len / spacing));
    std::vector<NedPoint> arc_pts;
    arc_pts.push_back(T1);

    double angle_start = std::atan2(T1.y - cy, T1.x - cx);
    double angle_end   = std::atan2(T2.y - cy, T2.x - cx);
    double angle_diff = std::atan2(std::sin(angle_end - angle_start), std::cos(angle_end - angle_start));
    if (perp_sign > 0 && angle_diff < 0) angle_diff += 2*M_PI;
    if (perp_sign < 0 && angle_diff > 0) angle_diff -= 2*M_PI;

    for (int i = 1; i < N; ++i) {
        double t = (double)i / N;
        double angle = angle_start + t * angle_diff;
        NedPoint p;
        p.x = cx + R * std::cos(angle);
        p.y = cy + R * std::sin(angle);
        p.speed_override = arc_speed_mps_;
        p.is_arc_point = true;
        p.navigation_mode_code = B.navigation_mode_code;
        arc_pts.push_back(p);
    }
    arc_pts.push_back(T2);

    // [Fix-A] 鍦═1涓婃父鐩寸嚎娈垫彃鍏ラ鍑忛€熺偣
    NedPoint pre_arc;
    double pre_arc_offset = d + pre_arc_decel_dist_m_;
    const double min_prev_gap = std::min(50.0, std::max(1.0, len_ab * 0.10));
    const double max_pre_arc_offset = std::max(d, len_ab - min_prev_gap);
    if (pre_arc_offset > max_pre_arc_offset) {
        RCLCPP_WARN(this->get_logger(),
            "[CoordTransform] pre-arc decel clamped: requested_offset=%.1fm max=%.1fm len_ab=%.1fm",
            pre_arc_offset, max_pre_arc_offset, len_ab);
        pre_arc_offset = max_pre_arc_offset;
    }
    pre_arc.x = B.x - pre_arc_offset * u1x;
    pre_arc.y = B.y - pre_arc_offset * u1y;
    pre_arc.speed_override = -1.0;
    pre_arc.speed_override_is_route_limit = false;
    pre_arc.is_arc_point = false;
    pre_arc.navigation_mode_code = B.navigation_mode_code;
    std::vector<NedPoint> result;
    result.push_back(pre_arc);
    result.push_back(T1);
    for (size_t i = 1; i < arc_pts.size(); ++i) {
        result.push_back(arc_pts[i]);
    }
    return result;
}

std::vector<NedPoint> CoordinateTransformNode::smooth_path(
    const std::vector<NedPoint>& raw)
{
    if (raw.size() < 3) return raw;
    std::vector<NedPoint> result;
    result.push_back(raw[0]);
    auto append_arc_point_safely = [&](const NedPoint& pt) {
        if (result.empty()) {
            result.push_back(pt);
            return;
        }
        const double last_dist = std::hypot(pt.x - result.back().x, pt.y - result.back().y);
        if (last_dist <= 1.0) {
            result.back() = pt;
            return;
        }
        if (result.size() >= 2) {
            const auto& prev = result[result.size() - 2];
            const auto& last = result.back();
            const double ax = last.x - prev.x;
            const double ay = last.y - prev.y;
            const double bx = pt.x - last.x;
            const double by = pt.y - last.y;
            const double al = std::hypot(ax, ay);
            const double bl = std::hypot(bx, by);
            if (al > 1e-6 && bl > 1e-6) {
                const double cos_turn = (ax * bx + ay * by) / (al * bl);
                if (cos_turn < -0.5) {
                    RCLCPP_WARN(this->get_logger(),
                        "[CoordTransform] skipped arc point causing reverse kink: last=(%.1f,%.1f) candidate=(%.1f,%.1f) turn_cos=%.3f",
                        last.x, last.y, pt.x, pt.y, cos_turn);
                    return;
                }
            }
        }
        result.push_back(pt);
    };

    for (size_t i = 1; i + 1 < raw.size(); ++i) {
        double dab_x = raw[i].x - raw[i-1].x, dab_y = raw[i].y - raw[i-1].y;
        double dbc_x = raw[i+1].x - raw[i].x, dbc_y = raw[i+1].y - raw[i].y;
        double len_ab = std::hypot(dab_x, dab_y);
        double len_bc = std::hypot(dbc_x, dbc_y);
        if (len_ab < 1e-3 || len_bc < 1e-3) { result.push_back(raw[i]); continue; }
        double cos_t = (dab_x*dbc_x + dab_y*dbc_y) / (len_ab * len_bc);
        cos_t = std::clamp(cos_t, -1.0, 1.0);
        double turn_deg = std::acos(cos_t) * 180.0 / M_PI;
        if (turn_deg < min_turn_angle_deg_) {
            result.push_back(raw[i]);
        } else {
            double radius = arc_radius_m_;
            if (turn_deg <= shallow_arc_angle_deg_) {
                radius = shallow_arc_radius_m_;
            } else if (turn_deg < 45.0 && shallow_arc_radius_m_ > arc_radius_m_) {
                const double denom = std::max(1.0, 45.0 - shallow_arc_angle_deg_);
                const double blend = std::clamp((turn_deg - shallow_arc_angle_deg_) / denom, 0.0, 1.0);
                radius = shallow_arc_radius_m_ + blend * (arc_radius_m_ - shallow_arc_radius_m_);
            }
            auto arc = insert_arc(raw[i-1], raw[i], raw[i+1], radius, arc_sample_spacing_m_);
            for (auto& pt : arc) append_arc_point_safely(pt);
        }
    }
    result.push_back(raw.back());
    return result;
}

std::vector<NedPoint> CoordinateTransformNode::insert_fap(
    const std::vector<NedPoint>& pts, double fap_dist, double fap_speed)
{
    if (pts.size() < 2) return pts;
    std::vector<NedPoint> result = pts;
    auto& last = pts.back();
    auto& prelast = pts[pts.size()-2];
    double dx = last.x - prelast.x, dy = last.y - prelast.y;
    double len = std::hypot(dx, dy);
    if (len < 1e-3) return result;
    NedPoint fap;
    fap.x = last.x - (dx/len) * fap_dist;
    fap.y = last.y - (dy/len) * fap_dist;
    fap.speed_override = fap_speed;
    fap.is_arc_point = false;
    fap.navigation_mode_code = last.navigation_mode_code;
    result.insert(result.end()-1, fap);
    return result;
}

void CoordinateTransformNode::init_feedback_log()
{
    if (!feedback_log_enabled_) {
        RCLCPP_INFO(this->get_logger(), "[FeedbackLog] disabled");
        return;
    }

    std::filesystem::path log_dir = feedback_log_dir_.empty()
        ? std::filesystem::path("/tmp/ship_feedback_logs")
        : std::filesystem::path(feedback_log_dir_);
    std::error_code ec;
    std::filesystem::create_directories(log_dir, ec);
    if (ec) {
        RCLCPP_ERROR(this->get_logger(),
            "[FeedbackLog] failed to create dir %s: %s",
            log_dir.string().c_str(), ec.message().c_str());
        feedback_log_enabled_ = false;
        return;
    }

    feedback_log_path_ = (log_dir / ("ship_feedback_" + file_time_string() + ".csv")).string();
    feedback_log_file_.open(feedback_log_path_, std::ios::out | std::ios::trunc);
    if (!feedback_log_file_.is_open()) {
        RCLCPP_ERROR(this->get_logger(),
            "[FeedbackLog] failed to open %s", feedback_log_path_.c_str());
        feedback_log_enabled_ = false;
        return;
    }

    feedback_log_file_
        << "wall_time,ros_time_sec,latitude_deg,longitude_deg,heading_deg,course_deg,"
        << "roll_deg,roll_rate_deg_s,roll_limit_exceeded,"
        << "speed_mps,surge_mps,sway_mps,vel_north_mps,vel_east_mps,yaw_rate_deg_s,"
        << "x_ned_m,y_ned_m,cross_track_error_m,cross_track_error_abs_m,"
        << "cross_track_segment_idx,nav_state,nav_state_str,origin_locked,"
        << "origin_lat_deg,origin_lon_deg,route_id,route_type,"
        << "raw_route_xte_m,raw_route_xte_abs_m,raw_route_segment_idx,"
        << "smoothed_route_xte_m,smoothed_route_xte_abs_m,smoothed_route_segment_idx,"
        << "guidance_xte_m,guidance_xte_abs_m,guidance_xte_valid\n";
    feedback_log_file_.flush();

    RCLCPP_INFO(this->get_logger(),
        "[FeedbackLog] writing %s period=%.2fs",
        feedback_log_path_.c_str(), feedback_log_period_s_);
}

void CoordinateTransformNode::write_feedback_log(const ship_interfaces::msg::GeoPosition& msg)
{
    if (!feedback_log_enabled_ || !feedback_log_file_.is_open()) {
        return;
    }

    const rclcpp::Time stamp(msg.header.stamp);
    if (feedback_log_period_s_ > 0.0 && last_feedback_log_time_.nanoseconds() > 0) {
        const double dt = (stamp - last_feedback_log_time_).seconds();
        if (dt >= 0.0 && dt < feedback_log_period_s_) {
            return;
        }
    }
    last_feedback_log_time_ = stamp;

    feedback_log_file_ << std::fixed
        << csv_escape(wall_time_string()) << ','
        << std::setprecision(3) << stamp.seconds() << ','
        << std::setprecision(8) << msg.latitude << ','
        << std::setprecision(8) << msg.longitude << ','
        << std::setprecision(2) << msg.heading_deg << ','
        << std::setprecision(2) << msg.course_deg << ','
        << std::setprecision(2) << msg.roll_deg << ','
        << std::setprecision(3) << msg.roll_rate_deg_s << ','
        << (msg.roll_limit_exceeded ? 1 : 0) << ','
        << std::setprecision(3) << msg.speed_mps << ','
        << msg.surge_mps << ','
        << msg.sway_mps << ','
        << msg.vel_north_mps << ','
        << msg.vel_east_mps << ','
        << std::setprecision(3) << msg.yaw_rate_deg_s << ','
        << std::setprecision(3) << msg.x_ned << ','
        << msg.y_ned << ','
        << msg.cross_track_error_m << ','
        << msg.cross_track_error_abs_m << ','
        << msg.cross_track_segment_idx << ','
        << msg.nav_state << ','
        << csv_escape(msg.nav_state_str) << ','
        << (msg.origin_locked ? 1 : 0) << ','
        << std::setprecision(8) << msg.origin_lat << ','
        << msg.origin_lon << ','
        << csv_escape(last_route_id_) << ','
        << csv_escape(last_route_type_) << ','
        << std::setprecision(3) << msg.cross_track_error_m << ','
        << msg.cross_track_error_abs_m << ','
        << msg.cross_track_segment_idx << ','
        << smoothed_cross_track_error_m_ << ','
        << smoothed_cross_track_error_abs_m_ << ','
        << smoothed_cross_track_segment_idx_ << ','
        << guidance_cross_track_error_m_ << ','
        << guidance_cross_track_error_abs_m_ << ','
        << (guidance_cross_track_valid_ ? 1 : 0) << '\n';
    feedback_log_file_.flush();
}

void CoordinateTransformNode::target_pose_callback(
    const geometry_msgs::msg::PoseStamped::SharedPtr msg)
{
    last_target_pose_x_ = msg->pose.position.x;
    last_target_pose_y_ = msg->pose.position.y;
    has_target_pose_ = true;

    const double guidance_xte = msg->pose.position.z;
    if (std::isfinite(guidance_xte)) {
        guidance_cross_track_error_m_ = guidance_xte;
        guidance_cross_track_error_abs_m_ = std::abs(guidance_xte);
        guidance_cross_track_valid_ = true;
    } else {
        guidance_cross_track_error_m_ = std::numeric_limits<double>::quiet_NaN();
        guidance_cross_track_error_abs_m_ = std::numeric_limits<double>::quiet_NaN();
        guidance_cross_track_valid_ = false;
    }
}

void CoordinateTransformNode::speed_setpoint_callback(
    const std_msgs::msg::Float64::SharedPtr msg)
{
    last_speed_setpoint_mps_ = msg->data;
    has_speed_setpoint_ = true;
}

void CoordinateTransformNode::odom_callback(
    const nav_msgs::msg::Odometry::SharedPtr msg)
{
    current_x_ = msg->pose.pose.position.x;
    current_y_ = msg->pose.pose.position.y;
    has_odom_ = true;

    ship_interfaces::msg::GeoPosition out;
    out.header.stamp = msg->header.stamp;
    out.header.frame_id = "map";

    if (!origin_locked_) {
        out.origin_locked = false;
        geo_pos_pub_->publish(out);
        return;
    }

    double x_ned = msg->pose.pose.position.x;
    double y_ned = msg->pose.pose.position.y;

    double dlat_rad = x_ned / M0_;
    double dlon_rad = y_ned / (N0_ * std::cos(lat0_rad_));
    out.latitude  = origin_lat_ + dlat_rad * (180.0 / M_PI);
    out.longitude = origin_lon_ + dlon_rad * (180.0 / M_PI);

    double qx = msg->pose.pose.orientation.x;
    double qy = msg->pose.pose.orientation.y;
    double qz = msg->pose.pose.orientation.z;
    double qw = msg->pose.pose.orientation.w;
    double roll_rad = std::atan2(2.0 * (qw * qx + qy * qz),
                                 1.0 - 2.0 * (qx * qx + qy * qy));
    double yaw_rad = std::atan2(2.0 * (qw * qz + qx * qy),
                                1.0 - 2.0 * (qy * qy + qz * qz));

    double heading_deg = std::fmod(yaw_rad * 180.0 / M_PI + 360.0, 360.0);
    out.heading_deg = heading_deg;
    out.roll_deg = roll_rad * 180.0 / M_PI;

    out.surge_mps     = msg->twist.twist.linear.x;
    out.sway_mps      = msg->twist.twist.linear.y;
    out.roll_rate_rads = msg->twist.twist.angular.x;
    out.roll_rate_deg_s = out.roll_rate_rads * 180.0 / M_PI;
    out.roll_limit_exceeded =
        roll_limit_deg_ > 0.0 && std::abs(out.roll_deg) > roll_limit_deg_;
    out.yaw_rate_rads = msg->twist.twist.angular.z;
    out.yaw_rate_deg_s = out.yaw_rate_rads * 180.0 / M_PI;
    out.speed_mps     = std::hypot(out.surge_mps, out.sway_mps);

    double u = out.surge_mps;
    double v = out.sway_mps;
    double cos_yaw = std::cos(yaw_rad);
    double sin_yaw = std::sin(yaw_rad);
    out.vel_north_mps = u * cos_yaw - v * sin_yaw;
    out.vel_east_mps  = u * sin_yaw + v * cos_yaw;

    double cog_rad = std::atan2(out.vel_east_mps, out.vel_north_mps);
    out.course_deg = std::fmod(cog_rad * 180.0 / M_PI + 360.0, 360.0);

    out.x_ned = msg->pose.pose.position.x;
    out.y_ned = msg->pose.pose.position.y;
    int cross_track_segment_idx = -1;
    out.cross_track_error_m = compute_cross_track_error(
        out.x_ned, out.y_ned, cross_track_segment_idx);
    out.cross_track_error_abs_m = std::isfinite(out.cross_track_error_m)
        ? std::abs(out.cross_track_error_m)
        : std::numeric_limits<double>::quiet_NaN();
    out.cross_track_segment_idx = cross_track_segment_idx;

    smoothed_cross_track_segment_idx_ = -1;
    smoothed_cross_track_error_m_ = compute_cross_track_error(
        out.x_ned, out.y_ned, last_local_path_, smoothed_cross_track_segment_idx_);
    smoothed_cross_track_error_abs_m_ = std::isfinite(smoothed_cross_track_error_m_)
        ? std::abs(smoothed_cross_track_error_m_)
        : std::numeric_limits<double>::quiet_NaN();
    if (xte_reference_mismatch_warn_m_ > 0.0 &&
        std::isfinite(out.cross_track_error_m) &&
        std::isfinite(smoothed_cross_track_error_m_)) {
        const double xte_reference_delta =
            std::abs(out.cross_track_error_m - smoothed_cross_track_error_m_);
        if (xte_reference_delta > xte_reference_mismatch_warn_m_) {
            RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 3000,
                "[XTE Reference Mismatch] raw_xte=%.1fm raw_seg=%d smoothed_xte=%.1fm smoothed_seg=%d delta=%.1fm > %.1fm",
                out.cross_track_error_m, out.cross_track_segment_idx,
                smoothed_cross_track_error_m_, smoothed_cross_track_segment_idx_,
                xte_reference_delta, xte_reference_mismatch_warn_m_);
        }
    }

    out.nav_state = 0;
    out.nav_state_str = "";
    if (!last_local_path_.empty()) {
        const NedPoint& final_wp = last_local_path_.back();
        const bool final_requires_dp = final_wp.navigation_mode_code == 5;
        const bool target_is_final =
            has_target_pose_ &&
            std::isfinite(last_target_pose_x_) &&
            std::isfinite(last_target_pose_y_) &&
            std::hypot(last_target_pose_x_ - final_wp.x,
                       last_target_pose_y_ - final_wp.y) <= 5.0;
        const bool target_is_current_hold =
            has_target_pose_ &&
            std::isfinite(last_target_pose_x_) &&
            std::isfinite(last_target_pose_y_) &&
            std::hypot(last_target_pose_x_ - out.x_ned,
                       last_target_pose_y_ - out.y_ned) <= 15.0;
        const bool zero_speed_setpoint =
            has_speed_setpoint_ &&
            std::isfinite(last_speed_setpoint_mps_) &&
            std::abs(last_speed_setpoint_mps_) <= 0.05;
        const bool dp_hold_observed =
            final_requires_dp &&
            zero_speed_setpoint &&
            (target_is_final || target_is_current_hold);
        if (dp_hold_observed) {
            feedback_dp_latched_ = true;
        }

        if (final_requires_dp && zero_speed_setpoint && feedback_dp_latched_) {
            out.nav_state = 3;
            out.nav_state_str = "DP_HOLD";
        } else {
            out.nav_state = 1;
            out.nav_state_str = "ILOS";
        }
    }

    out.origin_locked = true;
    out.origin_lat    = origin_lat_;
    out.origin_lon    = origin_lon_;

    write_feedback_log(out);
    geo_pos_pub_->publish(out);
}

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<CoordinateTransformNode>());
    rclcpp::shutdown();
    return 0;
}
