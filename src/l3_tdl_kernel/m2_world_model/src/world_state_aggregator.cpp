#include "m2_world_model/world_state_aggregator.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include <builtin_interfaces/msg/time.hpp>
#include <geographic_msgs/msg/geo_path.hpp>
#include <geographic_msgs/msg/geo_point.hpp>
#include <nlohmann/json.hpp>
#include <rclcpp/logging.hpp>
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
    health_(std::move(health)),
    env_sanity_checker_(std::make_shared<EnvSanityChecker>(cfg.env_sanity))
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

  // D2.2 Track C: run environmental sanity verification before caching
  {
    auto sanity = env_sanity_checker_->validate(snap, now);
    if (!sanity.passed) {
      RCLCPP_WARN(rclcpp::get_logger("WorldStateAggregator"),
                  "EnvSanityChecker: %s confidence_mult=%.2f",
                  sanity.reason.c_str(), sanity.confidence_multiplier);
      snap = sanity.corrected_snapshot;
      // Apply confidence multiplier through health monitor
      // (full integration via ViewHealthMonitor is a follow-up)
    }
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

// NOLINTNEXTLINE(readability-function-size,readability-function-cognitive-complexity)
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
      wt.cpa_covariance_m2 = cpa_opt->uncertainty.cpa_sigma_m
                           * cpa_opt->uncertainty.cpa_sigma_m;
      wt.tcpa_covariance_s2 = cpa_opt->uncertainty.tcpa_sigma_s
                            * cpa_opt->uncertainty.tcpa_sigma_s;
      // Positive-definite safety clamp: guard zero/negative and excessive values
      if (wt.cpa_covariance_m2 <= 0.0 || wt.cpa_covariance_m2 > 2500.0) {
        wt.cpa_covariance_m2 = 2500.0;  // sigma=50m conservative default
      }
      if (wt.tcpa_covariance_s2 <= 0.0 || wt.tcpa_covariance_s2 > 100.0) {
        wt.tcpa_covariance_s2 = 100.0;   // sigma=10s conservative default
      }
    } else {
      // Computation failed — mark as invalid
      wt.cpa_m = -1.0;
      wt.tcpa_s = -1.0;
      wt.cpa_covariance_m2 = 2500.0;   // max uncertainty
      wt.tcpa_covariance_s2 = 100.0;
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

    {
      // Per-target confidence: base × CPA-quality × track-age
      const double base_conf = health.aggregated;
      double cpa_quality = 0.5;
      if (cpa_opt.has_value()) {
        const double cpa_sigma = cpa_opt->uncertainty.cpa_sigma_m;
        const double cpa_mean = cpa_opt->cpa_m;
        if (cpa_mean > 0.1 && cpa_sigma > 0.0) {
          const double cpa_cv = cpa_sigma / cpa_mean;
          cpa_quality = 1.0 / (1.0 + cpa_cv);
        } else if (cpa_mean > 0.1) {
          cpa_quality = 1.0;  // zero sigma = perfect knowledge
        }
      }
      const double track_age_s =
          std::chrono::duration<double>(now - target.stamp).count();
      const double track_age_factor = 1.0 - std::exp(-track_age_s / 60.0);

      double per_target_conf = base_conf * cpa_quality * track_age_factor;
      per_target_conf = std::max(0.05, std::min(1.0, per_target_conf));
      wt.confidence = static_cast<float>(per_target_conf);
    }
    wt.source_sensor = "fused";

    wt.encounter.relative_bearing_deg = relative_bearing_deg;

    // ── D2.2 Track C: bearing, range, intent confidence ──
    wt.brg_deg = bearing_deg;  // true bearing (0°=True North)

    // Haversine distance from own-ship position to target
    {
      const double deg2rad = M_PI / 180.0;
      const double lat1 = own_ship_snap->latitude_deg * deg2rad;
      const double lon1 = own_ship_snap->longitude_deg * deg2rad;
      const double lat2 = target.latitude_deg * deg2rad;
      const double lon2 = target.longitude_deg * deg2rad;
      const double dlat = lat2 - lat1;
      const double dlon = lon2 - lon1;
      const double sin_dlat_2 = std::sin(dlat / 2.0);
      const double sin_dlon_2 = std::sin(dlon / 2.0);
      const double a = sin_dlat_2 * sin_dlat_2 +
                       std::cos(lat1) * std::cos(lat2) *
                       sin_dlon_2 * sin_dlon_2;
      wt.rng_m = 2.0 * 6371000.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    }

    // Intent confidence: radar-only default 0.30, vessel → 0.50,
    // young tracks (<30 s) clamped to 0.10, final clamp [0.05, 0.95]
    {
      double intent_conf = 0.30;
      if (target.classification == "vessel") {
        intent_conf = 0.50;
      }
      const double track_age_s =
          std::chrono::duration<double>(now - target.stamp).count();
      if (track_age_s < 30.0) {
        intent_conf = std::min(0.10, intent_conf);
      }
      intent_conf = std::max(0.05, std::min(0.95, intent_conf));
      wt.intent_confidence = static_cast<float>(intent_conf);
    }

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

    // D2.2 Track B: populate TSS lanes, exclusion zones, and min depth
    // via EncLoader polygon queries.
    if (own_ship_snap.has_value() && enc_loader_->loaded()) {
      const double own_lat = own_ship_snap->latitude_deg;
      const double own_lon = own_ship_snap->longitude_deg;

      // Horizon radius = max(dynamic horizon, 3x CPA-safe for current zone)
      const auto odd_zone_idx = odd_snap.has_value()
        ? static_cast<size_t>(odd_snap->current_zone)
        : 0UL;
      const double horizon_m = std::max(
          cfg_.dynamic_horizon_nm * 1852.0,
          cfg_.cpa_safe_m[odd_zone_idx] * 3.0);

      zc_msg.min_water_depth_m = static_cast<float>(
          enc_loader_->get_min_depth(own_lat, own_lon));

      // TSS lane polygons → GeoPath[]
      {
        const auto lanes = enc_loader_->get_tss_lanes(own_lat, own_lon);
        zc_msg.tss_lanes.reserve(lanes.size());
        for (const auto& lane_json : lanes) {
          try {
            auto arr = nlohmann::json::parse(lane_json);
            geographic_msgs::msg::GeoPath path;
            if (arr.is_array()) {
              for (const auto& pt : arr) {
                geographic_msgs::msg::GeoPoint gp;
                gp.longitude = pt[0].get<double>();
                gp.latitude = pt[1].get<double>();
                gp.altitude = 0.0;
                path.points.push_back(std::move(gp));
              }
            }
            zc_msg.tss_lanes.push_back(std::move(path));
          } catch (const std::exception&) {
          }
        }
      }

      // Exclusion zone polygons → float64[] (lat,lon pairs serialised)
      {
        const auto zones = enc_loader_->get_exclusion_zones(
            own_lat, own_lon, horizon_m);
        for (const auto& zone_json : zones) {
          try {
            auto arr = nlohmann::json::parse(zone_json);
            if (arr.is_array()) {
              for (const auto& pt : arr) {
                zc_msg.exclusion_zones.push_back(
                    pt[1].get<double>());
                zc_msg.exclusion_zones.push_back(
                    pt[0].get<double>());
              }
            }
          } catch (const std::exception&) {
          }
        }
      }
    }
  } else {
    zc_msg.zone_type = "unknown";
    zc_msg.in_tss = false;
    zc_msg.in_narrow_channel = false;
    zc_msg.min_water_depth_m = 0.0f;
  }

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

  // Downward adjustment when any view is degraded or worse
  if (health.dv_health >= ViewHealth::Degraded ||
      health.ev_health >= ViewHealth::Degraded ||
      health.sv_health >= ViewHealth::Degraded) {
    const double degraded_penalty = (health.dv_health >= ViewHealth::Degraded)
      ? cfg_.confidence_floor_dv_degraded : 1.0;
    conf = std::min(conf, degraded_penalty);
  }

  // Per-target quality weighting is applied inline in the target loop
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
