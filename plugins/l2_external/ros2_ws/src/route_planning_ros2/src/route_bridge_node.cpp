/**
 * route_bridge_node.cpp
 * ====================
 * project1: 航线规划 ↔ GNC 通讯桥接节点（不调用水动力）
 *
 * 对接协议：
 *   - 你(gnc_interface.py)生成的航线 JSON → 本节点读取
 *   - 本节点 → /route_planning/route_plan (ship_interfaces/RoutePlan)   发送航线
 *   - 同事 ship_dynamics_node → /ship/odometry (nav_msgs/Odometry)      订阅船位
 *   - 本节点 → WebSocket 8766 → 前端                                   推送船位
 *
 * 注意：不在project1里启动任何GNC节点，只做数据格式转换和通讯转发。
 */

#include <rclcpp/rclcpp.hpp>
#include <ship_interfaces/msg/route_plan.hpp>
#include <nav_msgs/msg/odometry.hpp>

#include <nlohmann/json.hpp>

#include <websocketpp/config/asio.hpp>
#include <websocketpp/server.hpp>

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

using ship_interfaces::msg::RoutePlan;
using nav_msgs::msg::Odometry;

using ws_server_t = websocketpp::server<websocketpp::config::asio>;

static constexpr double EARTH_RADIUS_M = 6378137.0;
static constexpr double KN_TO_MPS = 0.5144;
static constexpr double MPS_TO_KN = 1.94384;

// NED局部切平面 (米) → WGS84经纬度
static std::pair<double, double> local_to_latlon(
    double x, double y, double lat0, double lon0)
{
    double lat = lat0 + y / (EARTH_RADIUS_M * M_PI / 180.0);
    double lon = lon0 + x / (EARTH_RADIUS_M * M_PI / 180.0 * std::cos(lat0 * M_PI / 180.0));
    return {lat, lon};
}

class RouteBridgeNode : public rclcpp::Node {
public:
    RouteBridgeNode()
    : Node("route_bridge_node"),
      origin_lat_(0.0),
      origin_lon_(0.0),
      origin_set_(false)
    {
        // ────────── 参数 ──────────
        this->declare_parameter<std::string>(
            "route_data_path",
            "/home/qiao.huang/route_planning/route_output/gnc_route_data.json");
        this->declare_parameter<std::string>("selected_route", "shortest");
        this->declare_parameter<int>("ws_port", 8766);
        this->declare_parameter<double>("publish_rate", 1.0);

        // ────────── 加载航线数据 ──────────
        load_route_data();

        // ────────── Publisher: 同事的入口 ──────────
        route_plan_pub_ = this->create_publisher<RoutePlan>(
            "/route_planning/route_plan", 10);

        // ────────── Subscriber: 船位反馈 ──────────
        odom_sub_ = this->create_subscription<Odometry>(
            "/ship/odometry", 10,
            std::bind(&RouteBridgeNode::odom_callback, this, std::placeholders::_1));

        // ────────── 定时器: 定期重发RoutePlan ──────────
        double rate = this->get_parameter("publish_rate").as_double();
        timer_ = this->create_wall_timer(
            std::chrono::duration<double>(1.0 / rate),
            std::bind(&RouteBridgeNode::timer_callback, this));

        // ────────── WebSocket ──────────
        int ws_port = this->get_parameter("ws_port").as_int();
        ws_thread_ = std::thread(&RouteBridgeNode::run_ws_server, this, ws_port);

        RCLCPP_INFO(this->get_logger(),
            "RouteBridgeNode 启动完成 | selected_route=%s | 航点数=%zu | WS=:%d",
            this->get_parameter("selected_route").as_string().c_str(),
            sample_points_.size(), ws_port);
    }

    ~RouteBridgeNode()
    {
        if (ws_server_.is_running()) {
            ws_server_.stop();
        }
        if (ws_thread_.joinable()) {
            ws_thread_.join();
        }
    }

private:
    // Publishers/Subscribers
    rclcpp::Publisher<RoutePlan>::SharedPtr route_plan_pub_;
    rclcpp::Subscription<Odometry>::SharedPtr odom_sub_;
    rclcpp::TimerBase::SharedPtr timer_;

    // WebSocket
    ws_server_t ws_server_;
    std::set<websocketpp::connection_hdl, std::owner_less<websocketpp::connection_hdl>> ws_clients_;
    std::mutex ws_clients_mutex_;
    std::thread ws_thread_;

    // State
    json latest_ship_state_;
    std::mutex ship_state_mutex_;
    RoutePlan route_plan_msg_;
    bool route_plan_ready_ = false;
    double origin_lat_;
    double origin_lon_;
    bool origin_set_;
    json sample_points_;

    void load_route_data()
    {
        std::string path = this->get_parameter("route_data_path").as_string();
        std::string selected = this->get_parameter("selected_route").as_string();

        if (!fs::exists(path)) {
            RCLCPP_ERROR(this->get_logger(), "路线数据文件不存在: %s", path.c_str());
            return;
        }

        try {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                RCLCPP_ERROR(this->get_logger(), "无法打开路线数据文件: %s", path.c_str());
                return;
            }
            json route_data = json::parse(ifs);
            ifs.close();

            if (!route_data.contains("routes") || !route_data["routes"].contains(selected)) {
                std::string available;
                if (route_data.contains("routes")) {
                    for (auto it = route_data["routes"].begin(); it != route_data["routes"].end(); ++it) {
                        if (!available.empty()) available += ", ";
                        available += it.key();
                    }
                }
                RCLCPP_ERROR(this->get_logger(),
                    "路线键 \"%s\" 不存在, 可用: [%s]", selected.c_str(), available.c_str());
                return;
            }

            json route = route_data["routes"][selected];
            if (!route.contains("sample_points") || route["sample_points"].empty()) {
                RCLCPP_ERROR(this->get_logger(), "路线 \"%s\" 没有 sample_points", selected.c_str());
                return;
            }

            sample_points_ = route["sample_points"];

            // 原点 = 航线第一个点的经纬度
            origin_lat_ = sample_points_[0].value("lat", 0.0);
            origin_lon_ = sample_points_[0].value("lon", 0.0);
            origin_set_ = true;

            RCLCPP_INFO(this->get_logger(),
                "原点: lat=%.6f, lon=%.6f", origin_lat_, origin_lon_);

            // 构造 RoutePlan 消息
            auto now = std::chrono::system_clock::now();
            auto now_tt = std::chrono::system_clock::to_time_t(now);
            std::tm tm_utc{};
#ifdef _WIN32
            gmtime_s(&tm_utc, &now_tt);
#else
            gmtime_r(&now_tt, &tm_utc);
#endif
            std::ostringstream id_stream;
            id_stream << selected << "-" << std::put_time(&tm_utc, "%Y%m%d%H%M%S");

            route_plan_msg_.header.stamp = this->now();
            route_plan_msg_.header.frame_id = "map";
            route_plan_msg_.route_id = id_stream.str();
            route_plan_msg_.route_type = "transit";

            route_plan_msg_.latitude.clear();
            route_plan_msg_.longitude.clear();
            route_plan_msg_.speed_limit_mps.clear();
            route_plan_msg_.navigation_mode.clear();

            for (const auto& pt : sample_points_) {
                double lat = pt.value("lat", 0.0);
                double lon = pt.value("lon", 0.0);
                route_plan_msg_.latitude.push_back(lat);
                route_plan_msg_.longitude.push_back(lon);

                double spd_kn = pt.value("speed_kn", 0.0);
                route_plan_msg_.speed_limit_mps.push_back(
                    spd_kn > 0.0 ? spd_kn * KN_TO_MPS : 0.0);
                route_plan_msg_.navigation_mode.push_back(std::string(""));
            }

            route_plan_ready_ = true;
            RCLCPP_INFO(this->get_logger(),
                "RoutePlan 已构造: %zu 航点", sample_points_.size());
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "加载路线数据失败: %s", e.what());
        }
    }

    void timer_callback()
    {
        if (route_plan_ready_) {
            route_plan_pub_->publish(route_plan_msg_);
        }
    }

    void odom_callback(const Odometry::SharedPtr msg)
    {
        if (!origin_set_) {
            return;
        }

        double x = msg->pose.pose.position.x;
        double y = msg->pose.pose.position.y;

        // NED → WGS84
        auto [lat, lon] = local_to_latlon(x, y, origin_lat_, origin_lon_);

        // 四元数 → 欧拉角
        const auto& q = msg->pose.pose.orientation;
        double siny_cosp = 2.0 * (q.w * q.z + q.x * q.y);
        double cosy_cosp = 1.0 - 2.0 * (q.y * q.y + q.z * q.z);
        double yaw_rad = std::atan2(siny_cosp, cosy_cosp);
        double heading_deg = std::fmod(std::degrees(yaw_rad), 360.0);
        if (heading_deg < 0.0) heading_deg += 360.0;

        double sinr_cosp = 2.0 * (q.w * q.x + q.y * q.z);
        double cosr_cosp = 1.0 - 2.0 * (q.x * q.x + q.y * q.y);
        double roll_rad = std::atan2(sinr_cosp, cosr_cosp);
        double roll_deg = std::degrees(roll_rad);

        double sinp = 2.0 * (q.w * q.y - q.z * q.x);
        sinp = std::max(-1.0, std::min(1.0, sinp));
        double pitch_rad = std::asin(sinp);
        double pitch_deg = std::degrees(pitch_rad);

        double vx = msg->twist.twist.linear.x;
        double vy = msg->twist.twist.linear.y;
        double speed_mps = std::sqrt(vx * vx + vy * vy);

        // 时间戳
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

        json ship_state = {
            {"latitude",       lat},
            {"longitude",      lon},
            {"heading_deg",    heading_deg},
            {"speed_mps",      speed_mps},
            {"speed_kn",       speed_mps * MPS_TO_KN},
            {"roll_deg",       roll_deg},
            {"pitch_deg",      pitch_deg},
            {"roll_rate_dps",  std::degrees(msg->twist.twist.angular.x)},
            {"pitch_rate_dps", std::degrees(msg->twist.twist.angular.y)},
            {"yaw_rate_dps",   std::degrees(msg->twist.twist.angular.z)},
            {"x_local",        x},
            {"y_local",        y},
            {"mode",           "simulation"},
            {"timestamp",      timestamp}
        };

        {
            std::lock_guard<std::mutex> lock(ship_state_mutex_);
            latest_ship_state_ = ship_state;
        }

        broadcast_ship_state();
    }

    void broadcast_ship_state()
    {
        std::string payload;
        {
            std::lock_guard<std::mutex> lock(ship_state_mutex_);
            payload = latest_ship_state_.dump();
        }

        std::lock_guard<std::mutex> lock(ws_clients_mutex_);
        for (auto it = ws_clients_.begin(); it != ws_clients_.end(); ) {
            try {
                ws_server_.send(*it, payload, websocketpp::frame::opcode::text);
                ++it;
            } catch (const std::exception&) {
                // 发送失败, 移除客户端
                it = ws_clients_.erase(it);
            }
        }
    }

    void run_ws_server(int port)
    {
        try {
            ws_server_.set_access_channels(websocketpp::log::alevel::none);
            ws_server_.clear_access_channels(websocketpp::log::alevel::all);

            ws_server_.init_asio();

            ws_server_.set_open_handler([this](websocketpp::connection_hdl hdl) {
                std::lock_guard<std::mutex> lock(ws_clients_mutex_);
                ws_clients_.insert(hdl);
                RCLCPP_INFO(this->get_logger(), "WebSocket 客户端连接");

                // 发送最新状态
                std::string payload;
                {
                    std::lock_guard<std::mutex> slock(ship_state_mutex_);
                    payload = latest_ship_state_.dump();
                }
                if (!payload.empty() && payload != "null") {
                    try {
                        ws_server_.send(hdl, payload, websocketpp::frame::opcode::text);
                    } catch (const std::exception&) {}
                }
            });

            ws_server_.set_close_handler([this](websocketpp::connection_hdl hdl) {
                std::lock_guard<std::mutex> lock(ws_clients_mutex_);
                ws_clients_.erase(hdl);
                RCLCPP_INFO(this->get_logger(), "WebSocket 客户端断开");
            });

            ws_server_.set_message_handler(
                [this](websocketpp::connection_hdl, ws_server_t::message_ptr) {
                    // 忽略客户端消息
                });

            ws_server_.listen(port);
            ws_server_.start_accept();

            RCLCPP_INFO(this->get_logger(), "WebSocket 服务已启动, 端口 %d", port);
            ws_server_.run();
        } catch (const std::exception& e) {
            RCLCPP_ERROR(this->get_logger(), "WebSocket 服务异常: %s", e.what());
        }
    }
};

int main(int argc, char** argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<RouteBridgeNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
