// test/unit/test_mid_mpc_acados_parity.cpp
// P1b-1b Task 18 — IPOPT ↔ acados output-contract PARITY test.
//
// Backend-switch gate (TDL Lead P1b-1b). Same MidMpcInput is fed to BOTH
// backends; the test proves the produced MidMpcSolution shapes are CONTRACT-
// compatible (downstream M4/L4/tail_gate is agnostic to which backend produced
// the trajectory). NOT bit-close: IPOPT (kinematics x=[psi,u], explicit Euler
// dead-reckon px/py) and acados (Path B 5-dim double-integrator state x=[px,py,
// psi,r,u_surge]) are different physics — the spec §P1b-1b parity gate and §P1b-
// 1c benchmark (T19) re-argue the physics-difference tolerance separately. T18
// is the BASIC contract-parity + behavioral-equivalence gate.
//
// Assertions (per task-18-brief §P1b-1b):
//   1. SameInput_ProducesCompatibleOutputShape
//        - trajectory.size() matches (both N=18 production horizon)
//        - both status usable (NOT NotInitialized / NumericalFailure / Infeasible)
//        - cost_total / solve_duration_ms / cpa_slack accessible on both
//   2. StraightLine_BothHoldCourse
//        - both trajectories' psi_rad stay close to planned_route_bearing_rad
//          (< 0.15 rad deviation) on a non-aggressive straight-line scenario
//   3. OutputContract_FieldsMatch
//        - same field names/types present in both solutions (TrajectoryPoint
//          {psi_rad, u_mps, x_m, y_m}; Status; cost_total; cpa_slack;
//          solve_duration_ms; ipopt_iterations)
//
// Scenario choice — straight-line (NOT CrossingGiveWay):
//   T17 confirmed IPOPT in the acados container has 8 pre-existing ENVIRONMENTAL
//   failures, among them MidMpcNlpTest.CrossingGiveWay (container IPOPT/MUMPS
//   differs from mass-l3-sil). The parity test must NOT use CrossingGiveWay.
//   The straight-line scenario is one IPOPT converges on in this container
//   (MidMpcNlpTest.StraightLineNoTargets EXPECT_EQ Converged PASSES), so it is
//   the HONEST scenario for parity.
//
// route_weight = 1.0 for BOTH backends (T17 finding, production-normal):
//   T17 confirmed the cold-capsule warm-up (acatos ctor) is route_weight-
//   INDEPENDENT (the first cold solve fails at both 0 and 1.0; warm-up fixes
//   it). route_weight=1.0 is retained because it is the documented production-
//   normal value (types.hpp:216 "cross-leg guard: 1.0 active, 0.0 inert/cross-
//   corner"; mid_mpc_node.cpp:746 `inp.route_weight = guard.crosses_corner ?
//   0.0 : 1.0`). A real ship ALWAYS has a route to track, so this is the
//   physically-correct input BOTH backends receive. IPOPT handles it fine
//   (MidMpcNlpTest already converges straight-line at route_weight=0; adding
//   the px/py Hessian block via route_weight=1.0 does not break IPOPT). This
//   is a FAIR comparison: same production-realistic input to both.
//
// acatos warm-up: the MidMpcAcadosSolver ctor runs the cold-capsule warm-up
// (2 throwaway benign solves) so the first REAL cycle converges. The parity
// test constructs both solvers fresh — the acatos one warms up in its ctor
// automatically. Do NOT add separate warm-up calls (T17 final-fix finding).
//
// Horizon: both formulations use the PRODUCTION N=18 (90 s @ dt=5 s). The IPOPT
// test_mid_mpc_solver fixture uses N=8 for speed, but here we use N=18 because
// the acatos generated solver .so is codegen'd for N=18 (cannot be changed at
// runtime); trajectory.size() parity requires the IPOPT formulation to match.
//
// OFF regression: this test is wired under BOTH M5_HAS_CASADI AND M5_HAS_ACADOS
// (the acados sub-block in CMakeLists sits inside the CasADi block, so the gate
// is implicit). Under OFF (acados disabled) the test compiles out — the IPOPT-
// only test suite already covers the OFF path.
//
// No mocks, no skips, no forced-pass, no threshold-tuning. If parity genuinely
// fails (e.g. the two physics diverge enough that "both hold course < 0.15 rad"
// does not hold), STOP and report — the spec §P1b-1c benchmark (T19) is where
// physics-difference tolerance is re-argued; T18 is the basic contract-parity
// gate.
//
// CasADi LGPL-3.0 / acados C lib 2-Clause BSD: internal MISRA violations
// exempted per coding-standards.md §10.

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <type_traits>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::MidMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::mid_mpc::MidMpcAcadosFormulation;
using mass_l3::m5::mid_mpc::MidMpcAcadosSolver;
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::mid_mpc::MidMpcSolver;

// ---------------------------------------------------------------------------
// Fixture — build BOTH backends once per test, both at the PRODUCTION horizon
// N=18. gtest creates a fresh fixture per test, so each test gets a fresh pair
// of capsules. The acatos ctor runs the cold-capsule warm-up (warm_up_capsule_)
// so the first REAL solve() in each test sees a primed capsule and converges
// (see T17 final-fix root-cause correction in task-17-report.md). The IPOPT
// formulation uses CasADi/IPOPT (loaded via dlopen, MUMPS linear solver).
// ---------------------------------------------------------------------------
class MidMpcAcadosParityTest : public ::testing::Test {
 protected:
  // Production horizon N=18 matches the acatos codegen .so (cannot be changed
  // at runtime). IPOPT default Config also has n_horizon=18, so both backends
  // are at the SAME horizon — trajectory.size() parity is meaningful.
  static constexpr int32_t kHorizon = MidMpcAcadosFormulation::kNDefault;  // 18

  void SetUp() override {
    // ---- IPOPT backend ----
    MidMpcNlpFormulation::Config icfg;
    icfg.n_horizon   = kHorizon;
    icfg.dt_s        = MidMpcAcadosFormulation::kDt;  // 5.0
    icfg.w_colreg    = 30.0;
    icfg.w_dist      = 10.0;
    icfg.w_vel       = 1.0;
    icfg.max_targets = 16;
    ipopt_form_ = std::make_unique<MidMpcNlpFormulation>(icfg);
    ipopt_form_->build_symbolic_graph();
    MidMpcSolver::IpoptOptions opts;
    opts.max_iter  = 150;
    opts.tol       = 1.0e-4;
    opts.timeout_s = 5.0;  // relaxed: N=18 is heavier than the N=8 fixture
    ipopt_ = std::make_unique<MidMpcSolver>(*ipopt_form_, opts);

    // ---- acatos backend ----
    // The ctor runs the cold-capsule warm-up (warm_up_capsule_) automatically;
    // the first REAL solve() in each test sees a primed capsule. Do NOT add a
    // separate warm-up call (T17 final-fix).
    acados_form_ = std::make_unique<MidMpcAcadosFormulation>();
    acados_form_->build_symbolic_graph();
    acados_ = std::make_unique<MidMpcAcadosSolver>(*acados_form_);
  }

  std::unique_ptr<MidMpcNlpFormulation> ipopt_form_;
  std::unique_ptr<MidMpcSolver> ipopt_;
  std::unique_ptr<MidMpcAcadosFormulation> acados_form_;
  std::unique_ptr<MidMpcAcadosSolver> acados_;

  // Same MidMpcInput for BOTH backends (the parity contract). Mirrors the T17
  // acatos straight_line() helper AND the IPOPT make_base_input, unified so
  // both backends receive byte-identical input. route_weight=1.0 is the
  // production-normal value (see file header rationale).
  static MidMpcInput straight_line_input() {
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
    // Mirror own_ship heading into COLREGs directional reference (Phase E2).
    inp.constraints.own_ship_psi_rad = inp.own_ship.psi_rad;
    // Physically-correct active-leg scenario (route_weight rationale): own is
    // ON the route leg at its origin, heading along it. These four fields are
    // the documented defaults; set explicitly so the test does not depend on
    // default-init drift. Eastward active-leg normal for bearing=0 (north).
    inp.route_frame_origin_x_m = 0.0;
    inp.route_frame_origin_y_m = 0.0;
    inp.route_frame_normal_x   = 0.0;
    inp.route_frame_normal_y   = 1.0;
    inp.lateral_scale_m        = 400.0;
    inp.route_weight           = 1.0;  // active cross-leg guard value (normal ops)
    return inp;
  }
};

constexpr int32_t MidMpcAcadosParityTest::kHorizon;

// ---------------------------------------------------------------------------
// Assertion 1 — SameInput_ProducesCompatibleOutputShape.
//
// Both backends receive the SAME MidMpcInput (route_weight=1.0 straight-line).
// The produced MidMpcSolution must have:
//   - trajectory.size() == N (both produce the production N=18 horizon). The
//     IPOPT formulation is configured with n_horizon=18 (matches the acatos
//     codegen .so), so the trajectory lengths MUST match exactly.
//   - status NOT in {NotInitialized, NumericalFailure, Infeasible} — i.e. the
//     solve produced a usable trajectory. We accept Converged, Timeout (still
//     usable per the MidMpc node contract); we REJECT the three failure modes
//     that mean the trajectory is unsafe to dispatch. (For this benign no-
//     target scenario, both backends should produce Converged outright; the
//     looser assertion is the contract floor, not the scenario-exact value.)
//   - cost_total / solve_duration_ms / cpa_slack ACCESSIBLE on both solutions
//     (the field reads below prove the field exists in the type and is finite
//     — the downstream M4/L4/tail_gate contract).
// ---------------------------------------------------------------------------
TEST_F(MidMpcAcadosParityTest, SameInput_ProducesCompatibleOutputShape) {
  const MidMpcInput in = straight_line_input();
  const MidMpcSolution si = ipopt_->solve(in, nullptr);
  const MidMpcSolution sa = acados_->solve(in, nullptr);

  // Trajectory length must match exactly (both configured for N=18).
  EXPECT_EQ(si.trajectory.size(), sa.trajectory.size())
      << "trajectory length mismatch: ipopt=" << si.trajectory.size()
      << " acados=" << sa.trajectory.size() << " (both must be N=" << kHorizon
      << ")";
  EXPECT_EQ(si.trajectory.size(), static_cast<std::size_t>(kHorizon))
      << "IPOPT trajectory length != production N=18";

  // Both status usable: NOT NotInitialized / NumericalFailure / Infeasible.
  EXPECT_NE(si.status, MidMpcSolution::Status::NotInitialized);
  EXPECT_NE(si.status, MidMpcSolution::Status::NumericalFailure);
  EXPECT_NE(si.status, MidMpcSolution::Status::Infeasible)
      << "IPOPT reported Infeasible on the benign straight-line scenario "
      << "(status=" << static_cast<int>(si.status) << ")";
  EXPECT_NE(sa.status, MidMpcSolution::Status::NotInitialized);
  EXPECT_NE(sa.status, MidMpcSolution::Status::NumericalFailure);
  EXPECT_NE(sa.status, MidMpcSolution::Status::Infeasible)
      << "acados reported Infeasible on the benign straight-line scenario "
      << "(status=" << static_cast<int>(sa.status) << ")";

  // cost_total / solve_duration_ms / cpa_slack accessible on both (the
  // downstream contract). Finiteness, not value parity — the two physics
  // produce different cost landscapes.
  EXPECT_TRUE(std::isfinite(si.cost_total)) << "ipopt cost_total not finite";
  EXPECT_TRUE(std::isfinite(sa.cost_total)) << "acados cost_total not finite";
  EXPECT_GE(si.solve_duration_ms, 0);
  EXPECT_GE(sa.solve_duration_ms, 0);
  EXPECT_TRUE(std::isfinite(si.cpa_slack)) << "ipopt cpa_slack not finite";
  EXPECT_TRUE(std::isfinite(sa.cpa_slack)) << "acados cpa_slack not finite";
}

// ---------------------------------------------------------------------------
// Assertion 2 — StraightLine_BothHoldCourse.
//
// Behavioral equivalence on a non-aggressive scenario: BOTH trajectories' psi
// stay close to planned_route_bearing_rad (< 0.15 rad ≈ 8.6° deviation). NOT
// bit-close — the two physics differ (IPOPT kinematics vs acatos double-
// integrator), so the trajectories will not match numerically. But on a no-
// target straight-line scenario, the cost-optimum for BOTH backends is to
// hold the route bearing (J_dist=0 at psi=planned, J_vel=0 at u=planned,
// J_route=0 at own on the leg). A backend that deflects >0.15 rad here has
// either a modeling bug or a cost-landscape divergence — important evidence
// for the TDL Lead (do NOT loosen the 0.15 gate; report it instead).
//
// Wrap psi deviation to [-π, π] before comparing (the trajectory may legitimately
// sit near ±π for a south-bearing route; here planned=0 so unwrapped is fine,
// but the wrap is defensive against route-bearing changes in future scenarios).
// ---------------------------------------------------------------------------
TEST_F(MidMpcAcadosParityTest, StraightLine_BothHoldCourse) {
  const MidMpcInput in = straight_line_input();
  const MidMpcSolution si = ipopt_->solve(in, nullptr);
  const MidMpcSolution sa = acados_->solve(in, nullptr);

  // Gate on usable status — a deflection check on a failed solve is meaningless.
  ASSERT_NE(si.status, MidMpcSolution::Status::NotInitialized);
  ASSERT_NE(si.status, MidMpcSolution::Status::NumericalFailure);
  ASSERT_NE(si.status, MidMpcSolution::Status::Infeasible);
  ASSERT_NE(sa.status, MidMpcSolution::Status::NotInitialized);
  ASSERT_NE(sa.status, MidMpcSolution::Status::NumericalFailure);
  ASSERT_NE(sa.status, MidMpcSolution::Status::Infeasible);
  ASSERT_FALSE(si.trajectory.empty());
  ASSERT_FALSE(sa.trajectory.empty());

  constexpr double kMaxDeviationRad = 0.15;  // ~8.6° (spec gate 3, NOT bit-close)
  const double bearing = in.planned_route_bearing_rad;

  for (std::size_t k = 0; k < si.trajectory.size(); ++k) {
    const double dpsi_i = std::atan2(
        std::sin(si.trajectory[k].psi_rad - bearing),
        std::cos(si.trajectory[k].psi_rad - bearing));
    EXPECT_LT(std::fabs(dpsi_i), kMaxDeviationRad)
        << "IPOPT psi[" << k << "] deviates " << (dpsi_i * 180.0 / M_PI)
        << "deg from route bearing (limit " << (kMaxDeviationRad * 180.0 / M_PI)
        << "deg)";
  }
  for (std::size_t k = 0; k < sa.trajectory.size(); ++k) {
    const double dpsi_a = std::atan2(
        std::sin(sa.trajectory[k].psi_rad - bearing),
        std::cos(sa.trajectory[k].psi_rad - bearing));
    EXPECT_LT(std::fabs(dpsi_a), kMaxDeviationRad)
        << "acados psi[" << k << "] deviates " << (dpsi_a * 180.0 / M_PI)
        << "deg from route bearing (limit " << (kMaxDeviationRad * 180.0 / M_PI)
        << "deg)";
  }
}

// ---------------------------------------------------------------------------
// Assertion 3 — OutputContract_FieldsMatch.
//
// Same field NAMES and TYPES present in both solutions. This is a STATIC
// contract check (the type is shared — MidMpcSolution is one struct — so the
// fields are byte-identical by construction) PLUS a runtime finiteness check
// that both backends actually POPULATED them. The static_asserts document the
// contract; the runtime checks prove the backends honored it on this scenario.
//
// Fields (per types.hpp):
//   TrajectoryPoint: psi_rad, u_mps, x_m, y_m (double)
//   MidMpcSolution:  status (Status enum), cost_total (double),
//                    cpa_slack (double), solve_duration_ms (int32),
//                    ipopt_iterations (int32 — name preserved for acatos per
//                    T17, holds SQP iter count).
// ---------------------------------------------------------------------------
TEST_F(MidMpcAcadosParityTest, OutputContract_FieldsMatch) {
  // ---- STATIC contract (compile-time): both backends produce MidMpcSolution.
  // If a backend returned a different type, this test would not compile. The
  // static_asserts pin the field NAMES + TYPES so a future contract drift
  // surfaces at compile time.
  static_assert(std::is_same_v<
                    decltype(std::declval<MidMpcSolution>().status),
                    MidMpcSolution::Status>,
                "MidMpcSolution.status must be MidMpcSolution::Status");
  static_assert(std::is_same_v<
                    decltype(std::declval<MidMpcSolution>().cost_total),
                    double>,
                "MidMpcSolution.cost_total must be double");
  static_assert(std::is_same_v<
                    decltype(std::declval<MidMpcSolution>().cpa_slack),
                    double>,
                "MidMpcSolution.cpa_slack must be double");
  static_assert(std::is_same_v<
                    decltype(std::declval<MidMpcSolution>().solve_duration_ms),
                    std::int32_t>,
                "MidMpcSolution.solve_duration_ms must be int32_t");
  static_assert(std::is_same_v<
                    decltype(std::declval<MidMpcSolution>().ipopt_iterations),
                    std::int32_t>,
                "MidMpcSolution.ipopt_iterations must be int32_t (name preserved "
                "for acatos; holds SQP iter count)");
  static_assert(std::is_same_v<
                    decltype(std::declval<TrajectoryPoint>().psi_rad),
                    double>,
                "TrajectoryPoint.psi_rad must be double");
  static_assert(std::is_same_v<
                    decltype(std::declval<TrajectoryPoint>().u_mps),
                    double>,
                "TrajectoryPoint.u_mps must be double");
  static_assert(std::is_same_v<
                    decltype(std::declval<TrajectoryPoint>().x_m),
                    double>,
                "TrajectoryPoint.x_m must be double");
  static_assert(std::is_same_v<
                    decltype(std::declval<TrajectoryPoint>().y_m),
                    double>,
                "TrajectoryPoint.y_m must be double");

  // ---- RUNTIME contract: both backends POPULATED the fields on this scenario.
  const MidMpcInput in = straight_line_input();
  const MidMpcSolution si = ipopt_->solve(in, nullptr);
  const MidMpcSolution sa = acados_->solve(in, nullptr);

  // Gate on usable status — field-population checks are only meaningful when
  // the solve actually produced a trajectory.
  ASSERT_NE(si.status, MidMpcSolution::Status::NotInitialized);
  ASSERT_NE(si.status, MidMpcSolution::Status::NumericalFailure);
  ASSERT_NE(si.status, MidMpcSolution::Status::Infeasible);
  ASSERT_NE(sa.status, MidMpcSolution::Status::NotInitialized);
  ASSERT_NE(sa.status, MidMpcSolution::Status::NumericalFailure);
  ASSERT_NE(sa.status, MidMpcSolution::Status::Infeasible);
  ASSERT_FALSE(si.trajectory.empty());
  ASSERT_FALSE(sa.trajectory.empty());

  // Per-point fields finite on both (psi/u/x/y).
  for (const auto& p : si.trajectory) {
    EXPECT_TRUE(std::isfinite(p.psi_rad)) << "ipopt psi_rad not finite";
    EXPECT_TRUE(std::isfinite(p.u_mps))   << "ipopt u_mps not finite";
    EXPECT_TRUE(std::isfinite(p.x_m))     << "ipopt x_m not finite";
    EXPECT_TRUE(std::isfinite(p.y_m))     << "ipopt y_m not finite";
  }
  for (const auto& p : sa.trajectory) {
    EXPECT_TRUE(std::isfinite(p.psi_rad)) << "acados psi_rad not finite";
    EXPECT_TRUE(std::isfinite(p.u_mps))   << "acados u_mps not finite";
    EXPECT_TRUE(std::isfinite(p.x_m))     << "acados x_m not finite";
    EXPECT_TRUE(std::isfinite(p.y_m))     << "acados y_m not finite";
  }

  // Solution-level fields finite / non-negative (where applicable) on both.
  EXPECT_TRUE(std::isfinite(si.cost_total));
  EXPECT_TRUE(std::isfinite(sa.cost_total));
  EXPECT_GE(si.solve_duration_ms, 0);
  EXPECT_GE(sa.solve_duration_ms, 0);
  EXPECT_TRUE(std::isfinite(si.cpa_slack));
  EXPECT_TRUE(std::isfinite(sa.cpa_slack));
  EXPECT_GE(si.cpa_slack, 0.0);  // slack is non-negative by construction
  EXPECT_GE(sa.cpa_slack, 0.0);
  // ipopt_iterations: IPOPT fills the actual iter count; acatos fills the SQP
  // iter count. Both must be >= 0 (the field name stays ipopt_iterations for
  // downstream compatibility — T17 finding).
  EXPECT_GE(si.ipopt_iterations, 0);
  EXPECT_GE(sa.ipopt_iterations, 0);
}
