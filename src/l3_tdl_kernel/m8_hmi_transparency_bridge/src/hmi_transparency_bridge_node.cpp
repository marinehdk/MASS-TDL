// src/hmi_transparency_bridge_node.cpp
#include "m8_hmi_transparency_bridge/hmi_transparency_bridge_node.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "l3_msgs/msg/encounter_classification.hpp"
#include "l3_risk_model/risk_model.hpp"

namespace mass_l3::m8 {

namespace {

constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kKnotsToMps = 0.5144444444444445;
constexpr std::uint8_t kRoleStandOn = 0U;
constexpr std::uint8_t kRoleGiveWay = 1U;
constexpr std::uint8_t kRoleBothGiveWay = 2U;

mass_l3::risk::OwnShipInput ownship_risk_input(const l3_msgs::msg::WorldState& world)
{
  return mass_l3::risk::OwnShipInput{
      0.0,
      0.0,
      world.own_ship.heading_deg * kDegToRad,
      std::max(0.0, world.own_ship.sog_kn * kKnotsToMps),
      46.0,
      static_cast<double>(world.own_ship.confidence),
      world.own_ship.nav_mode == "DEGRADED"};
}

mass_l3::risk::TargetInput target_risk_input(const l3_msgs::msg::TrackedTarget& target)
{
  const double bearing_rad = target.brg_deg * kDegToRad;
  const double range_m = std::max(0.0, target.rng_m);
  return mass_l3::risk::TargetInput{
      std::to_string(target.target_id),
      std::cos(bearing_rad) * range_m,
      std::sin(bearing_rad) * range_m,
      target.cog_deg * kDegToRad,
      std::max(0.0, target.sog_kn * kKnotsToMps),
      target.cpa_m,
      target.tcpa_s,
      static_cast<double>(target.confidence)};
}

mass_l3::risk::ColregsDuty colregs_duty_from(
    const std::optional<l3_msgs::msg::COLREGsConstraint>& colreg,
    const l3_msgs::msg::TrackedTarget& target)
{
  if (!colreg.has_value() || !colreg->conflict_detected) {
    return mass_l3::risk::ColregsDuty::Free;
  }
  if (target.encounter.is_giveway) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (target.encounter.encounter_type ==
          l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_CROSSED_BY ||
      target.encounter.encounter_type ==
          l3_msgs::msg::EncounterClassification::ENCOUNTER_TYPE_OVERTAKEN) {
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  if (colreg->primary_role == kRoleGiveWay) {
    return mass_l3::risk::ColregsDuty::GiveWay;
  }
  if (colreg->primary_role == kRoleBothGiveWay) {
    return mass_l3::risk::ColregsDuty::BothGiveWay;
  }
  if (colreg->primary_role == kRoleStandOn) {
    if (colreg->phase == "INDEPENDENT_ACTION" || colreg->phase == "CRITICAL_ACTION") {
      return mass_l3::risk::ColregsDuty::Rule17Action;
    }
    return mass_l3::risk::ColregsDuty::StandOnHold;
  }
  return mass_l3::risk::ColregsDuty::Free;
}

std::string relative_position_from_bearing(double bearing_deg)
{
  const double normalized = std::fmod(bearing_deg + 360.0, 360.0);
  if (normalized <= 45.0 || normalized >= 315.0) {
    return "ahead";
  }
  if (normalized >= 135.0 && normalized <= 225.0) {
    return "astern";
  }
  if (normalized > 45.0 && normalized < 135.0) {
    return "starboard";
  }
  return "port";
}

bool risk_is_primary(
    const mass_l3::risk::RiskVector& risk,
    const mass_l3::risk::RiskVector& primary)
{
  return !primary.target_id.empty() && risk.target_id == primary.target_id;
}

}  // namespace

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

  sub_m7_heartbeat_ = create_subscription<std_msgs::msg::Header>(
      "/l3/m7/heartbeat", rclcpp::SensorDataQoS().keep_last(5),
      [this](const std_msgs::msg::Header::SharedPtr m) { on_m7_heartbeat(m); });

  sub_override_ = create_subscription<l3_external_msgs::msg::OverrideActiveSignal>(
      "/l3/override/active", rclcpp::QoS(50).reliable().transient_local(),
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

  pub_sil_sat2_ = create_publisher<l3_msgs::msg::SAT2Data>(
      "/sil/sat2_data", rclcpp::SensorDataQoS().keep_last(5));
  pub_sil_sat3_ = create_publisher<l3_msgs::msg::SAT3Data>(
      "/sil/sat3_data", rclcpp::SensorDataQoS().keep_last(10));
  pub_sil_sotif_ = create_publisher<l3_msgs::msg::SotifMetrics>(
      "/sil/sotif_metrics", rclcpp::QoS(20).reliable().transient_local());
  pub_threat_state_ = create_publisher<l3_msgs::msg::ThreatState>(
      "/l3/m8/threat_state", rclcpp::SensorDataQoS().keep_last(5));
}

// ---------------------------------------------------------------------------
// init_timers
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::init_timers()
{
  using namespace std::chrono_literals;
  timer_ui_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      20ms,
      [this] { on_ui_publish_tick(); });
  timer_tor_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      500ms,
      [this] { on_tor_tick(); });
  timer_health_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      1000ms,
      [this] { on_health_check_tick(); });
  timer_asdr_snapshot_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      500ms,
      [this] { on_asdr_snapshot_tick(); });
  timer_sil_stub_ = rclcpp::create_timer(
      get_node_base_interface(),
      get_node_timers_interface(),
      get_clock(),
      1000ms,
      [this] { on_sil_stub_tick(); });
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
    double sim_now_s = msg->stamp.sec + msg->stamp.nanosec * 1e-9;
    health_monitor_->record_heartbeat(*src, sim_now_s);
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
  has_real_sat2_ = true;
}

void HmiTransparencyBridgeNode::on_avoidance_plan(const l3_msgs::msg::AvoidancePlan::SharedPtr msg)
{
  std::lock_guard<std::mutex> lock{state_mutex_};
  latest_avoidance_ = *msg;
  has_real_sat3_ = true;
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

void HmiTransparencyBridgeNode::on_m7_heartbeat(const std_msgs::msg::Header::SharedPtr msg)
{
  double sim_now_s = msg->stamp.sec + msg->stamp.nanosec * 1e-9;
  health_monitor_->record_heartbeat(
      SatAggregator::SourceModule::kM7,
      sim_now_s);
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
  // Snapshot all mutex-protected state (including tor_protocol_ which has no internal mutex)
  std::optional<l3_msgs::msg::ODDState> odd_snap;
  std::optional<l3_msgs::msg::WorldState> world_snap;
  std::optional<l3_msgs::msg::BehaviorPlan> behavior_snap;
  std::optional<l3_msgs::msg::COLREGsConstraint> colreg_snap;
  std::optional<l3_msgs::msg::SafetyAlert> alert_snap;
  bool override_snap = false;
  bool op_sat2_snap = false;
  TorProtocol::State tor_state_snap{TorProtocol::State::kIdle};
  double tor_remaining_snap{0.0};
  auto now = SatAggregator::Clock::now();
  {
    std::lock_guard lock{state_mutex_};
    odd_snap           = latest_odd_;
    world_snap         = latest_world_;
    behavior_snap      = latest_behavior_;
    colreg_snap        = latest_colreg_;
    alert_snap         = latest_alert_;
    override_snap      = override_active_;
    op_sat2_snap       = operator_requested_sat2_;
    tor_state_snap     = tor_protocol_->state();
    tor_remaining_snap = tor_protocol_->remaining_deadline_s(now);
  }
  // Build and publish without holding the lock (avoids DDS priority inversion)
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
  ctx.scenario        = infer_scenario_from(odd_snap, colreg_snap, override_snap, tor_state_snap);
  ctx.sat_decision    = sat_decision;
  ctx.odd             = odd_snap;
  ctx.world           = world_snap;
  ctx.behavior        = behavior_snap;
  ctx.colreg          = colreg_snap;
  ctx.latest_alert    = alert_snap;
  ctx.tor_state       = tor_state_snap;
  ctx.tor_remaining_s = tor_remaining_snap;
  ctx.override_active = override_snap;

  auto msg = ui_builder_->build(ctx, *sat_aggregator_);
  msg.stamp = get_clock()->now();
  pub_ui_state_->publish(msg);

  if (world_snap.has_value()) {
    auto threat = build_threat_state(*world_snap, colreg_snap);
    threat.stamp = msg.stamp;
    pub_threat_state_->publish(threat);
  }
}

// ---------------------------------------------------------------------------
// Timer: on_tor_tick (2 Hz)
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_tor_tick()
{
  auto now = SatAggregator::Clock::now();
  bool just_timed_out = false;
  {
    std::lock_guard lock{state_mutex_};
    just_timed_out = tor_protocol_->tick(now);
  }
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
  double sim_now_s = get_clock()->now().seconds();
  if (health_monitor_->is_m7_timed_out(sim_now_s)) {
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
    bool override_active,
    TorProtocol::State tor_state) const
{
  if (override_active) {
    return UiStateBuilder::Scenario::kOverrideActive;
  }
  if (tor_state == TorProtocol::State::kTimeoutMrc) {
    return UiStateBuilder::Scenario::kMrcActive;
  }
  if (tor_state == TorProtocol::State::kRequested) {
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
  bool triggered = false;
  std::string summary = "SAT-1: ";
  l3_msgs::msg::ODDState odd_snapshot{};
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    triggered = tor_protocol_->trigger(reason, now);
    if (triggered && latest_odd_.has_value()) {
      summary += latest_odd_->zone_reason;
      odd_snapshot = *latest_odd_;
    }
  }
  if (!triggered) {
    return;
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

l3_msgs::msg::ThreatState HmiTransparencyBridgeNode::build_threat_state(
    const l3_msgs::msg::WorldState& world,
    const std::optional<l3_msgs::msg::COLREGsConstraint>& colreg) const
{
  l3_msgs::msg::ThreatState msg{};
  msg.schema_version = 114;
  msg.stamp = world.stamp;
  msg.confidence = world.confidence;

  const mass_l3::risk::DomainConfig config{};
  const auto own = ownship_risk_input(world);
  const auto danger = mass_l3::risk::danger_axes(own);
  const auto warning = mass_l3::risk::warning_axes(own, config);
  msg.danger_forward_m = danger.forward_m;
  msg.danger_astern_m = danger.astern_m;
  msg.danger_starboard_m = danger.starboard_m;
  msg.danger_port_m = danger.port_m;
  msg.warning_forward_m = warning.forward_m;
  msg.warning_astern_m = warning.astern_m;
  msg.warning_starboard_m = warning.starboard_m;
  msg.warning_port_m = warning.port_m;
  msg.superellipse_power = config.superellipse_power;
  msg.action_horizon_s = config.action_horizon_s;
  msg.critical_horizon_s = config.critical_horizon_s;

  std::vector<mass_l3::risk::RiskVector> risks;
  risks.reserve(world.targets.size());
  for (const auto& target : world.targets) {
    if (target.rng_m > 0.0 && std::isfinite(target.rng_m)) {
      risks.push_back(mass_l3::risk::evaluate_target(
          own,
          target_risk_input(target),
          colregs_duty_from(colreg, target),
          config));
    }
  }

  if (risks.empty()) {
    msg.cpa_status = "cleared";
    msg.target_relative_position = "none";
    msg.rationale = "backend risk model: no valid target";
    return msg;
  }

  const auto primary = mass_l3::risk::select_primary(risks, nullptr);
  std::sort(risks.begin(), risks.end(), [&primary](
      const mass_l3::risk::RiskVector& lhs,
      const mass_l3::risk::RiskVector& rhs) {
        if (risk_is_primary(lhs, primary) != risk_is_primary(rhs, primary)) {
          return risk_is_primary(lhs, primary);
        }
        if (lhs.risk_phase != rhs.risk_phase) {
          return static_cast<std::uint8_t>(lhs.risk_phase) >
                 static_cast<std::uint8_t>(rhs.risk_phase);
        }
        if (lhs.risk_score > rhs.risk_score) {
          return true;
        }
        if (rhs.risk_score > lhs.risk_score) {
          return false;
        }
        return lhs.range_m < rhs.range_m;
      });

  msg.cpa_status = primary.closing_speed_mps > 0.0 ? "closing" : "sustained";
  msg.target_relative_position = relative_position_from_bearing(primary.relative_bearing_deg);

  msg.target_ids.reserve(risks.size());
  msg.risk_phases.reserve(risks.size());
  msg.risk_scores.reserve(risks.size());
  msg.primary_flags.reserve(risks.size());
  msg.range_m.reserve(risks.size());
  msg.dcpa_m.reserve(risks.size());
  msg.tcpa_s.reserve(risks.size());
  msg.warning_margin_m.reserve(risks.size());
  msg.danger_margin_m.reserve(risks.size());
  msg.closing_speed_mps.reserve(risks.size());
  msg.relative_bearing_deg.reserve(risks.size());
  msg.colregs_duties.reserve(risks.size());
  msg.tdv_warning_s.reserve(risks.size());
  msg.tdv_danger_s.reserve(risks.size());

  for (const auto& risk : risks) {
    msg.target_ids.push_back(risk.target_id);
    msg.risk_phases.push_back(mass_l3::risk::to_string(risk.risk_phase));
    msg.risk_scores.push_back(static_cast<float>(risk.risk_score));
    msg.primary_flags.push_back(risk_is_primary(risk, primary));
    msg.range_m.push_back(risk.range_m);
    msg.dcpa_m.push_back(risk.dcpa_m);
    msg.tcpa_s.push_back(risk.tcpa_s);
    msg.warning_margin_m.push_back(risk.warning_margin_m);
    msg.danger_margin_m.push_back(risk.danger_margin_m);
    msg.closing_speed_mps.push_back(risk.closing_speed_mps);
    msg.relative_bearing_deg.push_back(risk.relative_bearing_deg);
    msg.colregs_duties.push_back(mass_l3::risk::to_string(risk.colregs_duty));
    msg.tdv_warning_s.push_back(risk.tdv_warning_s);
    msg.tdv_danger_s.push_back(risk.tdv_danger_s);
  }

  std::ostringstream rationale;
  rationale << "backend risk model primary=" << primary.target_id
            << " phase=" << mass_l3::risk::to_string(primary.risk_phase)
            << " score=" << primary.risk_score
            << " duty=" << mass_l3::risk::to_string(primary.colregs_duty);
  msg.rationale = rationale.str();
  return msg;
}

// ---------------------------------------------------------------------------
// Timer: on_sil_stub_tick (1 Hz) — SIL frontend stub publishers
// ---------------------------------------------------------------------------

void HmiTransparencyBridgeNode::on_sil_stub_tick()
{
  auto now = get_clock()->now();

  bool publish_sat2 = false;
  bool publish_sat3 = false;
  {
    std::lock_guard<std::mutex> lock{state_mutex_};
    publish_sat2 = !has_real_sat2_;
    publish_sat3 = !has_real_sat3_;
  }

  // SAT2Data stub
  if (publish_sat2) {
    l3_msgs::msg::SAT2Data sat2{};
    sat2.schema_version = 114;
    sat2.stamp = now;
    sat2.confidence = 1.0f;
    sat2.system_confidence = 1.0f;
    sat2.rationale = "sil_stub";
    sat2.trigger_reason = "sil_stub";
    sat2.reasoning_latency_ms = 0.0f;
    pub_sil_sat2_->publish(sat2);
  }

  // SAT3Data stub
  if (publish_sat3) {
    l3_msgs::msg::SAT3Data sat3{};
    sat3.schema_version = 114;
    sat3.stamp = now;
    sat3.confidence = 1.0f;
    sat3.prediction_uncertainty = 0.0f;
    sat3.rationale = "sil_stub";
    sat3.tdl_s = 0.0f;
    sat3.tmr_s = 0.0f;
    sat3.forecast_horizon_s = 0.0;
    sat3.primary_trajectory_idx = 0;
    pub_sil_sat3_->publish(sat3);
  }

  // SotifMetrics stub
  bool m7_active = health_monitor_ && !health_monitor_->is_m7_timed_out(get_clock()->now().seconds());
  if (!m7_active) {
    static uint32_t seq = 0;
    l3_msgs::msg::SotifMetrics sotif{};
    sotif.schema_version = 114;
    sotif.stamp = now;
    sotif.sequence_number = ++seq;
    sotif.active_violation_count = 0;
    sotif.degradation_alert = false;
    sotif.degradation_display_latency_ms = 0.0f;
    pub_sil_sotif_->publish(sotif);
  }
}

}  // namespace mass_l3::m8
