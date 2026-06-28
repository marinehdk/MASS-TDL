#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "rclcpp/rclcpp.hpp"

#include "ship_interfaces/msg/avoidance_plan.hpp"
#include "ship_interfaces/msg/geo_position.hpp"
#include "ship_interfaces/msg/gnc_execution_odd.hpp"
#include "ship_interfaces/msg/route_execution_status.hpp"
#include "ship_interfaces/msg/route_plan.hpp"
#include "ship_guidance/navigation_mode_policy.hpp"

namespace {

constexpr double kDegToRad = M_PI / 180.0;
constexpr double kRadToDeg = 180.0 / M_PI;

std::string normalize_mode(std::string mode)
{
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char ch) {
        if (ch == '-' || ch == ' ') {
            return '_';
        }
        return static_cast<char>(std::tolower(ch));
    });
    return mode;
}

bool time_is_zero(const builtin_interfaces::msg::Time& stamp)
{
    return stamp.sec == 0 && stamp.nanosec == 0;
}

double clamp_angle_deg(double angle)
{
    while (angle >= 360.0) {
        angle -= 360.0;
    }
    while (angle < 0.0) {
        angle += 360.0;
    }
    return angle;
}

struct LocalPoint {
    double x{0.0};  // north, m
    double y{0.0};  // east, m
};

struct FeasibilityResult {
    bool accepted{false};
    bool executing{false};
    bool degraded{false};
    bool rejected{true};
    std::string state{"REJECTED"};
    std::string reason{"not_checked"};
    std::string suggested_action{"check_plan"};
    double requested_speed_mps{0.0};
    double applied_speed_mps{0.0};
    double requested_heading_deg{std::numeric_limits<double>::quiet_NaN()};
    double applied_heading_deg{std::numeric_limits<double>::quiet_NaN()};
    double required_turn_radius_m{0.0};
    double estimated_available_turn_radius_m{std::numeric_limits<double>::infinity()};
    double required_decel_distance_m{0.0};
    double available_decel_distance_m{std::numeric_limits<double>::infinity()};
    double suggested_max_speed_mps{0.0};
    double suggested_min_distance_m{0.0};
};

}  // namespace

class ActiveRouteManagerNode : public rclcpp::Node
{
public:
    ActiveRouteManagerNode()
        : Node("active_route_manager_node")
    {
        declare_parameter("nominal_route_topic", "/route_planning/route_plan");
        declare_parameter("avoidance_plan_topic", "/colav/avoidance_plan");
        declare_parameter("active_route_topic", "/gnc/active_route");
        declare_parameter("execution_status_topic", "/gnc/route_execution_status");
        declare_parameter("ship_state_topic", "/ship/geo_position");
        declare_parameter("max_command_speed_mps", 8.0);
        declare_parameter("min_segment_length_m", 30.0);
        declare_parameter("emergency_min_segment_length_m", 15.0);
        declare_parameter("min_turn_radius_m", 80.0);
        declare_parameter("emergency_min_turn_radius_m", 45.0);
        declare_parameter("max_lateral_accel_mps2", 0.25);
        declare_parameter("max_yaw_rate_deg_s", 1.2);
        declare_parameter("emergency_max_yaw_rate_deg_s", 2.0);
        declare_parameter("max_decel_mps2", 0.08);
        // W2: speed-envelope parameters duplicated from ship_guidance_node so the
        // whole GNC execution ODD is published from this single node (decision #3,
        // option a). Both nodes read the same overlay param block, staying in sync.
        declare_parameter("emergency_avoidance_speed_cap_mps", 3.2);
        declare_parameter("cruise_min_speed_mps", 3.8);
        declare_parameter("max_transit_speed_mps", 3.0);
        declare_parameter("default_avoidance_hold_s", 60.0);
        declare_parameter("publish_nominal_status", false);

        nominal_route_topic_ = get_parameter("nominal_route_topic").as_string();
        avoidance_plan_topic_ = get_parameter("avoidance_plan_topic").as_string();
        active_route_topic_ = get_parameter("active_route_topic").as_string();
        execution_status_topic_ = get_parameter("execution_status_topic").as_string();
        ship_state_topic_ = get_parameter("ship_state_topic").as_string();
        max_command_speed_mps_ = std::max(0.1, get_parameter("max_command_speed_mps").as_double());
        min_segment_length_m_ = std::max(1.0, get_parameter("min_segment_length_m").as_double());
        emergency_min_segment_length_m_ = std::max(1.0, get_parameter("emergency_min_segment_length_m").as_double());
        min_turn_radius_m_ = std::max(1.0, get_parameter("min_turn_radius_m").as_double());
        emergency_min_turn_radius_m_ = std::max(1.0, get_parameter("emergency_min_turn_radius_m").as_double());
        max_lateral_accel_mps2_ = std::max(0.01, get_parameter("max_lateral_accel_mps2").as_double());
        max_yaw_rate_deg_s_ = std::max(0.1, get_parameter("max_yaw_rate_deg_s").as_double());
        emergency_max_yaw_rate_deg_s_ = std::max(
            max_yaw_rate_deg_s_, get_parameter("emergency_max_yaw_rate_deg_s").as_double());
        max_decel_mps2_ = std::max(0.01, get_parameter("max_decel_mps2").as_double());
        emergency_avoidance_speed_cap_mps_ = std::clamp(
            get_parameter("emergency_avoidance_speed_cap_mps").as_double(),
            0.5, std::max(max_command_speed_mps_, 0.5));
        cruise_min_speed_mps_ = std::max(0.0, get_parameter("cruise_min_speed_mps").as_double());
        max_transit_speed_mps_ = std::max(0.0, get_parameter("max_transit_speed_mps").as_double());
        default_avoidance_hold_s_ = std::max(1.0, get_parameter("default_avoidance_hold_s").as_double());
        publish_nominal_status_ = get_parameter("publish_nominal_status").as_bool();

        auto route_qos = rclcpp::QoS(10).transient_local().reliable();
        active_route_pub_ = create_publisher<ship_interfaces::msg::RoutePlan>(
            active_route_topic_, route_qos);
        status_pub_ = create_publisher<ship_interfaces::msg::RouteExecutionStatus>(
            execution_status_topic_, 10);
        // W2: latched execution-ODD contract for TDL (single publisher, all ODD
        // params, transient_local so late-joining subscribers get the last value).
        execution_odd_pub_ = create_publisher<ship_interfaces::msg::GncExecutionOdd>(
            "/gnc/execution_odd", rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());
        publish_execution_odd();  // initial latched publish after params loaded

        nominal_route_sub_ = create_subscription<ship_interfaces::msg::RoutePlan>(
            nominal_route_topic_, 10,
            std::bind(&ActiveRouteManagerNode::nominal_route_callback, this, std::placeholders::_1));
        avoidance_plan_sub_ = create_subscription<ship_interfaces::msg::AvoidancePlan>(
            avoidance_plan_topic_, 10,
            std::bind(&ActiveRouteManagerNode::avoidance_plan_callback, this, std::placeholders::_1));
        ship_state_sub_ = create_subscription<ship_interfaces::msg::GeoPosition>(
            ship_state_topic_, 10,
            std::bind(&ActiveRouteManagerNode::ship_state_callback, this, std::placeholders::_1));
        deferred_nominal_timer_ = create_wall_timer(
            std::chrono::milliseconds(500),
            std::bind(&ActiveRouteManagerNode::retry_deferred_nominal_route, this));

        RCLCPP_INFO(
            get_logger(),
            "[ActiveRouteManager] nominal=%s avoidance=%s active=%s status=%s",
            nominal_route_topic_.c_str(), avoidance_plan_topic_.c_str(),
            active_route_topic_.c_str(), execution_status_topic_.c_str());
    }

private:
    // W2: publish the latched execution-ODD contract so TDL (M5) can consume the
    // actual GNC execution limits instead of hardcoding them. Called once after
    // params load; the transient_local QoS keeps it available to late joiners.
    void publish_execution_odd()
    {
        ship_interfaces::msg::GncExecutionOdd odd;
        odd.header.stamp = this->now();
        odd.emergency_avoidance_speed_cap_mps = emergency_avoidance_speed_cap_mps_;
        odd.cruise_min_speed_mps = cruise_min_speed_mps_;
        odd.max_transit_speed_mps = max_transit_speed_mps_;
        odd.max_lateral_accel_mps2 = max_lateral_accel_mps2_;
        odd.max_decel_mps2 = max_decel_mps2_;
        odd.emergency_min_turn_radius_m = emergency_min_turn_radius_m_;
        odd.emergency_max_yaw_rate_deg_s = emergency_max_yaw_rate_deg_s_;
        odd.schema_version = "1.0";
        execution_odd_pub_->publish(odd);
    }
    void ship_state_callback(const ship_interfaces::msg::GeoPosition::SharedPtr msg)
    {
        latest_ship_state_ = *msg;
        has_ship_state_ = true;
    }

    void nominal_route_callback(const ship_interfaces::msg::RoutePlan::SharedPtr msg)
    {
        if (!basic_route_valid(*msg)) {
            publish_status_for_route(*msg, rejected_result("invalid_nominal_route", "fix_route_plan"));
            return;
        }

        latest_nominal_route_ = *msg;
        if (avoidance_is_active()) {
            deferred_nominal_pending_ = true;
            auto result = accepted_result();
            result.executing = false;
            result.state = "DEFERRED";
            result.reason = "avoidance_active";
            result.suggested_action = "wait_until_avoidance_complete";
            publish_status_for_route(*msg, result);
            RCLCPP_INFO_THROTTLE(
                get_logger(), *get_clock(), 3000,
                "[ActiveRouteManager] deferred nominal route_id='%s' while avoidance plan_id='%s' is active",
                msg->route_id.c_str(), active_avoidance_plan_id_.c_str());
            return;
        }

        active_route_pub_->publish(*msg);
        deferred_nominal_pending_ = false;

        if (publish_nominal_status_) {
            auto result = accepted_result();
            result.reason = "nominal_route_forwarded";
            result.suggested_action = "none";
            publish_status_for_route(*msg, result);
        }

        RCLCPP_INFO(
            get_logger(), "[ActiveRouteManager] forwarded nominal route_id='%s' points=%zu",
            msg->route_id.c_str(), msg->latitude.size());
    }

    void avoidance_plan_callback(const ship_interfaces::msg::AvoidancePlan::SharedPtr msg)
    {
        const auto now = get_clock()->now();
        if (!time_is_zero(msg->valid_until) && rclcpp::Time(msg->valid_until) <= now) {
            auto result = rejected_result("plan_expired", "send_fresh_plan");
            publish_status_for_avoidance(*msg, result);
            RCLCPP_WARN(get_logger(), "[ActiveRouteManager] rejected expired avoidance plan_id='%s'",
                msg->plan_id.c_str());
            return;
        }

        auto route = to_route_plan(*msg);
        auto result = evaluate_avoidance_plan(*msg, route);
        if (!result.accepted) {
            publish_status_for_avoidance(*msg, result);
            RCLCPP_WARN(
                get_logger(),
                "[ActiveRouteManager] rejected avoidance plan_id='%s' reason=%s",
                msg->plan_id.c_str(), result.reason.c_str());
            return;
        }

        apply_speed_degradation(route, result.suggested_max_speed_mps);
        active_route_pub_->publish(route);
        mark_avoidance_active(*msg);
        publish_status_for_avoidance(*msg, result);

        RCLCPP_INFO(
            get_logger(),
            "[ActiveRouteManager] accepted avoidance plan_id='%s' points=%zu state=%s reason=%s speed_cap=%.2f",
            msg->plan_id.c_str(), route.latitude.size(), result.state.c_str(),
            result.reason.c_str(), result.suggested_max_speed_mps);
    }

    bool basic_route_valid(const ship_interfaces::msg::RoutePlan& route) const
    {
        return route.latitude.size() >= 2 &&
            route.latitude.size() == route.longitude.size() &&
            (route.speed_limit_mps.empty() || route.speed_limit_mps.size() == route.latitude.size()) &&
            (route.navigation_mode.empty() || route.navigation_mode.size() == route.latitude.size());
    }

    bool avoidance_is_active()
    {
        if (!has_active_avoidance_) {
            return false;
        }
        if (now() <= active_avoidance_until_) {
            return true;
        }
        has_active_avoidance_ = false;
        active_avoidance_plan_id_.clear();
        return false;
    }

    void mark_avoidance_active(const ship_interfaces::msg::AvoidancePlan& plan)
    {
        has_active_avoidance_ = true;
        active_avoidance_plan_id_ = plan.plan_id;
        deferred_nominal_pending_ =
            deferred_nominal_pending_ || basic_route_valid(latest_nominal_route_);
        if (!time_is_zero(plan.valid_until)) {
            active_avoidance_until_ = rclcpp::Time(plan.valid_until);
        } else {
            active_avoidance_until_ = now() + rclcpp::Duration::from_seconds(default_avoidance_hold_s_);
        }
    }

    void retry_deferred_nominal_route()
    {
        if (!deferred_nominal_pending_) {
            return;
        }
        if (avoidance_is_active()) {
            return;
        }
        if (!basic_route_valid(latest_nominal_route_)) {
            deferred_nominal_pending_ = false;
            return;
        }

        active_route_pub_->publish(latest_nominal_route_);
        auto result = accepted_result();
        result.reason = "nominal_route_forwarded_after_avoidance";
        publish_status_for_route(latest_nominal_route_, result);
        deferred_nominal_pending_ = false;
        RCLCPP_INFO(
            get_logger(),
            "[ActiveRouteManager] forwarded deferred nominal route_id='%s' after avoidance expiry",
            latest_nominal_route_.route_id.c_str());
    }

    ship_interfaces::msg::RoutePlan to_route_plan(
        const ship_interfaces::msg::AvoidancePlan& plan) const
    {
        ship_interfaces::msg::RoutePlan route;
        route.header = plan.header;
        route.route_id = plan.plan_id.empty() ? "avoidance_plan" : plan.plan_id;
        route.route_type = plan.behavior_mode.empty() ? "avoidance" : normalize_mode(plan.behavior_mode);
        route.latitude = plan.latitude;
        route.longitude = plan.longitude;
        route.speed_limit_mps = plan.command_speed_mps;
        route.navigation_mode = plan.navigation_mode;

        if (route.navigation_mode.empty() && !route.latitude.empty()) {
            const std::string mode = route.route_type.empty() ? "avoidance" : route.route_type;
            route.navigation_mode.assign(route.latitude.size(), mode);
        }
        return route;
    }

    FeasibilityResult evaluate_avoidance_plan(
        const ship_interfaces::msg::AvoidancePlan& plan,
        const ship_interfaces::msg::RoutePlan& route) const
    {
        if (!basic_route_valid(route)) {
            return rejected_result("invalid_avoidance_route", "fix_route_plan");
        }
        if (!plan.command_heading_deg.empty() &&
            plan.command_heading_deg.size() != route.latitude.size()) {
            return rejected_result("heading_length_mismatch", "fix_heading_array");
        }
        if (!plan.command_speed_mps.empty() &&
            plan.command_speed_mps.size() != route.latitude.size()) {
            return rejected_result("speed_length_mismatch", "fix_speed_array");
        }

        const bool emergency = is_colregs_protected_mode(plan.behavior_mode) ||
            std::any_of(
                route.navigation_mode.begin(), route.navigation_mode.end(),
                is_colregs_protected_mode);
        const double min_segment = emergency ? emergency_min_segment_length_m_ : min_segment_length_m_;
        const double static_min_turn_radius =
            emergency ? emergency_min_turn_radius_m_ : min_turn_radius_m_;
        const double yaw_rate_limit_rad_s =
            (emergency ? emergency_max_yaw_rate_deg_s_ : max_yaw_rate_deg_s_) * kDegToRad;

        auto points = route_to_local_points(route);
        if (points.size() < 2) {
            return rejected_result("projection_failed", "check_coordinates");
        }

        FeasibilityResult result = accepted_result();
        result.reason = "feasible";
        result.suggested_action = "none";
        result.requested_speed_mps = max_requested_speed(route);
        result.applied_speed_mps = std::min(result.requested_speed_mps, max_command_speed_mps_);
        result.suggested_max_speed_mps = result.applied_speed_mps;
        if (!plan.command_heading_deg.empty()) {
            result.requested_heading_deg = clamp_angle_deg(plan.command_heading_deg.front());
            result.applied_heading_deg = result.requested_heading_deg;
        }

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const double seg_len = distance(points[i], points[i + 1]);
            if (seg_len < min_segment) {
                auto rejected = rejected_result("segment_too_short", "increase_segment_length");
                rejected.available_decel_distance_m = seg_len;
                rejected.suggested_min_distance_m = min_segment;
                return rejected;
            }
        }

        for (size_t i = 1; i + 1 < points.size(); ++i) {
            const double available_radius = available_turn_radius(points[i - 1], points[i], points[i + 1]);
            const double speed = requested_speed_at(route, i);
            const double dynamic_required_radius = speed * speed / max_lateral_accel_mps2_;
            const double yaw_required_radius =
                yaw_rate_limit_rad_s > 1e-6 ? speed / yaw_rate_limit_rad_s : std::numeric_limits<double>::infinity();
            const double required_radius =
                std::max({static_min_turn_radius, dynamic_required_radius, yaw_required_radius});

            result.estimated_available_turn_radius_m =
                std::min(result.estimated_available_turn_radius_m, available_radius);
            result.required_turn_radius_m =
                std::max(result.required_turn_radius_m, required_radius);

            if (available_radius + 1e-6 < required_radius) {
                const double safe_speed_by_lateral_accel =
                    std::sqrt(std::max(0.0, available_radius * max_lateral_accel_mps2_));
                const double safe_speed_by_yaw_rate =
                    std::max(0.0, available_radius * yaw_rate_limit_rad_s);
                const double safe_speed =
                    std::min(safe_speed_by_lateral_accel, safe_speed_by_yaw_rate);
                const bool yaw_rate_is_dominant =
                    yaw_required_radius >= dynamic_required_radius &&
                    yaw_required_radius >= static_min_turn_radius;
                result.suggested_max_speed_mps =
                    std::min(result.suggested_max_speed_mps, safe_speed);
                if (plan.require_exact_speed || !plan.allow_degraded_execution) {
                    auto rejected = rejected_result(
                        yaw_rate_is_dominant ? "yaw_rate_too_high" : "turn_radius_too_small",
                        yaw_rate_is_dominant ? "slow_down_or_smooth_turn" : "slow_down_or_enlarge_turn_radius");
                    rejected.requested_speed_mps = speed;
                    rejected.suggested_max_speed_mps = safe_speed;
                    rejected.required_turn_radius_m = required_radius;
                    rejected.estimated_available_turn_radius_m = available_radius;
                    return rejected;
                }
                result.degraded = true;
                result.state = "EXECUTING_WITH_LIMIT";
                result.reason = yaw_rate_is_dominant ? "yaw_rate_limited" : "turn_speed_limited";
                result.suggested_action = yaw_rate_is_dominant ? "slow_down_or_smooth_turn" : "slow_down";
            }
        }

        for (size_t i = 0; i + 1 < points.size(); ++i) {
            const double v0 = requested_speed_at(route, i);
            const double v1 = requested_speed_at(route, i + 1);
            if (v0 <= v1) {
                continue;
            }
            const double required_decel = (v0 * v0 - v1 * v1) / (2.0 * max_decel_mps2_);
            const double available = distance(points[i], points[i + 1]);
            result.required_decel_distance_m =
                std::max(result.required_decel_distance_m, required_decel);
            result.available_decel_distance_m =
                std::min(result.available_decel_distance_m, available);
            if (available + 1e-6 < required_decel) {
                if (plan.require_exact_speed || !plan.allow_degraded_execution) {
                    auto rejected = rejected_result("decel_distance_not_enough", "send_points_earlier");
                    rejected.required_decel_distance_m = required_decel;
                    rejected.available_decel_distance_m = available;
                    rejected.suggested_min_distance_m = required_decel;
                    return rejected;
                }
                result.degraded = true;
                result.state = "EXECUTING_WITH_LIMIT";
                result.reason = "decel_distance_tight";
                result.suggested_action = "send_points_earlier";
            }
        }

        result.rejected = false;
        result.accepted = true;
        result.executing = true;
        if (!result.degraded) {
            result.state = "ACCEPTED";
        }
        result.applied_speed_mps = std::min(result.requested_speed_mps, result.suggested_max_speed_mps);
        return result;
    }

    std::vector<LocalPoint> route_to_local_points(const ship_interfaces::msg::RoutePlan& route) const
    {
        std::vector<LocalPoint> points;
        if (route.latitude.empty() || route.latitude.size() != route.longitude.size()) {
            return points;
        }
        const double lat0 = route.latitude.front();
        const double lon0 = route.longitude.front();
        const double meters_per_deg_lat = 111320.0;
        const double meters_per_deg_lon = 111320.0 * std::cos(lat0 * kDegToRad);
        points.reserve(route.latitude.size());
        for (size_t i = 0; i < route.latitude.size(); ++i) {
            if (!std::isfinite(route.latitude[i]) || !std::isfinite(route.longitude[i])) {
                points.clear();
                return points;
            }
            points.push_back(LocalPoint{
                (route.latitude[i] - lat0) * meters_per_deg_lat,
                (route.longitude[i] - lon0) * meters_per_deg_lon});
        }
        return points;
    }

    static double distance(const LocalPoint& a, const LocalPoint& b)
    {
        return std::hypot(b.x - a.x, b.y - a.y);
    }

    static double available_turn_radius(
        const LocalPoint& a, const LocalPoint& b, const LocalPoint& c)
    {
        const double v1x = b.x - a.x;
        const double v1y = b.y - a.y;
        const double v2x = c.x - b.x;
        const double v2y = c.y - b.y;
        const double len1 = std::hypot(v1x, v1y);
        const double len2 = std::hypot(v2x, v2y);
        if (len1 < 1e-6 || len2 < 1e-6) {
            return 0.0;
        }
        const double dot = (v1x * v2x + v1y * v2y) / (len1 * len2);
        const double angle = std::acos(std::clamp(dot, -1.0, 1.0));
        if (angle < 1.0 * kDegToRad) {
            return std::numeric_limits<double>::infinity();
        }
        return std::min(len1, len2) / std::tan(angle * 0.5);
    }

    static bool is_colregs_protected_mode(const std::string& raw_mode)
    {
        return ship_guidance::is_colregs_protected_mode(raw_mode);
    }

    double requested_speed_at(const ship_interfaces::msg::RoutePlan& route, size_t index) const
    {
        if (index < route.speed_limit_mps.size() &&
            std::isfinite(route.speed_limit_mps[index]) &&
            route.speed_limit_mps[index] > 0.0) {
            return std::min(route.speed_limit_mps[index], max_command_speed_mps_);
        }
        return max_command_speed_mps_;
    }

    double max_requested_speed(const ship_interfaces::msg::RoutePlan& route) const
    {
        double max_speed = 0.0;
        for (size_t i = 0; i < route.latitude.size(); ++i) {
            max_speed = std::max(max_speed, requested_speed_at(route, i));
        }
        return max_speed;
    }

    void apply_speed_degradation(
        ship_interfaces::msg::RoutePlan& route,
        double suggested_max_speed_mps) const
    {
        if (!std::isfinite(suggested_max_speed_mps) || suggested_max_speed_mps <= 0.0) {
            return;
        }
        const double cap = std::min(suggested_max_speed_mps, max_command_speed_mps_);
        if (route.speed_limit_mps.empty()) {
            route.speed_limit_mps.assign(route.latitude.size(), cap);
            return;
        }
        for (double& speed : route.speed_limit_mps) {
            if (!std::isfinite(speed) || speed <= 0.0) {
                speed = cap;
            } else {
                speed = std::min(speed, cap);
            }
        }
    }

    static FeasibilityResult accepted_result()
    {
        FeasibilityResult result;
        result.accepted = true;
        result.executing = true;
        result.degraded = false;
        result.rejected = false;
        result.state = "ACCEPTED";
        result.reason = "accepted";
        result.suggested_action = "none";
        return result;
    }

    static FeasibilityResult rejected_result(
        const std::string& reason,
        const std::string& suggested_action)
    {
        FeasibilityResult result;
        result.accepted = false;
        result.executing = false;
        result.degraded = false;
        result.rejected = true;
        result.state = "REJECTED";
        result.reason = reason;
        result.suggested_action = suggested_action;
        return result;
    }

    void publish_status_for_route(
        const ship_interfaces::msg::RoutePlan& route,
        const FeasibilityResult& result)
    {
        ship_interfaces::msg::RouteExecutionStatus status;
        status.header.stamp = now();
        status.header.frame_id = "map";
        status.plan_id = route.route_id;
        status.active_route_id = route.route_id;
        status.command_source = "route_planner";
        fill_status_common(status, result);
        status_pub_->publish(status);
    }

    void publish_status_for_avoidance(
        const ship_interfaces::msg::AvoidancePlan& plan,
        const FeasibilityResult& result)
    {
        ship_interfaces::msg::RouteExecutionStatus status;
        status.header.stamp = now();
        status.header.frame_id = "map";
        status.plan_id = plan.plan_id;
        status.active_route_id = plan.plan_id;
        status.command_source = plan.command_source.empty() ? "collision_avoidance" : plan.command_source;
        fill_status_common(status, result);
        status_pub_->publish(status);
    }

    void fill_status_common(
        ship_interfaces::msg::RouteExecutionStatus& status,
        const FeasibilityResult& result) const
    {
        status.accepted = result.accepted;
        status.executing = result.executing;
        status.degraded = result.degraded;
        status.rejected = result.rejected;
        status.execution_state = result.state;
        status.reason = result.reason;
        status.suggested_action = result.suggested_action;
        status.requested_speed_mps = result.requested_speed_mps;
        status.applied_speed_mps = result.applied_speed_mps;
        status.requested_heading_deg = result.requested_heading_deg;
        status.applied_heading_deg = result.applied_heading_deg;
        status.required_turn_radius_m = result.required_turn_radius_m;
        status.estimated_available_turn_radius_m = result.estimated_available_turn_radius_m;
        status.required_decel_distance_m = result.required_decel_distance_m;
        status.available_decel_distance_m = result.available_decel_distance_m;
        status.suggested_max_speed_mps = result.suggested_max_speed_mps;
        status.suggested_min_distance_m = result.suggested_min_distance_m;

        if (has_ship_state_) {
            status.current_latitude = latest_ship_state_.latitude;
            status.current_longitude = latest_ship_state_.longitude;
            status.current_heading_deg = latest_ship_state_.heading_deg;
            status.current_course_deg = latest_ship_state_.course_deg;
            status.current_speed_mps = latest_ship_state_.speed_mps;
            status.cross_track_error_m = latest_ship_state_.cross_track_error_m;
        }
    }

    std::string nominal_route_topic_;
    std::string avoidance_plan_topic_;
    std::string active_route_topic_;
    std::string execution_status_topic_;
    std::string ship_state_topic_;

    double max_command_speed_mps_{8.0};
    double min_segment_length_m_{30.0};
    double emergency_min_segment_length_m_{15.0};
    double min_turn_radius_m_{80.0};
    double emergency_min_turn_radius_m_{45.0};
    double max_lateral_accel_mps2_{0.25};
    double max_yaw_rate_deg_s_{1.2};
    double emergency_max_yaw_rate_deg_s_{2.0};
    double max_decel_mps2_{0.08};
    double emergency_avoidance_speed_cap_mps_{3.2};
    double cruise_min_speed_mps_{3.8};
    double max_transit_speed_mps_{3.0};
    double default_avoidance_hold_s_{60.0};
    bool publish_nominal_status_{false};
    bool has_active_avoidance_{false};
    bool deferred_nominal_pending_{false};
    std::string active_avoidance_plan_id_;
    rclcpp::Time active_avoidance_until_{0, 0, RCL_ROS_TIME};

    rclcpp::Subscription<ship_interfaces::msg::RoutePlan>::SharedPtr nominal_route_sub_;
    rclcpp::Subscription<ship_interfaces::msg::AvoidancePlan>::SharedPtr avoidance_plan_sub_;
    rclcpp::Subscription<ship_interfaces::msg::GeoPosition>::SharedPtr ship_state_sub_;
    rclcpp::Publisher<ship_interfaces::msg::RoutePlan>::SharedPtr active_route_pub_;
    rclcpp::Publisher<ship_interfaces::msg::RouteExecutionStatus>::SharedPtr status_pub_;
    rclcpp::Publisher<ship_interfaces::msg::GncExecutionOdd>::SharedPtr execution_odd_pub_;
    rclcpp::TimerBase::SharedPtr deferred_nominal_timer_;

    ship_interfaces::msg::RoutePlan latest_nominal_route_;
    ship_interfaces::msg::GeoPosition latest_ship_state_;
    bool has_ship_state_{false};
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    rclcpp::spin(std::make_shared<ActiveRouteManagerNode>());
    rclcpp::shutdown();
    return 0;
}
