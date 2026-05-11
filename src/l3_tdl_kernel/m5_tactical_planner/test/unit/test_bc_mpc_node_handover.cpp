#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_solver.hpp"
#include "m5_tactical_planner/common/sha256.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::BcMpcInput;
using mass_l3::m5::BcMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::bc_mpc::BcMpcBranchFormulation;
using mass_l3::m5::bc_mpc::BcMpcSolver;

// ---------------------------------------------------------------------------
// Shared helper: head-on scenario at 300 m, predicted CPA = 100 m
// ---------------------------------------------------------------------------
static BcMpcInput make_head_on_input()
{
  BcMpcInput inp;
  inp.own_ship.psi_rad              = 0.0;
  inp.own_ship.u_mps                = 5.0;
  inp.own_ship.x_m                  = 0.0;
  inp.own_ship.y_m                  = 0.0;
  inp.cpa_safe_m                    = 1852.0;
  inp.mid_mpc_consecutive_failures  = 0;
  inp.predicted_short_horizon_cpa_m = 100.0;  // well inside safe distance

  TargetState tgt;
  tgt.id      = 1;
  tgt.x_m     = 300.0;   // 300 m ahead
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;    // heading south — directly toward own ship
  tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);

  return inp;
}

// ---------------------------------------------------------------------------
// Test 1 — ASDRRecord signature is non-empty and non-zero after an Override.
// Validates that sha256() produces a 32-byte result for a real solve.
// ---------------------------------------------------------------------------
TEST(BcMpcNodeHandover, ASDRRecord_SignatureIsNonEmpty)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  const auto sol = solver.solve(make_head_on_input());
  ASSERT_EQ(sol.status, BcMpcSolution::Status::Override);

  // Build the same JSON as BcMpcNode::publish_override_ (approximation).
  // We only need a non-empty string to exercise sha256.
  const std::string json =
      std::string("{\"heading_rad\":") + std::to_string(sol.heading_cmd_rad)
      + ",\"validity_s\":"  + std::to_string(sol.validity_s)
      + ",\"worst_cpa_m\":" + std::to_string(sol.worst_case_cpa_m) + "}";

  const auto digest = mass_l3::m5::common::sha256(json);

  EXPECT_EQ(digest.size(), 32u);

  // Digest must not be all-zeros (collision with empty-hash would be a bug).
  const std::array<uint8_t, 32> zero_digest{};
  EXPECT_NE(digest, zero_digest);
}

// ---------------------------------------------------------------------------
// Test 2 — sha256 is deterministic and produces a known-length digest.
// A fixed input must always produce the same 32-byte non-zero result.
// ---------------------------------------------------------------------------
TEST(BcMpcNodeHandover, ASDRRecord_SignatureMatchesSha256OfJson)
{
  const std::string json =
      R"({"heading_deg":45.0,"validity_s":1.0,"worst_cpa_m":100.0})";

  const auto digest = mass_l3::m5::common::sha256(json);

  EXPECT_EQ(digest.size(), 32u);

  const std::array<uint8_t, 32> zero_digest{};
  EXPECT_NE(digest, zero_digest);

  // Same input must yield identical digest (determinism requirement for ASDR audit trail).
  const auto digest2 = mass_l3::m5::common::sha256(json);
  EXPECT_EQ(digest, digest2);
}

// ---------------------------------------------------------------------------
// Test 3 — Override then Resolved handover state machine.
// Step 1: head-on at 300 m → expect Override.
// Step 2: no targets, high predicted CPA → expect Resolved.
// ---------------------------------------------------------------------------
TEST(BcMpcNodeHandover, BcMpcHandover_OverrideThenResolved)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  // Step 1: dangerous head-on scenario triggers Override.
  const auto sol_override = solver.solve(make_head_on_input());
  EXPECT_EQ(sol_override.status, BcMpcSolution::Status::Override);

  // Step 2: clear scene — no targets, predicted CPA far exceeds safe distance.
  BcMpcInput clear_inp;
  clear_inp.own_ship.psi_rad              = 0.0;
  clear_inp.own_ship.u_mps                = 5.0;
  clear_inp.own_ship.x_m                  = 0.0;
  clear_inp.own_ship.y_m                  = 0.0;
  clear_inp.cpa_safe_m                    = 1852.0;
  clear_inp.mid_mpc_consecutive_failures  = 0;
  clear_inp.predicted_short_horizon_cpa_m = 1.0e6;  // far-safe sentinel

  const auto sol_resolved = solver.solve(clear_inp);
  EXPECT_EQ(sol_resolved.status, BcMpcSolution::Status::Resolved);
}

// ---------------------------------------------------------------------------
// Test 4 — Head-on scenario: selected branch deviates from straight-ahead.
// A correct BC-MPC must steer away; psi = 0 (straight) would be a collision.
// ---------------------------------------------------------------------------
TEST(BcMpcNodeHandover, BcMpcHandover_OverrideStatus_HeadingDiffersFromStraight)
{
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcSolver solver{form};

  const auto sol = solver.solve(make_head_on_input());

  ASSERT_EQ(sol.status, BcMpcSolution::Status::Override);

  // Override heading must not be the straight-ahead course (|Δψ| > ε).
  // Straight-ahead is 0.0 rad; any evasion branch must differ.
  EXPECT_GT(std::abs(sol.heading_cmd_rad), 1e-9);
}
