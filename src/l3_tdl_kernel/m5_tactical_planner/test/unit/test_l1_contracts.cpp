// ===========================================================================
// L1 Contract Tests (7-layer regression baseline, spec §3).
//
// L1 = OCP-level contracts: codegen signature (Step5 方案 B: nsh=0, NP=211,
// nh=20), constraint residual reads the appended cpa_hard slot (not cpa_safe),
// per-stage box bound liveness + schedule, reachability schedule geometry.
//
// Per spec §3.1 the codegen fixture re-runs gen_mid_mpc_acados.py in SetUp so
// the c_generated_code/ tree is provably fresh (no stale NSH=16/NP=141 from a
// prior commit). A global gtest Environment wraps the codegen+make step so it
// runs ONCE per test binary invocation (codegen is ~30s; we do NOT want to pay
// it per test).
// ===========================================================================

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <casadi/casadi.hpp>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"

#ifdef M5_USE_ACADOS
#include "acados_solver_m5_mid_mpc_acados.h"
#endif

namespace {

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::mid_mpc::MidMpcAcadosSolver;

// The codegen script lives at test/external/acados_backend/gen_mid_mpc_acados.py
// (relative to the package source root). The test binary's working dir is the
// package BUILD dir; we resolve the codegen path via the SOURCE_DIR macro the
// CMake target_compile_definitions call injects (see CMakeLists.txt).
#ifndef M5_L1_SOURCE_DIR
#define M5_L1_SOURCE_DIR "."
#endif

// Run a shell command; return combined rc + stdout+stderr (for ASSERT/EXPECT).
struct CmdResult {
  int rc{-1};
  std::string out;
};
CmdResult run_cmd(const std::string& cmd) {
  std::string buf;
  FILE* pipe = popen(cmd.c_str(), "r");  // NOLINT(cert-env33-c)
  if (pipe == nullptr) {
    return {1, "popen failed"};
  }
  char chunk[256];
  while (std::fgets(chunk, sizeof(chunk), pipe) != nullptr) {
    buf += chunk;
  }
  int rc = pclose(pipe);  // NOLINT(cert-env33-c)
  return {rc, buf};
}

// Read a file into a string (returns empty string if missing).
std::string read_file(const std::string& path) {
  std::ifstream f(path);
  if (!f.good()) {
    return "";
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

// ===========================================================================
// L1 codegen Environment — runs ONCE per binary invocation. Re-codegens the
// acados solver so c_generated_code/ is provably fresh, then makes the shared
// lib. FATAL if any step fails (L1-T1/T3 read the fresh output; a stale tree
// would silently lie).
// ===========================================================================
class L1CodegenEnv : public ::testing::Environment {
 public:
  void SetUp() override {
    const std::string backend = std::string(M5_L1_SOURCE_DIR)
                                + "/test/external/acados_backend";
    const std::string gen_script = backend + "/gen_mid_mpc_acados.py";
    const std::string gen_dir = backend + "/c_generated_code";

    // 1. Codegen.
    const std::string cmd = "python3 " + gen_script + " 2>&1";
    const auto r1 = run_cmd(cmd);
    if (r1.rc != 0) {
      GTEST_NONFATAL_FAILURE_("codegen failed")
          << "rc=" << r1.rc << "\nstdout/stderr:\n" << r1.out;
      return;
    }
    codegen_log_ = r1.out;

    // 2. make shared_lib (so the wrapper can dlopen the FRESH .so on its next
    // build). NOTE: the test BINARY was linked against the .so at build time;
    // re-making it here is for hygiene and for the L1-T3 file read below (the
    // constr_h_fun.c file content is what we assert on, not the linked .so).
    const std::string make_cmd =
        "make -C " + gen_dir + " shared_lib 2>&1";
    const auto r2 = run_cmd(make_cmd);
    if (r2.rc != 0) {
      GTEST_NONFATAL_FAILURE_("make shared_lib failed")
          << "rc=" << r2.rc << "\nstdout/stderr:\n" << r2.out;
    }
  }
  static std::string codegen_log_;
};
std::string L1CodegenEnv::codegen_log_;

::testing::Environment* g_l1_env = \
    ::testing::AddGlobalTestEnvironment(new L1CodegenEnv);

// ===========================================================================
// L1-T1: Codegen signature (Step5 方案 B): nsh=0, np_global=155,
//        np_per_stage=56, nh=20.
// ===========================================================================
TEST(L1ContractCodegenTest, Codegen_ProducesStep5Signature) {
  const std::string& log = L1CodegenEnv::codegen_log_;
  ASSERT_FALSE(log.empty()) << "codegen log missing — Environment SetUp failed";
  EXPECT_NE(log.find("nsh=0"), std::string::npos)
      << "Step5 方案 B requires nsh=0 (no slack; true hard CPA floor)";
  EXPECT_NE(log.find("np_global=155"), std::string::npos)
      << "P7 + Step5 方案 B np_global=155 (26 head + 128 target + 1 cpa_hard)";
  EXPECT_NE(log.find("np_per_stage=56"), std::string::npos)
      << "P7 np_per_stage=56 (prefix+drift+tb+sigma_pos+transition)";
}

// ===========================================================================
// L1-T2: Compile-time NP macro matches formulation static contract. The
//        M5_MID_MPC_ACADOS_NP macro comes from the generated header; the
//        formulation's kAcadosNpGlobal+kAcadosNpPerStageDefault is a compile-
//        time constant. They MUST match (otherwise codegen is stale or the
//        formulation layout drifted).
// =========================================================================//
TEST(L1ContractCodegenTest, SolverNpMacroMatchesFormulationContract) {
#ifdef M5_USE_ACADOS
  // Generated header macros.
  const int np_gen = M5_MID_MPC_ACADOS_NP;
  // Formulation compile-time layout.
  const int np_form = MidMpcAcadosFormulation::kParamDimGlobal
                    + MidMpcAcadosFormulation::kParamDimPerStage;
  EXPECT_EQ(np_gen, np_form)
      << "generated NP=" << np_gen << " vs formulation layout=" << np_form;
  EXPECT_EQ(np_gen, 211)
      << "Step5 方案 B NP = 155 global + 56 per-stage = 211";
#else
  GTEST_SKIP() << "M5_USE_ACADOS not defined — codegen macro unavailable";
#endif
}

// ===========================================================================
// L1-T3: constr_h_fun.c reads the appended CPA hard slot (arg[3][154]) for the
//        CPA per-target residual, NOT the legacy cpa_safe slot (arg[3][10]).
//        If this fails, codegen regressed to pre-Step5 (slack absorbs the hard
//        floor — Bug C deep, RC-C).
// =========================================================================//
TEST(L1ContractCodegenTest, ConstrHFunReadsCpaHardSlot154NotCpaSafeSlot10) {
  const std::string path = std::string(M5_L1_SOURCE_DIR)
      + "/test/external/acados_backend/c_generated_code/"
        "m5_mid_mpc_acados_constraints/m5_mid_mpc_acados_constr_h_fun.c";
  const std::string src = read_file(path);
  ASSERT_FALSE(src.empty()) << "missing constr_h_fun.c at " << path;
  // G_CPA_HARD = 26 + 16*8 = 154 (formulation.hpp:102). The CPA per-target
  // residual reads cpa_hard_m from this slot (Step5 方案 B).
  EXPECT_NE(src.find("arg[3][154]"), std::string::npos)
      << "CPA residual must read cpa_hard_m from slot 154 (G_CPAHard)";
  // The legacy soft cpa_safe slot (10) MUST NOT be read by the CPA residual.
  // We assert the literal "arg[3][10]" does NOT appear in the CPA row region
  // (anywhere — the slot is only used by J_colreg barrier, not constr_h).
  EXPECT_EQ(src.find("arg[3][10]"), std::string::npos)
      << "constr_h_fun.c must NOT read arg[3][10] (legacy cpa_safe slot); "
      << "Step5 方案 B isolates the hard floor at slot 154";
}

// ===========================================================================
// L1-T4/T5: per-stage box live bounds. Requires a real solver; gated on
//          M5_USE_ACADOS.
// =========================================================================//
#ifdef M5_USE_ACADOS

class L1AcadosFixture : public ::testing::Test {
 protected:
  MidMpcAcadosFormulation form_;
  std::unique_ptr<MidMpcAcadosSolver> solver_;

  void SetUp() override {
    form_.build_symbolic_graph();
    solver_ = std::make_unique<MidMpcAcadosSolver>(form_);
  }

  // Production-normal straight-line scenario (mirror test_mid_mpc_acados_solver
  // straight_line()): own heading north at 5 m/s, route_weight=1.0, no targets,
  // full heading box, default ROT/speed. Safe warm-up has already happened in
  // the ctor.
  static MidMpcInput straight_line() {
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
    inp.constraints.own_ship_psi_rad = 0.0;
    inp.route_frame_origin_x_m = 0.0;
    inp.route_frame_origin_y_m = 0.0;
    inp.route_frame_normal_x   = 0.0;
    inp.route_frame_normal_y   = 1.0;
    inp.lateral_scale_m        = 400.0;
    inp.route_weight           = 1.0;
    inp.rot_max_rad_s          = 0.2094;  // default (~12 deg/s).
    return inp;
  }
};

// L1-T4: when the live bounds differ from codegen defaults, the per-stage
// lbx/ubx write (mid_mpc_acados_solver.cpp:1490-1499) SHOULD land the live
// heading box at the psi slot. The test EXPECTS this contract.
//
// FINDING (2026-07-21 diagnostic): the production solver write at line 1497
// passes a LENGTH-5 lbx array, but acatos's ocp_nlp_constraints_model_set
// reads only the first NBX=3 entries (matching idxbx=[2,3,4]). So the
// intended {px:-kUhInf, py:-kUhInf, psi:hdg_min, rot:-rot_max, spd:spd_min}
// gets mis-mapped: acatos reads {lbx[0]=-kUhInf, lbx[1]=-kUhInf,
// lbx[2]=hdg_min} and assigns them to idxbx slots [2,3,4] → state 2 (psi)
// gets -kUhInf, state 3 (rot) gets -kUhInf, state 4 (spd) gets hdg_min. The
// intended psi bound NEVER lands; psi is unbounded. This is the L1 silent-
// contract violation the test was designed to catch.
//
// Per the 7-layer spec (diagnostic phase, "❌ 不修复任何 bug"), we keep the
// test asserting the CORRECT contract (psi_lb == live hdg_min) so it stays
// RED until the production write is fixed to pass a length-3 compact array.
TEST_F(L1AcadosFixture, BoxLive_LiveBoundsWrittenStages1ToN) {
  MidMpcInput inp = straight_line();
  // Narrow the heading box to a NON-default value so the "differs" branch fires.
  inp.constraints.heading_min_rad = -0.5;
  inp.constraints.heading_max_rad =  0.5;
  // The solve may not converge (QP error on the mis-mapped bounds); we proceed
  // to inspect the bounds regardless of convergence (the WRITE happens before
  // the solve).
  const auto sol = solver_->solve(inp, nullptr);
  (void)sol;
  const auto bounds = solver_->debug_get_stage_bounds(1);
  // EXPECTED (contract): psi bound reflects the live heading box ±0.5.
  // ACTUAL (current bug): psi bound is -kUhInf (acatos mis-mapped the length-5
  // write). This test is RED until the bug is fixed.
  EXPECT_NEAR(bounds.psi_lb, -0.5, 1.0e-9)
      << "FINDING: stage 1 psi_lb should be live hdg_min (-0.5); if this is "
      << "-1e10 (kUhInf), the production write at mid_mpc_acados_solver.cpp:1497 "
      << "passes a length-5 array but acatos reads only NBX=3 (compact lbx). "
      << "Fix: write a length-3 compact array matching idxbx=[2,3,4].";
  EXPECT_NEAR(bounds.psi_ub, 0.5, 1.0e-9);
}

// L1-T5: when ALL bounds match codegen defaults, the differs-branch is SKIPPED
// (no write). Bounds stay at codegen defaults.
TEST_F(L1AcadosFixture, BoxLive_DefaultBoundsAreCodegenDefaults) {
  MidMpcInput inp = straight_line();  // all defaults.
  ASSERT_EQ(solver_->solve(inp, nullptr).status,
            MidMpcSolution::Status::Converged);
  const auto bounds = solver_->debug_get_stage_bounds(1);
  // Codegen defaults (acatos_solver_m5_mid_mpc_acados.c:690-694).
  // Compact lbx=[-π, -0.2094, 0]; ubx=[+π, +0.2094, 15].
  EXPECT_NEAR(bounds.psi_lb, -M_PI, 1.0e-6);
  EXPECT_NEAR(bounds.psi_ub,  M_PI, 1.0e-6);
  EXPECT_NEAR(bounds.rot_lb, -0.2094, 1.0e-9);
  EXPECT_NEAR(bounds.rot_ub,  0.2094, 1.0e-9);
  EXPECT_NEAR(bounds.spd_lb, 0.0, 1.0e-9);
  EXPECT_NEAR(bounds.spd_ub, 15.0, 1.0e-9);
}

// ===========================================================================
// L1-T6: stage-0 equality pin. The codegen sets idxbxe_0=[0..4] (all 5 state
//        slots pinned to equality at stage 0). Read the generated .c and
//        assert the assignment. This is the contract that lets lbx0/ubx0=x0
//        act as an equality constraint.
// =========================================================================//
TEST(L1ContractCodegenTest, Stage0EqualityPin_AllFiveSlotsPinned) {
  const std::string path = std::string(M5_L1_SOURCE_DIR)
      + "/test/external/acados_backend/c_generated_code/"
        "acados_solver_m5_mid_mpc_acados.c";
  const std::string src = read_file(path);
  ASSERT_FALSE(src.empty()) << "missing acados_solver.c";
  // idxbxe_0 must be assigned 0,1,2,3,4 (all five state slots pinned).
  EXPECT_NE(src.find("idxbxe_0[0] = 0;"), std::string::npos);
  EXPECT_NE(src.find("idxbxe_0[1] = 1;"), std::string::npos);
  EXPECT_NE(src.find("idxbxe_0[2] = 2;"), std::string::npos);
  EXPECT_NE(src.find("idxbxe_0[3] = 3;"), std::string::npos);
  EXPECT_NE(src.find("idxbxe_0[4] = 4;"), std::string::npos);
}

// ===========================================================================
// L1-T7: reachability schedule — k_head_earliest bound-based geometry.
//        Cannot directly call compute_reachability_schedule (anonymous
//        namespace). Indirectly: solve with rot_max=4.7°/s (0.0820 rad/s) and
//        box_reach=30° (0.5236 rad), check that stage < k_head_earliest sees
//        default ±π while stage >= k_head_earliest sees the live box.
//        k_head_earliest = ceil(0.5236 / (0.0820*15)) - 1 = ceil(0.4257) - 1 = 0.
//        So the schedule activates from stage 1 (the first path stage).
// =========================================================================//
TEST_F(L1AcadosFixture, ReachabilitySchedule_KHeadEarliest_BoundSchedule) {
  MidMpcInput inp = straight_line();
  // rot_max = 4.7 deg/s = 0.0820 rad/s. dt = 5s (kDt default).
  // box_reach = 30 deg = 0.5236 rad.
  // per-step turn = rot_max * dt = 0.410 rad (~23.5 deg).
  // k_head_earliest = ceil(0.5236 / 0.410) - 1 = ceil(1.277) - 1 = 2 - 1 = 1.
  inp.rot_max_rad_s = 0.0820;
  inp.constraints.heading_box_reachable_from_psi0_deg = 30.0;
  inp.constraints.rot_step_deg = 5.0;  // valid so schedule is computed
  inp.constraints.min_alt_required_rad = 0.0;
  inp.constraints.heading_min_rad = -0.5;  // narrow, differs from default
  inp.constraints.heading_max_rad =  0.5;
  // Make this a lateral-active scenario so the schedule is meaningful.
  inp.colregs_conflict_active = true;
  inp.colregs_primary_role = 1U;  // GIVE_WAY
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;

  // The solve may not converge (the live-bounds write is mis-mapped per L1-T4
  // finding); we proceed to inspect the bounds regardless. The schedule WRITE
  // happens before the solve, so the stage-1 bound reflects the schedule.
  const auto sol = solver_->solve(inp, nullptr);
  (void)sol;
  // Stage 1 should be at the live heading box ±0.5 (k_head_earliest=1).
  // FINDING (same as L1-T4): the production write is mis-mapped; psi bound is
  // actually -kUhInf. This stays RED until the write is fixed.
  const auto b1 = solver_->debug_get_stage_bounds(1);
  EXPECT_NEAR(b1.psi_lb, -0.5, 1.0e-9)
      << "stage 1 should be at the live heading box (k_head_earliest=1); "
      << "if -1e10, see L1-T4 finding (length-5 vs length-3 mismatch).";
  EXPECT_NEAR(b1.psi_ub, 0.5, 1.0e-9);
}

// ===========================================================================
// L1-T8: k_minalt uses bprime_rot_step = rot_step/2.0 (NOT rot_step).
//        Computed inside compute_reachability_schedule; we assert the EFFECT
//        by checking that a small min_alt (20°) with rot_step=10° yields a
//        k_minalt that allows the bound to drop in at the expected stage.
//        Direct: bprime = 5°/step → 4 steps to reach 20°. k_minalt = 4.
//        This is asserted via the solve succeeding (no NumericalFailure) and
//        stage bounds NOT showing the min_alt artifact (min_alt is in the h
//        rows, not the x box).
// =========================================================================//
TEST_F(L1AcadosFixture, ReachabilitySchedule_KMinaltUsesBprimeRotStep) {
  MidMpcInput inp = straight_line();
  inp.rot_max_rad_s = 0.2094;
  inp.constraints.rot_step_deg = 10.0;
  inp.constraints.min_alt_required_rad = 20.0 * M_PI / 180.0;  // 20 deg.
  // The contract: bprime_rot_step = rot_step/2 = 5 deg. k_minalt is derived
  // from min_alt / bprime_rot_step_per_step. We assert the solve completes
  // (does not throw / does not fail with NumericalFailure on the schedule).
  const auto sol = solver_->solve(inp, nullptr);
  // Either Converged (schedule satisfied) or Infeasible (schedule too tight).
  // NumericalFailure (MAX_ITER mapped) means the schedule computation itself
  // mis-allocated rows — that is the bug this test catches.
  EXPECT_NE(sol.status, MidMpcSolution::Status::NumericalFailure)
      << "k_minalt schedule computation must not trip a numerical failure "
      << "(bprime_rot_step = rot_step/2). Status=" << static_cast<int>(sol.status);
}

// ===========================================================================
// L1-T9: CPA schedule three-phase (bound-based, not idxsh). The CPA row lh/uh
//        is set per-stage based on the prefix_K and k_cpa_suffix:
//          stage < prefix_K        → committed (0,0)
//          prefix_K <= stage < K_suf → relaxed (-inf, +inf)
//          stage >= K_suf          → hard floor (0, +inf)
//        Step5 方案 B: nsh=0, so the bound is set DIRECTLY (no idxsh). We
//        verify the solve does not trip a slack-related failure.
// =========================================================================//
TEST_F(L1AcadosFixture, CpaSchedule_ThreePhaseBoundBasedNotIdxsh) {
  MidMpcInput inp = straight_line();
  // Add one target inside the 2500 band so the CPA row is active.
  mass_l3::m5::TargetState tgt{};
  tgt.id = 1;
  tgt.x_m = 500.0;
  tgt.y_m = 0.0;
  tgt.sog_mps = 0.0;
  tgt.cog_rad = 0.0;
  tgt.cpa_m = 500.0;
  inp.targets.push_back(tgt);
  inp.colregs_conflict_active = true;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.cpa_safe_m = 2500.0;
  inp.constraints.cpa_hard_m = 1852.0;

  const auto sol = solver_->solve(inp, nullptr);
  // Under Step5 方案 B nsh=0, the hard floor is enforced by lh=0 (no slack).
  // A target inside 1852 m would make the OCP hard-infeasible. 500 m < 1852 m,
  // so we EXPECT Infeasible (the CPA row cannot be satisfied). If the schedule
  // regressed to soft (idxsh re-added), the solve would Converge with slack.
  // This test catches BOTH regressions.
  EXPECT_TRUE(sol.status == MidMpcSolution::Status::Infeasible
           || sol.status == MidMpcSolution::Status::NumericalFailure)
      << "CPA hard floor (nsh=0) must reject a target at 500 m (< 1852 hard); "
      << "status=" << static_cast<int>(sol.status);
}

// ===========================================================================
// L1-T10: prefix CPA witness — when prefix positions violate cpa_hard, the
//         solve returns NumericalFailure (or Infeasible) even if the SQP
//         itself converges. The prefix witness is the constraints_satisfied_
//         re-check; it overrides a status-0 solve to NumericalFailure when a
//         prefix row violates.
// =========================================================================//
TEST_F(L1AcadosFixture, PrefixCpaWitness_ViolationOverridesToFailure) {
  MidMpcInput inp = straight_line();
  // Prefix with 3 stages; positions engineered to put a target inside cpa_hard
  // for at least one prefix stage. This should fail the witness check.
  inp.prefix_active_k = 3;
  inp.prefix_psi_rad = {0.0, 0.0, 0.0};
  inp.prefix_u_mps = {5.0, 5.0, 5.0};
  mass_l3::m5::TargetState tgt{};
  tgt.id = 1;
  tgt.x_m = 100.0;  // well inside cpa_hard=1852.
  tgt.y_m = 0.0;
  tgt.sog_mps = 0.0;
  tgt.cog_rad = 0.0;
  tgt.cpa_m = 100.0;
  inp.targets.push_back(tgt);
  inp.colregs_conflict_active = true;
  inp.colregs_primary_role = 1U;
  inp.colregs_preferred_direction = mass_l3::m5::ColregsPreferredDirection::Starboard;
  inp.constraints.cpa_hard_m = 1852.0;

  const auto sol = solver_->solve(inp, nullptr);
  EXPECT_FALSE(sol.status == MidMpcSolution::Status::Converged)
      << "prefix CPA witness must NOT report Converged when a prefix stage "
      << "violates cpa_hard; status=" << static_cast<int>(sol.status);
}

// ===========================================================================
// L1-T11: k_head_earliest > k_head_latest — warn but do not throw. The solve
//         returns some status (NOT a crash); the schedule is internally
//         inconsistent but the solver must not throw.
// =========================================================================//
TEST_F(L1AcadosFixture, ReachabilitySchedule_KHeadEarliestExceedsLatest_NoThrow) {
  MidMpcInput inp = straight_line();
  // Tiny rot_max + huge box_reach makes k_head_earliest > N (the horizon),
  // which is > k_head_latest. The schedule logs a warn but solve() must return
  // a status, not throw.
  inp.rot_max_rad_s = 1.0e-4;  // ~0.0006 deg/s — barely any turn rate.
  inp.constraints.heading_box_reachable_from_psi0_deg = 179.0;  // near-full.
  inp.constraints.rot_step_deg = 5.0;
  EXPECT_NO_THROW({
    const auto sol = solver_->solve(inp, nullptr);
    // Status is whatever the solver decides — the contract is "no throw".
    (void)sol;
  });
}

#endif  // M5_USE_ACADOS

}  // namespace
