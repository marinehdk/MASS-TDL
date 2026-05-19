#include "m4_behavior_arbiter/behavior_priority.hpp"

#include <algorithm>

#include <spdlog/spdlog.h>

#include <l3_msgs/msg/odd_state.hpp>  // for ODDState::HEALTH_CRITICAL constant

namespace mass_l3::m4 {

namespace {

bool is_mrc_type(const BehaviorType bt) {
  return bt == BehaviorType::MrcDrift
      || bt == BehaviorType::MrcAnchor
      || bt == BehaviorType::MrcHeaveTo;
}

}  // namespace

bool BehaviorPriority::has_mrc(const std::vector<BehaviorType>& active_set) {
  return std::any_of(active_set.cbegin(), active_set.cend(), is_mrc_type);
}

BehaviorType BehaviorPriority::select_primary(
    const std::vector<BehaviorType>& active_set,
    const IvPSolution& /*ivp_solution*/,
    const ArbitrationInputs& inputs) const {
  // Rule 1: CRITICAL health forces MRC_DRIFT unconditionally (IEC 61508 SIL-2 path)
  if (inputs.odd_state.health == l3_msgs::msg::ODDState::HEALTH_CRITICAL) {
    return BehaviorType::MrcDrift;
  }

  // Rule 2: Any MRC_* behavior active → freeze IvP, return first MRC found
  for (const BehaviorType bt : active_set) {
    if (is_mrc_type(bt)) {
      return bt;
    }
  }

  // Rule 3: ColregAvoid overrides mission behaviors (safety-critical avoidance)
  for (const BehaviorType bt : active_set) {
    if (bt == BehaviorType::ColregAvoid) {
      return BehaviorType::ColregAvoid;
    }
  }

  // Normal path: IvP solution is valid; return the first active behavior
  if (!active_set.empty()) {
    return active_set.front();
  }

  // Fallback: empty active_set is a logic error (BehaviorActivation always includes MRC)
  spdlog::error("[M4] BehaviorPriority: empty active_set in select_primary — fallback to MrcDrift");
  return BehaviorType::MrcDrift;
}

}  // namespace mass_l3::m4
