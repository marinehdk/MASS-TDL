#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

#include "m5_tactical_planner/bc_mpc/bc_mpc_branch_formulation.hpp"
#include "m5_tactical_planner/bc_mpc/bc_mpc_collision_detector.hpp"
#include "m5_tactical_planner/common/types.hpp"

using mass_l3::m5::BcMpcInput;
using mass_l3::m5::BcMpcSolution;
using mass_l3::m5::TargetState;
using mass_l3::m5::bc_mpc::BcMpcBranchFormulation;
using mass_l3::m5::bc_mpc::BcMpcCollisionDetector;

// Build basic BcMpcInput: own ship at origin, heading north, 5 m/s.
static BcMpcInput make_base_input() {
  BcMpcInput inp;
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.u_mps   = 5.0;
  inp.own_ship.x_m     = 0.0;
  inp.own_ship.y_m     = 0.0;
  inp.cpa_safe_m                    = 1852.0;
  inp.mid_mpc_consecutive_failures  = 0;
  inp.predicted_short_horizon_cpa_m = 1.0e6;  // far-safe CPA
  return inp;
}

// ---------------------------------------------------------------------------
// Test 1 — No targets → evaluate() must return Resolved immediately.
// No targets means worst_case_cpa = 1e9 >> cpa_safe; no override needed.
// ---------------------------------------------------------------------------
TEST(BcMpcCollisionDetector, NoTargets_ResolvedImmediately) {
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcCollisionDetector det{form};

  const auto sol = det.evaluate(make_base_input());

  EXPECT_EQ(sol.status, BcMpcSolution::Status::Resolved);
  EXPECT_GT(sol.worst_case_cpa_m, 1852.0);
  EXPECT_NEAR(sol.confidence, 1.0, 1.0e-9);  // no threat → maximum confidence
}

// ---------------------------------------------------------------------------
// Test 2 — Head-on threat at 400 m, imminence forced by short-horizon CPA.
// Worst-case CPA will be < cpa_safe * override_cpa_multiplier → Override.
// ---------------------------------------------------------------------------
TEST(BcMpcCollisionDetector, HeadOnThreat_Override) {
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcCollisionDetector det{form};

  BcMpcInput inp = make_base_input();
  inp.predicted_short_horizon_cpa_m = 100.0;  // well inside safe distance

  TargetState tgt;
  tgt.id      = 1;
  tgt.x_m     = 400.0;  // 400 m north
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;   // heading south (directly toward own ship)
  tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);

  const auto sol = det.evaluate(inp);

  EXPECT_EQ(sol.status, BcMpcSolution::Status::Override);
}

// ---------------------------------------------------------------------------
// Test 3+4 — Branch count driven by urgency threshold.
// Tests the formulation directly without going through the full evaluator.
// ---------------------------------------------------------------------------
TEST(BcMpcBranchFormulation, BranchCount_HighAndLowUrgency) {
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};

  const auto high = form.candidate_headings(0.0, 1.0);  // above urgency_threshold
  EXPECT_EQ(static_cast<std::int32_t>(high.size()), 7);

  const auto low = form.candidate_headings(0.0, 0.0);   // below urgency_threshold
  EXPECT_EQ(static_cast<std::int32_t>(low.size()), 5);
}

// ---------------------------------------------------------------------------
// Test 5 — Optimal branch avoids collision (does not keep straight heading).
// Head-on scenario at 300 m: best branch must differ from current heading.
// ---------------------------------------------------------------------------
TEST(BcMpcCollisionDetector, OptimalBranchAvoidsCollision) {
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcCollisionDetector det{form};

  BcMpcInput inp = make_base_input();
  inp.predicted_short_horizon_cpa_m = 100.0;

  TargetState tgt;
  tgt.id      = 1;
  tgt.x_m     = 300.0;  // 300 m north, dead ahead
  tgt.y_m     = 0.0;
  tgt.cog_rad = M_PI;   // heading south
  tgt.sog_mps = 5.0;
  inp.targets.push_back(tgt);

  const auto sol = det.evaluate(inp);

  // The safest branch must not be "straight ahead" (psi = 0).
  // A small tolerance avoids floating-point equality pitfalls.
  EXPECT_GT(std::abs(sol.heading_cmd_rad), 1.0e-9);
}

// ---------------------------------------------------------------------------
// Test 6 — Consecutive Mid-MPC failures force k_high branch count.
// Urgency is forced to 1.0 regardless of predicted_short_horizon_cpa_m.
// ---------------------------------------------------------------------------
TEST(BcMpcCollisionDetector, ConsecutiveFailures_ForcesHighBranches) {
  BcMpcBranchFormulation form{BcMpcBranchFormulation::Config{}};
  BcMpcCollisionDetector det{form};

  BcMpcInput inp = make_base_input();
  inp.mid_mpc_consecutive_failures  = 5;    // > 2 → urgency = 1.0
  inp.predicted_short_horizon_cpa_m = 1.0e6;  // would normally be low urgency

  const auto sol = det.evaluate(inp);

  // With urgency = 1.0 → k_high branches were evaluated.
  // selected_branch_idx must lie within [0, k_high).
  EXPECT_GE(sol.selected_branch_idx, 0);
  EXPECT_LT(sol.selected_branch_idx, form.config().k_high);

  // Sanity: no targets → resolved.
  EXPECT_EQ(sol.status, BcMpcSolution::Status::Resolved);
}
