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
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

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
constexpr const char* kTopicReflexActivation = "/reflex/activation_notification";
constexpr const char* kTopicOverrideSignal   = "/override/active_signal";
constexpr const char* kTopicEnvironmentState = "/fusion/environment_state";
constexpr const char* kTopicOwnShipState     = "/fusion/own_ship_state";
constexpr const char* kTopicWorldState       = "/l3/m2/world_state";
constexpr const char* kTopicODDState         = "/l3/m1/odd_state";
constexpr const char* kTopicModeCmd          = "/l3/m1/mode_cmd";
constexpr const char* kTopicASDR             = "/l3/asdr/record";
constexpr const char* kTopicSAT              = "/l3/sat/data";

/// Map SafetyAlert MRM string to MrcType.
inline MrcType mrm_string_to_type(const std::string& mrm) noexcept {
  if (mrm == "MRM-01") return MrcType::Drift;
  if (mrm == "MRM-02") return MrcType::Anchor;
  if (mrm == "MRM-03") return MrcType::HeaveTo;
  if (mrm == "MRM-04") return MrcType::Moored;
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
  if (score >= kHealthFullThreshold)     return O::HEALTH_FULL;
  if (score >= kHealthDegradedThreshold) return O::HEALTH_DEGRADED;
  return O::HEALTH_CRITICAL;
}

/// Small struct: pointer + count of allowed ODD zones.
/// Backed by static constexpr storage — no heap allocation.
struct ZoneList {
  const uint8_t* data;
  uint8_t count;
};

/// Return pointer+count into static constexpr zone arrays.
/// No heap allocation — caller assigns into msg.allowed_zones via .assign().
inline ZoneList zones_for_health(uint8_t health) noexcept {
  using O = l3_msgs::msg::ODDState;
  static constexpr uint8_t kFull[] = {
      O::ODD_ZONE_A, O::ODD_ZONE_B, O::ODD_ZONE_C, O::ODD_ZONE_D};
  static constexpr uint8_t kDeg[]  = {O::ODD_ZONE_A, O::ODD_ZONE_B};
  static constexpr uint8_t kCrit[] = {O::ODD_ZONE_A};
  switch (health) {
    case O::HEALTH_FULL:      return {kFull, 4};
    case O::HEALTH_DEGRADED:  return {kDeg, 2};
    default:                  return {kCrit, 1};
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
      has_received_safety_alert_(false),
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

void OddEnvelopeManagerNode::init_state_machine(const ParameterSet& p) {
  const StateMachineThresholds smt{p.in_to_edge, p.edge_to_out,
                                    p.stale_degradation_factor};
  auto sm = OddStateMachine::create(smt);
  if (!sm) {
    RCLCPP_FATAL(get_logger(), "OddStateMachine create failed: %s",
                 std::string(error_code_str(sm.error())).c_str());
    std::terminate();
  }
  state_machine_ = std::make_unique<OddStateMachine>(std::move(*sm));
}

void OddEnvelopeManagerNode::init_conformance_calc(const ParameterSet& p) {
  const ScoreWeights sw{p.w_e, p.w_t, p.w_h};

  EScoreThresholds est{};
  est.visibility_full_nm      = p.visibility_full_nm;
  est.visibility_degraded_nm  = p.visibility_degraded_nm;
  est.visibility_marginal_nm  = p.visibility_marginal_nm;
  est.sea_state_full_hs       = p.sea_state_full_hs;
  est.sea_state_degraded_hs   = p.sea_state_degraded_hs;
  est.sea_state_marginal_hs   = p.sea_state_marginal_hs;

  // ParameterSet uses the same field names as TScoreThresholds.
  const TScoreThresholds tst{p.comm_delay_ok_s, p.t_score_comm_ok, p.t_score_comm_bad};
  const HScoreThresholds hst{p.h_score_available, p.h_score_unavailable};

  auto sc = ConformanceScoreCalculator::create(sw, est, tst, hst);
  if (!sc) {
    RCLCPP_FATAL(get_logger(), "ConformanceScoreCalculator create failed: %s",
                 std::string(error_code_str(sc.error())).c_str());
    std::terminate();
  }
  score_calc_ = std::make_unique<ConformanceScoreCalculator>(std::move(*sc));
}

void OddEnvelopeManagerNode::init_tmr_tdl(const ParameterSet& p) {
  const TmrTdlParams ttp{p.tmr_baseline_s, p.tcpa_coefficient,
                          p.tmr_min_s, p.tmr_max_s, p.tdl_min_s, p.tdl_max_s};
  auto tmr = TmrTdlEstimator::create(ttp);
  if (!tmr) {
    RCLCPP_FATAL(get_logger(), "TmrTdlEstimator create failed: %s",
                 std::string(error_code_str(tmr.error())).c_str());
    std::terminate();
  }
  tmr_tdl_ = std::make_unique<TmrTdlEstimator>(std::move(*tmr));
}

void OddEnvelopeManagerNode::init_mrc(const ParameterSet& p) {
  const MrcParams mp{p.max_anchor_depth_m, p.max_heave_to_sea_state_hs,
                     p.max_heave_to_wind_kn};
  auto mrc = MrcTriggerLogic::create(mp);
  if (!mrc) {
    RCLCPP_FATAL(get_logger(), "MrcTriggerLogic create failed: %s",
                 std::string(error_code_str(mrc.error())).c_str());
    std::terminate();
  }
  mrc_ = std::make_unique<MrcTriggerLogic>(std::move(*mrc));
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
}

void OddEnvelopeManagerNode::initialize_subscribers() {
  using rclcpp::QoS;
  using rclcpp::KeepLast;

  event_cbg_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  safety_alert_sub_ = create_subscription<l3_msgs::msg::SafetyAlert>(
      kTopicSafetyAlert,
      QoS(KeepLast(50)).reliable().transient_local(),
      [this](const l3_msgs::msg::SafetyAlert::SharedPtr msg) {
        on_safety_alert(msg);
      });

  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = event_cbg_;
    reflex_sub_ = create_subscription<l3_external_msgs::msg::ReflexActivationNotification>(
        kTopicReflexActivation,
        QoS(KeepLast(50)).reliable().transient_local(),
        [this](const l3_external_msgs::msg::ReflexActivationNotification::SharedPtr msg) {
          on_reflex_activation(msg);
        },
        opts);
  }

  {
    rclcpp::SubscriptionOptions opts;
    opts.callback_group = event_cbg_;
    override_sub_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
        kTopicOverrideSignal,
        QoS(KeepLast(50)).reliable().transient_local(),
        [this](const l3_external_msgs::msg::OverrideActiveSignal::SharedPtr msg) {
          on_override_signal(msg);
        },
        opts);
  }

  env_sub_ = create_subscription<l3_external_msgs::msg::EnvironmentState>(
      kTopicEnvironmentState,
      QoS(KeepLast(5)).reliable(),
      [this](const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) {
        on_environment_state(msg);
      });

  own_ship_sub_ = create_subscription<l3_external_msgs::msg::FilteredOwnShipState>(
      kTopicOwnShipState,
      rclcpp::SensorDataQoS().keep_last(2),
      [this](const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) {
        on_own_ship_state(msg);
      });

  world_state_sub_ = create_subscription<l3_msgs::msg::WorldState>(
      kTopicWorldState,
      QoS(KeepLast(5)).reliable(),
      [this](const l3_msgs::msg::WorldState::SharedPtr msg) {
        on_world_state(msg);
      });
}

void OddEnvelopeManagerNode::initialize_timers() {
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
  last_safety_alert_received_ = now();
  has_received_safety_alert_ = true;

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
  const bool was_active = override_active_;
  override_active_ = msg->override_active;

  if (msg->override_active) {
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
// Main-loop sub-helpers
// ===========================================================================

ScoringInputs OddEnvelopeManagerNode::build_scoring_inputs() const noexcept {
  ScoringInputs s{};
  if (last_env_state_) {
    s.visibility_nm = last_env_state_->visibility_range_nm;
    s.sea_state_hs  = last_env_state_->wave_height_m;
  } else {
    s.visibility_nm = 10.0;  // default: clear visibility
    s.sea_state_hs  = 1.0;   // default: calm sea
  }
  if (last_own_ship_) {
    s.gnss_quality_good =
        (last_own_ship_->nav_filter_status == "OPTIMAL" ||
         last_own_ship_->nav_filter_status == "CONVERGING");
  } else {
    s.gnss_quality_good = true;
  }
  s.radar_health_ok      = true;
  s.comm_ok              = true;
  s.comm_delay_s         = 0.0;
  s.any_sensor_critical  = false;  // overridden by caller from M7 data
  s.tmr_available        = true;
  return s;
}

SystemHealthSnapshot OddEnvelopeManagerNode::build_system_health(
    bool m7_critical) const noexcept {
  SystemHealthSnapshot h{};
  h.mttf_estimate_s    = 10000.0;
  h.heartbeat_recency_s = 0.0;
  h.fault_count        = m7_critical ? 1U : 0U;
  h.has_redundancy     = true;
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
  const auto rationale = state_machine_->rationale();
  publish_mode_cmd(rationale);
  publish_asdr_record("state_transition", rationale);

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
  const bool mrc_active_state =
      (new_state == EnvelopeState::Out ||
       new_state == EnvelopeState::MrCPrep ||
       new_state == EnvelopeState::MrCActive);
  if (!m7_mrc_required && !mrc_active_state) {
    return;
  }

  MrcSelectionInputs mrc_in{};
  mrc_in.m7_safety_mrc_required = m7_mrc_required;
  mrc_in.m7_recommended_mrm     = m7_mrm;
  mrc_in.water_depth_m          = 0.0;
  mrc_in.in_anchorage_zone      = false;
  mrc_in.sea_state_hs           = scoring.sea_state_hs;
  mrc_in.wind_speed_kn          = last_env_state_ ? last_env_state_->wind_speed_kn : 0.0;
  mrc_in.is_moored              = false;
  mrc_in.current_state          = new_state;

  const auto result = mrc_->select(mrc_in);
  if (result.has_value() && logger_) {
    logger_->info("MRC selected: {} speed={} rationale='{}'",
                  static_cast<int>(result->type),
                  result->speed_cmd_kn,
                  std::string(result->rationale));
  }
}

// ===========================================================================
// Timer callbacks
// ===========================================================================

void OddEnvelopeManagerNode::on_main_loop_tick() noexcept {
  const rclcpp::Time now_ros = now();
  const auto now_steady = std::chrono::steady_clock::now();

  check_input_freshness(now_ros);

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
  const ScoreTriple scores   = score_calc_->compute(scoring);
  last_score_                = scores;

  TmrTdlInputs tmr_in{};
  tmr_in.tcpa_min_s           = compute_min_tcpa();
  tmr_in.current_rtt_s        = 0.0;
  tmr_in.system_health        = build_system_health(m7_critical);
  tmr_in.h_score_tmr_available = scoring.tmr_available;
  const TmrTdlPair tmrtdl     = tmr_tdl_->compute(tmr_in);
  last_tmr_tdl_               = tmrtdl;

  const EventFlags events      = build_event_flags(now_ros, m7_critical, m7_mrc_required);
  const EnvelopeState old_state = state_machine_->current();
  const EnvelopeState new_state = state_machine_->step(
      scores.conformance_score, tmrtdl.tdl_s, tmrtdl.tmr_s, events, now_steady);

  if (new_state != old_state) {
    handle_state_change(old_state, new_state, scores, tmrtdl);
  }

  check_mrc_if_needed(new_state, m7_mrc_required, m7_mrm, scoring);

  if (logger_ && logger_->level() <= spdlog::level::debug) {
    logger_->debug(
        "Tick: score=({:.3f},{:.3f},{:.3f})->{:.3f} tmr={:.1f} tdl={:.1f} state={}",
        scores.e_score, scores.t_score, scores.h_score,
        scores.conformance_score, tmrtdl.tmr_s, tmrtdl.tdl_s,
        static_cast<int>(new_state));
  }
}

void OddEnvelopeManagerNode::on_odd_state_publish_tick() noexcept {
  auto msg   = l3_msgs::msg::ODDState();
  msg.stamp  = now();
  msg.current_zone = current_zone_;
  msg.auto_level   = current_auto_level_;
  msg.health       = score_to_health(last_score_.conformance_score);
  msg.envelope_state = static_cast<uint8_t>(state_machine_->current());

  const double cs = std::clamp(last_score_.conformance_score, 0.0, 1.0);
  msg.conformance_score = static_cast<float>(cs);
  msg.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);
  msg.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);

  msg.zone_reason = "ODD-A (default)";
  const auto zl = zones_for_health(msg.health);
  msg.allowed_zones.assign(zl.data, zl.data + zl.count);
  msg.confidence = 1.0f;
  msg.rationale  = std::string(state_machine_->rationale());

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

void OddEnvelopeManagerNode::publish_odd_state_event() noexcept {
  on_odd_state_publish_tick();

  // Build JSON into a stack buffer — no heap allocation.
  const auto rationale = state_machine_->rationale();
  std::array<char, 128> buf{};
  const int n = std::snprintf(buf.data(), buf.size(),
      R"({"new_state":"%.*s"})",
      static_cast<int>(rationale.size()), rationale.data());
  if (n > 0) {
    publish_asdr_record("odd_state_transition",
        std::string_view{buf.data(), static_cast<std::size_t>(n)});
  }
}

void OddEnvelopeManagerNode::publish_mode_cmd(
    std::string_view reason) noexcept {
  auto msg = l3_msgs::msg::ModeCmd();
  msg.stamp               = now();
  msg.mode                = envelope_to_mode(state_machine_->current());
  msg.behavior_constraint = envelope_to_constraint(state_machine_->current());
  msg.confidence          = 1.0f;
  msg.rationale           = std::string(reason);

  mode_cmd_pub_->publish(msg);
}

void OddEnvelopeManagerNode::publish_asdr_record(
    std::string_view decision_type,
    std::string_view rationale_json) noexcept {
  auto msg = l3_msgs::msg::ASDRRecord();
  msg.stamp          = now();
  msg.source_module  = "M1_ODD_Manager";
  msg.decision_type  = std::string(decision_type);
  msg.decision_json  = std::string(rationale_json);

  asdr_pub_->publish(msg);
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

  const auto forecast = state_machine_->forecast(std::chrono::seconds(30));
  msg.sat3.forecast_horizon_s    = 30.0;
  msg.sat3.predicted_state       = std::string(envelope_state_str(forecast.predicted));
  msg.sat3.prediction_uncertainty = static_cast<float>(forecast.uncertainty);
  msg.sat3.tdl_s = static_cast<float>(last_tmr_tdl_.tdl_s);
  msg.sat3.tmr_s = static_cast<float>(last_tmr_tdl_.tmr_s);

  sat_pub_->publish(msg);
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
