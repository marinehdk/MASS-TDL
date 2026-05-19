#include "m4_behavior_arbiter/behavior_activation.hpp"

#include <algorithm>
#include <vector>

#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

BehaviorActivationCondition::BehaviorActivationCondition(const BehaviorDictionary& dict)
    : dict_(dict) {}

bool BehaviorActivationCondition::is_active(BehaviorType behavior,
                                              const ArbitrationInputs& inputs) const {
  switch (behavior) {
    case BehaviorType::Transit:
      return predicate_transit(inputs);
    case BehaviorType::ColregAvoid:
      return predicate_colreg_avoid(inputs);
    case BehaviorType::DpHold:
      return predicate_dp_hold(inputs);
    case BehaviorType::Berth:
      return predicate_berth(inputs);
    case BehaviorType::MrcDrift:
    case BehaviorType::MrcAnchor:
    case BehaviorType::MrcHeaveTo:
      return predicate_mrc(behavior, inputs);
    default:
      spdlog::warn("BehaviorActivationCondition: Unknown behavior type");
      return false;
  }
}

std::vector<BehaviorType> BehaviorActivationCondition::compute_active_set(
    const ArbitrationInputs& inputs) const {
  std::vector<BehaviorType> active_set;

  for (size_t idx = 0; idx < kBehaviorCount; ++idx) {
    auto behavior_type = static_cast<BehaviorType>(idx);
    if (is_active(behavior_type, inputs)) {
      active_set.push_back(behavior_type);
    }
  }

  return active_set;
}

bool BehaviorActivationCondition::predicate_transit(const ArbitrationInputs& in) const {
  // Transit active if:
  // - Transit applicable in current ODD zone
  // - No targets with CPA < safe threshold
  // - Health not CRITICAL
  // - Mode is NORMAL or DEGRADED

  const auto& descriptor = dict_.get(BehaviorType::Transit);
  if (std::find(descriptor.applicable_zones.begin(), descriptor.applicable_zones.end(),
                in.odd_state.current_zone) == descriptor.applicable_zones.end()) {
    return false;
  }

  if (in.odd_state.health == l3_msgs::msg::ODDState::HEALTH_CRITICAL) {
    return false;
  }

  if (in.mode_cmd.mode != l3_msgs::msg::ModeCmd::MODE_NORMAL &&
      in.mode_cmd.mode != l3_msgs::msg::ModeCmd::MODE_DEGRADED) {
    return false;
  }

  for (const auto& target : in.world_state.targets) {
    if (target.cpa_m < in.cpa_safe_m) {
      return false;
    }
  }

  return true;
}

bool BehaviorActivationCondition::predicate_colreg_avoid(const ArbitrationInputs& in) const {
  // ColregAvoid active if:
  // - In ODD zone A, B, or D
  // - M6 constraint is fresh
  // - Any target with CPA < safe threshold
  // - Health not CRITICAL

  const auto& descriptor = dict_.get(BehaviorType::ColregAvoid);
  if (std::find(descriptor.applicable_zones.begin(), descriptor.applicable_zones.end(),
                in.odd_state.current_zone) == descriptor.applicable_zones.end()) {
    return false;
  }

  if (!in.m6_fresh) {
    return false;
  }

  if (in.odd_state.health == l3_msgs::msg::ODDState::HEALTH_CRITICAL) {
    return false;
  }

  bool has_close_target = false;
  for (const auto& target : in.world_state.targets) {
    if (target.cpa_m < in.cpa_safe_m) {
      has_close_target = true;
      break;
    }
  }

  return has_close_target;
}

bool BehaviorActivationCondition::predicate_dp_hold(const ArbitrationInputs& in) const {
  // DpHold active if:
  // - ODD zone is C
  // - Mode is LIMITED (DP indicator) OR Mode is NORMAL with CONSTRAINT_SPEED/BOTH

  if (in.odd_state.current_zone != l3_msgs::msg::ODDState::ODD_ZONE_C) {
    return false;
  }

  const bool dp_indicated =
      (in.mode_cmd.mode == l3_msgs::msg::ModeCmd::MODE_LIMITED) ||
      (in.mode_cmd.mode == l3_msgs::msg::ModeCmd::MODE_NORMAL &&
       (in.mode_cmd.behavior_constraint == l3_msgs::msg::ModeCmd::CONSTRAINT_SPEED ||
        in.mode_cmd.behavior_constraint == l3_msgs::msg::ModeCmd::CONSTRAINT_BOTH));

  return dp_indicated;
}

bool BehaviorActivationCondition::predicate_berth(const ArbitrationInputs& in) const {
  // Berth active if:
  // - ODD zone is C
  // - Mode is NORMAL and mission_goal has target (eta_to_target_s > 0)
  // - NOT DP hold conditions

  if (in.odd_state.current_zone != l3_msgs::msg::ODDState::ODD_ZONE_C) {
    return false;
  }

  if (in.mode_cmd.mode != l3_msgs::msg::ModeCmd::MODE_NORMAL) {
    return false;
  }

  // Check if in DP hold mode (would exclude berth)
  if (in.mode_cmd.behavior_constraint == l3_msgs::msg::ModeCmd::CONSTRAINT_SPEED ||
      in.mode_cmd.behavior_constraint == l3_msgs::msg::ModeCmd::CONSTRAINT_BOTH) {
    return false;
  }

  // Check if mission goal has valid target
  if (in.mission_goal.eta_to_target_s <= 0.0) {
    return false;
  }

  return true;
}

bool BehaviorActivationCondition::predicate_mrc(BehaviorType type,
                                                 const ArbitrationInputs& in) const {
  // MRC behaviors active if:
  // - Any MRC type if envelope_state == ENVELOPE_MRC_ACTIVE
  // - MrcDrift: mode == EMERGENCY
  // - MrcAnchor: mode == EMERGENCY && health == CRITICAL
  // - MrcHeaveTo: mode == EMERGENCY && current_zone == ODD-D

  if (in.odd_state.envelope_state == l3_msgs::msg::ODDState::ENVELOPE_MRC_ACTIVE) {
    return true;
  }

  switch (type) {
    case BehaviorType::MrcDrift:
      return in.mode_cmd.mode == l3_msgs::msg::ModeCmd::MODE_EMERGENCY;

    case BehaviorType::MrcAnchor:
      return (in.mode_cmd.mode == l3_msgs::msg::ModeCmd::MODE_EMERGENCY &&
              in.odd_state.health == l3_msgs::msg::ODDState::HEALTH_CRITICAL);

    case BehaviorType::MrcHeaveTo:
      return (in.mode_cmd.mode == l3_msgs::msg::ModeCmd::MODE_EMERGENCY &&
              in.odd_state.current_zone == l3_msgs::msg::ODDState::ODD_ZONE_D);

    default:
      return false;
  }
}

}  // namespace mass_l3::m4
