#pragma once

#include "m6_colregs_reasoner/rules/colregs_rule_base.hpp"

namespace mass_l3::m6_colregs::rules::colregs {

class Rule8_ActionToAvoid final : public ColregsRuleBase {
 public:
  Rule8_ActionToAvoid() = default;
  int32_t rule_id() const noexcept override { return 8; }
  std::string rule_name() const noexcept override { return "Rule8_ActionToAvoid"; }
  RuleEvaluation evaluate(const TargetGeometricState& geo,
                          OddDomain odd,
                          const RuleParameters& params) const override;
};

}  // namespace mass_l3::m6_colregs::rules::colregs
