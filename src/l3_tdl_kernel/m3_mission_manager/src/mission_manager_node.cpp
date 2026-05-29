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
namespace {

constexpr double kEarthRadiusNm = 3440.06479;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;

double haversineNm(double lat1_deg, double lon1_deg,
                   double lat2_deg, double lon2_deg) noexcept {
  const double lat1 = lat1_deg * kDegToRad;
  const double lon1 = lon1_deg * kDegToRad;
  const double lat2 = lat2_deg * kDegToRad;
  const double lon2 = lon2_deg * kDegToRad;

  const double dlat = lat2 - lat1;
  const double dlon = lon2 - lon1;
  const double sin_dlat = std::sin(dlat * 0.5);
  const double sin_dlon = std::sin(dlon * 0.5);
  const double a = sin_dlat * sin_dlat +
                   std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusNm * c;
}

} // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

MissionManagerNode::MissionManagerNode(const rclcpp::NodeOptions& options)
    : Node("m3_mission_manager", options)
{
  declare_parameter("l1_watchdog.bypass", true);
  declare_parameter("replan.cooldown_s", 10.0);

  declare_parameters();
  create_components();
  setup_logger();
  setup_publishers();
  setup_subscribers();
  setup_timers();

  l1_watchdog_bypass_ = get_parameter("l1_watchdog.bypass").as_bool();
  replan_cooldown_s_ = get_parameter("replan.cooldown_s").as_double();
  // Initialize last_replan_time_ to a point in the far past (e.g. 1 hour ago) to allow immediate replanning on first call
  last_replan_time_ = std::chrono::steady_clock::now() - std::chrono::hours(1);

  // Transition state machine from Init to Idle now that all subscribers/timers
  // are ready. Per spec §3.5: Init→Idle on "节点初始化完成、subscribers 就绪".
  {
    MissionEvent ready_event;
    ready_event.type = MissionEvent::Type::NodeReady;
    const auto prev = state_machine_->current();
    const auto prev_name = state_machine_->state_name();
    state_machine_->handle_event(ready_event);
    if (state_machine_->current() != prev) {
      RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (NodeReady)",
                  std::string(prev_name).c_str(),
                  std::string(state_machine_->state_name()).c_str());
    }
  }

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

  // CurrentErrorMonitor — D2.3
  declare_parameter("current_error.xte_high_nm",       0.5);
  declare_parameter("current_error.xte_medium_nm",     0.3);
  declare_parameter("current_error.current_high_kn",   2.0);
  declare_parameter("current_error.current_medium_kn", 1.5);
  declare_parameter("current_error.l4_stale_s",        2.0);

  // L1WatchdogMonitor — D2.3
  declare_parameter("l1_watchdog.warning_s",           60.0);
  declare_parameter("l1_watchdog.timeout_s",           120.0);
  declare_parameter("l1_watchdog.confidence_warning",  0.6);
  declare_parameter("l1_watchdog.confidence_timeout",  0.4);

  // Task Validity Safety Timeouts
  declare_parameter("task_validity.l1_timeout_s", 30.0);
  declare_parameter("task_validity.l2_timeout_s", 5.0);
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

  // -- CurrentErrorMonitor — D2.3 --
  CurrentErrorMonitorConfig cem_cfg;
  cem_cfg.xte_high_nm       = static_cast<float>(
      get_parameter("current_error.xte_high_nm").as_double());       // [HAZID 校准]
  cem_cfg.xte_medium_nm     = static_cast<float>(
      get_parameter("current_error.xte_medium_nm").as_double());     // [HAZID 校准]
  cem_cfg.current_high_kn   = static_cast<float>(
      get_parameter("current_error.current_high_kn").as_double());   // [HAZID 校准]
  cem_cfg.current_medium_kn = static_cast<float>(
      get_parameter("current_error.current_medium_kn").as_double()); // [HAZID 校准]
  cem_cfg.l4_stale_s        = get_parameter("current_error.l4_stale_s").as_double();
  current_error_monitor_ = std::make_unique<CurrentErrorMonitor>(cem_cfg);

  // -- L1WatchdogMonitor — D2.3 --
  L1WatchdogConfig wd_cfg;
  wd_cfg.warning_s          = get_parameter("l1_watchdog.warning_s").as_double();   // [HAZID 校准]
  wd_cfg.timeout_s          = get_parameter("l1_watchdog.timeout_s").as_double();   // [HAZID 校准]
  wd_cfg.confidence_warning = static_cast<float>(
      get_parameter("l1_watchdog.confidence_warning").as_double());
  wd_cfg.confidence_timeout = static_cast<float>(
      get_parameter("l1_watchdog.confidence_timeout").as_double());
  l1_watchdog_ = std::make_unique<L1WatchdogMonitor>(wd_cfg);

  // Retrieve task validity safety timeouts
  l1_timeout_s_ = get_parameter("task_validity.l1_timeout_s").as_double();
  l2_timeout_s_ = get_parameter("task_validity.l2_timeout_s").as_double();
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

  tor_pub_ = create_publisher<l3_msgs::msg::ToRRequest>(
      "/l3/m3/tor_request",
      rclcpp::QoS(10).reliable().transient_local());
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

  tracking_error_sub_ = create_subscription<l3_external_msgs::msg::TrackingError>(
      "/l4/tracking_error",
      rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_external_msgs::msg::TrackingError::SharedPtr msg) {
        on_tracking_error(msg);
      });
}

// ---------------------------------------------------------------------------
// Timer setup
// ---------------------------------------------------------------------------

void MissionManagerNode::setup_timers()
{
  const double goal_hz = get_parameter("mission_goal.publish_rate_hz").as_double();
  const auto goal_period = std::chrono::duration<double>(1.0 / goal_hz);
  mission_goal_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(goal_period),
      [this]() { publish_mission_goal(); });

  const double asdr_hz = get_parameter("asdr.heartbeat_rate_hz").as_double();
  const auto asdr_period = std::chrono::duration<double>(1.0 / asdr_hz);
  asdr_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration_cast<std::chrono::nanoseconds>(asdr_period),
      [this]() { publish_asdr_snapshot(); });

  replan_deadline_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { check_replan_deadline(); });

  heartbeat_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { log_heartbeat(); });

  l1_watchdog_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::seconds(1),
      [this]() { evaluate_l1_watchdog(); });
}

// ---------------------------------------------------------------------------
// Subscriber callbacks
// ---------------------------------------------------------------------------

void MissionManagerNode::on_voyage_task(
    const l3_external_msgs::msg::VoyageTask::SharedPtr msg)
{
  last_voyage_task_time_ = std::chrono::steady_clock::now();
  // Notify watchdog on any VoyageTask arrival (valid or invalid) — spec §4.2
  l1_watchdog_->notify_voyage_task_received(std::chrono::steady_clock::now());

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
    const auto prev_name = state_machine_->state_name();
    const auto prev = state_machine_->current();
    state_machine_->handle_event(recv_event);
    if (state_machine_->current() != prev) {
      RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (VoyageTaskReceived)",
                  std::string(prev_name).c_str(),
                  std::string(state_machine_->state_name()).c_str());
    }
  }

  // Step 2: TaskValidation → AwaitingRoute (valid) or → Idle (invalid)
  if (result.is_valid) {
    MissionEvent pass_event;
    pass_event.type = MissionEvent::Type::ValidationPassed;
    const auto prev_name = state_machine_->state_name();
    const auto prev = state_machine_->current();
    state_machine_->handle_event(pass_event);
    if (state_machine_->current() != prev) {
      RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (ValidationPassed)",
                  std::string(prev_name).c_str(),
                  std::string(state_machine_->state_name()).c_str());
    }
    RCLCPP_INFO(get_logger(), "VoyageTask validated OK — transitioning to AwaitingRoute");
    publish_asdr_record("voyage_task_accepted",
                        nlohmann::json{{"task_id", msg->task_id}});
    if (logger_) {
      logger_->info("VoyageTask accepted: id={}", msg->task_id);
    }

    // Immediate publish: new valid VoyageTask — spec §4.4
    if (state_machine_->current() == MissionState::Active) {
      publish_mission_goal();
    }
  } else {
    RCLCPP_WARN(get_logger(), "VoyageTask validation FAILED: %s",
                result.failed_check.c_str());
    MissionEvent fail_event;
    fail_event.type = MissionEvent::Type::ValidationFailed;
    const auto prev_name_f = state_machine_->state_name();
    const auto prev_f = state_machine_->current();
    state_machine_->handle_event(fail_event);
    if (state_machine_->current() != prev_f) {
      RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (ValidationFailed)",
                  std::string(prev_name_f).c_str(),
                  std::string(state_machine_->state_name()).c_str());
    }
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
  last_planned_route_time_ = std::chrono::steady_clock::now();
  last_planned_route_ = msg;
  current_wp_index_ = 0u;

  if (eta_projector_) {
    eta_projector_->update_route(*msg);
  }

  // If waiting for initial route, advance state machine
  if (state_machine_->current() == MissionState::AwaitingRoute) {
    const auto prev_name = state_machine_->state_name();
    MissionEvent event;
    event.type = MissionEvent::Type::RouteReceived;
    state_machine_->handle_event(event);
    RCLCPP_INFO(get_logger(), "[M3 FSM] %s → %s (RouteReceived, route_id=%lu)",
                std::string(prev_name).c_str(),
                std::string(state_machine_->state_name()).c_str(),
                msg->route_id);
    RCLCPP_INFO(get_logger(), "Route received — mission now ACTIVE");
    if (logger_) {
      logger_->info("[M3 FSM] {} → {} (RouteReceived, route_id={})",
                    prev_name, state_machine_->state_name(), msg->route_id);
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

  if (!state_machine_->has_active_mission()) {
    return;
  }

  const bool zone_changed = (msg->current_zone != last_odd_zone_);
  last_odd_zone_ = msg->current_zone;

  double current_eta_s = 0.0;
  if (eta_projector_ && last_world_state_) {
    const auto proj = eta_projector_->project(
        *last_world_state_, std::chrono::steady_clock::now());
    if (proj.has_value()) {
      current_eta_s = proj->eta_s;
    }
  }

  check_and_trigger_replan(*msg, current_eta_s, 0.0);

  if (zone_changed) {
    publish_mission_goal();  // immediate trigger: ODD zone change
  }
}

void MissionManagerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr msg)
{
  last_world_state_ = msg;
  current_position_.latitude  = msg->own_ship.position.latitude;
  current_position_.longitude = msg->own_ship.position.longitude;

  // Waypoint progression logic: advance to the next waypoint when within distance threshold
  if (last_planned_route_ && !last_planned_route_->route.poses.empty()) {
    const double threshold_m = state_machine_ ? state_machine_->distance_completion_m() : 50.0;
    while (current_wp_index_ < last_planned_route_->route.poses.size()) {
      const auto& target_pose = last_planned_route_->route.poses[current_wp_index_];
      const double dist_nm = haversineNm(
          current_position_.latitude, current_position_.longitude,
          target_pose.pose.position.latitude, target_pose.pose.position.longitude);
      const double dist_m = dist_nm * 1852.0;

      if (dist_m < threshold_m) {
        if (current_wp_index_ + 1 < last_planned_route_->route.poses.size()) {
          ++current_wp_index_;
          RCLCPP_INFO(get_logger(), "[M3] Advanced to waypoint index %zu (total %zu)",
                      current_wp_index_, last_planned_route_->route.poses.size());
        } else {
          // Reached the final waypoint, stop advancing
          break;
        }
      } else {
        break;
      }
    }
  }

  const auto now = std::chrono::steady_clock::now();
  current_error_monitor_->update_world_state(*msg, now);
  check_current_error_severity_change(now);

  if (state_machine_->current() == MissionState::Active) {
    bool has_l1_task = last_voyage_task_time_.has_value() &&
                       (now - last_voyage_task_time_.value()) < std::chrono::duration<double>(l1_timeout_s_);
    bool has_l2_route = last_planned_route_time_.has_value() &&
                        (now - last_planned_route_time_.value()) < std::chrono::duration<double>(l2_timeout_s_);
    bool has_enc_check = true;
    bool autonomy_ok = last_odd_state_ && last_odd_state_->current_zone != l3_msgs::msg::ODDState::ODD_ZONE_D;

    const auto prev_validity = state_machine_->task_validity();
    if (state_machine_->update_task_validity(has_l1_task, has_l2_route, has_enc_check, autonomy_ok)) {
      const auto curr_validity = state_machine_->task_validity();
      const char* prev_str = task_validity_to_str(prev_validity);
      const char* curr_str = task_validity_to_str(curr_validity);
      RCLCPP_INFO(get_logger(), "[M3 TaskValidity] Substate changed: %s → %s (L1=%d, L2=%d, ENC=%d, AutonomyOk=%d)",
                  prev_str, curr_str, has_l1_task, has_l2_route, has_enc_check, autonomy_ok);
      if (logger_) {
        logger_->info("[M3 TaskValidity] Substate changed: {} → {} (L1={}, L2={}, ENC={}, AutonomyOk={})",
                      prev_str, curr_str, has_l1_task, has_l2_route, has_enc_check, autonomy_ok);
      }
      publish_mission_goal(); // Immediate publish on validity substate change
    }
  }
}

void MissionManagerNode::on_tracking_error(
    const l3_external_msgs::msg::TrackingError::SharedPtr msg)
{
  const auto now = std::chrono::steady_clock::now();
  current_error_monitor_->update_tracking_error(*msg, now);
  check_current_error_severity_change(now);
}

void MissionManagerNode::evaluate_l1_watchdog()
{
  if (l1_watchdog_bypass_) {
    if (last_l1_watchdog_status_ != L1WatchdogStatus::OK) {
      last_l1_watchdog_status_ = L1WatchdogStatus::OK;
      publish_mission_goal();
    }
    return;
  }

  if (state_machine_->current() != MissionState::Active) {
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  const auto result = l1_watchdog_->evaluate(now);

  if (result.status == last_l1_watchdog_status_) {
    return;  // no state change
  }

  const L1WatchdogStatus prev = last_l1_watchdog_status_;
  last_l1_watchdog_status_ = result.status;

  if (result.status == L1WatchdogStatus::WARNING) {
    publish_asdr_record("l1_dropout_warning",
        nlohmann::json{{"elapsed_s", result.elapsed_s}});
    if (logger_) { logger_->warn("L1 watchdog WARNING: elapsed={:.1f}s", result.elapsed_s); }
  } else if (result.status == L1WatchdogStatus::TIMEOUT) {
    publish_asdr_record("l1_dropout_timeout",
        nlohmann::json{{"elapsed_s", result.elapsed_s}});
    publish_tor_request(l3_msgs::msg::ToRRequest::REASON_MANUAL_REQUEST, 60.0F);
    if (logger_) { logger_->error("L1 watchdog TIMEOUT: ToR requested, elapsed={:.1f}s", result.elapsed_s); }
  } else if (prev != L1WatchdogStatus::OK) {
    publish_asdr_record("l1_recovered", nlohmann::json{});
    if (logger_) { logger_->info("L1 watchdog recovered"); }
  }

  publish_mission_goal();  // immediate trigger: watchdog state change
}

void MissionManagerNode::check_current_error_severity_change(
    std::chrono::steady_clock::time_point now)
{
  if (state_machine_->current() != MissionState::Active) {
    return;
  }

  const CurrentErrorReading reading = current_error_monitor_->evaluate(now);
  if (reading.severity == last_current_error_severity_) {
    return;
  }

  const CurrentErrorSeverity prev = last_current_error_severity_;
  last_current_error_severity_ = reading.severity;

  if (reading.severity == CurrentErrorSeverity::HIGH) {
    publish_asdr_record("current_error_high_alert",
        nlohmann::json{{"xte_nm", reading.xte_nm},
                       {"sea_current_kn", reading.sea_current_kn}});
    RCLCPP_WARN(get_logger(), "Current error HIGH: xte=%.2f nm cur=%.2f kn",
                static_cast<double>(reading.xte_nm),
                static_cast<double>(reading.sea_current_kn));
  } else if (prev == CurrentErrorSeverity::HIGH) {
    publish_asdr_record("current_error_resolved",
        nlohmann::json{{"severity", static_cast<int>(reading.severity)}});
    RCLCPP_INFO(get_logger(), "Current error resolved to %s",
                (reading.severity == CurrentErrorSeverity::MEDIUM) ? "MEDIUM" : "NORMAL");
  }

  publish_mission_goal();  // immediate trigger: severity changed
}

// ---------------------------------------------------------------------------
// Timer callbacks
// ---------------------------------------------------------------------------

void MissionManagerNode::publish_mission_goal()
{
  const auto current_state = state_machine_->current();
  const uint8_t task_validity_val = map_task_validity(state_machine_->task_validity());

  if (current_state != MissionState::Active) {
    auto msg = l3_msgs::msg::MissionGoal();
    msg.stamp          = now();
    msg.schema_version = 121U;  // IDL v1.2.1 [W3 BUMP]
    msg.eta_to_target_s = -1.0F;
    msg.confidence      = 0.0F;
    msg.current_error_severity = 0U;
    msg.xte_nm                 = 0.0F;
    msg.sea_current_kn         = 0.0F;
    msg.l1_watchdog_status     = 0U;

    // Map FSM state
    uint8_t fsm_state_val = l3_msgs::msg::MissionGoal::FSM_INIT;
    switch (current_state) {
      case MissionState::Init:           fsm_state_val = l3_msgs::msg::MissionGoal::FSM_INIT; break;
      case MissionState::Idle:           fsm_state_val = l3_msgs::msg::MissionGoal::FSM_IDLE; break;
      case MissionState::TaskValidation: fsm_state_val = l3_msgs::msg::MissionGoal::FSM_TASK_VALIDATION; break;
      case MissionState::AwaitingRoute:  fsm_state_val = l3_msgs::msg::MissionGoal::FSM_AWAITING_ROUTE; break;
      case MissionState::ReplanWait:     fsm_state_val = l3_msgs::msg::MissionGoal::FSM_REPLAN_WAIT; break;
      default:                           fsm_state_val = l3_msgs::msg::MissionGoal::FSM_INIT; break;
    }
    msg.fsm_state = fsm_state_val;
    msg.task_validity = task_validity_val;

    msg.rationale = "[M3] Standby (" + std::string(state_machine_->state_name()) + ")";
    mission_goal_pub_->publish(std::move(msg));
    return;
  }

  const auto now_tp = std::chrono::steady_clock::now();
  auto msg = l3_msgs::msg::MissionGoal();
  msg.stamp          = now();
  msg.schema_version = 121U;  // IDL v1.2.1 [W3 BUMP]

  // Populate current_target_wp from cached route and waypoint index
  if (last_planned_route_ && !last_planned_route_->route.poses.empty()) {
    if (current_wp_index_ < last_planned_route_->route.poses.size()) {
      const auto& target_pose = last_planned_route_->route.poses[current_wp_index_];
      msg.current_target_wp.latitude  = target_pose.pose.position.latitude;
      msg.current_target_wp.longitude = target_pose.pose.position.longitude;
      msg.current_target_wp.altitude  = 0.0;
    }
  }

  // Map FSM state
  msg.fsm_state = l3_msgs::msg::MissionGoal::FSM_ACTIVE;
  msg.task_validity = task_validity_val;

  // ETA projection
  float eta_s = -1.0F;
  const bool route_fresh =
      last_planned_route_time_.has_value() &&
      (now_tp - last_planned_route_time_.value()) <= std::chrono::seconds(3);

  if (route_fresh && eta_projector_ && last_world_state_) {
    const auto proj = eta_projector_->project(*last_world_state_, now_tp);
    if (proj.has_value()) {
      eta_s = static_cast<float>(proj->eta_s);
    }
  }
  msg.eta_to_target_s = eta_s;

  // Confidence: watchdog_factor × current_error_factor — spec §4.1
  const L1WatchdogResult wd      = l1_watchdog_->evaluate(now_tp);
  const CurrentErrorReading cerd = current_error_monitor_->evaluate(now_tp);
  float confidence = wd.confidence_factor;
  if (cerd.severity == CurrentErrorSeverity::HIGH && confidence > 0.4F) {
    confidence = std::max(confidence * 0.85F, 0.4F);
  }
  msg.confidence = confidence;

  // New D2.3 fields
  msg.current_error_severity = static_cast<uint8_t>(cerd.severity);
  msg.xte_nm                 = cerd.xte_nm;
  msg.sea_current_kn         = cerd.sea_current_kn;
  msg.l1_watchdog_status     = static_cast<uint8_t>(wd.status);

  // SAT-2 rationale — spec §4.4
  const char* odd_zone_str = "?";
  if (last_odd_state_) {
    switch (last_odd_state_->current_zone) {
      case l3_msgs::msg::ODDState::ODD_ZONE_A: odd_zone_str = "A"; break;
      case l3_msgs::msg::ODDState::ODD_ZONE_B: odd_zone_str = "B"; break;
      case l3_msgs::msg::ODDState::ODD_ZONE_C: odd_zone_str = "C"; break;
      case l3_msgs::msg::ODDState::ODD_ZONE_D: odd_zone_str = "D"; break;
      default: break;
    }
  }
  const char* l1_str =
      (wd.status == L1WatchdogStatus::OK)      ? "OK"      :
      (wd.status == L1WatchdogStatus::WARNING)  ? "WARN"    : "TIMEOUT";
  
  const char* val_str =
      (state_machine_->task_validity() == TaskValidity::Pending)    ? "PEND" :
      (state_machine_->task_validity() == TaskValidity::Valid)      ? "VAL"  :
      (state_machine_->task_validity() == TaskValidity::Invalid)    ? "INVAL" : "REPLAN";

  msg.rationale = "[M3] eta=" + std::to_string(static_cast<int>(eta_s)) + "s" +
                  " conf=" + std::to_string(confidence).substr(0, 4) +
                  " xte=" + std::to_string(cerd.xte_nm).substr(0, 4) + "nm" +
                  " cur=" + std::to_string(cerd.sea_current_kn).substr(0, 4) + "kn" +
                  " l1=" + l1_str + " odd=" + odd_zone_str + " val=" + val_str;

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
  msg.schema_version = 120U;  // IDL v1.2.0

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

void MissionManagerNode::publish_tor_request(uint8_t reason, float deadline_s)
{
  auto msg = l3_msgs::msg::ToRRequest();
  msg.stamp        = now();
  msg.reason       = reason;
  msg.deadline_s   = deadline_s;
  msg.target_level = l3_msgs::msg::ToRRequest::TARGET_LEVEL_D2;
  msg.confidence   = 1.0F;
  msg.rationale    = "[M3] L1 watchdog TIMEOUT — operator takeover required";
  msg.context_summary   = "L1 VoyageTask link lost for > 120s";
  msg.recommended_action = "Assume manual control (D2)";
  tor_pub_->publish(std::move(msg));
}

void MissionManagerNode::check_and_trigger_replan(
    const l3_msgs::msg::ODDState& odd,
    double current_eta_s,
    double planned_eta_s)
{
  const auto now = std::chrono::steady_clock::now();

  // Apply cooldown suppression to avoid high-frequency replanning loops
  const auto elapsed_cooldown = std::chrono::duration_cast<std::chrono::duration<double>>(
      now - last_replan_time_).count();
  if (elapsed_cooldown < replan_cooldown_s_) {
    RCLCPP_INFO(get_logger(), "[M3] Replan request suppressed due to cooling-down (%.1fs < %.1fs)",
                elapsed_cooldown, replan_cooldown_s_);
    return;
  }

  const auto decision = replan_trigger_->evaluate(
      odd, current_eta_s, planned_eta_s, replan_attempt_count_, now);

  if (!decision.should_trigger) {
    return;
  }

  last_replan_time_ = now; // update last replan timestamp

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

uint8_t MissionManagerNode::map_task_validity(TaskValidity val) const {
  switch (val) {
    case TaskValidity::Pending:    return l3_msgs::msg::MissionGoal::TASK_VALIDITY_PENDING;
    case TaskValidity::Valid:      return l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID;
    case TaskValidity::Invalid:    return l3_msgs::msg::MissionGoal::TASK_VALIDITY_INVALID;
    case TaskValidity::Replanning: return l3_msgs::msg::MissionGoal::TASK_VALIDITY_REPLANNING;
  }
  return l3_msgs::msg::MissionGoal::TASK_VALIDITY_PENDING;
}

const char* MissionManagerNode::task_validity_to_str(TaskValidity val) noexcept {
  switch (val) {
    case TaskValidity::Pending:    return "PENDING";
    case TaskValidity::Valid:      return "VALID";
    case TaskValidity::Invalid:    return "INVALID";
    case TaskValidity::Replanning: return "REPLANNING";
  }
  return "UNKNOWN";
}

}  // namespace mass_l3::m3
