# M5 Mid-MPC Initial-Guess Feasibility Fix Implementation Plan

> **⛔ SUPERSEDED 2026-06-08 — DO NOT EXECUTE.** The x0-clamp hypothesis this plan implements was tested live on A4000 and FAILED (Restoration_Failed count unchanged at 29; infeasible-start is NOT the cause). See `docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md` §6 "修复尝试 #1". A new root-cause investigation is required before any fix.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the intermittent `Restoration_Failed` flapping in M5's Mid-MPC by guaranteeing the IPOPT initial guess is feasible w.r.t. the heading/speed box, so avoidance runs on the converged MPC instead of repeatedly dropping to the DEGRADED geometric fallback.

**Architecture:** When own-ship heading is outside the COLREG starboard window (the ship has not yet turned), the NLP's initial guess `x0` (cold = own heading; warm = previous trajectory) places `psi[k]` outside `[hmin,hmax]` for all `k` including `k=0`. With a near-flat objective in the feasible region, IPOPT enters its restoration phase and intermittently fails. Fix = clamp `x0` element-wise into the heading/speed box in `MidMpcSolver::solve()` before calling IPOPT. This is a single-function, deterministic change; it does not alter the NLP formulation, cost, or constraints.

**Tech Stack:** C++17, ROS2 Humble, CasADi + IPOPT (mumps, limited-memory L-BFGS), GoogleTest (`ament_add_gtest`), colcon, Docker (sil-nodes), A4000 server.

**Root cause provenance:** Reproduced live on A4000 (`fix/m5-nlp-convergence` @158bba9d, scenario `colreg-rule14-ho`) on 2026-06-08. Container logs: `[M5][MidMPC] IPOPT status=Restoration_Failed iter=43..406` interleaved with `Maximum_Iterations_Exceeded(iter=500)`; near-identical inputs flipped NORMAL↔DEGRADED across cycles (warm-start dependence). Full evidence: `docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md` §6.

**Out of scope (explicitly NOT this plan):**
- "J_colreg inert" — RETRACTED as a misread: `mid_mpc_nlp_formulation.cpp:285-287` leaves `cost_colreg/cost_dist/cost_vel` zero-init, so the `cost_colreg=0` in the plan rationale is a *display artifact*, not evidence. Existing test `HeadOnGiveWayRightTurn` proves `J_colreg` engages. Whether SIL avoidance is *geometrically correct* is gated AFTER this fix (Task 5), because Restoration_Failed contaminates any trajectory-quality assessment.
- M1/M7 hard gates (D3), bridge de-band-aiding (D4) — later plans.

---

## File Structure

| File | Responsibility | Change |
|---|---|---|
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp` | Per-cycle IPOPT solve; packs `x0`/params | Modify `solve()`: clamp `x0` into bounds |
| `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp` | gtest IPOPT integration tests | Add regression test (own heading outside narrow window) |

No new files. No header/signature changes (clamp uses `input.constraints` already available inside `solve()`).

---

## Task 1: Failing regression test — own heading outside the COLREG window

**Files:**
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp` (append; fixture `MidMpcNlpTest`, helpers `make_head_on_input()` already exist)

- [ ] **Step 1: Write the failing test**

Append after the `ConsecutiveFailuresResetOnSuccess` test (before the final `}` of the file is N/A — file ends at the test; just append at EOF):

```cpp
// ---------------------------------------------------------------------------
// 场景 6 (regression, 2026-06-08): own heading BELOW the COLREG starboard
// window. Initial guess (own heading + warm trajectory) is outside [hmin,hmax]
// at every step → before the x0-clamp fix this intermittently triggered IPOPT
// Restoration_Failed (reproduced on A4000, scenario colreg-rule14-ho).
// The solve MUST converge and the trajectory MUST respect the heading window.
// ---------------------------------------------------------------------------
TEST_F(MidMpcNlpTest, OwnHeadingBelowColregWindowConverges) {
  MidMpcInput input = make_head_on_input();   // closing target 500 m north
  input.own_ship.psi_rad = 0.0;               // pointing north (0°)
  input.constraints.own_ship_psi_rad = 0.0;
  input.constraints.heading_min_rad = 30.0 * M_PI / 180.0;  // window 30°..90°
  input.constraints.heading_max_rad = 90.0 * M_PI / 180.0;  // own 0° is BELOW it

  // Warm-start from a trajectory still at own heading (outside the window) —
  // mirrors the SIL case where the previous cycle's psi lags the shifted window.
  MidMpcSolution warm;
  warm.trajectory.resize(static_cast<std::size_t>(8));
  for (auto& pt : warm.trajectory) {
    pt.psi_rad = 0.0;
    pt.u_mps   = 5.0;
  }

  const auto sol = solver_->solve(input, &warm);

  EXPECT_EQ(sol.status, MidMpcSolver::SolveStatus::Converged);
  ASSERT_FALSE(sol.trajectory.empty());
  for (const auto& pt : sol.trajectory) {
    EXPECT_GE(pt.psi_rad, input.constraints.heading_min_rad - 1.0e-3);
    EXPECT_LE(pt.psi_rad, input.constraints.heading_max_rad + 1.0e-3);
  }
}
```

> Note: `MidMpcSolution::trajectory` elements have `.psi_rad` / `.u_mps` fields (see `mid_mpc_nlp_formulation.cpp:330-333`). `SolveStatus` is an alias of `MidMpcSolution::Status` (`mid_mpc_solver.hpp:45`).

- [ ] **Step 2: Build + run on A4000 to verify it FAILS**

Sync the branch to A4000 first (your normal flow — e.g. `git push` then `ssh a4000 'cd ~/Code/mass-l3 && git fetch && git checkout fix/m5-nlp-convergence && git reset --hard origin/fix/m5-nlp-convergence'`, or rsync the two changed files).

Then build the test target and run it inside the sil-nodes build environment:

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml build sil-nodes'
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -lc "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon test --packages-select m5_tactical_planner --ctest-args -R OwnHeadingBelowColregWindow ; colcon test-result --verbose"'
```

Expected: the new test **FAILS** (status != Converged, i.e. Restoration_Failed/NumericalFailure, or psi out of bounds). If it spuriously passes (IPOPT happened to restore), make the trigger harder: set `heading_min_rad = 60°`, `heading_max_rad = 75°` (narrower, further from own 0°) and re-run — the infeasible start must reproduce a non-Converged status before proceeding.

- [ ] **Step 3: Commit the failing test**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp
git commit -m "test(m5): failing regression for own-heading-outside-window Restoration_Failed"
```

---

## Task 2: Clamp the IPOPT initial guess into the heading/speed box

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp` (function `solve()`, currently lines 81-138; specifically lines 85-96 where `x0_val` and `arg` are built) and the include block (add `<algorithm>`)

- [ ] **Step 1: Add `<algorithm>` include**

In the include block (after line 9 `#include <exception>`), add:

```cpp
#include <algorithm>
```

- [ ] **Step 2: Make `x0_val` mutable and clamp it into the box**

Replace the current block (lines 85-96):

```cpp
  const casadi::DM p_val = formulation_.pack_parameters(input);
  const casadi::DM x0_val = (warm_start != nullptr)
      ? pack_warm_start_(*warm_start)
      : pack_cold_start_(input);

  const int32_t gdim = g_dim_();
  const casadi::DMDict arg = {
      {"x0", x0_val},
      {"p",  p_val},
      {"lbg", casadi::DM::zeros(gdim, 1)},
      {"ubg", casadi::DM::inf(gdim, 1)},
  };
```

with:

```cpp
  const casadi::DM p_val = formulation_.pack_parameters(input);
  casadi::DM x0_val = (warm_start != nullptr)
      ? pack_warm_start_(*warm_start)
      : pack_cold_start_(input);

  // Feasibility guard (2026-06-08): clamp the initial guess into the
  // heading/speed box so IPOPT starts FEASIBLE. When own heading is outside the
  // COLREG window (ship has not yet turned), an out-of-bounds x0 forces IPOPT
  // into its restoration phase, which intermittently fails (Restoration_Failed)
  // → DEGRADED fallback flapping. Clamping is deterministic and does not change
  // the NLP (cost/constraints unchanged); IPOPT still optimises within the box.
  const int32_t N    = formulation_.config().n_horizon;
  const double  hmin = input.constraints.heading_min_rad;
  const double  hmax = input.constraints.heading_max_rad;
  const double  umin = input.constraints.speed_min_mps;
  const double  umax = input.constraints.speed_max_mps;
  for (int32_t k = 0; k < N; ++k) {
    x0_val(k)     = std::min(std::max(static_cast<double>(x0_val(k)),     hmin), hmax);
    x0_val(N + k) = std::min(std::max(static_cast<double>(x0_val(N + k)), umin), umax);
  }

  const int32_t gdim = g_dim_();
  const casadi::DMDict arg = {
      {"x0", x0_val},
      {"p",  p_val},
      {"lbg", casadi::DM::zeros(gdim, 1)},
      {"ubg", casadi::DM::inf(gdim, 1)},
  };
```

> Defensive note: if `hmin > hmax` reached `solve()` (it shouldn't — `assemble_input_:151-153` swaps), `std::max(...,hmin)` then `std::min(...,hmax)` yields `hmax`, still a finite in-range value; IPOPT then reports Infeasible from the constraints (correct), not Restoration_Failed. No new failure mode introduced.

- [ ] **Step 3: Build + run the regression test to verify it PASSES**

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml build sil-nodes'
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -lc "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon test --packages-select m5_tactical_planner --ctest-args -R OwnHeadingBelowColregWindow ; colcon test-result --verbose"'
```

Expected: `OwnHeadingBelowColregWindowConverges` **PASSES** (Converged + all psi within window).

- [ ] **Step 4: Run the FULL m5 solver suite — no regressions**

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -lc "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon test --packages-select m5_tactical_planner ; colcon test-result --verbose"'
```

Expected: all pre-existing tests still PASS (`StraightLineNoTargets`, `HeadOnGiveWayRightTurn`, `CrossingGiveWay`, `InfeasibleProblem`, `WarmStartFasterThanColdStart`, `ConsecutiveFailuresResetOnSuccess`) + the new one. `InfeasibleProblem` must still report a non-Converged status (the clamp must NOT mask genuine infeasibility).

- [ ] **Step 5: Commit the fix**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp
git commit -m "fix(m5): clamp NLP initial guess into heading/speed box to stop Restoration_Failed"
```

---

## Task 3: A4000 SIL acceptance — measure DEGRADED rate before/after (the real symptom)

The unit test guards the clamp logic, but the *symptom* is integration-level. This task proves the flapping is gone on the live SIL.

**Files:** none (uses existing `/tmp/repro_m5_deep.py` from the debug session; if absent, recreate from the doc's §6 repro description, or reuse `scripts/_dbg_check_fix.py`).

- [ ] **Step 1: Capture the AFTER metric**

With sil-nodes rebuilt+restarted on the fixed branch:

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml up -d sil-nodes'
ssh a4000 'cd ~/Code/mass-l3 && export ORCH_URL=https://127.0.0.1:18000 && python3 /tmp/repro_m5_deep.py'
ssh a4000 'docker logs mass-l3-sil-sil-nodes-1 --since 3m 2>&1 | grep -c "Restoration_Failed"'
```

Expected: the 20-sample sweep shows **status=NORMAL across the encounter** (no DEGRADED runs at wall 9-15 / 33-39 as in the BEFORE run), and the `Restoration_Failed` count over the window drops from dozens to ~0.

- [ ] **Step 2: Record the result in the gap doc**

Append the before/after numbers to `docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md` §6 修复记录 (new dated sub-entry). Mark D1's `①` as resolved in the §4 table if the metric passes.

- [ ] **Step 3: Commit the evidence**

```bash
git add "docs/Doc From Claude/2026-06-08-avoidance-design-vs-implementation-gap.md"
git commit -m "docs(m5): record Restoration_Failed fix acceptance (DEGRADED-rate before/after)"
```

---

## Task 4: Update the stale "never converges" comment

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:244-249`

- [ ] **Step 1: Correct the comment to match reality**

The TRANSIT-gate comment (lines 244-249) asserts "the NLP solver is a Phase-3 stub that never converges". That is now false. Replace the parenthetical:

```cpp
    // D-DEMO1 spin fix: M4 is the COLREG authority on whether avoidance is
    // active. When M4 is in TRANSIT, emit an EMPTY plan (no waypoints) so the
    // execution bridge releases avoidance and resumes route-following. Without
    // this, the geometric fallback below keeps a VALID plan alive whenever the
    // NLP fails to converge, trapping the bridge in avoidance → endless
    // circling, no return.
```

(removes the inaccurate "stub that never converges → solver_failed always true" claim; the gate is still needed for the genuine fallback case.)

- [ ] **Step 2: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "docs(m5): correct stale 'never converges' comment in TRANSIT gate"
```

---

## Task 5: GATE — investigate whether SIL avoidance is geometrically correct (decides if ② is real)

**Not a fix.** A decision gate. Now that the solver is stable, measure whether the converged MPC actually commands a correct COLREG avoidance turn. This is what would confirm or dismiss the (currently unconfirmed) "J_colreg" concern.

**Files:** none (measurement only).

- [ ] **Step 1: Measure the commanded turn through the encounter**

Run `colreg-rule14-ho` (head-on) and `colreg-rule15-cs` (crossing) and capture, per cycle, the M5 plan's commanded heading vs own heading vs route bearing (extend `/tmp/repro_m5_window.py` to also echo the avoidance_plan waypoint heading, or read `/m5/sat_data`). Confirm: head-on → own ship turns **starboard** to a CPA ≥ the configured safe distance; crossing give-way → starboard/astern pass.

- [ ] **Step 2: Decide**

- If the turn is correct and CPA is achieved → **② is dismissed**; D1 is fully resolved; proceed to D3/D4 in a new plan.
- If the MPC converges but does NOT turn enough (CPA not achieved) → **② is real**; open a *new* systematic-debugging cycle on `build_colreg_cost_` / `cpa_safe_m` weighting (`assemble_input_:171-185`, `mid_mpc_nlp_formulation.cpp:119-165`) with its own plan. Do NOT patch it inside this plan.

- [ ] **Step 3: Record the verdict** in the gap doc §6 and (if ② is real) create the follow-up plan stub `docs/superpowers/plans/YYYY-MM-DD-m5-jcolreg-engagement.md`.

---

## Self-Review

- **Spec coverage:** Confirmed bug ① (Restoration_Failed) → Tasks 1-3. Stale comment → Task 4. Unconfirmed ② → Task 5 gate (correctly NOT a blind fix). D3/D4 explicitly deferred. ✓
- **Placeholder scan:** All code blocks complete; build/test commands concrete (docker compose + colcon). The only "your normal flow" is the git-sync step (Task 1 Step 2), which gives two concrete alternatives. ✓
- **Type consistency:** `MidMpcSolver::SolveStatus` (alias, hpp:45), `MidMpcSolution::trajectory[].psi_rad/.u_mps` (formulation.cpp:330-333), `input.constraints.heading_min_rad/heading_max_rad/speed_min_mps/speed_max_mps` (used in `assemble_input_` + `pack_parameters`), `formulation_.config().n_horizon` (hpp:100,67). All consistent. ✓
- **TDD/commit discipline:** Each task: failing test → fix → verify → commit. ✓
