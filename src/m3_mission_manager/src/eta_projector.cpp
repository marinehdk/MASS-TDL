#include "m3_mission_manager/eta_projector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "geographic_msgs/msg/geo_point.hpp"
#include "geographic_msgs/msg/geo_pose_stamped.hpp"

namespace mass_l3::m3 {
namespace {

constexpr double kEarthRadiusNm = 3440.06479;
constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
constexpr double kNmToM = 1852.0;
constexpr double kKnToMs = 0.514444;
constexpr double kMaxFutureStampToleranceS = 5.0;

double haversineNm(double lat1_deg, double lon1_deg,
                   double lat2_deg, double lon2_deg) noexcept {
  const double lat1 = lat1_deg * kDegToRad;
  const double lon1 = lon1_deg * kDegToRad;
  const double lat2 = lat2_deg * kDegToRad;
  const double lon2 = lon2_deg * kDegToRad;

  const double dlat = lat2 - lat1;
  const double dlon = lon2 - lon1;
  const double sin_dlat = std::sin(dlat * 0.5);
  const double sin_dlon = std::sin(dlon * 0.5);
  const double a = sin_dlat * sin_dlat +
                   std::cos(lat1) * std::cos(lat2) * sin_dlon * sin_dlon;
  const double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
  return kEarthRadiusNm * c;
}

double haversineNmGeo(const geographic_msgs::msg::GeoPoint& a,
                      const geographic_msgs::msg::GeoPoint& b) noexcept {
  return haversineNm(a.latitude, a.longitude, b.latitude, b.longitude);
}

// Compute fractional progress [0, 1] of ship position along a route.
// Uses nearest-waypoint approximation.
double compute_progress(
    const geographic_msgs::msg::GeoPoint& ship_pos,
    const std::vector<geographic_msgs::msg::GeoPoint>& waypoints) {
  if (waypoints.size() < 2) {
    return 0.0;
  }

  // Compute cumulative distances along route
  std::vector<double> cum_dist;
  cum_dist.reserve(waypoints.size());
  cum_dist.push_back(0.0);
  for (size_t i = 1; i < waypoints.size(); ++i) {
    cum_dist.push_back(cum_dist.back() +
                       haversineNmGeo(waypoints[i - 1], waypoints[i]));
  }

  const double total_dist = cum_dist.back();
  if (total_dist <= 0.0) {
    return 0.0;
  }

  // Find nearest waypoint
  double min_dist = std::numeric_limits<double>::max();
  double best_cum = 0.0;
  for (size_t i = 0; i < waypoints.size(); ++i) {
    const double d = haversineNmGeo(ship_pos, waypoints[i]);
    if (d < min_dist) {
      min_dist = d;
      best_cum = cum_dist[i];
    }
  }

  return best_cum / total_dist;
}

}  // anonymous namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

EtaProjector::EtaProjector(EtaProjectorConfig config)
    : config_(config) {}

// ---------------------------------------------------------------------------
// update_route — cache PlannedRoute, extract waypoints
// ---------------------------------------------------------------------------

bool EtaProjector::update_route(
    const l3_external_msgs::msg::PlannedRoute& route) {
  route_ = route;
  return true;
}

// ---------------------------------------------------------------------------
// update_speed_profile — cache SpeedProfile
// ---------------------------------------------------------------------------

bool EtaProjector::update_speed_profile(
    const l3_external_msgs::msg::SpeedProfile& profile) {
  profile_ = profile;
  return true;
}

// ---------------------------------------------------------------------------
// current_sog_kn — extract SOG from world state own_ship
// ---------------------------------------------------------------------------

double EtaProjector::current_sog_kn(
    const l3_msgs::msg::WorldState& ws) const {
  return ws.own_ship.sog_kn;
}

// ---------------------------------------------------------------------------
// project — compute ETA projection
// ---------------------------------------------------------------------------

std::optional<EtaProjection> EtaProjector::project(
    const l3_msgs::msg::WorldState& world_state,
    std::chrono::steady_clock::time_point now) const {
  // 1. Check route and profile are loaded
  if (!route_ || !profile_) {
    return std::nullopt;
  }

  // 2. Check world_state freshness
  {
    const int64_t stamp_ns =
        static_cast<int64_t>(world_state.stamp.sec) * 1'000'000'000LL +
        static_cast<int64_t>(world_state.stamp.nanosec);
    const int64_t now_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch())
            .count();
    const double age_s =
        static_cast<double>(now_ns - stamp_ns) / 1.0e9;

    // Reject if stamp is too far in the future or too old
    if (age_s < -kMaxFutureStampToleranceS ||
        age_s > config_.world_state_age_threshold_s) {
      return std::nullopt;
    }
  }

  // 3. Get current SOG
  const double sog_kn = current_sog_kn(world_state);

  // 4. Compute distance remaining
  const double total_distance_nm = route_->total_distance_nm;

  // Extract waypoints from route GeoPath
  std::vector<geographic_msgs::msg::GeoPoint> waypoints;
  waypoints.reserve(route_->route.poses.size());
  for (const auto& pose_stamped : route_->route.poses) {
    waypoints.push_back(pose_stamped.pose.position);
  }

  const double progress =
      compute_progress(world_state.own_ship.position, waypoints);
  const double distance_covered_nm = progress * total_distance_nm;
  const double distance_remaining_nm =
      std::max(0.0, total_distance_nm - distance_covered_nm);

  // 5. Integrate ETA
  const double eta_s = integrate_eta_(world_state, now);

  // 6. Apply uncertainty factor
  double uncertainty_s = 0.0;
  if (sog_kn > 0.0) {
    const double uncertainty_ratio =
        config_.sea_current_uncertainty_kn / sog_kn;
    uncertainty_s = eta_s * uncertainty_ratio;
  }

  // 7. Return projection
  return EtaProjection{eta_s, distance_remaining_nm, uncertainty_s, now};
}

// ---------------------------------------------------------------------------
// integrate_eta_ — trapezoidal integration along remaining route segments
// ---------------------------------------------------------------------------

double EtaProjector::integrate_eta_(
    const l3_msgs::msg::WorldState& world_state,
    std::chrono::steady_clock::time_point now) const {
  if (!route_ || !profile_) {
    return config_.infeasible_margin_s;
  }

  // Extract waypoints for progress computation
  std::vector<geographic_msgs::msg::GeoPoint> waypoints;
  waypoints.reserve(route_->route.poses.size());
  for (const auto& pose_stamped : route_->route.poses) {
    waypoints.push_back(pose_stamped.pose.position);
  }

  const double progress =
      compute_progress(world_state.own_ship.position, waypoints);
  const double total_dist_m = route_->total_distance_nm * kNmToM;
  const double dist_covered_m = progress * total_dist_m;

  if (dist_covered_m >= total_dist_m) {
    return 0.0;
  }

  const double remaining_m = total_dist_m - dist_covered_m;

  // --- Trapezoidal integration ---
  // Step along the remaining route in sampling_interval_s increments.
  // At each step, look up the speed profile speed at the current distance
  // and use trapezoidal rule: avg_speed = (v1 + v2) / 2.

  // Helper lambda: get speed from profile at a given distance along route
  auto speed_at_distance = [this](double dist_m) -> double {
    if (!profile_ || profile_->target_speeds_kn.empty()) {
      return -1.0;
    }
    const auto& starts = profile_->segment_start_distances_m;
    const auto& ends = profile_->segment_end_distances_m;
    const auto& speeds = profile_->target_speeds_kn;

    for (size_t i = 0; i < speeds.size() && i < starts.size() && i < ends.size(); ++i) {
      if (dist_m >= starts[i] && dist_m < ends[i]) {
        return speeds[i];
      }
    }
    // Beyond all defined segments: return last segment speed
    return speeds.back();
  };

  double total_time_s = 0.0;
  double current_m = dist_covered_m;
  const double sog_kn = current_sog_kn(world_state);

  while (current_m < total_dist_m - 1e-6) {
    // Speed at current position
    double v1_kn = speed_at_distance(current_m);
    if (v1_kn <= 0.0) {
      v1_kn = sog_kn;
    }
    if (v1_kn <= 0.0) {
      v1_kn = 1.0;  // minimum useful speed for integration
    }

    const double v1_ms = v1_kn * kKnToMs;
    const double step_m = std::min(v1_ms * static_cast<double>(config_.sampling_interval_s),
                                   remaining_m - (current_m - dist_covered_m));

    const double next_m = current_m + step_m;
    double v2_kn = speed_at_distance(next_m);
    if (v2_kn <= 0.0) {
      v2_kn = sog_kn;
    }
    if (v2_kn <= 0.0) {
      v2_kn = v1_kn;
    }

    const double avg_ms = (v1_kn + v2_kn) * 0.5 * kKnToMs;
    if (avg_ms > 0.0) {
      total_time_s += step_m / avg_ms;
    }

    current_m = next_m;
  }

  return total_time_s;
}

}  // namespace mass_l3::m3
