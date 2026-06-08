// test/unit/test_mid_mpc_solver.cpp
// Task 2.2 integration tests: real IPOPT solves over 5 scenarios.
// These tests link against CasADi + IPOPT and are NOT fast (1–5 s total).
// Grouped under MidMpcNlpTest fixture that builds NLP once in SetUp.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

using mass_l3::m5::ConstraintInputs;
using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;

// ---------------------------------------------------------------------------
// Fixture — builds NLP once; reused by all tests.
// N=8 (small horizon) balances test speed vs scenario realism.
// ---------------------------------------------------------------------------
class MidMpcNlpTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MidMpcNlpFormulation::Config cfg;
    cfg.n_horizon   = 8;
    cfg.dt_s        = 5.0;
    cfg.w_colreg    = 1000.0;
    cfg.w_dist      = 10.0;
    cfg.w_vel       = 1.0;
    cfg.max_targets = 4;
    formulation_ = std::make_unique<MidMpcNlpFormulation>(cfg);
    formulation_->build_symbolic_graph();

    MidMpcSolver::IpoptOptions opts;
    opts.max_iter  = 150;
    opts.tol       = 1.0e-4;
    opts.timeout_s = 2.0;  // relaxed for CI (IPOPT cold start may spike on first call)
    solver_ = std::make_unique<MidMpcSolver>(*formulation_, opts);
  }

  std::unique_ptr<MidMpcNlpFormulation> formulation_;
  std::unique_ptr<MidMpcSolver> solver_;
};

// ---------------------------------------------------------------------------
// Scenario helpers — all in anonymous namespace.
// ---------------------------------------------------------------------------
namespace {

// Baseline input: heading north, 5 m/s, no targets, wide bounds.
MidMpcInput make_base_input() {
  MidMpcInput inp;
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.u_mps   = 5.0;
  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  inp.planned_route_bearing_rad = 0.0;
  inp.planned_speed_mps         = 5.0;
  inp.constraints.heading_min_rad = -M_PI;
  inp.constraints.heading_max_rad =  M_PI;
  inp.constraints.speed_min_mps   = 0.0;
  inp.constraints.speed_max_mps   = 15.0;
  inp.constraints.cpa_safe_m      = 1852.0;
  // Mirror own_ship heading into COLREGs directional reference (Phase E2 hard constraints).
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
  return inp;
}

MidMpcInput make_straight_line_input() {
  return make_base_input();  // no targets → trivial route-tracking
}

// Head-on: target 500 m directly north, heading south.
// dt_s=5, N=8, own-ship 5 m/s, target 5 m/s; closing at 10 m/s.
// Collision would occur around step 5 (t=25 s) → massive CPA cost for psi=0.
// tw = 1/(max(0,1)*max(0,1)) = 1.0 (maximum weight).
MidMpcInput make_head_on_input() {
  MidMpcInput inp = make_base_input();
  TargetState tgt;
  tgt.x_m     =  500.0;
  tgt.y_m     =  0.0;
  tgt.cog_rad =  M_PI;   // heading south (NED: π = south, positive clockwise)
  tgt.sog_mps =  5.0;
  tgt.cpa_m   =  0.0;    // → clamped to kMinCpaForWeight=1.0 → tw=1.0
  tgt.tcpa_s  =  0.0;    // → clamped to 1.0
  inp.targets.push_back(tgt);
  return inp;
}

// Crossing give-way: target 500 m east, heading west.
// With psi=0 (north) own-ship, the target crosses from starboard; Rule 15 scenario.
MidMpcInput make_crossing_give_way_input() {
  MidMpcInput inp = make_base_input();
  TargetState tgt;
  tgt.x_m     =  0.0;
  tgt.y_m     =  500.0;   // 500 m east
  tgt.cog_rad = -M_PI / 2.0;  // heading west (3π/2 = -π/2 in NED clockwise)
  tgt.sog_mps =  5.0;
  tgt.cpa_m   =  0.0;
  tgt.tcpa_s  =  0.0;
  inp.targets.push_back(tgt);
  return inp;
}

// Infeasible: heading_min > heading_max → IPOPT must report infeasible.
MidMpcInput make_infeasible_input() {
  MidMpcInput inp = make_base_input();
  inp.constraints.heading_min_rad = 2.0;  // min > max → empty feasible set
  inp.constraints.heading_max_rad = 1.0;
  return inp;
}

// Max step-to-step heading delta in degrees over the trajectory.
double max_heading_delta_deg(const MidMpcSolution& sol) {
  double max_delta = 0.0;
  for (std::size_t k = 1u; k < sol.trajectory.size(); ++k) {
    const double delta = std::abs(
        sol.trajectory[k].psi_rad - sol.trajectory[k - 1u].psi_rad);
    max_delta = std::max(max_delta, delta);
  }
  return max_delta * 180.0 / M_PI;
}

// Final trajectory heading in degrees (psi[N-1] relative to north).
// Positive = starboard (right-of-route).
double final_heading_deg(const MidMpcSolution& sol) {
  if (sol.trajectory.empty()) {
    return 0.0;
  }
  return sol.trajectory.back().psi_rad * 180.0 / M_PI;
}

}  // namespace

// ---------------------------------------------------------------------------
// 场景 1: Straight line — no targets, should trivially track route.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, StraightLineNoTargets) {
  const MidMpcInput input = make_straight_line_input();
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(sol.solve_duration_ms, 500);
  // No targets → optimal is constant heading near route bearing; steps near-equal.
  EXPECT_LT(max_heading_delta_deg(sol), 1.0);
}

// ---------------------------------------------------------------------------
// 场景 2: Head-on give-way — Rule 14, own ship must turn starboard (positive psi).
// Soft COLREGs cost (Phase E1) forces heading right; final heading > 30°.
// ---------------------------------------------------------------------------
// Rule-14 give-way head-on: handed a give-way rule and a heading window that
// PERMITS both port and starboard ([-60°,+60°]), M5 must execute a STARBOARD
// avoidance (psi>0), not port. (The starboard *decision* is M4/M6's; here M5
// receives the give-way rule + window and must comply within it.) Magnitude is
// [TBD-HAZID] weight-dependent, so assert direction + within-window only.
TEST_F(MidMpcNlpTest, HeadOnGiveWayRightTurn) {
  MidMpcInput input = make_head_on_input();
  input.constraints.applicable_rules = {14};
  input.constraints.heading_min_rad = -M_PI / 3.0;  // window permits port too
  input.constraints.heading_max_rad =  M_PI / 3.0;
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(final_heading_deg(sol), 5.0);    // starboard, non-trivial
  EXPECT_LT(final_heading_deg(sol), 61.0);   // within the [-60,60] window
}

// ---------------------------------------------------------------------------
// 场景 3: Crossing give-way — Rule 15, own ship deflects to avoid starboard target.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, CrossingGiveWay) {
  const MidMpcInput input = make_crossing_give_way_input();
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(sol.solve_duration_ms, 500);
}

// ---------------------------------------------------------------------------
// 场景 4: Infeasible problem — heading_min > heading_max → empty feasible set.
// IPOPT must detect infeasibility and NOT crash.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, InfeasibleProblem) {
  const MidMpcInput input = make_infeasible_input();
  const auto sol = solver_->solve(input, nullptr);

  // Accept Infeasible, Timeout, or NumericalFailure — any non-crash status.
  EXPECT_NE(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_NE(sol.status, MidMpcSolver::SolveStatus::NotInitialized);
}

// ---------------------------------------------------------------------------
// 场景 5: Warm start — starting from the optimal requires fewer IPOPT iterations.
// Uses ipopt_iterations (deterministic) rather than wall-clock (hardware-dependent).
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, WarmStartFasterThanColdStart) {
  const MidMpcInput input = make_crossing_give_way_input();
  const auto cold = solver_->solve(input, nullptr);
  const auto warm = solver_->solve(input, &cold);

  // Both must converge for the comparison to be meaningful.
  ASSERT_EQ(cold.status, MidMpcSolver::SolveStatus::Converged);
  ASSERT_EQ(warm.status, MidMpcSolver::SolveStatus::Converged);
  // Warm start is near-optimal: IPOPT should need strictly fewer iterations.
  EXPECT_LT(warm.ipopt_iterations, cold.ipopt_iterations);
}

// ---------------------------------------------------------------------------
// Regression (Restoration_Failed root cause): the route bearing (0 = north)
// lies OUTSIDE the avoidance heading window, so the cost optimum is pinned to a
// window edge. With box limits as IPOPT variable bounds (lbx/ubx) this is the
// robust case; previously (box as general inequality rows under limited-memory
// Hessian + adaptive mu) a box-active optimum produced intermittent
// Restoration_Failed / Maximum_Iterations. Live failures spanned both tight
// (~2°) and wide (~100°) windows, so both are covered, cold + warm-outside.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, BearingOutsideWindow_OptimumPinnedToEdge_Converges) {
  struct Win { double lo_deg; double hi_deg; };
  const Win wins[] = {{65.0, 69.0}, {65.0, 165.0}};
  for (const Win& w : wins) {
    MidMpcInput input = make_head_on_input();  // bearing 0 (north) + head-on target
    input.constraints.heading_min_rad = w.lo_deg * M_PI / 180.0;
    input.constraints.heading_max_rad = w.hi_deg * M_PI / 180.0;

    // Cold start seeds psi at own_ship.psi_rad = 0 (outside the window).
    const auto cold = solver_->solve(input, nullptr);
    EXPECT_EQ(cold.status, MidMpcSolver::SolveStatus::Converged)
        << "cold solve failed for window [" << w.lo_deg << "," << w.hi_deg << "]";
    EXPECT_GE(final_heading_deg(cold), w.lo_deg - 0.5);
    EXPECT_LE(final_heading_deg(cold), w.hi_deg + 0.5);

    // Warm-start from a trajectory ABOVE the window — mirrors the live
    // "warm-start lands outside the moved M4 window" condition.
    MidMpcSolution stale_warm;
    stale_warm.trajectory.resize(8u);
    for (auto& pt : stale_warm.trajectory) {
      pt.psi_rad = (w.hi_deg + 10.0) * M_PI / 180.0;
      pt.u_mps   = 5.0;
    }
    const auto warm = solver_->solve(input, &stale_warm);
    EXPECT_EQ(warm.status, MidMpcSolver::SolveStatus::Converged)
        << "warm(outside) solve failed for window [" << w.lo_deg << "," << w.hi_deg << "]";
  }
}

// ---------------------------------------------------------------------------
// J_colreg exponential barrier engages: an off-axis (crossing) target deflects
// the heading vs the no-target straight line (proves the barrier is active).
// NOTE: a dead-ahead symmetric head-on is intentionally NOT used here — θ=0 is
// a symmetric stationary point (port/starboard reduce the barrier equally), so
// barrier-only solves sit there; symmetry is broken by the M4 box (real
// scenario) or the give_way asymmetry (GiveWayAsymmetry test), not the barrier.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, ColregBarrierEngages_OffAxisTargetDeflectsVsNoTarget) {
  const auto base = solver_->solve(make_straight_line_input(), nullptr);
  ASSERT_EQ(base.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(std::abs(final_heading_deg(base)), 5.0);

  const auto cross = solver_->solve(make_crossing_give_way_input(), nullptr);
  EXPECT_EQ(cross.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(std::abs(final_heading_deg(cross)), 10.0)
      << "J_colreg barrier did not deflect heading for a crossing target";
}

// ---------------------------------------------------------------------------
// Gated starboard asymmetry: a give-way (Rule-14) head-on must pick starboard
// (psi>0), not a port turn / -180 course reversal. Without a give-way rule the
// asymmetry is gated off.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, GiveWayAsymmetry_PrefersStarboardSide) {
  // Isolates the asymmetry GATE: wide box (no M4 window), so the side is decided
  // purely by J_asym. give-way ⇒ starboard side (psi>0), never port. Magnitude is
  // bounded by the M4 window in the real system (tested in HeadOnGiveWayRightTurn).
  MidMpcInput gw = make_head_on_input();
  gw.constraints.applicable_rules = {14};   // head-on give-way
  const auto sol = solver_->solve(gw, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_GT(final_heading_deg(sol), 0.0)
      << "give-way head-on did not prefer starboard (got "
      << final_heading_deg(sol) << " deg)";

  MidMpcInput none = make_head_on_input();  // applicable_rules empty → asym off
  const auto sol2 = solver_->solve(none, nullptr);
  EXPECT_EQ(sol2.status, MidMpcSolver::SolveStatus::Converged);
}

// ---------------------------------------------------------------------------
// consecutive_failures_ counter — verify it resets on success.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, ConsecutiveFailuresResetOnSuccess) {
  // Force a failure first (infeasible input increments counter).
  static_cast<void>(solver_->solve(make_infeasible_input(), nullptr));
  EXPECT_GT(solver_->consecutive_failures(), 0);

  // A successful solve must reset the counter.
  const auto sol = solver_->solve(make_straight_line_input(), nullptr);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_EQ(solver_->consecutive_failures(), 0);
}
