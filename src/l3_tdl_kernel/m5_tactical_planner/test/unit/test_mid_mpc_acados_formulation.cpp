// test/unit/test_mid_mpc_acados_formulation.cpp
// Task 15 (P1b-1b): MidMpcAcadosFormulation — production acados OCP CasADi MX
// symbol-graph dimension / parameter-partition assertions.
//
// This test verifies the SYMBOL-GRAPH CONTRACT of the production acados
// formulation (Path B 5-dim state, 2-dim control, 142-param IPOPT-equivalent
// global/per-stage partition), NOT a real acados solve (that is Task 16+).
// It mirrors the IPOPT MidMpcNlpFormulation kParamDim==142 contract but on
// the acados MX graph (state x=[px,py,psi,r,u_surge], control u=[delta,n]).
//
// The IPOPT formulation (mid_mpc_nlp_formulation.{hpp,cpp}) is READ-ONLY
// reference; this test does NOT touch it. acados codegen (gen_mid_mpc_acados.py)
// is verified separately in the container.
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md
// §10 (dynamic-link boundary).

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;

// ---------------------------------------------------------------------------
// Fixture: default config + built graph. Mirror of the brief's Step 2 test
// (build_symbolic_graph called once in SetUp).
// ---------------------------------------------------------------------------
class AcadosFormulationTest : public ::testing::Test {
 protected:
  MidMpcAcadosFormulation form_;
  void SetUp() override { form_.build_symbolic_graph(); }
};

// Dimension match (Path B 5-dim state, 2-dim control) — spec amendment
// 2026-07-17: state x=[px,py,psi,r,u_surge] (nx=5), control u=[delta,n] (nu=2).
TEST_F(AcadosFormulationTest, StateControlDims_MatchPathB) {
  EXPECT_EQ(form_.nx(), 5);  // [px,py,psi,r,u_surge]
  EXPECT_EQ(form_.nu(), 2);  // [delta,n]
}

// Parameter partition: global + per-stage total == IPOPT kParamDim == 142.
// The production acados formulation must preserve the IPOPT 142-param contract
// (kParamDim==142 in mid_mpc_nlp_formulation.hpp:78) so the solver's
// pack_parameters produces a vector the acados backend can consume with the
// SAME semantic layout. The split is global(stage-uniform) + per-stage(prefix):
//   global = 26 IPOPT head scalars (kIdx 0-25) + 16x5 target block (kIdx 62-141) = 106
//   per-stage = prefix psi[N] + prefix u[N] (kIdx 26-61) = 2*N = 36 (N=18 default)
// Sum = 142 (matches IPOPT). Per-stage values are set via the generated
// <name>_acados_update_params; the global block via ocp_nlp_in_set / set_p.
TEST_F(AcadosFormulationTest, ParamDims_SumTo142) {
  MidMpcInput in{};
  std::pair<std::vector<double>, std::vector<std::vector<double>>> r;
  EXPECT_NO_THROW({ r = form_.pack_parameters(in); });
  const auto& g = r.first;
  const auto& ps = r.second;
  ASSERT_FALSE(ps.empty());
  EXPECT_EQ(static_cast<int>(g.size()) + static_cast<int>(ps.front().size()), 142)
      << "global + per-stage must total IPOPT kParamDim=142";
  // Every per-stage vector has the SAME length (stage-uniform param layout).
  for (const auto& s : ps) {
    EXPECT_EQ(s.size(), ps.front().size())
        << "per-stage param vectors must be stage-uniform length";
  }
}

// Yaw gain c_u is the VDM-direct value (P1b-1a T8 finding), not an invented
// coefficient. Verified analytically == k_n_rudder * u^2 / izz_e at cruise.
TEST_F(AcadosFormulationTest, YawGain_IsVdmDirect) {
  EXPECT_NEAR(MidMpcAcadosFormulation::kC_u, 9.825342e-3, 1e-9);
}

// VDM-direct surge model coefficients (vessel_dynamics_model.cpp:47-48).
TEST_F(AcadosFormulationTest, SurgeModelCoeffs_IsVdmDirect) {
  EXPECT_NEAR(MidMpcAcadosFormulation::kKProp, 500.0, 1e-9);
  EXPECT_NEAR(MidMpcAcadosFormulation::kKDrag, 100.0, 1e-9);
}

// build_symbolic_graph does not throw (basic graph-build usability).
TEST_F(AcadosFormulationTest, BuildSymbolicGraph_NoThrow) {
  EXPECT_NO_THROW({ form_.build_symbolic_graph(); });
}

// pack_parameters does not throw on a default-constructed MidMpcInput.
TEST_F(AcadosFormulationTest, PackParameters_NoThrow) {
  MidMpcInput in{};
  EXPECT_NO_THROW({ [[maybe_unused]] auto r = form_.pack_parameters(in); });
}

// Discrete dynamics expression is non-null and maps nx -> nx (Path B 5-dim):
// disc_dyn_expr(x, u, p) must be a 5-vector. This is the symbol-graph evidence
// that the surge state + rudder/rpm control channel is wired. nh matches the
// gen script (gen_mid_mpc_acados.py) row count: Nt CPA + 1 direction + 1
// min_alt + 3 terminal = 16+1+1+3 = 21 at default Nt=16.
TEST_F(AcadosFormulationTest, DiscDynExpr_NonNullFiveRows) {
  EXPECT_FALSE(form_.disc_dyn_expr().is_null());
  EXPECT_EQ(form_.disc_dyn_expr().size1(), 5);   // Path B 5-dim dynamics
  EXPECT_FALSE(form_.con_h_expr().is_null());
  EXPECT_EQ(form_.nh(), 21);                      // 16+1+1+3 (matches gen script)
  EXPECT_EQ(form_.np_global(), 106);              // 26 head + 80 target block
  EXPECT_EQ(form_.np_per_stage(), 36);            // 2*N prefix (N=18)
}

// Default horizon N: production default (horizon_s=90s / dt=5s -> N=18).
TEST_F(AcadosFormulationTest, DefaultHorizon_IsProductionDefault) {
  EXPECT_EQ(MidMpcAcadosFormulation::kNDefault, 18);
  EXPECT_NEAR(MidMpcAcadosFormulation::kDt, 5.0, 1e-9);
}
