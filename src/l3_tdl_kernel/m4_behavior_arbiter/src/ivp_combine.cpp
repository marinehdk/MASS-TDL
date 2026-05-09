#include "m4_behavior_arbiter/ivp_combine.hpp"

namespace mass_l3::m4 {

double WeightedSumCombination::combine(
    double psi_deg, double u_kn,
    const std::vector<WeightedFunction>& fns) const {
  double aggregated = 0.0;
  for (const auto& wf : fns) {
    aggregated += wf.weight * wf.function.evaluate(psi_deg, u_kn);
  }
  return aggregated;
}

}  // namespace mass_l3::m4
