#pragma once

#include "m6_colregs_reasoner/types.hpp"

namespace mass_l3::m6_colregs {

class PhaseClassifier {
 public:
  PhaseClassifier() = default;
  TimingPhase classify(double tcpa_s, const RuleParameters& params) const;
};

}  // namespace mass_l3::m6_colregs
