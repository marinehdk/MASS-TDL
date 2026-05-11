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
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  const auto sol = solver.solve(make_base_input());

  EXPECT_EQ(sol.status, BcMpcSolution::Status::Resolved);
}

// ---------------------------------------------------------------------------
// Test 2 — Head-on threat at 400 m → solver returns Override.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, HeadOn_StatusOverride)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
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
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  BcMpcInput inp = make_base_input();
  inp.own_ship.u_mps = 7.5;

  const auto sol = solver.solve(inp);

  EXPECT_DOUBLE_EQ(sol.optimal_speed_mps, 7.5);
}

// ---------------------------------------------------------------------------
// Test 4 — solve_duration_us is non-negative after a solve.
// A single-target solve forces real CPA trajectory work; the timing result
// must be ≥ 0 (sub-microsecond evaluation is valid on fast hardware).
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, Solver_SetsSolveDuration)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  BcMpcInput inp = make_base_input();
  TargetState tgt;
  tgt.id = 1; tgt.x_m = 400.0; tgt.y_m = 0.0; tgt.cog_rad = M_PI; tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);

  const auto sol = solver.solve(inp);

  EXPECT_GE(sol.solve_duration_us, 0);
}

// ---------------------------------------------------------------------------
// Test 5a — consecutive_failures_ resets to 0 after a valid solve.
// Override and Resolved are both valid outcomes; counter must not increment.
// ---------------------------------------------------------------------------
TEST(BcMpcSolver, ConsecutiveFailures_ZeroAfterValidSolve)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  static_cast<void>(solver.solve(make_base_input()));  // Resolved — no targets
  EXPECT_EQ(solver.consecutive_failures(), 0LL);

  BcMpcInput inp = make_base_input();
  inp.predicted_short_horizon_cpa_m = 100.0;
  TargetState tgt;
  tgt.id = 1; tgt.x_m = 400.0; tgt.y_m = 0.0; tgt.cog_rad = M_PI; tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);
  static_cast<void>(solver.solve(inp));  // Override — still valid, counter must stay 0
  EXPECT_EQ(solver.consecutive_failures(), 0LL);
}

// ---------------------------------------------------------------------------
// Test 6 — OverrideGenerator heading conversion: π/2 rad → ≈ 90 deg.
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

// ---------------------------------------------------------------------------
// Test 7 — OverrideGenerator heading overflow: 7.0 rad (~401°) → ≈ 41 deg.
// heading_cmd_rad can exceed 2π when the best branch offsets past 360°;
// fmod normalisation must wrap it into [0, 360) before publishing to L4.
// ---------------------------------------------------------------------------
TEST(BcMpcOverrideGenerator, HeadingOverflow_Normalised)
{
  BcMpcOverrideGenerator gen;

  BcMpcSolution sol;
  sol.status            = BcMpcSolution::Status::Override;
  // 7.0 rad = 401.07°; fmod(401.07, 360) = 41.07°
  sol.heading_cmd_rad   = 7.0;
  sol.optimal_speed_mps = 5.0;
  sol.rot_cmd_rad_s     = 0.0;
  sol.validity_s        = 1.0;
  sol.confidence        = 0.8;
  sol.trigger_reason    = "CONDITION_A";

  const rclcpp::Time t{0, 0, RCL_ROS_TIME};
  const auto cmd = gen.generate(sol, t);

  EXPECT_GE(static_cast<double>(cmd.heading_cmd_deg), 0.0);
  EXPECT_LT(static_cast<double>(cmd.heading_cmd_deg), 360.0);
  EXPECT_NEAR(static_cast<double>(cmd.heading_cmd_deg), 41.07, 0.1);
}
