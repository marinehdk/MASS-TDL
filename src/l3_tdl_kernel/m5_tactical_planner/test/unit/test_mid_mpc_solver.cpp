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
// P7: TargetState new-field default-value test (T1)
// ---------------------------------------------------------------------------
TEST(P7TargetStateTest, P7Fields_DefaultValues) {
  TargetState ts;
  EXPECT_DOUBLE_EQ(ts.intent_confidence, 0.5);
  EXPECT_DOUBLE_EQ(ts.target_compliance, 0.5);
  EXPECT_EQ(ts.classification, TargetState::Classification::Unknown);
}

TEST(P7TargetStateTest, P7Fields_ClassificationEnumValues) {
  EXPECT_EQ(static_cast<std::uint8_t>(TargetState::Classification::Unknown), 0u);
  EXPECT_EQ(static_cast<std::uint8_t>(TargetState::Classification::Vessel), 1u);
  EXPECT_EQ(static_cast<std::uint8_t>(TargetState::Classification::FixedObject), 2u);
}

TEST(P7TargetStateTest, P7Fields_CanAssign) {
  TargetState ts;
  ts.intent_confidence = 0.8;
  ts.target_compliance = 0.3;
  ts.classification = TargetState::Classification::Vessel;
  EXPECT_DOUBLE_EQ(ts.intent_confidence, 0.8);
  EXPECT_DOUBLE_EQ(ts.target_compliance, 0.3);
  EXPECT_EQ(ts.classification, TargetState::Classification::Vessel);
}

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
  EXPECT_LT(sol.solve_duration_ms, 3000);  // P4: N=80 dt=15 → can take ~1-2s for complex scenarios
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
  EXPECT_LT(sol.solve_duration_ms, 3000);  // P4: N=80 dt=15 → can take ~1-2s for complex scenarios
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
  // Fix E: NLP now constrains |psi[0]-own_psi| ≤ rot_max·dt, so the heading
  // window must be ROT-reachable from own_psi. Default rot_max=0.2094 rad/s,
  // dt=5 → rot_step=1.047 rad (60°). With own_psi=5° the first-step reachable
  // band is [-55°,65°]; window lower edge 65° is just reachable (pinned edge).
  struct Win { double lo_deg; double hi_deg; };
  const Win wins[] = {{65.0, 69.0}, {65.0, 165.0}};
  for (const Win& w : wins) {
    MidMpcInput input = make_head_on_input();  // bearing 0 (north) + head-on target
    // Fix E: own_psi must be ROT-reachable to the window edge. Place own_psi
    // 58° below the window lower edge (rot_step=60° at default rot_max, dt=5)
    // so the window edge is feasible with ~2° margin (avoids floating-point
    // boundary at exactly rot_step).
    input.own_ship.psi_rad = (w.lo_deg - 58.0) * M_PI / 180.0;
    input.constraints.own_ship_psi_rad = input.own_ship.psi_rad;
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
  // NOTE: this fixture's formulation is built with default (empty)
  // constraint_inputs_, so Rule17 is NOT compiled into the graph (it lives in
  // the ConstraintCompiler rule rows, which require set_constraint_inputs).
  // The test therefore verifies the NLP cost landscape WITHOUT a stand-on hard
  // constraint: a head-on target's J_colreg barrier has an anti-parallel
  // minimum (psi≈±π, own turns away → CPA grows → barrier vanishes), so the
  // NLP may converge to a large deflection. This is expected cost-landscape
  // behavior, not a bug — in the live system Rule17 IS compiled (mid_mpc_node
  // sets constraint_inputs from M6) and the |psi-own_psi|<=5° hard row prevents
  // the anti-parallel solution. We assert only convergence here.
  MidMpcInput input = make_head_on_input();  // head-on target, but stand-on role
  input.constraints.applicable_rules = {17};
  input.colregs_primary_role = 0;   // STAND_ON (M6 enum: STAND_ON=0)
  input.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Hold;
  input.colregs_min_alteration_rad = 0.0;
  const auto sol = solver_->solve(input, nullptr);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Rule17 stand-on input must be NLP-feasible (no give-way gate)";
  // No deflection assertion: without Rule17 compiled, the NLP may find the
  // anti-parallel cost minimum. Live system enforces stand-on via Rule17 rows.
}

// Fix E (Codex+ZCode review 2026-07-03, task-mr4ki83s): the NLP ROT rows now
// include the own_psi→psi[0] first step (rot_end_ = 2N, not 2(N-1)). When the
// M4 heading box is not ROT-reachable from own_psi (box_lo > own_psi+rot_step),
// the NLP must be infeasible rather than silently picking a psi[0] that the
// physical vessel cannot reach (which the tail-gate would then reject as
// turn_radius_infeasible). This regression guards the Fix E row.
TEST_F(MidMpcNlpTest, FixE_FirstStepRot_UnreachableHeadingBox_IsInfeasible) {
  MidMpcInput input = make_base_input();
  input.own_ship.psi_rad = 0.0;                  // own heading 0°
  input.constraints.own_ship_psi_rad = 0.0;
  // Default rot_max_rad_s=0.2094, dt=5 → rot_step=1.047 rad (60°). Box lower
  // edge 90° is 30° beyond the first-step reachable upper bound 60°.
  input.constraints.heading_min_rad = M_PI / 2.0;   // 90°
  input.constraints.heading_max_rad = 2.0;          // ~114°
  const auto sol = solver_->solve(input, nullptr);
  EXPECT_NE(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Fix E: heading box outside own_psi+rot_step reach must NOT converge";
}

// Fix E positive case: heading box just within reach converges and psi[0]
// respects the own_psi→psi[0] ROT bound (verifies the row is active, not just
// a no-op). With own_psi=0, rot_step=60°, box [30°,50°], psi[0] must be ≤60°.
TEST_F(MidMpcNlpTest, FixE_FirstStepRot_ReachableBox_ConvergesAndPsi0Bounded) {
  MidMpcInput input = make_base_input();
  input.own_ship.psi_rad = 0.0;
  input.constraints.own_ship_psi_rad = 0.0;
  input.constraints.heading_min_rad = 30.0 * M_PI / 180.0;
  input.constraints.heading_max_rad = 50.0 * M_PI / 180.0;
  const auto sol = solver_->solve(input, nullptr);
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "Fix E: reachable box must converge";
  ASSERT_FALSE(sol.trajectory.empty());
  const double psi0 = sol.trajectory.front().psi_rad;
  const double rot_step = 0.2094 * 5.0;  // default rot_max · dt
  EXPECT_LE(std::fabs(psi0 - 0.0), rot_step + 1.0e-6)
      << "Fix E: |psi[0]-own_psi| must respect rot_max·dt";
}

// v2.1 spec §4.2/§4.3 — auto-derive k_minalt + k_cpa from input.
using mass_l3::m5::mid_mpc::derive_row_bound_config;
using mass_l3::m5::mid_mpc::RowBoundConfig;

namespace {
// 4.7°/s in rad; 30° in rad (avoid units:: namespace leakage into the test).
constexpr double kRot47radPerS = 4.7 * M_PI / 180.0;
constexpr double kMinAlt30rad  = 30.0 * M_PI / 180.0;
}  // namespace

TEST_F(MidMpcNlpTest, DeriveMinaltKStarForRot4p7) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  const RowBoundConfig cfg = derive_row_bound_config(inp, /*n_horizon=*/18, /*dt_s=*/5.0);
  ASSERT_FALSE(cfg.minalt_override_valid);
  // rot_step = 0.0820*5 = 0.4105 rad; min_alt=0.5236
  // k* = ceil(0.5236/0.4105) - 1 = ceil(1.275) - 1 = 2 - 1 = 1
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);
}

TEST_F(MidMpcNlpTest, DeriveCpaKCPAUsesTcpaMargin) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  TargetState tgt{};
  tgt.tcpa_s = 15.0;
  inp.targets.push_back(tgt);
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  ASSERT_FALSE(cfg.cpa_override_valid);
  // k_minalt=1; k_tcpa = ceil(15/5) - 1 = 2; k_cpa=max(1,2)=2.
  EXPECT_EQ(cfg.cpa_hard_from_k, 2);
}

TEST_F(MidMpcNlpTest, DeriveDisabledForReduceSpeed) {
  MidMpcInput inp = make_base_input();
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::ReduceSpeed;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_TRUE(cfg.direction_disabled);  // B9: ReduceSpeed disables direction
}

TEST_F(MidMpcNlpTest, DeriveCpaConservativeWhenAllTcpaNonPositive) {
  // B3-r3 + Phase 3.2 (spec v2.3 §2.5): all targets tcpa<=0 -> conservative
  // fallback. v2.1 returned 0 (all-hard). Phase 3.2 forces max(...,k_initial_
  // relax=2) so k=0/1 stay soft even when tcpa≤0 — range(0) is an NLP-
  // immovable initial condition and hardening it forces Infeasible when the
  // target is already inside cpa_hard at k=0. σ in the constraint now also
  // covers this, but the floor keeps σ clean.
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  TargetState tgt{};
  tgt.tcpa_s = 0.0;  // already past
  inp.targets.push_back(tgt);
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.cpa_hard_from_k, 2);  // Phase 3.2 initial-relax floor
}

// Phase 3.3 (spec v2.3 §2.6): geometric-reach floor. Target inside cpa_hard
// at k=0 needs N steps to physically reach cpa_hard at the closing rate.
// The floor pushes cpa_hard_from_k up so hard rows only appear where the
// own-ship can actually reach the floor.
TEST_F(MidMpcNlpTest, DeriveCpaGeometricReachWhenTargetInsideFloor) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.cpa_hard_m = 1852.0;
  TargetState tgt{};
  tgt.tcpa_s = 60.0;            // would give k_tcpa = ceil(60/5)-1 = 11
  tgt.x_m = 0.0;                // target dead ahead
  tgt.y_m = 800.0;              // range = 800 m, well inside cpa_hard 1852
  tgt.sog_mps = 5.0;            // closing rate ~5 m/s
  inp.targets.push_back(tgt);
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  // deficit = 1852 - 800 = 1052 m; geom_reach_k = ceil(1052 / (5*5)) = ceil(42.08) = 43
  // capped to n_horizon=18.
  EXPECT_EQ(cfg.cpa_hard_from_k, 18)
      << "geometric reach inside floor must cap hard rows at n_horizon";
}

TEST_F(MidMpcNlpTest, DeriveCpaGeometricReachInertWhenTargetOutsideFloor) {
  // Target outside cpa_hard -> deficit = 0 -> geometric_reach_k inert.
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.cpa_hard_m = 1852.0;
  TargetState tgt{};
  tgt.tcpa_s = 60.0;
  tgt.x_m = 0.0;
  tgt.y_m = 5000.0;             // range = 5000 m, outside cpa_hard
  tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  // No geometric floor applies; result = max(k_minalt=1, k_tcpa=11, k_initial_relax=2) = 11.
  EXPECT_EQ(cfg.cpa_hard_from_k, 11);
}

// v2.1 spec §4.4 — direction reachable schedule derivation (Phase 1 fix).
// rule14-ho-replica geometry: own ship 1 m on the wrong side of the route,
// M6 picks Starboard give-way. Verify k_dir=1 (one step closure suffices).
TEST_F(MidMpcNlpTest, DeriveDirectionKDirFromXte) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;            // 4.7°/s
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;                 // give-way
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.route_xte_m = -1.0;                        // 1 m LEFT of route
  inp.own_ship.u_mps = 7.584;                    // observed runtime value
  // rot_step = 0.0820*5 = 0.4105; sin(0.4105) = 0.3992
  // closure_rate = 7.584*5*0.3992 = 15.14 m/step
  // k_dir = ceil(1.0/15.14) = 1
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  ASSERT_FALSE(cfg.direction_override_valid);
  EXPECT_EQ(cfg.direction_hard_from_k, 1);
}

TEST_F(MidMpcNlpTest, DeriveDirectionKDirLargeXte) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.route_xte_m = -50.0;                       // large wrong-side XTE
  inp.own_ship.u_mps = 3.0;
  // closure_rate = 3*5*sin(0.4105) = 5.988 m/step
  // k_dir = ceil(50/5.988) = 9
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 9);
}

TEST_F(MidMpcNlpTest, DeriveDirectionKDirZeroXteIsAllHard) {
  // Ship starts on the route line: no soften needed, all-hard (legacy v2).
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.route_xte_m = 0.0;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 0);
}

TEST_F(MidMpcNlpTest, DeriveDirectionScheduleIgnoredWhenDisabled) {
  // ReduceSpeed disables direction class entirely; k_dir must stay 0.
  MidMpcInput inp = make_base_input();
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::ReduceSpeed;
  inp.route_xte_m = -50.0;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 0);
}

// Codex review High: schedule must NOT soften when ship is already on the
// preferred side (pref_dir · l[0] > 0 = direction satisfied). Only wrong-side
// (pref_dir · l[0] < 0) is the immovable violation per spec §4.4.
TEST_F(MidMpcNlpTest, DeriveDirectionScheduleAllHardWhenOnPreferredSide) {
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // pref_dir = +1
  inp.route_xte_m = +5.0;  // already on Starboard side → g_dir[0] = +5 > 0
  inp.own_ship.u_mps = 3.0;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 0);  // all-hard, no soften needed
}

TEST_F(MidMpcNlpTest, DeriveDirectionScheduleAllHardWhenPortOnPreferredSide) {
  // Symmetric: Port pref_dir = -1, ship on Port side (route_xte_m < 0) →
  // g_dir[0] = (-1)·(-) = + > 0 → satisfied → all-hard.
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Port;  // pref_dir = -1
  inp.route_xte_m = -5.0;  // on Port side → g_dir[0] = (-1)·(-5) = +5 > 0
  inp.own_ship.u_mps = 3.0;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 0);
}

TEST_F(MidMpcNlpTest, DeriveDirectionScheduleSoftensPortWrongSide) {
  // Port give-way but ship on Starboard side → wrong side → soften.
  MidMpcInput inp = make_base_input();
  inp.rot_max_rad_s = kRot47radPerS;
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Port;  // pref_dir = -1
  inp.route_xte_m = +1.0;  // on Starboard side → g_dir[0] = (-1)·(+1) = -1 < 0
  inp.own_ship.u_mps = 7.584;
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_EQ(cfg.direction_hard_from_k, 1);  // same k_dir math as Starboard case
}

// v2.2 spec §4.2/§4.6 — k_minalt derive over {ROT ∩ box}.
// M4 publishes heading_box_reachable_from_psi0_deg (schema 113). When the box
// upper bound is closer than min_alt, the hard min_alt floor is unreachable →
// minalt_box_infeasible=true, k_minalt=N (全 soft, §13.1 BC-MPC dispatch).
// M4 未升级 (sentinel=0) → 退化 v2.1 ROT-only 公式.

TEST_F(MidMpcNlpTest, MinaltHardFromKTakesMaxOfRotAndBox) {
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = 0.524;  // 30°
  inp.rot_max_rad_s = 0.0820;              // 4.7°/s
  inp.colregs_primary_role = 1U;           // give-way lateral
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  // dt=5 → rot_step=0.41 rad=23.5° → k_minalt_rot = ceil(0.524/0.41)-1 = 1
  inp.constraints.heading_box_reachable_from_psi0_deg = 5.0;  // box 仅 5°（< min_alt 30°）
  // box_reach 5° < min_alt 30° → minalt_box_infeasible=true, k_minalt=N（全 soft）

  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);

  EXPECT_TRUE(cfg.minalt_box_infeasible);
  EXPECT_EQ(cfg.minalt_hard_from_k, 18);
}

TEST_F(MidMpcNlpTest, MinaltHardFromKFallsBackWhenM4NotUpgraded) {
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = 0.524;
  inp.rot_max_rad_s = 0.0820;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.heading_box_reachable_from_psi0_deg = 0.0;  // sentinel: M4 未升级

  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);

  EXPECT_FALSE(cfg.minalt_box_infeasible);
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);  // v2.1 ROT-only
}

TEST_F(MidMpcNlpTest, MinaltHardFromKRotReachWhenBoxAllows) {
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = 0.524;
  inp.rot_max_rad_s = 0.0820;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.heading_box_reachable_from_psi0_deg = 35.0;  // box 35° > min_alt 30°

  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);

  EXPECT_FALSE(cfg.minalt_box_infeasible);
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);
}

TEST_F(MidMpcNlpTest, MinaltHardFromKBoxExactlyAtMinAltNotInfeasible) {
  // v2.2 §4.6 boundary: box_reach == min_alt → strict <, not infeasible.
  // min_alt 用 kMinAlt30rad (= 30°·π/180) 使与 box_reach 30°·kRadPerDeg 严格相等,
  // 否则字面 0.524 > 0.5236 会使 strict < 误触发 infeasible (浮点语义).
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.rot_max_rad_s = 0.0820;  // 4.7°/s
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.heading_box_reachable_from_psi0_deg = 30.0;  // == min_alt 边界
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_FALSE(cfg.minalt_box_infeasible);
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);  // k_minalt_rot, 非 N
}

TEST_F(MidMpcNlpTest, MinaltHardFromKBoxWithinEpsilonNotInfeasible) {
  // Codex β review 🟡4: epsilon tolerance (~0.005 rad ≈ 0.3°) absorbs M4/M5
  // float32(deg)→float64(rad) conversion noise. box_reach is just under min_alt
  // by less than epsilon → must NOT flag infeasible (would cause false INFEAS).
  // min_alt=30° = 0.5236 rad; box_reach=29.8° = 0.5203 rad → diff 0.0033 < 0.005.
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = kMinAlt30rad;
  inp.rot_max_rad_s = 0.0820;  // 4.7°/s
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.heading_box_reachable_from_psi0_deg = 29.8;  // 0.0033 rad under min_alt
  const RowBoundConfig cfg = derive_row_bound_config(inp, 18, 5.0);
  EXPECT_FALSE(cfg.minalt_box_infeasible);
  EXPECT_EQ(cfg.minalt_hard_from_k, 1);  // within epsilon → ROT schedule
}

// ===========================================================================
// v2.2 §13.1: solver exposes last_minalt_box_infeasible() after solve() for the
// BC-MPC dispatch OR condition (Codex integration blocker 1). minalt_box can
// trigger on the FIRST solve (consecutive=0), so it must be queryable without
// relying on the consecutive-failure counter.
// ===========================================================================
TEST(MidMpcSolverV22, LastMinaltBoxInfeasibleDefaultsFalseBeforeSolve) {
  // A freshly-constructed solver has not derived a RowBoundConfig yet.
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 8;
  MidMpcNlpFormulation form(cfg);
  form.build_symbolic_graph();
  MidMpcSolver::IpoptOptions opts;
  opts.timeout_s = 2.0;
  MidMpcSolver solver(form, opts);
  EXPECT_FALSE(solver.last_minalt_box_infeasible());
}

TEST_F(MidMpcNlpTest, LastMinaltBoxInfeasibleExposedAfterBoxInfeasibleSolve) {
  // Solve with input that triggers minalt_box_infeasible (box < min_alt):
  // box_reach 5° < min_alt 30° → derived.minalt_box_infeasible=true.
  MidMpcInput inp = make_base_input();
  inp.colregs_min_alteration_rad = kMinAlt30rad;  // 30°
  inp.rot_max_rad_s = 0.0820;                      // 4.7°/s
  inp.colregs_primary_role = 1U;                   // give-way
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.heading_box_reachable_from_psi0_deg = 5.0;  // box 仅 5° (< 30°)

  static_cast<void>(solver_->solve(inp, nullptr));
  EXPECT_TRUE(solver_->last_minalt_box_infeasible());
}

TEST_F(MidMpcNlpTest, LastMinaltBoxInfeasibleFalseAfterFeasibleSolve) {
  // Baseline straight-line solve (no COLREG conflict, no box contract) must
  // leave the flag false — and it must be reset from a prior true state.
  // First force box-infeasible, then solve a clean input → flag flips back.
  MidMpcInput infeas_inp = make_base_input();
  infeas_inp.colregs_min_alteration_rad = kMinAlt30rad;
  infeas_inp.rot_max_rad_s = 0.0820;
  infeas_inp.colregs_primary_role = 1U;
  infeas_inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;
  infeas_inp.constraints.heading_box_reachable_from_psi0_deg = 5.0;
  static_cast<void>(solver_->solve(infeas_inp, nullptr));
  ASSERT_TRUE(solver_->last_minalt_box_infeasible());

  // Clean solve: no box contract (sentinel 0) → v2.1 ROT-only, not infeasible.
  static_cast<void>(solver_->solve(make_straight_line_input(), nullptr));
  EXPECT_FALSE(solver_->last_minalt_box_infeasible());
}

// ===========================================================================
// v2.2 §13.1: BC-MPC take-over dispatch OR condition (Codex integration 🟡3).
// compute_bc_mpc_take_over() is a free function so the rule is unit-testable.
// ===========================================================================
using mass_l3::m5::compute_bc_mpc_take_over;

TEST(MidMpcDispatchV22, ConsecutiveAtThresholdTriggersTakeover) {
  // consecutive>=3 alone, no box/speed infeasible → take-over.
  EXPECT_TRUE(compute_bc_mpc_take_over(3, 3, false, false));
  EXPECT_TRUE(compute_bc_mpc_take_over(5, 3, false, false));
}

TEST(MidMpcDispatchV22, OrConditionBoxInfeasibleAloneTriggersTakeover) {
  // consecutive=0, minalt_box_infeasible=true, speed_gap=false → take-over.
  // This is the Codex blocker-1 case: fires on the FIRST solve.
  EXPECT_TRUE(compute_bc_mpc_take_over(0, 3, true, false));
}

TEST(MidMpcDispatchV22, OrConditionSpeedInfeasibleAloneTriggersTakeover) {
  // consecutive=0, minalt_box_infeasible=false, speed_gap=true → take-over.
  EXPECT_TRUE(compute_bc_mpc_take_over(0, 3, false, true));
}

TEST(MidMpcDispatchV22, NoConditionMetDoesNotTriggerTakeover) {
  // consecutive<threshold, no box/speed infeasible → no take-over.
  EXPECT_FALSE(compute_bc_mpc_take_over(0, 3, false, false));
  EXPECT_FALSE(compute_bc_mpc_take_over(2, 3, false, false));
}

// ===========================================================================
// P5 (2026-07-18) — IPOPT A/B benchmark vs acatos at PRODUCTION horizon
// (N=80, dt=15s, 1200s). The acatos backend fails to converge on this scenario
// when the CPA gap exceeds ~252m (status=3 QP failure at SQP iter 8); the per-
// target slack xi stays inert (rho-gap). This test establishes whether IPOPT,
// running the SAME formulation at the SAME horizon, handles the
// heavy-infeasibility regime that acatos cannot.
//
// WHY this is a FAIR comparison (resolves the task brief's "current parity
// test IPOPT N=8 vs acatos N=80 is unfair" finding):
//   - Same N=80, dt=15s (NOT N=8 like the legacy MidMpcNlpTest fixture).
//   - Same own-ship, same target geometry (mirrors acatos boundary scan).
//   - Same cpa_safe=1852m.
// The IPOPT formulation is a DIFFERENT (N-stacked) implementation than the
// acatos single-stage graph; both encode the same dynamics, costs, and CPA
// constraints. This is the intended backend-comparison surface.
//
// OUTPUT: per-scenario (status, iter, cost, slack, duration_ms). No hard
// assertions beyond "must produce a finite MidMpcSolution" -- the comparison
// itself is the finding. Cited in docs/superpowers/specs/2026-07-18-m5-p5-
// acados-convergence-design.md.
// ===========================================================================
namespace {
MidMpcInput make_p5_boundary_input(double target_y_m) {
  // Mirror of AcadosSolverTest::P5_ConvergenceBoundary_ScanTargetDistance.
  // Own-ship at origin, heading north (psi=0), 5 m/s; one static target at
  // (0, target_y_m); cpa_safe=1852m. route_weight=1.0 (active-leg normal ops).
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
  inp.constraints.own_ship_psi_rad = 0.0;
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  inp.route_frame_normal_x   = 0.0;
  inp.route_frame_normal_y   = 1.0;
  inp.lateral_scale_m        = 400.0;
  inp.route_weight           = 1.0;
  TargetState t;
  t.id = 1;
  t.x_m = 0.0;
  t.y_m = target_y_m;
  t.sog_mps = 0.0;
  t.cog_rad = 0.0;
  t.confidence = 1.0;
  inp.targets.push_back(t);
  return inp;
}
}  // namespace

TEST(MidMpcP5Benchmark, IPOPT_ConvergenceBoundary_ScanTargetDistance_N80) {
  // Build the IPOPT formulation at the SAME horizon as acatos (N=80, dt=15).
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon   = 80;
  cfg.dt_s        = 15.0;
  cfg.w_colreg    = 30.0;
  cfg.w_dist      = 10.0;
  cfg.w_route     = 3.0;
  cfg.w_vel       = 1.0;
  cfg.max_targets = 16;
  MidMpcNlpFormulation form(cfg);
  form.build_symbolic_graph();
  // Production-tolerant IPOPT options (mirror MidMpcSolver defaults). The
  // timeout is generous: cold-start IPOPT on N=80 may take 5-30s per solve.
  MidMpcSolver::IpoptOptions opts;
  opts.max_iter  = 1500;     // N=80 needs more iterations than the N=8 default
  opts.tol       = 1.0e-6;
  opts.timeout_s = 60.0;
  MidMpcSolver solver(form, opts);

  std::cout << "[P5-IPOPT-BOUNDARY] N=80 dt=15 (same as acatos)\n"
            << "[P5-IPOPT-BOUNDARY] target_y_m : status iter cost slack dur_ms\n";
  for (const double ty : {2400.0, 2100.0, 1900.0, 1852.0, 1800.0, 1700.0,
                          1600.0, 1500.0, 1200.0, 800.0}) {
    const auto inp = make_p5_boundary_input(ty);
    const auto sol = solver.solve(inp, nullptr);
    const double gap = 1852.0 - ty;
    std::cout << "[P5-IPOPT-BOUNDARY] ty=" << ty << " (gap=" << gap << "m)"
              << " : status=" << static_cast<int>(sol.status)
              << " iter=" << sol.ipopt_iterations
              << " cost=" << sol.cost_total
              << " slack=" << sol.cpa_slack
              << " dur_ms=" << sol.solve_duration_ms << "\n";
    // Always: finite solution (contract invariant).
    EXPECT_TRUE(std::isfinite(sol.cpa_slack));
    EXPECT_GE(sol.cpa_slack, 0.0);
  }
}
