// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
//
// SPDX-License-Identifier: Proprietary
//
// M3 MissionManagerNode — ROS2 lifecycle node.
//
// Per v1.1.2 §7 (Mission Manager) + §3.3 Node Topology.
// PATH-D: MISRA C++:2023; exceptions allowed at ROS2 spin boundary.

#pragma once

#include <memory>
#include <optional>
#include <string>

#include <rclcpp/rclcpp.hpp>

#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/route_replan_request.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/replan_response.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "l3_external_msgs/msg/voyage_task.hpp"

#include "m3_mission_manager/eta_projector.hpp"
#include "m3_mission_manager/mission_state_machine.hpp"
#include "m3_mission_manager/replan_request_trigger.hpp"
#include "m3_mission_manager/replan_response_handler.hpp"
#include "m3_mission_manager/voyage_task_validator.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {

class MissionManagerNode : public rclcpp::Node {
 public:
  MissionManagerNode();
  ~MissionManagerNode() override = default;
  MissionManagerNode(const MissionManagerNode&) = delete;
  MissionManagerNode& operator=(const MissionManagerNode&) = delete;

 private:
  void declare_parameters();
  void create_components();
  void setup_publishers();
  void setup_subscribers();
  void setup_timers();
  void setup_logger();

  // Subscriber callbacks
  void on_voyage_task(const l3_external_msgs::msg::VoyageTask::SharedPtr msg);
  void on_planned_route(const l3_external_msgs::msg::PlannedRoute::SharedPtr msg);
  void on_speed_profile(const l3_external_msgs::msg::SpeedProfile::SharedPtr msg);
  void on_replan_response(const l3_external_msgs::msg::ReplanResponse::SharedPtr msg);
  void on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg);
  void on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg);

  // Timer callbacks
  void publish_mission_goal();
  void publish_asdr_snapshot();
  void check_replan_deadline();
  void log_heartbeat();

  // Internal helpers
  void publish_replan_request(ReplanReason reason, double deadline_s,
                              const geographic_msgs::msg::GeoPoint& current_pos);
  void publish_asdr_record(const std::string& type, const std::string& json);
  void check_and_trigger_replan(const l3_msgs::msg::ODDState& odd,
                                double current_eta_s, double planned_eta_s);

  // Component pointers
  std::unique_ptr<VoyageTaskValidator> validator_;
  std::unique_ptr<EtaProjector> eta_projector_;
  std::unique_ptr<ReplanRequestTrigger> replan_trigger_;
  std::unique_ptr<ReplanResponseHandler> replan_handler_;
  std::unique_ptr<MissionStateMachine> state_machine_;

  // Cached state
  l3_msgs::msg::ODDState::SharedPtr last_odd_state_;
  l3_msgs::msg::WorldState::SharedPtr last_world_state_;
  int32_t replan_attempt_count_ = 0;
  std::optional<std::chrono::steady_clock::time_point> replan_deadline_;
  geographic_msgs::msg::GeoPoint current_position_;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::MissionGoal>::SharedPtr mission_goal_pub_;
  rclcpp::Publisher<l3_msgs::msg::RouteReplanRequest>::SharedPtr replan_request_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;

  // Subscribers
  rclcpp::Subscription<l3_external_msgs::msg::VoyageTask>::SharedPtr voyage_task_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr planned_route_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr speed_profile_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::ReplanResponse>::SharedPtr replan_response_sub_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_state_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_state_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr mission_goal_timer_;
  rclcpp::TimerBase::SharedPtr asdr_timer_;
  rclcpp::TimerBase::SharedPtr replan_deadline_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
};

}  // namespace mass_l3::m3
