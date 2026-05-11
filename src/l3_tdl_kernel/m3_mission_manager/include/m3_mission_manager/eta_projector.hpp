#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "l3_msgs/msg/world_state.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "l3_external_msgs/msg/speed_profile.hpp"
#include "m3_mission_manager/types.hpp"

namespace mass_l3::m3 {

struct EtaProjectorConfig {
  int32_t sampling_interval_s;          // [TBD-HAZID] 60 s
  int64_t forecast_horizon_max_s;       // [TBD-HAZID] 3600
  double sea_current_uncertainty_kn;    // [TBD-HAZID] 0.3
  double world_state_age_threshold_s;   // [TBD-HAZID] 0.5
  double infeasible_margin_s;           // [TBD-HAZID] 600.0
};

class EtaProjector {
 public:
  explicit EtaProjector(EtaProjectorConfig config);
  ~EtaProjector() = default;
  EtaProjector(const EtaProjector&) = delete;
  EtaProjector& operator=(const EtaProjector&) = delete;
  EtaProjector(EtaProjector&&) = default;
  EtaProjector& operator=(EtaProjector&&) = default;

  /// Update route/profile data. Returns false if route/profile mismatch.
  bool update_route(const l3_external_msgs::msg::PlannedRoute& route);
  bool update_speed_profile(const l3_external_msgs::msg::SpeedProfile& profile);

  /// Compute ETA projection. Returns nullopt if data insufficient.
  [[nodiscard]] std::optional<EtaProjection> project(
      const l3_msgs::msg::WorldState& world_state,
      std::chrono::steady_clock::time_point now) const;

  /// Current speed over ground from world state.
  [[nodiscard]] double current_sog_kn(const l3_msgs::msg::WorldState& ws) const;

  bool has_route() const { return route_.has_value(); }
  bool has_profile() const { return profile_.has_value(); }

 private:
  /// Compute piecewise-linear ETA along route segments.
  [[nodiscard]] double integrate_eta_(
      const l3_msgs::msg::WorldState& world_state,
      std::chrono::steady_clock::time_point now) const;

  EtaProjectorConfig config_;
  std::optional<l3_external_msgs::msg::PlannedRoute> route_;
  std::optional<l3_external_msgs::msg::SpeedProfile> profile_;
};

}  // namespace mass_l3::m3
