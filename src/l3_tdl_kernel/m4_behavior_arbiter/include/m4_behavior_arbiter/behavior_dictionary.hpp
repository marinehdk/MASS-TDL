#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

enum class BehaviorType : uint8_t {
  TRANSIT = 0,
  COLREG_AVOID = 1,
  DP_HOLD = 2,
  BERTH = 3,
  MRC_DRIFT = 4,
  MRC_ANCHOR = 5,
  MRC_HEAVE_TO = 6,
};

struct BehaviorDescriptor {
  BehaviorType type;
  std::string name;
  double priority_weight;
  std::string activation_rule;
  std::string ivp_function_type;
};

class BehaviorDictionary {
public:
  BehaviorDictionary() = default;

  bool load(const std::string& yaml_path);

  std::vector<BehaviorDescriptor> get_active_subset(
      const ODDStateMsg& odd_state,
      const ModeCmdMsg& mode_cmd) const;

  const std::vector<BehaviorDescriptor>& all_behaviors() const {
    return behaviors_;
  }

  const BehaviorDescriptor* find(BehaviorType type) const;

  size_t size() const { return behaviors_.size(); }

private:
  std::vector<BehaviorDescriptor> behaviors_;
};

}  // namespace mass_l3::m4
