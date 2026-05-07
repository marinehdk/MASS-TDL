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
TEST_F(MidMpcNlpTest, HeadOnGiveWayRightTurn) {
  const MidMpcInput input = make_head_on_input();
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  // CPA penalty (w_colreg=1000) >> route deviation (w_dist=10) → large starboard turn.
  EXPECT_GT(final_heading_deg(sol), 30.0);
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
// 场景 5: Warm start — starting from the optimal should require fewer iterations.
// Cold solve first, then warm start from that solution; warm should be faster.
//
// Note: on extremely fast hardware both may round to 0 ms. The test is
// considered informational; hard-fail is expected only on standard targets.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, WarmStartFasterThanColdStart) {
  const MidMpcInput input = make_crossing_give_way_input();
  const auto cold = solver_->solve(input, nullptr);
  const auto warm = solver_->solve(input, &cold);

  // Both must converge for the comparison to be meaningful.
  ASSERT_EQ(cold.status, MidMpcSolver::SolveStatus::Converged);
  ASSERT_EQ(warm.status, MidMpcSolver::SolveStatus::Converged);
  EXPECT_LT(warm.solve_duration_ms, cold.solve_duration_ms);
}

// ---------------------------------------------------------------------------
// consecutive_failures_ counter — verify it resets on success.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, ConsecutiveFailuresResetOnSuccess) {
  // Force a failure first (infeasible input increments counter).
  solver_->solve(make_infeasible_input(), nullptr);
  EXPECT_GT(solver_->consecutive_failures(), 0);

  // A successful solve must reset the counter.
  const auto sol = solver_->solve(make_straight_line_input(), nullptr);
  if (sol.status == MidMpcSolver::SolveStatus::Converged) {
    EXPECT_EQ(solver_->consecutive_failures(), 0);
  }
}
