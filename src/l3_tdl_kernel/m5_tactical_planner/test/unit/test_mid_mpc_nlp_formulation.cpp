// test/unit/test_mid_mpc_nlp_formulation.cpp
// Task 2.1 stub tests: verify the symbolic graph builds, the solver Function
// is non-null, and pack_parameters() returns a vector of the expected
// dimension. Full integration tests (StraightLineNoTargets,
// HeadOnGiveWayRightTurn, etc.) belong to Task 2.2 which also implements
// MidMpcSolver.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md
// §10 (dynamic-link boundary).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::kParamDim;

TEST(MidMpcNlpFormulationTest, BuildSymbolicGraph_NoThrow) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon  = 4;   // small for speed
  cfg.dt_s       = 5.0;
  cfg.max_targets = 4;  // small for graph-build time
  MidMpcNlpFormulation formulation(cfg);
  EXPECT_NO_THROW(formulation.build_symbolic_graph());
}

TEST(MidMpcNlpFormulationTest, SolverValid_AfterBuild) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon  = 4;
  cfg.max_targets = 4;
  MidMpcNlpFormulation formulation(cfg);
  formulation.build_symbolic_graph();
  EXPECT_FALSE(formulation.solver().is_null());
}

TEST(MidMpcNlpFormulationTest, PackParameters_CorrectDim) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon  = 4;
  cfg.max_targets = 16;
  MidMpcNlpFormulation formulation(cfg);
  formulation.build_symbolic_graph();

  MidMpcInput input{};
  input.own_ship.psi_rad           = 0.1;
  input.own_ship.u_mps             = 5.0;
  input.planned_route_bearing_rad  = 0.0;
  input.planned_speed_mps          = 5.0;
  input.constraints.heading_min_rad = -M_PI;
  input.constraints.heading_max_rad =  M_PI;
  input.constraints.speed_min_mps   = 0.0;
  input.constraints.speed_max_mps   = 15.0;

  const casadi::DM p = formulation.pack_parameters(input);
  EXPECT_EQ(static_cast<int32_t>(p.size1()), kParamDim);
  EXPECT_EQ(static_cast<int32_t>(p.size2()), 1);
}

TEST(MidMpcNlpFormulationTest, GDim_MatchesTwoNMinus1_RotOnly) {
  // g holds only the ROT differential rows — two smooth linear bounds (upper +
  // lower) per step = 2*(N-1). Heading/speed box limits moved to IPOPT variable
  // bounds (lbx/ubx) for restoration robustness.
  for (const int32_t n : {2, 6, 12}) {
    MidMpcNlpFormulation::Config cfg;
    cfg.n_horizon   = n;
    cfg.max_targets = 4;
    MidMpcNlpFormulation formulation(cfg);
    formulation.build_symbolic_graph();
    EXPECT_EQ(formulation.g_dim(), 2 * (n - 1))
        << "g_dim mismatch for N=" << n;
  }
}

TEST(MidMpcNlpFormulationTest, PackGiveWayFlag_FromApplicableRules) {
  using mass_l3::m5::mid_mpc::kIdxGiveWay;
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 4;
  cfg.max_targets = 4;
  MidMpcNlpFormulation formulation(cfg);
  formulation.build_symbolic_graph();

  MidMpcInput inp{};
  inp.constraints.heading_min_rad = -M_PI;
  inp.constraints.heading_max_rad = M_PI;
  inp.constraints.speed_min_mps = 0.0;
  inp.constraints.speed_max_mps = 15.0;

  inp.constraints.applicable_rules = {17};  // stand-on → no give-way
  EXPECT_DOUBLE_EQ(
      static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 0.0);

  inp.constraints.applicable_rules = {14};  // head-on give-way
  EXPECT_DOUBLE_EQ(
      static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 1.0);

  inp.constraints.applicable_rules = {15};  // crossing give-way
  EXPECT_DOUBLE_EQ(
      static_cast<double>(formulation.pack_parameters(inp)(kIdxGiveWay)), 1.0);
}

TEST(MidMpcNlpFormulationTest, GDimIncludesCpaHardConstraintRows) {
  constexpr int32_t kHorizon = 4;
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = kHorizon;
  cfg.max_targets = 1;

  mass_l3::m5::ConstraintInputs constraints;
  constraints.cpa_safe_m = 1852.0;
  mass_l3::m5::TargetState target;
  target.x_m = 1000.0;
  target.y_m = 0.0;
  target.cog_rad = 0.0;
  target.sog_mps = 0.0;
  constraints.targets.push_back(target);

  MidMpcNlpFormulation formulation(cfg);
  formulation.set_constraint_inputs(constraints);
  formulation.build_symbolic_graph();

  EXPECT_EQ(formulation.g_dim(), 2 * (kHorizon - 1) + kHorizon)
      << "one target must add one CPA hard-constraint row per horizon step";
}

TEST(MidMpcNlpFormulationTest, RuntimeConstraintContextFeedsCompilerRows) {
  constexpr int32_t kHorizon = 4;
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = kHorizon;
  cfg.max_targets = 1;

  MidMpcInput input{};
  input.own_ship.psi_rad = 0.2;
  input.constraints.cpa_safe_m = 1852.0;
  mass_l3::m5::TargetState target;
  target.x_m = 1000.0;
  target.y_m = 0.0;
  target.cog_rad = 0.0;
  target.sog_mps = 0.0;
  input.targets.push_back(target);

  mass_l3::m5::synchronize_mid_mpc_constraint_context(input);

  EXPECT_EQ(input.constraints.targets.size(), input.targets.size());
  EXPECT_DOUBLE_EQ(input.constraints.own_ship_psi_rad, input.own_ship.psi_rad);

  MidMpcNlpFormulation formulation(cfg);
  formulation.set_constraint_inputs(input.constraints);
  formulation.build_symbolic_graph();

  EXPECT_EQ(formulation.g_dim(), 2 * (kHorizon - 1) + kHorizon)
      << "runtime targets must reach ConstraintCompiler CPA hard rows";
}

TEST(MidMpcNlpFormulationTest, HorizonSecondsOverridesDefaultStepCount) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 18;
  cfg.dt_s = 5.0;

  const auto resolved = mass_l3::m5::mid_mpc::resolve_mid_mpc_horizon_config(
      cfg,
      60.0,
      cfg.n_horizon,
      5.0);

  EXPECT_EQ(resolved.n_horizon, 12);
  EXPECT_DOUBLE_EQ(resolved.dt_s, 5.0);
}
