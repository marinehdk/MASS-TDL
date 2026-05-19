#include "m4_behavior_arbiter/ivp_solver.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include <spdlog/spdlog.h>

namespace mass_l3::m4 {

IvPSolver::IvPSolver(IvPHeadingDomain heading_domain,
                     IvPSpeedDomain speed_domain,
                     std::unique_ptr<IvPCombinationStrategy> strategy,
                     std::chrono::milliseconds timeout)
    : heading_domain_(heading_domain)
    , speed_domain_(speed_domain)
    , strategy_(std::move(strategy))
    , timeout_(timeout)
{
  if (!strategy_) {
    throw std::invalid_argument("IvPSolver: strategy must not be null");
  }
}

bool IvPSolver::is_heading_feasible(
    double psi_deg,
    const std::vector<std::pair<double, double>>& allowed_ranges) {
  if (allowed_ranges.empty()) {
    return true;
  }
  for (const auto& r : allowed_ranges) {
    if (psi_deg >= r.first && psi_deg <= r.second) {
      return true;
    }
  }
  return false;
}

double IvPSolver::find_best_utility(
    const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
    const IvPHardConstraints& constraints,
    std::chrono::steady_clock::time_point t_start,
    size_t& cells_evaluated_out,
    size_t& cells_feasible_out) const {
  double best = -1.0;
  size_t cells = 0U;

  for (size_t i = 0U; i < heading_domain_.size(); ++i) {
    // Timeout check once per heading row — avoids now() overhead in inner loop
    if ((std::chrono::steady_clock::now() - t_start) > timeout_) {
      spdlog::warn("[M4] IvPSolver: timeout in first pass after {} cells", cells);
      cells_evaluated_out = cells;
      cells_feasible_out = cells;
      return -1.0;
    }
    const double psi_h = heading_domain_.at(i);
    if (!is_heading_feasible(psi_h, constraints.heading_allowed_ranges_deg)) {
      continue;
    }
    for (size_t j = 0U; j < speed_domain_.size(); ++j) {
      const double u_s = speed_domain_.at(j);
      if (u_s < constraints.speed_min_kn || u_s > constraints.speed_max_kn) {
        continue;
      }
      ++cells;
      const double util = strategy_->combine(psi_h, u_s, weighted_fns);
      if (util > best) {
        best = util;
      }
    }
  }

  cells_evaluated_out = cells;
  cells_feasible_out = cells;
  return best;
}

std::optional<IvPSolution> IvPSolver::collect_interval(
    const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
    const IvPHardConstraints& constraints,
    double best_utility,
    std::chrono::steady_clock::time_point t_start) const {
  const double threshold = kTopFeasibleFraction * best_utility;
  double h_min = std::numeric_limits<double>::max();
  double h_max = -1.0;
  double u_min = std::numeric_limits<double>::max();
  double u_max = -1.0;
  size_t top_cells = 0U;

  for (size_t i = 0U; i < heading_domain_.size(); ++i) {
    // Timeout check once per heading row — matches first-pass granularity
    if ((std::chrono::steady_clock::now() - t_start) > timeout_) {
      spdlog::warn("[M4] IvPSolver: timeout in second pass after {} top cells", top_cells);
      return std::nullopt;
    }
    const double psi_h = heading_domain_.at(i);
    if (!is_heading_feasible(psi_h, constraints.heading_allowed_ranges_deg)) {
      continue;
    }
    for (size_t j = 0U; j < speed_domain_.size(); ++j) {
      const double u_s = speed_domain_.at(j);
      if (u_s < constraints.speed_min_kn || u_s > constraints.speed_max_kn) {
        continue;
      }
      const double util = strategy_->combine(psi_h, u_s, weighted_fns);
      if (util >= threshold) {
        if (psi_h < h_min) { h_min = psi_h; }
        if (psi_h > h_max) { h_max = psi_h; }
        if (u_s < u_min) { u_min = u_s; }
        if (u_s > u_max) { u_max = u_s; }
        ++top_cells;
      }
    }
  }

  if (top_cells == 0U) {
    spdlog::error("[M4] IvPSolver: second pass found no top cells (logic error)");
    return std::nullopt;
  }

  const std::string rationale = "IvP: best_util=" + std::to_string(best_utility)
      + " top_cells=" + std::to_string(top_cells);

  return IvPSolution{h_min, h_max, u_min, u_max, best_utility, rationale};
}

std::optional<IvPSolution> IvPSolver::solve(
    const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
    const IvPHardConstraints& constraints) const {
  if (weighted_fns.empty()) {
    return std::nullopt;
  }
  if (constraints.speed_min_kn > constraints.speed_max_kn) {
    return std::nullopt;
  }

  const auto t_start = std::chrono::steady_clock::now();
  diag_ = {};

  const double best = find_best_utility(
      weighted_fns, constraints, t_start,
      diag_.grid_cells_evaluated, diag_.grid_cells_feasible);

  if (best < 0.0) {
    diag_.duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t_start);
    return std::nullopt;
  }

  auto result = collect_interval(weighted_fns, constraints, best, t_start);
  diag_.duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t_start);
  return result;
}

}  // namespace mass_l3::m4
