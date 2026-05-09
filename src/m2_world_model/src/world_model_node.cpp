#include "m2_world_model/world_model_node.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <builtin_interfaces/msg/time.hpp>
#include <spdlog/async.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "l3_msgs/msg/sat1_data.hpp"
#include "l3_msgs/msg/sat2_data.hpp"
#include "l3_msgs/msg/sat3_data.hpp"

#include "m2_world_model/detail/time_utils.hpp"

namespace mass_l3::m2 {

using namespace std::chrono_literals;
using detail::to_msg_time;
using std::placeholders::_1;

// ── Constructor ─────────────────────────────────────────────────────────────

WorldModelNode::WorldModelNode(const rclcpp::NodeOptions& options)
  : Node("m2_world_model", options) {
  load_parameters();
  create_components();
  setup_subscribers();
  setup_publishers();
  setup_timers();
  setup_logger();
  RCLCPP_INFO(get_logger(), "M2 World Model node initialized");
}

// ── Parameter loading ──────────────────────────────────────────────────────

void WorldModelNode::load_parameters() {
  declare_m2_parameters(*this);

  // Rates
  params_.rates.aggregation_rate_hz =
      get_parameter("aggregation_rate_hz").as_double();
  params_.rates.sat_rate_hz =
      get_parameter("sat_rate_hz").as_double();
  params_.rates.asdr_periodic_rate_hz =
      get_parameter("asdr_periodic_rate_hz").as_double();
  params_.rates.heartbeat_rate_hz =
      get_parameter("heartbeat_rate_hz").as_double();

  // Buffer
  params_.buffer.max_targets =
      static_cast<int32_t>(get_parameter("max_targets").as_int());
  params_.buffer.target_disappearance_periods =
      static_cast<int32_t>(get_parameter("target_disappearance_periods").as_int());
  params_.buffer.environment_cache_ttl_s =
      get_parameter("environment_cache_ttl_s").as_double();

  // CPA safe thresholds (4-element arrays for ODD zones A/B/C/D)
  const auto cpa_safe_m =
      get_parameter("cpa_safe_m").as_double_array();
  const auto tcpa_safe_s =
      get_parameter("tcpa_safe_s").as_double_array();
  if (cpa_safe_m.size() >= 4) {
    params_.cpa.cpa_safe_a_m = cpa_safe_m[0];
    params_.cpa.cpa_safe_b_m = cpa_safe_m[1];
    params_.cpa.cpa_safe_c_m = cpa_safe_m[2];
    params_.cpa.cpa_safe_d_m = cpa_safe_m[3];
  } else {
    params_.cpa.cpa_safe_a_m = 1852.0;
    params_.cpa.cpa_safe_b_m = 555.6;
    params_.cpa.cpa_safe_c_m = 277.8;
    params_.cpa.cpa_safe_d_m = 2778.0;
  }
  if (tcpa_safe_s.size() >= 4) {
    params_.cpa.tcpa_safe_a_s = tcpa_safe_s[0];
    params_.cpa.tcpa_safe_b_s = tcpa_safe_s[1];
    params_.cpa.tcpa_safe_c_s = tcpa_safe_s[2];
    params_.cpa.tcpa_safe_d_s = tcpa_safe_s[3];
  } else {
    params_.cpa.tcpa_safe_a_s = 720.0;
    params_.cpa.tcpa_safe_b_s = 240.0;
    params_.cpa.tcpa_safe_c_s = 180.0;
    params_.cpa.tcpa_safe_d_s = 600.0;
  }

  // COLREG geometry
  params_.colreg.overtaking_bearing_min_deg =
      get_parameter("overtaking_bearing_min_deg").as_double();
  params_.colreg.overtaking_bearing_max_deg =
      get_parameter("overtaking_bearing_max_deg").as_double();
  params_.colreg.head_on_heading_diff_tol_deg =
      get_parameter("head_on_heading_diff_tol_deg").as_double();
  params_.colreg.safe_pass_speed_threshold_mps =
      get_parameter("safe_pass_speed_threshold_mps").as_double();
  params_.colreg.safe_pass_min_cpa_m =
      get_parameter("safe_pass_min_cpa_m").as_double();

  // View health
  params_.health.dv_loss_periods_to_degraded =
      static_cast<int32_t>(get_parameter("dv_loss_periods_to_degraded").as_int());
  params_.health.dv_loss_periods_to_critical =
      static_cast<int32_t>(get_parameter("dv_loss_periods_to_critical").as_int());
  params_.health.ev_loss_ms_to_critical =
      get_parameter("ev_loss_ms_to_critical").as_double();
  params_.health.sv_loss_s_to_degraded =
      get_parameter("sv_loss_s_to_degraded").as_double();

  // Defaults for parameters not in YAML
  params_.health.dv_degraded_to_critical_timeout_s = 30.0;
  params_.health.confidence_floor_dv_degraded = 0.5;
}

// ── Component creation ─────────────────────────────────────────────────────

void WorldModelNode::create_components() {
  // Coordinate transform (origin will be set on first own-ship update)
  coord_transform_ = std::make_shared<CoordTransform>();

  // CPA/TCPA calculator
  CpaTcpaCalculator::Config cpa_cfg{};
  cpa_cfg.method = CpaTcpaCalculator::UncertaintyMethod::Linear;
  cpa_cfg.monte_carlo_samples = 1000;
  cpa_cfg.safety_factor = 1.0;
  cpa_cfg.odd_d_multiplier = 1.5;
  cpa_cfg.max_align_dt_s = 1.0;
  cpa_cfg.static_target_speed_mps = 0.0;
  cpa_calc_ = std::make_shared<CpaTcpaCalculator>(cpa_cfg);

  // Encounter classifier
  EncounterClassifier::Config enc_cfg{};
  enc_cfg.overtaking_bearing_min_deg = params_.colreg.overtaking_bearing_min_deg;
  enc_cfg.overtaking_bearing_max_deg = params_.colreg.overtaking_bearing_max_deg;
  enc_cfg.head_on_heading_diff_tol_deg = params_.colreg.head_on_heading_diff_tol_deg;
  enc_cfg.safe_pass_speed_threshold_mps = params_.colreg.safe_pass_speed_threshold_mps;
  enc_cfg.safe_pass_min_cpa_m = params_.colreg.safe_pass_min_cpa_m;
  classifier_ = std::make_shared<EncounterClassifier>(enc_cfg);

  // Track buffer
  TrackBuffer::Config tb_cfg{};
  tb_cfg.max_targets = params_.buffer.max_targets;
  tb_cfg.disappearance_periods = params_.buffer.target_disappearance_periods;
  tb_cfg.max_target_age_s =
      static_cast<double>(params_.buffer.target_disappearance_periods) /
      params_.rates.aggregation_rate_hz;
  track_buffer_ = std::make_shared<TrackBuffer>(tb_cfg);

  // ENC loader
  const auto enc_data_root =
      get_parameter("enc_data_root").as_string();
  const auto enc_metadata_file =
      get_parameter("enc_metadata_file").as_string();
  EncLoader::Config el_cfg{};
  el_cfg.enc_data_root = enc_data_root;
  el_cfg.enc_metadata_file = enc_metadata_file;
  enc_loader_ = std::make_shared<EncLoader>(el_cfg);
  if (!enc_data_root.empty() && !enc_metadata_file.empty()) {
    if (enc_loader_->load()) {
      RCLCPP_INFO(get_logger(), "ENC data loaded from %s", enc_metadata_file.c_str());
    } else {
      RCLCPP_WARN(get_logger(), "ENC data load failed from %s; zone constraints disabled",
                   enc_metadata_file.c_str());
    }
  } else {
    RCLCPP_INFO(get_logger(), "ENC not configured; zone constraints disabled");
  }

  // View health monitor
  ViewHealthMonitor::Config vh_cfg{};
  vh_cfg.dv_loss_periods_to_degraded = params_.health.dv_loss_periods_to_degraded;
  vh_cfg.dv_loss_periods_to_critical = params_.health.dv_loss_periods_to_critical;
  vh_cfg.dv_degraded_to_critical_timeout_s =
      params_.health.dv_degraded_to_critical_timeout_s;
  vh_cfg.ev_loss_ms_to_critical = params_.health.ev_loss_ms_to_critical;
  vh_cfg.sv_loss_s_to_degraded = params_.health.sv_loss_s_to_degraded;
  vh_cfg.confidence_floor_dv_degraded = params_.health.confidence_floor_dv_degraded;
  health_ = std::make_shared<ViewHealthMonitor>(vh_cfg);

  // World state aggregator
  WorldStateAggregator::Config agg_cfg{};
  agg_cfg.max_targets = params_.buffer.max_targets;
  agg_cfg.target_disappearance_periods = params_.buffer.target_disappearance_periods;
  agg_cfg.environment_cache_ttl_s = params_.buffer.environment_cache_ttl_s;
  agg_cfg.timing_drift_warn_ms = 50.0;
  agg_cfg.confidence_floor_dv_degraded = params_.health.confidence_floor_dv_degraded;
  agg_cfg.cpa_safe_m = {params_.cpa.cpa_safe_a_m,
                         params_.cpa.cpa_safe_b_m,
                         params_.cpa.cpa_safe_c_m,
                         params_.cpa.cpa_safe_d_m};
  agg_cfg.tcpa_safe_s = {params_.cpa.tcpa_safe_a_s,
                          params_.cpa.tcpa_safe_b_s,
                          params_.cpa.tcpa_safe_c_s,
                          params_.cpa.tcpa_safe_d_s};

  aggregator_ = std::make_shared<WorldStateAggregator>(
      agg_cfg, cpa_calc_, classifier_,
      track_buffer_, enc_loader_, health_);
}

// ── Subscriber setup ───────────────────────────────────────────────────────

void WorldModelNode::setup_subscribers() {
  // Callback group 1: reentrant for high-frequency EV
  ego_cbg_ = create_callback_group(rclcpp::CallbackGroupType::Reentrant);

  // Callback group 2: mutually exclusive for mid/low-frequency DV, SV, ODD
  dv_sv_cbg_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

  rclcpp::SubscriptionOptions opt_ego;
  opt_ego.callback_group = ego_cbg_;

  rclcpp::SubscriptionOptions opt_dv;
  opt_dv.callback_group = dv_sv_cbg_;

  // /fusion/own_ship_state @ 50 Hz — SensorDataQoS, keep_last(2)
  own_ship_sub_ = create_subscription<l3_external_msgs::msg::FilteredOwnShipState>(
      "/fusion/own_ship_state",
      rclcpp::SensorDataQoS().keep_last(2),
      std::bind(&WorldModelNode::on_own_ship_state, this, _1),
      opt_ego);

  // /fusion/tracked_targets @ 2 Hz — reliable, keep_last(5)
  targets_sub_ = create_subscription<l3_external_msgs::msg::TrackedTargetArray>(
      "/fusion/tracked_targets",
      rclcpp::QoS(5).reliable(),
      std::bind(&WorldModelNode::on_tracked_targets, this, _1),
      opt_dv);

  // /fusion/environment_state @ 0.2 Hz — reliable, transient_local
  env_sub_ = create_subscription<l3_external_msgs::msg::EnvironmentState>(
      "/fusion/environment_state",
      rclcpp::QoS(5).reliable().transient_local(),
      std::bind(&WorldModelNode::on_environment_state, this, _1),
      opt_dv);

  // /l3/m1/odd_state @ 1 Hz — reliable, transient_local
  odd_state_sub_ = create_subscription<l3_msgs::msg::ODDState>(
      "/l3/m1/odd_state",
      rclcpp::QoS(10).reliable().transient_local(),
      std::bind(&WorldModelNode::on_odd_state, this, _1),
      opt_dv);

  RCLCPP_INFO(get_logger(), "Subscribers set up (ego reentrant, DV/SV/ODD mutex)");
}

// ── Publisher setup ────────────────────────────────────────────────────────

void WorldModelNode::setup_publishers() {
  world_state_pub_ = create_publisher<l3_msgs::msg::WorldState>(
      "/l3/m2/world_state",
      rclcpp::SensorDataQoS().keep_last(2));

  sat_pub_ = create_publisher<l3_msgs::msg::SATData>(
      "/l3/sat/data",
      rclcpp::SensorDataQoS().keep_last(1));

  asdr_pub_ = create_publisher<l3_msgs::msg::ASDRRecord>(
      "/l3/asdr/record",
      rclcpp::QoS(50).reliable().transient_local());

  RCLCPP_INFO(get_logger(), "Publishers set up");
}

// ── Timer setup ────────────────────────────────────────────────────────────

void WorldModelNode::setup_timers() {
  using namespace std::chrono;

  const auto agg_ms = static_cast<int64_t>(1000.0 / params_.rates.aggregation_rate_hz);
  const auto sat_ms = static_cast<int64_t>(1000.0 / params_.rates.sat_rate_hz);
  const auto asdr_ms = static_cast<int64_t>(1000.0 / params_.rates.asdr_periodic_rate_hz);
  const auto hb_ms = static_cast<int64_t>(1000.0 / params_.rates.heartbeat_rate_hz);

  aggregation_timer_ = create_wall_timer(
      milliseconds(agg_ms),
      std::bind(&WorldModelNode::on_aggregation_timer, this));

  sat_timer_ = create_wall_timer(
      milliseconds(sat_ms),
      std::bind(&WorldModelNode::on_sat_timer, this));

  asdr_periodic_timer_ = create_wall_timer(
      milliseconds(asdr_ms),
      std::bind(&WorldModelNode::on_asdr_periodic_timer, this));

  heartbeat_timer_ = create_wall_timer(
      milliseconds(hb_ms),
      std::bind(&WorldModelNode::on_heartbeat_timer, this));

  RCLCPP_INFO(get_logger(), "Timers set up (agg=%ldms, sat=%ldms, asdr=%ldms, hb=%ldms)",
               agg_ms, sat_ms, asdr_ms, hb_ms);
}

// ── Logger setup ───────────────────────────────────────────────────────────

void WorldModelNode::setup_logger() {
  try {
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        "/var/log/mass-l3/m2_world_model.log",
        10 * 1024 * 1024,  // 10 MB
        5);                 // max 5 files

    std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
    logger_ = std::make_shared<spdlog::logger>(
        "mass_l3_m2", sinks.begin(), sinks.end());

    // Set pattern: [%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v
    logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::warn);

    spdlog::register_logger(logger_);

    RCLCPP_INFO(get_logger(), "spdlog logger 'mass_l3_m2' initialized");
  } catch (const spdlog::spdlog_ex& e) {
    RCLCPP_WARN(get_logger(), "spdlog init failed: %s; falling back to ROS logging",
                 e.what());
    logger_ = nullptr;
  }
}

// ── Subscriber callbacks ───────────────────────────────────────────────────

void WorldModelNode::on_tracked_targets(
    const l3_external_msgs::msg::TrackedTargetArray::SharedPtr msg) {
  const auto now = std::chrono::steady_clock::now();
  track_buffer_->update(*msg, now);

  if (logger_) {
    logger_->info("on_tracked_targets: {} targets, buffer size={}",
                   msg->targets.size(), track_buffer_->size());
  }
}

void WorldModelNode::on_own_ship_state(
    const l3_external_msgs::msg::FilteredOwnShipState::SharedPtr msg) {
  // Initialize coord transform on first reception
  if (!coord_transform_->initialized()) {
    coord_transform_->init(msg->position.latitude,
                            msg->position.longitude,
                            0.0);
    RCLCPP_INFO(get_logger(), "CoordTransform origin set at (%.6f, %.6f)",
                 msg->position.latitude, msg->position.longitude);
  }

  aggregator_->update_own_ship(*msg);
}

void WorldModelNode::on_environment_state(
    const l3_external_msgs::msg::EnvironmentState::SharedPtr msg) {
  aggregator_->update_environment(*msg);

  if (logger_) {
    logger_->info("on_environment_state: wind={:.1f}kn, wave={:.1f}m, vis={:.1f}nm",
                   msg->wind_speed_kn, msg->wave_height_m,
                   msg->visibility_range_nm);
  }
}

void WorldModelNode::on_odd_state(
    const l3_msgs::msg::ODDState::SharedPtr msg) {
  aggregator_->update_odd_state(*msg);

  if (logger_) {
    logger_->info("on_odd_state: zone={}, auto_level={}, env_state={}, conf={:.2f}",
                   static_cast<int>(msg->current_zone),
                   static_cast<int>(msg->auto_level),
                   static_cast<int>(msg->envelope_state),
                   msg->conformance_score);
  }
}

// ── Timer callbacks ────────────────────────────────────────────────────────

void WorldModelNode::on_aggregation_timer() {
  publish_world_state();
}

void WorldModelNode::on_sat_timer() {
  publish_sat_data();
}

void WorldModelNode::on_asdr_periodic_timer() {
  // Publish a periodic ASDR snapshot with current view health
  const auto health = aggregator_->aggregated_health();
  const auto own_ship = aggregator_->latest_own_ship();

  std::ostringstream ss;
  ss << "{"
     << "\"view_health\":{"
     << "\"dv\":" << static_cast<int>(health.dv_health) << ","
     << "\"ev\":" << static_cast<int>(health.ev_health) << ","
     << "\"sv\":" << static_cast<int>(health.sv_health) << ","
     << "\"dv_confidence\":" << health.dv_confidence << ","
     << "\"ev_confidence\":" << health.ev_confidence << ","
     << "\"sv_confidence\":" << health.sv_confidence << ","
     << "\"aggregated\":" << health.aggregated
     << "},"
     << "\"own_ship\":{"
     << "\"sog_kn\":" << own_ship.sog_kn << ","
     << "\"cog_deg\":" << own_ship.cog_deg << ","
     << "\"heading_deg\":" << own_ship.heading_deg
     << "}"
     << "}";

  publish_asdr_record("world_state_snapshot", ss.str());
}

void WorldModelNode::on_heartbeat_timer() {
  const auto health = aggregator_->aggregated_health();

  RCLCPP_DEBUG(get_logger(),
                "Heartbeat — DV=%s(c=%.2f) EV=%s(c=%.2f) SV=%s(c=%.2f) agg=%.2f",
                health.dv_health == ViewHealth::Full ? "Full" :
                    health.dv_health == ViewHealth::Degraded ? "Degraded" :
                        health.dv_health == ViewHealth::Critical ? "Critical" : "Lost",
                health.dv_confidence,
                health.ev_health == ViewHealth::Full ? "Full" :
                    health.ev_health == ViewHealth::Degraded ? "Degraded" :
                        health.ev_health == ViewHealth::Critical ? "Critical" : "Lost",
                health.ev_confidence,
                health.sv_health == ViewHealth::Full ? "Full" :
                    health.sv_health == ViewHealth::Degraded ? "Degraded" :
                        health.sv_health == ViewHealth::Critical ? "Critical" : "Lost",
                health.sv_confidence,
                health.aggregated);

  if (logger_) {
    logger_->info("HB| DV={}/c={:.2f} EV={}/c={:.2f} SV={}/c={:.2f} agg={:.2f}",
                   static_cast<int>(health.dv_health), health.dv_confidence,
                   static_cast<int>(health.ev_health), health.ev_confidence,
                   static_cast<int>(health.sv_health), health.sv_confidence,
                   health.aggregated);
  }
}

// ── Publisher helpers ──────────────────────────────────────────────────────

void WorldModelNode::publish_world_state() {
  const auto now = std::chrono::steady_clock::now();
  auto ws = aggregator_->compose_world_state(now);

  if (ws.has_value()) {
    const auto target_count = ws->targets.size();
    const float confidence = ws->confidence;
    world_state_pub_->publish(std::move(ws.value()));

    if (logger_) {
      logger_->info("WS pub | targets={} conf={:.3f}", target_count, confidence);
    }
  }
  // If no value (EV critical or no own-ship snapshot), silently skip publication.
}

void WorldModelNode::publish_sat_data() {
  const auto health = aggregator_->aggregated_health();
  const auto own_ship = aggregator_->latest_own_ship();
  const auto zone = aggregator_->latest_zone();

  l3_msgs::msg::SATData msg;
  msg.stamp = to_msg_time();
  msg.source_module = "M2_World_Model";

  // SAT1 — Status summary
  {
    std::ostringstream ss;
    ss << "POS=(" << own_ship.latitude_deg << ", "
       << own_ship.longitude_deg << ") "
       << "SOG=" << own_ship.sog_kn << "kn "
       << "HDG=" << own_ship.heading_deg << "deg "
       << "ZONE=" << zone.zone_type;
    msg.sat1.state_summary = ss.str();
  }

  // SAT2 — Reasoning / view health
  {
    std::ostringstream ss;
    ss << "DV=";
    switch (health.dv_health) {
      case ViewHealth::Full:     ss << "Full"; break;
      case ViewHealth::Degraded: ss << "Degraded"; break;
      case ViewHealth::Critical: ss << "Critical"; break;
      case ViewHealth::Lost:     ss << "Lost"; break;
    }
    ss << " EV=";
    switch (health.ev_health) {
      case ViewHealth::Full:     ss << "Full"; break;
      case ViewHealth::Degraded: ss << "Degraded"; break;
      case ViewHealth::Critical: ss << "Critical"; break;
      case ViewHealth::Lost:     ss << "Lost"; break;
    }
    ss << " SV=";
    switch (health.sv_health) {
      case ViewHealth::Full:     ss << "Full"; break;
      case ViewHealth::Degraded: ss << "Degraded"; break;
      case ViewHealth::Critical: ss << "Critical"; break;
      case ViewHealth::Lost:     ss << "Lost"; break;
    }
    msg.sat2.trigger_reason = "periodic";
    msg.sat2.reasoning_chain = ss.str();
    msg.sat2.system_confidence = static_cast<float>(health.aggregated);
  }

  // SAT3 — Forecast / predictions (minimal)
  {
    msg.sat3.forecast_horizon_s = 90.0;  // Mid-MPC horizon
    msg.sat3.predicted_state = "nominal";
    msg.sat3.prediction_uncertainty = static_cast<float>(
        1.0 - health.aggregated);  // uncertainty = 1 - confidence
    msg.sat3.tdl_s = 0.0f;  // Not tracked in M2
    msg.sat3.tmr_s = 0.0f;
  }

  sat_pub_->publish(msg);

  if (logger_) {
    logger_->info("SAT pub | conf={:.3f} summary='{}'",
                   health.aggregated, msg.sat1.state_summary);
  }
}

void WorldModelNode::publish_asdr_record(
    const std::string& decision_type,
    const std::string& rationale_json) {
  l3_msgs::msg::ASDRRecord msg;
  msg.stamp = to_msg_time();
  msg.source_module = "M2_World_Model";
  msg.decision_type = decision_type;
  msg.decision_json = rationale_json;
  // Signature field left empty for v1.0 (SHA-256 to be added in v1.1)
  msg.signature.clear();

  asdr_pub_->publish(msg);
}

}  // namespace mass_l3::m2
