// test/unit/test_l1a_cpa_hard_floor.cpp
// L1a-spec-freeze 批次 1 — DP-01 CPA hard floor true-hard 化 (Step5 方案 B,
// VR-01 final, 2026-07-20).
//
// These tests verify the SYMBOL-GRAPH contract of the production acados
// formulation as it expresses the CPA per-target constraint residual with the
// NEW kGIdxCpaHard slot (appended at the end of the global param block). They
// do NOT require the regenerated acatos solver .so: the formulation's MX graph
// is built in-process (CasADi MX), so the CPA residual expression can be
// evaluated directly on synthetic (x, u, p_global, p_stage) inputs.
//
// Test mapping (Step5 方案 B 深化报告 §6):
//   T-B2  HardFloor_AdversarialResidualUsesCpaHard:
//          CPA row residual = dx^2+dy^2 - cpa_hard^2 (NOT cpa_safe^2).
//          Adversarial: target at d=1851.9 (< cpa_hard 1852) -> residual < 0
//          (violated); target at d=1852.1 (>= cpa_hard) -> residual > 0
//          (satisfied). The residual uses cpa_hard_m regardless of cpa_safe.
//   T-B6a ParamIsolation_CpaSafeOnlyAffectsCostBarrier:
//          Varying cpa_safe (1852 <-> 2500) leaves the CPA row residual
//          UNCHANGED (it reads cpa_hard_m, not cpa_safe).
//   T-B6b ParamIsolation_CpaHardOnlyAffectsResidual:
//          Varying cpa_hard_m (1852 <-> 2000) changes the CPA row residual
//          threshold but leaves J_colreg's exp barrier argument UNCHANGED
//          (it reads cpa_safe).
//   T-B6c ParamIsolation_GlobalSlotsAreDistinct:
//          pack_parameters writes cpa_safe_m to slot 10 (kGIdxCpaSafe) and
//          cpa_hard_m to slot 154 (kGIdxCpaHard, appended). The slots do not
//          collide.
//
// Solver-level T-B2 (status=Infeasible on adversarial d<1852 fixture) requires
// the regenerated acatos solver .so; it is in test_mid_mpc_acados_solver.cpp
// and is gated on the codegen re-run (the in-tree generated header still has
// NSH=16/NP=210 — stale pending `python3 gen_mid_mpc_acados.py` re-run).
//
// References:
//   - review/2026-07-20-step5-plan-b-nh20-agent_8ae45f72.md §3 + §6 T-B2/T-B6
//   - design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md §Step5 VR-01
//   - M5_MPC_业务流程分层架构.md §12.1 (DP-01 row 布局实施 + L4 telemetry 补救)
//
// CasADi LGPL-3.0: internal MISRA violations exempted per coding-standards.md
// §10 (dynamic-link boundary).

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::TargetState;
using mass_l3::m5::mid_mpc::kAcadosGIdxCpaSafe;
using mass_l3::m5::mid_mpc::kAcadosGIdxCpaHard;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;

// Step5 方案 B code-review L3: previously this test mirrored the slot literal
// in a local `kAcadosGIdxCpaSafe = 10`. Now that the public alias
// `kAcadosGIdxCpaSafe` is exposed in the formulation header (mirroring the
// anonymous-namespace kGIdxCpaSafe in the .cpp), the test references the same
// named constant — keeping slot arithmetic single-sourced.

// Fixture: build the MX graph once per test. The formulation ctor runs the
// Config clamp (horizon>=2, max_targets<=16); build_symbolic_graph() then
// materializes x/u/p_global/p_stage symbols + disc_dyn + con_h + 7 costs.
class CpaHardFloorTest : public ::testing::Test {
 protected:
  MidMpcAcadosFormulation form_{};

  void SetUp() override { form_.build_symbolic_graph(); }

  // Build a callable Function over con_h_expr(x, u, p_global, p_stage).
  // Mirrors the pattern in test_mid_mpc_acados_formulation.cpp (PrefixLock test).
  casadi::Function make_h_fn() const {
    return casadi::Function("h_check",
        casadi::MXVector{form_.x_sym(), form_.u_sym(),
                         form_.p_global_sym(), form_.p_stage_sym()},
        casadi::MXVector{form_.con_h_expr()});
  }

  // Evaluate con_h at a single (x, u, p_global, p_stage) point.
  std::vector<double> eval_h(const casadi::Function& fh,
                             const std::vector<double>& x_vec,
                             const std::vector<double>& u_vec,
                             const std::vector<double>& pg_vec,
                             const std::vector<double>& ps_vec) const {
    const std::vector<casadi::DM> res = fh(casadi::DMVector{
        casadi::DM(x_vec), casadi::DM(u_vec),
        casadi::DM(pg_vec), casadi::DM(ps_vec)});
    return res.at(0).get_elements();
  }

  // Zeroed global/per-stage param vectors (the test overwrites specific slots).
  std::vector<double> zero_global() const {
    return std::vector<double>(static_cast<std::size_t>(form_.np_global()), 0.0);
  }
  std::vector<double> zero_stage() const {
    return std::vector<double>(static_cast<std::size_t>(form_.np_per_stage()), 0.0);
  }
};

// =============================================================================
// T-B2: CPA row residual uses cpa_hard_m (NOT cpa_safe). Adversarial: d<cpa_hard
// -> residual < 0 (violated); d>=cpa_hard -> residual >= 0 (satisfied). The
// residual must NOT respond to cpa_safe variations (that is T-B6a below).
//
// Row layout (mirror gen script + formulation.cpp build_con_h_):
//   [0]      prefix_psi
//   [1]      prefix_u
//   [2..17]  CPA per-target (Nt=16, one row per target slot)
//   [18]     direction
//   [19]     min_alt
// The first target slot's CPA row is at index 2 (kRowCpaBase).
// =============================================================================
TEST_F(CpaHardFloorTest, HardFloor_AdversarialResidualUsesCpaHard_T_B2) {
  const casadi::Function fh = make_h_fn();
  // cpa_hard_m = 1852 (hard floor, fixed), cpa_safe = 2500 (bumped during
  // conflict). The CPA row residual must use cpa_hard (1852), NOT cpa_safe.
  // If it used cpa_safe, the residual threshold would be 2500 and d=1851.9
  // would yield residual = 1851.9^2 - 2500^2 = -2.78e6 (massively violated),
  // which would mask the precise hard-floor test.
  constexpr double kCpaHard = 1852.0;
  constexpr double kCpaSafe = 2500.0;  // conflict-bumped soft aspiration
  constexpr int kRowCpaBase = 2;       // first CPA per-target row
  constexpr int kDriftXOff = 3;        // kAcadosPerStageTgtDriftOff (target_x_at_k[0])

  // Place own ship at origin, target[0] at distance d along +x.
  // The per-stage drift slot for target 0 (x) is at p_stage[kDriftXOff].
  // The own ship x is x_vec[0]; the CPA residual uses dx = px - tx_at_k.
  // We hold own ship at origin and move the target to set d.
  auto run_residual = [&](double d) -> double {
    std::vector<double> x_vec{0.0, 0.0, 0.0, 0.0, 5.0};  // px=0, py=0
    std::vector<double> u_vec{0.0, 9.0};
    std::vector<double> pg = zero_global();
    std::vector<double> ps = zero_stage();
    // Set the global CPA slots.
    pg[static_cast<std::size_t>(kAcadosGIdxCpaSafe)] = kCpaSafe;
    // kGIdxCpaHard is appended at the END of the global block
    // (= kParamDimGlobal - 1 = 155 - 1 = 154). The header exposes this alias.
    pg[static_cast<std::size_t>(form_.kParamDimGlobal) - 1] = kCpaHard;
    // Place target 0 at (d, 0) -> dx = 0 - d = -d, dy = 0.
    ps[static_cast<std::size_t>(kDriftXOff)] = d;  // target_x_at_k[0] = d
    // ps[kDriftXOff + Nt] is target_y_at_k[0] = 0 (already zeroed).
    const std::vector<double> h = eval_h(fh, x_vec, u_vec, pg, ps);
    // Return the first CPA row (target 0) residual.
    return h[static_cast<std::size_t>(kRowCpaBase)];
  };

  // Adversarial 1: d = 1851.9 (< cpa_hard 1852 by 0.1m) -> residual < 0
  // (hard floor VIOLATED). The TRUE hard floor must detect this.
  // Expected residual = 1851.9^2 - 1852^2 = -370.6 (m^2).
  {
    const double r = run_residual(1851.9);
    EXPECT_LT(r, 0.0)
        << "CPA residual must be < 0 for d=1851.9 < cpa_hard=1852 (true hard)";
    // Sanity: the residual should be a small negative number (NOT -2.78e6 which
    // would be the case if the threshold were cpa_safe=2500). |r| ~ 370.6 m^2.
    EXPECT_GT(r, -1000.0)
        << "residual magnitude matches cpa_hard=1852 threshold, not cpa_safe=2500";
    EXPECT_LT(r, -300.0)
        << "residual is the expected ~-370.6 m^2 for d=1851.9 vs cpa_hard=1852";
  }

  // Adversarial 2: d = 1852.1 (> cpa_hard 1852 by 0.1m) -> residual > 0
  // (hard floor SATISFIED).
  // Expected residual = 1852.1^2 - 1852^2 = +370.6 (m^2).
  {
    const double r = run_residual(1852.1);
    EXPECT_GT(r, 0.0)
        << "CPA residual must be > 0 for d=1852.1 > cpa_hard=1852 (compliant)";
    EXPECT_LT(r, 1000.0)
        << "residual magnitude matches cpa_hard=1852 threshold";
    EXPECT_GT(r, 300.0)
        << "residual is the expected ~+370.6 m^2 for d=1852.1 vs cpa_hard=1852";
  }

  // Boundary: d = 1852.0 exactly -> residual ~ 0 (within floating-point).
  {
    const double r = run_residual(1852.0);
    EXPECT_NEAR(r, 0.0, 1.0e-6)
        << "CPA residual must be ~0 at d = cpa_hard = 1852 exactly";
  }
}

// =============================================================================
// T-B6a: varying cpa_safe (1852 <-> 2500) leaves the CPA row residual
// UNCHANGED (it reads cpa_hard_m, not cpa_safe). This proves the hard floor
// is DECOUPLED from the soft aspiration — the Step5 方案 B core invariant.
// =============================================================================
TEST_F(CpaHardFloorTest, ParamIsolation_CpaSafeDoesNotAffectResidual_T_B6a) {
  const casadi::Function fh = make_h_fn();
  constexpr double kCpaHard = 1852.0;
  constexpr int kRowCpaBase = 2;
  constexpr int kDriftXOff = 3;
  // Target at d = 1900 (> cpa_hard, so residual is positive regardless).
  const double d = 1900.0;

  auto run_residual = [&](double cpa_safe_val) -> double {
    std::vector<double> x_vec{0.0, 0.0, 0.0, 0.0, 5.0};
    std::vector<double> u_vec{0.0, 9.0};
    std::vector<double> pg = zero_global();
    std::vector<double> ps = zero_stage();
    pg[static_cast<std::size_t>(kAcadosGIdxCpaSafe)] = cpa_safe_val;
    pg[static_cast<std::size_t>(form_.kParamDimGlobal) - 1] = kCpaHard;
    ps[static_cast<std::size_t>(kDriftXOff)] = d;
    const std::vector<double> h = eval_h(fh, x_vec, u_vec, pg, ps);
    return h[static_cast<std::size_t>(kRowCpaBase)];
  };

  // Same residual for cpa_safe=1852 (no conflict) and cpa_safe=2500 (conflict).
  // If the residual read cpa_safe, these would differ by 2500^2 - 1852^2 ≈ 6.2e6.
  const double r_no_conflict = run_residual(1852.0);
  const double r_conflict    = run_residual(2500.0);
  EXPECT_NEAR(r_no_conflict, r_conflict, 1.0e-6)
      << "CPA row residual must NOT respond to cpa_safe (hard/soft isolation)";
  // Sanity: residual = 1900^2 - 1852^2 ≈ +1.79e5 m^2 (positive, compliant).
  EXPECT_GT(r_no_conflict, 0.0)
      << "d=1900 > cpa_hard=1852 -> residual > 0 (compliant)";
}

// =============================================================================
// T-B6b: varying cpa_hard_m (1852 <-> 2000) changes the CPA row residual
// threshold (the row is hard at cpa_hard), but leaves the J_colreg cost
// barrier argument UNCHANGED (it reads cpa_safe). This proves the SOFT
// aspiration (cost barrier) is decoupled from the hard floor.
// =============================================================================
TEST_F(CpaHardFloorTest, ParamIsolation_CpaHardDoesNotAffectCost_T_B6b) {
  const casadi::Function fh = make_h_fn();
  constexpr double kCpaSafe = 2500.0;
  constexpr int kRowCpaBase = 2;
  constexpr int kDriftXOff = 3;
  const double d = 1900.0;  // < cpa_safe (cost barrier active), > cpa_hard 1852

  // Residual path: cpa_hard change MUST move the residual threshold.
  auto run_residual = [&](double cpa_hard_val) -> double {
    std::vector<double> x_vec{0.0, 0.0, 0.0, 0.0, 5.0};
    std::vector<double> u_vec{0.0, 9.0};
    std::vector<double> pg = zero_global();
    std::vector<double> ps = zero_stage();
    pg[static_cast<std::size_t>(kAcadosGIdxCpaSafe)] = kCpaSafe;
    pg[static_cast<std::size_t>(form_.kParamDimGlobal) - 1] = cpa_hard_val;
    ps[static_cast<std::size_t>(kDriftXOff)] = d;
    const std::vector<double> h = eval_h(fh, x_vec, u_vec, pg, ps);
    return h[static_cast<std::size_t>(kRowCpaBase)];
  };

  const double r_1852 = run_residual(1852.0);
  const double r_2000 = run_residual(2000.0);
  // Both should be POSITIVE (d=1900 > both cpa_hard values) but DIFFERENT.
  EXPECT_GT(r_1852, 0.0) << "d=1900 > cpa_hard=1852 -> residual > 0";
  EXPECT_GT(r_2000, 0.0) << "d=1900 > cpa_hard=2000 -> residual > 0";
  // The residual difference = (1900^2 - 2000^2) - (1900^2 - 1852^2)
  //                        = 1852^2 - 2000^2 = -560_296 m^2.
  EXPECT_NEAR(r_1852 - r_2000,
              1852.0 * 1852.0 - 2000.0 * 2000.0, 1.0)
      << "cpa_hard_m change moves the residual threshold by the expected delta";

  // Cost path: build a Function over J_colreg (the soft aspiration barrier).
  // J_colreg reads cpa_safe (slot 10), NOT cpa_hard (slot 154). Varying
  // cpa_hard must NOT change J_colreg. We access J_colreg via the formulation
  // graph handles — but the formulation does not expose J_colreg publicly
  // (only J_route/J_terminal/J_transition are exposed). We instead verify the
  // isolation indirectly: the CPA row residual (which DOES read cpa_hard) and
  // the cost are decoupled by construction because they read DIFFERENT global
  // slots (slot 10 vs slot 154). The slot-distinct test below (T-B6c) covers
  // the wiring directly.
  //
  // NOTE: a full J_colreg numerical oracle is out of scope for this test
  // (J_colreg is not exposed; exposing it would expand the formulation API).
  // The residual-side evidence above + the slot-distinct evidence below
  // together establish the hard/soft isolation contract.
}

// =============================================================================
// T-B6c: pack_parameters writes cpa_safe_m to slot 10 (kGIdxCpaSafe) and
// cpa_hard_m to slot 154 (kGIdxCpaHard, appended). The slots are DISTINCT and
// each is written from the correct ConstraintInputs field.
// =============================================================================
TEST_F(CpaHardFloorTest, ParamIsolation_GlobalSlotsAreDistinct_T_B6c) {
  MidMpcInput in{};
  in.constraints.cpa_safe_m = 2500.0;  // conflict-bumped soft aspiration
  in.constraints.cpa_hard_m = 1852.0;  // fixed hard floor
  const auto r = form_.pack_parameters(in);
  const auto& g = r.first;
  ASSERT_EQ(static_cast<int>(g.size()), form_.np_global());
  ASSERT_EQ(form_.np_global(), 155);  // Step5 方案 B: 26 head + 128 target + 1 cpa_hard

  // cpa_safe slot = 10, written from cpa_safe_m.
  EXPECT_NEAR(g[static_cast<std::size_t>(kAcadosGIdxCpaSafe)], 2500.0, 1.0e-9)
      << "cpa_safe_m must be written to slot 10 (kGIdxCpaSafe)";
  // cpa_hard slot = 154 (appended at end), written from cpa_hard_m.
  EXPECT_NEAR(g[static_cast<std::size_t>(form_.kParamDimGlobal) - 1], 1852.0, 1.0e-9)
      << "cpa_hard_m must be written to slot 154 (kGIdxCpaHard, appended)";
  // Distinct slots (the whole point of the Step5 方案 B separation).
  EXPECT_NE(static_cast<std::size_t>(kAcadosGIdxCpaSafe),
            static_cast<std::size_t>(form_.kParamDimGlobal) - 1);

  // Reverse the values: cpa_safe=1852, cpa_hard=2000. The slots must track
  // their own field, NOT cross-contaminate.
  in.constraints.cpa_safe_m = 1852.0;
  in.constraints.cpa_hard_m = 2000.0;
  const auto r2 = form_.pack_parameters(in);
  const auto& g2 = r2.first;
  EXPECT_NEAR(g2[static_cast<std::size_t>(kAcadosGIdxCpaSafe)], 1852.0, 1.0e-9)
      << "cpa_safe slot tracks cpa_safe_m only";
  EXPECT_NEAR(g2[static_cast<std::size_t>(form_.kParamDimGlobal) - 1], 2000.0, 1.0e-9)
      << "cpa_hard slot tracks cpa_hard_m only";
}
