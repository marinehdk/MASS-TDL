#pragma once
#include <memory>
#include <string>
#include <vector>

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs::rules {

class ColregsRuleBase {
 public:
  virtual ~ColregsRuleBase() = default;
  virtual int32_t rule_id() const noexcept = 0;
  virtual std::string rule_name() const noexcept = 0;
  virtual RuleEvaluation evaluate(const TargetGeometricState& geo,
                                  OddDomain odd,
                                  const RuleParameters& params) const = 0;
 protected:
  ColregsRuleBase() = default;
  ColregsRuleBase(const ColregsRuleBase&) = default;
  ColregsRuleBase(ColregsRuleBase&&) = default;
  ColregsRuleBase& operator=(const ColregsRuleBase&) = default;
  ColregsRuleBase& operator=(ColregsRuleBase&&) = default;
};

using RulePtr = std::unique_ptr<ColregsRuleBase>;
using RuleSet = std::vector<RulePtr>;

}  // namespace mass_l3::m6_colregs::rules
