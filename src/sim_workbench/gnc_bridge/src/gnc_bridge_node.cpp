// gnc_bridge_node implementation (Track A A4). Three nodes share one in-process
// handoff: L3SideNode (domain 42, subscribes L3), GncSideNode (domain 50,
// subscribes GNC + publishes GNC), L3PublisherNode (domain 42, publishes L3).
// Each node runs on its own single-threaded executor so the two DDS domains
// never share a context.
#include "gnc_bridge/gnc_bridge_node.hpp"

namespace gnc_bridge {

L3SideNode::L3SideNode(std::shared_ptr<CrossDomainHandoff> handoff,
                       const rclcpp::NodeOptions& options)
    : rclcpp::Node("gnc_bridge_l3_side", options), handoff_(std::move(handoff)) {
  sub_avoidance_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", 10,
      [this](const l3_msgs::msg::AvoidancePlan::SharedPtr msg) {
        last_avoidance_plan_wall_time_ = now();
        CrossDomainHandoff::L3ToGnc item;
        item.avoidance_plan = to_gnc_avoidance_plan(*msg, msg->stamp);
        item.has_avoidance = true;
        handoff_->push_l3_to_gnc(std::move(item));
      });
  avoidance_watchdog_timer_ = create_wall_timer(
      std::chrono::seconds(5),
      [this]() {
        if (!last_avoidance_plan_wall_time_.has_value()) {
          return;
        }
        const double quiet_s = (now() - last_avoidance_plan_wall_time_.value()).seconds();
        if (quiet_s > 60.0) {
          RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
              "no /l3/m5/avoidance_plan heartbeat for %.1fs (> TMR 60s)", quiet_s);
        }
      });
  sub_route_ = create_subscription<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route",
      rclcpp::QoS(rclcpp::KeepLast(10)).transient_local(),
      [this](const l3_external_msgs::msg::PlannedRoute::SharedPtr msg) {
        CrossDomainHandoff::L3ToGnc item;
        item.route_plan = to_gnc_route_plan(*msg, msg->stamp);
        item.has_route = true;
        handoff_->push_l3_to_gnc(std::move(item));
      });
  sub_reset_ = create_subscription<sil_msgs::msg::ShipReset>(
      "/l3/sim/reset_own_ship", latched_reset_qos(),
      [this](const sil_msgs::msg::ShipReset::SharedPtr msg) {
        CrossDomainHandoff::L3ToGnc item;
        ship_interfaces::msg::ShipReset out;
        out.header = msg->header;
        out.latitude = msg->latitude;
        out.longitude = msg->longitude;
        out.heading_deg = msg->heading_deg;
        out.sog_kn = msg->sog_kn;
        item.ship_reset = std::move(out);
        item.has_reset = true;
        RCLCPP_INFO(get_logger(),
            "received reset_own_ship: lat=%.6f lon=%.6f heading=%.1f sog=%.1f",
            msg->latitude, msg->longitude, msg->heading_deg, msg->sog_kn);
        handoff_->push_l3_to_gnc(std::move(item));
      });
  RCLCPP_INFO(get_logger(),
      "L3 side ready: sub /l3/m5/avoidance_plan, /l2/planned_route, /l3/sim/reset_own_ship");
}

GncSideNode::GncSideNode(std::shared_ptr<CrossDomainHandoff> handoff,
                         const rclcpp::NodeOptions& options)
    : rclcpp::Node("gnc_bridge_gnc_side", options), handoff_(std::move(handoff)) {
  sub_geo_ = create_subscription<ship_interfaces::msg::GeoPosition>(
      "/ship/geo_position", 10,
      [this](const ship_interfaces::msg::GeoPosition::SharedPtr msg) {
        CrossDomainHandoff::GncToL3 item;
        item.own_ship = to_sil_own_ship_state(*msg, msg->header.stamp);
        item.has_own_ship = true;
        handoff_->push_gnc_to_l3(std::move(item));
      });
  sub_status_ = create_subscription<ship_interfaces::msg::RouteExecutionStatus>(
      "/gnc/route_execution_status", 10,
      [this](const ship_interfaces::msg::RouteExecutionStatus::SharedPtr msg) {
        CrossDomainHandoff::GncToL3 item;
        item.exec_status = to_l3_gnc_execution_status(*msg, msg->header.stamp);
        item.has_exec_status = true;
        handoff_->push_gnc_to_l3(std::move(item));
      });
  // W2: forward the latched GNC execution-ODD contract so TDL (M5) on domain 42
  // can consume the actual execution limits. transient_local matches the arm
  // publisher QoS so late-joining subscribers get the last latched value.
  sub_odd_ = create_subscription<ship_interfaces::msg::GncExecutionOdd>(
      "/gnc/execution_odd",
      rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable(),
      [this](const ship_interfaces::msg::GncExecutionOdd::SharedPtr msg) {
        CrossDomainHandoff::GncToL3 item;
        item.execution_odd = *msg;
        item.has_execution_odd = true;
        handoff_->push_gnc_to_l3(std::move(item));
      });
  pub_avoidance_ = create_publisher<ship_interfaces::msg::AvoidancePlan>(
      "/colav/avoidance_plan", 10);
  pub_route_ = create_publisher<ship_interfaces::msg::RoutePlan>(
      "/route_planning/route_plan",
      rclcpp::QoS(rclcpp::KeepLast(10)).transient_local());
  pub_geo_reset_ = create_publisher<ship_interfaces::msg::ShipReset>(
      "/ship/geo_origin_reset", latched_reset_qos());
  pub_dynamics_reset_ = create_publisher<ship_interfaces::msg::ShipReset>(
      "/ship/dynamics_reset", latched_reset_qos());
  // Drain L3->GNC items at 20 Hz and publish on the GNC domain. Uses the
  // non-blocking try_pop so the executor thread is never stalled (a blocking
  // pop here would starve subscription callbacks on the same executor).
  drain_timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        CrossDomainHandoff::L3ToGnc item;
        while (handoff_->try_pop_l3_to_gnc(item)) {
          if (item.has_avoidance) {
            rebase_avoidance_plan_timebase(item.avoidance_plan, now());
            pub_avoidance_->publish(item.avoidance_plan);
          }
          if (item.has_route)     pub_route_->publish(item.route_plan);
          if (item.has_reset) {
            pub_geo_reset_->publish(item.ship_reset);
            pub_dynamics_reset_->publish(item.ship_reset);
            RCLCPP_INFO(get_logger(),
                "forwarded reset to GNC: lat=%.6f lon=%.6f heading=%.1f sog=%.1f",
                item.ship_reset.latitude, item.ship_reset.longitude,
                item.ship_reset.heading_deg, item.ship_reset.sog_kn);
          }
        }
      });
  RCLCPP_INFO(get_logger(),
      "GNC side ready: sub /ship/geo_position, /gnc/route_execution_status, "
      "/gnc/execution_odd; "
      "pub /colav/avoidance_plan, /route_planning/route_plan, "
      "/ship/geo_origin_reset, /ship/dynamics_reset");
}

L3PublisherNode::L3PublisherNode(std::shared_ptr<CrossDomainHandoff> handoff,
                                 const rclcpp::NodeOptions& options)
    : rclcpp::Node("gnc_bridge_l3_pub", options), handoff_(std::move(handoff)) {
  pub_own_ship_ = create_publisher<sil_msgs::msg::OwnShipState>(
      "/sil/own_ship_state", 10);
  pub_exec_status_ = create_publisher<l3_external_msgs::msg::GncExecutionStatus>(
      "/l3/gnc/execution_status", 10);
  // W2: republish the execution-ODD contract on domain 42 (same topic name).
  pub_odd_ = create_publisher<ship_interfaces::msg::GncExecutionOdd>(
      "/gnc/execution_odd",
      rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());
  drain_timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        CrossDomainHandoff::GncToL3 item;
        while (handoff_->try_pop_gnc_to_l3(item)) {
          if (item.has_own_ship)    pub_own_ship_->publish(item.own_ship);
          if (item.has_exec_status) pub_exec_status_->publish(item.exec_status);
          if (item.has_execution_odd) pub_odd_->publish(item.execution_odd);
        }
      });
  RCLCPP_INFO(get_logger(),
      "L3 pub side ready: pub /sil/own_ship_state, /l3/gnc/execution_status, "
      "/gnc/execution_odd");
}

}  // namespace gnc_bridge
