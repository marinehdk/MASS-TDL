#include "m7_safety_supervisor/safety_supervisor_node.hpp"

#include <chrono>
#include <functional>

namespace mass_l3::m7 {

using namespace std::chrono_literals;

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

SafetySupervisorNode::SafetySupervisorNode(rclcpp::NodeOptions const& options)
: rclcpp::Node("m7_safety_supervisor", options)
{
  // Callback groups
  cb_group_main_ = create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);
  cb_group_events_ = create_callback_group(
    rclcpp::CallbackGroupType::Reentrant);

  // Boot-time module construction (heap allocation allowed here)
  watchdog_ = std::make_unique<iec61508::WatchdogMonitor>(
    iec61508::WatchdogConfig::make_default());

  fault_monitor_   = std::make_unique<iec61508::FaultMonitor>();
  diag_coverage_   = std::make_unique<iec61508::DiagnosticCoverage>();

  assumption_monitor_ = std::make_unique<sotif::AssumptionMonitor>(
    sotif::AssumptionConfig{});

  performance_monitor_ = std::make_unique<sotif::PerformanceMonitor>(
    sotif::PerformanceConfig{});

  triggering_detector_ = std::make_unique<sotif::TriggeringConditionDetector>();

  veto_handler_ = std::make_unique<checker::VetoHandler>();

  {
    mrm::MrmSelector::Config cfg{};
    mrm::MrmCommandSet cmd = mrm::MrmCommandSetLoader::safe_default();
    mrm_selector_ = std::make_unique<mrm::MrmSelector>(cfg, cmd, last_odd_);
  }

  arbitrator_ = std::make_unique<arbitrator::SafetyArbitrator>();

  // QoS profiles
  rclcpp::QoS const qos_reliable =
    rclcpp::QoS(rclcpp::KeepLast(50)).reliable();
  rclcpp::QoS const qos_besteffort =
    rclcpp::QoS(rclcpp::KeepLast(10)).best_effort();

  setup_subscriptions(qos_reliable, qos_besteffort);
  setup_publishers();
  setup_timers();

  RCLCPP_INFO(get_logger(), "M7 SafetySupervisorNode initialised");
}

// ---------------------------------------------------------------------------
// setup_subscriptions
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_subscriptions(
    rclcpp::QoS const& qos_reliable,
    rclcpp::QoS const& /*qos_besteffort*/)
{
  rclcpp::QoS const qos_events = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();

  // Main-loop group subscriptions
  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = cb_group_main_;

    sub_odd_ = create_subscription<l3_msgs::msg::ODDState>(
      "/l3/m2/odd_state", qos_reliable,
      [this](l3_msgs::msg::ODDState::ConstSharedPtr msg) { on_odd_state(msg); },
      opts);

    sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state", qos_reliable,
      [this](l3_msgs::msg::WorldState::ConstSharedPtr msg) { on_world_state(msg); },
      opts);

    sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
      "/l3/m4/behavior_plan", qos_reliable,
      [this](l3_msgs::msg::BehaviorPlan::ConstSharedPtr msg) { on_behavior_plan(msg); },
      opts);

    sub_avoidance_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
      "/l3/m5/avoidance_plan", qos_reliable,
      [this](l3_msgs::msg::AvoidancePlan::ConstSharedPtr msg) { on_avoidance_plan(msg); },
      opts);

    sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
      "/l3/m6/colregs_constraint", qos_reliable,
      [this](l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg) {
        on_colregs_constraint(msg);
      },
      opts);
  }

  // Events group subscriptions
  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = cb_group_events_;

    sub_override_cmd_ = create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
      "/l3/m4/reactive_override_cmd", qos_events,
      [this](l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr msg) {
        on_override_cmd(msg);
      },
      opts);

    sub_veto_ = create_subscription<l3_external_msgs::msg::CheckerVetoNotification>(
      "/l3/checker/veto", qos_events,
      [this](l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr msg) {
        on_checker_veto(msg);
      },
      opts);

    sub_reflex_ = create_subscription<l3_external_msgs::msg::ReflexActivationNotification>(
      "/l3/reflex/activation", qos_events,
      [this](l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr msg) {
        on_reflex_activation(msg);
      },
      opts);

    sub_override_signal_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
      "/l3/override/active", qos_events,
      [this](l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr msg) {
        on_override_signal(msg);
      },
      opts);
  }
}

// ---------------------------------------------------------------------------
// setup_publishers
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_publishers()
{
  pub_alert_ = create_publisher<l3_msgs::msg::SafetyAlert>(
    "/l3/m7/safety_alert",
    rclcpp::QoS(rclcpp::KeepLast(50)).reliable());

  pub_asdr_ = create_publisher<l3_msgs::msg::ASDRRecord>(
    "/l3/asdr/record",
    rclcpp::QoS(rclcpp::KeepLast(100)).reliable());

  pub_sat_ = create_publisher<l3_msgs::msg::SATData>(
    "/l3/sat/data",
    rclcpp::QoS(rclcpp::KeepLast(10)).best_effort());

  pub_heartbeat_ = create_publisher<std_msgs::msg::Header>(
    "/l3/m7/heartbeat",
    rclcpp::QoS(rclcpp::KeepLast(10)).best_effort());
}

// ---------------------------------------------------------------------------
// setup_timers — all in main_loop callback group
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_timers()
{
  timer_main_ = create_timer(
    250ms,
    [this]() { on_main_loop_tick(); },
    cb_group_main_);

  timer_sat_ = create_timer(
    100ms,
    [this]() { on_sat_tick(); },
    cb_group_main_);

  timer_asdr_periodic_ = create_timer(
    500ms,
    [this]() { on_asdr_periodic_tick(); },
    cb_group_main_);

  timer_heartbeat_ = create_timer(
    100ms,
    [this]() { on_heartbeat_tick(); },
    cb_group_main_);
}

// ---------------------------------------------------------------------------
// Subscriber callbacks — main_loop group
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_odd_state(
    l3_msgs::msg::ODDState::ConstSharedPtr msg)
{
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM1, now);
  last_odd_ = *msg;
}

void SafetySupervisorNode::on_world_state(
    l3_msgs::msg::WorldState::ConstSharedPtr msg)
{
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM2, now);
  last_world_ = *msg;
}

void SafetySupervisorNode::on_behavior_plan(
    l3_msgs::msg::BehaviorPlan::ConstSharedPtr /*msg*/)
{
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM4, now);
}

void SafetySupervisorNode::on_avoidance_plan(
    l3_msgs::msg::AvoidancePlan::ConstSharedPtr msg)
{
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM5, now);
  last_avoidance_ = *msg;
}

void SafetySupervisorNode::on_colregs_constraint(
    l3_msgs::msg::COLREGsConstraint::ConstSharedPtr msg)
{
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM6, now);
  last_colregs_ = *msg;
}

// ---------------------------------------------------------------------------
// Subscriber callbacks — events group
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_override_cmd(
    l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr /*msg*/)
{
  // M3 reactive override command: update watchdog for M3 messages
  auto const now = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM3, now);
}

void SafetySupervisorNode::on_checker_veto(
    l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr msg)
{
  // Forward to VetoHandler for rate tracking (events group)
  veto_handler_->on_veto_received(*msg);
}

void SafetySupervisorNode::on_reflex_activation(
    l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr msg)
{
  reflex_freeze_required_ = msg->l3_freeze_required;
}

void SafetySupervisorNode::on_override_signal(
    l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr msg)
{
  override_active_ = msg->override_active;
  if (!override_active_) {
    revert_from_override();
  }
}

// ---------------------------------------------------------------------------
// on_main_loop_tick — 4 Hz (250 ms)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_main_loop_tick()
{
  auto const now = std::chrono::steady_clock::now();

  // Advance veto window cursor (no veto in this cycle; vetoes arrive asynchronously)
  veto_handler_->on_cycle_tick(false);

  // Evaluate all monitors
  auto const watchdog_result  = watchdog_->evaluate(now);
  auto const diag_result      = fault_monitor_->run(last_odd_, last_world_, last_colregs_);
  double const veto_rate      = veto_handler_->current_rate();

  // AssumptionMonitor::evaluate requires 7 args:
  // (odd, world, colregs, veto_rate, rtt_s, packet_loss_pct, now)
  // rtt_s and packet_loss_pct: 0.0 until a comm monitor is integrated
  auto const assumption_status = assumption_monitor_->evaluate(
    last_odd_, last_world_, last_colregs_,
    veto_rate,
    0.0,   // rtt_s — [TBD] comm monitor not yet integrated
    0.0,   // packet_loss_pct — [TBD]
    now);

  auto const perf_status = performance_monitor_->evaluate(last_world_, now);
  bool const extreme     = triggering_detector_->detect(assumption_status);

  // Update diagnostic coverage metric
  diag_coverage_->update(diag_result, watchdog_result);

  // Build scenario context and select MRM
  mrm::ScenarioContext ctx{assumption_status, watchdog_result, perf_status};
  auto const mrm_decision = mrm_selector_->select(ctx, last_odd_, last_world_, now);

  // Arbitrate and publish if severity > INFO
  auto const alert = arbitrator_->arbitrate(
    ctx, mrm_decision, diag_result, extreme, now);

  if (alert.severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) {
    pub_alert_->publish(alert);
  }
}

// ---------------------------------------------------------------------------
// on_sat_tick — 10 Hz (100 ms)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_sat_tick()
{
  auto const stamp = get_clock()->now();
  builtin_interfaces::msg::Time ros_stamp{};
  ros_stamp.sec     = static_cast<int32_t>(stamp.seconds());
  ros_stamp.nanosec = static_cast<uint32_t>(stamp.nanoseconds() % 1000000000LL);

  auto const sat_msg = arbitrator::AlertGenerator::build_sat_data(
    ros_stamp,
    "M7_Safety_Supervisor: periodic health",
    0.8F,
    "periodic");

  pub_sat_->publish(sat_msg);
}

// ---------------------------------------------------------------------------
// on_asdr_periodic_tick — 2 Hz (500 ms)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_asdr_periodic_tick()
{
  auto const stamp = get_clock()->now();
  builtin_interfaces::msg::Time ros_stamp{};
  ros_stamp.sec     = static_cast<int32_t>(stamp.seconds());
  ros_stamp.nanosec = static_cast<uint32_t>(stamp.nanoseconds() % 1000000000LL);

  auto const asdr = arbitrator::AlertGenerator::build_asdr_record(
    ros_stamp,
    "M7_Safety_Supervisor",
    "periodic_health_check",
    "status=nominal");

  pub_asdr_->publish(asdr);
}

// ---------------------------------------------------------------------------
// on_heartbeat_tick — 10 Hz (100 ms)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_heartbeat_tick()
{
  std_msgs::msg::Header hb{};
  hb.stamp    = get_clock()->now();
  hb.frame_id = "m7_safety_supervisor";
  pub_heartbeat_->publish(hb);
}

// ---------------------------------------------------------------------------
// revert_from_override
// ---------------------------------------------------------------------------

void SafetySupervisorNode::revert_from_override()
{
  mrm_selector_->reset();
  reflex_freeze_required_ = false;
}

}  // namespace mass_l3::m7
