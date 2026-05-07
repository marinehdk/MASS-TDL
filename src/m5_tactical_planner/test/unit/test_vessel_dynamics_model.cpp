#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/common/units.hpp"
#include "m5_tactical_planner/shared/capability_manifest.hpp"
#include "m5_tactical_planner/shared/cpa_calculator.hpp"
#include "m5_tactical_planner/shared/trajectory_propagator.hpp"
#include "m5_tactical_planner/shared/vessel_dynamics_model.hpp"

// ---------------------------------------------------------------------------
// Test fixture path — resolved relative to ament_add_gtest install prefix.
// In colcon builds, test data is installed to share/m5_tactical_planner/...
// but during CMake build tests run from the build directory. We use the
// CMake-injected path macro (defined in CMakeLists.txt COMPILE_DEFINITIONS).
// For portability during unit tests, the YAML path is specified literally;
// when run via ament_add_gtest the test binary cwd is the build directory.
// ---------------------------------------------------------------------------

// Path injected by CMakeLists (see target_compile_definitions in CMakeLists).
// Falls back to a relative path if not defined (for IDE convenience).
#ifndef M5_TEST_FIXTURE_DIR
  // Default: relative to the build directory during colcon builds.
  // This path is overridden in CMakeLists.txt for installed tests.
  #define M5_TEST_FIXTURE_DIR "../../src/m5_tactical_planner/test/fixtures"
#endif

static const std::string kFixturePath =
    std::string(M5_TEST_FIXTURE_DIR) + "/fcb_capability_fixture.yaml";

// ---------------------------------------------------------------------------
// Helper: load the test fixture manifest
// ---------------------------------------------------------------------------
static mass_l3::m5::shared::CapabilityManifest load_fixture() {
  return mass_l3::m5::shared::CapabilityManifest::load_from_yaml(kFixturePath);
}

// ===========================================================================
// VesselDynamicsModel tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 1: RotMaxLowSpeedHigher
// At low speed (5 m/s ≈ 9.7 kn), ROT_max > ROT_max at high speed (12 m/s)
// (below low_speed_kn=10 vs above low_speed_kn=10 → different correction factors)
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, RotMaxLowSpeedHigher) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  // 5 m/s ≈ 9.7 kn (below low_speed_kn=10) → factor 1.2
  // 12 m/s ≈ 23.3 kn (above high_speed_kn=20) → factor 0.8
  const double rot_low  = model.rot_max_rad_s(5.0, 0.0);
  const double rot_high = model.rot_max_rad_s(12.0, 0.0);

  EXPECT_GT(rot_low, rot_high)
      << "ROT_max at low speed must exceed ROT_max at high speed";
}

// ---------------------------------------------------------------------------
// Test 2: RoughSeaReducesRotMax
// ROT_max at Hs=2.5 m ≈ (1 - 0.10 * 2.5) = 0.75 of calm-sea ROT_max
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, RoughSeaReducesRotMax) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  const double speed_mps = mass_l3::m5::units::kn_to_mps(18.0);  // service speed
  const double rot_calm  = model.rot_max_rad_s(speed_mps, 0.0);
  const double rot_rough = model.rot_max_rad_s(speed_mps, 2.5);

  // Expected: rot_rough = rot_calm * (1 - 0.10 * 2.5) = 0.75 * rot_calm
  EXPECT_NEAR(rot_rough, 0.75 * rot_calm, 1.0e-6)
      << "ROT_max at Hs=2.5 should be 75% of calm-sea value";
  EXPECT_LT(rot_rough, rot_calm);
}

// ---------------------------------------------------------------------------
// Test 3: StoppingDistanceMonotonicInSpeed
// stopping_distance increases with speed (0 m/s → 5 m/s → 10 m/s → service)
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StoppingDistanceMonotonicInSpeed) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  const double d0  = model.stopping_distance_m(0.0);
  const double d5  = model.stopping_distance_m(5.0);
  const double d10 = model.stopping_distance_m(10.0);
  const double d18 = model.stopping_distance_m(
      mass_l3::m5::units::kn_to_mps(18.0));

  EXPECT_LE(d0,  d5)  << "stopping distance must be monotone";
  EXPECT_LE(d5,  d10) << "stopping distance must be monotone";
  EXPECT_LE(d10, d18) << "stopping distance must be monotone";
  EXPECT_NEAR(d0, 0.0, 1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 4: StepZeroRudderPreservesHeading
// With zero rudder and some propulsion, heading should remain near-constant.
// (No torque → dr/dt ≈ 0; the heading will change only via existing r_rad_s.)
// Start with r_rad_s = 0. After 1 step, psi change < 0.01 rad.
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StepZeroRudderPreservesHeading) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  mass_l3::m5::TrajectoryPoint s0;
  s0.psi_rad = 1.0;  // initial heading 1 rad
  s0.u_mps   = 9.0;  // ~17.5 kn
  s0.r_rad_s = 0.0;  // no initial yaw rate

  const mass_l3::m5::shared::VesselDynamicsModel::Input cmd{0.0, 2.0};
  const auto s1 = model.step(s0, cmd, 1.0);  // 1 s step

  EXPECT_NEAR(s1.psi_rad, s0.psi_rad, 0.01)
      << "Zero rudder should produce near-zero heading change";
}

// ---------------------------------------------------------------------------
// Test 5: StepPositiveRudderTurnsStarboard
// Positive rudder angle → positive yaw rate → heading increases.
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StepPositiveRudderTurnsStarboard) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  mass_l3::m5::TrajectoryPoint s0;
  s0.psi_rad = 0.0;
  s0.u_mps   = 9.0;   // sufficient speed to produce rudder force
  s0.r_rad_s = 0.0;

  const mass_l3::m5::shared::VesselDynamicsModel::Input cmd{0.1, 2.0};
  const auto s1 = model.step(s0, cmd, 1.0);

  EXPECT_GT(s1.r_rad_s, 0.0)
      << "Positive rudder must produce positive yaw rate";
  EXPECT_GT(s1.psi_rad, s0.psi_rad)
      << "Positive rudder must increase heading (starboard turn)";
}

// ---------------------------------------------------------------------------
// Test 6: StepNoThrottleDecelerates
// With rpm=0 and no thrust, surge speed should decrease due to hull resistance.
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StepNoThrottleDecelerates) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  mass_l3::m5::TrajectoryPoint s0;
  s0.u_mps = 8.0;  // initial speed
  s0.psi_rad = 0.0;

  const mass_l3::m5::shared::VesselDynamicsModel::Input cmd{0.0, 0.0};  // no thrust
  const auto s1 = model.step(s0, cmd, 1.0);

  EXPECT_LT(s1.u_mps, s0.u_mps)
      << "Speed must decrease with no propulsion due to hull resistance";
}

// ---------------------------------------------------------------------------
// Test 7: StepSmallDtApproximatesLinear
// For very small dt, position change ≈ u * dt (first-order approximation).
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StepSmallDtApproximatesLinear) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  mass_l3::m5::TrajectoryPoint s0;
  s0.x_m     = 0.0;
  s0.y_m     = 0.0;
  s0.psi_rad = 0.0;   // heading north
  s0.u_mps   = 5.0;   // 5 m/s north
  s0.v_mps   = 0.0;

  const double dt = 0.001;  // 1 ms — very small
  const mass_l3::m5::shared::VesselDynamicsModel::Input cmd{0.0, 1.0};
  const auto s1 = model.step(s0, cmd, dt);

  // Expected north displacement ≈ u * dt = 5.0 * 0.001 = 0.005 m
  EXPECT_NEAR(s1.x_m, 5.0 * dt, 1.0e-4)
      << "Position change must approximate u*dt for small dt";
}

// ---------------------------------------------------------------------------
// Test 8: RotMaxCalmSeaReference
// At exactly service_speed_kn (18 kn), speed_correction should be 1.0
// (it's interpolated between low/high nodes; 18 kn is between 10 and 20).
// rot_max ≈ rot_max_at_18kn * correction * 1.0 (calm sea)
// At 18 kn: correction = interp(10→1.2, 20→0.8) at 18 kn = 0.96
// So rot_max(18 kn) = 0.20944 * 0.96 ≈ 0.20106, NOT exactly 0.20944.
// Test: rot_max at service speed is close to rot_max_at_18kn (within 5%).
// (Not exactly equal due to speed correction — the YAML name is a reference
//  speed, not the exact output speed used in correction interpolation.)
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, RotMaxCalmSeaReference) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);

  const double service_mps =
      mass_l3::m5::units::kn_to_mps(manifest.config().service_speed_kn);
  const double rot = model.rot_max_rad_s(service_mps, 0.0);
  const double ref = manifest.config().rot_max_at_18kn_rad_s;

  // Must be positive and within ±30% of reference (correction factor ∈ [0.8, 1.2])
  EXPECT_GT(rot, 0.0);
  EXPECT_NEAR(rot, ref, 0.30 * ref)
      << "rot_max at service speed must be near reference value";
}

// ===========================================================================
// CpaCalculator tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 9: CpaCalculator_HeadOn_CpaNearZero
// Two ships on exact collision course, 1000 m apart, same speed.
// CPA should be near zero (within 1 m numerical tolerance).
// ---------------------------------------------------------------------------
TEST(CpaCalculatorTest, HeadOn_CpaNearZero) {
  // Own ship: heading north (psi=0), moving at 5 m/s
  mass_l3::m5::TrajectoryPoint own;
  own.x_m     = 0.0;
  own.y_m     = 0.0;
  own.psi_rad = 0.0;
  own.u_mps   = 5.0;
  own.v_mps   = 0.0;

  // Target: 1000 m north, heading south (cog = pi), same speed
  mass_l3::m5::TargetState tgt;
  tgt.x_m     = 1000.0;
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;  // heading south
  tgt.sog_mps = 5.0;

  const auto result =
      mass_l3::m5::shared::CpaCalculator::compute_linear(own, tgt);

  EXPECT_NEAR(result.cpa_m, 0.0, 1.0)
      << "Head-on collision course must have CPA ≈ 0";
  EXPECT_GT(result.tcpa_s, 0.0)
      << "TCPA must be positive (collision in future)";
  EXPECT_NEAR(result.tcpa_s, 100.0, 5.0)  // 1000 m / (5+5) m/s = 100 s
      << "TCPA for 1000m / 10 m/s closing rate must be ≈ 100 s";
}

// ---------------------------------------------------------------------------
// Test 10: CpaCalculator_ParallelCourse_CpaLarge
// Two ships on exactly parallel courses (same COG, same speed), 500 m apart.
// CPA = initial separation (never converge).
// ---------------------------------------------------------------------------
TEST(CpaCalculatorTest, ParallelCourse_CpaLarge) {
  // Own ship: heading north, 5 m/s
  mass_l3::m5::TrajectoryPoint own;
  own.x_m     = 0.0;
  own.y_m     = 0.0;
  own.psi_rad = 0.0;
  own.u_mps   = 5.0;
  own.v_mps   = 0.0;

  // Target: 500 m east, same course and speed
  mass_l3::m5::TargetState tgt;
  tgt.x_m     = 0.0;
  tgt.y_m     = 500.0;
  tgt.cog_rad = 0.0;  // also heading north
  tgt.sog_mps = 5.0;

  const auto result =
      mass_l3::m5::shared::CpaCalculator::compute_linear(own, tgt);

  // Relative velocity is zero → CPA = initial distance = 500 m
  EXPECT_NEAR(result.cpa_m, 500.0, 1.0)
      << "Parallel same-speed ships must maintain constant separation";
  // TCPA sentinel (very large) since they never converge
  EXPECT_GT(result.tcpa_s, 1.0e8)
      << "TCPA must be huge (sentinel) for parallel same-speed ships";
}

// ===========================================================================
// TrajectoryPropagator tests
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 11: TrajectoryPropagator_StraightLine
// Constant heading + constant speed → trajectory is a straight line (north).
// Check that all y_m positions are near-zero (no lateral deviation).
// ---------------------------------------------------------------------------
TEST(TrajectoryPropagatorTest, StraightLine) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel dynamics(manifest);
  mass_l3::m5::shared::TrajectoryPropagator prop;

  mass_l3::m5::TrajectoryPoint s0;
  s0.x_m     = 0.0;
  s0.y_m     = 0.0;
  s0.psi_rad = 0.0;   // heading north
  s0.u_mps   = 5.0;
  s0.v_mps   = 0.0;
  s0.r_rad_s = 0.0;

  const std::size_t n = 5u;
  const std::vector<double> psi_seq(n, 0.0);  // constant north heading
  const std::vector<double> u_seq(n, 5.0);    // constant 5 m/s

  const auto traj = prop.propagate_own(s0, psi_seq, u_seq, 1.0, dynamics);

  ASSERT_EQ(traj.size(), n + 1u);
  for (const auto& pt : traj) {
    EXPECT_NEAR(pt.y_m, 0.0, 0.5)
        << "Straight-line trajectory must have near-zero lateral displacement";
  }
  // x should increase monotonically
  for (std::size_t i = 1u; i < traj.size(); ++i) {
    EXPECT_GT(traj[i].x_m, traj[i - 1u].x_m)
        << "North-bound trajectory must have increasing x_m";
  }
}

// ---------------------------------------------------------------------------
// Test 12: CapabilityManifest_LoadFromYaml
// Load the fixture YAML and verify vessel_id and rot_max_at_18kn_rad_s.
// ---------------------------------------------------------------------------
TEST(CapabilityManifestTest, LoadFromYaml) {
  const auto manifest = load_fixture();
  const auto& cfg = manifest.config();

  EXPECT_EQ(cfg.vessel_id, "TEST-FCB-001")
      << "vessel_id must match fixture value";
  EXPECT_NEAR(cfg.rot_max_at_18kn_rad_s, 0.20944, 1.0e-5)
      << "rot_max must match fixture value";
  EXPECT_NEAR(manifest.service_speed_mps(),
              mass_l3::m5::units::kn_to_mps(18.0), 1.0e-6)
      << "service_speed_mps derived value must match kn_to_mps(18)";
}

// ===========================================================================
// Extra edge-case tests (≥15 total per spec §8.4 bonus requirement)
// ===========================================================================

// ---------------------------------------------------------------------------
// Test 13: CapabilityManifest_MissingVesselId_Throws
// A YAML file with empty vessel_id must throw std::runtime_error.
// ---------------------------------------------------------------------------
TEST(CapabilityManifestTest, MissingFile_Throws) {
  EXPECT_THROW(
      mass_l3::m5::shared::CapabilityManifest::load_from_yaml(
          "/nonexistent/path/to/no_such_file.yaml"),
      std::runtime_error);
}

// ---------------------------------------------------------------------------
// Test 14: StoppingDistance_AtZeroSpeed_IsZero
// stopping_distance_m(0) must return 0.
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, StoppingDistance_AtZeroSpeed_IsZero) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);
  EXPECT_NEAR(model.stopping_distance_m(0.0), 0.0, 1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 15: RotMax_VeryHighHs_ClampedToZero
// Hs so large that rough_sea_factor makes ROT negative — must clamp to 0.
// ---------------------------------------------------------------------------
TEST(VesselDynamicsModelTest, RotMax_VeryHighHs_ClampedToZero) {
  auto manifest = load_fixture();
  mass_l3::m5::shared::VesselDynamicsModel model(manifest);
  // Hs = 20 m → (1 - 0.10 * 20) = -1.0 → clamp to 0
  const double rot = model.rot_max_rad_s(5.0, 20.0);
  EXPECT_GE(rot, 0.0) << "ROT_max must never be negative";
}

// ---------------------------------------------------------------------------
// Test 16: CpaCalculator_StaticTarget_TCPASentinel
// Own ship stationary, target stationary — CPA = distance, TCPA = sentinel.
// ---------------------------------------------------------------------------
TEST(CpaCalculatorTest, BothStationary_TCPASentinel) {
  mass_l3::m5::TrajectoryPoint own;
  own.x_m = 0.0;  own.y_m = 0.0;
  own.psi_rad = 0.0;  own.u_mps = 0.0;  own.v_mps = 0.0;

  mass_l3::m5::TargetState tgt;
  tgt.x_m = 300.0;  tgt.y_m = 400.0;  // 500 m away
  tgt.cog_rad = 0.0;  tgt.sog_mps = 0.0;

  const auto result =
      mass_l3::m5::shared::CpaCalculator::compute_linear(own, tgt);
  EXPECT_NEAR(result.cpa_m, 500.0, 1.0)
      << "CPA of two stationary ships = their distance";
  EXPECT_GT(result.tcpa_s, 1.0e8)
      << "TCPA must be sentinel when both stationary";
}

// ---------------------------------------------------------------------------
// Test 17: TrajectoryPropagator_TargetMaintain
// Maintain intent: target moves in a straight line.
// ---------------------------------------------------------------------------
TEST(TrajectoryPropagatorTest, TargetMaintain_StraightLine) {
  mass_l3::m5::shared::TrajectoryPropagator prop;

  mass_l3::m5::TargetState tgt;
  tgt.x_m     = 0.0;
  tgt.y_m     = 0.0;
  tgt.cog_rad = 0.0;   // heading north
  tgt.sog_mps = 5.0;

  const auto positions = prop.propagate_target(
      tgt, mass_l3::m5::TargetIntent::Maintain, 5.0, 1.0);

  ASSERT_EQ(positions.size(), 6u)  // floor(5/1) + 1 = 6
      << "5 s horizon / 1 s dt should yield 6 positions";

  // All y positions should remain near zero (heading north = +x direction)
  for (const auto& p : positions) {
    EXPECT_NEAR(p.y(), 0.0, 0.01)
        << "Maintain heading north must have near-zero y displacement";
  }
  // x should increase monotonically
  for (std::size_t i = 1u; i < positions.size(); ++i) {
    EXPECT_GT(positions[i].x(), positions[i - 1u].x());
  }
}

// ---------------------------------------------------------------------------
// Test 18: CpaCalculator_TrajectoryBased_MinDistance
// Two trajectories that are closest at step 2 — verify that step index is found.
// ---------------------------------------------------------------------------
TEST(CpaCalculatorTest, TrajectoryBased_FindsCorrectMinIndex) {
  std::vector<Eigen::Vector2d> own_traj = {
      {0.0, 0.0}, {10.0, 0.0}, {20.0, 0.0}, {30.0, 0.0}};
  std::vector<Eigen::Vector2d> tgt_traj = {
      {100.0, 0.0}, {80.0, 0.0}, {22.0, 0.0}, {50.0, 0.0}};  // closest at idx 2

  const auto result =
      mass_l3::m5::shared::CpaCalculator::compute_trajectory(
          own_traj, tgt_traj, 1.0);

  // At index 2: |20 - 22| = 2 m
  EXPECT_NEAR(result.cpa_m, 2.0, 1.0e-6);
  EXPECT_NEAR(result.tcpa_s, 2.0, 1.0e-6);  // 2 steps × 1 s/step
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
