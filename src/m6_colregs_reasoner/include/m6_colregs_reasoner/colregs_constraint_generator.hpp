#pragma once

#include <vector>

#include "l3_msgs/msg/colregs_constraint.hpp"
#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class ConstraintGenerator {
 public:
  l3_msgs::msg::COLREGsConstraint generate(
      const std::vector<RuleEvaluation>& evaluations,
      const RuleParameters& params, double confidence) const;
};

}  // namespace mass_l3::m6_colregs
