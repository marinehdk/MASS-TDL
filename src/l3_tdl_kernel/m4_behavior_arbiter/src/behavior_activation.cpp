#include "m4_behavior_arbiter/behavior_activation.hpp"
#include <algorithm>
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

HealthState BehaviorActivationCondition::compute_health_state(
    const ArbitrationInputs& inputs) {
  int stale_count = 0;
  if (inputs.age_odd_ms     > inputs.timeout_critical_ms) ++stale_count;
  if (inputs.age_world_ms   > inputs.timeout_critical_ms) ++stale_count;
  if (inputs.age_mission_ms > inputs.timeout_critical_ms) ++stale_count;
  if (inputs.age_colregs_ms > inputs.timeout_critical_ms) ++stale_count;

  if (stale_count >= 3) return HealthState::Critical;

  bool any_degraded =
      (inputs.age_odd_ms     > inputs.timeout_degraded_ms) ||
      (inputs.age_world_ms   > inputs.timeout_degraded_ms) ||
      (inputs.age_mission_ms > inputs.timeout_degraded_ms) ||
      (inputs.age_colregs_ms > inputs.timeout_degraded_ms);

  return any_degraded ? HealthState::Degraded : HealthState::Normal;
}

bool BehaviorActivationCondition::is_transit_applicable(
    const ArbitrationInputs& inputs) {
  if (inputs.odd_zone > 2) return false;
  if (!inputs.mission_received) return false;
  return true;
}

bool BehaviorActivationCondition::is_colreg_avoid_applicable(
    const ArbitrationInputs& inputs) {
  if (inputs.odd_zone == 2) return false;
  if (!inputs.colregs_received) return false;
  if (inputs.age_colregs_ms > inputs.timeout_degraded_ms) return false;
  if (!inputs.colregs_conflict_detected) return false;
  return true;
}

bool BehaviorActivationCondition::is_restricted_vis_applicable(
    const ArbitrationInputs& inputs) {
  if (inputs.odd_zone != 3) return false;
  if (!inputs.world_received) return false;
  if (inputs.world_visibility_nm >= 2.0) return false;
  if (inputs.own_speed_kn <= 0.0) return false;
  return true;
}

bool BehaviorActivationCondition::is_channel_follow_applicable(
    const ArbitrationInputs& inputs) {
  if (inputs.odd_zone != 1) return false;
  if (!inputs.world_received) return false;
  if (!inputs.world_in_vts_zone) return false;
  return true;
}

bool BehaviorActivationCondition::is_mrc_drift_applicable(
    const ArbitrationInputs& inputs) {
  if (inputs.mode_mrc_triggered) return true;
  HealthState hs = compute_health_state(inputs);
  if (hs == HealthState::Critical) return true;
  return false;
}

std::vector<BehaviorType> BehaviorActivationCondition::compute_active_set(
    const ArbitrationInputs& inputs,
    const BehaviorDictionary& dictionary) {
  (void)dictionary;
  std::vector<BehaviorType> active;

  if (is_mrc_drift_applicable(inputs)) {
    active.push_back(BehaviorType::MRC_DRIFT);
    return active;
  }

  if (is_transit_applicable(inputs)) {
    active.push_back(BehaviorType::TRANSIT);
  }
  if (is_colreg_avoid_applicable(inputs)) {
    active.push_back(BehaviorType::COLREG_AVOID);
  }
  if (is_restricted_vis_applicable(inputs)) {
    active.push_back(BehaviorType::DP_HOLD);
  }
  if (is_channel_follow_applicable(inputs)) {
    active.push_back(BehaviorType::BERTH);
  }

  spdlog::debug("[M4] compute_active_set: {} behaviors active", active.size());
  return active;
}

}  // namespace mass_l3::m4
