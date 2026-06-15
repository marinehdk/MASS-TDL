/**
 * gnc_sim_node.cpp - ROS2 桥接节点 (project1)
 *
 * 发布: /route_planning/route_plan (ship_interfaces/RoutePlan)
 *       → coordinate_transform_node 自动转成 /ship/waypoints (nav_msgs/Path)
 *       → ship_guidance_node 订阅 /ship/waypoints
 *
 * 订阅: /ship/geo_position (ship_interfaces/GeoPosition)
 *       → coordinate_transform_node 从 /ship/odometry 反算经纬度/航向
 *       → 本节点直接用, 不用自己反算
 *
 * 共享文件:
 *   gnc_bridge_route.json  (gnc_sim.py 写入, 航线航点)
 *   gnc_bridge_state.json  (本节点写入, 最新船位)
 */

#include <rclcpp/rclcpp.hpp>
#include <ship_interfaces/msg/route_plan.hpp>
#include <ship_interfaces/msg/geo_position.hpp>
#include <std_msgs/msg/header.hpp>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;
using json = nlohmann::json;

using ship_interfaces::msg::RoutePlan;
using ship_interfaces::msg::GeoPosition;
using std_msgs::msg::Header;

static const std::string SHARED_STATE_FILE = "/home/qiao.huang/route_planning/gnc_bridge_state.json";
static const std::string SHARED_ROUTE_FILE = "/home/qiao.huang/route_planning/gnc_bridge_route.json";
static const std::string GEO_LOG_FILE      = "/home/qiao.huang/route_planning/geo_position_log.txt";

class GncSimNode : public rclcpp::Node {
public:
    GncSimNode()
    : Node("gnc_sim_node"),
      current_route_id_()
    {
        // ── 发布者: RoutePlan → coordinate_transform_node 会自动转成 Path ──
        route_plan_pub_ = this->create_publisher<RoutePlan>(
            "/route_planning/route_plan", 10);

        // ── 订阅者: GeoPosition (coordinate_transform_node 从 odom 反算) ──
        geo_pos_sub_ = this->create_subscription<GeoPosition>(
            "/ship/geo_position", 10,
            std::bind(&GncSimNode::geo_pos_callback, this, std::placeholders::_1));

        // ── 定时器: 周期性重发 RoutePlan + 0.5s 轮询共享文件 ──
        publish_timer_ = this->create_wall_timer(
            std::chrono::duration<double>(1.0),
            [this]() { /* timer_callback: 不需要重发 */ });

        route_check_timer_ = this->create_wall_timer(
            std::chrono::duration<double>(0.5),
            std::bind(&GncSimNode::check_route_update, this));

        RCLCPP_INFO(this->get_logger(),
            "gnc_sim_node 已启动 (发布RoutePlan + 订阅GeoPosition)");
    }

private:
    rclcpp::Publisher<RoutePlan>::SharedPtr route_plan_pub_;
    rclcpp::Subscription<GeoPosition>::SharedPtr geo_pos_sub_;
    rclcpp::TimerBase::SharedPtr publish_timer_;
    rclcpp::TimerBase::SharedPtr route_check_timer_;

    std::string current_route_id_;
    json latest_ship_state_;

    void check_route_update()
    {
        if (!fs::exists(SHARED_ROUTE_FILE)) {
            return;
        }

        try {
            std::ifstream ifs(SHARED_ROUTE_FILE);
            if (!ifs.is_open()) {
                return;
            }
            json data = json::parse(ifs);
            ifs.close();

            if (!data.contains("sample_points") || data["sample_points"].empty()) {
                return;
            }

            auto route_pts = data["sample_points"];
            std::string new_id = data.value("route_id", "");
            if (new_id == current_route_id_) {
                return;
            }

            // ── 构造 RoutePlan (WGS84) → coordinate_transform_node 自动转 NED Path ──
            RoutePlan msg;
            msg.header = Header();
            msg.header.stamp = this->now();
            msg.header.frame_id = "map";
            msg.route_id = data.value("route_id", std::string(""));
            msg.route_type = data.value("route_type", std::string("transit"));

            for (const auto& p : route_pts) {
                msg.latitude.push_back(static_cast<double>(p.value("lat", 0.0)));
                msg.longitude.push_back(static_cast<double>(p.value("lon", 0.0)));
                double speed_kn = p.value("speed_kn", 0.0);
                msg.speed_limit_mps.push_back(speed_kn * 0.5144);
                msg.navigation_mode.push_back(std::string(""));
            }

            current_route_id_ = new_id;
            route_plan_pub_->publish(msg);

            double first_lat = route_pts[0].value("lat", 0.0);
            double first_lon = route_pts[0].value("lon", 0.0);
            RCLCPP_INFO(this->get_logger(),
                "已发布新航线: %zu 航点, 首点 (%.6f, %.6f), route_id=%s",
                route_pts.size(), first_lat, first_lon, new_id.c_str());
        } catch (const std::exception& e) {
            RCLCPP_WARN(this->get_logger(), "读取航线文件失败: %s", e.what());
        }
    }

    void geo_pos_callback(const GeoPosition::SharedPtr msg)
    {
        // coordinate_transform_node 从 odom 反算的经纬度/航向, 直接用
        auto now = std::chrono::system_clock::now();
        auto now_tt = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        std::tm tm_utc{};
#ifdef _WIN32
        gmtime_s(&tm_utc, &now_tt);
#else
        gmtime_r(&now_tt, &tm_utc);
#endif
        std::ostringstream ts_stream;
        ts_stream << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S")
                  << "." << std::setfill('0') << std::setw(3) << now_ms.count() << "Z";
        std::string timestamp = ts_stream.str();

        latest_ship_state_ = json{
            {"latitude",       msg->latitude},
            {"longitude",      msg->longitude},
            {"heading_deg",    msg->heading_deg},
            {"course_deg",     msg->course_deg},
            {"speed_mps",      msg->speed_mps},
            {"speed_kn",       msg->speed_mps * 1.94384},
            {"surge_mps",      msg->surge_mps},
            {"sway_mps",       msg->sway_mps},
            {"yaw_rate_dps",   msg->yaw_rate_deg_s},
            {"vel_north_mps",  msg->vel_north_mps},
            {"vel_east_mps",   msg->vel_east_mps},
            {"x_ned",          msg->x_ned},
            {"y_ned",          msg->y_ned},
            {"nav_state",      msg->nav_state},
            {"nav_state_str",  msg->nav_state_str},
            {"origin_locked",  msg->origin_locked},
            {"origin_lat",     msg->origin_lat},
            {"origin_lon",     msg->origin_lon},
            {"timestamp",      timestamp}
        };

        // 记录每次收到的经纬度到日志文件
        try {
            std::ofstream log_file(GEO_LOG_FILE, std::ios::app);
            if (log_file.is_open()) {
                log_file << std::fixed << std::setprecision(8)
                         << timestamp << " "
                         << "lat=" << msg->latitude << " "
                         << "lon=" << msg->longitude << " "
                         << std::setprecision(2)
                         << "hdg=" << msg->heading_deg << " "
                         << "cog=" << msg->course_deg << " "
                         << std::setprecision(3)
                         << "spd=" << msg->speed_mps << "m/s "
                         << "surge=" << msg->surge_mps << " "
                         << "sway=" << msg->sway_mps << " "
                         << std::setprecision(2)
                         << "x_ned=" << msg->x_ned << " "
                         << "y_ned=" << msg->y_ned << " "
                         << std::setprecision(8)
                         << "origin=(" << msg->origin_lat << "," << msg->origin_lon << ") "
                         << "locked=" << static_cast<int>(msg->origin_locked) << " "
                         << "nav=" << msg->nav_state
                         << "(" << msg->nav_state_str << ")\n";
            }
        } catch (const std::exception&) {
            // 静默忽略日志写入失败
        }

        // 原子写入状态文件 (write to .tmp then rename)
        try {
            std::string tmp_path = SHARED_STATE_FILE + ".tmp";
            {
                std::ofstream ofs(tmp_path);
                if (ofs.is_open()) {
                    ofs << latest_ship_state_.dump();
                }
            }
            fs::rename(tmp_path, SHARED_STATE_FILE);
        } catch (const std::exception&) {
            // 静默忽略状态文件写入失败
        }
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<GncSimNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
