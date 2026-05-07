#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_override_generator.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_solver.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::BcMpcInput;
using mass_l3::m5::BcMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::bc_mpc::BcMpcBranchFormulation;
using mass_l3::m5::bc_mpc::BcMpcOverrideGenerator;
using mass_l3::m5::bc_mpc::BcMpcSolver;

static BcMpcInput make_base_input()
{
  BcMpcInput inp;
  inp.own_ship.psi_rad              = 0.0;
  inp.own_ship.u_mps                = 5.0;
  inp.own_ship.x_m                  = 0.0;
  inp.own_ship.y_m                  = 0.0;
  inp.cpa_safe_m                    = 1852.0;
  inp.mid_mpc_consecutive_failures  = 0;
  inp.predicted_short_horizon_cpa_m = 1.0e6;
  return inp;
}

// ---------------------------------------------------------------------------
// Test 1 — No targets → solver returns Resolved.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, NoTargets_StatusResolved)
{
  BcMpcBranchFormulation form;
  BcMpcSolver solver{form};

  const auto sol = solver.solve(make_base_input());

  EXPECT_EQ(sol.status, BcMpcSolution::Status::Resolved);
}

// ---------------------------------------------------------------------------
// Test 2 — Head-on threat at 400 m → solver returns Override.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, HeadOn_StatusOverride)
{
  BcMpcBranchFormulation form;
  BcMpcSolver solver{form};

  BcMpcInput inp = make_base_input();
  inp.predicted_short_horizon_cpa_m = 100.0;  // well inside safe distance

  TargetState tgt;
  tgt.id      = 1;
  tgt.x_m     = 400.0;   // 400 m north
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;    // heading south — directly toward own ship
  tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);

  const auto sol = solver.solve(inp);

  EXPECT_EQ(sol.status, BcMpcSolution::Status::Override);
}

// ---------------------------------------------------------------------------
// Test 3 — Speed passthrough: optimal_speed_mps equals input u_mps.
// Phase E1 holds current speed; solver must not modify it.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, Solver_SetsSpeedFromInput)
{
  BcMpcBranchFormulation form;
  BcMpcSolver solver{form};

  BcMpcInput inp = make_base_input();
  inp.own_ship.u_mps = 7.5;

  const auto sol = solver.solve(inp);

  EXPECT_DOUBLE_EQ(sol.optimal_speed_mps, 7.5);
}

// ---------------------------------------------------------------------------
// Test 4 — solve_duration_us is positive after a solve.
// The detector always does branch evaluation work, so wall-clock > 0.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, Solver_SetsSolveDuration)
{
  BcMpcBranchFormulation form;
  BcMpcSolver solver{form};

  const auto sol = solver.solve(make_base_input());

  EXPECT_GT(sol.solve_duration_us, 0);
}

// ---------------------------------------------------------------------------
// Test 5 — OverrideGenerator heading conversion: π/2 rad → ≈ 90 deg.
// Validates the rad-to-deg boundary conversion and [0, 360) normalisation.
// ---------------------------------------------------------------------------
TEST(BcMpcOverrideGenerator, HeadingConversion)
{
  BcMpcOverrideGenerator gen;

  BcMpcSolution sol;
  sol.status           = BcMpcSolution::Status::Override;
  sol.heading_cmd_rad  = M_PI / 2.0;
  sol.optimal_speed_mps = 5.0;
  sol.rot_cmd_rad_s    = 0.0;
  sol.validity_s       = 1.0;
  sol.confidence       = 0.8;
  sol.trigger_reason   = "CONDITION_A";

  const rclcpp::Time t{0, 0, RCL_ROS_TIME};
  const auto cmd = gen.generate(sol, t);

  EXPECT_NEAR(static_cast<double>(cmd.heading_cmd_deg), 90.0, 0.001);
}
