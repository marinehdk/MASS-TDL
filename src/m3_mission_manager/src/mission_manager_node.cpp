// Copyright 2026 MASS-L3-TDL Authors. All rights reserved.
//
// SPDX-License-Identifier: Proprietary
//
// M3 MissionManagerNode — ROS2 node implementation.
//
// Per v1.1.2 §7 + §3.3 Node Topology.
// PATH-D: MISRA C++:2023; spdlog/ROS2 API calls exempt per project policy.

#include "m3_mission_manager/mission_manager_node.hpp"

#include <chrono>
#include <cstdint>
#include <ratio>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace mass_l3::m3 {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MissionManagerNode::MissionManagerNode()
    : Node("m3_mission_manager")
{
  declare_parameters();
  create_components();
  setup_logger();
  setup_publishers();
  setup_subscribers();
  setup_timers();

  // Transition state machine from Init to Idle now that all subscribers/timers
  // are ready. Per spec §3.5: Init→Idle on "节点初始化完成、subscribers 就绪".
  MissionEvent ready_event;
  ready_event.type = MissionEvent::Type::NodeReady;
  state_machine_->handle_event(ready_event);

  RCLCPP_INFO(get_logger(), "M3 MissionManagerNode initialised");
  if (logger_) {
    logger_->info("MissionManagerNode initialised");
  }
}

// ---------------------------------------------------------------------------
// Parameter declaration
// ---------------------------------------------------------------------------

void MissionManagerNode::declare_parameters()
{
  // Voyage Task Validator
  declare_parameter("voyage_task.departure_distance_max_km", 2.0);
  declare_parameter("voyage_task.eta_window_min_s", 600);
  declare_parameter("voyage_task.eta_window_max_s", 259200);
  declare_parameter("voyage_task.waypoint_max_distance_nm", 50.0);
  declare_parameter("voyage_task.exclusion_zone_buffer_m", 500.0);

  // EtaProjector
  declare_parameter("eta.sampling_interval_s", 60);
  declare_parameter("eta.forecast_horizon_max_s", 3600);
  declare_parameter("eta.sea_current_uncertainty_kn", 0.3);
  declare_parameter("eta.world_state_age_threshold_s", 0.5);

  // Replan
  declare_parameter("replan.deadline_mrc_required_s", 30.0);
  declare_parameter("replan.deadline_odd_exit_critical_s", 60.0);
  declare_parameter("replan.deadline_odd_exit_degraded_s", 120.0);
  declare_parameter("replan.deadline_mission_infeasible_s", 120.0);
  declare_parameter("replan.deadline_congestion_s", 300.0);
  declare_parameter("replan.attempt_max_count", 3);

  // ODD thresholds
  declare_parameter("odd.degraded_threshold", 0.7);
  declare_parameter("odd.critical_threshold", 0.3);
  declare_parameter("odd.degraded_buffer_s", 1.0);

  // General
  declare_parameter("eta_infeasible_margin_s", 600.0);
  declare_parameter("mission_goal.publish_rate_hz", 0.5);
  declare_parameter("asdr.heartbeat_rate_hz", 2.0);
  declare_parameter("timeout.world_state_s", 0.5);
}

// ---------------------------------------------------------------------------
// Component creation
// ---------------------------------------------------------------------------

void MissionManagerNode::create_components()
{
  // -- VoyageTaskValidator config --
  VoyageTaskValidatorConfig vtv_cfg;
  vtv_cfg.departure_distance_max_km = get_parameter("voyage_task.departure_distance_max_km").as_double();
  vtv_cfg.eta_window_min_s = get_parameter("voyage_task.eta_window_min_s").as_int();
  vtv_cfg.eta_window_max_s = get_parameter("voyage_task.eta_window_max_s").as_int();
  vtv_cfg.waypoint_max_distance_nm = get_parameter("voyage_task.waypoint_max_distance_nm").as_double();
  vtv_cfg.exclusion_zone_buffer_m = get_parameter("voyage_task.exclusion_zone_buffer_m").as_double();
  validator_ = std::make_unique<VoyageTaskValidator>(vtv_cfg);

  // -- EtaProjector config --
  EtaProjectorConfig ep_cfg;
  ep_cfg.sampling_interval_s = static_cast<int32_t>(get_parameter("eta.sampling_interval_s").as_int());
  ep_cfg.forecast_horizon_max_s = get_parameter("eta.forecast_horizon_max_s").as_int();
  ep_cfg.sea_current_uncertainty_kn = get_parameter("eta.sea_current_uncertainty_kn").as_double();
  ep_cfg.world_state_age_threshold_s = get_parameter("eta.world_state_age_threshold_s").as_double();
  ep_cfg.infeasible_margin_s = get_parameter("eta_infeasible_margin_s").as_double();
  eta_projector_ = std::make_unique<EtaProjector>(ep_cfg);

  // -- ReplanRequestTrigger config --
  ReplanTriggerConfig rt_cfg;
  rt_cfg.odd_degraded_threshold = get_parameter("odd.degraded_threshold").as_double();
  rt_cfg.odd_critical_threshold = get_parameter("odd.critical_threshold").as_double();
  rt_cfg.odd_degraded_buffer_s = get_parameter("odd.degraded_buffer_s").as_double();
  rt_cfg.eta_infeasible_margin_s = get_parameter("eta_infeasible_margin_s").as_double();
  rt_cfg.attempt_max_count = static_cast<int32_t>(get_parameter("replan.attempt_max_count").as_int());
  rt_cfg.deadline_mrc_required_s = get_parameter("replan.deadline_mrc_required_s").as_double();
  rt_cfg.deadline_odd_exit_critical_s = get_parameter("replan.deadline_odd_exit_critical_s").as_double();
  rt_cfg.deadline_odd_exit_degraded_s = get_parameter("replan.deadline_odd_exit_degraded_s").as_double();
  rt_cfg.deadline_mission_infeasible_s = get_parameter("replan.deadline_mission_infeasible_s").as_double();
  rt_cfg.deadline_congestion_s = get_parameter("replan.deadline_congestion_s").as_double();
  replan_trigger_ = std::make_unique<ReplanRequestTrigger>(rt_cfg);

  // -- ReplanResponseHandler config --
  ReplanResponseHandlerConfig rh_cfg;
  rh_cfg.attempt_max_count = static_cast<int32_t>(get_parameter("replan.attempt_max_count").as_int());
  replan_handler_ = std::make_unique<ReplanResponseHandler>(rh_cfg);

  // -- MissionStateMachine config --
  MissionStateMachineConfig sm_cfg{};
  // distance_completion_m from params if available; use default 50.0
  declare_parameter("distance_completion_m", 50.0);
  sm_cfg.distance_completion_m = get_parameter("distance_completion_m").as_double();
  state_machine_ = std::make_unique<MissionStateMachine>(sm_cfg);
}

// ---------------------------------------------------------------------------
// Logger setup
// ---------------------------------------------------------------------------

void MissionManagerNode::setup_logger()
{
  try {
    logger_ = spdlog::get("mass_l3_m3");
    if (!logger_) {
      logger_ = spdlog::rotating_logger_mt(
          "mass_l3_m3",
          "/var/log/mass-l3/m3_mission_manager.log",
          10 * 1024 * 1024,  // 10 MB
          5);                // max 5 files
      logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
      logger_->set_level(spdlog::level::info);
    }
  } catch (const spdlog::spdlog_ex& ex) {
    RCLCPP_WARN(get_logger(), "spdlog init failed (non-fatal): %s", ex.what());
  }
}

// ---------------------------------------------------------------------------
// Publisher setup
// ---------------------------------------------------------------------------

void MissionManagerNode::setup_publishers()
{
  mission_goal_pub_ = create_publisher<l3_msgs::msg::MissionGoal>(
      "/l3/m3/mission_goal",
      rclcpp::QoS(5).reliable());

  replan_request_pub_ = create_publisher<l3_msgs::msg::RouteReplanRequest>(
      "/l3/m3/route_replan_request",
      rclcpp::QoS(50).reliable().transient_local());

  asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record",
      rclcpp::QoS(50).reliable().transient_local());
}

// ---------------------------------------------------------------------------
// Subscriber setup
// ---------------------------------------------------------------------------

void MissionManagerNode::setup_subscribers()
{
  voyage_task_sub_ = create_subscription<l3_external_msgs::msg::VoyageTask>(
      "/l1/voyage_task",
      rclcpp::QoS(50).reliable().transient_local(),
      [this](const l3_external_msgs::msg::VoyageTask::SharedPtr msg) {
        on_voyage_task(msg);
      });

  planned_route_sub_ = create_subscription<l3_external_msgs::msg::PlannedRoute>(
      "/l2/planned_route",
      rclcpp::QoS(5).reliable(),
      [this](const l3_external_msgs::msg::PlannedRoute::SharedPtr msg) {
        on_planned_route(msg);
      });

  speed_profile_sub_ = create_subscription<l3_external_msgs::msg::SpeedProfile>(
      "/l2/speed_profile",
      rclcpp::QoS(5).reliable(),
      [this](const l3_external_msgs::msg::SpeedProfile::SharedPtr msg) {
        on_speed_profile(msg);
      });

  replan_response_sub_ = create_subscription<l3_external_msgs::msg::ReplanResponse>(
      "/l2/replan_response",
      rclcpp::QoS(50).reliable().transient_local(),
      [this](const l3_external_msgs::msg::ReplanResponse::SharedPtr msg) {
        on_replan_response(msg);
      });

  odd_state_sub_ = create_subscription<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      [this](const l3_msgs::msg::ODDState::SharedPtr msg) {
        on_odd_state(msg);
      });

  world_state_sub_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state",
      rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state(msg);
      });
}

// ---------------------------------------------------------------------------
// Timer setup
// ---------------------------------------------------------------------------

void MissionManagerNode::setup_timers()
{
  const double goal_hz = get_parameter("mission_goal.publish_rate_hz").as_double();
  const auto goal_period = std::chrono::duration<double>(1.0 / goal_hz);
  mission_goal_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(goal_period),
      [this]() { publish_mission_goal(); });

  const double asdr_hz = get_parameter("asdr.heartbeat_rate_hz").as_double();
  const auto asdr_period = std::chrono::duration<double>(1.0 / asdr_hz);
  asdr_timer_ = create_wall_timer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(asdr_period),
      [this]() { publish_asdr_snapshot(); });

  replan_deadline_timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { check_replan_deadline(); });

  heartbeat_timer_ = create_wall_timer(
      std::chrono::seconds(1),
      [this]() { log_heartbeat(); });
}

// ---------------------------------------------------------------------------
// Subscriber callbacks
// ---------------------------------------------------------------------------

void MissionManagerNode::on_voyage_task(
    const l3_external_msgs::msg::VoyageTask::SharedPtr msg)
{
  RCLCPP_INFO(get_logger(), "VoyageTask received: id=%lu priority=%s",
              msg->task_id, msg->optimization_priority.c_str());

  // Use cached position for validation
  geographic_msgs::msg::GeoPoint pos = current_position_;
  if (last_world_state_) {
    pos.latitude = last_world_state_->own_ship.position.latitude;
    pos.longitude = last_world_state_->own_ship.position.longitude;
  }

  const auto result = validator_->validate(
      *msg, pos, now().nanoseconds());

  // Step 1: Idle → TaskValidation (unconditionally on receipt)
  {
    MissionEvent recv_event;
    recv_event.type = MissionEvent::Type::VoyageTaskReceived;
    state_machine_->handle_event(recv_event);
  }

  // Step 2: TaskValidation → AwaitingRoute (valid) or → Idle (invalid)
  if (result.is_valid) {
    RCLCPP_INFO(get_logger(), "VoyageTask validated OK — transitioning to AwaitingRoute");
    MissionEvent pass_event;
    pass_event.type = MissionEvent::Type::ValidationPassed;
    state_machine_->handle_event(pass_event);
    publish_asdr_record("voyage_task_accepted",
                        nlohmann::json{{"task_id", msg->task_id}});
    if (logger_) {
      logger_->info("VoyageTask accepted: id={}", msg->task_id);
    }
  } else {
    RCLCPP_WARN(get_logger(), "VoyageTask validation FAILED: %s",
                result.failed_check.c_str());
    MissionEvent fail_event;
    fail_event.type = MissionEvent::Type::ValidationFailed;
    state_machine_->handle_event(fail_event);
    publish_asdr_record("voyage_task_rejected",
                        nlohmann::json{{"reason", result.failed_check}});
    if (logger_) {
      logger_->warn("VoyageTask rejected: {}", result.failed_check);
    }
  }
}

void MissionManagerNode::on_planned_route(
    const l3_external_msgs::msg::PlannedRoute::SharedPtr msg)
{
  RCLCPP_DEBUG(get_logger(), "PlannedRoute received: id=%lu", msg->route_id);

  if (eta_projector_) {
    eta_projector_->update_route(*msg);
  }

  // If waiting for initial route, advance state machine
  if (state_machine_->current() == MissionState::AwaitingRoute) {
    MissionEvent event;
    event.type = MissionEvent::Type::RouteReceived;
    state_machine_->handle_event(event);
    RCLCPP_INFO(get_logger(), "Route received — mission now ACTIVE");
    if (logger_) {
      logger_->info("Route received, state->ACTIVE, route_id={}", msg->route_id);
    }
  }
}

void MissionManagerNode::on_speed_profile(
    const l3_external_msgs::msg::SpeedProfile::SharedPtr msg)
{
  RCLCPP_DEBUG(get_logger(), "SpeedProfile received: id=%lu", msg->profile_id);

  if (eta_projector_) {
    eta_projector_->update_speed_profile(*msg);
  }
}

void MissionManagerNode::on_replan_response(
    const l3_external_msgs::msg::ReplanResponse::SharedPtr msg)
{
  RCLCPP_INFO(get_logger(), "ReplanResponse received: status=%u",
              static_cast<unsigned>(msg->status));

  const auto outcome = replan_handler_->handle_response(*msg, replan_attempt_count_);

  MissionEvent event;
  event.type = MissionEvent::Type::ReplanResponseReceived;
  event.replan_outcome = outcome;
  state_machine_->handle_event(event);

  if (outcome.success) {
    replan_attempt_count_ = 0;
    replan_deadline_.reset();
    replan_trigger_->reset_degraded_timer();
    RCLCPP_INFO(get_logger(), "Replan succeeded — back to ACTIVE");
    publish_asdr_record("replan_success", nlohmann::json::object());
    if (logger_) {
      logger_->info("Replan succeeded, resuming ACTIVE");
    }
  } else if (outcome.escalate_to_mrc) {
    replan_deadline_.reset();
    RCLCPP_WARN(get_logger(), "Replan failed — escalating to MRC");
    publish_asdr_record("replan_escalate_mrc",
                        nlohmann::json{{"rationale", outcome.rationale}});
    if (logger_) {
      logger_->warn("Replan escalation to MRC: {}", outcome.rationale);
    }
  } else {
    RCLCPP_WARN(get_logger(), "Replan failed (no escalation): %s",
                outcome.rationale.c_str());
    publish_asdr_record("replan_failed",
                        nlohmann::json{{"rationale", outcome.rationale}});
    if (logger_) {
      logger_->warn("Replan failed: {}", outcome.rationale);
    }
  }
}

void MissionManagerNode::on_odd_state(
    const l3_msgs::msg::ODDState::SharedPtr msg)
{
  last_odd_state_ = msg;

  // Only act when there is an active mission
  if (!state_machine_->has_active_mission()) {
    return;
  }

  // Compute current ETA if possible
  double current_eta_s = 0.0;
  if (eta_projector_ && last_world_state_) {
    const auto proj = eta_projector_->project(
        *last_world_state_, std::chrono::steady_clock::now());
    if (proj.has_value()) {
      current_eta_s = proj->eta_s;
    }
  }

  // planned_eta_s is not available at node level in v1.1.2; pass 0 = unknown
  check_and_trigger_replan(*msg, current_eta_s, 0.0);
}

void MissionManagerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr msg)
{
  last_world_state_ = msg;

  // Cache current position for validation use
  current_position_.latitude = msg->own_ship.position.latitude;
  current_position_.longitude = msg->own_ship.position.longitude;
}

// ---------------------------------------------------------------------------
// Timer callbacks
// ---------------------------------------------------------------------------

void MissionManagerNode::publish_mission_goal()
{
  if (state_machine_->current() != MissionState::Active) {
    return;
  }

  auto msg = l3_msgs::msg::MissionGoal();
  msg.stamp = now();

  if (eta_projector_ && last_world_state_) {
    const auto proj = eta_projector_->project(
        *last_world_state_, std::chrono::steady_clock::now());
    if (proj.has_value()) {
      msg.eta_to_target_s = static_cast<float>(proj->eta_s);
    }
  }

  msg.confidence = 1.0F;
  msg.rationale = "periodic mission goal update";
  mission_goal_pub_->publish(std::move(msg));
}

void MissionManagerNode::publish_asdr_snapshot()
{
  auto msg = l3_msgs::msg::ASDRRecord();
  msg.stamp = now();
  msg.source_module = "M3_Mission_Manager";
  msg.decision_type = "heartbeat";

  const nlohmann::json j = {
      {"mission_state", std::string(state_machine_->state_name())},
      {"replan_attempts", replan_attempt_count_}};
  msg.decision_json = j.dump();
  asdr_pub_->publish(std::move(msg));
}

void MissionManagerNode::check_replan_deadline()
{
  if (state_machine_->current() != MissionState::ReplanWait) {
    return;
  }

  if (!replan_deadline_.has_value()) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now >= replan_deadline_.value()) {
    RCLCPP_WARN(get_logger(), "Replan deadline expired");
    MissionEvent event;
    event.type = MissionEvent::Type::ReplanDeadlineExpired;
    state_machine_->handle_event(event);
    publish_asdr_record("replan_deadline_expired",
                        nlohmann::json{{"attempts", replan_attempt_count_}});
    replan_deadline_.reset();
    if (logger_) {
      logger_->warn("Replan deadline expired, attempts={}", replan_attempt_count_);
    }
  }
}

void MissionManagerNode::log_heartbeat()
{
  RCLCPP_DEBUG(get_logger(), "heartbeat — state: %s, replan_attempts: %d",
               std::string(state_machine_->state_name()).c_str(),
               replan_attempt_count_);
  if (logger_) {
    logger_->info("heartbeat — state: {}, replan_attempts: {}",
                  state_machine_->state_name(), replan_attempt_count_);
  }
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void MissionManagerNode::publish_replan_request(
    ReplanReason reason, double deadline_s,
    const geographic_msgs::msg::GeoPoint& current_pos)
{
  auto msg = l3_msgs::msg::RouteReplanRequest();
  msg.stamp = now();

  // Map ReplanReason to RouteReplanRequest reason constant
  switch (reason) {
    case ReplanReason::OddExit:
      msg.reason = l3_msgs::msg::RouteReplanRequest::REASON_ODD_EXIT;
      break;
    case ReplanReason::MissionInfeasible:
      msg.reason = l3_msgs::msg::RouteReplanRequest::REASON_MISSION_INFEASIBLE;
      break;
    case ReplanReason::MrcRequired:
      msg.reason = l3_msgs::msg::RouteReplanRequest::REASON_MRC_REQUIRED;
      break;
    case ReplanReason::Congestion:
      msg.reason = l3_msgs::msg::RouteReplanRequest::REASON_CONGESTION;
      break;
    default:
      msg.reason = l3_msgs::msg::RouteReplanRequest::REASON_MISSION_INFEASIBLE;
      break;
  }

  msg.deadline_s = static_cast<float>(deadline_s);
  msg.context_summary = "triggered by M3 ReplanRequestTrigger";
  msg.current_position = current_pos;
  msg.confidence = 1.0F;
  msg.rationale = "replan requested by ReplanRequestTrigger";
  replan_request_pub_->publish(std::move(msg));
}

void MissionManagerNode::publish_asdr_record(const std::string& type,
                                              const nlohmann::json& payload)
{
  auto msg = l3_msgs::msg::ASDRRecord();
  msg.stamp = now();
  msg.source_module = "M3_Mission_Manager";
  msg.decision_type = type;
  msg.decision_json = payload.dump();
  asdr_pub_->publish(std::move(msg));
}

void MissionManagerNode::check_and_trigger_replan(
    const l3_msgs::msg::ODDState& odd,
    double current_eta_s,
    double planned_eta_s)
{
  const auto now = std::chrono::steady_clock::now();
  const auto decision = replan_trigger_->evaluate(
      odd, current_eta_s, planned_eta_s, replan_attempt_count_, now);

  if (!decision.should_trigger) {
    return;
  }

  RCLCPP_WARN(get_logger(), "Replan triggered: reason=%s deadline=%.1fs",
              decision.rationale.c_str(), decision.deadline_s);

  publish_replan_request(decision.reason, decision.deadline_s, current_position_);

  MissionEvent event;
  event.type = MissionEvent::Type::ReplanTriggered;
  state_machine_->handle_event(event);

  replan_attempt_count_++;
  replan_deadline_ = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
      std::chrono::duration<double>(decision.deadline_s));

  publish_asdr_record("replan_triggered",
      nlohmann::json{{"reason", decision.rationale},
                     {"deadline_s", decision.deadline_s}});

  if (logger_) {
    logger_->warn("Replan triggered: {}, deadline={}s",
                  decision.rationale, decision.deadline_s);
  }
}

}  // namespace mass_l3::m3
