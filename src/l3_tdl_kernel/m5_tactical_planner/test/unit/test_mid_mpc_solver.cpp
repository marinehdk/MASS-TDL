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

// Rule 13 overtaking: own is the give-way (overtaking) vessel. Target is dead
// ahead moving in the same direction but slower. M6 assigns preferred_direction
// Starboard (default overtake side) + min_alteration. The NLP must (a) converge
// and (b) deflect to starboard (psi > 0) to clear the target's stern on the
// M6-mandated side.
MidMpcInput make_overtake_give_way_input() {
  MidMpcInput inp = make_base_input();
  TargetState tgt;
  tgt.x_m     =  300.0;        // dead ahead
  tgt.y_m     =    0.0;
  tgt.cog_rad =  0.0;          // same heading (north)
  tgt.sog_mps =  2.0;          // slower → own overtakes
  tgt.cpa_m   =  0.0;
  tgt.tcpa_s  =  0.0;
  inp.targets.push_back(tgt);
  // Rule13 give-way: lateral preferred direction + give-way role gate the
  // direction/min_alt hard rows + terminal cost (§3.3/§5.5/§7.1).
  inp.colregs_primary_role = 1;   // GIVE_WAY
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.colregs_min_alteration_rad = 15.0 * M_PI / 180.0;
  inp.constraints.applicable_rules = {13};
  return inp;
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

// Bug C deep (RC-C): the HARD CPA floor must be cpa_hard_m, NOT the cost-scaled
// cpa_safe_m. The node bumps cpa_safe_m→2500 during conflict for SOFT cost
// scaling; that bump must not leak into the hard floor. Target abeam at 2000 m
// (inside 2500, outside 1852): feasible only when the hard floor is cpa_hard=1852.
// Before the fix the bumped cpa_safe (2500) was used → Infeasible for any target
// inside 2500 m. Graph is baked at build_symbolic_graph from constraint_inputs_,
// so this test builds its own formulation with the target set.
TEST_F(MidMpcNlpTest, HardCpaFloorStaysAtCpaHardWhenCpaSafeIsBumped) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 8; cfg.dt_s = 5.0; cfg.max_targets = 4;
  cfg.w_colreg = 30.0; cfg.w_dist = 10.0; cfg.w_vel = 1.0;
  MidMpcNlpFormulation form(cfg);

  TargetState tgt;
  tgt.x_m = 0.0; tgt.y_m = 2000.0;  // 2000 m abeam (east); own north → d grows
  tgt.cog_rad = 0.0; tgt.sog_mps = 0.0;
  mass_l3::m5::ConstraintInputs ci;
  ci.cpa_safe_m = 2500.0;   // cost-scaling bump (active encounter)
  ci.cpa_hard_m = 1852.0;   // hard floor (un-bumped, shared)
  ci.targets.push_back(tgt);
  form.set_constraint_inputs(ci);
  form.build_symbolic_graph();
  MidMpcSolver::IpoptOptions opts;
  opts.max_iter = 150; opts.tol = 1.0e-4; opts.timeout_s = 2.0;
  MidMpcSolver solver(form, opts);

  MidMpcInput input = make_base_input();
  input.constraints.cpa_safe_m = 2500.0;
  input.constraints.cpa_hard_m = 1852.0;
  input.constraints.heading_min_rad = -M_PI;
  input.constraints.heading_max_rad =  M_PI;
  input.targets.push_back(tgt);
  const auto sol = solver.solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "target at 2000m must be feasible when hard floor is cpa_hard=1852, not 2500";
}

// ---------------------------------------------------------------------------
// 场景 5: Warm start contract (Slice C1, spec §6.5).
//
// The solver's warm-start is now SPLIT prefix/suffix:
//   prefix (k<K): x0 = prefix_psi/prefix_u (equality-pinned values)
//   suffix (k>=K): x0 = COLD-START seed (own_psi/own_u), NOT the previous
//                  solution. Spec §6.5 rationale: prevent accumulation drift.
//
// When K=0 (no committed prefix, the default for these test inputs), the entire
// horizon uses the cold-start seed, so solve(input, &prev) behaves IDENTICALLY
// to solve(input, nullptr). This replaces the v1 "warm start needs fewer
// iterations" expectation, which is no longer the contract: the suffix is
// deliberately NOT seeded from the previous solution to avoid drift.
//
// We verify the new contract: with K=0, warm-start and cold-start produce the
// same iteration count (both use the cold-start seed). The trajectory must also
// match (deterministic solve from the same seed).
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, WarmStartSuffixUsesColdStartSeedWhenKZero) {
  const MidMpcInput input = make_crossing_give_way_input();
  const auto cold = solver_->solve(input, nullptr);
  const auto warm = solver_->solve(input, &cold);

  // Both must converge for the comparison to be meaningful.
  ASSERT_EQ(cold.status, MidMpcSolver::SolveStatus::Converged);
  ASSERT_EQ(warm.status, MidMpcSolver::SolveStatus::Converged);
  // C1 §6.5: with K=0 both use the cold-start seed → same iteration count
  // (the previous solution is deliberately ignored for the suffix).
  EXPECT_EQ(warm.ipopt_iterations, cold.ipopt_iterations)
      << "K=0 warm-start should use cold-start seed (same iterations); "
      << "got cold=" << cold.ipopt_iterations
      << " warm=" << warm.ipopt_iterations;
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

// ---------------------------------------------------------------------------
// FAIL-CLOSED on row registry / g_dim size mismatch (spec §3.8/§10.1 review High).
// The registry (total_rows) MUST equal the formulation g_dim. A divergence is a
// row-contract bug (registry and symbolic g out of sync). Previously solve()
// silently fell back to legacy zeros/inf — re-hardening softened COLREG rows /
// degrading active equalities into half-constraints. It must now return
// NumericalFailure WITHOUT calling IPOPT (no silent solve).
//
// A formulation with NO build_symbolic_graph() call exposes the contract: g_dim
// returns its legacy fallback 2*(N-1) while the registry is default-constructed
// (total_rows==0) → nb != gdim → fail-closed. Crucially the uncached solver()
// Function would be empty, so proving we return before invoking IPOPT.
// ---------------------------------------------------------------------------
TEST(MidMpcSolverMismatch, SizeMismatchReturnsNumericalFailureNotSilentSolve) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 8;
  MidMpcNlpFormulation form(cfg);  // NO build_symbolic_graph() → registry empty
  MidMpcSolver::IpoptOptions opts;
  opts.timeout_s = 2.0;
  MidMpcSolver solver(form, opts);

  const int32_t gdim = form.g_dim();
  const int32_t nb = form.row_registry().total_rows();
  ASSERT_NE(nb, gdim) << "test precondition: registry/g_dim must diverge";
  ASSERT_EQ(nb, 0);
  ASSERT_GT(gdim, 0);

  const MidMpcInput input = make_base_input();
  const auto sol = solver.solve(input, nullptr);

  // Fail-closed: must be NumericalFailure, not a converged/silent solve.
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::NumericalFailure);
  EXPECT_GT(solver.consecutive_failures(), 0);  // failure counter incremented
}

// ===========================================================================
// Slice D (Fix D): Rule13/14/15 NLP regression guards.
//
// These fixtures exercise the NLP at the rule-by-rule level: each rule's
// give-way role + preferred_direction + min_alteration gate the §3.3 hard rows
// (direction/min_alt) and §5.4/§5.5 terminal cost/rows. The fixtures confirm:
//   1. NLP converges for each rule's canonical geometry
//   2. The deflection direction matches the M6-mandated side (starboard for
//      Rule13/14, starboard for Rule15 crossing give-way)
//
// Run via container: build/m5_tactical_planner/test_mid_mpc_solver
//   --gtest_filter=ColregRuleFixture.*
// ===========================================================================
class ColregRuleFixture : public ::testing::Test {
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
    opts.timeout_s = 2.0;
    solver_ = std::make_unique<MidMpcSolver>(*formulation_, opts);
  }

  std::unique_ptr<MidMpcNlpFormulation> formulation_;
  std::unique_ptr<MidMpcSolver> solver_;
};

// Rule 13 (overtaking give-way): NLP must converge and deflect to the
// M6-mandated starboard side (preferred_direction=Starboard). The direction
// hard row (§7.1 g_dir = pref_dir·l[k] ≥ 0) + min_alt row force lateral to
// starboard; J_asym adds the soft starboard preference. Magnitude is gated by
// the M6 min_alteration (15° here); the test asserts side, not exact angle.
TEST_F(ColregRuleFixture, Rule13_OvertakeGiveWay_ConvergesAndDeflectsStarboard) {
  const MidMpcInput input = make_overtake_give_way_input();
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Rule13 overtake give-way must be NLP-feasible (target ahead, slow)";
  // Starboard deflection: psi[N-1] > own_psi + min_alteration/2. The min_alt
  // hard row already enforces ≥ min_alt; we assert the sign (starboard = +).
  ASSERT_FALSE(sol.trajectory.empty());
  const double final_psi_deg = final_heading_deg(sol);
  EXPECT_GT(final_psi_deg, 5.0)
      << "Rule13 give-way did not deflect starboard (got " << final_psi_deg << " deg)";
}

// Rule 14 (head-on give-way): NLP converges and picks starboard (the
// convention-mandated side). Strengthens HeadOnGiveWayRightTurn by also setting
// the role + preferred_direction + min_alteration fields (not just applicable_rules)
// to exercise the full §3.3/§5.5 activation path.
TEST_F(ColregRuleFixture, Rule14_HeadOnGiveWay_ConvergesAndDeflectsStarboard) {
  MidMpcInput input = make_head_on_input();
  input.constraints.applicable_rules = {14};
  input.colregs_primary_role = 1;   // GIVE_WAY
  input.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  input.colregs_min_alteration_rad = 30.0 * M_PI / 180.0;
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Rule14 head-on give-way must be NLP-feasible";
  const double final_psi_deg = final_heading_deg(sol);
  EXPECT_GT(final_psi_deg, 5.0)
      << "Rule14 give-way did not deflect starboard (got " << final_psi_deg << " deg)";
}

// Rule 15 (crossing give-way): NLP converges. Target approaches from starboard,
// own must give way. Unlike 13/14 the side is not convention-fixed, but M6's
// preferred_direction (Starboard here — own alters to pass behind target).
// The test confirms convergence + non-trivial deflection (barrier engages).
TEST_F(ColregRuleFixture, Rule15_CrossingGiveWay_ConvergesAndDeflects) {
  MidMpcInput input = make_crossing_give_way_input();
  input.constraints.applicable_rules = {15};
  input.colregs_primary_role = 1;   // GIVE_WAY
  input.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  input.colregs_min_alteration_rad = 15.0 * M_PI / 180.0;
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Rule15 crossing give-way must be NLP-feasible";
  const double final_psi_deg = final_heading_deg(sol);
  EXPECT_GT(std::abs(final_psi_deg), 10.0)
      << "Rule15 give-way did not deflect meaningfully (got " << final_psi_deg << " deg)";
}

// Rule 17 (stand-on): NLP converges AND the trajectory stays close to the route
// bearing (no avoidance action). Stand-on must hold course/speed — the terminal
// cost/rows + direction rows are disabled (§3.3/§5.5 stand-on gate), so the NLP
// is just route-tracking + CPA floor. This guards the stand-on path: no spur
// deflection from a mis-activated give-way gate.
TEST_F(ColregRuleFixture, Rule17_StandOn_ConvergesAndHoldsCourse) {
  MidMpcInput input = make_head_on_input();  // head-on target, but stand-on role
  input.constraints.applicable_rules = {17};
  input.colregs_primary_role = 3;   // STAND_ON
  input.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Hold;
  input.colregs_min_alteration_rad = 0.0;
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Rule17 stand-on must be NLP-feasible (no give-way gate)";
  // Stand-on holds course: deflection < 5° (no avoidance maneuver expected).
  const double final_psi_deg = final_heading_deg(sol);
  EXPECT_LT(std::abs(final_psi_deg), 5.0)
      << "Rule17 stand-on unexpectedly deflected (got " << final_psi_deg << " deg)";
}
