#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "m4_behavior_arbiter/error.hpp"
#include "m4_behavior_arbiter/types.hpp"

namespace mass_l3::m4 {

struct BehaviorDescriptor {
  BehaviorType        type;
  std::string_view    code_name;
  std::vector<uint8_t> applicable_zones;  // ODD zone values (0=A,1=B,2=C,3=D)
  double              default_weight;     // [TBD-HAZID] from YAML
};

class BehaviorDictionary {
 public:
  BehaviorDictionary() = default;
  ~BehaviorDictionary() = default;

  BehaviorDictionary(const BehaviorDictionary&) = delete;
  BehaviorDictionary& operator=(const BehaviorDictionary&) = delete;

  /**
   * @brief Load behavior definitions from a YAML file.
   * @param yaml_path Absolute or relative path to behavior_definitions.yaml.
   * @return ErrorCode::Ok on success; YamlLoadFailed / YamlMissingKey / YamlInvalidValue on failure.
   * @post is_loaded() == true iff Ok is returned.
   */
  ErrorCode load(const std::string& yaml_path);

  /**
   * @brief Return behaviors whose applicable_zones include odd_zone.
   * @param odd_zone Current ODD zone value (0=A, 1=B, 2=C, 3=D).
   * @return Filtered subset; empty vector if not loaded or zone matches none.
   * @pre is_loaded() == true (logs warn and returns {} otherwise).
   */
  std::vector<BehaviorDescriptor> get_active_subset(uint8_t odd_zone) const;

  /**
   * @brief Look up a BehaviorDescriptor by enum.
   * @param type The behavior to look up.
   * @return Const reference to the descriptor.
   * @pre is_loaded() == true.
   * @throws std::logic_error if not loaded.
   * @throws std::out_of_range if type value is out of range.
   */
  const BehaviorDescriptor& get(BehaviorType type) const;

  /**
   * @brief Return true after a successful load() call.
   * @return true if load() completed without error.
   */
  bool is_loaded() const { return loaded_; }

 private:
  /**
   * @brief Parse a single behavior entry from a YAML node.
   * @param node The YAML node for one behavior entry.
   * @param idx The index in the behaviors sequence (for error messages).
   * @return ErrorCode::Ok on success, or an error code on validation failure.
   */
  ErrorCode parse_behavior_entry(const YAML::Node& node, size_t idx);

  std::array<BehaviorDescriptor, kBehaviorCount> entries_{};
  bool loaded_{false};
};

}  // namespace mass_l3::m4
