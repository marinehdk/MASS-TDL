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

TEST(MidMpcNlpFormulationTest, GDim_Matches5NMinus1) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon  = 6;
  cfg.max_targets = 4;
  MidMpcNlpFormulation formulation(cfg);
  formulation.build_symbolic_graph();
  // 5N - 1 = 5*6 - 1 = 29
  EXPECT_EQ(formulation.g_dim(), 29);
}
