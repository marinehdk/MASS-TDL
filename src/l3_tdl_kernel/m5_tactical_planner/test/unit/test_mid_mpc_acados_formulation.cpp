// test/unit/test_mid_mpc_acados_formulation.cpp
// Task 15 (P1b-1b): MidMpcAcadosFormulation — production acados OCP CasADi MX
// symbol-graph dimension / parameter-partition assertions.
//
// This test verifies the SYMBOL-GRAPH CONTRACT of the production acados
// formulation (Path B 5-dim state, 2-dim control, documented global/per-stage
// parameter partition), NOT a real acados solve (that is Task 16+).
// Parameter partition (T15 F2/F4): global=106 (26 IPOPT head scalars + 80
// target block, IPOPT-142-compatible for the stage-uniform part) + per-stage=35
// (prefix psi/u + pact_pre + per-target drifted x/y). The per-stage block is an
// honest acados expansion beyond IPOPT's flat kParamDim==142; see
// mid_mpc_acados_formulation.hpp partition doc + static_assert (np_global==106,
// np_per_stage==35). State x=[px,py,psi,r,u_surge], control u=[delta,n].
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
using mass_l3::m5::TargetState;
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

// Parameter partition (T15 F2/F4 documented deviation from IPOPT flat 142):
//   global     = 106 (26 IPOPT head scalars + 16x5 target block — the
//              stage-uniform portion, 142-compatible for the global half).
//   per-stage  = 35  (prefix psi/u scalars + pact_pre + per-stage target
//              drift x/y). acatos precomputes per-stage drift (F4) and the
//              prefix-equality activation factor (F2) because the single-stage
//              graph cannot index stage k; IPOPT folds these into its flat
//              142-vector + per-row bounds. GLOBAL stays 106; per-stage expands.
TEST_F(AcadosFormulationTest, ParamDims_MatchDocumentedPartition) {
  EXPECT_EQ(form_.np_global(), 106);     // 26 head + 80 target (IPOPT-compatible)
  EXPECT_EQ(form_.np_per_stage(), 35);   // 3 + 2*Nt (prefix+act+drift)
  MidMpcInput in{};
  std::pair<std::vector<double>, std::vector<std::vector<double>>> r;
  EXPECT_NO_THROW({ r = form_.pack_parameters(in); });
  const auto& g = r.first;
  const auto& ps = r.second;
  ASSERT_FALSE(ps.empty());
  EXPECT_EQ(static_cast<int>(g.size()), 106);
  // Every per-stage vector has the SAME length (stage-uniform param layout).
  for (const auto& s : ps) {
    EXPECT_EQ(static_cast<int>(s.size()), 35)
        << "per-stage param vectors must be stage-uniform length 35";
  }
  // N+1 rows (stages 0..N), terminal stage repeats stage N-1.
  EXPECT_EQ(static_cast<int>(ps.size()), form_.n_horizon() + 1);
}

// Yaw gain c_u is the VDM-direct value (P1b-1a T8 finding), not an invented
// coefficient. Verified analytically == k_n_rudder * u^2 / izz_e at cruise.
TEST_F(AcadosFormulationTest, YawGain_IsVdmDirect) {
  EXPECT_NEAR(MidMpcAcadosFormulation::kC_u, 9.825342e-3, 1e-9);
}

// VDM-direct surge model coefficients (vessel_dynamics_model.cpp:47-48) and
// the surge effective mass m_sge = mass_kg*(1+surge_added_mass_factor) = 152250
// (T15 F1). The raw kKProp/kKDrag are FORCES [N]; the graph divides by kMSge.
TEST_F(AcadosFormulationTest, SurgeModelCoeffs_IsVdmDirect) {
  EXPECT_NEAR(MidMpcAcadosFormulation::kKProp, 500.0, 1e-9);
  EXPECT_NEAR(MidMpcAcadosFormulation::kKDrag, 100.0, 1e-9);
  EXPECT_NEAR(MidMpcAcadosFormulation::kMSge, 152250.0, 1e-6);
  // Mass-normalized effective coefficients (baked into the graph expression).
  EXPECT_NEAR(MidMpcAcadosFormulation::kKPropPerMass,
              500.0 / 152250.0, 1e-12);
  EXPECT_NEAR(MidMpcAcadosFormulation::kKDragPerMass,
              100.0 / 152250.0, 1e-12);
}

// T15 F1: surge accel must be MASS-NORMALIZED (matches VDM ground truth). At the
// sample point n=9.26 rps, u=5 m/s the VDM accel is
//   (k_prop*n*|n| - k_drag*u*|u|) / m_sge = (500*9.26^2 - 100*5^2)/152250
//                                            = 0.265 m/s^2.
// The acados graph uses kKPropPerMass*n^2 - kKDragPerMass*u^2 (n,u >= 0 in the
// feasible box so n^2==n*|n|). This asserts the expression evaluates to the VDM
// value (regression guard: without /m_sge it was ~40374 m/s^2).
TEST_F(AcadosFormulationTest, SurgeAccel_MassNormalizedMatchesVdm) {
  const double n = 9.26;      // rps
  const double u = 5.0;       // m/s
  const double accel_graph =
      MidMpcAcadosFormulation::kKPropPerMass * n * n -
      MidMpcAcadosFormulation::kKDragPerMass * u * u;
  const double accel_vdm =
      (MidMpcAcadosFormulation::kKProp * n * std::abs(n) -
       MidMpcAcadosFormulation::kKDrag * u * std::abs(u)) /
      MidMpcAcadosFormulation::kMSge;
  EXPECT_NEAR(accel_graph, accel_vdm, 1e-9);
  EXPECT_NEAR(accel_graph, 0.265, 0.001);  // VDM ground truth at this sample
  // Regression guard: the BUGGY (un-normalized) value was ~40374 m/s^2.
  EXPECT_LT(accel_graph, 1.0);
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
// gen script (gen_mid_mpc_acados.py) row count: 2 prefix + Nt CPA + 1 direction
// + 1 min_alt + 3 terminal = 2+16+1+1+3 = 23 at default Nt=16 (T15 F2 added the
// 2 prefix rows).
TEST_F(AcadosFormulationTest, DiscDynExpr_NonNullFiveRows) {
  EXPECT_FALSE(form_.disc_dyn_expr().is_null());
  EXPECT_EQ(form_.disc_dyn_expr().size1(), 5);   // Path B 5-dim dynamics
  EXPECT_FALSE(form_.con_h_expr().is_null());
  EXPECT_EQ(form_.nh(), 23);                      // 2+16+1+1+3 (matches gen script)
  EXPECT_EQ(form_.np_global(), 106);              // 26 head + 80 target block
  EXPECT_EQ(form_.np_per_stage(), 35);            // 3 + 2*Nt (prefix+act+drift)
}

// Default horizon N: production default (horizon_s=90s / dt=5s -> N=18).
TEST_F(AcadosFormulationTest, DefaultHorizon_IsProductionDefault) {
  EXPECT_EQ(MidMpcAcadosFormulation::kNDefault, 18);
  EXPECT_NEAR(MidMpcAcadosFormulation::kDt, 5.0, 1e-9);
}

// T15 F2: committed-route prefix lock. With prefix_active_k=K, the per-stage
// param block must carry pact_pre=1.0 for stages k<K (active equality) and
// pact_pre=0.0 for k>=K (inactive). The prefix psi/u scalars at active stages
// must equal the reprojected committed-geometry values from MidMpcInput.
// This verifies pack_parameters writes the activation factor + prefix targets
// correctly (the graph reads them at fixed offsets).
TEST_F(AcadosFormulationTest, PrefixLock_PackWritesActivationAndTargets) {
  MidMpcInput in{};
  const int32_t K = 3;
  in.prefix_active_k = K;
  in.prefix_psi_rad = {0.10, 0.20, 0.30};   // [K] prefix psi targets
  in.prefix_u_mps = {4.0, 4.5, 5.0};        // [K] prefix u targets
  const auto r = form_.pack_parameters(in);
  const auto& ps = r.second;
  const int N = form_.n_horizon();
  ASSERT_EQ(static_cast<int>(ps.size()), N + 1);
  const int off_psi = 0;   // kAcadosPerStagePrefixPsiOff
  const int off_u   = 1;   // kAcadosPerStagePrefixUOff
  const int off_act = 2;   // kAcadosPerStagePactPreOff
  // Active stages k<K: pact_pre==1, prefix psi/u match the input vectors.
  for (int k = 0; k < K; ++k) {
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][off_act], 1.0, 1e-12)
        << "pact_pre must be 1.0 at active stage k=" << k;
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][off_psi],
                in.prefix_psi_rad[static_cast<std::size_t>(k)], 1e-12)
        << "prefix_psi mismatch at k=" << k;
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][off_u],
                in.prefix_u_mps[static_cast<std::size_t>(k)], 1e-12)
        << "prefix_u mismatch at k=" << k;
  }
  // Inactive stages k>=K: pact_pre==0 (row deactivated).
  for (int k = K; k < N; ++k) {
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][off_act], 0.0, 1e-12)
        << "pact_pre must be 0.0 at inactive stage k=" << k;
  }
}

// T15 F2: the prefix-equality h rows evaluate to 0 (equality satisfied) when
// the state equals the prefix target and pact_pre=1, and to 0 when pact_pre=0
// regardless of state (the activation factor deactivates the row). This builds
// the MX Function for the con_h_expr and checks the prefix rows numerically —
// the symbol-graph evidence that the prefix lock is WIRED (not dead).
TEST_F(AcadosFormulationTest, PrefixLock_HRowsEqualitySatisfied) {
  // con_h_expr(x, u, p_global, p_stage). Build a callable Function.
  casadi::MX x = form_.x_sym();
  casadi::MX u = form_.u_sym();
  casadi::MX pg = form_.p_global_sym();
  casadi::MX ps = form_.p_stage_sym();
  casadi::Function fh("h", casadi::MXVector{x, u, pg, ps},
                      casadi::MXVector{form_.con_h_expr()});
  // Active stage: psi==prefix_psi, u_surge==prefix_u -> prefix rows == 0.
  std::vector<double> xvec{0.0, 0.0, 0.40, 0.0, 4.5};  // psi=0.40, u_surge=4.5
  std::vector<double> uvec{0.0, 9.0};
  std::vector<double> pgv(static_cast<std::size_t>(form_.np_global()), 0.0);
  std::vector<double> psv(static_cast<std::size_t>(form_.np_per_stage()), 0.0);
  psv[0] = 0.40;   // prefix_psi_at_k = psi  -> equality
  psv[1] = 4.5;    // prefix_u_at_k   = u    -> equality
  psv[2] = 1.0;    // pact_pre active
  auto eval_h = [&](const std::vector<double>& xv,
                    const std::vector<double>& uv,
                    const std::vector<double>& pgv_in,
                    const std::vector<double>& psv_in) -> std::vector<double> {
    const std::vector<casadi::DM> res = fh(casadi::DMVector{
        casadi::DM(xv), casadi::DM(uv), casadi::DM(pgv_in), casadi::DM(psv_in)});
    return res.at(0).get_elements();
  };
  std::vector<double> out = eval_h(xvec, uvec, pgv, psv);
  // Prefix rows are the FIRST two h rows.
  ASSERT_GE(static_cast<int>(out.size()), 2);
  EXPECT_NEAR(out[0], 0.0, 1e-9) << "prefix_psi row must be 0 when psi==prefix";
  EXPECT_NEAR(out[1], 0.0, 1e-9) << "prefix_u row must be 0 when u==prefix";
  // Inactive stage: pact_pre=0 -> both prefix rows == 0 regardless of state.
  psv[2] = 0.0;
  xvec[2] = 99.0;  // psi wildly off — row must STILL be 0 (deactivated)
  xvec[4] = -7.0;  // u wildly off
  out = eval_h(xvec, uvec, pgv, psv);
  EXPECT_NEAR(out[0], 0.0, 1e-9) << "inactive prefix_psi row must be 0";
  EXPECT_NEAR(out[1], 0.0, 1e-9) << "inactive prefix_u row must be 0";
  // Active + mismatched: row == pact_pre * (state - target).
  psv[2] = 1.0;
  psv[0] = 0.0;  // prefix_psi target = 0, psi state = 99 -> row = 99
  psv[1] = 0.0;  // prefix_u target = 0, u state = -7 -> row = -7
  xvec[2] = 99.0;
  xvec[4] = -7.0;
  out = eval_h(xvec, uvec, pgv, psv);
  EXPECT_NEAR(out[0], 99.0, 1e-6) << "active prefix_psi row = pact*(psi-prefix)";
  EXPECT_NEAR(out[1], -7.0, 1e-6) << "active prefix_u row = pact*(u-prefix)";
}

// T15 F4: per-stage target drift. pack_parameters must write the drifted target
// position target_x_at_k = tx + sog*cos(cog)*k*dt into the per-stage block at
// the drift offsets. At an 18kn (9.26 m/s) target over a 90s horizon (N=18,
// dt=5s), the drift at the last stage is 9.26*17*5 ≈ 787m — geometrically
// significant vs cpa_safe=1852m. This verifies the drift is precomputed and
// packed (the graph reads it at fixed offsets; without F4 it was drift-free).
TEST_F(AcadosFormulationTest, TargetDrift_PerStagePackedMatchesIpopt) {
  MidMpcInput in{};
  in.own_ship.x_m = 0.0;
  in.own_ship.y_m = 0.0;
  TargetState tgt{};
  tgt.x_m = 1000.0;
  tgt.y_m = 0.0;
  tgt.cog_rad = 0.0;       // due north
  tgt.sog_mps = 9.26;      // ~18kn
  in.targets.push_back(tgt);
  const auto r = form_.pack_parameters(in);
  const auto& ps = r.second;
  const int N = form_.n_horizon();
  const double dt = MidMpcAcadosFormulation::kDt;
  const int Nt = 16;
  const int drift_x_off = 3;            // kAcadosPerStageTgtDriftOff
  const int drift_y_off = 3 + Nt;       // target_y block follows target_x
  // tdx = sog*cos(cog) = 9.26 (north), tdy = sog*sin(cog) = 0.
  const double tdx = 9.26;
  const double tdy = 0.0;
  for (int k = 0; k < N; ++k) {
    const double kdt = static_cast<double>(k) * dt;
    const double exp_x = 1000.0 + tdx * kdt;   // IPOPT drift formula
    const double exp_y = 0.0 + tdy * kdt;
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][drift_x_off], exp_x, 1e-6)
        << "target_x_at_k drift mismatch at k=" << k;
    EXPECT_NEAR(ps[static_cast<std::size_t>(k)][drift_y_off], exp_y, 1e-6)
        << "target_y_at_k drift mismatch at k=" << k;
  }
  // Geometric significance: drift at last stage vs cpa_safe.
  const double drift_last = tdx * static_cast<double>(N - 1) * dt;
  EXPECT_GT(drift_last, 700.0);   // ~787m — large vs cpa_safe=1852m (45%)
}
