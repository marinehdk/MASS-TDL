#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <l3_msgs/msg/tracked_target.hpp>

#include "m4_behavior_arbiter/ivp_combine.hpp"
#include "m4_behavior_arbiter/ivp_domain.hpp"

namespace mass_l3::m4 {

/**
 * @brief Hard constraints for the IvP grid search (detailed design §5.3 Step 3).
 *
 * If constraints admit no feasible grid cell, solve() returns std::nullopt.
 */
struct IvPHardConstraints {
  /// Allowed heading ranges [min, max] in degrees. Empty = unconstrained.
  /// A cell is feasible if its heading falls in ANY of these ranges.
  std::vector<std::pair<double, double>> heading_allowed_ranges_deg;

  double speed_min_kn{0.0};  ///< ODD lower speed bound
  double speed_max_kn{0.0};  ///< ODD upper speed bound

  /// Per-target CPA constraint; empty = no CPA filtering applied.
  /// [TBD-HAZID] Per-cell CPA geometry deferred (own-ship state not in IvPHardConstraints scope)
  std::vector<l3_msgs::msg::TrackedTarget> targets;
  double cpa_safe_m{0.0};

  double rot_max_deg_s{0.0};  ///< [TBD-HAZID] ROT cap from Capability Manifest; not yet enforced per-cell
};

/**
 * @brief IvP solver result — optimal heading × speed interval.
 */
struct IvPSolution {
  double heading_min_deg{0.0};
  double heading_max_deg{0.0};
  double speed_min_kn{0.0};
  double speed_max_kn{0.0};
  double optimality_margin{0.0};  ///< Best aggregated utility achieved across the feasible grid
  std::string rationale{};        ///< SAT-2 summary for BehaviorPlan.rationale
};

/**
 * @brief Grid-search IvP solver over (heading, speed) domain.
 *
 * Two-pass algorithm (detailed design §5.3):
 *   1. First pass: find best aggregated utility across all feasible cells.
 *   2. Second pass: collect bounding interval of cells within kTopFeasibleFraction of best.
 *   3. Return nullopt on: empty active set, infeasible speed bounds, timeout, no feasible cell.
 *
 * No heap allocation in the inner grid loop. Total cost ≈ 20–40 ms (i7 baseline);
 * timeout guard is m4_params.yaml/ivp.timeout_ms = 100 ms.
 *
 * @note solve() is not thread-safe; concurrent calls on the same instance race on diag_.
 */
class IvPSolver {
 public:
  /**
   * @brief Construct solver with domains, aggregation strategy, and timeout.
   * @param heading_domain Discretized heading search space.
   * @param speed_domain Discretized speed search space.
   * @param strategy Aggregation strategy (ownership transferred).
   * @param timeout Maximum solve wall-clock time.
   * @pre strategy != nullptr.
   * @throws std::invalid_argument if strategy is null.
   */
  IvPSolver(IvPHeadingDomain heading_domain,
            IvPSpeedDomain speed_domain,
            std::unique_ptr<IvPCombinationStrategy> strategy,
            std::chrono::milliseconds timeout);

  /**
   * @brief Solve aggregated IvP objective subject to hard constraints.
   * @param weighted_fns Active behaviors with weights; nullopt returned if empty.
   * @param constraints Hard constraints applied during grid search.
   * @return Optimal interval or std::nullopt on infeasibility or timeout.
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

  /// @return Diagnostics from the last solve() invocation.
  const SolveDiagnostics& last_diagnostics() const { return diag_; }

 private:
  IvPHeadingDomain heading_domain_;
  IvPSpeedDomain   speed_domain_;
  std::unique_ptr<IvPCombinationStrategy> strategy_;
  std::chrono::milliseconds timeout_;
  mutable SolveDiagnostics diag_;

  // [TBD-HAZID] Fraction of best utility defining the "feasible interval" boundary
  static constexpr double kTopFeasibleFraction = 0.9;

  /// @return True if psi_deg falls inside any allowed range, or allowed_ranges is empty.
  static bool is_heading_feasible(
      double psi_deg,
      const std::vector<std::pair<double, double>>& allowed_ranges);

  /// First pass: find maximum aggregated utility over the feasible grid.
  /// @return Best utility found, or -1.0 on timeout or no feasible cells.
  double find_best_utility(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints,
      std::chrono::steady_clock::time_point t_start,
      size_t& cells_evaluated_out,
      size_t& cells_feasible_out) const;

  /// Second pass: collect bounding box of cells with utility >= threshold.
  /// @return Solution or nullopt on timeout during second pass.
  std::optional<IvPSolution> collect_interval(
      const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
      const IvPHardConstraints& constraints,
      double best_utility,
      std::chrono::steady_clock::time_point t_start) const;
};

}  // namespace mass_l3::m4
