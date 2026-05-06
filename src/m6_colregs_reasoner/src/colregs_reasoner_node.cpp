#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include <yaml-cpp/yaml.h>

#include "m6_colregs_reasoner/error_codes.hpp"
#include "m6_colregs_reasoner/geometry_utils.hpp"

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
  if (classification == "vessel") return 1;
  if (classification == "fixed_object") return 0;
  return -1;  // unknown
}

/// Compute initial great-circle bearing (degrees) from point A to point B.
double bearing_between(double lat_a_deg, double lon_a_deg,
                       double lat_b_deg, double lon_b_deg) {
  const double lat_a = deg2rad(lat_a_deg);
  const double lat_b = deg2rad(lat_b_deg);
  const double d_lon = deg2rad(lon_b_deg - lon_a_deg);

  const double y = std::sin(d_lon) * std::cos(lat_b);
  const double x = std::cos(lat_a) * std::sin(lat_b)
                 - std::sin(lat_a) * std::cos(lat_b) * std::cos(d_lon);
  return normalize_bearing_deg(rad2deg(std::atan2(y, x)));
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

ColregsReasonerNode::ColregsReasonerNode()
  : Node("m6_colregs_reasoner"),
    last_world_stamp_(0, 0, RCL_ROS_TIME),
    last_odd_stamp_(0, 0, RCL_ROS_TIME) {
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

void ColregsReasonerNode::load_odd_thresholds() {
  const std::string config_dir = get_parameter("config_dir").as_string();
  const std::string yaml_path = config_dir + "/odd_aware_thresholds.yaml";

  YAML::Node doc;
  try {
    doc = YAML::LoadFile(yaml_path);
  } catch (const std::exception& e) {
    RCLCPP_WARN(get_logger(), "Cannot load odd_aware_thresholds.yaml: %s", e.what());
    return;
  }

  // Four ODD zones
  const std::vector<OddDomain> zones = {
    OddDomain::ODD_A, OddDomain::ODD_B,
    OddDomain::ODD_C, OddDomain::ODD_D
  };

  for (const auto& zone : zones) {
    const std::string key = odd_yaml_key(zone);
    const YAML::Node node = doc[key];
    if (!node) {
      RCLCPP_WARN(get_logger(), "Missing ODD config section: %s", key.c_str());
      continue;
    }

    RuleParameters params{};
    params.t_standOn_s        = node["t_standOn_s"].as<double>(480.0);
    params.t_act_s            = node["t_act_s"].as<double>(240.0);
    params.t_emergency_s      = node["t_emergency_s"].as<double>(60.0);
    params.min_alteration_deg = node["min_alteration_deg"].as<double>(15.0);
    params.cpa_safe_m         = node["cpa_safe_m"].as<double>(1852.0);
    params.max_turn_rate_deg_s = 5.0;    // default, [TBD-HAZID]
    params.rule_9_weight       = 0.0;    // default, not used in Phase E1

    odd_thresholds_[zone] = params;
    RCLCPP_DEBUG(get_logger(), "Loaded thresholds for %s: act=%f, emergency=%f",
                 key.c_str(), params.t_act_s, params.t_emergency_s);
  }
}

// ------------------------------------------------------------------
// Create components
// ------------------------------------------------------------------

void ColregsReasonerNode::create_components() {
  // Target state cache
  TargetStateCache::Config cache_cfg{};
  cache_cfg.max_targets = get_parameter("max_targets").as_int();
  target_cache_ = std::make_unique<TargetStateCache>(cache_cfg);

  // Phase classifier
  phase_classifier_ = std::make_unique<PhaseClassifier>();

  // Constraint generator
  constraint_gen_ = std::make_unique<ConstraintGenerator>();

  // Rule library loader
  const std::string config_dir = get_parameter("config_dir").as_string();
  const std::string rule_lib_path = config_dir + "/colregs_rule_library.yaml";
  try {
    RuleLibraryLoader loader(rule_lib_path);
    rules_ = loader.load_colregs_rules();
    RCLCPP_INFO(get_logger(), "Loaded %zu COLREGs rules from %s",
                rules_.size(), rule_lib_path.c_str());
  } catch (const std::exception& e) {
    RCLCPP_WARN(get_logger(), "Failed to load COLREGs rules: %s", e.what());
    rules_.clear();
  }
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
    [this](const l3_msgs::msg::ODDState::SharedPtr msg) { on_odd_state(msg); });

  world_sub_ = create_subscription<l3_msgs::msg::WorldState>(
    "/l3/m2/world_state",
    rclcpp::SensorDataQoS().keep_last(2),
    [this](const l3_msgs::msg::WorldState::SharedPtr msg) { on_world_state(msg); });
}

// ------------------------------------------------------------------
// Timers
// ------------------------------------------------------------------

void ColregsReasonerNode::setup_timers() {
  const auto reasoning_period = std::chrono::milliseconds(
    get_parameter("reasoning_period_ms").as_int());
  const auto health_period = std::chrono::milliseconds(
    get_parameter("health_check_period_ms").as_int());
  const auto asdr_period = std::chrono::milliseconds(
    get_parameter("asdr_snapshot_period_ms").as_int());
  const auto sat_period = std::chrono::milliseconds(
    get_parameter("sat_publish_period_ms").as_int());

  reasoning_timer_ = create_wall_timer(
    reasoning_period, [this]() { run_reasoning(); });
  health_timer_ = create_wall_timer(
    health_period, [this]() { check_health(); });
  asdr_timer_ = create_wall_timer(
    asdr_period, [this]() { publish_asdr_snapshot(); });
  sat_timer_ = create_wall_timer(
    sat_period, [this]() { publish_sat_data(); });
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

void ColregsReasonerNode::on_odd_state(
    const l3_msgs::msg::ODDState::SharedPtr msg) {
  current_odd_ = odd_domain_from_zone(msg->current_zone);

  // Convert ROS2 Time stamp
  last_odd_stamp_ = rclcpp::Time(msg->stamp);

  RCLCPP_DEBUG(get_logger(), "ODD state updated: zone=%s, health=%d, score=%.3f",
               odd_domain_str(*current_odd_).c_str(),
               static_cast<int>(msg->health),
               msg->conformance_score);
}

void ColregsReasonerNode::on_world_state(
    const l3_msgs::msg::WorldState::SharedPtr msg) {
  last_world_state_ = *msg;
  last_world_stamp_ = rclcpp::Time(msg->stamp);
}

// ==================================================================
// Timer callbacks
// ==================================================================

// ------------------------------------------------------------------
// run_reasoning()        2 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::run_reasoning() {
  if (!last_world_state_.has_value()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
      "run_reasoning: no world state received yet");
    return;
  }

  const auto now = now();
  const double world_age = (now - last_world_stamp_).seconds();
  const double timeout = get_parameter("world_state_timeout_s").as_double();

  if (world_age > timeout) {
    // Publish degraded constraint on stale world state
    RCLCPP_WARN(get_logger(), "World state stale (%f s > %f s) — publishing degraded constraint",
                world_age, timeout);

    l3_msgs::msg::COLREGsConstraint degraded;
    degraded.stamp = now;
    degraded.phase = "DEGRADED";
    degraded.confidence = 0.5f;
    degraded.rationale = "World state stale: age=" + std::to_string(world_age) + "s";

    l3_msgs::msg::Constraint c;
    c.constraint_type = "colregs_degraded";
    c.description = "Stale world state, using last known configuration";
    c.numeric_value = world_age;
    c.unit = "s";
    degraded.constraints.push_back(c);

    constraint_pub_->publish(degraded);
    publish_asdr_record("world_state_stale",
      "{\"age_s\":" + std::to_string(world_age) + "}");
    return;
  }

  // 1. Convert WorldState targets to geometric states
  const auto target_states = convert_world_state(*last_world_state_);

  // 2. Update cache
  target_cache_->update(target_states);

  // 3. Determine RuleParameters from current ODD
  const RuleParameters params = get_current_rule_params();

  // 4. Run all rules against all targets
  const OddDomain domain = current_odd_.value_or(OddDomain::ODD_A);
  std::vector<RuleEvaluation> evaluations;
  evaluations.reserve(target_states.size() * rules_.size());

  for (const auto& target : target_states) {
    for (const auto& rule : rules_) {
      auto eval = rule->evaluate(target, domain, params);
      evaluations.push_back(eval);
    }
  }

  // 5. Generate and publish COLREGsConstraint
  auto constraint = constraint_gen_->generate(
    evaluations, params,
    static_cast<double>(last_world_state_->confidence));
  constraint.stamp = now;

  constraint_pub_->publish(constraint);

  RCLCPP_DEBUG(get_logger(), "Reasoning cycle: %zu targets, %zu evaluations, %zu active rules",
               target_states.size(), evaluations.size(),
               constraint.active_rules.size());
}

// ------------------------------------------------------------------
// check_health()         1 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::check_health() {
  const auto now = now();
  bool all_healthy = true;

  // ODD state freshness
  const double odd_timeout = get_parameter("odd_state_timeout_s").as_double();
  if (last_odd_stamp_.nanoseconds() == 0) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
      "ODD state never received");
    all_healthy = false;
  } else {
    const double odd_age = (now - last_odd_stamp_).seconds();
    if (odd_age > odd_timeout) {
      RCLCPP_WARN(get_logger(), "ODD state stale (%f s > %f s)",
                  odd_age, odd_timeout);
      all_healthy = false;
    }
  }

  // World state freshness
  const double world_timeout = get_parameter("world_state_timeout_s").as_double();
  if (!last_world_state_.has_value()) {
    RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 10000,
      "World state never received");
    all_healthy = false;
  } else {
    const double world_age = (now - last_world_stamp_).seconds();
    if (world_age > world_timeout) {
      RCLCPP_WARN(get_logger(), "World state stale (%f s > %f s)",
                  world_age, world_timeout);
      all_healthy = false;
    }
  }

  if (!all_healthy) {
    publish_asdr_record("health_degraded",
      "{\"status\":\"degraded\",\"odd_stale\":" +
      std::to_string(last_odd_stamp_.nanoseconds() != 0 &&
        (now - last_odd_stamp_).seconds() > odd_timeout) +
      ",\"world_stale\":" +
      std::to_string(last_world_state_.has_value() &&
        (now - last_world_stamp_).seconds() > world_timeout) +
      "}");
  }
}

// ------------------------------------------------------------------
// publish_asdr_snapshot()   2 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::publish_asdr_snapshot() {
  const std::string odd_str = current_odd_.has_value()
    ? odd_domain_str(*current_odd_)
    : "unknown";

  const std::string json =
    std::string("{") +
    "\"target_count\":" + std::to_string(target_cache_->size()) + "," +
    "\"odd_domain\":\"" + odd_str + "\"," +
    "\"rules_loaded\":" + std::to_string(rules_.size()) +
    "}";

  publish_asdr_record("reasoner_snapshot", json);
}

// ------------------------------------------------------------------
// publish_sat_data()       10 Hz
// ------------------------------------------------------------------

void ColregsReasonerNode::publish_sat_data() {
  l3_msgs::msg::SATData msg;
  msg.stamp = now();
  msg.source_module = "M6_COLREGs_Reasoner";

  // SAT1: current state
  const std::string odd_str = current_odd_.has_value()
    ? odd_domain_str(*current_odd_)
    : "unknown";
  msg.sat1.state_summary = "M6 active: targets=" +
    std::to_string(target_cache_->size()) +
    ", odd=" + odd_str;

  // SAT2: reasoning — periodic (no trigger)
  msg.sat2.trigger_reason = "periodic";
  msg.sat2.reasoning_chain = "";
  msg.sat2.system_confidence = last_world_state_.has_value()
    ? last_world_state_->confidence : 0.0f;

  // SAT3: forecast
  const double t_act = get_current_rule_params().t_act_s;
  msg.sat3.forecast_horizon_s = t_act;
  msg.sat3.predicted_state = "nominal";
  msg.sat3.prediction_uncertainty = 0.5f;
  msg.sat3.tdl_s = static_cast<float>(
    get_parameter("reasoning_period_ms").as_int()) / 1000.0f;
  msg.sat3.tmr_s = 60.0f;

  sat_pub_->publish(msg);
}

// ==================================================================
// Helpers
// ==================================================================

void ColregsReasonerNode::publish_asdr_record(
    const std::string& type, const std::string& json) {
  l3_msgs::msg::ASDRRecord msg;
  msg.stamp = now();
  msg.source_module = "M6_COLREGs_Reasoner";
  msg.decision_type = type;
  msg.decision_json = json;
  // SHA-256 signature not computed in Phase E1
  asdr_pub_->publish(msg);
}

// ------------------------------------------------------------------
// World state to target geometric state conversion
// ------------------------------------------------------------------

std::vector<TargetGeometricState>
ColregsReasonerNode::convert_world_state(
    const l3_msgs::msg::WorldState& ws) const {
  std::vector<TargetGeometricState> result;
  result.reserve(ws.targets.size());

  const double own_lat = ws.own_ship.position.latitude;
  const double own_lon = ws.own_ship.position.longitude;
  const double own_heading = ws.own_ship.heading_deg;
  const double own_speed = ws.own_ship.sog_kn;

  for (const auto& tgt : ws.targets) {
    TargetGeometricState gs{};

    gs.target_id = tgt.target_id;

    // Compute bearing from ownship to target
    const double bearing_to_target = bearing_between(
      own_lat, own_lon,
      tgt.position.latitude, tgt.position.longitude);

    // Use M2's pre-classified bearing/aspect if available; else compute
    gs.bearing_deg = normalize_bearing_deg(bearing_to_target - own_heading);
    gs.aspect_deg = normalize_bearing_deg(
      tgt.heading_deg - own_heading - gs.bearing_deg + 180.0);

    // Relative speed: component along line-of-sight
    const double bearing_rad = deg2rad(gs.bearing_deg);
    const double own_cog_rad = deg2rad(ws.own_ship.cog_deg);
    const double tgt_cog_rad = deg2rad(tgt.cog_deg);
    gs.relative_speed_kn =
      tgt.sog_kn * std::cos(tgt_cog_rad - bearing_rad) -
      own_speed * std::cos(own_cog_rad - bearing_rad);

    // CPA / TCPA from M2
    gs.cpa_m = tgt.cpa_m;
    gs.tcpa_s = tgt.tcpa_s;

    // Ownship state
    gs.ownship_heading_deg = own_heading;
    gs.ownship_speed_kn = own_speed;

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
  const OddDomain domain = current_odd_.value_or(OddDomain::ODD_A);

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
