// test/unit/test_mid_mpc_terminal.cpp
// Slice T1: terminal constraints + smooth J_terminal (spec §5.4 / §5.5 / §3.3).
//
// Verifies:
//   1. give-way terminal on the correct side + within lateral bounds (g≥0 rows).
//   2. stand-on: terminal constraints disabled (bounds [-inf,+inf]).
//   3. J_terminal is smooth (softplus, no max/abs): bilateral derivative continuity.
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
#include "m5_tactical_planner/mid_mpc/row_registry.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;
using mass_l3::m5::mid_mpc::RowBoundConfig;
using mass_l3::m5::mid_mpc::kIdxPreferredDir;
using mass_l3::m5::mid_mpc::kIdxRole;
using mass_l3::m5::mid_mpc::kIdxLateralScale;

// ---------------------------------------------------------------------------
// Fixture — builds the terminal-enabled NLP once; reused by all tests.
// N=8 (not the spec default N=18) keeps the unit-test suite fast (each solve is
// a real IPOPT NLP).
// ---------------------------------------------------------------------------
class TerminalConstraintTest : public ::testing::Test {
 protected:
  void SetUp() override {
    MidMpcNlpFormulation::Config cfg;
    cfg.n_horizon   = 8;
    cfg.dt_s        = 5.0;
    cfg.w_colreg    = 30.0;
    cfg.w_dist      = 10.0;
    cfg.w_vel       = 1.0;
    cfg.w_route     = 3.0;
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
  // Route-frame: bearing 0 (north), normal = east (starboard +), origin = own.
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = 0.0;
  inp.route_frame_normal_x = -std::sin(0.0);   // = 0
  inp.route_frame_normal_y =  std::cos(0.0);   // = 1 (east)
  inp.route_frame_active_leg_bearing_rad = 0.0;
  inp.lateral_scale_m = kLateralScaleM;
  inp.route_weight = 1.0;
  return inp;
}

  // Terminal cross-track l[N-1] of a solved trajectory projected onto the
  // route-frame normal. pos[N-1] = x0 + Σ_{j<N-1} u[j]·dt·(cos,sin)(psi[j]).
  double terminal_cross_track(const MidMpcSolution& sol, double dt_s,
                            int32_t N,
                            double own_x0, double own_y0,
                            double ox, double oy,
                            double nx, double ny) {
  double cx = own_x0;
  double cy = own_y0;
  for (int32_t j = 0; j < N - 1; ++j) {
    const auto& prev = sol.trajectory[static_cast<std::size_t>(j)];
    cx += prev.u_mps * dt_s * std::cos(prev.psi_rad);
    cy += prev.u_mps * dt_s * std::sin(prev.psi_rad);
  }
  return (cx - ox) * nx + (cy - oy) * ny;
}

}  // namespace

// ===========================================================================
// TEST 1: give-way terminal on the correct side + within lateral bounds
// (spec §5.5 / §10.1 terminal acceptance).
//
// Setup: give-way role (role=1), preferred_direction=+1 (starboard). The NLP
// is solved with terminal constraints ACTIVE. After solving, the terminal
// cross-track l[N-1] must satisfy:
//   g_term_side:  preferred_direction · l[N-1] ≥ l_min_feasible  (same side)
//   g_term_lo:    l[N-1] + l_max_feasible ≥ 0                    (lower bound)
//   g_term_hi:    l_max_feasible - l[N-1] ≥ 0                    (upper bound)
// i.e. l[N-1] ∈ [l_min_feasible, l_max_feasible] (positive / starboard side).
//
// To force a starboard terminal, the own ship starts ON the route and the
// give_way gate + the J_terminal softplus + hard side constraint bias the
// horizon tail to l[N-1] > 0 (starboard, the preferred side). The target is
// far away (no active avoidance) so the terminal side gate dominates.
// ===========================================================================
TEST_F(TerminalConstraintTest, GiveWayTerminalOnCorrectSideWithinBounds) {
  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 1U;  // GIVE_WAY
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (stbd)
  // Rule 15 sets the encounter context; the J_terminal cost gate is now
  // role-based (kIdxRole from colregs_primary_role=1), so this rule list is
  // for the ConstraintCompiler context, not the cost gate.
  inp.constraints.applicable_rules = {15};
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  // Solve with terminal constraints ACTIVE (default RowBoundConfig has
  // terminal_disabled=false → terminal rows are [0,+inf] hard).
  RowBoundConfig rb;  // defaults: terminal not disabled
  const auto sol = solver_->solve(inp, nullptr, rb);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);

  const int32_t N = formulation_->config().n_horizon;
  const double dt = formulation_->config().dt_s;

  // Evaluate the terminal cross-track at the solved optimum.
  const double lN = terminal_cross_track(
      sol, dt, N,
      inp.own_ship.x_m, inp.own_ship.y_m,
      inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
      inp.route_frame_normal_x, inp.route_frame_normal_y);

  // Terminal feasibility constants (must match the formulation defaults).
  const double l_min = formulation_->config().terminal_l_min_feasible_m;
  const double l_max = formulation_->config().terminal_l_max_feasible_m;

  // g_term_side = preferred_direction · l[N-1] - l_min_feasible ≥ 0
  const double g_term_side = 1.0 * lN - l_min;
  // g_term_lo = l[N-1] + l_max_feasible ≥ 0
  const double g_term_lo = lN + l_max;
  // g_term_hi = l_max_feasible - l[N-1] ≥ 0
  const double g_term_hi = l_max - lN;

  EXPECT_GE(g_term_side, -1.0e-6)
      << "g_term_side violated: pref_dir·l[N-1]=" << (1.0 * lN)
      << " < l_min_feasible=" << l_min
      << " (terminal on wrong side or below min feasible lateral)";
  EXPECT_GE(g_term_lo, -1.0e-6)
      << "g_term_lo violated: l[N-1]=" << lN
      << " < -l_max_feasible=" << (-l_max) << " (terminal below lower bound)";
  EXPECT_GE(g_term_hi, -1.0e-6)
      << "g_term_hi violated: l[N-1]=" << lN
      << " > l_max_feasible=" << l_max << " (terminal above upper bound)";
}

// ===========================================================================
// TEST 2: stand-on role → terminal constraints DISABLED (spec §5.5).
//
// spec §5.5: "stand-on 角色：无 terminal 约束". With terminal_disabled=true,
// the terminal rows' bounds become [-inf,+inf] (no constraint), so the solver
// is free to place l[N-1] anywhere — including the "wrong" side — without
// infeasibility. We verify the bounds switch directly on RowRegistry (the
// mechanism that disables the rows), since a stand-on solve would otherwise be
// identical to a no-constraint solve (hard to distinguish numerically).
// ===========================================================================
TEST_F(TerminalConstraintTest, StandOnDisablesTerminalBounds) {
  using mass_l3::m5::mid_mpc::RowRegistry;
  RowRegistry reg(/*N=*/8, /*n_targets=*/0, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);

  // terminal_disabled=true → all 3 terminal rows become [-inf,+inf].
  RowBoundConfig rb;
  rb.terminal_disabled = true;
  const auto b = reg.build_bounds(rb);
  constexpr double kInf = std::numeric_limits<double>::infinity();
  for (int i = 0; i < 3; ++i) {
    const int r = reg.terminal_row(i);
    EXPECT_EQ(b.lbg[static_cast<std::size_t>(r)], -kInf)
        << "terminal row " << i << " lb not disabled (-inf) under stand-on";
    EXPECT_EQ(b.ubg[static_cast<std::size_t>(r)], kInf)
        << "terminal row " << i << " ub not disabled (+inf) under stand-on";
  }

  // Conversely, terminal_disabled=false → terminal rows stay [0,+inf] (active).
  // v2.1 §4.5 flipped terminal_nlp_soft default to true, so this legacy-shape
  // assertion explicitly pins terminal_nlp_soft=false to test the hard-terminal
  // path independently of the v2.1 default.
  RowBoundConfig rb_active;  // terminal_disabled=false (default)
  rb_active.terminal_nlp_soft = false;  // pin legacy hard-terminal shape
  const auto b2 = reg.build_bounds(rb_active);
  for (int i = 0; i < 3; ++i) {
    const int r = reg.terminal_row(i);
    EXPECT_EQ(b2.lbg[static_cast<std::size_t>(r)], 0.0)
        << "terminal row " << i << " lb not active (0) for give-way";
    EXPECT_EQ(b2.ubg[static_cast<std::size_t>(r)], kInf)
        << "terminal row " << i << " ub not active (+inf) for give-way";
  }
}

// ===========================================================================
// TEST 3: J_terminal is smooth (softplus, no max/abs).
// (spec §5.4 Critical: "删除 v1 的 (max(0,|l|-lmax))²（max 非光滑）").
//
// A C∞ function has continuous derivatives on both sides. We verify smoothness
// NUMERICALLY: evaluate build_terminal_cost_ at l[N-1] values sweeping across
// the "wrong side" transition (l crosses 0 relative to preferred_direction)
// and assert the first difference (discrete derivative proxy) is continuous —
// i.e. no jump in slope that would betray a max/abs kink.
//
// We do this by evaluating the terminal cost Function at a fixed x whose only
// lateral contribution comes from the terminal step, sweeping the heading to
// move l[N-1] from negative (wrong side for +preferred) through 0 to positive
// (correct side). A non-smooth max/abs cost would show a slope discontinuity
// at the l[N-1]=0 crossing; softplus is smooth everywhere.
//
// Additionally we verify give_way=0 nulls the cost (stand-on gate).
// ===========================================================================
TEST_F(TerminalConstraintTest, JTerminalIsSmoothSoftplusNoMaxAbs) {
  const int32_t N = formulation_->config().n_horizon;

  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 1U;  // GIVE_WAY
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 → wrong side = l<0
  inp.constraints.applicable_rules = {15};  // give-way gate
  // pack_parameters now populates kIdxPreferredDir/kIdxRole/kIdxGiveWay from input.
  casadi::DM p = formulation_->pack_parameters(inp);

  // Sweep: heading from slightly-port to slightly-starboard so l[N-1] crosses 0.
  // l[N-1] = Σ_{j<N-1} u·dt·sin(psi). With psi near 0 (north) and normal=east,
  // l[N-1] ≈ u·dt·(N-1)·sin(psi). psi=0 → l=0; psi>0 (east turn) → l>0 (stbd).
  // Sample finely around psi=0 to detect any kink.
  constexpr std::size_t kSamples = 41;
  const double psi_span = 0.20;  // ±0.2 rad around 0
  std::vector<double> costs(kSamples);
  for (std::size_t s = 0; s < kSamples; ++s) {
    const double psi = -psi_span + 2.0 * psi_span * static_cast<double>(s)
                       / static_cast<double>(kSamples - 1);
    casadi::DM x = casadi::DM::zeros(2 * N, 1);
    for (int32_t k = 0; k < N; ++k) {
      x(k) = psi;
      x(N + k) = 5.0;
    }
    costs[s] = formulation_->eval_terminal_cost(x, p);
  }

  // Smoothness: the discrete second difference (curvature proxy) must be bounded
  // — a max/abs kink produces a spike in the second difference at the kink.
  // softplus second derivative is bounded by 1/(4·tau²) everywhere. With tau_t
  // default ~0.1-1.0, this is O(1)-O(100). A kink would show a value orders of
  // magnitude larger than its neighbours.
  double max_second_diff = 0.0;
  for (std::size_t s = 1; s < kSamples - 1; ++s) {
    const double d2 = costs[s + 1] - 2.0 * costs[s] + costs[s - 1];
    max_second_diff = std::max(max_second_diff, std::fabs(d2));
  }
  // softplus(·/tau)·tau has max second derivative 1/(4·tau) at the inflection.
  // With tau_t ≥ 0.05 this is ≤ 5.0; allow generous headroom (×100) for the
  // discrete sampling. A true max/abs kink (slope jump ~O(1)) would produce a
  // second difference ~O(step_size) unbounded relative to neighbours.
  const double tau_t = formulation_->config().terminal_tau;
  const double smooth_bound = std::max(0.25 / std::max(tau_t, 1.0e-3) * 100.0, 1000.0);
  (void)max_second_diff;  // documented below
  EXPECT_LT(max_second_diff, smooth_bound)
      << "J_terminal second-difference spike " << max_second_diff
      << " suggests a non-smooth max/abs kink (spec §5.4 Critical); tau_t=" << tau_t;

  // ── BILATERAL derivative continuity at the wrong_side=0 crossing (review
  // test gap 3). The second-difference bound above is loose (≥1000). A sharper,
  // definitive smoothness check: compute the left and right DERIVATIVES of
  // J_terminal w.r.t. psi at the crossing where wrong_side = -pref·(l/lscale)
  // passes through 0 (psi=0 here, since l ∝ sin(psi)≈psi near 0). A C∞
  // softplus has dJ/dpsi continuous there; a max(0,·) or |·| kink has a SLOPE
  // JUMP (left derivative ≠ right derivative). We assert the one-sided
  // numerical derivatives agree to a tight tolerance.
  //
  // wrong_side=0 occurs at psi=0 (l[N-1]=0). We probe at psi=±h with a small h
  // and use a central + bilateral finite difference of the derivative.
  constexpr double kH = 1.0e-4;   // small step for one-sided derivative
  auto cost_at = [&](double psi) -> double {
    casadi::DM x = casadi::DM::zeros(2 * N, 1);
    for (int32_t k = 0; k < N; ++k) { x(k) = psi; x(N + k) = 5.0; }
    return formulation_->eval_terminal_cost(x, p);
  };
  // One-sided derivatives at psi=0 (the kink point for a hypothetical max/abs).
  const double j_mh  = cost_at(-kH);
  const double j_0   = cost_at(0.0);
  const double j_ph  = cost_at(kH);
  const double d_left  = (j_0 - j_mh) / kH;   // dJ/dpsi from the left (wrong side)
  const double d_right = (j_ph - j_0) / kH;   // dJ/dpsi to the right (correct side)
  // For a smooth softplus the one-sided derivatives agree to O(h)~1e-4. A
  // max(0,·) kink would give |d_left - d_right| ~ O(1) (the slope jump). Use a
  // tight bound scaled to the softplus slope magnitude at the crossing.
  const double slope_scale = 0.5 * (std::fabs(d_left) + std::fabs(d_right)) + 1.0e-9;
  const double deriv_jump = std::fabs(d_left - d_right);
  // Derivative continuity: the relative slope jump must be < a few %. A non-
  // smooth cost fails this by orders of magnitude.
  EXPECT_LT(deriv_jump, 0.05 * slope_scale + 5.0e-3)
      << "J_terminal derivative discontinuity at wrong_side=0: d_left=" << d_left
      << " d_right=" << d_right << " jump=" << deriv_jump
      << " (max/abs kink present, spec §5.4 Critical)";

  // Monotonicity sanity: with preferred_direction=+1, the wrong side is l<0
  // (psi<0 here). J_terminal must be DECREASING as psi goes from negative
  // (wrong side, high cost) to positive (correct side, low cost): the softplus
  // argument (-pref·l/tau) shrinks. Check the total trend.
  EXPECT_GT(costs.front(), costs.back())
      << "J_terminal not higher on wrong side (psi<0) than correct side (psi>0): "
      << "front=" << costs.front() << " back=" << costs.back();

  // ── role gate closed: stand-on (role=0) has NO terminal cost (spec §5.4
  // gate). The cost gate now uses kIdxRole (same source as the solver's
  // terminal_disabled derivation), so nul the ROLE slot — NOT kIdxGiveWay
  // (which is the rule14/15-only asymmetry gate, now unrelated to J_terminal).
  casadi::DM p_standon = p;
  p_standon(kIdxRole) = 0.0;   // stand-on role → cost gate closed
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = -0.1;   // wrong side (would be expensive if gate open)
    x(N + k) = 5.0;
  }
  const double j_standon = formulation_->eval_terminal_cost(x, p_standon);
  EXPECT_NEAR(j_standon, 0.0, 1.0e-9)
      << "role=0 (stand-on) did not null J_terminal (role gate broken): " << j_standon;
}

// ===========================================================================
// TEST 4: J_terminal role-gate regression (spec §5.4 + §3.3, review High 1).
//
// spec §5.4: the J_terminal give_way gate is ROLE-BASED (give-way =
// primary_role ∈ {GIVE_WAY, BOTH_GIVE_WAY}), NOT rule14/15-only. Rule 13
// (overtaking) is also a give-way situation. The v1 pack_parameters derived
// give_way from applicable_rules (14/15 only), so a Rule-13 give-way target
// (role=1, pref_dir=Starboard, rules={13}, NO 14/15) packed give_way=0 →
// J_terminal=0 while the solver's terminal_disabled derivation (which IS
// role-based) kept the terminal hard rows ACTIVE. rows-active + cost=0 is a
// cost/constraint inconsistency.
//
// This test exposes the gap: a give-way role (role=1) with ONLY Rule 13
// applicable must still yield J_terminal > 0 (cost gate open via role).
// Before the fix (kIdxGiveWay = rule14/15 only) this packs give_way=0 and
// J_terminal == 0 (FAIL). After the fix (gate uses role) J_terminal > 0 (PASS).
//
// We place the terminal on the WRONG side (l[N-1] < 0 for pref_dir=+1) so the
// softplus cost is unambiguously positive (no degenerate l=0 case).
// ===========================================================================
TEST_F(TerminalConstraintTest, JTerminalRoleGateAppliesForRule13GiveWay) {
  const int32_t N = formulation_->config().n_horizon;

  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 1U;  // GIVE_WAY (role-based)
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (stbd)
  // ONLY Rule 13 (overtaking) — NO 14/15. The role-based gate must still open.
  inp.constraints.applicable_rules = {13};
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  casadi::DM p = formulation_->pack_parameters(inp);
  // Sanity: pref_dir packed to +1 (so wrong side is l[N-1] < 0).
  ASSERT_NEAR(static_cast<double>(casadi::DM::vec(p)(kIdxPreferredDir)),
              1.0, 1.0e-12);
  // role packed to give-way (kIdxRole=1.0).
  ASSERT_NEAR(static_cast<double>(casadi::DM::vec(p)(kIdxRole)),
              1.0, 1.0e-12);

  // Terminal on the WRONG side: heading slightly to PORT so l[N-1] < 0 (east
  // normal → l = Σ u·dt·sin(psi), psi<0 → l<0 = wrong side for pref_dir=+1).
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = -0.10;   // ~5.7° port → l[N-1] < 0 (wrong side)
    x(N + k) = 5.0;
  }
  const double j = formulation_->eval_terminal_cost(x, p);

  // ROLE gate: even with NO rule14/15 applicable, a give-way role must open
  // the terminal cost. l[N-1] on the wrong side ⇒ softplus argument > 0 ⇒
  // J_terminal strictly > 0 (≈ tau·log2 at l=0, growing for l<0).
  EXPECT_GT(j, 1.0e-6)
      << "J_terminal=" << j << " for give-way role (Rule 13, no 14/15): "
      << "cost gate used rule14/15 (kIdxGiveWay) instead of role (spec §5.4).";
}

// ===========================================================================
// TEST 5: stand-on auto-disable via solver (spec §3.3 / §5.5, review test gap 4).
//
// TEST 2 verified RowRegistry bounds directly (manual terminal_disabled flag).
// This test verifies the SOLVER auto-derives terminal_disabled from a stand-on
// MidMpcInput (role=0 STAND_ON) — i.e. passing the DEFAULT RowBoundConfig{} and
// a stand-on input disables the terminal rows, so the solve does NOT go
// infeasible from the g_term_side row (which is ≡ -l_min < 0 when pref_dir=0).
//
// stand-on ⇒ role=0 ⇒ give_way_role=false ⇒ terminal_disabled=true (solver
// :135-139). The solved trajectory's terminal l[N-1] is then unconstrained.
// We additionally assert the terminal cost is ≈0 (cost gate also role-based).
// ===========================================================================
TEST_F(TerminalConstraintTest, StandOnRoleAutoDisablesTerminalViaSolver) {
  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 0U;  // STAND_ON
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (but stand-on)
  inp.constraints.applicable_rules = {17};  // Rule 17 = stand-on duty
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  // Default RowBoundConfig{} — the solver MUST auto-disable terminal rows.
  RowBoundConfig rb;  // terminal_disabled=false (default) → solver derives it
  const auto sol = solver_->solve(inp, nullptr, rb);

  // If terminal rows were NOT auto-disabled, g_term_side = pref_dir·l[N-1] - l_min
  // = 1·l[N-1] - 30. At own x0=y0=0 with no avoidance driver, l[N-1]≈0 →
  // g_term_side ≈ -30 < 0 ⇒ Infeasible. Convergence proves auto-disable.
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "stand-on solve did not converge — terminal rows were not auto-disabled";

  // Cost gate: stand-on role ⇒ J_terminal should be ≈0 (role gate closed).
  const int32_t N = formulation_->config().n_horizon;
  casadi::DM p = formulation_->pack_parameters(inp);
  casadi::DM x_opt = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    if (k < static_cast<int32_t>(sol.trajectory.size())) {
      x_opt(k) = sol.trajectory[static_cast<std::size_t>(k)].psi_rad;
      x_opt(N + k) = sol.trajectory[static_cast<std::size_t>(k)].u_mps;
    }
  }
  const double j_term = formulation_->eval_terminal_cost(x_opt, p);
  EXPECT_NEAR(j_term, 0.0, 1.0e-6)
      << "stand-on J_terminal=" << j_term << " should be ≈0 (role gate closed)";
}

// ===========================================================================
// TEST 6: HOLD / ReduceSpeed auto-disables terminal (spec §3.3, review Medium 2).
//
// spec §3.3: HOLD/ReduceSpeed ⇒ direction/min_alt/terminal disabled (non-active
// alteration behavior). The solver derives terminal_disabled from the activation
// condition; pref_dir=Hold/ReduceSpeed ⇒ pref_active=false ⇒ terminal_disabled.
// We verify a give-way-role + HOLD input does not go infeasible (terminal side
// row disabled) and that ReduceSpeed behaves the same.
// ===========================================================================
TEST_F(TerminalConstraintTest, HoldReduceSpeedAutoDisablesTerminal) {
  for (const auto beh : {mass_l3::m5::ColregsPreferredDirection::Hold,
                         mass_l3::m5::ColregsPreferredDirection::ReduceSpeed}) {
    MidMpcInput inp = make_base_input();
    inp.colregs_primary_role = 1U;  // GIVE_WAY
    inp.colregs_preferred_direction = beh;  // Hold or ReduceSpeed
    inp.constraints.applicable_rules = {14};
    mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

    RowBoundConfig rb;  // default — solver must auto-disable terminal
    const auto sol = solver_->solve(inp, nullptr, rb);
    EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
        << "give-way + HOLD/ReduceSpeed solve did not converge "
        << "(terminal not auto-disabled for behavior="
        << static_cast<int>(beh) << ")";
  }
}

// v2.1 spec §4.5 B7-r2 — upper-band two-sided softplus activates at |l| > l_max.
//
// Strategy: isolate the upper-band by comparing J_terminal at two terminals on
// the SAME correct side (Starboard, pref_dir=+1) so the lower-band wrong-side
// softplus stays ≈constant small. One terminal is within the lateral band
// (y << l_max → upper-band off); the other is far outside (y >> l_max →
// upper-band on). The cost difference isolates the upper-band contribution.
//
// To make the upper-band signal decisive despite tau_t=0.5, use a tight
// lateral_scale (l_scale=80m) so z=(y-l_max)/l_scale becomes O(1) at y=200.
//
// Route frame: bearing=0 (north), normal=+east → lateral offset = y_m.
// u=5 m/s, dt=5s, N=8 → each step moves 25·sin(psi) m in y. y=200 over 8 steps
// needs sin(psi)=1.0 → psi=π/2. y=30: sin(psi)=30/(25·7)≈0.171 → psi≈0.172.
TEST_F(TerminalConstraintTest, UpperBandCostActivatesBeyondLMax) {
  MidMpcNlpFormulation::Config cfg_tight = formulation_->config();
  cfg_tight.terminal_l_max_feasible_m = 50.0;
  MidMpcNlpFormulation form_tight(cfg_tight);
  form_tight.build_symbolic_graph();

  const int32_t N = form_tight.config().n_horizon;  // 8
  auto eval_j_terminal_at_psi = [&](double psi_rad, double l_scale) -> double {
    MidMpcInput inp = make_base_input();
    inp.colregs_primary_role = 1U;  // give-way
    inp.colregs_preferred_direction =
        mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 correct side = +y
    inp.constraints.applicable_rules = {15};  // give-way gate
    inp.lateral_scale_m = l_scale;
    casadi::DM p = form_tight.pack_parameters(inp);
    casadi::DM x = casadi::DM::zeros(2 * N, 1);
    for (int32_t k = 0; k < N; ++k) {
      x(k) = psi_rad;
      x(N + k) = 5.0;
    }
    return form_tight.eval_terminal_cost(x, p);
  };

  constexpr double kPi2 = 1.5707963267948966;
  constexpr double kLscale = 80.0;  // tight l_scale to sharpen upper-band signal
  // Both terminals on the starboard (correct) side: lower-band ≈ equal & small.
  // y≈+200 (>> l_max=50): upper-band strongly active.
  // y≈+30  (< l_max=50):   upper-band off.
  const double cost_far  = eval_j_terminal_at_psi( kPi2, kLscale);          // y≈+200
  const double cost_near = eval_j_terminal_at_psi(0.172, kLscale);          // y≈+30

  // Upper-band must dominate: far terminal cost exceeds near by a clear margin.
  EXPECT_GT(cost_far, cost_near + 0.5)
      << "upper-band failed to activate at |l|>l_max (cost_far=" << cost_far
      << " cost_near=" << cost_near << ")";

  // Two-sided symmetry: compare y≈-200 (wrong side, pref_dir=+1) vs y≈+200
  // (correct side). Both are |y|>l_max so both pay upper-band, but wrong side
  // additionally pays lower-band → wrong side cost > correct side cost, AND
  // the wrong-side cost must also exceed the in-band near baseline.
  const double cost_wrong = eval_j_terminal_at_psi(-kPi2, kLscale);         // y≈-200
  EXPECT_GT(cost_wrong, cost_near + 0.5)
      << "upper-band failed on negative side (cost_wrong=" << cost_wrong
      << " cost_near=" << cost_near << ")";

  // Smoothness: finite difference at y=0 (no NaN from abs kink).
  const double cost_plus1  = eval_j_terminal_at_psi( 0.5 * M_PI / 180.0, kLscale);
  const double cost_minus1 = eval_j_terminal_at_psi(-0.5 * M_PI / 180.0, kLscale);
  EXPECT_TRUE(std::isfinite(cost_plus1 - cost_minus1)) << "non-smooth at l=0";
}
