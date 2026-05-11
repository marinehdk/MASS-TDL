#include "m6_colregs_reasoner/rule_library_loader.hpp"

#include <yaml-cpp/yaml.h>  // NOLINT(misc-include-cleaner)

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

#include "m6_colregs_reasoner/rules/colregs/rule5_lookout.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule6_safe_speed.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule7_risk_of_collision.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule8_action_to_avoid.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule13_overtaking.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule14_head_on.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule15_crossing.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule16_give_way.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule17_stand_on.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule18_responsibilities.hpp"
#include "m6_colregs_reasoner/rules/colregs/rule19_restricted_visibility.hpp"

namespace mass_l3::m6_colregs {

namespace {

rules::RulePtr create_rule_by_id(int32_t rule_id) {
  switch (rule_id) {
    case 5:
      return std::make_unique<rules::colregs::Rule5_Lookout>();
    case 6:
      return std::make_unique<rules::colregs::Rule6_SafeSpeed>();
    case 7:
      return std::make_unique<rules::colregs::Rule7_RiskOfCollision>();
    case 8:
      return std::make_unique<rules::colregs::Rule8_ActionToAvoid>();
    case 13:
      return std::make_unique<rules::colregs::Rule13_Overtaking>();
    case 14:
      return std::make_unique<rules::colregs::Rule14_HeadOn>();
    case 15:
      return std::make_unique<rules::colregs::Rule15_Crossing>();
    case 16:
      return std::make_unique<rules::colregs::Rule16_GiveWay>();
    case 17:
      return std::make_unique<rules::colregs::Rule17_StandOn>();
    case 18:
      return std::make_unique<rules::colregs::Rule18_Responsibilities>();
    case 19:
      return std::make_unique<rules::colregs::Rule19_RestrictedVisibility>();
    default:
      return nullptr;
  }
}

}  // anonymous namespace

// NOLINTNEXTLINE(modernize-pass-by-value)
RuleLibraryLoader::RuleLibraryLoader(const std::string& yaml_path) : yaml_path_(yaml_path) {}

rules::RuleSet RuleLibraryLoader::load_colregs_rules() {
  rules::RuleSet rules;

  YAML::Node config = YAML::LoadFile(yaml_path_);  // NOLINT(misc-include-cleaner)
  const YAML::Node kColregsNode = config["colregs_rules"];  // NOLINT(misc-include-cleaner)

  if (!kColregsNode) {
    return rules;  // empty; no COLREGs rules configured
  }

  for (const auto& entry : kColregsNode) {
    const auto kRuleId = static_cast<int32_t>(entry["rule_id"].as<int32_t>());
    const bool kEnabled = entry["enabled"].as<bool>(true);

    if (!kEnabled) {
      continue;
    }

    auto rule = create_rule_by_id(kRuleId);
    if (rule) {
      rules.push_back(std::move(rule));
    }
  }

  return rules;
}

// NOLINTNEXTLINE(readability-convert-member-functions-to-static)
rules::RuleSet RuleLibraryLoader::load_cevni_rules() {
  // Stub: CEVNI (European inland waterways) rules not yet implemented
  return rules::RuleSet{};
}

}  // namespace mass_l3::m6_colregs
