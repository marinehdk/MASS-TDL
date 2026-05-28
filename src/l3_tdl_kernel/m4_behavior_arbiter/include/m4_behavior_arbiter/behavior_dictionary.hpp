#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

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

  void set_priority_weight(BehaviorType type, double weight) {
    for (auto& b : behaviors_) {
      if (b.type == type) {
        b.priority_weight = weight;
        return;
      }
    }
  }

  void add_behavior(const BehaviorDescriptor& desc) {
    behaviors_.push_back(desc);
  }

  size_t size() const { return behaviors_.size(); }

private:
  std::vector<BehaviorDescriptor> behaviors_;
};

}  // namespace mass_l3::m4
