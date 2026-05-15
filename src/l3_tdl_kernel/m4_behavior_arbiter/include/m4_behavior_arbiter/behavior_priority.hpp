#pragma once

#include <vector>

#include "m4_behavior_arbiter/behavior_dictionary.hpp"

namespace mass_l3::m4 {

class BehaviorPriority {
public:
  BehaviorPriority() = default;

  static BehaviorDescriptor select_primary(
      const std::vector<BehaviorDescriptor>& active_set);

  static bool has_mrc(
      const std::vector<BehaviorDescriptor>& active_set);
};

}  // namespace mass_l3::m4
