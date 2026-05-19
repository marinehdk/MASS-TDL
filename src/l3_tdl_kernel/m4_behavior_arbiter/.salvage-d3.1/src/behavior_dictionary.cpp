#include "m4_behavior_arbiter/behavior_dictionary.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>

#include <spdlog/spdlog.h>
#include <yaml-cpp/yaml.h>

namespace mass_l3::m4 {

// Static code names for string_view stability
static constexpr std::array<std::string_view, kBehaviorCount> kCodeNames{
    "TRANSIT", "COLREG_AVOID", "DP_HOLD", "BERTH", "MRC_DRIFT", "MRC_ANCHOR", "MRC_HEAVE_TO"};

ErrorCode BehaviorDictionary::parse_behavior_entry(const YAML::Node& behavior_node, size_t idx) {
  if (!behavior_node["name"] || !behavior_node["type"] ||
      !behavior_node["applicable_zones"] || !behavior_node["default_weight"]) {
    spdlog::error("[M4] BehaviorDictionary: missing required fields at index {}", idx);
    return ErrorCode::YamlMissingKey;
  }

  const uint8_t type_value = behavior_node["type"].as<uint8_t>();
  if (type_value >= kBehaviorCount) {
    spdlog::error("[M4] BehaviorDictionary: invalid behavior type {} at index {}", type_value, idx);
    return ErrorCode::YamlInvalidValue;
  }

  const auto& zones_node = behavior_node["applicable_zones"];
  if (!zones_node.IsSequence()) {
    spdlog::error("[M4] BehaviorDictionary: applicable_zones must be sequence at index {}", idx);
    return ErrorCode::YamlInvalidValue;
  }

  std::vector<uint8_t> zones;
  for (const auto& zone_node : zones_node) {
    zones.push_back(zone_node.as<uint8_t>());
  }

  const double weight = behavior_node["default_weight"].as<double>();
  const auto behavior_type = static_cast<BehaviorType>(type_value);

  entries_[static_cast<size_t>(type_value)] = BehaviorDescriptor{
      .type = behavior_type,
      .code_name = kCodeNames[static_cast<size_t>(type_value)],
      .applicable_zones = zones,
      .default_weight = weight,
  };

  return ErrorCode::Ok;
}

ErrorCode BehaviorDictionary::load(const std::string& yaml_path) {
  try {
    std::ifstream file(yaml_path);
    if (!file.is_open()) {
      spdlog::error("[M4] BehaviorDictionary: cannot open YAML: {}", yaml_path);
      return ErrorCode::YamlLoadFailed;
    }

    const YAML::Node root = YAML::Load(file);

    if (!root["behaviors"]) {
      spdlog::error("[M4] BehaviorDictionary: missing 'behaviors' key");
      return ErrorCode::YamlMissingKey;
    }

    const auto& behaviors_node = root["behaviors"];
    if (!behaviors_node.IsSequence()) {
      spdlog::error("[M4] BehaviorDictionary: 'behaviors' must be a sequence");
      return ErrorCode::YamlInvalidValue;
    }

    if (behaviors_node.size() != kBehaviorCount) {
      spdlog::error("[M4] BehaviorDictionary: expected {} behaviors, found {}",
                    kBehaviorCount, behaviors_node.size());
      return ErrorCode::YamlInvalidValue;
    }

    for (size_t i = 0; i < behaviors_node.size(); ++i) {
      const ErrorCode ec = parse_behavior_entry(behaviors_node[i], i);
      if (ec != ErrorCode::Ok) {
        return ec;
      }
    }

    loaded_ = true;
    spdlog::info("[M4] BehaviorDictionary: loaded {} behaviors from {}", kBehaviorCount, yaml_path);
    return ErrorCode::Ok;

  } catch (const YAML::Exception& e) {
    spdlog::error("[M4] BehaviorDictionary: YAML error: {}", e.what());
    return ErrorCode::YamlLoadFailed;
  } catch (const std::exception& e) {
    spdlog::error("[M4] BehaviorDictionary: unexpected error: {}", e.what());
    return ErrorCode::YamlLoadFailed;
  }
}

std::vector<BehaviorDescriptor> BehaviorDictionary::get_active_subset(uint8_t odd_zone) const {
  if (!loaded_) {
    spdlog::warn("BehaviorDictionary::get_active_subset called before load()");
    return {};
  }

  std::vector<BehaviorDescriptor> subset;

  for (const auto& entry : entries_) {
    const auto& zones = entry.applicable_zones;
    if (std::find(zones.begin(), zones.end(), odd_zone) != zones.end()) {
      subset.push_back(entry);
    }
  }

  return subset;
}

const BehaviorDescriptor& BehaviorDictionary::get(BehaviorType type) const {
  if (!loaded_) {
    throw std::logic_error("BehaviorDictionary::get called before load()");
  }
  const size_t idx = static_cast<size_t>(type);
  if (idx >= kBehaviorCount) {
    throw std::out_of_range("BehaviorDictionary::get: invalid BehaviorType index");
  }
  return entries_[idx];
}

}  // namespace mass_l3::m4
