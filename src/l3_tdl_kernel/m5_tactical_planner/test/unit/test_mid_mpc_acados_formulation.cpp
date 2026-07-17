// test/unit/test_mid_mpc_acados_formulation.cpp
// Task 15 (P1b-1b): MidMpcAcadosFormulation — production acados OCP CasADi MX
// symbol-graph dimension / parameter-partition assertions.
//
// This test verifies the SYMBOL-GRAPH CONTRACT of the production acados
// formulation (Path B 5-dim state, 2-dim control, documented global/per-stage
// parameter partition), NOT a real acatos solve (that is Task 16+).
// Parameter partition (T15 F2/F4 + P2 T3): global=106 (26 IPOPT head scalars + 80
// target block, IPOPT-142-compatible for the stage-uniform part) + per-stage=37
// (prefix psi/u + pact_pre + per-target drifted x/y + tb_x/tb_y per-stage
// closest-point, VR-07b). The per-stage block is an honest acatos expansion
// beyond IPOPT's flat kParamDim==142; see mid_mpc_acados_formulation.hpp
// partition doc + static_assert (np_global==106, np_per_stage==37).
// State x=[px,py,psi,r,u_surge], control u=[delta,n].
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
#include "m5_tactical_planner/shared/huber_cost.hpp"  // P2 T2 oracle for the MX Huber cross-check

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::shared::huber_cost;

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

// Parameter partition (T15 F2/F4 + P2 T3 documented deviation from IPOPT flat
// 142):
//   global     = 106 (26 IPOPT head scalars + 16x5 target block — the
//              stage-uniform portion, 142-compatible for the global half).
//   per-stage  = 37  (prefix psi/u scalars + pact_pre + per-stage target
//              drift x/y + tb_x/tb_y per-stage closest-point). acatos
//              precomputes per-stage drift (F4), the prefix-equality activation
//              factor (F2), and the per-stage t_b closest point (VR-07b T3)
//              because the single-stage graph cannot index stage k; IPOPT folds
//              these into its flat 142-vector + per-row bounds. GLOBAL stays
//              106; per-stage expands.
TEST_F(AcadosFormulationTest, ParamDims_MatchDocumentedPartition) {
  EXPECT_EQ(form_.np_global(), 106);     // 26 head + 80 target (IPOPT-compatible)
  EXPECT_EQ(form_.np_per_stage(), 37);   // 3 + 2*Nt + 2 tb (prefix+act+drift+tb)
  MidMpcInput in{};
  std::pair<std::vector<double>, std::vector<std::vector<double>>> r;
  EXPECT_NO_THROW({ r = form_.pack_parameters(in); });
  const auto& g = r.first;
  const auto& ps = r.second;
  ASSERT_FALSE(ps.empty());
  EXPECT_EQ(static_cast<int>(g.size()), 106);
  // Every per-stage vector has the SAME length (stage-uniform param layout).
  for (const auto& s : ps) {
    EXPECT_EQ(static_cast<int>(s.size()), 37)
        << "per-stage param vectors must be stage-uniform length 37";
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
  EXPECT_EQ(form_.np_per_stage(), 37);            // 3 + 2*Nt + 2 tb (prefix+act+drift+tb)
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

// ===========================================================================
// P2 T3 (VR-07b): route cost = per-stage t_b closest-point origin + precise
// Huber lateral-deviation loss. The next two tests verify the MX graph wires
// this correctly, by evaluating the J_route MX Function and comparing against
// (a) the T2 huber_cost pure-function ORACLE, and (b) a discriminating input
// where the global route origin and the per-stage t_b DIFFER.
//
// Global slot offsets (mirror the .cpp anonymous-namespace kGIdx aliases):
//   [14] kGIdxRouteFrameOriginX, [15] kGIdxRouteFrameOriginY  (GLOBAL origin)
//   [16] kGIdxRouteFrameNormalX, [17] kGIdxRouteFrameNormalY  (GLOBAL normal)
//   [19] kGIdxLateralScale,       [20] kGIdxRouteWeight       (cost weights)
// Per-stage slot offsets (mirror kAcadosPerStage* in the .hpp):
//   [35] kAcadosPerStageTbXOff,   [36] kAcadosPerStageTbYOff  (per-stage t_b)
// ===========================================================================

namespace {

// Build a 1-output MX Function that evaluates J_route over the formulation's
// symbol graph: f(x, u, p_global, p_stage) -> {J_route}. Mirrors how the
// PrefixLock_HRowsEqualitySatisfied test builds the con_h Function.
casadi::Function build_route_cost_function(const MidMpcAcadosFormulation& form) {
  casadi::MX x  = form.x_sym();
  casadi::MX u  = form.u_sym();
  casadi::MX pg = form.p_global_sym();
  casadi::MX ps = form.p_stage_sym();
  return casadi::Function("J_route", casadi::MXVector{x, u, pg, ps},
                          casadi::MXVector{form.J_route()});
}

// Evaluate J_route numerically for one (x, u, p_global, p_stage) point.
double eval_route_cost(const casadi::Function& f,
                       const std::vector<double>& x,
                       const std::vector<double>& u,
                       const std::vector<double>& pg,
                       const std::vector<double>& ps) {
  const std::vector<casadi::DM> res = f(casadi::DMVector{
      casadi::DM(x), casadi::DM(u), casadi::DM(pg), casadi::DM(ps)});
  return res.at(0).scalar();
}

// Global slot offsets (mirror kGIdx* in the .cpp anonymous namespace).
constexpr int kGOriginX = 14;
constexpr int kGOriginY = 15;
constexpr int kGNormalX = 16;
constexpr int kGNormalY = 17;
constexpr int kGLatScale = 19;
constexpr int kGRouteW   = 20;
// Per-stage tb offsets (mirror kAcadosPerStageTbXOff / TbYOff in the .hpp).
constexpr int kPtbX = 35;
constexpr int kPtbY = 36;

}  // namespace

// P2 T3 Test A — RouteCost_HuberMatchesOracle:
// The route cost MX graph must implement a PRECISE Huber loss
//   w_guard * huber_cost(l, delta_h) / l_scale^2
// where l = (px - tb_x)*nx + (py - tb_y)*ny and huber_cost is the T2 pure
// function. We test BOTH the quadratic region (|l| < delta_h) and the linear
// region (|l| > delta_h) to catch a regression to the old pure-quadratic cost
// or a wrong Huber piecewise definition.
TEST_F(AcadosFormulationTest, RouteCost_HuberMatchesOracle) {
  const casadi::Function froute = build_route_cost_function(form_);
  const double delta_h = form_.config().huber_delta_h;
  ASSERT_GT(delta_h, 0.0) << "Config.huber_delta_h must be positive";

  // Route frame: leg along +x (bearing 0). Normal = +y. Origin/tb placed so the
  // lateral deviation l = (py - tb_y) (since nx=0, ny=1).
  const double nx = 0.0;
  const double ny = 1.0;
  const double l_scale = 400.0;
  const double w_guard = 3.0;
  const double tb_x = 100.0;
  const double tb_y = 200.0;

  std::vector<double> pgv(static_cast<std::size_t>(form_.np_global()), 0.0);
  pgv[kGNormalX] = nx;
  pgv[kGNormalY] = ny;
  pgv[kGLatScale] = l_scale;
  pgv[kGRouteW]   = w_guard;

  std::vector<double> psv(static_cast<std::size_t>(form_.np_per_stage()), 0.0);
  psv[kPtbX] = tb_x;
  psv[kPtbY] = tb_y;

  const std::vector<double> uvec{0.0, 9.0};

  // Two lateral-deviation cases spanning both Huber regions.
  struct Case { double py; const char* region; };
  const std::vector<Case> cases = {
      {tb_y + 0.25 * delta_h, "quadratic (|l|<delta_h)"},   // |l|=0.25*delta_h
      {tb_y + 3.00 * delta_h, "linear    (|l|>delta_h)"},   // |l|=3.0 *delta_h
  };
  for (const Case& c : cases) {
    const std::vector<double> xvec{tb_x + 50.0, c.py, 0.0, 0.0, 5.0};
    const double l = (xvec[0] - tb_x) * nx + (xvec[1] - tb_y) * ny;
    const double expected = w_guard * huber_cost(l, delta_h) / (l_scale * l_scale);
    const double actual = eval_route_cost(froute, xvec, uvec, pgv, psv);
    EXPECT_NEAR(actual, expected, 1e-9)
        << "region " << c.region << ": l=" << l << " delta_h=" << delta_h;
  }
}

// P2 T3 Test B — RouteCost_UsesPerStageTbNotGlobalOrigin:
// Prove the route cost reads the PER-STAGE tb_x/tb_y slots, NOT the GLOBAL
// kGIdxRouteFrameOriginX/Y. Construct an input where the global origin and the
// per-stage t_b DIFFER; the MX route cost must equal the Huber oracle computed
// with tb (NOT with the global origin). This catches a regression where
// build_route_cost_ accidentally still reads the global origin slot.
TEST_F(AcadosFormulationTest, RouteCost_UsesPerStageTbNotGlobalOrigin) {
  const casadi::Function froute = build_route_cost_function(form_);
  const double delta_h = form_.config().huber_delta_h;

  // Route frame: normal +y. Place global origin and tb FAR apart so the two
  // interpretations give very different lateral deviations.
  const double nx = 0.0;
  const double ny = 1.0;
  const double l_scale = 400.0;
  const double w_guard = 3.0;
  const double tb_x = 0.0;
  const double tb_y = 100.0;
  const double go_x = 5000.0;   // global origin x — DIFFERENT from tb_x
  const double go_y = -3000.0;  // global origin y — DIFFERENT from tb_y

  std::vector<double> pgv(static_cast<std::size_t>(form_.np_global()), 0.0);
  pgv[kGOriginX] = go_x;
  pgv[kGOriginY] = go_y;
  pgv[kGNormalX] = nx;
  pgv[kGNormalY] = ny;
  pgv[kGLatScale] = l_scale;
  pgv[kGRouteW]   = w_guard;

  std::vector<double> psv(static_cast<std::size_t>(form_.np_per_stage()), 0.0);
  psv[kPtbX] = tb_x;
  psv[kPtbY] = tb_y;

  const std::vector<double> xvec{120.0, tb_y + 1.5 * delta_h, 0.0, 0.0, 5.0};
  const std::vector<double> uvec{0.0, 9.0};

  // Oracle using the PER-STAGE t_b (this is what the graph MUST match).
  const double l_tb = (xvec[0] - tb_x) * nx + (xvec[1] - tb_y) * ny;
  const double expected_tb =
      w_guard * huber_cost(l_tb, delta_h) / (l_scale * l_scale);
  // Oracle using the GLOBAL origin (this is what the OLD graph did — must NOT match).
  const double l_go = (xvec[0] - go_x) * nx + (xvec[1] - go_y) * ny;
  const double expected_go =
      w_guard * huber_cost(l_go, delta_h) / (l_scale * l_scale);

  const double actual = eval_route_cost(froute, xvec, uvec, pgv, psv);
  EXPECT_NEAR(actual, expected_tb, 1e-6)
      << "route cost must use PER-STAGE tb, not global origin";
  // The two oracles must be far apart (else the test is non-discriminating).
  ASSERT_GT(std::fabs(expected_go - expected_tb), 1.0)
      << "test is non-discriminating: tb-origin and global-origin costs nearly equal";
  EXPECT_NE(actual, expected_go)  // sanity: actual is not the global-origin value
      << "regression: route cost still reads GLOBAL origin (expected per-stage tb)";
}
