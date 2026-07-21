// L4-T2: unit tests for compute_soft_aspiration_telemetry() free function.
// Exercises all exit paths of the pure-geometry d_min / violation computation
// that underpins MidMpcAcadosSolver::compute_soft_aspiration_telemetry_().
// No acados dependency — builds and runs with M5_USE_ACADOS=OFF.
//
// PATH-D (MISRA C++:2023): no float, no bare new/delete.

#include "m5_tactical_planner/shared/soft_aspiration_telemetry.hpp"

#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using mass_l3::m5::shared::compute_soft_aspiration_telemetry;
using mass_l3::m5::shared::SoftAspirationTelemetry;

namespace {

// --------------- helpers ---------------------------------------------------

// Build an N+1 point ownship trajectory straight along +x (y=0).
// start_x: initial x position [m];  step_x: spacing between points [m].
std::vector<double> straight_x_traj(double start_x, double step_x, int n_pts) {
  std::vector<double> out(static_cast<std::size_t>(n_pts), 0.0);
  for (int k = 0; k < n_pts; ++k) {
    out[static_cast<std::size_t>(k)] = start_x + step_x * static_cast<double>(k);
  }
  return out;
}

std::vector<double> constant_y_traj(double y_val, int n_pts) {
  return std::vector<double>(static_cast<std::size_t>(n_pts), y_val);
}

// --------------- test cases ------------------------------------------------

// T1: Normal case — target at (0, 500), ownship along +x at y=0.
// Minimum distance ~500 m.  cpa_safe = 1852 → violation = 1852-500 = 1352.
TEST(SoftAspirationTelemetry, NormalCase_TargetAt500m) {
  constexpr int n_pts = 81;  // kAcadosN + 1 for production horizon
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);  // 0 .. 1200 m
  const auto py = constant_y_traj(0.0, n_pts);          // ownship at y=0

  // Single target at (0, 500)
  const std::vector<double> tx{0.0};
  const std::vector<double> ty{500.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 1, cpa_safe);

  EXPECT_NEAR(r.d_min_m, 500.0, 1e-6);
  EXPECT_NEAR(r.violation_m, cpa_safe - 500.0, 1e-6);
  EXPECT_GT(r.violation_m, 0.0);
}

// T2: Zero targets — empty targets vector → d_min=0, violation=0.
TEST(SoftAspirationTelemetry, ZeroTargets_ReturnsZero) {
  constexpr int n_pts = 81;
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{};
  const std::vector<double> ty{};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 0, cpa_safe);

  EXPECT_DOUBLE_EQ(r.d_min_m, 0.0);
  EXPECT_DOUBLE_EQ(r.violation_m, 0.0);
}

// T3: Target inside cpa_hard — target at (0, 100) → d_min ≈ 100 m,
// violation = 1852 - 100 = 1752 > 0.
TEST(SoftAspirationTelemetry, TargetInsideCpaHard_ViolationPositive) {
  constexpr int n_pts = 81;
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{0.0};
  const std::vector<double> ty{100.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 1, cpa_safe);

  EXPECT_NEAR(r.d_min_m, 100.0, 1e-6);
  EXPECT_NEAR(r.violation_m, cpa_safe - 100.0, 1e-6);
  EXPECT_GT(r.violation_m, 0.0);
}

// T4: Target outside cpa_safe — target at (0, 3000) → d_min ≈ 3000 m,
// violation = max(0, 1852-3000) = 0.
TEST(SoftAspirationTelemetry, TargetOutsideCpaSafe_ViolationZero) {
  constexpr int n_pts = 81;
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{0.0};
  const std::vector<double> ty{3000.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 1, cpa_safe);

  EXPECT_NEAR(r.d_min_m, 3000.0, 1e-6);
  EXPECT_DOUBLE_EQ(r.violation_m, 0.0);
}

// T5 (extra): Negative n_targets — treated as zero, returns {0,0}.
TEST(SoftAspirationTelemetry, NegativeTargets_ReturnsZero) {
  constexpr int n_pts = 81;
  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{0.0};
  const std::vector<double> ty{500.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, -1, 1852.0);

  EXPECT_DOUBLE_EQ(r.d_min_m, 0.0);
  EXPECT_DOUBLE_EQ(r.violation_m, 0.0);
}

// T6 (extra): Multiple targets — closest target at y=200m among {500,200,800}.
// d_min ≈ 200 m.
TEST(SoftAspirationTelemetry, MultipleTargets_ClosestDominates) {
  constexpr int n_pts = 81;
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{0.0, 0.0, 0.0};
  const std::vector<double> ty{500.0, 200.0, 800.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 3, cpa_safe);

  EXPECT_NEAR(r.d_min_m, 200.0, 1e-6);
  EXPECT_NEAR(r.violation_m, cpa_safe - 200.0, 1e-6);
  EXPECT_GT(r.violation_m, 0.0);
}

// T7 (extra): Empty trajectory — returns {0,0}.
TEST(SoftAspirationTelemetry, EmptyTrajectory_ReturnsZero) {
  const std::vector<double> px{};
  const std::vector<double> py{};
  const std::vector<double> tx{100.0};
  const std::vector<double> ty{200.0};

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 1, 1852.0);

  EXPECT_DOUBLE_EQ(r.d_min_m, 0.0);
  EXPECT_DOUBLE_EQ(r.violation_m, 0.0);
}

// T8 (extra): Target exactly at cpa_safe distance → violation == 0.
TEST(SoftAspirationTelemetry, TargetAtCpaSafeBoundary_ViolationZero) {
  constexpr int n_pts = 81;
  constexpr double cpa_safe = 1852.0;

  const auto px = straight_x_traj(0.0, 15.0, n_pts);
  const auto py = constant_y_traj(0.0, n_pts);
  const std::vector<double> tx{0.0};
  const std::vector<double> ty{cpa_safe};  // exactly at cpa_safe

  const auto r = compute_soft_aspiration_telemetry(px, py, tx, ty, 1, cpa_safe);

  EXPECT_NEAR(r.d_min_m, cpa_safe, 1e-6);
  EXPECT_DOUBLE_EQ(r.violation_m, 0.0);
}

}  // namespace
