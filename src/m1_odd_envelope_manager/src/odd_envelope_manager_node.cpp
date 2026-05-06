/// Implementation of OddEnvelopeManagerNode — ROS2 node for M1 ODD/Envelope
/// Manager.
///
/// PATH-S compliance:
///   - noexcept on all callbacks
///   - Pre-allocated state (no dynamic allocation on control paths)
///   - Independent spdlog logger (third-party-library-policy.md Sect. 3.1)
///   - No exceptions (build-wide -fno-exceptions)
///   - Functions <= 40 lines where practical, cyclomatic <= 8
///
/// Design authority: M1 ODD/Envelope Manager.

#include "m1_odd_envelope_manager/odd_envelope_manager_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

#include <builtin_interfaces/msg/time.hpp>

#include "m1_odd_envelope_manager/types.hpp"

namespace mass_l3::m1 {

// ===========================================================================
// Constants
// ===========================================================================
namespace {

/// Staleness timeouts (seconds).
constexpr double kWorldStateTimeoutS = 5.0;
constexpr double kEnvStateTimeoutS = 15.0;
constexpr double kOwnShipTimeoutS = 2.0;

/// Timer periods (seconds).
constexpr double kMainLoopPeriodS = 0.25;    // 4 Hz
constexpr double kOddPublishPeriodS = 1.0;    // 1 Hz
constexpr double kAsdrPeriodicPeriodS = 2.0;  // 0.5 Hz
constexpr double kSatPeriodS = 0.1;            // 10 Hz

/// Health score thresholds.
constexpr double kHealthFullThreshold = 0.7;
constexpr double kHealthDegradedThreshold = 0.3;

/// Epsilon for floating-point comparison avoidance (Wfloat-equal).
constexpr double kEps = 1e-9;

/// Default ROS2 topic names.
constexpr const char* kTopicSafetyAlert = "/l3/m7/safety_alert";
constexpr const char* kTopicReflexActivation = "/reflex/activation_notification";
constexpr const char* kTopicOverrideSignal = "/override/active_signal";
constexpr const char* kTopicEnvironmentState = "/fusion/environment_state";
constexpr const char* kTopicOwnShipState = "/fusion/own_ship_state";
constexpr const char* kTopicWorldState = "/l3/m2/world_state";
constexpr const char* kTopicODDState = "/l3/m1/odd_state";
constexpr const char* kTopicModeCmd = "/l3/m1/mode_cmd";
constexpr const char* kTopicASDR = "/l3/asdr/record";
constexpr const char* kTopicSAT = "/l3/sat/data";

/// Map SafetyAlert severity to MrcType string recommendation.
inline MrcType mrm_string_to_type(const std::string& mrm) noexcept {
  if (mrm == "MRM-01") return MrcType::Drift;
  if (mrm == "MRM-02") return MrcType::Anchor;
  if (mrm == "MRM-03") return MrcType::HeaveTo;
  if (mrm == "MRM-04") return MrcType::Moored;
  return MrcType::Drift;  // default fallback
}

/// Determine ModeCmd mode from envelope state.
inline uint8_t envelope_to_mode(EnvelopeState state) noexcept {
  using M = l3_msgs::msg::ModeCmd;
  switch (state) {
    case EnvelopeState::In:
    case EnvelopeState::Edge:
      return M::MODE_NORMAL;
    case EnvelopeState::Out:
      return M::MODE_LIMITED;
    case EnvelopeState::MrCPrep:
      return M::MODE_DEGRADED;
    case EnvelopeState::MrCActive:
    case EnvelopeState::Overridden:
      return M::MODE_EMERGENCY;
  }
  return M::MODE_NORMAL;
}

/// Determine ModeCmd behavior constraint from envelope state.
inline uint8_t envelope_to_constraint(EnvelopeState state) noexcept {
  using M = l3_msgs::msg::ModeCmd;
  switch (state) {
    case EnvelopeState::In:
    case EnvelopeState::Edge:
      return M::CONSTRAINT_NONE;
    case EnvelopeState::MrCPrep:
      return M::CONSTRAINT_SPEED;
    case EnvelopeState::Out:
    case EnvelopeState::MrCActive:
    case EnvelopeState::Overridden:
      return M::CONSTRAINT_BOTH;
  }
  return M::CONSTRAINT_NONE;
}

/// Determine ODDState health from conformance score.
inline uint8_t score_to_health(double score) noexcept {
  using O = l3_msgs::msg::ODDState;
  if (score >= kHealthFullThreshold - kEps) return O::HEALTH_FULL;
  if (score >= kHealthDegradedThreshold - kEps) return O::HEALTH_DEGRADED;
  return O::HEALTH_CRITICAL;
}

/// Build allowed_zones list from health level.
inline std::vector<uint8_t> allowed_zones_for_health(uint8_t health) noexcept {
  using O = l3_msgs::msg::ODDState;
  switch (health) {
    case O::HEALTH_FULL:
      return {O::ODD_ZONE_A, O::ODD_ZONE_B, O::ODD_ZONE_C, O::ODD_ZONE_D};
    case O::HEALTH_DEGRADED:
      return {O::ODD_ZONE_A, O::ODD_ZONE_B};
    case O::HEALTH_CRITICAL:
    default:
      return {O::ODD_ZONE_A};
  }
}

}  // anonymous namespace

// ===========================================================================
// Constructor
// ===========================================================================

OddEnvelopeManagerNode::OddEnvelopeManagerNode()
    : Node("m1_odd_envelope_manager"),
      override_active_(false),
      reflex_active_(false),
      has_received_world_state_(false),
      has_received_env_state_(false),
      has_received_own_ship_(false),
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
// Initialization
// ===========================================================================

void OddEnvelopeManagerNode::initialize_parameters() {
  // Declare and retrieve the YAML config path.
  declare_parameter<std::string>("yaml_path", "");
  std::string yaml_path = get_parameter("yaml_path").as_string();

  // Fall back to a relative development path if empty.
  if (yaml_path.empty()) {
    yaml_path = "config/m1_params.yaml";
  }

  // Load all parameters (YAML + defaults).
  params_ = load_parameters(yaml_path);

  // --- Build domain objects from parameter subsets ---

  // 1. OddStateMachine
  StateMachineThresholds smt{};
  smt.in_to_edge = params_.in_to_edge;
  smt.edge_to_out = params_.edge_to_out;
  smt.stale_degradation_factor = params_.stale_degradation_factor;

  auto sm = OddStateMachine::create(smt);
  if (!sm) {
    RCLCPP_FATAL(get_logger(), "OddStateMachine create failed: %s",
                 std::string(error_code_str(sm.error())).c_str());
    std::terminate();
  }
  state_machine_ = std::make_unique<OddStateMachine>(std::move(*sm));

  // 2. ConformanceScoreCalculator
  ScoreWeights sw{};
  sw.w_e = params_.w_e;
  sw.w_t = params_.w_t;
  sw.w_h = params_.w_h;

  EScoreThresholds est{};
  est.visibility_full_nm = params_.visibility_full_nm;
  est.visibility_degraded_nm = params_.visibility_degraded_nm;
  est.visibility_marginal_nm = params_.visibility_marginal_nm;
  est.sea_state_full_hs = params_.sea_state_full_hs;
  est.sea_state_degraded_hs = params_.sea_state_degraded_hs;
  est.sea_state_marginal_hs = params_.sea_state_marginal_hs;

  TScoreThresholds tst{};
  tst.comm_delay_ok_s = params_.comm_delay_ok_s;
  // Map ParameterSet T-score values to TScoreThresholds fields.
  //   t_score_degraded -> t_score_comm_ok  (sensor degraded, comm OK)
  //   t_score_critical -> t_score_comm_bad (comm bad or critical failure)
  tst.t_score_comm_ok = params_.t_score_degraded;
  tst.t_score_comm_bad = params_.t_score_critical;

  HScoreThresholds hst{};
  hst.h_score_tmr_available = params_.h_score_available;
  hst.h_score_tmr_unavailable = params_.h_score_unavailable;

  auto sc = ConformanceScoreCalculator::create(sw, est, tst, hst);
  if (!sc) {
    RCLCPP_FATAL(get_logger(), "ConformanceScoreCalculator create failed: %s",
                 std::string(error_code_str(sc.error())).c_str());
    std::terminate();
  }
  score_calc_ = std::make_unique<ConformanceScoreCalculator>(std::move(*sc));

  // 3. TmrTdlEstimator
  TmrTdlParams ttp{};
  ttp.tmr_baseline_s = params_.tmr_baseline_s;
  ttp.tcpa_coefficient = params_.tcpa_coefficient;
  ttp.tmr_min_s = params_.tmr_min_s;
  ttp.tmr_max_s = params_.tmr_max_s;
  ttp.tdl_min_s = params_.tdl_min_s;
  ttp.tdl_max_s = params_.tdl_max_s;

  auto tmr = TmrTdlEstimator::create(ttp);
  if (!tmr) {
    RCLCPP_FATAL(get_logger(), "TmrTdlEstimator create failed: %s",
                 std::string(error_code_str(tmr.error())).c_str());
    std::terminate();
  }
  tmr_tdl_ = std::make_unique<TmrTdlEstimator>(std::move(*tmr));

  // 4. MrcTriggerLogic
  MrcParams mp{};
  mp.max_anchor_depth_m = params_.max_anchor_depth_m;
  mp.max_heave_to_sea_state_hs = params_.max_heave_to_sea_state_hs;
  mp.max_heave_to_wind_kn = params_.max_heave_to_wind_kn;

  auto mrc = MrcTriggerLogic::create(mp);
  if (!mrc) {
    RCLCPP_FATAL(get_logger(), "MrcTriggerLogic create failed: %s",
                 std::string(error_code_str(mrc.error())).c_str());
    std::terminate();
  }
  mrc_ = std::make_unique<MrcTriggerLogic>(std::move(*mrc));
}

void OddEnvelopeManagerNode::initialize_logger() {
  // Rotating file sink: 10 MB per file, max 5 files.
  // With -fno-exceptions, if spdlog cannot open the file (e.g., missing
  // /var/log/mass-l3/ directory) the process terminates. This is acceptable
  // for a safety-critical system -- missing log configuration is fatal.
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

  // ODDState: QoS(10).reliable().transient_local()
  odd_state_pub_ = create_publisher<l3_msgs::msg::ODDState>(
      kTopicODDState,
      QoS(KeepLast(10)).reliable().transient_local());

  // ModeCmd: QoS(50).reliable().transient_local()
  mode_cmd_pub_ = create_publisher<l3_msgs::msg::ModeCmd>(
      kTopicModeCmd,
      QoS(KeepLast(50)).reliable().transient_local());

  // ASDRRecord: QoS(50).reliable().transient_local()
  asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      kTopicASDR,
      QoS(KeepLast(50)).reliable().transient_local());

  // SATData: QoS(5).reliable()
  sat_pub_ = create_publisher<l3_msgs::msg::SATData>(
      kTopicSAT,
      QoS(KeepLast(5)).reliable());
}

void OddEnvelopeManagerNode::initialize_subscribers() {
  using rclcpp::QoS;
  using rclcpp::KeepLast;

  // Create reentrant callback group for event subscriptions.
  event_cbg_ = create_callback_group(
      rclcpp::CallbackGroupType::Reentrant);

  // SafetyAlert: QoS(50).reliable().transient_local()
  safety_alert_sub_ = create_subscription<l3_msgs::msg::SafetyAlert>(
      kTopicSafetyAlert,
      QoS(KeepLast(50)).reliable().transient_local(),
      [this](const l3_msgs::msg::SafetyAlert::SharedPtr msg) {
        on_safety_alert(msg);
      });

  // ReflexActivationNotification: QoS(50).reliable().transient_local()
  {
    rclcpp::SubscriptionOptions sub_opts;
    sub_opts.callback_group = event_cbg_;
    reflex_sub_ = create_subscription<l3_external_msgs::msg::ReflexActivationNotification>(
        kTopicReflexActivation,
        QoS(KeepLast(50)).reliable().transient_local(),
        [this](const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr msg) {
          on_reflex_activation(msg);
        },
        sub_opts);
  }

  // OverrideActiveSignal: QoS(50).reliable().transient_local()
  {
    rclcpp::SubscriptionOptions sub_opts;
    sub_opts.callback_group = event_cbg_;
    override_sub_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
        kTopicOverrideSignal,
        QoS(KeepLast(50)).reliable().transient_local(),
        [this](const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg) {
          on_override_signal(msg);
        },
        sub_opts);
  }

  // EnvironmentState: QoS(5).reliable()
  env_sub_ = create_subscription<l3_external_msgs::msg::EnvironmentState>(
      kTopicEnvironmentState,
      QoS(KeepLast(5)).reliable(),
      [this](const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) {
        on_environment_state(msg);
      });

  // FilteredOwnShipState: SensorDataQoS().keep_last(2)
  // rclcpp::SensorDataQoS() is equivalent to QoS(5).best_effort().durability_volatile()
  own_ship_sub_ = create_subscription<l3_external_msgs::msg::FilteredOwnShipState>(
      kTopicOwnShipState,
      rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) {
        on_own_ship_state(msg);
      });

  // WorldState: QoS(5).reliable()
  world_state_sub_ = create_subscription<l3_msgs::msg::WorldState>(
      kTopicWorldState,
      QoS(KeepLast(5)).reliable(),
      [this](const l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state(msg);
      });
}

void OddEnvelopeManagerNode::initialize_timers() {
  using namespace std::chrono_literals;

  main_loop_timer_ = create_wall_timer(
      std::chrono::duration<double>(kMainLoopPeriodS),
      [this]() { on_main_loop_tick(); });

  odd_publish_timer_ = create_wall_timer(
      std::chrono::duration<double>(kOddPublishPeriodS),
      [this]() { on_odd_state_publish_tick(); });

  asdr_periodic_timer_ = create_wall_timer(
      std::chrono::duration<double>(kAsdrPeriodicPeriodS),
      [this]() { on_asdr_record_periodic_tick(); });

  sat_timer_ = create_wall_timer(
      std::chrono::duration<double>(kSatPeriodS),
      [this]() { on_sat_data_publish_tick(); });
}

// ===========================================================================
// Subscriber callbacks
// ===========================================================================

void OddEnvelopeManagerNode::on_safety_alert(
    const l3_msgs::msg::SafetyAlert::SharedPtr msg) noexcept {
  last_safety_alert_ = msg;

  if (logger_) {
    logger_->info("SafetyAlert received: severity={} type={}",
                  static_cast<int>(msg->severity),
                  static_cast<int>(msg->alert_type));
  }
}

void OddEnvelopeManagerNode::on_reflex_activation(
    const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr msg)
    noexcept {
  reflex_active_ = msg->is_active;

  if (logger_) {
    logger_->info("Reflex activation: active={} reason='{}'",
                  msg->is_active, msg->activation_reason);
  }
}

void OddEnvelopeManagerNode::on_override_signal(
    const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg) noexcept {
  bool was_active = override_active_;
  override_active_ = msg->override_active;

  if (msg->override_active) {
    // Store the entry timestamp from the message or current ROS time.
    override_entry_at_ = now();
  }

  if (was_active != override_active_ && logger_) {
    logger_->info("Override state change: {} -> {} source='{}'",
                  was_active, override_active_, msg->override_source);
  }
}

void OddEnvelopeManagerNode::on_environment_state(
    const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) noexcept {
  last_env_state_ = msg;
  last_env_state_received_ = now();
  has_received_env_state_ = true;
}

void OddEnvelopeManagerNode::on_own_ship_state(
    const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) noexcept {
  last_own_ship_ = msg;
  last_own_ship_received_ = now();
  has_received_own_ship_ = true;
}

void OddEnvelopeManagerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr msg) noexcept {
  last_world_state_ = msg;
  last_world_state_received_ = now();
  has_received_world_state_ = true;
}

// ===========================================================================
// Timer callbacks
// ===========================================================================

void OddEnvelopeManagerNode::on_main_loop_tick() noexcept {
  const rclcpp::Time now_ros = now();
  const auto now_steady = std::chrono::steady_clock::now();

  // ---- 1. Check input freshness ----
  check_input_freshness(now_ros);

  // ---- 2. Build ScoringInputs from cached messages ----
  ScoringInputs scoring{};

  if (last_env_state_) {
    scoring.visibility_nm = last_env_state_->visibility_range_nm;
    scoring.sea_state_hs = last_env_state_->wave_height_m;
  } else {
    scoring.visibility_nm = 10.0;   // Default clear visibility
    scoring.sea_state_hs = 1.0;    // Default calm sea
  }

  if (last_own_ship_) {
    scoring.gnss_quality_good =
        (last_own_ship_->nav_filter_status == "OPTIMAL" ||
         last_own_ship_->nav_filter_status == "CONVERGING");
  } else {
    scoring.gnss_quality_good = true;
  }

  // Default values for signals not directly mapped from messages.
  scoring.radar_health_ok = true;
  scoring.comm_ok = true;
  scoring.comm_delay_s = 0.0;
  scoring.any_sensor_critical = false;
  scoring.tmr_available = true;

  // Extract M7 alert state from cached SafetyAlert.
  bool m7_critical = false;
  bool m7_mrc_required = false;
  MrcType m7_mrm = MrcType::Drift;

  if (last_safety_alert_) {
    const auto& sa = *last_safety_alert_;
    m7_critical = (sa.severity == l3_msgs::msg::SafetyAlert::SEVERITY_CRITICAL);
    m7_mrc_required =
        (sa.severity == l3_msgs::msg::SafetyAlert::SEVERITY_MRC_REQUIRED);
    scoring.any_sensor_critical = m7_critical;
    m7_mrm = mrm_string_to_type(sa.recommended_mrm);
  }

  // ---- 3. Compute conformance scores ----
  const ScoreTriple scores = score_calc_->compute(scoring);
  last_score_ = scores;

  // ---- 4. Compute TMR/TDL ----
  // Build SystemHealthSnapshot.
  SystemHealthSnapshot health{};
  health.mttf_estimate_s = 10000.0;  // Default: very reliable
  health.heartbeat_recency_s = 0.0;  // Default: just received
  health.fault_count =
      (m7_critical || scoring.any_sensor_critical) ? 1U : 0U;
  health.has_redundancy = true;

  // Find minimum TCPA across all targets.
  double tcpa_min = std::numeric_limits<double>::max();
  if (last_world_state_) {
    for (const auto& t : last_world_state_->targets) {
      if (t.tcpa_s < tcpa_min && t.tcpa_s >= 0.0) {
        tcpa_min = t.tcpa_s;
      }
    }
  }
  // No targets or all negative.
  if (tcpa_min >= std::numeric_limits<double>::max() - kEps) {
    tcpa_min = 9999.0;
  }

  TmrTdlInputs tmr_inputs{};
  tmr_inputs.tcpa_min_s = tcpa_min;
  tmr_inputs.current_rtt_s = 0.0;
  tmr_inputs.system_health = health;
  tmr_inputs.h_score_tmr_available = scoring.tmr_available;

  const TmrTdlPair tmrtdl = tmr_tdl_->compute(tmr_inputs);
  last_tmr_tdl_ = tmrtdl;

  // ---- 5. Build EventFlags ----
  // Staleness is determined in check_input_freshness. For now, compute
  // from timeouts.
  bool m2_stale = false;
  bool m7_stale = false;
  if (has_received_world_state_) {
    const double ws_age = (now_ros - last_world_state_received_).seconds();
    m2_stale = (ws_age > kWorldStateTimeoutS);
  }
  if (last_safety_alert_) {
    // SafetyAlert is event-driven; staleness is determined differently.
    // Default: not stale unless we have a specific reason.
    m7_stale = false;
  }

  EventFlags events{};
  events.override_active = override_active_;
  events.reflex_activation = reflex_active_;
  events.m7_safety_critical = m7_critical;
  events.m7_safety_mrc_required = m7_mrc_required;
  events.m2_input_stale = m2_stale;
  events.m7_input_stale = m7_stale;

  // ---- 6. Step state machine ----
  const EnvelopeState old_state = state_machine_->current();
  const EnvelopeState new_state = state_machine_->step(
      scores.conformance_score, tmrtdl.tdl_s, tmrtdl.tmr_s, events,
      now_steady);

  // ---- 7. On state change, publish events ----
  if (new_state != old_state) {
    publish_odd_state_event(old_state, new_state);
    publish_mode_cmd(current_auto_level_, state_machine_->rationale());
    publish_asdr_record(
        "state_transition",
        std::string(state_machine_->rationale()));

    if (logger_) {
      logger_->info(
          "State transition: {} -> {} | score={:.3f} tmr={:.1f} tdl={:.1f}",
          static_cast<int>(old_state), static_cast<int>(new_state),
          scores.conformance_score, tmrtdl.tmr_s, tmrtdl.tdl_s);
    }

    // If entering Overridden from another state, log override details.
    if (new_state == EnvelopeState::Overridden && old_state != EnvelopeState::Overridden) {
      if (logger_) {
        logger_->warn("Override entered at t={:.3f}",
                      override_entry_at_.seconds());
      }
    }
  }

  // ---- 8. Check MRC selection ----
  if (m7_mrc_required ||
      new_state == EnvelopeState::Out ||
      new_state == EnvelopeState::MrCPrep ||
      new_state == EnvelopeState::MrCActive) {
    MrcSelectionInputs mrc_inputs{};
    mrc_inputs.m7_safety_mrc_required = m7_mrc_required;
    mrc_inputs.m7_recommended_mrm = m7_mrm;
    mrc_inputs.water_depth_m = 0.0;  // Not available from current messages
    mrc_inputs.in_anchorage_zone = false;
    mrc_inputs.sea_state_hs = scoring.sea_state_hs;
    mrc_inputs.wind_speed_kn =
        last_env_state_ ? last_env_state_->wind_speed_kn : 0.0;
    mrc_inputs.is_moored = false;
    mrc_inputs.current_state = new_state;

    const auto mrc_result = mrc_->select(mrc_inputs);
    if (mrc_result.has_value()) {
      if (logger_) {
        logger_->info("MRC selected: {} speed={} rationale='{}'",
                      static_cast<int>(mrc_result->type),
                      mrc_result->speed_cmd_kn,
                      std::string(mrc_result->rationale));
      }
    }
  }

  // ---- 9. Once-per-cycle logging ----
  if (logger_ && logger_->level() <= spdlog::level::debug) {
    logger_->debug(
        "Tick: score=({:.3f},{:.3f},{:.3f})->{:.3f} "
        "tmr={:.1f} tdl={:.1f} state={}",
        scores.e_score, scores.t_score, scores.h_score,
        scores.conformance_score, tmrtdl.tmr_s, tmrtdl.tdl_s,
        static_cast<int>(new_state));
  }
}

void OddEnvelopeManagerNode::on_odd_state_publish_tick() noexcept {
  auto msg = l3_msgs::msg::ODDState();
  msg.stamp = now();
  msg.current_zone = current_zone_;
  msg.auto_level = current_auto_level_;
  msg.health = score_to_health(last_score_.conformance_score);
  msg.envelope_state = static_cast<uint8_t>(state_machine_->current());

  // Clamp and cast conformance_score to float.
  double cs = last_score_.conformance_score;
  if (cs < 0.0) cs = 0.0;
  if (cs > 1.0) cs = 1.0;
  msg.conformance_score = static_cast<float>(cs);

  // TMR/TDL from last computed values.
  msg.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);
  msg.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);

  msg.zone_reason = "ODD-A (default)";
  msg.allowed_zones = allowed_zones_for_health(msg.health);
  msg.confidence = 1.0f;
  msg.rationale = std::string(state_machine_->rationale());

  odd_state_pub_->publish(msg);
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

void OddEnvelopeManagerNode::publish_odd_state_event(
    const EnvelopeState /*old_state*/,
    const EnvelopeState new_state) noexcept {
  // Publish an event-driven ODDState on transition. Same content as the
  // periodic publication but sent immediately.
  on_odd_state_publish_tick();

  // Also log to ASDR for audit.
  std::string json = R"({"new_state":")";
  json += std::string(state_machine_->rationale());
  json += "\"}";

  publish_asdr_record("odd_state_transition", json);
}

void OddEnvelopeManagerNode::publish_mode_cmd(
    const uint8_t new_level,
    std::string_view reason) noexcept {
  auto msg = l3_msgs::msg::ModeCmd();
  msg.stamp = now();
  msg.mode = envelope_to_mode(state_machine_->current());
  msg.behavior_constraint = envelope_to_constraint(state_machine_->current());
  msg.confidence = 1.0f;
  msg.rationale = std::string(reason);

  mode_cmd_pub_->publish(msg);
}

void OddEnvelopeManagerNode::publish_asdr_record(
    std::string_view decision_type,
    std::string_view rationale_json) noexcept {
  auto msg = l3_msgs::msg::ASDRRecord();
  msg.stamp = now();
  msg.source_module = "M1_ODD_Manager";
  msg.decision_type = std::string(decision_type);
  msg.decision_json = std::string(rationale_json);
  // Signature field left empty (SHA-256 integration pending).

  asdr_pub_->publish(msg);
}

void OddEnvelopeManagerNode::publish_sat_data() noexcept {
  auto msg = l3_msgs::msg::SATData();
  msg.stamp = now();
  msg.source_module = "M1_ODD_Manager";

  // SAT-1: current status summary.
  msg.sat1.state_summary = std::string(state_machine_->rationale());
  msg.sat1.active_alerts = {};

  // SAT-2: reasoning (triggered externally, empty unless set).
  msg.sat2.trigger_reason = "periodic";
  msg.sat2.reasoning_chain = std::string(state_machine_->rationale());
  msg.sat2.system_confidence = static_cast<float>(last_score_.conformance_score);

  // SAT-3: forecast.
  auto forecast = state_machine_->forecast(std::chrono::seconds(30));
  msg.sat3.forecast_horizon_s = 30.0;
  msg.sat3.predicted_state = std::to_string(static_cast<int>(forecast.predicted));
  msg.sat3.prediction_uncertainty = static_cast<float>(forecast.uncertainty);
  msg.sat3.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);
  msg.sat3.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);

  sat_pub_->publish(msg);
}

void OddEnvelopeManagerNode::check_input_freshness(
    const rclcpp::Time& now) noexcept {
  // Compute staleness and trigger ASDR if needed.
  bool any_stale = false;

  if (has_received_world_state_) {
    const double ws_age = (now - last_world_state_received_).seconds();
    if (ws_age > kWorldStateTimeoutS) {
      any_stale = true;
    }
  }

  if (has_received_env_state_) {
    const double env_age = (now - last_env_state_received_).seconds();
    if (env_age > kEnvStateTimeoutS) {
      any_stale = true;
    }
  }

  if (has_received_own_ship_) {
    const double os_age = (now - last_own_ship_received_).seconds();
    if (os_age > kOwnShipTimeoutS) {
      any_stale = true;
    }
  }

  if (any_stale && logger_) {
    logger_->warn("Input staleness detected");
  }
}

}  // namespace mass_l3::m1
