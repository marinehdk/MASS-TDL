#include "m2_world_model/env_sanity_checker.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

#include <rclcpp/logging.hpp>

namespace mass_l3::m2 {

// ── Public ────────────────────────────────────────────────────────────────

EnvSanityChecker::ValidationResult EnvSanityChecker::validate(
    const ZoneSnapshot& snapshot,
    std::chrono::steady_clock::time_point now) {
  ValidationResult result;
  result.corrected_snapshot = snapshot;

  std::lock_guard<std::mutex> lock(state_mutex_);

  // 1. Staleness check — reject snapshots older than staleness_max_s
  const double age_s =
      std::chrono::duration<double>(now - snapshot.stamp).count();
  if (age_s > cfg_.staleness_max_s) {
    result.passed = false;
    result.confidence_multiplier *= 0.5;
    std::ostringstream ss;
    ss << "stale(" << age_s << "s > " << cfg_.staleness_max_s << "s);";
    result.reason += ss.str();
  }

  // 2. Current speed sanity — unreasonably high ocean current
  if (snapshot.current_speed_kn > cfg_.current_speed_max_kn) {
    result.passed = false;
    result.confidence_multiplier *= 0.8;
    result.corrected_snapshot.current_speed_kn = 0.0;
    std::ostringstream ss;
    ss << "current_speed=" << snapshot.current_speed_kn
       << "kn > " << cfg_.current_speed_max_kn << "kn;";
    result.reason += ss.str();
  }

  // 3. Zone-type transition continuity
  if (!check_zone_transition_(snapshot)) {
    result.passed = false;
    result.confidence_multiplier *= 0.7;
    result.reason += "zone_transition;";
    // Fall back to last known valid zone type
    if (fallback_snapshot_.has_value()) {
      result.corrected_snapshot.zone_type =
          fallback_snapshot_->zone_type;
    }
  }

  // 4. Cache valid snapshot for future fallback
  if (result.passed) {
    last_zone_type_ = snapshot.zone_type;
    fallback_snapshot_ = snapshot;
  } else if (fallback_snapshot_.has_value()) {
    // Use fully cached snapshot when validation fails
    result.corrected_snapshot = fallback_snapshot_.value();
    result.corrected_snapshot.stamp = snapshot.stamp;  // keep current time
  }

  // Log the failure at WARN level for observability
  if (!result.passed) {
    RCLCPP_WARN(rclcpp::get_logger("EnvSanityChecker"),
                "Validation failed: %s (confidence_mult=%.2f)",
                result.reason.c_str(), result.confidence_multiplier);
  }

  return result;
}

void EnvSanityChecker::reset() {
  last_zone_type_.reset();
  fallback_snapshot_.reset();
}

// ── Private ───────────────────────────────────────────────────────────────

bool EnvSanityChecker::check_zone_transition_(
    const ZoneSnapshot& snapshot) {
  // No prior zone → accept (first call initializes the cache)
  if (!last_zone_type_.has_value()) {
    return true;
  }

  // Reject direct "open_water" → "port" without narrow_channel
  const bool from_open_water = (last_zone_type_.value() == "open_water");
  const bool to_port = (snapshot.zone_type == "port");

  if (from_open_water && to_port && !snapshot.in_narrow_channel) {
    return false;
  }

  return true;
}

}  // namespace mass_l3::m2
