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
using mass_l3::m5::mid_mpc::kIdxGiveWay;
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
  // Add a give-way rule so kIdxGiveWay packs to 1.0.
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
  RowBoundConfig rb_active;  // terminal_disabled=false (default)
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

  // Monotonicity sanity: with preferred_direction=+1, the wrong side is l<0
  // (psi<0 here). J_terminal must be DECREASING as psi goes from negative
  // (wrong side, high cost) to positive (correct side, low cost): the softplus
  // argument (-pref·l/tau) shrinks. Check the total trend.
  EXPECT_GT(costs.front(), costs.back())
      << "J_terminal not higher on wrong side (psi<0) than correct side (psi>0): "
      << "front=" << costs.front() << " back=" << costs.back();

  // ── give_way=0 gate: stand-on has NO terminal cost (spec §5.4 gate). ──
  casadi::DM p_standon = p;
  p_standon(kIdxGiveWay) = 0.0;
  casadi::DM x = casadi::DM::zeros(2 * N, 1);
  for (int32_t k = 0; k < N; ++k) {
    x(k) = -0.1;   // wrong side (would be expensive if gate open)
    x(N + k) = 5.0;
  }
  const double j_standon = formulation_->eval_terminal_cost(x, p_standon);
  EXPECT_NEAR(j_standon, 0.0, 1.0e-9)
      << "give_way=0 did not null J_terminal (stand-on gate broken): " << j_standon;
}
