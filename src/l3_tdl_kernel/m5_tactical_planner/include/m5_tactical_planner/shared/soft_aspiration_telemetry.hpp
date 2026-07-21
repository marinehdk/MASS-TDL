#ifndef MASS_L3_M5_SHARED_SOFT_ASPIRATION_TELEMETRY_HPP_
#define MASS_L3_M5_SHARED_SOFT_ASPIRATION_TELEMETRY_HPP_

// M5 Tactical Planner — L4-T2: soft-aspiration telemetry free function.
// Extracts the core geometry logic from MidMpcAcadosSolver::
// compute_soft_aspiration_telemetry_() so unit tests can exercise
// all exit paths without an acados dependency.  The private method
// delegates to this function after its own kAcadosN / kAcadosNt guards.
//
// Pure C++ (no CasADi, no acados).  Header-only inline, same discipline
// as huber_cost.hpp and cpa_calculator.hpp.
//
// PATH-D (MISRA C++:2023): <60 lines, CC <= 10, no float, no bare new/delete.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace mass_l3::m5::shared {

/// Return value of compute_soft_aspiration_telemetry().
struct SoftAspirationTelemetry {
  double d_min_m{0.0};       ///< minimum Euclidean distance over horizon [m]
  double violation_m{0.0};   ///< max(0, cpa_safe - d_min)  [m]
};

/// Compute the minimum Euclidean distance from any trajectory point to any
/// real target, and the soft-aspiration violation degree.
///
/// @param px_traj    ownship x positions  (size N+1)
/// @param py_traj    ownship y positions  (size N+1)
/// @param target_x   per-target x positions (undrifted)
/// @param target_y   per-target y positions (undrifted)
/// @param n_targets  number of real targets (may be 0)
/// @param cpa_safe_val  soft-aspiration CPA threshold [m]
///
/// @return  {d_min_m, violation_m}.
///          d_min_m == 0.0 when n_targets == 0 or no valid trajectory points;
///          violation_m == max(0.0, cpa_safe_val - d_min_m).
inline SoftAspirationTelemetry compute_soft_aspiration_telemetry(
    const std::vector<double>& px_traj,
    const std::vector<double>& py_traj,
    const std::vector<double>& target_x,
    const std::vector<double>& target_y,
    int n_targets,
    double cpa_safe_val) {
  SoftAspirationTelemetry result{};

  const int n_t = std::max(0, n_targets);
  if (n_t == 0) {
    return result;  // d_min=0, violation=0
  }

  const std::size_t n_pts = std::min(px_traj.size(), py_traj.size());
  if (n_pts == 0) {
    return result;
  }

  const std::size_t n_tu = static_cast<std::size_t>(n_t);
  if (target_x.size() < n_tu || target_y.size() < n_tu) {
    return result;
  }

  double d_min_over_horizon = std::numeric_limits<double>::infinity();
  for (std::size_t k = 0U; k < n_pts; ++k) {
    for (int t = 0; t < n_t; ++t) {
      const std::size_t tt = static_cast<std::size_t>(t);
      const double dx = px_traj[k] - target_x[tt];
      const double dy = py_traj[k] - target_y[tt];
      const double d2 = dx * dx + dy * dy;
      if (d2 > 0.0 && std::isfinite(d2)) {
        const double d_kt = std::sqrt(d2);
        if (d_kt < d_min_over_horizon) {
          d_min_over_horizon = d_kt;
        }
      }
    }
  }

  result.d_min_m = std::isfinite(d_min_over_horizon) ? d_min_over_horizon : 0.0;
  result.violation_m =
      std::isfinite(d_min_over_horizon)
          ? std::max(0.0, cpa_safe_val - d_min_over_horizon)
          : 0.0;

  return result;
}

}  // namespace mass_l3::m5::shared

#endif  // MASS_L3_M5_SHARED_SOFT_ASPIRATION_TELEMETRY_HPP_
