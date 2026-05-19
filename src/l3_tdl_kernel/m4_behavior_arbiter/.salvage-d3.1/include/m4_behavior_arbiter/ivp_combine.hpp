#pragma once

#include <vector>

#include "m4_behavior_arbiter/ivp_function.hpp"

namespace mass_l3::m4 {

/**
 * @brief Combines N weighted IvP functions into a single aggregated utility.
 *
 * Strategy interface for multi-objective aggregation per [R3] Benjamin et al. 2010.
 * Currently supports WeightedSumCombination only (detailed design §5.3).
 */
class IvPCombinationStrategy {
 public:
  struct WeightedFunction {
    double weight;              ///< From BehaviorWeightSet for current health state
    IvPFunctionDefault function;
  };

  IvPCombinationStrategy() = default;
  virtual ~IvPCombinationStrategy() = default;

  IvPCombinationStrategy(const IvPCombinationStrategy&) = delete;
  IvPCombinationStrategy& operator=(const IvPCombinationStrategy&) = delete;

  /**
   * @brief Aggregate weighted utility at a single (psi, u) point.
   * @param psi_deg Heading in degrees.
   * @param u_kn Speed in knots.
   * @param fns List of weighted functions to aggregate.
   * @return Aggregated utility (may exceed 1.0 with multiple behaviors).
   */
  virtual double combine(double psi_deg, double u_kn,
                         const std::vector<WeightedFunction>& fns) const = 0;
};

/**
 * @brief Weighted-sum aggregation: aggregated(ψ, u) = Σ_i (w_i · f_i(ψ, u)).
 */
class WeightedSumCombination : public IvPCombinationStrategy {
 public:
  /**
   * @brief Compute weighted sum of utilities at (psi_deg, u_kn).
   * @param psi_deg Heading in degrees.
   * @param u_kn Speed in knots.
   * @param fns Weighted functions to aggregate.
   * @return Sum of (weight_i * evaluate_i(psi_deg, u_kn)).
   */
  double combine(double psi_deg, double u_kn,
                 const std::vector<WeightedFunction>& fns) const override;
};

}  // namespace mass_l3::m4
