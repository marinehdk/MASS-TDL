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

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

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

#include "l3_msgs/msg/to_r_request.hpp"
#include "l3_external_msgs/msg/tracking_error.hpp"
#include "m3_mission_manager/current_error_monitor.hpp"
#include "m3_mission_manager/l1_watchdog_monitor.hpp"

namespace mass_l3::m3 {

class MissionManagerNode : public rclcpp::Node {
 public:
  explicit MissionManagerNode(
      const rclcpp::NodeOptions& options = rclcpp::NodeOptions{});
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
  void publish_asdr_record(const std::string& type, const nlohmann::json& payload);
  void check_and_trigger_replan(const l3_msgs::msg::ODDState& odd,
                                double current_eta_s, double planned_eta_s);
  void on_tracking_error(
      const l3_external_msgs::msg::TrackingError::SharedPtr msg);
  void evaluate_l1_watchdog();
  void publish_tor_request(uint8_t reason, float deadline_s);
  void check_current_error_severity_change(
      std::chrono::steady_clock::time_point now);
  uint8_t map_task_validity(TaskValidity val) const;
  static const char* task_validity_to_str(TaskValidity val) noexcept;

  // Component pointers
  std::unique_ptr<VoyageTaskValidator> validator_;
  std::unique_ptr<EtaProjector> eta_projector_;
  std::unique_ptr<ReplanRequestTrigger> replan_trigger_;
  std::unique_ptr<ReplanResponseHandler> replan_handler_;
  std::unique_ptr<MissionStateMachine> state_machine_;

  // D2.3 component pointers
  std::unique_ptr<CurrentErrorMonitor>  current_error_monitor_;
  std::unique_ptr<L1WatchdogMonitor>    l1_watchdog_;

  // Cached state
  l3_msgs::msg::ODDState::SharedPtr last_odd_state_;
  l3_msgs::msg::WorldState::SharedPtr last_world_state_;
  int32_t replan_attempt_count_ = 0;
  std::optional<std::chrono::steady_clock::time_point> replan_deadline_;
  geographic_msgs::msg::GeoPoint current_position_;

  // D2.3 state tracking
  CurrentErrorSeverity last_current_error_severity_ = CurrentErrorSeverity::NORMAL;
  L1WatchdogStatus     last_l1_watchdog_status_     = L1WatchdogStatus::OK;
  uint8_t              last_odd_zone_                = 0xFFU;  // 0xFF = uninitialized
  std::optional<std::chrono::steady_clock::time_point> last_planned_route_time_;
  std::optional<std::chrono::steady_clock::time_point> last_voyage_task_time_;
  double                                               l1_timeout_s_ = 30.0;
  double                                               l2_timeout_s_ = 5.0;

  // Logger
  std::shared_ptr<spdlog::logger> logger_;

  // Publishers
  rclcpp::Publisher<l3_msgs::msg::MissionGoal>::SharedPtr mission_goal_pub_;
  rclcpp::Publisher<l3_msgs::msg::RouteReplanRequest>::SharedPtr replan_request_pub_;
  rclcpp::Publisher<l3_msgs::msg::ASDRRecord>::SharedPtr asdr_pub_;
  rclcpp::Publisher<l3_msgs::msg::ToRRequest>::SharedPtr tor_pub_;

  // Subscribers
  rclcpp::Subscription<l3_external_msgs::msg::VoyageTask>::SharedPtr voyage_task_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::PlannedRoute>::SharedPtr planned_route_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::SpeedProfile>::SharedPtr speed_profile_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::ReplanResponse>::SharedPtr replan_response_sub_;
  rclcpp::Subscription<l3_msgs::msg::ODDState>::SharedPtr odd_state_sub_;
  rclcpp::Subscription<l3_msgs::msg::WorldState>::SharedPtr world_state_sub_;
  rclcpp::Subscription<l3_external_msgs::msg::TrackingError>::SharedPtr
      tracking_error_sub_;

  // Timers
  rclcpp::TimerBase::SharedPtr mission_goal_timer_;
  rclcpp::TimerBase::SharedPtr asdr_timer_;
  rclcpp::TimerBase::SharedPtr replan_deadline_timer_;
  rclcpp::TimerBase::SharedPtr heartbeat_timer_;
  rclcpp::TimerBase::SharedPtr l1_watchdog_timer_;
};

}  // namespace mass_l3::m3
