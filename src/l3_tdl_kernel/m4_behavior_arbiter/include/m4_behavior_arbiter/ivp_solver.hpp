#pragma once

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <l3_msgs/msg/tracked_target.hpp>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"

namespace mass_l3::m4 {

/**
 * @brief Hard constraints for the IvP grid search (per detailed design §5.3 Step 3).
 *
 * If any constraint admits no feasible grid cell, solve() returns std::nullopt.
 */
struct IvPHardConstraints {
  /// Allowed heading ranges [min, max] in degrees. Empty = unconstrained.
  /// A cell is feasible if its heading falls in ANY of these ranges.
  std::vector<std::pair<double, double>> heading_allowed_ranges_deg;

  double speed_min_kn;  ///< ODD lower speed bound
  double speed_max_kn;  ///< ODD upper speed bound

  /// Per-target CPA constraint. Empty = no CPA filtering.
  /// [TBD-HAZID] per-cell CPA computation deferred to Task 5 (own-ship state not in scope here)
  std::vector<l3_msgs::msg::TrackedTarget> targets;
  double cpa_safe_m;

  double rot_max_deg_s;  ///< [TBD-HAZID] ROT cap from Capability Manifest; not yet enforced per-cell
};

/**
 * @brief IvP solver result — heading × speed interval with max aggregated utility.
 */
struct IvPSolution {
  double heading_min_deg;
  double heading_max_deg;
  double speed_min_kn;
  double speed_max_kn;
  double optimality_margin;   ///< Best aggregated utility achieved (unnormalized)
  std::string rationale;      ///< SAT-2 summary string for BehaviorPlan.rationale
};

/**
 * @brief Grid-search IvP solver over (heading, speed) domain.
 *
 * Algorithm (detailed design §5.3):
 *   1. Early feasibility: speed_min ≤ speed_max.
 *   2. First pass: find best aggregated utility across all feasible cells.
 *   3. Second pass: collect cells within kTopFeasibleFraction of best; form bounding interval.
 *   4. Return nullopt if no feasible cells or timeout exceeded.
 */
class IvPSolver {
 public:
  /**
   * @brief Construct solver with given domains, strategy, and timeout.
   * @param heading_domain Discretized heading search space.
   * @param speed_domain Discretized speed search space.
   * @param strategy Combination strategy (takes ownership).
   * @param timeout Maximum solve wall-clock time.
   * @pre strategy != nullptr.
   */
  IvPSolver(IvPHeadingDomain heading_domain,
            IvPSpeedDomain speed_domain,
            std::unique_ptr<IvPCombinationStrategy> strategy,
            std::chrono::milliseconds timeout);

  /**
   * @brief Solve aggregated IvP objective subject to hard constraints.
   * @param weighted_fns Active behaviors with weights; may be empty.
   * @param constraints Hard constraints to apply during grid search.
   * @return Optimal interval or std::nullopt on infeasibility / timeout.
   */
  std::optional<IvPSolution> solve(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints) const;

  /// @brief Diagnostics from the most recent solve() call.
  struct SolveDiagnostics {
    std::chrono::microseconds duration{0};
    size_t grid_cells_evaluated{0};
    size_t grid_cells_feasible{0};
  };

  /// @return Diagnostics from last solve() invocation.
  const SolveDiagnostics& last_diagnostics() const { return diag_; }

 private:
  IvPHeadingDomain heading_domain_;
  IvPSpeedDomain   speed_domain_;
  std::unique_ptr<IvPCombinationStrategy> strategy_;
  std::chrono::milliseconds timeout_;
  mutable SolveDiagnostics diag_;

  // [TBD-HAZID] fraction of best utility used to define the feasible interval
  static constexpr double kTopFeasibleFraction = 0.9;

  /// Returns true if psi_deg satisfies the heading constraint (or constraint is empty).
  static bool is_heading_feasible(double psi_deg,
      const std::vector<std::pair<double, double>>& allowed_ranges);

  /// First pass: find best utility and counts across the feasible grid.
  /// Returns best utility found, or -1.0 if no feasible cell.
  double find_best_utility(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints,
      std::chrono::steady_clock::time_point t_start,
      size_t& cells_evaluated_out,
      size_t& cells_feasible_out) const;

  /// Second pass: collect bounding box of cells with utility >= threshold.
  IvPSolution collect_interval(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints,
      double best_utility) const;
};

}  // namespace mass_l3::m4
