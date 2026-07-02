// test/unit/test_mid_mpc_route_cost.cpp
// Slice R1: J_route dimensionless + route-frame cross-track convergence +
// COLREG dominance verification (spec §3.2 / §4.3 / §10.1).
//
// These tests link against CasADi + IPOPT and perform real solves (1-5 s).
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;

// ---------------------------------------------------------------------------
// Fixture — builds the route-frame-enabled NLP once; reused by all tests.
// N=8 (NOT the spec §3.2/§4.1 default N=18) is used here to keep the unit-test
// suite fast (each cost/dominance solve is a real IPOPT NLP). The dominance
// ratios measured below are ratio-based (w·J / w·J), so they are not sensitive
// to the horizon length; the route-convergence TEST 1 asserts DIRECTION (solver
// turns toward the route), not full convergence at N=8.
// ---------------------------------------------------------------------------
class RouteCostTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MidMpcNlpFormulation::Config cfg;
    cfg.n_horizon   = 8;
    cfg.dt_s        = 5.0;
    cfg.w_colreg    = 30.0;   // spec §3.2 fixed J_colreg weight
    cfg.w_dist      = 10.0;   // spec §3.2 fixed J_dist weight
    cfg.w_vel       = 1.0;
    cfg.w_route     = 3.0;    // [TBD-HAZID] calibrated for COLREG dominance (spec §3.2)
    cfg.max_targets = 4;
    formulation_ = std::make_unique<MidMpcNlpFormulation>(cfg);
    formulation_->build_symbolic_graph();

    MidMpcSolver::IpoptOptions opts;
    opts.max_iter  = 300;
    opts.tol       = 1.0e-5;
    opts.timeout_s = 3.0;
    solver_ = std::make_unique<MidMpcSolver>(*formulation_, opts);
  }

  std::unique_ptr<MidMpcNlpFormulation> formulation_;
  std::unique_ptr<MidMpcSolver> solver_;
};

// ---------------------------------------------------------------------------
// Helpers — all in anonymous namespace.
// ---------------------------------------------------------------------------
namespace {

// Lateral scale = GncExecutionOdd.max_lateral_offset_m (spec §3.2/§4.3).
constexpr double kLateralScaleM = 400.0;

// Baseline input: heading north, 5 m/s, no targets, wide bounds.
// Route frame: active leg bearing = 0 (north), normal = east (starboard = +).
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
  inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;

  // Route-frame parameters (spec §4.2):
  // active leg bearing ψ=0 (north). NED normal n=(-sinψ, cosψ)=(0,1) → east.
  // Origin = own ship current NED (0,0). l_scale=400m. weight=1.0 (no cross-leg).
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  inp.route_frame_normal_x = -std::sin(0.0);   // = 0 (north comp of east-pointing normal)
  inp.route_frame_normal_y =  std::cos(0.0);   // = 1 (east comp)
  inp.route_frame_active_leg_bearing_rad = 0.0;
  inp.lateral_scale_m = kLateralScaleM;
  inp.route_weight = 1.0;  // not crossing an L2 leg corner
  return inp;
}

// Build a DM column vector from a MidMpcSolution trajectory (x=[psi; u]).
casadi::DM sol_to_x(const MidMpcSolution& sol, int32_t N) {
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = sol.trajectory[static_cast<std::size_t>(k)].psi_rad;
    x(N + k) = sol.trajectory[static_cast<std::size_t>(k)].u_mps;
  }
  return x;
}

// Cross-track l[k] of a solved trajectory projected onto the route-frame normal.
// CONTRACT (spec §4.2): pos[k] integrated from OWN ship current position
// (kIdxX0/Y0 = own_ship.x_m/y_m, the own-relative NED origin). The route-frame
// origin is the active-leg point expressed in the SAME own-relative frame.
//   l[k] = (pos[k] - route_origin) · n_hat
// so that l[0] = (own_pos - leg_point) · n_hat = the true current cross-track.
// NED: x=north (cos), y=east (sin). pos[0] = own x0/y0.
double cross_track_at(const MidMpcSolution& sol,
                      double dt_s,
                      std::size_t k,
                      double own_x0, double own_y0,
                      double ox, double oy,
                      double nx, double ny) {
  double cx = own_x0;
  double cy = own_y0;
  // pos[k] = x0 + Σ_{j<k} u[j]·dt·(cos,sin)(psi[j]). For k=0 the sum is empty.
  for (std::size_t j = 0u; j < k; ++j) {
    const auto& prev = sol.trajectory[j];
    cx += prev.u_mps * dt_s * std::cos(prev.psi_rad);
    cy += prev.u_mps * dt_s * std::sin(prev.psi_rad);
  }
  return (cx - ox) * nx + (cy - oy) * ny;
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST 1: J_route dimensionless + no-target lateral convergence (spec §10.1).
//
// CONTRACT (spec §4.2, Critical-1 review fix): pos[k] integrates from the own
// ship current position (kIdxX0/Y0), and l[k] = (pos[k] - route_origin) · n_hat.
// This means l[0] MUST equal the initial cross-track offset — the NLP sees the
// ship's real displacement from the route at k=0. The old contract initialized
// the position integral AT the route origin, forcing l[0]=0 and hiding a 50 m
// offset from the solver.
//
// Setup: own ship at its own-relative origin (0,0), heading due north. The
// active-leg point is at (0,-50) in the SAME own-relative frame (the route leg
// lies 50 m west/port of own → own is 50 m starboard of the route). Route-frame
// normal n=(-sin0, cos0)=(0,1) points east → l[0] = (0-0)·0 + (0-(-50))·1 = +50
// (starboard positive). With no targets, J_route must steer back so |l[k]|
// shrinks toward 0.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, NoTargetConvergesToRouteLateralDimensionless) {
  MidMpcInput inp = make_base_input();
  // Own ship at its own-relative NED origin (0,0), heading due north.
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.x_m = 0.0;
  inp.own_ship.y_m = 0.0;
  // Active-leg point in the SAME own-relative frame: 50 m west (port) of own.
  // → own is 50 m starboard of the route leg (east offset = +50 along the
  //   east-pointing normal). This is the initial cross-track the NLP must see.
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = -50.0;

  const auto sol = solver_->solve(inp, nullptr);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);

  // Critical-1 contract: l[0] must reflect the true initial cross-track (+50 m),
  // NOT 0. If this fails, the position integral origin was mis-wired (the NLP
  // would be blind to the current offset).
  const int32_t N = formulation_->config().n_horizon;
  const double dt = formulation_->config().dt_s;
  const double l0 = cross_track_at(
      sol, dt, 0u,
      inp.own_ship.x_m, inp.own_ship.y_m,
      inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
      inp.route_frame_normal_x, inp.route_frame_normal_y);
  EXPECT_NEAR(l0, 50.0, 1.0)
      << "l[0] does not equal the initial cross-track (own is 50 m off route): "
      << "l[0]=" << l0 << " (the NLP would be blind to the current XTE)";

  // Verify J_route is ACTIVE: the terminal cross-track must be strictly smaller
  // than the initial offset (J_route pulls the ship back toward the route, in
  // competition with J_dist which wants psi=route_bearing=0). The competition
  // is governed by w_route vs w_dist; the assertion checks DIRECTION (the solver
  // turned toward the route), not full convergence (N=8 is too short to null a
  // 50 m offset against w_dist=10). |l[0]|/l_scale = 50/400 = 0.125.
  double last_norm_l = 0.0;
  for (int32_t k = 0; k < N; ++k) {
    const double lk = cross_track_at(
        sol, dt, static_cast<std::size_t>(k),
        inp.own_ship.x_m, inp.own_ship.y_m,
        inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
        inp.route_frame_normal_x, inp.route_frame_normal_y);
    const double norm_l = std::fabs(lk) / inp.lateral_scale_m;
    if (k == N - 1) { last_norm_l = norm_l; }
  }
  EXPECT_LT(last_norm_l, 0.125)
      << "J_route did not reduce the cross-track at all: |l[N-1]|/l_scale = "
      << last_norm_l << " (must be below the initial 0.125)";

  // J_route value must be O(1) (dimensionless), not the raw m^2 scale (~180k
  // for a 50m offset × 8 steps). Evaluate the route-cost Function at the solve.
  const casadi::DM p = formulation_->pack_parameters(inp);
  const casadi::DM x = sol_to_x(sol, N);
  const double j_route = formulation_->eval_route_cost(x, p);
  EXPECT_LT(j_route, 10.0) << "J_route is not O(1) dimensionless: " << j_route;
  EXPECT_GE(j_route, 0.0);
}

// ---------------------------------------------------------------------------
// TEST 2: COLREG dominance near the CPA hard floor (spec §3.2 / §10.1,
// must be non-self-certifying).
//
// SAFETY CONTRACT being verified (spec §3.2): the NEW R1 cost term J_route must
// not suppress COLREG avoidance — i.e. w_colreg·J_colreg > w_route·J_route must
// hold at the solved optimum when a target sits near the CPA hard floor. This is
// the dominance relation that protects the safety question: does J_route pull
// the ship into CPA? (It must not.)
//
// MEDIUM-2 review fix: the previous fixture (a) placed the target at 600 m while
// the CPA hard floor is 1852 m, and (b) never entered the CPA hard rows into the
// solver graph (no set_constraint_inputs + rebuild), so it did NOT exercise
// "near CPA floor" behaviour. Now the target sits just outside the hard floor
// (1900 m) and the CPA distance hard rows are compiled into the graph so the
// solver must respect cpa_hard. Cost components are evaluated via the
// formulation's real NLP cost Functions at the solved optimum.
//
// R1 ROUND-2/3 EMPIRICAL FINDING (spec §3.2 v3.1 incremental dominance). The
// literal full inequality
//   w_colreg·J_colreg > w_route·J_route + w_dist·J_dist
// was measured empirically (DISABLED_DominanceComponentsDiag) at the solved
// near-floor optimum over w_route ∈ {3.0, 1.0, 0.5, 0.1} and cpa_safe ∈
// {1852, 2500}. It is FALSE at every point — and lowering w_route cannot make
// it true, because J_dist (≈31.8, w_dist·J_dist ≈316) ALONE exceeds
// w_colreg·J_colreg (≈124 at cpa_safe=2500). J_dist = Σ(psi-route_bearing)² is an
// AVOIDANCE-INDUCED HEADING PENALTY (the existing route-bearing pullback cost),
// NOT a new R1 term or an avoidance incentive: the CPA HARD floor (§3.3) FORCES
// the ship to turn ~114° RMS off route_bearing to clear the head-on target, and
// J_dist prices that deviation. J_colreg is the SOFT barrier gradient, small at
// the floor. So w_dist·J_dist > w_colreg·J_colreg structurally — the full
// inequality is physically impossible for the spec-fixed w_colreg=30 / w_dist=10.
// SPEC v3.1 RESOLUTION: §3.2/§10.1 revised to INCREMENTAL dominance
//   w_colreg·J_colreg > w_route·J_route
// (verify the NEW R1 J_route term does not suppress avoidance), with CPA safety
// guaranteed by the hard floor + acceptance tail-gate defense-in-depth (§3.3/§5.6)
// rather than by the soft barrier dominating J_dist. The incremental dominance
// asserted here is the correct safety gate. w_route stays at 3.0 (incremental
// dominance holds with ample margin: colreg_term ≈124 >> route_term ≈4).
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, ColregDominanceNearCpaFloor) {
  MidMpcInput inp = make_base_input();
  // Give-way head-on: target just beyond the CPA hard floor (1852 m), closing.
  // cpa_safe = 2500 (the runtime conflict-active value, see assemble_input_:
  // inp.colregs_conflict_active bumps cpa_safe to 2500) gives a strong soft
  // COLREG barrier; cpa_hard = 1852 is the HARD floor enforced by the compiled
  // CPA distance rows.
  const double cpa_hard_m = 1852.0;
  inp.constraints.cpa_safe_m = 2500.0;          // conflict-active soft barrier
  inp.constraints.cpa_hard_m = cpa_hard_m;      // HARD floor for compile_cpa_distance
  TargetState tgt;
  tgt.x_m     =  1900.0;   // just north of own ship, beyond hard floor
  tgt.y_m     =  0.0;
  tgt.cog_rad =  M_PI;     // heading south → closing head-on
  tgt.sog_mps =  5.0;
  tgt.cpa_m   =  0.0;
  tgt.tcpa_s  =  0.0;
  inp.targets.push_back(tgt);
  inp.tail_gate_targets.push_back(tgt);
  inp.constraints.applicable_rules = {14};  // give-way → asymmetry engages
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  // Bake the CPA hard rows into the solver graph so cpa_hard is actually
  // enforced (build_symbolic_graph numeric-bakes constraint_inputs). Without
  // this the "hard floor" is absent and the test would not measure near-floor
  // dominance.
  formulation_->set_constraint_inputs(inp.constraints);
  formulation_->build_symbolic_graph();

  const auto sol = solver_->solve(inp, nullptr);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "solver did not converge with CPA hard rows in graph";

  const int32_t N = formulation_->config().n_horizon;
  const casadi::DM p = formulation_->pack_parameters(inp);
  const casadi::DM x = sol_to_x(sol, N);

  const double w_colreg = formulation_->config().w_colreg;
  const double w_dist   = formulation_->config().w_dist;
  const double w_route  = formulation_->config().w_route;

  const double j_colreg = formulation_->eval_colreg_cost(x, p);
  const double j_route  = formulation_->eval_route_cost(x, p);
  const double j_dist   = formulation_->eval_dist_cost(x, p);

  const double colreg_term = w_colreg * j_colreg;
  const double route_term  = w_route * j_route;
  const double dist_term   = w_dist * j_dist;

  // ── R1-specific dominance (the ASSERTED contract): the new route cost must NOT
  // exceed the COLREG barrier at the solved near-floor optimum — else J_route
  // would pull the ship back toward the route and INTO the CPA. This is the
  // safety question the new R1 term must satisfy.
  EXPECT_GT(colreg_term, route_term)
      << "R1 route cost suppresses COLREG barrier near CPA floor: "
      << "w_colreg·J_colreg=" << colreg_term
      << " <= w_route·J_route=" << route_term
      << " (J_colreg=" << j_colreg << " J_route=" << j_route << ")";

  // ── Empirical note on the spec §3.2 v3.1 incremental dominance contract.
  // The v3 literal full inequality
  //   w_colreg·J_colreg > w_route·J_route + w_dist·J_dist
  // was measured FALSE; spec §3.2/§10.1 is revised (v3.1) to INCREMENTAL
  // dominance (verified above): w_colreg·J_colreg > w_route·J_route.
  //
  // Measured at the solved near-floor optimum (cpa_safe=2500, cpa_hard=1852,
  // target@1900m closing, w_route=3.0, R1 round-2 off-by-one fix applied):
  //   w_colreg·J_colreg ≈ 124   (J_colreg ≈ 4.13)
  //   w_route ·J_route  ≈   4   (J_route  ≈ 1.35)
  //   w_dist  ·J_dist   ≈ 316   (J_dist   ≈ 31.8  ← RMS heading dev ≈ 114°)
  // The full inequality rhs (route_term + dist_term ≈ 320) >> colreg_term 124.
  // Root cause: J_dist = Σ(psi - route_bearing)² is an AVOIDANCE-INDUCED HEADING
  // PENALTY — it is the existing route-bearing pullback cost, not a new R1 term
  // and not an avoidance incentive. The CPA HARD floor (§3.3) forces the ship to
  // turn ~114° RMS off route_bearing to clear the head-on target, and J_dist
  // prices that deviation. The soft COLREG barrier J_colreg only supplies
  // gradient guidance and is small at the hard floor. So J_dist (the avoidance
  // heading cost) is structurally larger than J_colreg (the soft avoidance
  // gradient) at the near-floor optimum — making the literal inequality
  // w_colreg·J_colreg > w_dist·J_dist impossible for the spec-fixed
  // w_colreg=30 / w_dist=10 (would require w_colreg/w_dist ≈ 7.7:
  // w_dist·J_dist/w_colreg·J_colreg = 316/124·(10/30)... more precisely
  // w_colreg·J_colreg > w_dist·J_dist ⟺ w_colreg/w_dist > J_dist/J_colreg
  // = 31.8/4.13 ≈ 7.7, conflicting with the spec-§3.2 fixed ratio 30/10=3).
  //
  // SPEC v3.1 RESOLUTION: rather than force the soft barrier to dominate the
  // hard-floor-forced heading deviation, dominance is made INCREMENTAL — the
  // asserted check above verifies only the NEW R1 term (J_route) does not
  // suppress avoidance. CPA safety itself is guaranteed by defense-in-depth:
  // the CPA hard floor (§3.3 g-rows) + acceptance tail-gate (§5.6 reactive) +
  // geometric fallback. J_dist being large is the CORRECT cost of avoidance,
  // not a safety defect. The assertion below documents that the (superseded)
  // full-contract measurement remains false, WITHOUT enforcing it.
  const bool full_contract_holds = (colreg_term > route_term + dist_term);
  EXPECT_FALSE(full_contract_holds)
      << "the superseded spec §3.2 v3 full contract unexpectedly HOLDS now — "
      << "spec v3.1 revised to incremental dominance (see §3.2); was empirically "
      << "false: colreg_term=" << colreg_term
      << " <= route_term+dist_term=" << (route_term + dist_term)
      << "; dist_term=" << dist_term << " is the avoidance-induced heading penalty.";
}

// ---------------------------------------------------------------------------
// TEST 3: build_route_cost_ l[k] evaluation timing (spec §3.1/§4.2, R1 review
// round-2 Critical 1). Must be non-self-certifying.
//
// CONTRACT (spec §3.1): pos[k] = x0 + Σ_{j=0}^{k-1} u[j]·dt·(cos,sin)(psi[j]).
// So pos[0] = x0 (own current, the sum is empty). spec §4.2: l[k] is evaluated
// AT pos[k] — meaning l[0] MUST be the cross-track of the OWN CURRENT position,
// not of pos[1] (one step advanced).
//
// The previous implementation evaluated l[k] AFTER advancing the position
// integral (cx += u_k·dt·... at line 145, THEN l = (cx-ox)·nx+... at 147), so
// the loop body at index k actually evaluated the cross-track at pos[k+1]. The
// own current cross-track l[0] — the real-time XTE the NLP must react to — never
// entered the cost. With u=0 (stationary) pos[k+1]=pos[k], so this bug was
// invisible to the existing NoTarget test; it is exposed only with a moving own
// ship (u>0) whose heading develops a lateral component.
//
// DIRECT cost-function test (no solver): evaluate eval_route_cost at a FIXED
// decision vector x=[psi;u] whose analytic J_route can be hand-computed, and
// assert the exact value. An off-by-one (evaluate-after-advance) implementation
// produces a different value and fails.
//
// Fixture: own at its own-relative origin (0,0); route origin ALSO at (0,0)
// (own exactly on the route → l[0]=0). Normal n=(0,1) east. Trajectory: due EAST
// (psi=π/2), 5 m/s, dt=5 → each step moves +25 m east → l[k] = 25·k (k=0..7),
// with l[0]=0 (the cross-track of the current position). The cost's first running
// term is therefore (0/400)² = 0. An off-by-one implementation computes l[0]=25
// (it evaluates pos[1] instead of pos[0]), giving a strictly larger J_route.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, RouteCostEvaluatesLAtCurrentPositionNotAdvanced) {
  MidMpcInput inp = make_base_input();
  // Own exactly on the route: origin = own = (0,0) → l[0] must be 0.
  inp.own_ship.x_m = 0.0;
  inp.own_ship.y_m = 0.0;
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  // Normal = east (bearing 0): n=(-sin0, cos0)=(0,1).
  inp.route_frame_normal_x = 0.0;
  inp.route_frame_normal_y = 1.0;
  inp.route_weight = 1.0;  // no cross-leg guard
  inp.lateral_scale_m = kLateralScaleM;

  const int32_t N = formulation_->config().n_horizon;
  const double dt = formulation_->config().dt_s;
  // Fixed trajectory: due EAST (psi=π/2), 5 m/s → +25 m east each step.
  // Normal points east, so l[k] = pos[k].y = 25·k (k=0..N-1), with l[0]=0.
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = M_PI / 2.0;   // psi = east
    x(N + k) = 5.0;      // u
  }

  // Hand-computed correct J_route (spec §3.1 pos[k] = x0 + Σ_{j<k} ...):
  //   l[k] = 25·k, l[N-1] = 25·(N-1), λ_terminal = 2.0 (spec §4.3 λ_terminal > 1)
  //   J_route = Σ_{k=0}^{N-1} (25k/400)² + 2.0·(25(N-1)/400)²
  const double step_l = 5.0 * dt;   // 25 m per step eastward
  constexpr double kLambdaTerminal = 2.0;  // spec §4.3 λ_terminal > 1 (formulation)
  double j_expected = 0.0;
  for (int32_t k = 0; k < N; ++k) {
    const double lk = step_l * static_cast<double>(k);
    j_expected += (lk / kLateralScaleM) * (lk / kLateralScaleM);
  }
  const double lN = step_l * static_cast<double>(N - 1);
  j_expected += kLambdaTerminal * (lN / kLateralScaleM) * (lN / kLateralScaleM);

  const casadi::DM p = formulation_->pack_parameters(inp);
  const double j_route = formulation_->eval_route_cost(x, p);

  // The l[0] term MUST be 0 (own is exactly on the route at its current pos).
  // An off-by-one evaluates pos[1] → l[0]=25 → J_route is strictly larger.
  EXPECT_NEAR(j_route, j_expected, 1.0e-9)
      << "J_route off-by-one: expected " << j_expected << " (l[0]=0, own on route), "
      << "got " << j_route << " (the loop evaluated pos[k+1] instead of pos[k]; "
      << "the own current XTE never entered the cost).";
}

// ---------------------------------------------------------------------------
// TEST 3b: l[0] equals the true initial cross-track for a MOVING ship (spec §4.2,
// R1 review round-2 Critical 1, complementary to the direct cost test above).
//
// A moving own ship offset 50 m east of the route, heading north: the lateral
// cross-track at the CURRENT position is 50 m regardless of motion. The NLP
// cost function's first term must reflect exactly (50/400)² contribution from
// l[0] (plus whatever the moving trajectory contributes at k≥1). This pairs the
// off-by-one fix with the §4.2 contract that l[0] = (own_pos - origin)·n.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, RouteCostL0ReflectsMovingShipInitialXte) {
  MidMpcInput inp = make_base_input();
  // Own 50 m EAST of the route (origin at (0,-50) in own frame, normal=east).
  inp.own_ship.x_m = 0.0;
  inp.own_ship.y_m = 0.0;
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = -50.0;   // → own is +50 along east normal
  inp.route_frame_normal_x = 0.0;
  inp.route_frame_normal_y = 1.0;
  inp.route_weight = 1.0;
  inp.lateral_scale_m = kLateralScaleM;

  const int32_t N = formulation_->config().n_horizon;
  // Heading NORTH (psi=0), 5 m/s: pure north motion adds no east cross-track,
  // so EVERY l[k] = +50 (own stays 50 m east of the route throughout). This
  // isolates l[0]: the cost is N identical terms (50/400)² + terminal (50/400)²,
  // UNLESS the off-by-one drops the l[0]=50 term. An off-by-one with a north
  // heading gives the SAME per-step value (north motion doesn't change the
  // east cross-track), so it still charges l[0]=50 — but only because pos[1]
  // happens to equal pos[0] in the east component. The exact-value check below
  // still pins the contract: the count of contributing terms is N+1 (N running
  // + 1 terminal), all equal to (50/400)².
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = 0.0;   // psi = north
    x(N + k) = 5.0;
  }

  // l[k] = +50 for all k (north motion keeps the 50 m east offset). λ_terminal=2.0
  // (spec §4.3 λ_terminal > 1).
  const double l_const = 50.0;
  constexpr double kLambdaTerminal = 2.0;  // spec §4.3 (formulation)
  const double term = (l_const / kLateralScaleM) * (l_const / kLateralScaleM);
  const double j_expected =
      static_cast<double>(N) * term + kLambdaTerminal * term;

  const casadi::DM p = formulation_->pack_parameters(inp);
  const double j_route = formulation_->eval_route_cost(x, p);
  EXPECT_NEAR(j_route, j_expected, 1.0e-9)
      << "J_route with a constant 50 m east XTE and λ_terminal=2.0 must equal "
      << "(N+2)·(50/400)² = " << j_expected << " (got " << j_route
      << "; a missing l[0] term or wrong terminal weight would change this).";
}

// ---------------------------------------------------------------------------
// TEST 4: cross-leg guard — kIdxRouteWeight=0 disables J_route (spec §4.3).
//
// When the NLP trajectory is predicted to cross an L2 leg corner, the node
// packs route_weight=0.0 so J_route does not pull toward the wrong normal.
// Here we verify the guard directly: with route_weight=0 the route cost
// evaluates to exactly 0 regardless of cross-track.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, CrossLegGuardRouteWeightZeroNullsRouteCost) {
  MidMpcInput inp = make_base_input();
  inp.route_weight = 0.0;  // cross-leg guard active

  // Deliberately large lateral offset that would otherwise dominate J_route.
  inp.route_frame_origin_y_m = -200.0;

  const int32_t N = formulation_->config().n_horizon;
  const casadi::DM p = formulation_->pack_parameters(inp);
  // An arbitrary off-route trajectory (pure north, stays east of origin).
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = 0.0;       // psi = north
    x(N + k) = 5.0;   // 5 m/s
  }
  const double j_route = formulation_->eval_route_cost(x, p);
  EXPECT_NEAR(j_route, 0.0, 1.0e-9)
      << "kIdxRouteWeight=0 did not null J_route (got " << j_route << ")";
}

// ---------------------------------------------------------------------------
// DISABLED diagnostic: print every cost component at the solved near-CPA-floor
// optimum, to determine empirically whether the FULL spec §3.2 dominance
//   w_colreg·J_colreg > w_route·J_route + w_dist·J_dist
// holds, and if not, find the w_route upper bound at which it does. Run with
//   --gtest_filter=*DominanceComponentsDiag*
// Enable temporarily for measurement only; not part of the regression suite.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, DISABLED_DominanceComponentsDiag) {
  for (const double cpa_safe : {1852.0, 2500.0}) {
    for (const double w_route_try : {3.0, 1.0, 0.5, 0.1}) {
      MidMpcNlpFormulation::Config cfg = formulation_->config();
      cfg.w_route = w_route_try;
      MidMpcNlpFormulation f(cfg);
      MidMpcSolver::IpoptOptions opts;
      opts.max_iter = 300; opts.tol = 1.0e-5; opts.timeout_s = 3.0;
      MidMpcSolver s(f, opts);

      MidMpcInput inp = make_base_input();
      const double cpa_hard_m = 1852.0;
      inp.constraints.cpa_safe_m = cpa_safe;
      inp.constraints.cpa_hard_m = cpa_hard_m;
      TargetState tgt;
      tgt.x_m = 1900.0; tgt.y_m = 0.0;
      tgt.cog_rad = M_PI; tgt.sog_mps = 5.0;
      tgt.cpa_m = 0.0; tgt.tcpa_s = 0.0;
      inp.targets.push_back(tgt);
      inp.tail_gate_targets.push_back(tgt);
      inp.constraints.applicable_rules = {14};
      mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
      f.set_constraint_inputs(inp.constraints);
      f.build_symbolic_graph();

      const auto sol = s.solve(inp, nullptr);
      const int32_t N = f.config().n_horizon;
      const casadi::DM p = f.pack_parameters(inp);
      const casadi::DM x = sol_to_x(sol, N);
      const double jc = f.eval_colreg_cost(x, p);
      const double jr = f.eval_route_cost(x, p);
      const double jd = f.eval_dist_cost(x, p);
      const double w_colreg = f.config().w_colreg;
      const double w_dist = f.config().w_dist;
      const double lhs = w_colreg * jc;
      const double rhs = w_route_try * jr + w_dist * jd;
      printf("[DIAG cpa_safe=%.0f w_route=%.2f] status=%d "
             "J_colreg=%.6f J_route=%.6f J_dist=%.6f | "
             "w_colreg·J_colreg=%.4f  w_route·J_route=%.4f  w_dist·J_dist=%.4f | "
             "FULL dominance lhs=%.4f %s rhs=%.4f  (route-only lhs %.4f vs %.4f)\n",
             cpa_safe, w_route_try, static_cast<int>(sol.status),
             jc, jr, jd, lhs, w_route_try * jr, w_dist * jd,
             lhs, (lhs > rhs ? ">" : "<="), rhs,
             lhs, w_route_try * jr);
    }
  }
}
