#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include "m2_world_model/types.hpp"

namespace mass_l3::m2 {

/// Environmental sanity checker for M2 World Model (D2.2 Track C).
///
/// Validates incoming environment state (ZoneSnapshot) for physical
/// plausibility, temporal continuity, and zone-type transition consistency.
/// On failure, provides a corrected snapshot and confidence multiplier
/// for downstream health aggregation.
class EnvSanityChecker final {
 public:
  struct Config {
    double visibility_min_nm{0.1};
    double visibility_max_nm{15.0};
    double hs_max_m{8.0};
    double current_speed_max_kn{10.0};
    double staleness_max_s{60.0};
    double temp_consistency_max_delta_c{5.0};
  };

  struct ValidationResult {
    bool passed{true};
    std::string reason;
    ZoneSnapshot corrected_snapshot;
    double confidence_multiplier{1.0};
  };

  explicit EnvSanityChecker(Config cfg) : cfg_(std::move(cfg)) {}

  /// Validate an environment snapshot against sanity criteria.
  /// Returns result with passed/failed, corrected snapshot, and multiplier.
  ValidationResult validate(
      const ZoneSnapshot& snapshot,
      std::chrono::steady_clock::time_point now);

  /// Reset cached state (zone transition history, fallback snapshot).
  void reset();

 private:
  /// Check zone-type transition validity.
  /// Rejects direct "open_water" → "port" jumps without narrow_channel.
  bool check_zone_transition_(const ZoneSnapshot& snapshot);

  Config cfg_;

  // Cached last valid zone type for transition checking
  std::optional<std::string> last_zone_type_;

  // Cached last valid snapshot for fallback on consecutive failures
  std::optional<ZoneSnapshot> fallback_snapshot_;
};

}  // namespace mass_l3::m2
