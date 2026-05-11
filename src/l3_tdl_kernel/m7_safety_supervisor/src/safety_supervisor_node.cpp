#include "m7_safety_supervisor/safety_supervisor_node.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>

#include "builtin_interfaces/msg/time.hpp"
#include "l3_external_msgs/msg/checker_veto_notification.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/avoidance_plan.hpp"
#include "l3_msgs/msg/behavior_plan.hpp"
#include "l3_msgs/msg/colre_gs_constraint.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/reactive_override_cmd.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "m7_safety_supervisor/arbitrator/alert_generator.hpp"
#include "m7_safety_supervisor/arbitrator/safety_arbitrator.hpp"
#include "m7_safety_supervisor/checker/veto_handler.hpp"
#include "m7_safety_supervisor/iec61508/diagnostic_coverage.hpp"
#include "m7_safety_supervisor/iec61508/fault_monitor.hpp"
#include "m7_safety_supervisor/iec61508/watchdog_monitor.hpp"
#include "m7_safety_supervisor/mrm/mrm_command_set.hpp"
#include "m7_safety_supervisor/mrm/mrm_selector.hpp"
#include "m7_safety_supervisor/sotif/assumption_monitor.hpp"  // includes CommLinkState
#include "m7_safety_supervisor/sotif/performance_monitor.hpp"
#include "m7_safety_supervisor/sotif/triggering_condition_detector.hpp"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/clock.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/node_options.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/subscription_options.hpp"
#include "std_msgs/msg/header.hpp"

namespace mass_l3::m7 {

namespace {

// ---------------------------------------------------------------------------
// build_ros_stamp — extract ROS2 clock stamp building
// ---------------------------------------------------------------------------

builtin_interfaces::msg::Time build_ros_stamp(rclcpp::Clock::SharedPtr const& clk) noexcept
{
  builtin_interfaces::msg::Time ros_stamp{};
  try {
    auto const kWallNow = clk->now();
    ros_stamp.sec    = static_cast<int32_t>(kWallNow.seconds());
    ros_stamp.nanosec = static_cast<uint32_t>(kWallNow.nanoseconds() % 1000000000LL);
  } catch (...) {
    ros_stamp.sec = 0;  // Clock access failure: leave stamp at zero (handled upstream)
    ros_stamp.nanosec = 0U;
  }
  return ros_stamp;
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
SafetySupervisorNode::SafetySupervisorNode(rclcpp::NodeOptions const& options)
: rclcpp::Node("m7_safety_supervisor", options)
{
  // Callback groups
  cb_group_main_ = create_callback_group(
    rclcpp::CallbackGroupType::MutuallyExclusive);
  cb_group_events_ = create_callback_group(
    rclcpp::CallbackGroupType::Reentrant);

  instantiate_modules();

  rclcpp::QoS const kQosReliable = rclcpp::QoS(rclcpp::KeepLast(50)).reliable();
  rclcpp::QoS const kQosEvents   = rclcpp::QoS(rclcpp::KeepLast(10)).reliable();
  setup_main_loop_subscriptions(kQosReliable);
  setup_event_subscriptions(kQosEvents);
  setup_publishers();
  setup_timers();

  RCLCPP_INFO(get_logger(), "M7 SafetySupervisorNode initialised");
}

// ---------------------------------------------------------------------------
// instantiate_modules — boot-time heap allocation (called from constructor)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::instantiate_modules() noexcept
{
  watchdog_ = std::make_unique<iec61508::WatchdogMonitor>(
    iec61508::WatchdogConfig::make_default());
  fault_monitor_       = std::make_unique<iec61508::FaultMonitor>();
  diag_coverage_       = std::make_unique<iec61508::DiagnosticCoverage>();
  assumption_monitor_  = std::make_unique<sotif::AssumptionMonitor>(sotif::AssumptionConfig{});
  performance_monitor_ = std::make_unique<sotif::PerformanceMonitor>(sotif::PerformanceConfig{});
  triggering_detector_ = std::make_unique<sotif::TriggeringConditionDetector>();
  veto_handler_        = std::make_unique<checker::VetoHandler>();
  {
    mrm::MrmSelector::Config const kCfg{};
    mrm::MrmCommandSet const kCmd = mrm::MrmCommandSetLoader::safe_default();
    mrm_selector_ = std::make_unique<mrm::MrmSelector>(kCfg, kCmd, last_odd_);
  }
  arbitrator_ = std::make_unique<arbitrator::SafetyArbitrator>();
}

// ---------------------------------------------------------------------------
// setup_main_loop_subscriptions
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_main_loop_subscriptions(
    rclcpp::QoS const& qos_reliable) noexcept
{
  rclcpp::SubscriptionOptions opts;
  opts.callback_group = cb_group_main_;

  sub_odd_ = create_subscription<l3_msgs::msg::ODDState>(
    "/l3/m2/odd_state", qos_reliable,
    [this](l3_msgs::msg::ODDState::ConstSharedPtr const& msg) { on_odd_state(msg); },
    opts);

  sub_world_ = create_subscription<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state", qos_reliable,
    [this](l3_msgs::msg::WorldState::ConstSharedPtr const& msg) { on_world_state(msg); },
    opts);

  sub_behavior_ = create_subscription<l3_msgs::msg::BehaviorPlan>(
    "/l3/m4/behavior_plan", qos_reliable,
    [this](l3_msgs::msg::BehaviorPlan::ConstSharedPtr const& msg) { on_behavior_plan(msg); },
    opts);

  sub_avoidance_ = create_subscription<l3_msgs::msg::AvoidancePlan>(
    "/l3/m5/avoidance_plan", qos_reliable,
    [this](l3_msgs::msg::AvoidancePlan::ConstSharedPtr const& msg) { on_avoidance_plan(msg); },
    opts);

  sub_colregs_ = create_subscription<l3_msgs::msg::COLREGsConstraint>(
    "/l3/m6/colregs_constraint", qos_reliable,
    [this](l3_msgs::msg::COLREGsConstraint::ConstSharedPtr const& msg) {
      on_colregs_constraint(msg);
    },
    opts);
}

// ---------------------------------------------------------------------------
// setup_event_subscriptions
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_event_subscriptions(
    rclcpp::QoS const& qos_events) noexcept
{
  rclcpp::SubscriptionOptions opts;
  opts.callback_group = cb_group_events_;

  sub_override_cmd_ = create_subscription<l3_msgs::msg::ReactiveOverrideCmd>(
    "/l3/m4/reactive_override_cmd", qos_events,
    [this](l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr const& msg) {
      on_override_cmd(msg);
    },
    opts);

  sub_veto_ = create_subscription<l3_external_msgs::msg::CheckerVetoNotification>(
    "/l3/checker/veto", qos_events,
    [this](l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr const& msg) {
      on_checker_veto(msg);
    },
    opts);

  sub_reflex_ = create_subscription<l3_external_msgs::msg::ReflexActivationNotification>(
    "/l3/reflex/activation", qos_events,
    [this](l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr const& msg) {
      on_reflex_activation(msg);
    },
    opts);

  sub_override_signal_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
    "/l3/override/active", qos_events,
    [this](l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr const& msg) {
      on_override_signal(msg);
    },
    opts);
}

// ---------------------------------------------------------------------------
// setup_publishers
// ---------------------------------------------------------------------------

void SafetySupervisorNode::setup_publishers() noexcept
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

void SafetySupervisorNode::setup_timers() noexcept
{
  timer_main_ = create_timer(
    std::chrono::milliseconds{250},
    [this]() { on_main_loop_tick(); },
    cb_group_main_);

  timer_sat_ = create_timer(
    std::chrono::milliseconds{100},
    [this]() { on_sat_tick(); },
    cb_group_main_);

  timer_asdr_periodic_ = create_timer(
    std::chrono::milliseconds{500},
    [this]() { on_asdr_periodic_tick(); },
    cb_group_main_);

  timer_heartbeat_ = create_timer(
    std::chrono::milliseconds{100},
    [this]() { on_heartbeat_tick(); },
    cb_group_main_);
}

// ---------------------------------------------------------------------------
// Subscriber callbacks — main_loop group
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_odd_state(
    l3_msgs::msg::ODDState::ConstSharedPtr const& msg) noexcept
{
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM1, kNow);
  last_odd_ = *msg;
}

void SafetySupervisorNode::on_world_state(
    l3_msgs::msg::WorldState::ConstSharedPtr const& msg) noexcept
{
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM2, kNow);
  last_world_ = *msg;
}

void SafetySupervisorNode::on_behavior_plan(
    l3_msgs::msg::BehaviorPlan::ConstSharedPtr const& /*msg*/) noexcept
{
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM4, kNow);
}

void SafetySupervisorNode::on_avoidance_plan(
    l3_msgs::msg::AvoidancePlan::ConstSharedPtr const& msg) noexcept
{
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM5, kNow);
  last_avoidance_ = *msg;
}

void SafetySupervisorNode::on_colregs_constraint(
    l3_msgs::msg::COLREGsConstraint::ConstSharedPtr const& msg) noexcept
{
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM6, kNow);
  last_colregs_ = *msg;
}

// ---------------------------------------------------------------------------
// Subscriber callbacks — events group
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_override_cmd(
    l3_msgs::msg::ReactiveOverrideCmd::ConstSharedPtr const& /*msg*/) noexcept
{
  // M3 reactive override command: update watchdog for M3 messages
  auto const kNow = std::chrono::steady_clock::now();
  watchdog_->on_message_received(iec61508::MonitoredModule::kM3, kNow);
}

void SafetySupervisorNode::on_checker_veto(
    l3_external_msgs::msg::CheckerVetoNotification::ConstSharedPtr const& msg) noexcept
{
  // Forward to VetoHandler for rate tracking (events group)
  veto_handler_->on_veto_received(*msg);
}

void SafetySupervisorNode::on_reflex_activation(
    l3_external_msgs::msg::ReflexActivationNotification::ConstSharedPtr const& msg) noexcept
{
  reflex_freeze_required_ = msg->l3_freeze_required;
}

void SafetySupervisorNode::on_override_signal(
    l3_external_msgs::msg::OverrideActiveSignal::ConstSharedPtr const& msg) noexcept
{
  override_active_ = msg->override_active;
  if (!override_active_) {
    revert_from_override();
  }
}

// ---------------------------------------------------------------------------
// on_main_loop_tick — 4 Hz (250 ms)
// ---------------------------------------------------------------------------

void SafetySupervisorNode::on_main_loop_tick() noexcept
{
  auto const kNow = std::chrono::steady_clock::now();

  // Advance veto window cursor (no veto in this cycle; vetoes arrive asynchronously)
  veto_handler_->on_cycle_tick(false);

  // L3 freeze: reflex arc active — hold current MRM, do not re-evaluate
  if (reflex_freeze_required_) {
    return;
  }

  run_monitor_evaluation(kNow);
}

// ---------------------------------------------------------------------------
// run_monitor_evaluation — extracted from on_main_loop_tick for size/complexity
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void SafetySupervisorNode::run_monitor_evaluation(
    std::chrono::steady_clock::time_point now) noexcept
{
  // Evaluate all monitors
  auto const kWatchdogResult  = watchdog_->evaluate(now);
  auto const kDiagResult      = fault_monitor_->run(last_odd_, last_world_, last_colregs_);
  double const kVetoRate      = veto_handler_->current_rate();

  // AssumptionMonitor::evaluate: comm_link zeroed until comm monitor integrated
  // [TBD-comm-monitor] rtt_s and packet_loss_pct are zero until a communication
  // monitor is integrated; tracked in HAZID calibration. Blocker: medium.
  sotif::CommLinkState const kCommLink{};
  auto const kAssumptionStatus = assumption_monitor_->evaluate(
    last_odd_, last_world_, last_colregs_,
    kVetoRate,
    kCommLink,
    now);

  auto const kPerfStatus = performance_monitor_->evaluate(last_world_, now);
  bool const kExtreme    = sotif::TriggeringConditionDetector::detect(kAssumptionStatus);

  // Update diagnostic coverage metric and capture result
  diag_coverage_->update(kDiagResult, kWatchdogResult);
  last_coverage_ = diag_coverage_->current();

  // Build scenario context and select MRM
  mrm::ScenarioContext const kCtx{kAssumptionStatus, kWatchdogResult, kPerfStatus};
  auto const kMrmDecision = mrm_selector_->select(kCtx, last_odd_, last_world_, now);

  // Get ROS2 clock stamp for the alert
  builtin_interfaces::msg::Time const kRosStamp = build_ros_stamp(get_clock());

  // Arbitrate and publish if severity > INFO
  auto const kAlert = arbitrator_->arbitrate(
    kRosStamp, kCtx, kMrmDecision, kDiagResult, kExtreme);

  if (kAlert.severity > l3_msgs::msg::SafetyAlert::SEVERITY_INFO) {
    try {
      pub_alert_->publish(kAlert);
    } catch (std::exception const& ex) {
      RCLCPP_DEBUG(get_logger(), "run_monitor_evaluation: alert publish skipped (%s)", ex.what());
    } catch (...) {
      RCLCPP_DEBUG(get_logger(), "run_monitor_evaluation: alert publish skipped (unknown)");
    }
  }
}

// ---------------------------------------------------------------------------
// on_sat_tick — 10 Hz (100 ms)
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void SafetySupervisorNode::on_sat_tick() noexcept
{
  try {
    builtin_interfaces::msg::Time const kRosStamp = build_ros_stamp(get_clock());
    auto const kSatMsg = arbitrator::AlertGenerator::build_sat_data(
      kRosStamp, "M7_Safety_Supervisor: periodic health", 0.8F, "periodic");
    pub_sat_->publish(kSatMsg);
  } catch (std::exception const& ex) {
    RCLCPP_DEBUG(get_logger(), "on_sat_tick: publish skipped (%s)", ex.what());
  } catch (...) {
    RCLCPP_DEBUG(get_logger(), "on_sat_tick: publish skipped (unknown exception)");
  }
}

// ---------------------------------------------------------------------------
// on_asdr_periodic_tick — 2 Hz (500 ms)
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void SafetySupervisorNode::on_asdr_periodic_tick() noexcept
{
  try {
    builtin_interfaces::msg::Time const kRosStamp = build_ros_stamp(get_clock());
    std::array<char, 64> summary_buf{};
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
    [[maybe_unused]] auto const kN = std::snprintf(summary_buf.data(), summary_buf.size(),
        "status=nominal dc_ratio=%.3f",
        static_cast<double>(last_coverage_.coverage_ratio));
    auto const kAsdr = arbitrator::AlertGenerator::build_asdr_record(
      kRosStamp,
      arbitrator::AsdrRecordParams{
        "M7_Safety_Supervisor",
        "periodic_health_check",
        std::string_view{summary_buf.data()}});
    pub_asdr_->publish(kAsdr);
  } catch (std::exception const& ex) {
    RCLCPP_DEBUG(get_logger(), "on_asdr_periodic_tick: publish skipped (%s)", ex.what());
  } catch (...) {
    RCLCPP_DEBUG(get_logger(), "on_asdr_periodic_tick: publish skipped (unknown exception)");
  }
}

// ---------------------------------------------------------------------------
// on_heartbeat_tick — 10 Hz (100 ms)
// ---------------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void SafetySupervisorNode::on_heartbeat_tick() noexcept
{
  try {
    std_msgs::msg::Header hb{};
    hb.stamp    = get_clock()->now();
    hb.frame_id = "m7_safety_supervisor";
    pub_heartbeat_->publish(hb);
  } catch (std::exception const& ex) {
    RCLCPP_DEBUG(get_logger(), "on_heartbeat_tick: publish skipped (%s)", ex.what());
  } catch (...) {
    RCLCPP_DEBUG(get_logger(), "on_heartbeat_tick: publish skipped (unknown exception)");
  }
}

// ---------------------------------------------------------------------------
// revert_from_override
// ---------------------------------------------------------------------------

void SafetySupervisorNode::revert_from_override() noexcept
{
  mrm_selector_->reset();
  reflex_freeze_required_ = false;
}

}  // namespace mass_l3::m7
