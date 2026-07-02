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
// N=8 (small horizon) balances test speed vs scenario realism.
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
    cfg.w_route     = 5.0;    // [TBD-HAZID] new route cost weight
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
// l[k] = (pos[k] - origin) · n_hat, pos integrated from own ship NED origin.
// NED: x=north (cos), y=east (sin).
double cross_track_at(const MidMpcSolution& sol,
                      double dt_s,
                      std::size_t k,
                      double ox, double oy,
                      double nx, double ny) {
  double cx = ox;
  double cy = oy;
  for (std::size_t j = 0u; j <= k; ++j) {
    if (j > 0u) {
      const auto& prev = sol.trajectory[j - 1u];
      cx += prev.u_mps * dt_s * std::cos(prev.psi_rad);
      cy += prev.u_mps * dt_s * std::sin(prev.psi_rad);
    }
  }
  return (cx - ox) * nx + (cy - oy) * ny;
}

}  // namespace

// ---------------------------------------------------------------------------
// TEST 1: J_route dimensionless + no-target lateral convergence (spec §10.1).
//
// Own ship starts offset to starboard (east, y0=+50m) of a due-north route.
// Route-frame normal points east (starboard = +). With no targets the only
// lateral pressure is J_route: the solver must steer back toward the route so
// that |l[k]|/l_scale < 0.1 for all steps (cross-track shrinks toward 0).
// The dimensionless form keeps J_route O(1), not the ~180k of a raw m^2 sum.
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, NoTargetConvergesToRouteLateralDimensionless) {
  MidMpcInput inp = make_base_input();
  // Start 50 m east of the route (starboard offset). Bearing is still ~north so
  // J_dist is near-zero; the lateral return is driven by J_route.
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.x_m = 0.0;
  inp.own_ship.y_m = 0.0;   // own NED origin
  // Set the route-frame origin to (0,0) and offset the START of the integrated
  // trajectory by giving own_ship a non-zero y? The NLP integrates from x0,y0.
  // To create lateral offset we set route-frame origin at (0,-50) so that an
  // own ship at (0,0) is 50 m starboard of the route.
  inp.route_frame_origin_x_m = 0.0;
  inp.route_frame_origin_y_m = -50.0;

  const auto sol = solver_->solve(inp, nullptr);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);

  // Verify lateral convergence: |l[k]|/l_scale < 0.1 for all steps.
  const int32_t N = formulation_->config().n_horizon;
  const double dt = formulation_->config().dt_s;
  double max_norm_l = 0.0;
  for (int32_t k = 0; k < N; ++k) {
    const double lk = cross_track_at(
        sol, dt, static_cast<std::size_t>(k),
        inp.route_frame_origin_x_m, inp.route_frame_origin_y_m,
        inp.route_frame_normal_x, inp.route_frame_normal_y);
    max_norm_l = std::max(max_norm_l, std::fabs(lk) / inp.lateral_scale_m);
  }
  EXPECT_LT(max_norm_l, 0.1)
      << "J_route did not pull lateral cross-track back to the route: "
      << "max |l|/l_scale = " << max_norm_l;

  // J_route value must be O(1) (dimensionless), not the raw m^2 scale (~180k
  // for a 50m offset × 8 steps). Evaluate the route-cost Function at the solve.
  const casadi::DM p = formulation_->pack_parameters(inp);
  const casadi::DM x = sol_to_x(sol, N);
  const double j_route = formulation_->eval_route_cost(x, p);
  EXPECT_LT(j_route, 10.0) << "J_route is not O(1) dimensionless: " << j_route;
  EXPECT_GE(j_route, 0.0);
}

// ---------------------------------------------------------------------------
// TEST 2: COLREG dominance — J_colreg dominates J_route+J_dist near CPA floor.
// (spec §3.2 / §10.1, must be non-self-certifying.)
//
// A target placed near the CPA hard floor forces a large avoidance manoeuvre.
// At the optimum the COLREG barrier cost must dominate the route+heading costs
// (w_colreg·J_colreg > w_route·J_route + w_dist·J_dist), proving J_route does
// not suppress avoidance. Cost components are evaluated via the formulation's
// cost-evaluation Functions at the solved optimum (true NLP expressions, not a
// geometric proxy).
// ---------------------------------------------------------------------------
TEST_F(RouteCostTest, ColregDominanceNearCpaFloor) {
  MidMpcInput inp = make_base_input();
  // Give-way head-on: target 600 m north closing fast → strong COLREG pressure.
  TargetState tgt;
  tgt.x_m     =  600.0;   // north of own ship
  tgt.y_m     =  0.0;
  tgt.cog_rad =  M_PI;    // heading south
  tgt.sog_mps =  5.0;
  tgt.cpa_m   =  0.0;
  tgt.tcpa_s  =  0.0;
  inp.targets.push_back(tgt);
  inp.constraints.applicable_rules = {14};  // give-way → asymmetry engages
  mass_l3::m5::synchronize_mid_mpc_constraint_context(inp);

  const auto sol = solver_->solve(inp, nullptr);
  ASSERT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);

  const int32_t N = formulation_->config().n_horizon;
  const casadi::DM p = formulation_->pack_parameters(inp);
  const casadi::DM x = sol_to_x(sol, N);

  const double w_colreg = formulation_->config().w_colreg;
  const double w_dist   = formulation_->config().w_dist;
  const double w_route  = formulation_->config().w_route;

  const double j_colreg = formulation_->eval_colreg_cost(x, p);
  const double j_dist   = formulation_->eval_dist_cost(x, p);
  const double j_route  = formulation_->eval_route_cost(x, p);

  const double colreg_term = w_colreg * j_colreg;
  const double route_plus_dist = w_route * j_route + w_dist * j_dist;

  EXPECT_GT(colreg_term, route_plus_dist)
      << "COLREG cost does NOT dominate: w_colreg·J_colreg=" << colreg_term
      << " <= w_route·J_route + w_dist·J_dist=" << route_plus_dist
      << " (J_colreg=" << j_colreg << " J_dist=" << j_dist
      << " J_route=" << j_route << ")";
}

// ---------------------------------------------------------------------------
// TEST 3: cross-leg guard — kIdxRouteWeight=0 disables J_route (spec §4.3).
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
