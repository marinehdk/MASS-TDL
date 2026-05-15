#include <algorithm>

#include "m4_behavior_arbiter/behavior_priority.hpp"

namespace mass_l3::m4 {

BehaviorDescriptor BehaviorPriority::select_primary(
    const std::vector<BehaviorDescriptor>& active_set) {
  // active_set is guaranteed non-empty by the caller
  // (arbitration_timer_callback gates on odd+world received)
  return *std::max_element(
      active_set.begin(), active_set.end(),
      [](const BehaviorDescriptor& a, const BehaviorDescriptor& b) {
        return a.priority_weight < b.priority_weight;
      });
}

bool BehaviorPriority::has_mrc(
    const std::vector<BehaviorDescriptor>& active_set) {
  return std::any_of(
      active_set.begin(), active_set.end(),
      [](const BehaviorDescriptor& b) {
        return b.type == BehaviorType::MRC_DRIFT ||
               b.type == BehaviorType::MRC_ANCHOR ||
               b.type == BehaviorType::MRC_HEAVE_TO;
      });
}

}  // namespace mass_l3::m4
