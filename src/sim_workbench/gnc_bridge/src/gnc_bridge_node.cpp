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
  sub_avoidance_ = create_subscription<l3_external_msgs::msg::AvoidanceWaypoints>(
      "/l3/m5/avoidance_waypoints", 10,
      [this](const l3_external_msgs::msg::AvoidanceWaypoints::SharedPtr msg) {
        CrossDomainHandoff::L3ToGnc item;
        item.avoidance_plan = to_gnc_avoidance_plan(*msg, msg->stamp);
        item.has_avoidance = true;
        handoff_->push_l3_to_gnc(std::move(item));
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
  RCLCPP_INFO(get_logger(), "L3 side ready: sub /l3/m5/avoidance_waypoints, /l2/planned_route");
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
  pub_avoidance_ = create_publisher<ship_interfaces::msg::AvoidancePlan>(
      "/colav/avoidance_plan", 10);
  pub_route_ = create_publisher<ship_interfaces::msg::RoutePlan>(
      "/route_planning/route_plan",
      rclcpp::QoS(rclcpp::KeepLast(10)).transient_local());
  // Drain L3->GNC items at 20 Hz and publish on the GNC domain.
  drain_timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        CrossDomainHandoff::L3ToGnc item;
        while (handoff_->pop_l3_to_gnc(item)) {
          if (item.has_avoidance) pub_avoidance_->publish(item.avoidance_plan);
          if (item.has_route)     pub_route_->publish(item.route_plan);
        }
      });
  RCLCPP_INFO(get_logger(),
      "GNC side ready: sub /ship/geo_position, /gnc/route_execution_status; "
      "pub /colav/avoidance_plan, /route_planning/route_plan");
}

L3PublisherNode::L3PublisherNode(std::shared_ptr<CrossDomainHandoff> handoff,
                                 const rclcpp::NodeOptions& options)
    : rclcpp::Node("gnc_bridge_l3_pub", options), handoff_(std::move(handoff)) {
  pub_own_ship_ = create_publisher<sil_msgs::msg::OwnShipState>(
      "/sil/own_ship_state", 10);
  pub_exec_status_ = create_publisher<l3_external_msgs::msg::GncExecutionStatus>(
      "/l3/gnc/execution_status", 10);
  drain_timer_ = create_wall_timer(
      std::chrono::milliseconds(50),
      [this]() {
        CrossDomainHandoff::GncToL3 item;
        while (handoff_->pop_gnc_to_l3(item)) {
          if (item.has_own_ship)    pub_own_ship_->publish(item.own_ship);
          if (item.has_exec_status) pub_exec_status_->publish(item.exec_status);
        }
      });
  RCLCPP_INFO(get_logger(),
      "L3 pub side ready: pub /sil/own_ship_state, /l3/gnc/execution_status");
}

}  // namespace gnc_bridge
