// src/hmi_transparency_bridge_node.cpp
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

#include <string>

namespace mass_l3::m8 {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

HmiTransparencyBridgeNode::HmiTransparencyBridgeNode(const rclcpp::NodeOptions& options)
: rclcpp::Node("m8_hmi_transparency_bridge", options)
{
  load_parameters();

  sat_aggregator_ = std::make_unique<SatAggregator>();
  adaptive_trigger_ = std::make_unique<AdaptiveSatTrigger>(
      AdaptiveSatTrigger::Thresholds{
          sat3_priority_high_tdl_s_,
          sat2_system_confidence_threshold_,
          0.7F, 0.8F});
  tor_protocol_ = std::make_unique<TorProtocol>(
      TorProtocol::Config{tor_deadline_s_, sat1_min_display_s_, 30.0, 1});
  ui_builder_     = std::make_unique<UiStateBuilder>();
  tor_generator_  = std::make_unique<ToRRequestGenerator>();
  asdr_logger_    = std::make_unique<AsdrLogger>();
  health_monitor_ = std::make_unique<ModuleHealthMonitor>(ModuleHealthMonitor::Thresholds{});

  init_subscriptions();
  init_publishers();
  init_timers();

  RCLCPP_INFO(get_logger(), "M8 HmiTransparencyBridgeNode initialized");
}

// ---------------------------------------------------------------------------
// load_parameters
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::load_parameters()
{
  tor_deadline_s_                   = declare_parameter("tor_deadline_s", 60.0);
  sat1_min_display_s_               = declare_parameter("tor_sat1_min_display_s", 5.0);
  sat3_priority_high_tdl_s_         = declare_parameter("sat3_priority_high_tdl_s", 30.0);
  sat2_system_confidence_threshold_ =
      declare_parameter("sat2_system_confidence_drop_threshold", 0.6);
}

// ---------------------------------------------------------------------------
// init_subscriptions
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::init_subscriptions()
{
  sub_sat_ = create_subscription<l3_msgs::msg::SATData>(
      "/l3/sat/data", rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_msgs::msg::SATData::SharedPtr m) { on_sat_data(m); });

  sub_odd_ = create_subscription<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state", rclcpp::QoS(10).reliable().transient_local(),
      [this](const l3_msgs::msg::ODDState::SharedPtr m) { on_odd_state(m); });

  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_msgs::msg::WorldState::SharedPtr m) { on_world_state(m); });

  sub_mission_ = create_subscription<l3_msgs::msg::MissionGoal>(
      "/l3/m3/mission_goal", rclcpp::QoS(5).reliable(),
      [this](const l3_msgs::msg::MissionGoal::SharedPtr m) { on_mission_goal(m); });

  sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", rclcpp::QoS(5).reliable(),
      [this](const l3_msgs::msg::BehaviorPlan::SharedPtr m) { on_behavior_plan(m); });

  sub_avoid_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", rclcpp::QoS(5).reliable(),
      [this](const l3_msgs::msg::AvoidancePlan::SharedPtr m) { on_avoidance_plan(m); });

  sub_colreg_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", rclcpp::QoS(5).reliable(),
      [this](const l3_msgs::msg::COLREGsConstraint::SharedPtr m) {
        on_colreg_constraint(m); });

  sub_alert_ = create_subscription<l3_msgs::msg::SafetyAlert>(
      "/l3/m7/safety_alert", rclcpp::QoS(50).reliable().transient_local(),
      [this](const l3_msgs::msg::SafetyAlert::SharedPtr m) { on_safety_alert(m); });

  sub_override_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
      "/override/active_signal", rclcpp::QoS(50).reliable().transient_local(),
      [this](const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr m) {
        on_override_signal(m); });
}

// ---------------------------------------------------------------------------
// init_publishers
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::init_publishers()
{
  pub_ui_state_ = create_publisher<l3_msgs::msg::UIState>(
      "/l3/m8/ui_state", rclcpp::SensorDataQoS().keep_last(1));
  pub_tor_ = create_publisher<l3_msgs::msg::ToRRequest>(
      "/l3/m8/tor_request", rclcpp::QoS(50).reliable().transient_local());
  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record", rclcpp::QoS(50).reliable().transient_local());
}

// ---------------------------------------------------------------------------
// init_timers
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::init_timers()
{
  using namespace std::chrono_literals;
  timer_ui_            = create_wall_timer(20ms,   [this] { on_ui_publish_tick(); });
  timer_tor_           = create_wall_timer(500ms,  [this] { on_tor_tick(); });
  timer_health_        = create_wall_timer(1000ms, [this] { on_health_check_tick(); });
  timer_asdr_snapshot_ = create_wall_timer(500ms,  [this] { on_asdr_snapshot_tick(); });
}

// ---------------------------------------------------------------------------
// Subscription callbacks
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_sat_data(const l3_msgs::msg::SATData::SharedPtr msg)
{
  auto now = SatAggregator::Clock::now();
  // SatAggregator and ModuleHealthMonitor are internally thread-safe; no state_mutex_ needed.
  sat_aggregator_->ingest(*msg, now);
  auto src = SatAggregator::from_string(msg->source_module);
  if (src.has_value()) {
    health_monitor_->record_heartbeat(*src, now);
  }
}

void HmiTransparencyBridgeNode::on_odd_state(const l3_msgs::msg::ODDState::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_odd_ = *msg;
}

void HmiTransparencyBridgeNode::on_world_state(const l3_msgs::msg::WorldState::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_world_ = *msg;
}

void HmiTransparencyBridgeNode::on_mission_goal(const l3_msgs::msg::MissionGoal::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_mission_ = *msg;
}

void HmiTransparencyBridgeNode::on_behavior_plan(const l3_msgs::msg::BehaviorPlan::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_behavior_ = *msg;
}

void HmiTransparencyBridgeNode::on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_avoidance_ = *msg;
}

void HmiTransparencyBridgeNode::on_colreg_constraint(
    const l3_msgs::msg::COLREGsConstraint::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_colreg_ = *msg;
}

void HmiTransparencyBridgeNode::on_safety_alert(const l3_msgs::msg::SafetyAlert::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_alert_ = *msg;
}

void HmiTransparencyBridgeNode::on_override_signal(
    const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  override_active_ = msg->override_active;
}

// ---------------------------------------------------------------------------
// Timer: on_ui_publish_tick (50 Hz)
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_ui_publish_tick()
{
  // Snapshot state under lock
  std::optional<l3_msgs::msg::ODDState> odd_snap;
  std::optional<l3_msgs::msg::WorldState> world_snap;
  std::optional<l3_msgs::msg::BehaviorPlan> behavior_snap;
  std::optional<l3_msgs::msg::COLREGsConstraint> colreg_snap;
  std::optional<l3_msgs::msg::SafetyAlert> alert_snap;
  bool override_snap = false;
  bool op_sat2_snap = false;
  {
    std::lock_guard lock{state_mutex_};
    odd_snap      = latest_odd_;
    world_snap    = latest_world_;
    behavior_snap = latest_behavior_;
    colreg_snap   = latest_colreg_;
    alert_snap    = latest_alert_;
    override_snap = override_active_;
    op_sat2_snap  = operator_requested_sat2_;
  }
  // Build and publish without holding the lock
  auto now = SatAggregator::Clock::now();
  auto sat_decision = adaptive_trigger_->decide(
      odd_snap.value_or(l3_msgs::msg::ODDState{}),
      *sat_aggregator_,
      alert_snap,
      colreg_snap,
      op_sat2_snap,
      now);

  UiStateBuilder::BuildContext ctx{};
  ctx.now             = now;
  ctx.role            = UiStateBuilder::Role::kRocOperator;
  ctx.scenario        = infer_scenario_from(odd_snap, colreg_snap, override_snap);
  ctx.sat_decision    = sat_decision;
  ctx.odd             = odd_snap;
  ctx.world           = world_snap;
  ctx.behavior        = behavior_snap;
  ctx.colreg          = colreg_snap;
  ctx.latest_alert    = alert_snap;
  ctx.tor_state       = tor_protocol_->state();
  ctx.tor_remaining_s = tor_protocol_->remaining_deadline_s(now);
  ctx.override_active = override_snap;

  auto msg = ui_builder_->build(ctx, *sat_aggregator_);
  msg.stamp = get_clock()->now();
  pub_ui_state_->publish(msg);
}

// ---------------------------------------------------------------------------
// Timer: on_tor_tick (2 Hz)
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_tor_tick()
{
  auto now = SatAggregator::Clock::now();
  bool just_timed_out = tor_protocol_->tick(now);
  if (just_timed_out) {
    RCLCPP_WARN(get_logger(), "ToR 60s timeout — triggering MRC preparation");
    emit_asdr_event("tor_timeout_mrc", "{\"reason\":\"deadline_exceeded\"}");
  }
}

// ---------------------------------------------------------------------------
// Timer: on_health_check_tick (1 Hz)
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_health_check_tick()
{
  auto now = SatAggregator::Clock::now();
  if (health_monitor_->is_m7_timed_out(now)) {
    RCLCPP_ERROR(get_logger(), "M7 heartbeat timeout — forcing D2 safety mode");
  }
}

// ---------------------------------------------------------------------------
// Timer: on_asdr_snapshot_tick (2 Hz)
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_asdr_snapshot_tick()
{
  auto record = asdr_logger_->build_ui_snapshot_record(
      get_clock()->now(), "periodic_snapshot");
  pub_asdr_->publish(record);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UiStateBuilder::Scenario HmiTransparencyBridgeNode::infer_scenario_from(
    const std::optional<l3_msgs::msg::ODDState>& /*odd*/,
    const std::optional<l3_msgs::msg::COLREGsConstraint>& colreg,
    bool override_active) const
{
  if (override_active) {
    return UiStateBuilder::Scenario::kOverrideActive;
  }
  if (tor_protocol_->state() == TorProtocol::State::kTimeoutMrc) {
    return UiStateBuilder::Scenario::kMrcActive;
  }
  if (tor_protocol_->state() == TorProtocol::State::kRequested) {
    return UiStateBuilder::Scenario::kMrcPreparation;
  }
  if (colreg.has_value() && colreg->conflict_detected) {
    return UiStateBuilder::Scenario::kColregAvoidance;
  }
  return UiStateBuilder::Scenario::kTransit;
}

void HmiTransparencyBridgeNode::publish_tor_request(TorProtocol::Reason reason)
{
  auto now = SatAggregator::Clock::now();
  bool triggered = tor_protocol_->trigger(reason, now);
  if (!triggered) {
    return;
  }
  std::string summary = "SAT-1: ";
  l3_msgs::msg::ODDState odd_snapshot{};
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    if (latest_odd_.has_value()) {
      summary += latest_odd_->zone_reason;
      odd_snapshot = *latest_odd_;
    }
  }
  auto req = tor_generator_->generate(reason, odd_snapshot, summary, tor_deadline_s_);
  req.stamp = get_clock()->now();
  pub_tor_->publish(req);
  emit_asdr_event(
      "tor_requested",
      "{\"reason\":" + std::to_string(static_cast<int>(reason)) + "}");
}

void HmiTransparencyBridgeNode::emit_asdr_event(
    const std::string& event_type, const std::string& decision_json)
{
  auto record = asdr_logger_->build_record(get_clock()->now(), event_type, decision_json);
  pub_asdr_->publish(record);
}

}  // namespace mass_l3::m8
