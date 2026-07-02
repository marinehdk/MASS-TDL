// test/unit/test_mid_mpc_direction.cpp
// Slice D1: COLREG direction + min_alt internalization (spec §7.1 / §3.3 / §10.1).
//
// Verifies:
//   1. give-way + pref_dir=+1 (stbd): direction row g_dir[k] = pref_dir·l[k] ≥ 0
//      holds on the suffix (lateral on the M6-required side), and the solve
//      converges (the hard row is feasible + active as a constraint).
//   2. give-way + pref_dir=+1: min_alt row g_minalt[k] = pref_dir·(psi[k]-own_psi)
//      - min_alt ≥ 0 holds (heading alteration meets the COLREG minimum).
//   3. preferred_direction==0 → direction/min_alt rows disabled (bounds [-inf,+inf]).
//   4. stand-on role → direction/min_alt rows disabled.
//   5. HOLD/ReduceSpeed → direction/min_alt rows disabled.
//
// Tests 1-2 evaluate the solved trajectory's constraint rows numerically.
// Tests 3-5 verify the RowRegistry bound switch + solver auto-disable convergence.
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
using mass_l3::m5::mid_mpc::RowRegistry;

// ---------------------------------------------------------------------------
// Fixture — builds the direction-enabled NLP once; reused by all tests.
// N=8 (not the spec default N=18) keeps the unit-test suite fast (each solve is
// a real IPOPT NLP).
// ---------------------------------------------------------------------------
class DirectionConstraintTest : public ::testing::Test {
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
// With normal = (-sin(0), cos(0)) = (0, 1) = east, l[k] = (pos-orig)·(0,1) = y_m[k]
// (the east / starboard offset). So pref_dir·l[k] ≥ 0 ⟺ l[k] ≥ 0 ⟺ on stbd side.
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

// Per-step cross-track l[k] = (pos[k]-origin)·n_hat of a solved trajectory.
// pos[k] = x0 + Σ_{j<k} u[j]·dt·(cos,sin)(psi[j]) (spec §3.1).
std::vector<double> cross_track_all(const MidMpcSolution& sol, double dt_s,
                                    int32_t N,
                                    double own_x0, double own_y0,
                                    double ox, double oy,
                                    double nx, double ny) {
  std::vector<double> l(static_cast<std::size_t>(N), 0.0);
  double cx = own_x0;
  double cy = own_y0;
  for (int32_t k = 0; k < N; ++k) {
    l[static_cast<std::size_t>(k)] = (cx - ox) * nx + (cy - oy) * ny;
    if (k < static_cast<int32_t>(sol.trajectory.size())) {
      const auto& pt = sol.trajectory[static_cast<std::size_t>(k)];
      cx += pt.u_mps * dt_s * std::cos(pt.psi_rad);
      cy += pt.u_mps * dt_s * std::sin(pt.psi_rad);
    }
  }
  return l;
}

}  // namespace

// ===========================================================================
// TEST 1: give-way + pref_dir=+1 (stbd) → direction row satisfied on suffix.
// (spec §7.1 / §10.1: g_dir[k] = preferred_direction·l[k] ≥ 0, k ∈ [K,N).)
//
// K=0 (no prefix) so the ENTIRE horizon is the suffix. With pref_dir=+1 and
// normal=east, g_dir[k] = l[k] ≥ 0 must hold at the solved optimum.
//
// To make the direction row NON-trivial we first establish that the
// UNCONSTRAINED cost optimum (direction rows disabled) places the lateral on
// the PORT side, then verify enabling the rows forces it to STBD:
//   - route bearing = own_psi = -0.30 rad (PORT), route_weight=0 (J_route OFF),
//     rules={13} (J_asym OFF) → J_dist pulls ψ→−0.3 (PORT). Solving with
//     direction_disabled=true (rows inert) yields l[k] < 0 at the terminal step.
//   - solving with direction rows ACTIVE then forces l[k] ≥ 0 on the suffix.
// This compares disabled-vs-enabled on the SAME fixture, so the GREEN assertion
// (l[k] ≥ 0 with rows active) is meaningful: it is the rows, not the cost, that
// put the lateral on the stbd side. l[0]=0 (own on route) is marginal-feasible
// (0 ≥ 0) so the k=0 row does not force infeasibility.
// ===========================================================================
TEST_F(DirectionConstraintTest, GiveWayDirectionRowSatisfiedOnSuffixStbd) {
  auto build_port_fixture = []() {
    MidMpcInput inp;
    inp.own_ship.psi_rad = -0.30;  // PORT seed
    inp.own_ship.u_mps   = 5.0;
    inp.own_ship.x_m     = 0.0;
    inp.own_ship.y_m     = 0.0;    // on the route (origin = own → l[0]=0)
    inp.planned_route_bearing_rad = -0.30;  // J_dist pulls ψ→−0.3 (PORT)
    inp.planned_speed_mps         = 5.0;
    inp.constraints.heading_min_rad = -M_PI;
    inp.constraints.heading_max_rad =  M_PI;
    inp.constraints.speed_min_mps   = 0.0;
    inp.constraints.speed_max_mps   = 15.0;
    inp.constraints.cpa_safe_m      = 1852.0;
    inp.constraints.own_ship_psi_rad = -0.30;
    inp.route_frame_origin_x_m = 0.0;
    inp.route_frame_origin_y_m = 0.0;
    inp.route_frame_normal_x = -std::sin(0.0);   // = 0
    inp.route_frame_normal_y =  std::cos(0.0);   // = 1 (east, stbd +)
    inp.route_frame_active_leg_bearing_rad = 0.0;
    inp.lateral_scale_m = kLateralScaleM;
    inp.route_weight = 0.0;                   // J_route OFF
    inp.colregs_primary_role = 1U;            // GIVE_WAY
    inp.colregs_preferred_direction =
        mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (stbd)
    inp.constraints.applicable_rules = {13};  // NOT 14/15 → J_asym OFF
    mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);
    return inp;
  };

  const int32_t N = formulation_->config().n_horizon;
  const double dt = formulation_->config().dt_s;

  // (a) UNCONSTRAINED: direction + terminal rows disabled → cost optimum drifts
  // PORT. This proves the fixture's cost is PORT-preferring, so the stbd result
  // in (b) is due to the direction hard rows, not the cost or the T1 terminal
  // rows (which also force stbd at k=N-1 for a give-way + pref_dir=+1 input).
  {
    MidMpcInput inp = build_port_fixture();
    RowBoundConfig rb_disabled;
    rb_disabled.direction_disabled = true;  // direction/min_alt rows inert
    rb_disabled.terminal_disabled = true;   // T1 terminal rows inert too
    const auto sol = solver_->solve(inp, nullptr, rb_disabled);
    ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
    const std::vector<double> l = cross_track_all(
        sol, dt, N, inp.own_ship.x_m, inp.own_ship.y_m,
        inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
        inp.route_frame_normal_x, inp.route_frame_normal_y);
    // Terminal step: cost optimum (port bearing) → l[N-1] < 0 (port side).
    EXPECT_LT(l[static_cast<std::size_t>(N - 1)], -1.0)
        << "fixture sanity: disabled-rows solve should drift PORT (l[N-1]="
        << l[static_cast<std::size_t>(N - 1)] << " >= -1); if not, the stbd "
        << "assertion below is trivially satisfied by the cost, not the rows";
  }

  // (b) CONSTRAINED: direction rows ACTIVE → suffix forced to STBD (l[k] ≥ 0).
  // (terminal rows left disabled so ONLY the direction rows supply the stbd
  // force — isolating D1 from T1.)
  MidMpcInput inp = build_port_fixture();
  RowBoundConfig rb;
  rb.terminal_disabled = true;  // isolate D1 from T1: only direction rows active
  const auto sol = solver_->solve(inp, nullptr, rb);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "give-way + pref_dir=+1 solve did not converge (direction rows broken)";

  const std::vector<double> l = cross_track_all(
      sol, dt, N,
      inp.own_ship.x_m, inp.own_ship.y_m,
      inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
      inp.route_frame_normal_x, inp.route_frame_normal_y);

  // g_dir[k] = pref_dir·l[k] ≥ 0; pref_dir=+1 → l[k] ≥ 0 for all suffix steps.
  // K=0 so the entire horizon [0,N) is suffix. Tolerance for IPOPT feasibility.
  for (int32_t k = 0; k < N; ++k) {
    EXPECT_GE(l[static_cast<std::size_t>(k)], -1.0e-3)
        << "g_dir violated at step k=" << k << ": l[k]=" << l[static_cast<std::size_t>(k)]
        << " < 0 (lateral on port side, not the M6-required starboard side)";
  }
}

// ===========================================================================
// TEST 2: give-way + pref_dir=+1 → min_alt row satisfied on suffix.
// (spec §7.1: g_minalt[k] = preferred_direction·(psi[k]-own_psi) ≥ min_alt.)
//
// own_psi is the own-ship CURRENT heading. With pref_dir=+1 (stbd), g_minalt ≥
// min_alt means psi[k]-own_psi ≥ min_alt, i.e. the suffix heading must alter to
// starboard by at least min_alt rad.
//
// TDD RED design: route bearing = own_psi = 0, so J_dist's minimum is ψ=0
// (NO alteration). With route_weight=0 (J_route off) and rules={13} (J_asym
// off), the cost optimum is ψ≈0 with NO starboard alteration. A zero-placeholder
// min_alt row passes trivially → ψ[k]-own_psi ≈ 0 < min_alt ⇒ assertion FAILS
// (RED). After D1 the hard row pref_dir·(ψ-own_psi) ≥ min_alt forces ψ[k] ≥
// min_alt (GREEN) — the heading pays a J_dist penalty but the hard constraint
// wins.
// ===========================================================================
TEST_F(DirectionConstraintTest, GiveWayMinAltRowSatisfiedOnSuffix) {
  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 1U;  // GIVE_WAY
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (stbd)
  inp.colregs_min_alteration_rad = 0.05;  // 2.9° minimum alteration
  // Cost optimum = no alteration (ψ=0=bearing=own_psi); min_alt row is the only
  // force requiring a starboard turn.
  inp.planned_route_bearing_rad = 0.0;
  inp.own_ship.psi_rad = 0.0;
  inp.constraints.own_ship_psi_rad = 0.0;
  inp.route_weight = 0.0;                   // J_route OFF
  inp.constraints.applicable_rules = {13};  // NOT 14/15 → J_asym OFF
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  RowBoundConfig rb;
  const auto sol = solver_->solve(inp, nullptr, rb);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "give-way + min_alt solve did not converge";

  const int32_t N = formulation_->config().n_horizon;
  const double min_alt = inp.colregs_min_alteration_rad;
  const double own_psi = inp.constraints.own_ship_psi_rad;
  // g_minalt[k] = pref_dir·(psi[k]-own_psi) - min_alt ≥ 0; pref_dir=+1.
  for (int32_t k = 0; k < N; ++k) {
    ASSERT_LT(static_cast<std::size_t>(k), sol.trajectory.size());
    const double psi_k = sol.trajectory[static_cast<std::size_t>(k)].psi_rad;
    const double g_minalt = 1.0 * (psi_k - own_psi) - min_alt;
    EXPECT_GE(g_minalt, -5.0e-3)
        << "g_minalt violated at step k=" << k << ": psi[k]-own_psi="
        << (psi_k - own_psi) << " < min_alt=" << min_alt
        << " (heading alteration below COLREG minimum)";
  }
}

// ===========================================================================
// TEST 3: preferred_direction==0 → direction/min_alt rows DISABLED.
// (spec §3.3: "preferred_direction==0 时 direction/min_alt 行禁用".)
//
// We verify the RowRegistry bound switch directly: direction_disabled=true →
// ALL direction + min_alt rows become [-inf,+inf] (no constraint). We also check
// the converse (active) is [0,+inf].
// ===========================================================================
TEST_F(DirectionConstraintTest, DirectionDisabledWhenPreferredDirZero) {
  RowRegistry reg(/*N=*/8, /*n_targets=*/0, /*n_rule_rows=*/0,
                  /*n_zone_rows=*/0);

  // direction_disabled=true → all direction + min_alt rows [-inf,+inf].
  RowBoundConfig rb;
  rb.direction_disabled = true;
  const auto b = reg.build_bounds(rb);
  constexpr double kInf = std::numeric_limits<double>::infinity();
  for (int k = 0; k < 8; ++k) {
    const int rd = reg.direction_row(k);
    const int rm = reg.min_alt_row(k);
    EXPECT_EQ(b.lbg[static_cast<std::size_t>(rd)], -kInf)
        << "direction row " << k << " lb not disabled (-inf) when pref_dir=0";
    EXPECT_EQ(b.ubg[static_cast<std::size_t>(rd)], kInf)
        << "direction row " << k << " ub not disabled (+inf) when pref_dir=0";
    EXPECT_EQ(b.lbg[static_cast<std::size_t>(rm)], -kInf)
        << "min_alt row " << k << " lb not disabled (-inf) when pref_dir=0";
    EXPECT_EQ(b.ubg[static_cast<std::size_t>(rm)], kInf)
        << "min_alt row " << k << " ub not disabled (+inf) when pref_dir=0";
  }

  // Conversely, direction_disabled=false → direction/min_alt rows stay [0,+inf].
  RowBoundConfig rb_active;
  const auto b2 = reg.build_bounds(rb_active);
  for (int k = 0; k < 8; ++k) {
    const int rd = reg.direction_row(k);
    const int rm = reg.min_alt_row(k);
    EXPECT_EQ(b2.lbg[static_cast<std::size_t>(rd)], 0.0)
        << "direction row " << k << " lb not active (0) for give-way";
    EXPECT_EQ(b2.ubg[static_cast<std::size_t>(rd)], kInf)
        << "direction row " << k << " ub not active (+inf) for give-way";
    EXPECT_EQ(b2.lbg[static_cast<std::size_t>(rm)], 0.0)
        << "min_alt row " << k << " lb not active (0) for give-way";
    EXPECT_EQ(b2.ubg[static_cast<std::size_t>(rm)], kInf)
        << "min_alt row " << k << " ub not active (+inf) for give-way";
  }
}

// ===========================================================================
// TEST 4: stand-on role → solver auto-disables direction/min_alt rows.
// (spec §3.3: "STAND_ON 时 direction/min_alt 行禁用".)
//
// The solver derives direction_disabled from the input (same activation logic as
// terminal_disabled, §3.3). A stand-on input with pref_dir=+1 would otherwise
// be INFEASIBLE: g_dir[k]=pref_dir·l[k] with l[0]=0 (own on route) gives 0≥0
// (ok), but g_minalt[k]=pref_dir·(psi[k]-own_psi)-min_alt with psi=own_psi gives
// -min_alt < 0 ⇒ infeasible. The auto-disable makes the stand-on solve converge.
// ===========================================================================
TEST_F(DirectionConstraintTest, StandOnRoleAutoDisablesDirectionViaSolver) {
  MidMpcInput inp = make_base_input();
  inp.colregs_primary_role = 0U;  // STAND_ON
  inp.colregs_preferred_direction =
      mass_l3::m5::ColregsPreferredDirection::Starboard;  // +1 (but stand-on)
  inp.colregs_min_alteration_rad = 0.05;  // would force alteration if active
  inp.constraints.own_ship_psi_rad = 0.0;
  inp.constraints.applicable_rules = {17};  // Rule 17 = stand-on duty
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  // Default RowBoundConfig{} — the solver MUST auto-disable direction rows.
  RowBoundConfig rb;  // direction_disabled=false (default) → solver derives it
  const auto sol = solver_->solve(inp, nullptr, rb);

  // If direction rows were NOT auto-disabled, g_minalt[k] = 1·(0-0)-0.05 = -0.05 < 0
  // ⇒ Infeasible (own heading = own_psi = 0, no avoidance driver to alter). The
  // auto-disable makes it converge.
  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
      << "stand-on solve did not converge — direction rows were not auto-disabled";
}

// ===========================================================================
// TEST 5: HOLD / ReduceSpeed → solver auto-disables direction/min_alt rows.
// (spec §3.3: "HOLD/ReduceSpeed 时 direction/min_alt 行禁用".)
//
// HOLD/ReduceSpeed are non-lateral-alteration behaviors; the direction/min_alt
// rows are disabled. A give-way-role + HOLD input with min_alt>0 would otherwise
// be infeasible (own heading unaltered). The auto-disable makes it converge.
// ===========================================================================
TEST_F(DirectionConstraintTest, HoldReduceSpeedAutoDisablesDirection) {
  for (const auto beh : {mass_l3::m5::ColregsPreferredDirection::Hold,
                         mass_l3::m5::ColregsPreferredDirection::ReduceSpeed}) {
    MidMpcInput inp = make_base_input();
    inp.colregs_primary_role = 1U;  // GIVE_WAY
    inp.colregs_preferred_direction = beh;  // Hold or ReduceSpeed
    inp.colregs_min_alteration_rad = 0.05;
    inp.constraints.own_ship_psi_rad = 0.0;
    inp.constraints.applicable_rules = {14};
    mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

    RowBoundConfig rb;  // default — solver must auto-disable direction rows
    const auto sol = solver_->solve(inp, nullptr, rb);
    EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged)
        << "give-way + HOLD/ReduceSpeed solve did not converge "
        << "(direction not auto-disabled for behavior="
        << static_cast<int>(beh) << ")";
  }
}
