#pragma once

#include <string>

#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs {

class RuleLibraryLoader {
 public:
  explicit RuleLibraryLoader(const std::string& yaml_path);

  rules::RuleSet load_colregs_rules();
  rules::RuleSet load_cevni_rules();  // stub

 private:
  std::string yaml_path_;
};

}  // namespace mass_l3::m6_colregs
