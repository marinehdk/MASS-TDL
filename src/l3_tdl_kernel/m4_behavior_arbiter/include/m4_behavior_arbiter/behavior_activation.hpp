#pragma once

#include <vector>
#include <string>
#include <chrono>

#include "m4_behavior_arbiter/behavior_dictionary.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

/// @brief System health state based on input freshness.
enum class HealthState : uint8_t {
  Normal = 0,
  Degraded = 1,
  Critical = 2
};

struct ArbitrationInputs {
  uint8_t  odd_zone{0};
  bool     odd_received{false};
  bool     mode_mrc_triggered{false};
  bool     mode_received{false};
  bool     world_received{false};
  double   world_visibility_nm{999.0};
  bool     world_in_vts_zone{false};
  double   own_speed_kn{0.0};
  bool     mission_received{false};
  double   mission_heading_desired_deg{0.0};
  bool     colregs_received{false};
  bool     colregs_conflict_detected{false};

  int64_t  age_odd_ms{0};
  int64_t  age_world_ms{0};
  int64_t  age_mission_ms{0};
  int64_t  age_colregs_ms{0};

  int64_t  timeout_degraded_ms{2000};
  int64_t  timeout_critical_ms{5000};
};

class BehaviorActivationCondition {
public:
  static std::vector<BehaviorType> compute_active_set(
      const ArbitrationInputs& inputs,
      const BehaviorDictionary& dictionary);

  static HealthState compute_health_state(const ArbitrationInputs& inputs);

  static bool is_transit_applicable(const ArbitrationInputs& inputs);
  static bool is_colreg_avoid_applicable(const ArbitrationInputs& inputs);
  static bool is_restricted_vis_applicable(const ArbitrationInputs& inputs);
  static bool is_channel_follow_applicable(const ArbitrationInputs& inputs);
  static bool is_mrc_drift_applicable(const ArbitrationInputs& inputs);
};

}  // namespace mass_l3::m4
