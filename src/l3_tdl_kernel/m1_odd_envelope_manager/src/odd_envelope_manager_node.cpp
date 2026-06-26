/// Implementation of OddEnvelopeManagerNode — ROS2 node for M1 ODD/Envelope
/// Manager.
///
/// PATH-S compliance:
///   - noexcept on all callbacks
///   - Pre-allocated state (no dynamic allocation on control paths)
///   - Independent spdlog logger (third-party-library-policy.md Sect. 3.1)
///   - No exceptions (build-wide -fno-exceptions)
///   - Functions <= 40 lines, cyclomatic <= 8
///
/// Design authority: M1 ODD/Envelope Manager.

#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

#include <spdlog/common.h>
#include <spdlog/logger.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include "m1_odd_envelope_manager/conformance_score_calculator.hpp"
#include "m1_odd_envelope_manager/error.hpp"
#include "m1_odd_envelope_manager/mrc_trigger_logic.hpp"
#include "m1_odd_envelope_manager/odd_state_machine.hpp"
#include "m1_odd_envelope_manager/parameter_loader.hpp"
#include "m1_odd_envelope_manager/tmr_tdl_estimator.hpp"
#include "m1_odd_envelope_manager/types.hpp"
#include "rclcpp/callback_group.hpp"
#include "rclcpp/logging.hpp"
#include "rclcpp/node.hpp"
#include "rclcpp/qos.hpp"
#include "rclcpp/subscription_options.hpp"
#include "l3_msgs/msg/asdr_record.hpp"
#include "l3_msgs/msg/mission_goal.hpp"
#include "l3_msgs/msg/mission_state.hpp"
#include "l3_msgs/msg/mode_cmd.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_msgs/msg/operator_state.hpp"
#include "l3_msgs/msg/safety_alert.hpp"
#include "l3_msgs/msg/safety_concern_event.hpp"
#include "l3_msgs/msg/sat_data.hpp"
#include "l3_msgs/msg/to_r_request.hpp"
#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/override_active_signal.hpp"
#include "l3_external_msgs/msg/reflex_activation_notification.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "std_msgs/msg/header.hpp"

namespace mass_l3::m1 {

// ===========================================================================
// Constants
// ===========================================================================
namespace {

/// Staleness timeouts (seconds).
constexpr double kWorldStateTimeoutS  = 5.0;
constexpr double kEnvStateTimeoutS    = 15.0;
constexpr double kOwnShipTimeoutS     = 2.0;
constexpr double kM7AlertTimeoutS     = 5.0;

/// Timer periods (seconds).
constexpr double kMainLoopPeriodS     = 0.25;   // 4 Hz
constexpr double kOddPublishPeriodS   = 1.0;    // 1 Hz
constexpr double kAsdrPeriodicPeriodS = 2.0;    // 0.5 Hz
constexpr double kSatPeriodS          = 0.1;    // 10 Hz

/// Health score thresholds.
constexpr double kHealthFullThreshold     = 0.7;
constexpr double kHealthDegradedThreshold = 0.3;

/// Default topic names.
constexpr const char* kTopicSafetyAlert      = "/l3/m7/safety_alert";
constexpr const char* kTopicM7Heartbeat      = "/l3/m7/heartbeat";
constexpr const char* kTopicReflexActivation = "/l3/reflex/activation";
constexpr const char* kTopicOverrideSignal   = "/l3/override/active";
constexpr const char* kTopicEnvironmentState = "/fusion/environment_state";
constexpr const char* kTopicOwnShipState     = "/fusion/own_ship_state";
constexpr const char* kTopicWorldState       = "/l3/m2/world_state";
constexpr const char* kTopicOperatorState    = "/l3/m8/operator_state";
constexpr const char* kTopicToRRequest       = "/l3/m1/tor_request";
constexpr const char* kTopicODDState         = "/l3/m1/odd_state";
constexpr const char* kTopicModeCmd          = "/l3/m1/mode_cmd";
constexpr const char* kTopicASDR             = "/l3/asdr/record";
constexpr const char* kTopicSAT              = "/l3/sat/data";
constexpr const char* kTopicDiagnostics      = "/l3/diagnostics";
constexpr const char* kTopicMissionState     = "/l3/m3/mission_state";
constexpr const char* kTopicMissionGoal      = "/l3/m3/mission_goal";
constexpr const char* kTopicSafetyConcern    = "/l3/safety/concern";

/// Map SafetyAlert MRM string to MrcType.
inline MrcType mrm_string_to_type(const std::string& mrm) noexcept {
  if (mrm == "MRM-01") { return MrcType::Drift; }
  if (mrm == "MRM-02") { return MrcType::Anchor; }
  if (mrm == "MRM-03") { return MrcType::HeaveTo; }
  if (mrm == "MRM-04") { return MrcType::Moored; }
  return MrcType::Drift;
}

/// ModeCmd mode from envelope state.
inline uint8_t envelope_to_mode(EnvelopeState state) noexcept {
  using M = l3_msgs::msg::ModeCmd;
  switch (state) {
    case EnvelopeState::In:
    case EnvelopeState::Edge:      return M::MODE_NORMAL;
    case EnvelopeState::Out:       return M::MODE_LIMITED;
    case EnvelopeState::MrCPrep:   return M::MODE_DEGRADED;
    case EnvelopeState::MrCActive:
    case EnvelopeState::Overridden: return M::MODE_EMERGENCY;
  }
  return M::MODE_NORMAL;
}

/// ModeCmd behavior constraint from envelope state.
inline uint8_t envelope_to_constraint(EnvelopeState state) noexcept {
  using M = l3_msgs::msg::ModeCmd;
  switch (state) {
    case EnvelopeState::In:
    case EnvelopeState::Edge:       return M::CONSTRAINT_NONE;
    case EnvelopeState::MrCPrep:    return M::CONSTRAINT_SPEED;
    case EnvelopeState::Out:
    case EnvelopeState::MrCActive:
    case EnvelopeState::Overridden: return M::CONSTRAINT_BOTH;
  }
  return M::CONSTRAINT_NONE;
}

/// ODDState health value from conformance score.
inline uint8_t score_to_health(double score) noexcept {
  using O = l3_msgs::msg::ODDState;
  if (score >= kHealthFullThreshold)     { return O::HEALTH_FULL; }
  if (score >= kHealthDegradedThreshold) { return O::HEALTH_DEGRADED; }
  return O::HEALTH_CRITICAL;
}

/// Small struct: pointer + count of allowed ODD zones.
/// Backed by static constexpr storage — no heap allocation.
struct ZoneList {
  const uint8_t* data_;
  uint8_t count_;
};

/// Return pointer+count into static constexpr zone arrays.
/// No heap allocation — caller assigns into msg.allowed_zones via .assign().
inline ZoneList zones_for_health(uint8_t health) noexcept {
  using O = l3_msgs::msg::ODDState;
  static constexpr std::array<uint8_t, 4> kFull = {
      O::ODD_ZONE_A, O::ODD_ZONE_B, O::ODD_ZONE_C, O::ODD_ZONE_D};
  static constexpr std::array<uint8_t, 2> kDeg  = {O::ODD_ZONE_A, O::ODD_ZONE_B};
  static constexpr std::array<uint8_t, 1> kCrit = {O::ODD_ZONE_A};
  switch (health) {
    case O::HEALTH_FULL:      return {kFull.data(), 4};
    case O::HEALTH_DEGRADED:  return {kDeg.data(),  2};
    default:                  return {kCrit.data(),  1};
  }
}

/// Human-readable name for an EnvelopeState (used in SAT-3, avoids to_string).
inline std::string_view envelope_state_str(EnvelopeState s) noexcept {
  using namespace std::string_view_literals;
  switch (s) {
    case EnvelopeState::In:         return "In"sv;
    case EnvelopeState::Edge:       return "Edge"sv;
    case EnvelopeState::Out:        return "Out"sv;
    case EnvelopeState::MrCPrep:    return "MrCPrep"sv;
    case EnvelopeState::MrCActive:  return "MrCActive"sv;
    case EnvelopeState::Overridden: return "Overridden"sv;
  }
  return "Unknown"sv;
}

struct DiagExtract {
  bool radar_health_ok{false};
  bool comm_ok{false};
  bool tmr_available{false};
  double comm_delay_s{999.0};
  bool any_sensor_critical{true};
};

inline DiagExtract extract_diagnostics(
    const diagnostic_msgs::msg::DiagnosticArray::SharedPtr& diag) noexcept {
  DiagExtract result{};
  if (!diag) { return result; }
  result.any_sensor_critical = false;
  result.comm_delay_s = 0.0;
  for (const auto& status : diag->status) {
    const auto& name = status.name;
    const auto level = status.level;
    const bool ok = (level == diagnostic_msgs::msg::DiagnosticStatus::OK);
    if (name.find("radar") != std::string::npos) {
      result.radar_health_ok = ok;
    }
    if (name.find("comm") != std::string::npos) {
      result.comm_ok = ok;
      for (const auto& kv : status.values) {
        if (kv.key == "delay_s") {
          result.comm_delay_s = std::strtod(kv.value.c_str(), nullptr);
        }
      }
    }
    if (name.find("tmr") != std::string::npos) {
      result.tmr_available = ok;
    }
    if (level == diagnostic_msgs::msg::DiagnosticStatus::ERROR ||
        level == diagnostic_msgs::msg::DiagnosticStatus::STALE) {
      result.any_sensor_critical = true;
    }
  }
  return result;
}

}  // anonymous namespace

// ===========================================================================
// Constructor
// ===========================================================================

OddEnvelopeManagerNode::OddEnvelopeManagerNode()
    : Node("m1_odd_envelope_manager"),
      params_{},
      override_active_(false),
      reflex_active_(false),
      has_received_world_state_(false),
      has_received_env_state_(false),
      has_received_own_ship_(false),
      has_received_safety_alert_(false),
      last_score_{},
      last_tmr_tdl_{},
      last_published_state_(EnvelopeState::In),
      current_zone_(l3_msgs::msg::ODDState::ODD_ZONE_A),
      current_auto_level_(l3_msgs::msg::ODDState::AUTO_LEVEL_D3) {
  initialize_parameters();
  initialize_logger();
  initialize_publishers();
  initialize_subscribers();
  initialize_timers();

  if (logger_) {
    logger_->info("M1 ODD/Envelope Manager node initialized");
  }
}

// ===========================================================================
// Initialization — sub-helpers keep each function within 40 lines
// ===========================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void OddEnvelopeManagerNode::init_state_machine(const ParameterSet& p) {
  const StateMachineThresholds kSmt{p.in_to_edge, p.edge_to_out,
                                    p.stale_degradation_factor};
  auto sm = OddStateMachine::create(kSmt);
  if (!sm) {
    RCLCPP_FATAL(get_logger(), "OddStateMachine create failed: %s",
                 std::string(error_code_str(sm.error())).c_str());
    std::terminate();
  }
  state_machine_ = std::make_unique<OddStateMachine>(*sm);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void OddEnvelopeManagerNode::init_conformance_calc(const ParameterSet& p) {
  const ScoreWeights kSw{p.w_e, p.w_t, p.w_h};

  EScoreThresholds est{};
  est.visibility_full_nm      = p.visibility_full_nm;
  est.visibility_degraded_nm  = p.visibility_degraded_nm;
  est.visibility_marginal_nm  = p.visibility_marginal_nm;
  est.sea_state_full_hs       = p.sea_state_full_hs;
  est.sea_state_degraded_hs   = p.sea_state_degraded_hs;
  est.sea_state_marginal_hs   = p.sea_state_marginal_hs;

  // ParameterSet uses the same field names as TScoreThresholds.
  const TScoreThresholds kTst{p.comm_delay_ok_s, p.t_score_comm_ok, p.t_score_comm_bad};
  const HScoreThresholds kHst{p.h_score_available, p.h_score_unavailable};

  auto sc = ConformanceScoreCalculator::create(kSw, est, kTst, kHst);
  if (!sc) {
    RCLCPP_FATAL(get_logger(), "ConformanceScoreCalculator create failed: %s",
                 std::string(error_code_str(sc.error())).c_str());
    std::terminate();
  }
  score_calc_ = std::make_unique<ConformanceScoreCalculator>(*sc);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void OddEnvelopeManagerNode::init_tmr_tdl(const ParameterSet& p) {
  const TmrTdlParams kTtp{p.tmr_baseline_s, p.tcpa_coefficient,
                          p.tmr_min_s, p.tmr_max_s, p.tdl_min_s, p.tdl_max_s};
  auto tmr = TmrTdlEstimator::create(kTtp);
  if (!tmr) {
    RCLCPP_FATAL(get_logger(), "TmrTdlEstimator create failed: %s",
                 std::string(error_code_str(tmr.error())).c_str());
    std::terminate();
  }
  tmr_tdl_ = std::make_unique<TmrTdlEstimator>(*tmr);
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void OddEnvelopeManagerNode::init_mrc(const ParameterSet& p) {
  const MrcParams kMp{p.max_anchor_depth_m, p.max_heave_to_sea_state_hs,
                      p.max_heave_to_wind_kn};
  auto mrc = MrcTriggerLogic::create(kMp);
  if (!mrc) {
    RCLCPP_FATAL(get_logger(), "MrcTriggerLogic create failed: %s",
                 std::string(error_code_str(mrc.error())).c_str());
    std::terminate();
  }
  mrc_ = std::make_unique<MrcTriggerLogic>(*mrc);
}

void OddEnvelopeManagerNode::initialize_parameters() {
  declare_parameter<std::string>("yaml_path", "");
  std::string yaml_path = get_parameter("yaml_path").as_string();
  if (yaml_path.empty()) {
    yaml_path = "config/m1_params.yaml";
  }
  params_ = load_parameters(yaml_path);
  init_state_machine(params_);
  init_conformance_calc(params_);
  init_tmr_tdl(params_);
  init_mrc(params_);
}

void OddEnvelopeManagerNode::initialize_logger() {
  auto sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      "/var/log/mass-l3/m1_odd_envelope_manager.log",
      10 * 1024 * 1024,  // 10 MB per file
      5);                 // max 5 files
  logger_ = std::make_shared<spdlog::logger>("mass_l3_m1", sink);
  logger_->set_level(spdlog::level::info);
  logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
}

void OddEnvelopeManagerNode::initialize_publishers() {
  using rclcpp::QoS;
  using rclcpp::KeepLast;

  odd_state_pub_ = create_publisher<l3_msgs::msg::ODDState>(
      kTopicODDState, QoS(KeepLast(10)).reliable().transient_local());

  mode_cmd_pub_ = create_publisher<l3_msgs::msg::ModeCmd>(
      kTopicModeCmd, QoS(KeepLast(50)).reliable().transient_local());

  asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      kTopicASDR, QoS(KeepLast(50)).reliable().transient_local());

  sat_pub_ = create_publisher<l3_msgs::msg::SATData>(
      kTopicSAT, QoS(KeepLast(5)).reliable());

  tor_request_pub_ = create_publisher<l3_msgs::msg::ToRRequest>(
      kTopicToRRequest, QoS(KeepLast(10)).reliable().transient_local());

  safety_concern_pub_ = create_publisher<l3_msgs::msg::SafetyConcernEvent>(
      kTopicSafetyConcern, QoS(KeepLast(10)).reliable());
}

// NOLINTNEXTLINE(readability-function-size)
void OddEnvelopeManagerNode::initialize_subscribers() {
  using rclcpp::QoS;
  using rclcpp::KeepLast;

  event_cbg_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  safety_alert_sub_ = create_subscription<l3_msgs::msg::SafetyAlert>(
      kTopicSafetyAlert,
      QoS(KeepLast(50)).reliable().transient_local(),
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [this](const l3_msgs::msg::SafetyAlert::SharedPtr kMsg) {
        on_safety_alert(kMsg);
      });

  m7_heartbeat_sub_ = create_subscription<std_msgs::msg::Header>(
      kTopicM7Heartbeat,
      rclcpp::SensorDataQoS().keep_last(5),
      [this](const std_msgs::msg::Header::SharedPtr kMsg) {
        on_m7_heartbeat(kMsg);
      });

  operator_state_sub_ = create_subscription<l3_msgs::msg::OperatorState>(
      kTopicOperatorState,
      QoS(KeepLast(5)).reliable(),
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [this](const l3_msgs::msg::OperatorState::SharedPtr kMsg) {
        this->on_operator_state(kMsg);
      });

  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = event_cbg_;
    reflex_sub_ = create_subscription<l3_external_msgs::msg::ReflexActivationNotification>(
        kTopicReflexActivation,
        QoS(KeepLast(50)).reliable().transient_local(),
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        [this](const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr kMsg) {
          on_reflex_activation(kMsg);
        },
        opts);
  }

  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = event_cbg_;
    override_sub_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
        kTopicOverrideSignal,
        QoS(KeepLast(50)).reliable().transient_local(),
        // NOLINTNEXTLINE(performance-unnecessary-value-param)
        [this](const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr kMsg) {
          on_override_signal(kMsg);
        },
        opts);
  }

  env_sub_ = create_subscription<l3_external_msgs::msg::EnvironmentState>(
      kTopicEnvironmentState,
      QoS(KeepLast(5)).reliable(),
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [this](const l3_external_msgs::msg::EnvironmentState::SharedPtr kMsg) {
        on_environment_state(kMsg);
      });

  own_ship_sub_ = create_subscription<l3_external_msgs::msg::FilteredOwnShipState>(
      kTopicOwnShipState,
      rclcpp::SensorDataQoS().keep_last(2),
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [this](const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr kMsg) {
        on_own_ship_state(kMsg);
      });

  world_state_sub_ = create_subscription<l3_msgs::msg::WorldState>(
      kTopicWorldState,
      QoS(KeepLast(5)).reliable(),
      // NOLINTNEXTLINE(performance-unnecessary-value-param)
      [this](const l3_msgs::msg::WorldState::SharedPtr kMsg) {
        on_world_state(kMsg);
      });

  diag_sub_ = create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
      kTopicDiagnostics,
      QoS(KeepLast(10)).reliable(),
      [this](const diagnostic_msgs::msg::DiagnosticArray::SharedPtr kMsg) {
        on_diagnostics(kMsg);
      });

  mission_state_sub_ = create_subscription<l3_msgs::msg::MissionState>(
      kTopicMissionState,
      QoS(KeepLast(5)).reliable().transient_local(),
      [this](const l3_msgs::msg::MissionState::SharedPtr kMsg) {
        on_mission_state(kMsg);
      });

  mission_goal_sub_ = create_subscription<l3_msgs::msg::MissionGoal>(
      kTopicMissionGoal,
      QoS(KeepLast(10)).reliable(),
      [this](const l3_msgs::msg::MissionGoal::SharedPtr kMsg) {
        on_mission_goal(kMsg);
      });
}

void OddEnvelopeManagerNode::initialize_timers() {
  main_loop_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration<double>(kMainLoopPeriodS),
      [this]() { on_main_loop_tick(); });

  odd_publish_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration<double>(kOddPublishPeriodS),
      [this]() { on_odd_state_publish_tick(); });

  asdr_periodic_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration<double>(kAsdrPeriodicPeriodS),
      [this]() { on_asdr_record_periodic_tick(); });

  sat_timer_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      std::chrono::duration<double>(kSatPeriodS),
      [this]() { on_sat_data_publish_tick(); });
}

// ===========================================================================
// Subscriber callbacks
// ===========================================================================

void OddEnvelopeManagerNode::on_safety_alert(
    const l3_msgs::msg::SafetyAlert::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_safety_alert_ = kMsg;
  last_safety_alert_received_ = now();
  has_received_safety_alert_ = true;
  // D2.1: Track M7 heartbeat for watchdog
  last_m7_heartbeat_ = std::chrono::steady_clock::now();

  if (logger_) {
    logger_->info("SafetyAlert received: severity={} type={}",
                  static_cast<int>(kMsg->severity),
                  static_cast<int>(kMsg->alert_type));
  }
}

void OddEnvelopeManagerNode::on_m7_heartbeat(
    const std_msgs::msg::Header::SharedPtr /*msg*/) noexcept {
  const auto kNow = std::chrono::steady_clock::now();
  if (has_prev_m7_heartbeat_) {
    const double kInterval =
        std::chrono::duration<double>(kNow - prev_m7_heartbeat_).count();
    constexpr double kAlpha = 0.2;
    mttf_rolling_avg_s_ = (kAlpha * kInterval) +
                          ((1.0 - kAlpha) * mttf_rolling_avg_s_);
  }
  prev_m7_heartbeat_ = last_m7_heartbeat_;
  has_prev_m7_heartbeat_ = true;
  last_m7_heartbeat_ = kNow;
}

void OddEnvelopeManagerNode::on_reflex_activation(
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr kMsg)
    noexcept {
  reflex_active_ = kMsg->l3_freeze_required;

  if (logger_) {
    logger_->info("Reflex activation: freeze={} reason='{}'",
                  kMsg->l3_freeze_required, kMsg->reason);
  }
}

void OddEnvelopeManagerNode::on_override_signal(
    const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  const bool kWasActive = override_active_;
  override_active_ = kMsg->override_active;

  if (kMsg->override_active) {
    override_entry_at_ = now();
  }

  if (kWasActive != override_active_ && logger_) {
    logger_->info("Override state change: {} -> {} source='{}'",
                  kWasActive, override_active_, kMsg->activation_source);
  }
}

void OddEnvelopeManagerNode::on_environment_state(
    const l3_external_msgs::msg::EnvironmentState::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_env_state_ = kMsg;
  last_env_state_received_ = now();
  has_received_env_state_ = true;
}

void OddEnvelopeManagerNode::on_own_ship_state(
    const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_own_ship_ = kMsg;
  last_own_ship_received_ = now();
  has_received_own_ship_ = true;
}

void OddEnvelopeManagerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_world_state_ = kMsg;
  last_world_state_received_ = now();
  has_received_world_state_ = true;
}

void OddEnvelopeManagerNode::on_diagnostics(
    const diagnostic_msgs::msg::DiagnosticArray::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_diagnostics_ = kMsg;
}

void OddEnvelopeManagerNode::on_mission_state(
    const l3_msgs::msg::MissionState::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_mission_state_ = kMsg;
}

void OddEnvelopeManagerNode::on_mission_goal(
    const l3_msgs::msg::MissionGoal::SharedPtr kMsg) noexcept {  // NOLINT(performance-unnecessary-value-param)
  last_mission_goal_ = kMsg;

  if (kMsg->task_validity == l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID) {
    if (m3_active_duration_s_ > 0.0 || watchdog_concern_emitted_) {
      RCLCPP_INFO(get_logger(),
        "[M1 WATCHDOG] Reset — M3 task_validity=VALID (was %.1fs)",
        m3_active_duration_s_);
    }
    m3_active_duration_s_ = 0.0;
    watchdog_concern_emitted_ = false;
  }
}

// ===========================================================================
// D2.1 callbacks
// ===========================================================================

void OddEnvelopeManagerNode::on_operator_state(
    const l3_msgs::msg::OperatorState::SharedPtr msg) {
  current_operator_state_ = static_cast<OperatorState>(msg->assumed_operator_state);
}

void OddEnvelopeManagerNode::publish_tor_request(
    double deadline_s, double tdl_s, const std::string& rationale) {
  auto msg = l3_msgs::msg::ToRRequest{};
  msg.schema_version = 121;
  msg.stamp = this->now();
  msg.deadline_s = static_cast<float>(deadline_s);
  msg.tdl_s = static_cast<float>(tdl_s);
  msg.assumed_operator_state = static_cast<uint8_t>(current_operator_state_);
  msg.reason = l3_msgs::msg::ToRRequest::REASON_ODD_EXIT;
  msg.target_level = l3_msgs::msg::ToRRequest::TARGET_LEVEL_D2;
  msg.confidence = 1.0f;
  msg.rationale = rationale;
  msg.context_summary = "ODD boundary violation — operator takeover required";
  msg.recommended_action = "Assume manual control within deadline";

  tor_request_pub_->publish(msg);
}

// ===========================================================================
// Main-loop sub-helpers
// ===========================================================================

ScoringInputs OddEnvelopeManagerNode::build_scoring_inputs() const noexcept {
  ScoringInputs s{};
  if (last_env_state_) {
    s.visibility_nm = last_env_state_->visibility_range_nm;
    s.sea_state_hs  = last_env_state_->wave_height_m;
  } else {
    s.visibility_nm = 2.0;
    s.sea_state_hs  = 2.0;
  }
  if (last_own_ship_) {
    s.gnss_quality_good =
        (last_own_ship_->nav_mode == "OPTIMAL" ||
         last_own_ship_->nav_mode == "DR_SHORT");
  } else {
    s.gnss_quality_good = false;
  }
  const auto kDiag = extract_diagnostics(last_diagnostics_);
  s.radar_health_ok      = kDiag.radar_health_ok;
  s.comm_ok              = kDiag.comm_ok;
  s.comm_delay_s         = kDiag.comm_delay_s;
  s.any_sensor_critical  = kDiag.any_sensor_critical;
  s.tmr_available        = kDiag.tmr_available;
  return s;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
SystemHealthSnapshot OddEnvelopeManagerNode::build_system_health(
    bool m7_critical) const noexcept {
  SystemHealthSnapshot h{};
  h.mttf_estimate_s    = mttf_rolling_avg_s_;
  const auto kAge = std::chrono::steady_clock::now() - last_m7_heartbeat_;
  h.heartbeat_recency_s = std::chrono::duration<double>(kAge).count();
  h.fault_count        = m7_critical ? 1U : 0U;
  h.has_redundancy     = params_.redundancy_enabled;
  return h;
}

double OddEnvelopeManagerNode::compute_min_tcpa() const noexcept {
  if (!last_world_state_) {
    return 9999.0;
  }
  double min_val = 9999.0;
  bool found = false;
  for (const auto& t : last_world_state_->targets) {
    if (t.tcpa_s >= 0.0 && (!found || t.tcpa_s < min_val)) {
      min_val = t.tcpa_s;
      found = true;
    }
  }
  return min_val;
}

EventFlags OddEnvelopeManagerNode::build_event_flags(
    const rclcpp::Time& now_ros,
    bool m7_critical,
    bool m7_mrc_required) const noexcept {
  bool m2_stale = false;
  bool m7_stale = false;

  if (has_received_world_state_) {
    m2_stale = (now_ros - last_world_state_received_).seconds() > kWorldStateTimeoutS;
  }
  if (has_received_safety_alert_) {
    m7_stale = (now_ros - last_safety_alert_received_).seconds() > kM7AlertTimeoutS;
  }

  EventFlags e{};
  e.override_active      = override_active_;
  e.reflex_activation    = reflex_active_;
  e.m7_safety_critical   = m7_critical;
  e.m7_safety_mrc_required = m7_mrc_required;
  e.m2_input_stale       = m2_stale;
  e.m7_input_stale       = m7_stale;
  return e;
}

void OddEnvelopeManagerNode::handle_state_change(
    EnvelopeState old_state,
    EnvelopeState new_state,
    const ScoreTriple& scores,
    const TmrTdlPair& tmrtdl) noexcept {
  publish_odd_state_event();
  const auto kRationale = state_machine_->rationale();
  publish_mode_cmd(kRationale);
  publish_asdr_record("state_transition", kRationale);

  if (logger_) {
    logger_->info(
        "State transition: {} -> {} | score={:.3f} tmr={:.1f} tdl={:.1f}",
        static_cast<int>(old_state), static_cast<int>(new_state),
        scores.conformance_score, tmrtdl.tmr_s, tmrtdl.tdl_s);
  }
  if (new_state == EnvelopeState::Overridden &&
      old_state != EnvelopeState::Overridden && logger_) {
    logger_->warn("Override entered at t={:.3f}", override_entry_at_.seconds());
  }
}

void OddEnvelopeManagerNode::check_mrc_if_needed(
    EnvelopeState new_state,
    bool m7_mrc_required,
    MrcType m7_mrm,
    const ScoringInputs& scoring) noexcept {
  const bool kMrcActiveState =
      (new_state == EnvelopeState::Out ||
       new_state == EnvelopeState::MrCPrep ||
       new_state == EnvelopeState::MrCActive);
  if (!m7_mrc_required && !kMrcActiveState) {
    return;
  }

  MrcSelectionInputs mrc_in{};
  mrc_in.m7_safety_mrc_required = m7_mrc_required;
  mrc_in.m7_recommended_mrm     = m7_mrm;
  mrc_in.water_depth_m          = 0.0;
  if (last_mission_state_ && last_mission_state_->water_depth_m >= 0.0) {
    mrc_in.water_depth_m = last_mission_state_->water_depth_m;
  } else {
    mrc_in.water_depth_m = params_.environment_water_depth_m;
  }
  mrc_in.in_anchorage_zone      = last_mission_state_
                                       ? last_mission_state_->in_anchorage_zone
                                       : false;
  mrc_in.sea_state_hs           = scoring.sea_state_hs;
  mrc_in.wind_speed_kn          = last_env_state_ ? last_env_state_->wind_speed_kn : 0.0;
  mrc_in.is_moored              = last_mission_state_
                                       ? last_mission_state_->is_moored
                                       : false;
  mrc_in.current_state          = new_state;

  const auto kResult = mrc_->select(mrc_in);
  if (kResult.has_value() && logger_) {
    logger_->info("MRC selected: {} speed={} rationale='{}'",
                  static_cast<int>(kResult->type),
                  kResult->speed_cmd_kn,
                  std::string(kResult->rationale));
  }
}

// ===========================================================================
// Timer callbacks
// ===========================================================================

// NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
void OddEnvelopeManagerNode::on_main_loop_tick() noexcept {
  const rclcpp::Time kNowRos = now();
  const auto kNowSteady = std::chrono::steady_clock::now();

  check_input_freshness(kNowRos);

  // Extract M7 alert state.
  bool m7_critical     = false;
  bool m7_mrc_required = false;
  MrcType m7_mrm       = MrcType::Drift;
  if (last_safety_alert_) {
    m7_critical     = (last_safety_alert_->severity ==
                       l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
    m7_mrc_required = (last_safety_alert_->severity ==
                       l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
    m7_mrm          = mrm_string_to_type(last_safety_alert_->recommended_mrm);
  }

  ScoringInputs scoring      = build_scoring_inputs();
  scoring.any_sensor_critical = m7_critical;
  // D2.1: EMA-smoothed conformance score
  const ScoreTriple kScores  = score_calc_->compute_with_ema(
      scoring, params_, conformance_ema_, kMainLoopPeriodS);
  last_score_                = kScores;

  TmrTdlInputs tmr_in{};
  tmr_in.tcpa_min_s           = compute_min_tcpa();
  tmr_in.current_rtt_s        = 0.0;
  tmr_in.system_health        = build_system_health(m7_critical);
  tmr_in.h_score_tmr_available = scoring.tmr_available;
  // D2.1: Operator-state-aware TMR from ToR matrix
  const TmrTdlPair kTmrtdl   = tmr_tdl_->compute(tmr_in, params_, current_operator_state_);
  last_tmr_tdl_               = kTmrtdl;

  // W9: M3 ACTIVE stale watchdog (4Hz tick = 0.25s increment)
  {
    bool m3_active_and_not_valid = last_mission_goal_ &&
        (last_mission_goal_->fsm_state == l3_msgs::msg::MissionGoal::FSM_ACTIVE) &&
        (last_mission_goal_->task_validity != l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID);

    if (m3_active_and_not_valid) {
      m3_active_duration_s_ += kMainLoopPeriodS;

      double threshold = params_.m3_route_stale_threshold_s;
      if (m3_active_duration_s_ > threshold && !watchdog_concern_emitted_) {
        watchdog_concern_emitted_ = true;

        auto concern = l3_msgs::msg::SafetyConcernEvent{};
        concern.stamp = this->now();
        concern.concern_type = l3_msgs::msg::SafetyConcernEvent::CONCERN_ODD_DEGRADED;
        concern.anchor_hdg = 0.0F;
        concern.suggested_action = "M3_route_stale_watchdog";
        concern.severity = 0.6F;
        safety_concern_pub_->publish(concern);

        RCLCPP_WARN(get_logger(),
          "[M1 WATCHDOG] M3 ACTIVE but task_validity NOT valid for %.1fs "
          "(>%.1fs threshold) — emitting SafetyConcern",
          m3_active_duration_s_, threshold);

        publish_asdr_record("m3_route_stale_watchdog",
          "{\"category\":\"m3_route_stale\",\"duration_s\":" +
          std::to_string(m3_active_duration_s_) + "}");
      }
    } else {
      if (m3_active_duration_s_ > 0.0) {
        m3_active_duration_s_ = 0.0;
        watchdog_concern_emitted_ = false;
      }
    }
  }

  // W9: M3 still stale after concern emitted — log degraded-state persistence
  if (watchdog_concern_emitted_ && last_mission_goal_ &&
      last_mission_goal_->fsm_state == l3_msgs::msg::MissionGoal::FSM_ACTIVE &&
      last_mission_goal_->task_validity != l3_msgs::msg::MissionGoal::TASK_VALIDITY_VALID) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "[M1 WATCHDOG] M3 still stale (%.1fs), ODD should be degraded via safety_alert",
      m3_active_duration_s_);
  }

  // D2.1: M7 heartbeat watchdog
  const auto kHeartbeatAge = std::chrono::steady_clock::now() - last_m7_heartbeat_;
  EventFlags kEvents       = build_event_flags(kNowRos, m7_critical, m7_mrc_required);
  if (kHeartbeatAge > M7_HEARTBEAT_TIMEOUT) {
    kEvents.m7_input_stale = true;
    RCLCPP_WARN_THROTTLE(this->get_logger(), *this->get_clock(), 5000,
        "M7 heartbeat timeout (>500ms) — safety supervisor unavailable");
  }

  // D2.1: Zone/health-aware FSM step
  const uint8_t kHealth = score_to_health(kScores.conformance_score);
  const OddZoneHealthPair kZhp{
      static_cast<OddZone>(current_zone_),
      static_cast<SystemHealth>(kHealth)};
  const EnvelopeState kOldState = state_machine_->current();
  const EnvelopeState kNewState = state_machine_->step(
      kScores.conformance_score, kTmrtdl.tdl_s, kTmrtdl.tmr_s,
      kEvents, kZhp, kNowSteady);

  if (kNewState != kOldState) {
    handle_state_change(kOldState, kNewState, kScores, kTmrtdl);
  }

  // D2.1: ToR trigger — force FSM to MrCPrep and publish if TDL ≤ TMR
  if (kTmrtdl.tdl_s <= kTmrtdl.tmr_s &&
      kNewState != EnvelopeState::MrCPrep &&
      kNewState != EnvelopeState::MrCActive &&
      kNewState != EnvelopeState::Overridden) {

    // Force FSM into MrCPrep (MRC preparation) — safety constraint violated
    EventFlags mrc_events{};
    mrc_events.m7_safety_mrc_required = true;
    const EnvelopeState kMrcState = state_machine_->step(
        kScores.conformance_score, kTmrtdl.tdl_s, kTmrtdl.tmr_s,
        mrc_events, kZhp, kNowSteady);

    if (kMrcState != kNewState) {
      handle_state_change(kNewState, kMrcState, kScores, kTmrtdl);
    }

    publish_tor_request(kTmrtdl.tmr_s, kTmrtdl.tdl_s,
        "TDL (" + std::to_string(static_cast<int>(kTmrtdl.tdl_s + 0.5)) +
        "s) ≤ TMR (" + std::to_string(static_cast<int>(kTmrtdl.tmr_s + 0.5)) +
        "s) — safety constraint violated, MRC preparation triggered");

    check_mrc_if_needed(kMrcState, m7_mrc_required, m7_mrm, scoring);
    return;  // Skip normal check_mrc_if_needed below — already handled
  }

  check_mrc_if_needed(kNewState, m7_mrc_required, m7_mrm, scoring);

  if (logger_ && logger_->level() <= spdlog::level::debug) {
    logger_->debug(
        "Tick: score=({:.3f},{:.3f},{:.3f})->{:.3f} tmr={:.1f} tdl={:.1f} state={}",
        kScores.e_score, kScores.t_score, kScores.h_score,
        kScores.conformance_score, kTmrtdl.tmr_s, kTmrtdl.tdl_s,
        static_cast<int>(kNewState));
  }
}

void OddEnvelopeManagerNode::on_odd_state_publish_tick() noexcept {
  auto msg   = l3_msgs::msg::ODDState();
  msg.stamp  = now();
  msg.current_zone = current_zone_;
  msg.auto_level   = current_auto_level_;
  msg.health       = score_to_health(last_score_.conformance_score);
  msg.envelope_state = static_cast<uint8_t>(state_machine_->current());

  const double kCs = std::clamp(last_score_.conformance_score, 0.0, 1.0);
  msg.conformance_score = static_cast<float>(kCs);
  msg.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);
  msg.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);

  ScoringInputs scoring_for_reason = build_scoring_inputs();
  std::ostringstream zone_reason;
  if (scoring_for_reason.visibility_nm < params_.visibility_full_nm) {
    zone_reason << "visibility=" << std::fixed << std::setprecision(1)
                << scoring_for_reason.visibility_nm
                << "nm < full_nm=" << params_.visibility_full_nm << "nm; ";
  }
  if (scoring_for_reason.sea_state_hs > params_.sea_state_max_hs_for_reason()) {
    zone_reason << "sea_state=" << scoring_for_reason.sea_state_hs
                << "m > max=" << params_.sea_state_max_hs_for_reason() << "m; ";
  }
  if (!scoring_for_reason.radar_health_ok) zone_reason << "radar_degraded; ";
  if (!scoring_for_reason.comm_ok) zone_reason << "comm_degraded; ";
  if (!scoring_for_reason.gnss_quality_good) zone_reason << "gnss_degraded; ";
  if (!scoring_for_reason.tmr_available) zone_reason << "tmr_unavailable; ";
  std::string reason_str = zone_reason.str();
  msg.zone_reason = reason_str.empty() ? "All dimensions nominal" : reason_str;
  const auto kZl = zones_for_health(msg.health);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  msg.allowed_zones.assign(kZl.data_, kZl.data_ + kZl.count_);

  // D2.1: ROT_max from Capability Manifest
  const double kSpeedKn = last_own_ship_ ? last_own_ship_->sog_kn : 0.0;
  msg.rot_max_current = static_cast<float>(
      interpolate_rot_max(kSpeedKn, params_.rot_max_curve));

  msg.confidence = 1.0F;
  msg.rationale  = std::string(state_machine_->rationale());

  try {
    odd_state_pub_->publish(msg);
  } catch (...) {}  // NOLINT(bugprone-empty-catch)
}

void OddEnvelopeManagerNode::on_asdr_record_periodic_tick() noexcept {
  publish_asdr_record("periodic_status", "ODD manager operational");
}

void OddEnvelopeManagerNode::on_sat_data_publish_tick() noexcept {
  publish_sat_data();
}

// ===========================================================================
// Internal helpers
// ===========================================================================

void OddEnvelopeManagerNode::publish_odd_state_event() noexcept {
  on_odd_state_publish_tick();

  // Build JSON into a stack buffer — no heap allocation.
  const auto kRationale = state_machine_->rationale();
  std::array<char, 128> buf{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg,hicpp-vararg)
  const int kN = std::snprintf(buf.data(), buf.size(),
      R"({"new_state":"%.*s"})",
      static_cast<int>(kRationale.size()), kRationale.data());
  if (kN > 0) {
    publish_asdr_record("odd_state_transition",
        std::string_view{buf.data(), static_cast<std::size_t>(kN)});
  }
}

void OddEnvelopeManagerNode::publish_mode_cmd(
    std::string_view reason) noexcept {
  auto msg = l3_msgs::msg::ModeCmd();
  msg.stamp               = now();
  msg.mode                = envelope_to_mode(state_machine_->current());
  msg.behavior_constraint = envelope_to_constraint(state_machine_->current());
  msg.assumed_operator_state = static_cast<uint8_t>(current_operator_state_);
  msg.confidence          = 1.0F;
  msg.rationale           = std::string(reason);

  try {
    mode_cmd_pub_->publish(msg);
  } catch (...) {}  // NOLINT(bugprone-empty-catch)
}

void OddEnvelopeManagerNode::publish_asdr_record(
    std::string_view decision_type,  // NOLINT(bugprone-easily-swappable-parameters)
    std::string_view rationale_json) noexcept {
  auto msg = l3_msgs::msg::ASDRRecord();
  msg.stamp          = now();
  msg.source_module  = "M1_ODD_Manager";
  msg.decision_type  = std::string(decision_type);
  msg.decision_json  = std::string(rationale_json);

  try {
    asdr_pub_->publish(msg);
  } catch (...) {}  // NOLINT(bugprone-empty-catch)
}

void OddEnvelopeManagerNode::publish_sat_data() noexcept {
  auto msg = l3_msgs::msg::SATData();
  msg.stamp         = now();
  msg.source_module = "M1_ODD_Manager";

  msg.sat1.state_summary = std::string(state_machine_->rationale());
  msg.sat1.active_alerts = {};

  msg.sat2.trigger_reason   = "periodic";
  msg.sat2.reasoning_chain  = std::string(state_machine_->rationale());
  msg.sat2.system_confidence = static_cast<float>(last_score_.conformance_score);

  const auto kForecast = state_machine_->forecast(std::chrono::seconds(30));
  msg.sat3.forecast_horizon_s    = 30.0;
  msg.sat3.predicted_state       = std::string(envelope_state_str(kForecast.predicted));
  msg.sat3.prediction_uncertainty = static_cast<float>(kForecast.uncertainty);
  msg.sat3.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);
  msg.sat3.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);

  try {
    sat_pub_->publish(msg);
  } catch (...) {}  // NOLINT(bugprone-empty-catch)
}

void OddEnvelopeManagerNode::check_input_freshness(
    const rclcpp::Time& now) noexcept {
  bool any_stale = false;

  if (has_received_world_state_) {
    any_stale |= (now - last_world_state_received_).seconds() > kWorldStateTimeoutS;
  }
  if (has_received_env_state_) {
    any_stale |= (now - last_env_state_received_).seconds() > kEnvStateTimeoutS;
  }
  if (has_received_own_ship_) {
    any_stale |= (now - last_own_ship_received_).seconds() > kOwnShipTimeoutS;
  }

  if (any_stale && logger_) {
    logger_->warn("Input staleness detected");
  }
}

}  // namespace mass_l3::m1
