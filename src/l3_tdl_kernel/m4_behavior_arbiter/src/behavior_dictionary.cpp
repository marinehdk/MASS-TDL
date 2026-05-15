#include "m4_behavior_arbiter/behavior_dictionary.hpp"

#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

bool BehaviorDictionary::load(const std::string& yaml_path) {
  try {
    YAML::Node root = YAML::LoadFile(yaml_path);
    const auto& behaviors_node = root["behaviors"];
    if (!behaviors_node) {
      spdlog::error("[BehaviorDictionary] YAML missing 'behaviors' key in {}",
                    yaml_path);
      return false;
    }

    behaviors_.clear();
    for (const auto& node : behaviors_node) {
      BehaviorDescriptor desc;
      desc.type = static_cast<BehaviorType>(node["type"].as<int>());
      desc.name = node["name"].as<std::string>();
      desc.priority_weight = node["priority_weight"].as<double>();
      desc.activation_rule = node["activation_rule"].as<std::string>();
      desc.ivp_function_type = node["ivp_function_type"].as<std::string>();
      behaviors_.push_back(desc);
    }

    spdlog::info("[BehaviorDictionary] Loaded {} behaviors from {}",
                 behaviors_.size(), yaml_path);
    return true;

  } catch (const YAML::Exception& e) {
    spdlog::error("[BehaviorDictionary] YAML parse error: {}", e.what());
    return false;
  }
}

std::vector<BehaviorDescriptor> BehaviorDictionary::get_active_subset(
    const ODDStateMsg& odd_state,
    const ModeCmdMsg& mode_cmd) const {
  (void)odd_state;
  (void)mode_cmd;

  // Task 2 stub: return Transit behaviour as always active.
  // Full activation-logic delegation to BehaviorActivationCondition
  // will be implemented in D3.1 Task 2.
  std::vector<BehaviorDescriptor> active;
  for (const auto& b : behaviors_) {
    if (b.type == BehaviorType::TRANSIT) {
      active.push_back(b);
    }
  }
  return active;
}

const BehaviorDescriptor* BehaviorDictionary::find(BehaviorType type) const {
  for (const auto& b : behaviors_) {
    if (b.type == type) {
      return &b;
    }
  }
  return nullptr;
}

}  // namespace mass_l3::m4
