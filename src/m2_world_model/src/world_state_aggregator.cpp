#include "m2_world_model/world_state_aggregator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <builtin_interfaces/msg/time.hpp>
#include <geographic_msgs/msg/geo_point.hpp>
#include <l3_msgs/msg/tracked_target.hpp>
#include <l3_msgs/msg/own_ship_state.hpp>
#include <l3_msgs/msg/zone_constraint.hpp>

#include "m2_world_model/detail/time_utils.hpp"

namespace mass_l3::m2 {

using detail::to_msg_time;

// ── Helper: compute initial bearing (forward azimuth) from own to target ──
// Uses the spherical Earth approximation (haversine formula) for v1.0.
// Real implementation should use GeographicLib Geodesic::Inverse.
static double compute_bearing_deg_(double own_lat, double own_lon,
                                    double tgt_lat, double tgt_lon) {
  const double deg2rad = M_PI / 180.0;
  const double lat1 = own_lat * deg2rad;
  const double lat2 = tgt_lat * deg2rad;
  const double dlon = (tgt_lon - own_lon) * deg2rad;

  const double y = std::sin(dlon) * std::cos(lat2);
  const double x = std::cos(lat1) * std::sin(lat2) -
                   std::sin(lat1) * std::cos(lat2) * std::cos(dlon);
  double bearing_rad = std::atan2(y, x);
  if (bearing_rad < 0.0) {
    bearing_rad += 2.0 * M_PI;
  }
  return bearing_rad / deg2rad;
}

// ── Helper: convert 6x6 row-major float[36] to Eigen::Matrix6d ──
static Eigen::Matrix<double, 6, 6> to_eigen_cov6(const double* data) {
  Eigen::Matrix<double, 6, 6> m;
  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 6; ++c) {
      m(r, c) = data[r * 6 + c];
    }
  }
  return m;
}

// ── WorldStateAggregator implementation ──────────────────────────────────

WorldStateAggregator::WorldStateAggregator(
    Config cfg,
    std::shared_ptr<CpaTcpaCalculator> cpa_calc,
    std::shared_ptr<EncounterClassifier> classifier,
    std::shared_ptr<TrackBuffer> track_buffer,
    std::shared_ptr<EncLoader> enc_loader,
    std::shared_ptr<ViewHealthMonitor> health)
  : cfg_(std::move(cfg)),
    cpa_calc_(std::move(cpa_calc)),
    classifier_(std::move(classifier)),
    track_buffer_(std::move(track_buffer)),
    enc_loader_(std::move(enc_loader)),
    health_(std::move(health))
{}

void WorldStateAggregator::update_own_ship(
    const l3_external_msgs::msg::FilteredOwnShipState& msg) {
  const auto now = std::chrono::steady_clock::now();

  OwnShipSnapshot snap;
  snap.sog_kn = msg.sog_kn;
  snap.cog_deg = msg.cog_deg;
  snap.heading_deg = msg.heading_deg;
  snap.u_water = msg.u_water;
  snap.v_water = msg.v_water;
  // current speed/direction not available from nav filter directly;
  // will be applied from environment cache in compose_world_state.
  snap.current_speed_kn = 0.0;
  snap.current_direction_deg = 0.0;
  snap.latitude_deg = msg.position.latitude;
  snap.longitude_deg = msg.position.longitude;

  // Convert row-major 36-element covariance to 6x6 Eigen matrix
  snap.covariance = to_eigen_cov6(msg.covariance.data());
  snap.stamp = now;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    own_ship_cache_ = snap;
  }

  // Report fresh EV age (0 = healthy) to the health monitor.
  health_->report_ev_age(0.0, now);
}

void WorldStateAggregator::update_environment(
    const l3_external_msgs::msg::EnvironmentState& msg) {
  const auto now = std::chrono::steady_clock::now();

  std::lock_guard<std::mutex> lock(mutex_);

  ZoneSnapshot snap;
  snap.in_tss = false;
  snap.in_narrow_channel = false;
  snap.min_water_depth_m = 0.0;
  snap.current_speed_kn = msg.current_speed_kn;
  snap.current_direction_deg = msg.current_direction_deg;
  snap.stamp = now;

  // Query ENC loader for zone at current own-ship position
  if (own_ship_cache_.has_value() && enc_loader_->loaded()) {
    auto zone = enc_loader_->query_zone(own_ship_cache_->latitude_deg,
                                         own_ship_cache_->longitude_deg);
    if (zone.has_value()) {
      snap = zone.value();
      snap.stamp = now;
    } else {
      snap.zone_type = "open_water";
    }
  } else if (!own_ship_cache_.has_value()) {
    snap.zone_type = "unknown";
  } else {
    // ENC not loaded — mark as unknown zone type
    snap.zone_type = "open_water";
  }

  environment_cache_ = snap;
}

void WorldStateAggregator::update_odd_state(
    const l3_msgs::msg::ODDState& msg) {
  const auto now = std::chrono::steady_clock::now();

  OddSnapshot snap;
  snap.current_zone = static_cast<OddZone>(msg.current_zone);
  snap.auto_level = msg.auto_level;
  snap.health = msg.health;
  snap.envelope_state = msg.envelope_state;
  snap.conformance_score = msg.conformance_score;
  snap.tmr_s = msg.tmr_s;
  snap.tdl_s = msg.tdl_s;
  snap.stamp = now;

  std::lock_guard<std::mutex> lock(mutex_);
  odd_cache_ = snap;
}

std::optional<l3_msgs::msg::WorldState>
WorldStateAggregator::compose_world_state(
    std::chrono::steady_clock::time_point now) {
  // 1. Snapshot caches under mutex, then release before heavy computation.
  std::optional<OwnShipSnapshot> own_ship_snap;
  std::optional<OddSnapshot> odd_snap;
  std::optional<ZoneSnapshot> env_snap;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    own_ship_snap = own_ship_cache_;
    odd_snap = odd_cache_;
    env_snap = environment_cache_;
  }

  const auto health = health_->aggregated_health();

  // 2. Check EV health — critical EV means no safe world state
  if (health.ev_health == ViewHealth::Critical) {
    return std::nullopt;
  }

  // We must have an own-ship snapshot to compose anything meaningful.
  if (!own_ship_snap.has_value()) {
    return std::nullopt;
  }

  // Merge current water data from environment snapshot (RFC-002).
  if (env_snap.has_value()) {
    own_ship_snap->current_speed_kn = env_snap->current_speed_kn;
    own_ship_snap->current_direction_deg = env_snap->current_direction_deg;
  }

  // 3. Get active targets from TrackBuffer (TrackBuffer has its own mutex)
  track_buffer_->evict_stale(now);
  const auto targets = track_buffer_->active_targets();

  // 4. Process each target
  std::vector<l3_msgs::msg::TrackedTarget> world_targets;
  world_targets.reserve(targets.size());

  const auto current_zone = odd_snap.has_value()
    ? odd_snap->current_zone
    : OddZone::A;

  for (const auto& target : targets) {
    l3_msgs::msg::TrackedTarget wt;

    // 4a. Compute CPA/TCPA
    auto cpa_opt = cpa_calc_->compute(
        own_ship_snap.value(), target, current_zone);

    if (cpa_opt.has_value()) {
      wt.cpa_m = cpa_opt->cpa_m;
      wt.tcpa_s = cpa_opt->tcpa_s;
    } else {
      // Computation failed — mark as invalid
      wt.cpa_m = -1.0;
      wt.tcpa_s = -1.0;
    }

    // 4b. Compute relative bearing
    const double bearing_deg = compute_bearing_deg_(
        own_ship_snap->latitude_deg,
        own_ship_snap->longitude_deg,
        target.latitude_deg,
        target.longitude_deg);
    double relative_bearing_deg = bearing_deg - own_ship_snap->heading_deg;
    // Normalise to [-180, 180)
    relative_bearing_deg = std::fmod(relative_bearing_deg + 540.0, 360.0) - 180.0;

    // 4c. Relative speed (absolute value — sign handled by classifier)
    const double rel_speed_mps =
        std::abs((target.sog_kn - own_ship_snap->sog_kn) * 0.514444);

    // 4d. Classify encounter
    wt.encounter = classifier_->classify(
        relative_bearing_deg,
        own_ship_snap->heading_deg,
        target.heading_deg,
        rel_speed_mps,
        wt.cpa_m);

    // 4e. Populate remaining TrackedTarget fields
    wt.stamp = to_msg_time();
    wt.target_id = target.target_id;
    wt.position.latitude = target.latitude_deg;
    wt.position.longitude = target.longitude_deg;
    wt.position.altitude = 0.0;
    wt.sog_kn = target.sog_kn;
    wt.cog_deg = target.cog_deg;
    wt.heading_deg = target.heading_deg;

    // Convert 3x3 Eigen covariance to row-major float64[9]
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        wt.covariance[static_cast<size_t>(r * 3 + c)] = target.covariance(r, c);
      }
    }

    wt.classification.assign(target.classification.data(),
                             target.classification.size());
    wt.classification_confidence = target.classification_confidence;
    wt.confidence = static_cast<float>(health.aggregated);
    wt.source_sensor = "fused";

    wt.encounter.relative_bearing_deg = relative_bearing_deg;

    world_targets.push_back(std::move(wt));
  }

  // 5. Build OwnShipState from snapshot
  l3_msgs::msg::OwnShipState os_msg;
  os_msg.stamp = to_msg_time();
  os_msg.position.latitude = own_ship_snap->latitude_deg;
  os_msg.position.longitude = own_ship_snap->longitude_deg;
  os_msg.position.altitude = 0.0;
  os_msg.sog_kn = own_ship_snap->sog_kn;
  os_msg.cog_deg = own_ship_snap->cog_deg;
  os_msg.heading_deg = own_ship_snap->heading_deg;
  os_msg.u_water = own_ship_snap->u_water;
  os_msg.v_water = own_ship_snap->v_water;
  // r_dot_deg_s and nav_mode not available from OwnShipSnapshot — use defaults
  os_msg.r_dot_deg_s = 0.0;
  os_msg.current_speed_kn = own_ship_snap->current_speed_kn;
  os_msg.current_direction_deg = own_ship_snap->current_direction_deg;

  for (int r = 0; r < 6; ++r) {
    for (int c = 0; c < 6; ++c) {
      os_msg.covariance[static_cast<size_t>(r * 6 + c)] = own_ship_snap->covariance(r, c);
    }
  }
  os_msg.nav_mode = "OPTIMAL";

  // 6. Build ZoneConstraint from environment snapshot
  l3_msgs::msg::ZoneConstraint zc_msg;
  if (env_snap.has_value()) {
    zc_msg.zone_type = env_snap->zone_type;
    zc_msg.in_tss = env_snap->in_tss;
    zc_msg.in_narrow_channel = env_snap->in_narrow_channel;
    zc_msg.min_water_depth_m =
        static_cast<float>(env_snap->min_water_depth_m);
  } else {
    zc_msg.zone_type = "unknown";
    zc_msg.in_tss = false;
    zc_msg.in_narrow_channel = false;
    zc_msg.min_water_depth_m = 0.0f;
  }
  // tss_lanes and exclusion_zones empty for v1.0

  // 7. Compute aggregated confidence
  const double agg_conf = compute_aggregated_confidence_();

  // 8. Build WorldState
  l3_msgs::msg::WorldState ws;
  ws.stamp = to_msg_time();
  ws.targets = std::move(world_targets);
  ws.own_ship = os_msg;
  ws.zone = zc_msg;
  ws.confidence = static_cast<float>(agg_conf);

  // Compute SV age for rationale
  double sv_age_s = std::numeric_limits<double>::max();
  if (env_snap.has_value()) {
    sv_age_s = std::chrono::duration<double>(
        now - env_snap->stamp).count();
  }

  ws.rationale = compose_rationale_(
      health, static_cast<int32_t>(targets.size()));

  // 9. Update ViewHealthMonitor
  health_->report_dv_update(!targets.empty(), now);
  health_->report_sv_age(sv_age_s, now);

  return ws;
}

OwnShipSnapshot WorldStateAggregator::latest_own_ship() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (own_ship_cache_.has_value()) {
    return own_ship_cache_.value();
  }
  // Return a default zero-initialised snapshot if never received
  OwnShipSnapshot snap{};
  snap.covariance.setZero();
  snap.stamp = std::chrono::steady_clock::time_point{};
  return snap;
}

ZoneSnapshot WorldStateAggregator::latest_zone() const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (environment_cache_.has_value()) {
    return environment_cache_.value();
  }
  ZoneSnapshot snap{};
  snap.zone_type = "unknown";
  snap.stamp = std::chrono::steady_clock::time_point{};
  return snap;
}

AggregatedHealth WorldStateAggregator::aggregated_health() const {
  return health_->aggregated_health();
}

double WorldStateAggregator::compute_aggregated_confidence_() const {
  const auto health = health_->aggregated_health();
  double conf = std::min({health.dv_confidence,
                           health.ev_confidence,
                           health.sv_confidence});

  // Apply confidence floor when DV is degraded
  if (health.dv_health == ViewHealth::Degraded) {
    conf = std::min(conf, cfg_.confidence_floor_dv_degraded);
  }

  return conf;
}

std::string WorldStateAggregator::compose_rationale_(
    const AggregatedHealth& h, int32_t target_count) const {
  auto view_str = [](ViewHealth v) -> const char* {
    switch (v) {
      case ViewHealth::Full:     return "Full";
      case ViewHealth::Degraded: return "Degraded";
      case ViewHealth::Critical: return "Critical";
      case ViewHealth::Lost:     return "Lost";
      default:                   return "Unknown";
    }
  };

  std::ostringstream ss;
  ss << "DV=" << view_str(h.dv_health)
     << "/c=" << h.dv_confidence
     << " EV=" << view_str(h.ev_health)
     << "/c=" << h.ev_confidence
     << " SV=" << view_str(h.sv_health)
     << "/c=" << h.sv_confidence
     << " targets=" << target_count
     << " agg_c=" << h.aggregated;
  return ss.str();
}

}  // namespace mass_l3::m2
