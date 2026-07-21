// M5 Tactical Planner — huber_cost: precise Huber loss pure function.
//
// P2 T2 (Eriksen relative-track t_b + Huber, VR-07b). Header-only inline pure
// function with NO CasADi/acados dependency. This is the numeric ORACLE that:
//   (a) T3's acados MX Huber helper mirrors symbolically,
//   (b) T3's formulation unit test compares MX Huber output against, and
//   (c) T6's parity/benchmark cross-checks.
//
// Used for the SQP-friendly position cost: quadratic near zero, linear far (no
// exponential pull-back when the solver is pushed off-route by an obstacle).
//
// PATH-D (MISRA C++:2023): noexcept, branch-light, no dynamic allocation.
#pragma once
#include <cmath>

namespace mass_l3::m5::shared {

// Huber loss (VR-07b): 0.5*l^2 if |l|<=delta_h, else delta_h*(|l|-0.5*delta_h).
// C0 continuous, C1 smooth at delta_h (one-sided derivative = delta_h in the
// linear region). Used for SQP-friendly position cost: quadratic near zero,
// linear far (no exponential pull-back when pushed off-route by an obstacle).
inline double huber_cost(double l, double delta_h) noexcept {
  const double a = std::fabs(l);
  return (a <= delta_h) ? 0.5 * l * l : delta_h * (a - 0.5 * delta_h);
}

}  // namespace mass_l3::m5::shared
