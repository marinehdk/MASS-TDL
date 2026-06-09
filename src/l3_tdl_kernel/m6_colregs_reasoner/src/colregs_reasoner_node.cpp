#include "m6_colregs_reasoner/colregs_reasoner_node.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <l3_msgs/msg/asdr_record.hpp>
#include <l3_msgs/msg/colre_gs_constraint.hpp>
#include <l3_msgs/msg/colregs_chain_layer.hpp>
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

std::string encounter_type_str(EncounterType enc) {
  switch (enc) {
    case EncounterType::HEAD_ON: return "Rule14 HEAD-ON";
    case EncounterType::OVERTAKING: return "Rule13 OVERTAKING";
    case EncounterType::CROSSING: return "Rule15 CROSSING";
    case EncounterType::RESTRICTED_VIS: return "Rule19 RESTRICTED-VIS";
    case EncounterType::AMBIGUOUS: return "AMBIGUOUS";
    default: return "NONE";
  }
}

std::string role_str(Role r) {
  switch (r) {
    case Role::GIVE_WAY: return "GIVE-WAY";
    case Role::STAND_ON: return "STAND-ON";
    case Role::BOTH_GIVE_WAY: return "BOTH-GIVE-WAY";
    default: return "FREE";
  }
}

l3_msgs::msg::ColregsChainLayer make_chain_layer(
    uint8_t layer_num, const std::string& label, const std::string& conclusion,
    const std::vector<std::string>& keys, const std::vector<std::string>& vals,
    float confidence, uint8_t timing_stage, bool escalation, const std::string& rationale) {
  l3_msgs::msg::ColregsChainLayer layer;
  layer.layer = layer_num;
  layer.label = label;
  layer.conclusion = conclusion;
  layer.input_keys = keys;
  layer.input_vals = vals;
  layer.confidence = confidence;
  layer.timing_stage = timing_stage;
  layer.escalation = escalation;
  layer.rationale = rationale;
  return layer;
}

std::string encounter_geometry_str(EncounterType enc) {
  switch (enc) {
    case EncounterType::HEAD_ON: return "HEAD-ON";
    case EncounterType::OVERTAKING: return "OVERTAKING";
    case EncounterType::CROSSING: return "CROSSING";
    case EncounterType::RESTRICTED_VIS: return "RESTRICTED-VIS";
    case EncounterType::AMBIGUOUS: return "AMBIGUOUS";
    default: return "NONE";
  }
}

std::string role_action_str(Role r) {
  switch (r) {
    case Role::GIVE_WAY: return "give_way";
    case Role::STAND_ON: return "stand_on";
    case Role::BOTH_GIVE_WAY: return "both_give_way";
    default: return "free";
  }
}

float compute_geometry_clarity(double relative_bearing_deg, EncounterType enc_type) {
  constexpr double kHeadOnHalfWidth = 22.5;
  constexpr double kCrossingInner = 22.5;
  constexpr double kCrossingOuter = 112.5;
  constexpr double kOvertakingStern = 247.5;

  double rb = relative_bearing_deg;
  if (rb > 180.0) rb = 360.0 - rb;

  double dist_to_boundary = 0.0;
  double max_dist = 45.0;

  switch (enc_type) {
    case EncounterType::HEAD_ON:
      dist_to_boundary = kHeadOnHalfWidth - std::min(rb, kHeadOnHalfWidth);
      max_dist = kHeadOnHalfWidth;
      break;
    case EncounterType::CROSSING:
      if (rb >= kCrossingInner && rb <= kCrossingOuter) {
        dist_to_boundary = std::min(rb - kCrossingInner, kCrossingOuter - rb);
      }
      max_dist = (kCrossingOuter - kCrossingInner) / 2.0;
      break;
    case EncounterType::OVERTAKING:
      if (rb >= kCrossingOuter && rb <= kOvertakingStern) {
        dist_to_boundary = std::min(rb - kCrossingOuter, kOvertakingStern - rb);
      }
      max_dist = (kOvertakingStern - kCrossingOuter) / 2.0;
      break;
    default:
      return 0.5f;
  }

  float clarity = 0.5f + 0.5f * static_cast<float>(
      std::max(0.0, dist_to_boundary) / std::max(1.0, max_dist));
  return std::min(clarity, 1.0f);
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
// Static helpers
// ------------------------------------------------------------------

std::string ColregsReasonerNode::odd_domain_str(OddDomain d) {
  switch (d) {
    case OddDomain::ODD_A: return "ODD-A";
    case OddDomain::ODD_B: return "ODD-B";
    case OddDomain::ODD_C: return "ODD-C";
    case OddDomain::ODD_D: return "ODD-D";
    default: return "ODD-UNKNOWN";
  }
}

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

  rule_assessment_pub_ = create_publisher<l3_msgs::msg::RuleAssessment>(
    "/l3/m6/rule_assessment",
    rclcpp::QoS(10).reliable());
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

  reasoning_timer_ = rclcpp::create_timer(
    get_node_base_interface(),
    get_node_timers_interface(),
    get_clock(),
    kReasoningPeriod, [this]() { run_reasoning(); });
  health_timer_ = rclcpp::create_timer(
    get_node_base_interface(),
    get_node_timers_interface(),
    get_clock(),
    kHealthPeriod, [this]() { check_health(); });
  asdr_timer_ = rclcpp::create_timer(
    get_node_base_interface(),
    get_node_timers_interface(),
    get_clock(),
    kAsdrPeriod, [this]() { publish_asdr_snapshot(); });
  sat_timer_ = rclcpp::create_timer(
    get_node_base_interface(),
    get_node_timers_interface(),
    get_clock(),
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

  double dt_s = 0.5;
  if (prev_world_stamp_.nanoseconds() > 0) {
    dt_s = (ws_stamp - prev_world_stamp_).seconds();
    // New-run detection: world_state is stamped with sim time, which resets to ~0
    // on a fresh scenario (cleanup→configure→activate). A large backward jump means
    // a new run started — drop all cross-run encounter state so a prior run's latched
    // give-way (or stale range history) cannot bleed into the next scenario's onset.
    if (dt_s < -1.0) {
      rule_latches_.clear();
      prev_target_range_.clear();
      prev_target_bearing_.clear();
      RCLCPP_INFO(get_logger(),
        "New run detected (sim time %.1fs → %.1fs) — cleared cross-run latch/history state",
        prev_world_stamp_.seconds(), ws_stamp.seconds());
    }
  }
  prev_world_stamp_ = ws_stamp;
  if (dt_s <= 0.0) {
    dt_s = 0.5;
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
    uint32_t mmsi = static_cast<uint32_t>(target.target_id);
    for (const auto& rule : rules_) {
      auto eval = rule->evaluate(target, domain, kParams);
      eval.target_id = target.target_id;
      
      // Onset-latched hysteresis for Rules 14 (head-on) and 15 (crossing):
      // hold encounter classification through own-ship's avoidance maneuver
      // (Rule 13(d)) so we don't chatter back to TRANSIT and U-turn.
      const int rid = rule->rule_id();
      if (rid == 14 || rid == 15) {
        const uint64_t key = (static_cast<uint64_t>(mmsi) << 8) | static_cast<uint64_t>(rid);
        auto it = rule_latches_.find(key);
        if (it == rule_latches_.end()) {
          it = rule_latches_.emplace(key, RuleLatch{kParams.cpa_safe_m, 1.5}).first;
        }
        // Look up current range from ws_snapshot (TargetGeometricState lacks rng_m)
        double current_rng = -1.0;
        for (const auto& tgt_snap : ws_snapshot->targets) {
          if (static_cast<uint32_t>(tgt_snap.target_id) == mmsi) {
            current_rng = tgt_snap.rng_m;
            break;
          }
        }
        const bool range_closing = (prev_target_range_.count(mmsi) > 0) &&
            (current_rng >= 0.0 && current_rng < prev_target_range_[mmsi]);
        // Rule 16 "finally past and clear": target has drawn abaft the beam.
        // Relative bearing of the target from own-ship heading, normalized [-180,180].
        double rel_brg = target.bearing_deg - target.ownship_heading_deg;
        while (rel_brg > 180.0) rel_brg -= 360.0;
        while (rel_brg < -180.0) rel_brg += 360.0;
        const bool past_and_clear = std::fabs(rel_brg) > 112.5;  // 2 points abaft the beam
        // Pass the raw evaluation so the latch can snapshot the give-way
        // classification at the latching cycle (Rule 13(d): fixed at onset).
        const bool latched = it->second.update(
            eval.is_active, target.cpa_m, range_closing, past_and_clear, &eval);
        if (latched) {
          if (!eval.is_active) {
            // Raw geometry fell out of the rule cone mid-maneuver (own ship's own
            // starboard turn rotated the target off the ±6° head-on axis), so it
            // re-evaluated to role=FREE. Hold the ONSET give-way classification so
            // requires_action()/conflict_detected stay stable through the maneuver
            // until finally past and clear (Rule 8(d)) — otherwise conflict_detected
            // is carried by flickering secondary rules and M4 flaps AVOID↔TRANSIT.
            it->second.apply_onset(eval);
            eval.rationale += " [latched: onset classification held]";
          }
        } else {
          eval.is_active = false;
        }
      } else {
        // COLREG Rule 7 (risk of collision): a rule obligation applies ONLY
        // when risk of collision exists. Non-latched rules (e.g. Rule 18
        // priority give-way, Rule 13 overtaking, Rule 17 stand-on) must NOT
        // fire for a target that poses no risk — already passed CPA
        // (tcpa < 0) or will clear (cpa ≥ cpa_safe). Without this gate a
        // higher-priority vessel far astern and diverging keeps Rule 18
        // give-way active forever → conflict_detected stuck → M4 holds
        // give-way → avoidance never releases → no route return.
        const bool risk_of_collision =
            (target.tcpa_s >= 0.0) && (target.cpa_m < kParams.cpa_safe_m);
        if (!risk_of_collision) {
          eval.is_active = false;
        }
      }

      evaluations.push_back(eval);
    }
  }

  // 5. Generate and publish COLREGsConstraint
  auto constraint = constraint_gen_->generate(
    evaluations, kParams, static_cast<double>(ws_confidence));
  constraint.stamp = kNowTime;
  const auto kChain = build_colregs_chain(evaluations, domain, kParams, kTargetStates);
  constraint.colregs_chain = kChain.layers;
  constraint.colregs_chain_target_id = kChain.target_id;

  constraint_pub_->publish(constraint);

  // W5 Head-On classifier and RuleAssessment
  uint32_t primary_target_mmsi = 0;
  double min_cpa = std::numeric_limits<double>::max();
  for (const auto& tgt : ws_snapshot->targets) {
    if (tgt.cpa_m < min_cpa) {
      min_cpa = tgt.cpa_m;
      primary_target_mmsi = static_cast<uint32_t>(tgt.target_id);
    }
  }

  // Update target history for range-closing detection (used by RuleLatch)
  for (const auto& tgt : ws_snapshot->targets) {
    prev_target_bearing_[static_cast<uint32_t>(tgt.target_id)] = tgt.brg_deg;
    prev_target_range_[static_cast<uint32_t>(tgt.target_id)] = tgt.rng_m;
  }

  if (primary_target_mmsi > 0) {
    const uint64_t kKey = (static_cast<uint64_t>(primary_target_mmsi) << 8) | 14ULL;
    auto it = rule_latches_.find(kKey);
    if (it != rule_latches_.end() && it->second.latched()) {
      l3_msgs::msg::RuleAssessment assessment;
      assessment.stamp = kNowTime;
      assessment.target_mmsi = primary_target_mmsi;
      assessment.applicable_rule = "Rule 14";
      assessment.expected_action = "turn_starboard";
      assessment.confidence = 0.91f;
      assessment.trigger_conditions = {
        "heading_diff < 22.5°",
        "bearing_rate < 0.5°/min",
        "range_closing"
      };
      rule_assessment_pub_->publish(assessment);
    }
  }

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

// ------------------------------------------------------------------
// build_colregs_chain() — construct 5-layer decision rationale
// ------------------------------------------------------------------

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
ColregsReasonerNode::ColregsChainResult ColregsReasonerNode::build_colregs_chain(
    const std::vector<RuleEvaluation>& evals, OddDomain domain,
    const RuleParameters& params,
    const std::vector<TargetGeometricState>& targets) {
  ColregsChainResult result;
  if (evals.empty() || targets.empty()) return result;

  const TargetGeometricState* primary_geo = nullptr;
  double min_cpa = std::numeric_limits<double>::max();
  for (const auto& t : targets) {
    if (t.cpa_m < min_cpa) { min_cpa = t.cpa_m; primary_geo = &t; }
  }
  if (!primary_geo) return result;

  const RuleEvaluation* primary_eval = nullptr;
  for (const auto& ev : evals) {
    if (ev.is_active && ev.target_id == primary_geo->target_id) {
      primary_eval = &ev; break;
    }
  }
  if (!primary_eval) {
    for (const auto& ev : evals) {
      if (ev.is_active) { primary_eval = &ev; break; }
    }
  }
  if (!primary_eval) return result;

  result.target_id = std::to_string(primary_geo->target_id);
  result.layers.resize(5);

  const double kRelBearing = normalize_bearing_deg(
      primary_geo->bearing_deg - primary_geo->ownship_heading_deg);
  const std::string kRuleIdStr = "Rule" + std::to_string(primary_eval->rule_id);
  const std::string kEncStr = encounter_type_str(primary_eval->encounter_type);

  std::vector<const RuleEvaluation*> primary_evals;
  for (const auto& ev : evals) {
    if (ev.is_active && ev.target_id == primary_geo->target_id) {
      primary_evals.push_back(&ev);
    }
  }

  result.layers[0] = make_chain_layer(1, "rule_identification", kRuleIdStr,
      {"encounter_type", "odd_domain"},
      {kEncStr, odd_domain_str(domain)},
      1.0f, 0, false,
      "Encounter classified as " + kEncStr + ", applicable " + kRuleIdStr);

  const float kGeoClarity = compute_geometry_clarity(
      kRelBearing, primary_eval->encounter_type);
  result.layers[1] = make_chain_layer(2, "geometric_classification",
      encounter_geometry_str(primary_eval->encounter_type),
      {"relative_bearing_deg", "aspect_deg", "cpa_m", "tcpa_s"},
      {std::to_string(static_cast<int>(kRelBearing)),
       std::to_string(static_cast<int>(primary_geo->aspect_deg)),
       std::to_string(static_cast<int>(primary_geo->cpa_m)),
       std::to_string(static_cast<int>(primary_geo->tcpa_s))},
      kGeoClarity, 0, false,
      "Relative bearing " + std::to_string(static_cast<int>(kRelBearing)) +
      "\u00B0, aspect " + std::to_string(static_cast<int>(primary_geo->aspect_deg)) +
      "\u00B0, CPA " + std::to_string(static_cast<int>(primary_geo->cpa_m)) + "m");

  result.layers[2] = make_chain_layer(3, "action_determination",
      role_action_str(primary_eval->role),
      {"role", "rule"},
      {role_str(primary_eval->role), kRuleIdStr},
      1.0f, 0, false,
      "Own vessel is " + role_str(primary_eval->role) + " per " + kRuleIdStr);

  std::string priority_conclusion;
  float priority_confidence = 1.0f;
  std::string priority_rationale;
  if (primary_evals.size() <= 1) {
    priority_conclusion = "Single rule applicable";
    priority_rationale = "Only " + kRuleIdStr + " is active for primary target";
  } else {
    std::string rules_list;
    for (const auto* ev : primary_evals) {
      if (!rules_list.empty()) rules_list += " > ";
      rules_list += "Rule" + std::to_string(ev->rule_id);
    }
    priority_conclusion = "Priority: " + rules_list;
    priority_confidence = 0.8f;
    priority_rationale = "Multiple rules active, resolved by COLREGs priority hierarchy";
  }
  result.layers[3] = make_chain_layer(4, "priority_resolution", priority_conclusion,
      {"active_rule_count"}, {std::to_string(primary_evals.size())},
      priority_confidence, 0, false, priority_rationale);

  bool is_compliant = true;
  std::string non_compliance_reason;
  float compliance_score = 1.0f;

  if (primary_eval->role == Role::GIVE_WAY) {
    if (primary_geo->cpa_m < params.cpa_safe_m && primary_geo->tcpa_s > 0) {
      is_compliant = false;
      non_compliance_reason = "CPA " + std::to_string(static_cast<int>(primary_geo->cpa_m)) +
          "m below safe threshold " + std::to_string(static_cast<int>(params.cpa_safe_m)) + "m";
      compliance_score = (params.cpa_safe_m > 0.0)
          ? std::max(0.0f, static_cast<float>(primary_geo->cpa_m) /
                            static_cast<float>(params.cpa_safe_m))
          : 0.0f;
    }
  } else if (primary_eval->role == Role::STAND_ON) {
    if (primary_eval->phase == TimingPhase::INDEPENDENT_ACTION ||
        primary_eval->phase == TimingPhase::CRITICAL_ACTION) {
      is_compliant = false;
      non_compliance_reason = "Stand-on vessel should take independent action per Rule 17(a)(ii)";
      compliance_score = 0.5f;
    }
  }

  bool escalation = (primary_eval->phase == TimingPhase::INDEPENDENT_ACTION ||
                     primary_eval->phase == TimingPhase::CRITICAL_ACTION);
  result.layers[4] = make_chain_layer(5, "compliance_check",
      is_compliant ? "Compliant" : "Non-compliant",
      {"cpa_m", "cpa_safe_m"},
      {std::to_string(static_cast<int>(primary_geo->cpa_m)),
       std::to_string(static_cast<int>(params.cpa_safe_m))},
      compliance_score,
      static_cast<uint8_t>(primary_eval->phase) + 1,
      escalation,
      is_compliant ? "Compliant" : "Non-compliant: " + non_compliance_reason);

  return result;
}

ColregsReasonerNode::ColregsChainResult ColregsReasonerNode::test_build_colregs_chain(
    const std::vector<RuleEvaluation>& evals, OddDomain domain,
    const RuleParameters& params,
    const std::vector<TargetGeometricState>& targets) {
  return ColregsReasonerNode::build_colregs_chain(evals, domain, params, targets);
}

}  // namespace mass_l3::m6_colregs
