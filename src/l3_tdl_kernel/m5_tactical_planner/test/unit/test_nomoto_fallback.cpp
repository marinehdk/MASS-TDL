#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

#include "m5_tactical_planner/common/types.hpp"
#include "m5_tactical_planner/mid_mpc/nomoto_fallback.hpp"
#include "m5_tactical_planner/shared/capability_manifest.hpp"
#include "m5_tactical_planner/shared/cpa_calculator.hpp"

using mass_l3::m5::MidMpcInput;
using mass_l3::m5::TargetState;
using mass_l3::m5::TrajectoryPoint;
using mass_l3::m5::mid_mpc::NomotoFallback;
using mass_l3::m5::mid_mpc::NomotoFallbackConfig;
using mass_l3::m5::mid_mpc::NomotoFallbackSolution;
using mass_l3::m5::shared::CapabilityManifest;

// ---------------------------------------------------------------------------
// Test fixture path — follows existing test convention in this project.
// ---------------------------------------------------------------------------
#ifndef M5_TEST_FIXTURE_DIR
  #define M5_TEST_FIXTURE_DIR "../../src/m5_tactical_planner/test/fixtures"
#endif

static const std::string kFixturePath =
    std::string(M5_TEST_FIXTURE_DIR) + "/fcb_capability_fixture.yaml";

// ---------------------------------------------------------------------------
// Helper: load the test fixture manifest
// ---------------------------------------------------------------------------
static CapabilityManifest load_fixture() {
  return CapabilityManifest::load_from_yaml(kFixturePath);
}

// ---------------------------------------------------------------------------
// Helper: build a basic MidMpcInput with own ship at origin, heading north,
// 5 m/s, and (optionally) a single head-on target.
// ---------------------------------------------------------------------------
static MidMpcInput make_input_no_targets() {
  MidMpcInput inp;
  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  inp.own_ship.psi_rad = 0.0;  // heading north
  inp.own_ship.u_mps   = 5.0;
  inp.own_ship.v_mps   = 0.0;
  inp.own_ship.r_rad_s = 0.0;
  inp.planned_speed_mps = 5.0;
  return inp;
}

// ===========================================================================
// Test 1 — IntegrateStraightLine
// Center branch at psi=0, u=5 m/s, n_steps=12, dt=5 s.
// The vessel holds heading north at constant speed.
// Final north position should be ~300 m, east position ~0 m.
// ===========================================================================
TEST(NomotoFallbackTest, IntegrateStraightLine) {
  const auto manifest = load_fixture();

  NomotoFallbackConfig cfg;
  cfg.n_steps = 12;
  cfg.dt_s    = 5.0;
  cfg.n_branches = 1;

  NomotoFallback fallback(cfg, manifest);
  const auto sol = fallback.solve(make_input_no_targets());

  ASSERT_EQ(sol.trajectories.size(), 1u);
  const auto& traj = sol.trajectories[0];

  // N + 1 = 13 positions (start + 12 steps)
  ASSERT_EQ(traj.size(), 13u);

  // First position is the origin
  EXPECT_NEAR(traj[0].x(), 0.0, 1.0e-9);
  EXPECT_NEAR(traj[0].y(), 0.0, 1.0e-9);

  // Last position: 12 steps × 5 s × 5 m/s = 300 m north, 0 m east
  EXPECT_NEAR(traj.back().x(), 300.0, 0.5);
  EXPECT_NEAR(traj.back().y(), 0.0, 0.5);

  // Positions should be monotonically increasing in x
  for (std::size_t i = 1u; i < traj.size(); ++i) {
    EXPECT_GT(traj[i].x(), traj[i - 1u].x());
  }
}

// ===========================================================================
// Test 2 — BranchesCoverFullRange
// 13 branches with delta_psi = 10 deg → range is -60 deg to +60 deg.
// ===========================================================================
TEST(NomotoFallbackTest, BranchesCoverFullRange) {
  const auto manifest = load_fixture();
  const auto input    = make_input_no_targets();

  NomotoFallbackConfig cfg;
  cfg.n_steps     = 12;
  cfg.dt_s        = 5.0;
  cfg.n_branches  = 13;
  cfg.delta_psi_rad = 10.0 * 0.0174533;  // 10 degrees

  NomotoFallback fallback(cfg, manifest);
  const auto sol = fallback.solve(input);

  ASSERT_EQ(sol.headings_rad.size(), 13u);
  ASSERT_EQ(sol.cpa_vals.size(), 13u);
  ASSERT_EQ(sol.trajectories.size(), 13u);

  // Primary branch is the center (index 6 for 13 branches)
  EXPECT_EQ(sol.primary_branch_idx, 6);

  // First branch: -60 deg ≈ -1.0472 rad
  EXPECT_NEAR(sol.headings_rad[0], -1.0472, 0.001);

  // Center branch: 0 rad
  EXPECT_NEAR(sol.headings_rad[6], 0.0, 0.001);

  // Last branch: +60 deg ≈ +1.0472 rad
  EXPECT_NEAR(sol.headings_rad[12], 1.0472, 0.001);

  // Without targets, all CPA values remain at the sentinel (1e9)
  for (std::size_t i = 0u; i < sol.cpa_vals.size(); ++i) {
    EXPECT_GT(sol.cpa_vals[i], 1.0e8);
  }
}

// ===========================================================================
// Test 3 — CpaWithTarget
// Head-on target 500 m north of own ship, coming south at 5 m/s.
// Center branch (heading straight north) has near-zero CPA (collision course).
// Side branches (turning away) have larger CPA.
// ===========================================================================
TEST(NomotoFallbackTest, CpaWithTarget) {
  const auto manifest = load_fixture();

  // Own ship at origin, heading north, 5 m/s
  MidMpcInput input = make_input_no_targets();

  // Head-on target: 500 m north, heading south, 5 m/s
  TargetState tgt;
  tgt.id      = 1;
  tgt.x_m     = 500.0;
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;   // heading south (directly toward own ship)
  tgt.sog_mps = 5.0;
  input.targets.push_back(tgt);

  NomotoFallbackConfig cfg;
  cfg.n_steps     = 12;
  cfg.dt_s        = 5.0;
  cfg.n_branches  = 13;
  cfg.delta_psi_rad = 10.0 * 0.0174533;

  NomotoFallback fallback(cfg, manifest);
  const auto sol = fallback.solve(input);

  // Center branch (index 6): heading straight north → collision course → CPA ≈ 0
  EXPECT_NEAR(sol.cpa_vals[6], 0.0, 1.0);

  // The extreme side branches (turning far left/right) should have
  // significantly better CPA than the center branch.
  // With the target head-on at 500 m, a 60-degree turn gives CPA > 200 m.
  EXPECT_GT(sol.cpa_vals[0], sol.cpa_vals[6] + 100.0);
  EXPECT_GT(sol.cpa_vals[12], sol.cpa_vals[6] + 100.0);

  // Verify trajectories exist and have correct dimensions
  for (std::size_t i = 0u; i < sol.trajectories.size(); ++i) {
    // Each trajectory has n_steps + 1 = 13 positions
    ASSERT_EQ(sol.trajectories[i].size(), 13u);
  }
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
