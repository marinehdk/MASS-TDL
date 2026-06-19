#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include <Eigen/Core>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_msgs/msg/odd_state.hpp"
#include "l3_external_msgs/msg/tracked_target_array.hpp"
#include "l3_external_msgs/msg/filtered_own_ship_state.hpp"
#include "l3_external_msgs/msg/environment_state.hpp"

#include "m2_world_model/cpa_tcpa_calculator.hpp"
#include "m2_world_model/encounter_classifier.hpp"
#include "m2_world_model/track_buffer.hpp"
#include "m2_world_model/enc_loader.hpp"
#include "m2_world_model/view_health_monitor.hpp"
#include "m2_world_model/env_sanity_checker.hpp"
#include "m2_world_model/types.hpp"
#include "m2_world_model/woerner_compliance_scorer.hpp"

namespace mass_l3::m2 {

class WorldStateAggregator final {
 public:
  struct Config {
    int32_t max_targets;
    int32_t target_disappearance_periods;
    double environment_cache_ttl_s;
    double timing_drift_warn_ms;
    double confidence_floor_dv_degraded;
    std::array<double, 4> cpa_safe_m;     // index by OddZone
    std::array<double, 4> tcpa_safe_s;
    double dynamic_horizon_nm{5.0};
    EnvSanityChecker::Config env_sanity;
    bool target_classification_enabled{false};
  };

  WorldStateAggregator(Config cfg,
                       std::shared_ptr<CpaTcpaCalculator> cpa_calc,
                       std::shared_ptr<EncounterClassifier> classifier,
                       std::shared_ptr<TrackBuffer> track_buffer,
                       std::shared_ptr<EncLoader> enc_loader,
                       std::shared_ptr<ViewHealthMonitor> health);

  ~WorldStateAggregator() = default;
  // No copy/move
  WorldStateAggregator(const WorldStateAggregator&) = delete;
  WorldStateAggregator& operator=(const WorldStateAggregator&) = delete;

  /// Update own-ship snapshot (called from 50 Hz EV callback).
  void update_own_ship(const l3_external_msgs::msg::FilteredOwnShipState& msg, TimePoint now);

  /// Update environment snapshot (called from 0.2 Hz SV callback).
  void update_environment(const l3_external_msgs::msg::EnvironmentState& msg, TimePoint now);

  /// Update ODD state cache (called from 1 Hz ODD callback).
  void update_odd_state(const l3_msgs::msg::ODDState& msg, TimePoint now);

  /// Compose World_StateMsg snapshot. Returns nullopt if EV is CRITICAL.
  [[nodiscard]] std::optional<l3_msgs::msg::WorldState>
  compose_world_state(TimePoint now);

  /// Snapshot accessors for SAT and ASDR publishers.
  [[nodiscard]] OwnShipSnapshot latest_own_ship() const;
  [[nodiscard]] ZoneSnapshot latest_zone() const;
  [[nodiscard]] OddSnapshot latest_odd_state() const;
  [[nodiscard]] bool has_odd_state() const;
  [[nodiscard]] AggregatedHealth aggregated_health() const;

  struct Sat3Forecast {
    std::string predicted_state;
    float prediction_uncertainty;
  };

  [[nodiscard]] Sat3Forecast compute_sat3_forecast(TimePoint now) const;

 private:
  [[nodiscard]] double compute_aggregated_confidence_() const;
  [[nodiscard]] std::string compose_rationale_(const AggregatedHealth& h,
                                                int32_t target_count) const;

  Config cfg_;
  std::shared_ptr<CpaTcpaCalculator> cpa_calc_;
  std::shared_ptr<EncounterClassifier> classifier_;
  std::shared_ptr<TrackBuffer> track_buffer_;
  std::shared_ptr<EncLoader> enc_loader_;
  std::shared_ptr<ViewHealthMonitor> health_;
  std::shared_ptr<EnvSanityChecker> env_sanity_checker_;
  std::unordered_map<uint64_t, WoernerComplianceScorer> compliance_scorers_;

  mutable std::mutex mutex_;
  std::optional<OwnShipSnapshot> own_ship_cache_;
  std::optional<ZoneSnapshot> environment_cache_;
  std::optional<OddSnapshot> odd_cache_;
};

}  // namespace mass_l3::m2
