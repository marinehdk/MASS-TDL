#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <l3_msgs/msg/asdr_record.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>
#include <l3_msgs/msg/constraint.hpp>
#include <l3_msgs/msg/odd_state.hpp>
#include <l3_msgs/msg/sat_data.hpp>
#include <l3_msgs/msg/world_state.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/time.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>  // NOLINT(misc-include-cleaner)

#include "m6_colregs_reasoner/colregs_constraint_generator.hpp"
#include "m6_colregs_reasoner/colregs_phase_classifier.hpp"
#include "m6_colregs_reasoner/geometry_utils.hpp"
#include "m6_colregs_reasoner/rule_library_loader.hpp"
#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"
#include "m6_colregs_reasoner/target_state_cache.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

namespace {

// ------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------

OddDomain odd_domain_from_zone(uint8_t zone) {
  switch (zone) {
    case l3_msgs::msg::ODDState::ODD_ZONE_A: return OddDomain::ODD_A;
    case l3_msgs::msg::ODDState::ODD_ZONE_B: return OddDomain::ODD_B;
    case l3_msgs::msg::ODDState::ODD_ZONE_C: return OddDomain::ODD_C;
    case l3_msgs::msg::ODDState::ODD_ZONE_D: return OddDomain::ODD_D;
    default: return OddDomain::ODD_UNKNOWN;
  }
}

std::string odd_domain_str(OddDomain d) {
  switch (d) {
    case OddDomain::ODD_A: return "ODD-A";
    case OddDomain::ODD_B: return "ODD-B";
    case OddDomain::ODD_C: return "ODD-C";
    case OddDomain::ODD_D: return "ODD-D";
    default: return "ODD-UNKNOWN";
  }
}

std::string odd_yaml_key(OddDomain d) {
  switch (d) {
    case OddDomain::ODD_A: return "odd_a";
    case OddDomain::ODD_B: return "odd_b";
    case OddDomain::ODD_C: return "odd_c";
    case OddDomain::ODD_D: return "odd_d";
    default: return "odd_a";
  }
}

int32_t classify_ship_priority(const std::string& classification) {
  if (classification == "vessel") { return 1; }
  if (classification == "fixed_object") { return 0; }
  return -1;  // unknown
}

/// Compute initial great-circle bearing (degrees) from point A to point B.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
double bearing_between(double lat_a_deg, double lon_a_deg, double lat_b_deg, double lon_b_deg) {
  const double kLatA = deg2rad(lat_a_deg);
  const double kLatB = deg2rad(lat_b_deg);
  const double kDLon = deg2rad(lon_b_deg - lon_a_deg);

  const double kY = std::sin(kDLon) * std::cos(kLatB);
  const double kX = (std::cos(kLatA) * std::sin(kLatB))
                 - (std::sin(kLatA) * std::cos(kLatB) * std::cos(kDLon));
  return normalize_bearing_deg(rad2deg(std::atan2(kY, kX)));
}

/// Convert builtin_interfaces::msg::Time to std::chrono::system_clock::time_point.
std::chrono::system_clock::time_point to_chrono(
    const builtin_interfaces::msg::Time& t) {
  return std::chrono::system_clock::time_point(
      std::chrono::seconds(t.sec) +
      std::chrono::nanoseconds(t.nanosec));
}

}  // anonymous namespace

// ------------------------------------------------------------------
// Constructor
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
ColregsReasonerNode::ColregsReasonerNode()
  : Node("m6_colregs_reasoner"),
    last_world_stamp_(0, 0, RCL_ROS_TIME),  // NOLINT(misc-include-cleaner)
    last_odd_stamp_(0, 0, RCL_ROS_TIME) {   // NOLINT(misc-include-cleaner)
  declare_parameters();
  load_odd_thresholds();
  create_components();
  setup_publishers();
  setup_subscribers();
  setup_timers();
  setup_logger();
  RCLCPP_INFO(get_logger(), "M6 COLREGs Reasoner v1.0.0 initialized with %zu rules",
              rules_.size());
}

// ------------------------------------------------------------------
// Parameter declaration
// ------------------------------------------------------------------

void ColregsReasonerNode::declare_parameters() {
  // m6_params
  declare_parameter("reasoning_period_ms", 500);
  declare_parameter("health_check_period_ms", 1000);
  declare_parameter("asdr_snapshot_period_ms", 500);
  declare_parameter("sat_publish_period_ms", 100);
  declare_parameter("world_state_timeout_s", 5.0);
  declare_parameter("odd_state_timeout_s", 10.0);
  declare_parameter("max_targets", 50);

  // Path for finding config files at runtime
  declare_parameter("config_dir", std::string("config"));
}

// ------------------------------------------------------------------
// Load ODD-aware thresholds from YAML
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void ColregsReasonerNode::load_odd_thresholds() {
  const std::string kConfigDir = get_parameter("config_dir").as_string();
  const std::string kYamlPath = kConfigDir + "/odd_aware_thresholds.yaml";

  YAML::Node doc;  // NOLINT(misc-include-cleaner)
  try {
    doc = YAML::LoadFile(kYamlPath);  // NOLINT(misc-include-cleaner)
  } catch (const std::exception& e) {
    RCLCPP_WARN(get_logger(), "Cannot load odd_aware_thresholds.yaml: %s", e.what());
    return;
  }

  // Four ODD zones
  const std::vector<OddDomain> kZones = {
    OddDomain::ODD_A, OddDomain::ODD_B,
    OddDomain::ODD_C, OddDomain::ODD_D
  };

  for (const auto& zone : kZones) {
    const std::string kKey = odd_yaml_key(zone);
    const YAML::Node kNode = doc[kKey];
    if (!kNode) {
      RCLCPP_WARN(get_logger(), "Missing ODD config section: %s", kKey.c_str());
      continue;
    }

    RuleParameters params{};
    params.t_standOn_s         = kNode["t_standOn_s"].as<double>(480.0);
    params.t_act_s             = kNode["t_act_s"].as<double>(240.0);
    params.t_emergency_s       = kNode["t_emergency_s"].as<double>(60.0);
    params.min_alteration_deg  = kNode["min_alteration_deg"].as<double>(15.0);
    params.cpa_safe_m          = kNode["cpa_safe_m"].as<double>(1852.0);
    params.max_speed_kn        = kNode["max_speed_kn"].as<double>(20.0);
    params.max_turn_rate_deg_s = kNode["max_turn_rate_deg_s"].as<double>(5.0);
    params.rule_9_weight       = 0.0;

    odd_thresholds_[zone] = params;
    RCLCPP_DEBUG(get_logger(), "Loaded thresholds for %s: act=%f, emergency=%f",
                 kKey.c_str(), params.t_act_s, params.t_emergency_s);
  }
}

// ------------------------------------------------------------------
// Create components
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void ColregsReasonerNode::create_components() {
  // Target state cache
  TargetStateCache::Config cache_cfg{};
  cache_cfg.max_targets = static_cast<int32_t>(get_parameter("max_targets").as_int());
  target_cache_ = std::make_unique<TargetStateCache>(cache_cfg);

  // Phase classifier
  phase_classifier_ = std::make_unique<PhaseClassifier>();

  // Constraint generator
  constraint_gen_ = std::make_unique<ConstraintGenerator>();

  // Rule library loader
  const std::string kConfigDir = get_parameter("config_dir").as_string();
  const std::string kRuleLibPath = kConfigDir + "/colregs_rule_library.yaml";
  try {
    RuleLibraryLoader loader(kRuleLibPath);
    rules_ = loader.load_colregs_rules();
  } catch (const std::exception& e) {
    const std::string kMsg = std::string("Failed to load COLREGs rules: ") + e.what();
    RCLCPP_FATAL(get_logger(), "%s", kMsg.c_str());
    throw std::runtime_error(kMsg);
  }

  if (rules_.empty()) {
    const std::string kMsg = "No COLREGs rules loaded from: " + kRuleLibPath;
    RCLCPP_FATAL(get_logger(), "%s", kMsg.c_str());
    throw std::runtime_error(kMsg);
  }

  RCLCPP_INFO(get_logger(), "Loaded %zu COLREGs rules from %s",
              rules_.size(), kRuleLibPath.c_str());
}

// ------------------------------------------------------------------
// Publishers
// ------------------------------------------------------------------

void ColregsReasonerNode::setup_publishers() {
  constraint_pub_ = create_publisher<l3_msgs::msg::COLREGsConstraint>(
    "/l3/m6/colregs_constraint",
    rclcpp::QoS(5).reliable());

  asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
    "/l3/asdr/record",
    rclcpp::QoS(50).reliable().transient_local());

  sat_pub_ = create_publisher<l3_msgs::msg::SATData>(
    "/l3/sat/data",
    rclcpp::SensorDataQoS().keep_last(2));
}

// ------------------------------------------------------------------
// Subscribers
// ------------------------------------------------------------------

void ColregsReasonerNode::setup_subscribers() {
  odd_sub_ = create_subscription<l3_msgs::msg::ODDState>(
    "/l3/m1/odd_state",
    rclcpp::QoS(10).reliable().transient_local(),
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    [this](const l3_msgs::msg::ODDState::SharedPtr kMsg) { on_odd_state(kMsg); });

  world_sub_ = create_subscription<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state",
    rclcpp::SensorDataQoS().keep_last(2),
    // NOLINTNEXTLINE(performance-unnecessary-value-param)
    [this](const l3_msgs::msg::WorldState::SharedPtr kMsg) { on_world_state(kMsg); });
}

// ------------------------------------------------------------------
// Timers
// ------------------------------------------------------------------

void ColregsReasonerNode::setup_timers() {
  const auto kReasoningPeriod = std::chrono::milliseconds(
    get_parameter("reasoning_period_ms").as_int());
  const auto kHealthPeriod = std::chrono::milliseconds(
    get_parameter("health_check_period_ms").as_int());
  const auto kAsdrPeriod = std::chrono::milliseconds(
    get_parameter("asdr_snapshot_period_ms").as_int());
  const auto kSatPeriod = std::chrono::milliseconds(
    get_parameter("sat_publish_period_ms").as_int());

  reasoning_timer_ = create_wall_timer(
    kReasoningPeriod, [this]() { run_reasoning(); });
  health_timer_ = create_wall_timer(
    kHealthPeriod, [this]() { check_health(); });
  asdr_timer_ = create_wall_timer(
    kAsdrPeriod, [this]() { publish_asdr_snapshot(); });
  sat_timer_ = create_wall_timer(
    kSatPeriod, [this]() { publish_sat_data(); });
}

// ------------------------------------------------------------------
// Logger
// ------------------------------------------------------------------

void ColregsReasonerNode::setup_logger() {
  logger_ = spdlog::get("m6_colregs_reasoner");
  if (!logger_) {
    logger_ = spdlog::stdout_color_mt("m6_colregs_reasoner");
  }
}

// ==================================================================
// Subscriber callbacks
// ==================================================================

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size,performance-unnecessary-value-param)
void ColregsReasonerNode::on_odd_state(const l3_msgs::msg::ODDState::SharedPtr kMsg) {
  const OddDomain kNewOdd = odd_domain_from_zone(kMsg->current_zone);
  const rclcpp::Time kNewStamp(kMsg->stamp);
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    current_odd_ = kNewOdd;
    last_odd_stamp_ = kNewStamp;
  }
  RCLCPP_DEBUG(get_logger(), "ODD state updated: zone=%s, health=%d, score=%.3f",
               odd_domain_str(kNewOdd).c_str(),
               static_cast<int>(kMsg->health),
               kMsg->conformance_score);
}

// NOLINTNEXTLINE(performance-unnecessary-value-param)
void ColregsReasonerNode::on_world_state(const l3_msgs::msg::WorldState::SharedPtr kMsg) {
  const rclcpp::Time kNewStamp(kMsg->stamp);
  const std::lock_guard<std::mutex> kLock(state_mutex_);
  last_world_state_ = *kMsg;
  last_world_stamp_ = kNewStamp;
}

// ==================================================================
// Timer callbacks
// ==================================================================

// ------------------------------------------------------------------
// run_reasoning()        2 Hz
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void ColregsReasonerNode::run_reasoning() {
  // Take a consistent snapshot of shared state under the lock
  std::optional<l3_msgs::msg::WorldState> ws_snapshot;
  rclcpp::Time ws_stamp;
  OddDomain domain{};
  float ws_confidence = 0.0F;
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    if (!last_world_state_.has_value()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
        "run_reasoning: no world state received yet");
      return;
    }
    ws_snapshot = last_world_state_;
    ws_stamp = last_world_stamp_;
    domain = current_odd_.value_or(OddDomain::ODD_A);
    ws_confidence = last_world_state_->confidence;
  }

  const rclcpp::Time kNowTime = this->now();
  const double kWorldAge = (kNowTime - ws_stamp).seconds();
  const double kTimeout = get_parameter("world_state_timeout_s").as_double();

  if (kWorldAge > kTimeout) {
    RCLCPP_WARN(get_logger(), "World state stale (%f s > %f s) — publishing degraded constraint",
                kWorldAge, kTimeout);

    l3_msgs::msg::COLREGsConstraint degraded;
    degraded.stamp = kNowTime;
    degraded.phase = "DEGRADED";
    degraded.confidence = 0.5F;
    degraded.rationale = "World state stale: age=" + std::to_string(kWorldAge) + "s";

    l3_msgs::msg::Constraint c;
    c.constraint_type = "colregs_degraded";
    c.description = "Stale world state, using last known configuration";
    c.numeric_value = kWorldAge;
    c.unit = "s";
    degraded.constraints.push_back(c);

    constraint_pub_->publish(degraded);
    publish_asdr_record("world_state_stale",
      std::string("{\"age_s\":") + std::to_string(kWorldAge) + "}");
    return;
  }

  // 1. Convert WorldState targets to geometric states
  const auto kTargetStates = convert_world_state(*ws_snapshot);

  // 2. Update cache
  target_cache_->update(kTargetStates);

  // 3. Determine RuleParameters from current ODD
  const RuleParameters kParams = get_current_rule_params();

  // 4. Run all rules against all targets; propagate target_id after each evaluate()
  std::vector<RuleEvaluation> evaluations;
  evaluations.reserve(kTargetStates.size() * rules_.size());

  for (const auto& target : kTargetStates) {
    for (const auto& rule : rules_) {
      auto eval = rule->evaluate(target, domain, kParams);
      eval.target_id = target.target_id;
      evaluations.push_back(eval);
    }
  }

  // 5. Generate and publish COLREGsConstraint
  auto constraint = constraint_gen_->generate(
    evaluations, kParams, static_cast<double>(ws_confidence));
  constraint.stamp = kNowTime;

  constraint_pub_->publish(constraint);

  RCLCPP_DEBUG(get_logger(), "Reasoning cycle: %zu targets, %zu evaluations, %zu active rules",
               kTargetStates.size(), evaluations.size(),
               constraint.active_rules.size());
}

// ------------------------------------------------------------------
// check_health()         1 Hz
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void ColregsReasonerNode::check_health() {
  const rclcpp::Time kNowTime = this->now();
  rclcpp::Time odd_stamp;
  rclcpp::Time world_stamp;
  bool world_received = false;
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    odd_stamp = last_odd_stamp_;
    world_stamp = last_world_stamp_;
    world_received = last_world_state_.has_value();
  }

  bool all_healthy = true;
  const double kOddTimeout = get_parameter("odd_state_timeout_s").as_double();
  const double kWorldTimeout = get_parameter("world_state_timeout_s").as_double();

  if (odd_stamp.nanoseconds() == 0) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
      "ODD state never received");
    all_healthy = false;
  } else {
    const double kOddAge = (kNowTime - odd_stamp).seconds();
    if (kOddAge > kOddTimeout) {
      RCLCPP_WARN(get_logger(), "ODD state stale (%f s > %f s)", kOddAge, kOddTimeout);
      all_healthy = false;
    }
  }

  if (!world_received) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
      "World state never received");
    all_healthy = false;
  } else {
    const double kWorldAge = (kNowTime - world_stamp).seconds();
    if (kWorldAge > kWorldTimeout) {
      RCLCPP_WARN(get_logger(), "World state stale (%f s > %f s)", kWorldAge, kWorldTimeout);
      all_healthy = false;
    }
  }

  if (!all_healthy) {
    const bool kOddStale = (odd_stamp.nanoseconds() != 0) &&
      ((kNowTime - odd_stamp).seconds() > kOddTimeout);
    const bool kWsStale = world_received &&
      ((kNowTime - world_stamp).seconds() > kWorldTimeout);
    publish_asdr_record("health_degraded",
      std::string(R"({"status":"degraded","odd_stale":)") +
      std::to_string(static_cast<int>(kOddStale)) +
      R"(,"world_stale":)" + std::to_string(static_cast<int>(kWsStale)) + "}");
  }
}

// ------------------------------------------------------------------
// publish_asdr_snapshot()   2 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::publish_asdr_snapshot() {
  std::string odd_str;
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    odd_str = current_odd_.has_value() ? odd_domain_str(*current_odd_) : "unknown";
  }

  const std::string kJson =
    std::string("{") +
    R"("target_count":)" + std::to_string(target_cache_->size()) + "," +
    R"("odd_domain":")" + odd_str + R"(",)" +
    R"("rules_loaded":)" + std::to_string(rules_.size()) +
    "}";

  publish_asdr_record("reasoner_snapshot", kJson);
}

// ------------------------------------------------------------------
// publish_sat_data()       10 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::publish_sat_data() {
  std::string odd_str;
  float ws_confidence = 0.0F;
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    odd_str = current_odd_.has_value() ? odd_domain_str(*current_odd_) : "unknown";
    ws_confidence = last_world_state_.has_value()
      ? last_world_state_->confidence : 0.0F;
  }

  l3_msgs::msg::SATData msg;
  msg.stamp = this->now();
  msg.source_module = "M6_COLREGs_Reasoner";

  msg.sat1.state_summary = "M6 active: targets=" +
    std::to_string(target_cache_->size()) + ", odd=" + odd_str;

  msg.sat2.trigger_reason = "periodic";
  msg.sat2.reasoning_chain = "";
  msg.sat2.system_confidence = ws_confidence;

  const double kTAct = get_current_rule_params().t_act_s;
  msg.sat3.forecast_horizon_s = kTAct;
  msg.sat3.predicted_state = "nominal";
  msg.sat3.prediction_uncertainty = 0.5F;
  msg.sat3.tdl_s = static_cast<float>(
    get_parameter("reasoning_period_ms").as_int()) / 1000.0F;
  msg.sat3.tmr_s = 60.0F;

  sat_pub_->publish(msg);
}

// ==================================================================
// Helpers
// ==================================================================

void ColregsReasonerNode::publish_asdr_record(
    const std::string& type, const std::string& json) {
  l3_msgs::msg::ASDRRecord msg;
  msg.stamp = this->now();
  msg.source_module = "M6_COLREGs_Reasoner";
  msg.decision_type = type;
  msg.decision_json = json;
  // msg.signature: SHA-256 not computed in Phase E1 — field left empty
  asdr_pub_->publish(msg);
}

// ------------------------------------------------------------------
// World state to target geometric state conversion
// ------------------------------------------------------------------

std::vector<TargetGeometricState>
// NOLINTNEXTLINE(readability-convert-member-functions-to-static,readability-function-cognitive-complexity,readability-function-size)
ColregsReasonerNode::convert_world_state(
    const l3_msgs::msg::WorldState& ws) const {
  std::vector<TargetGeometricState> result;
  result.reserve(ws.targets.size());

  const double kOwnLat = ws.own_ship.position.latitude;
  const double kOwnLon = ws.own_ship.position.longitude;
  const double kOwnHeading = ws.own_ship.heading_deg;
  const double kOwnSpeed = ws.own_ship.sog_kn;

  for (const auto& tgt : ws.targets) {
    TargetGeometricState gs{};

    gs.target_id = tgt.target_id;

    // Absolute bearing from ownship to target [0, 360)
    const double kBearingToTarget = bearing_between(
      kOwnLat, kOwnLon,
      tgt.position.latitude, tgt.position.longitude);
    gs.bearing_deg = normalize_bearing_deg(kBearingToTarget);

    // Target's true heading [0, 360)
    gs.target_heading_deg = normalize_bearing_deg(tgt.heading_deg);

    // Aspect angle: angle from target's bow to the bearing-from-target-to-ownship
    // Convention: 0° = target facing us (red), 180° = target facing away (stern)
    gs.aspect_deg = normalize_bearing_deg(gs.target_heading_deg - kBearingToTarget + 180.0);

    // Relative speed: component along LOS using absolute bearing
    const double kBearingRad = deg2rad(kBearingToTarget);
    const double kOwnCogRad = deg2rad(ws.own_ship.cog_deg);
    const double kTgtCogRad = deg2rad(tgt.cog_deg);
    gs.relative_speed_kn =
      tgt.sog_kn * std::cos(kTgtCogRad - kBearingRad) -
      kOwnSpeed * std::cos(kOwnCogRad - kBearingRad);

    // CPA / TCPA from M2
    gs.cpa_m = tgt.cpa_m;
    gs.tcpa_s = tgt.tcpa_s;

    // Ownship state
    gs.ownship_heading_deg = kOwnHeading;
    gs.ownship_speed_kn = kOwnSpeed;

    // Ship type priority from classification string
    gs.target_ship_type_priority = classify_ship_priority(tgt.classification);

    // Timestamp
    gs.stamp = to_chrono(tgt.stamp);

    result.push_back(gs);
  }

  return result;
}

// ------------------------------------------------------------------
// Get current ODD-aware RuleParameters
// ------------------------------------------------------------------

RuleParameters ColregsReasonerNode::get_current_rule_params() const {
  OddDomain domain{};
  {
    const std::lock_guard<std::mutex> kLock(state_mutex_);
    domain = current_odd_.value_or(OddDomain::ODD_A);
  }

  auto it = odd_thresholds_.find(domain);
  if (it != odd_thresholds_.end()) {
    return it->second;
  }

  // Fallback defaults (ODD-A baseline)
  RuleParameters defaults{};
  defaults.t_standOn_s = 480.0;
  defaults.t_act_s = 240.0;
  defaults.t_emergency_s = 60.0;
  defaults.min_alteration_deg = 15.0;
  defaults.cpa_safe_m = 1852.0;
  defaults.max_turn_rate_deg_s = 5.0;
  defaults.rule_9_weight = 0.0;
  return defaults;
}

}  // namespace mass_l3::m6_colregs
