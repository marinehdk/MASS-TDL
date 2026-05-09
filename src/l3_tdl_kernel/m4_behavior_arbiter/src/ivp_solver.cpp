#include "m4_behavior_arbiter/ivp_solver.hpp"

#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <sstream>

namespace mass_l3::m4 {

IvPSolver::IvPSolver(IvPHeadingDomain heading_domain,
                     IvPSpeedDomain speed_domain,
                     std::unique_ptr<IvPCombinationStrategy> strategy,
                     std::chrono::milliseconds timeout)
    : heading_domain_(heading_domain),
      speed_domain_(speed_domain),
      strategy_(std::move(strategy)),
      timeout_(timeout) {
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
  for (const auto& range : allowed_ranges) {
    if (psi_deg >= range.first && psi_deg <= range.second) {
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
  cells_evaluated_out = 0;
  cells_feasible_out = 0;

  for (size_t i = 0; i < heading_domain_.size(); ++i) {
    double psi_h = heading_domain_.at(i);
    if (!is_heading_feasible(psi_h, constraints.heading_allowed_ranges_deg)) {
      continue;
    }

    for (size_t j = 0; j < speed_domain_.size(); ++j) {
      double u_s = speed_domain_.at(j);
      if (u_s < constraints.speed_min_kn || u_s > constraints.speed_max_kn) {
        continue;
      }

      ++cells_evaluated_out;

      // Timeout check
      if ((std::chrono::steady_clock::now() - t_start) > timeout_) {
        return -1.0;
      }

      ++cells_feasible_out;
      double util = strategy_->combine(psi_h, u_s, weighted_fns);
      if (util > best) {
        best = util;
      }
    }
  }

  return best;
}

IvPSolution IvPSolver::collect_interval(
    const std::vector<IvPCombinationStrategy::WeightedFunction>& weighted_fns,
    const IvPHardConstraints& constraints,
    double best_utility) const {
  double threshold = kTopFeasibleFraction * best_utility;
  double h_min = std::numeric_limits<double>::max();
  double h_max = -1.0;
  double u_min = std::numeric_limits<double>::max();
  double u_max = -1.0;

  for (size_t i = 0; i < heading_domain_.size(); ++i) {
    double psi_h = heading_domain_.at(i);
    if (!is_heading_feasible(psi_h, constraints.heading_allowed_ranges_deg)) {
      continue;
    }

    for (size_t j = 0; j < speed_domain_.size(); ++j) {
      double u_s = speed_domain_.at(j);
      if (u_s < constraints.speed_min_kn || u_s > constraints.speed_max_kn) {
        continue;
      }

      double util = strategy_->combine(psi_h, u_s, weighted_fns);
      if (util >= threshold) {
        if (psi_h < h_min) h_min = psi_h;
        if (psi_h > h_max) h_max = psi_h;
        if (u_s < u_min) u_min = u_s;
        if (u_s > u_max) u_max = u_s;
      }
    }
  }

  // Build rationale string
  std::ostringstream oss;
  oss << "IvP: best_util=" << best_utility
      << " cells_feasible=" << diag_.grid_cells_feasible;
  std::string rationale = oss.str();

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

  auto t_start = std::chrono::steady_clock::now();
  diag_ = {};

  double best = find_best_utility(weighted_fns, constraints, t_start,
                                   diag_.grid_cells_evaluated,
                                   diag_.grid_cells_feasible);
  diag_.duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - t_start);

  if (best < 0.0) {
    return std::nullopt;
  }

  return collect_interval(weighted_fns, constraints, best);
}

}  // namespace mass_l3::m4
