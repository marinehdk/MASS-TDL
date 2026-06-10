# COLREGs Rejoin Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make strict 8-probe acceptance depend on A4000 data plus trajectory screenshots, while first fixing the M6 release condition that currently starves route rejoin.

**Architecture:** M6 remains the COLREGs authority and releases a latched duty only after the encounter is safe and past. The new fallback treats M2's clamped post-CPA projection as a valid "past" signal only when range is opening and CPA/current separation is safe. Bridge route-return remains the temporary SIL guidance path for this pass.

**Tech Stack:** ROS2 C++ (`m6_colregs_reasoner`), gtest through `colcon`, Python strict 8-probe SIL harness on A4000, generated PNG trajectory artifacts under `runs/`.

---

## File Structure

- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`
  - Responsibility: release a latched encounter when M2 reports the closest point is past and safe, but only while range is opening.
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
  - Responsibility: mark a target resolved when either encounter-reference past-and-clear or projected-past-and-safe release is satisfied.
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp`
  - Responsibility: red/green tests for the new projected-past release and the no-release-while-closing guard.

## Parallelization Notes

This first pass is intentionally single-lane because all edits are in M6 release
semantics. A later pass can split into:

- Lane A: M4 committed envelope fields.
- Lane B: M5 `REJOIN_CAPTURE`/LOS controller.
- Lane C: frontend authoritative plan layer and screenshot automation.

Do not start those lanes until the A4000 evidence from this pass is known.

### Task 1: Projected-Past Safe Release In RuleLatch

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp`
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp`

- [ ] **Step 1: Replace the stale no-release test and add the closing guard**

In `test_rule_latch.cpp`, replace `DoesNotReleaseOnCpaProjectionPastAndSafeWithoutPastAndClear` with:

```cpp
TEST(RuleLatch, DoesNotReleaseOnCpaProjectionPastAndSafeWhileStillClosing) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_TRUE(latch.update(true, 900.0, true, false));
  EXPECT_TRUE(latch.update(false, 2000.0, true, false, nullptr,
                           /*cpa_projection_past_and_safe=*/true));
}

TEST(RuleLatch, ReleasesOnCpaProjectionPastAndSafeWhenOpening) {
  RuleLatch latch{1852.0, 1.5};
  EXPECT_TRUE(latch.update(true, 900.0, true, false));
  EXPECT_FALSE(latch.update(false, 2000.0, false, false, nullptr,
                            /*cpa_projection_past_and_safe=*/true));
  EXPECT_FALSE(latch.latched());
  EXPECT_FALSE(latch.has_onset());
}
```

- [ ] **Step 2: Run the focused M6 test and confirm RED**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch
```

Expected: `RuleLatch.ReleasesOnCpaProjectionPastAndSafeWhenOpening` fails because current code holds the latch unless `past_and_clear=true`.

- [ ] **Step 3: Implement the minimal release logic**

In `rule_latch.hpp`, change the latched release block to:

```cpp
const bool opening = !range_closing;
const bool past_clear_and_safe = opening && past_and_clear && (cpa_m >= cpa_safe_m_);
const bool projected_past_and_safe = opening && cpa_projection_past_and_safe;
if (past_clear_and_safe || projected_past_and_safe) {
  latched_ = false;
  has_onset_ = false;
  released_past_clear_ = true;
}
```

Update the class comment so it says the projected-past fallback is allowed only
after M2 reports `tcpa_s <= epsilon`, CPA/current separation is safe, and range
is opening.

- [ ] **Step 4: Run focused M6 tests and confirm GREEN**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch
colcon test-result --verbose
```

Expected: all `test_rule_latch` cases pass.

### Task 2: Propagate Projected-Past Resolution In M6 Node

**Files:**
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`

- [ ] **Step 1: Change target resolution predicate**

In `colregs_reasoner_node.cpp`, replace:

```cpp
const bool finally_resolved =
    past_and_clear && !range_closing && target.cpa_m >= kParams.cpa_safe_m;
```

with:

```cpp
const bool projected_past_and_safe = !range_closing && cpa_projection_past_and_safe;
const bool reference_past_and_safe =
    past_and_clear && !range_closing && target.cpa_m >= kParams.cpa_safe_m;
const bool finally_resolved = reference_past_and_safe || projected_past_and_safe;
```

- [ ] **Step 2: Run focused M6 tests**

Run:

```bash
colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ --ctest-args -R test_rule_latch
colcon test-result --verbose
```

Expected: all M6 focused tests pass.

### Task 3: Local Regression Before A4000

**Files:**
- No additional edits.

- [ ] **Step 1: Run local Python regressions**

Run:

```bash
python3 -m pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
```

Expected: all tests pass.

- [ ] **Step 2: Inspect diff**

Run:

```bash
git diff -- src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/rule_latch.hpp src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp src/l3_tdl_kernel/m6_colregs_reasoner/test/test_rule_latch.cpp docs/superpowers/specs/2026-06-10-colregs-rejoin-acceptance-design.md docs/superpowers/plans/2026-06-10-colregs-rejoin-acceptance.md
```

Expected: diff is limited to spec/plan and M6 release semantics.

### Task 4: A4000 Strict 8-Probe Evidence

**Files:**
- No repository edits unless A4000 evidence fails.

- [ ] **Step 1: Sync this branch to A4000**

Use the existing A4000 sync path for this project and ensure the remote checkout
contains this branch's M6 files before running the batch.

- [ ] **Step 2: Build M6 on A4000**

Run on A4000:

```bash
colcon build --packages-select m6_colregs_reasoner --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo
./build/m6_colregs_reasoner/test/test_rule_latch
```

Expected: build succeeds and `test_rule_latch` passes.

- [ ] **Step 3: Run clean strict 8-probe**

Run on A4000 with the existing clean-restart harness:

```bash
MPLBACKEND=Agg python3 -u run_8_clean.py
```

Expected: `8/8 PASS`.

- [ ] **Step 4: Capture data and screenshots**

Copy back to the local worktree:

```text
runs/batch_colregs_clean.json
runs/*_trajectory.png
```

Create a local artifact directory named:

```text
artifacts/colregs_8probe_rejoin_20260610/
```

Expected: JSON and eight PNGs are present locally. The PNGs show complete own-ship
trajectory per probe.

### Task 5: Decide Next Lane From Evidence

**Files:**
- No repository edits unless evidence fails.

- [ ] **Step 1: Parse final A4000 summary**

Report for each probe:

```text
name, overall_pass, cpa_min_m, returned_to_route, final_xte_m, final_heading_dev_deg, behavior_periods
```

- [ ] **Step 2: Continue only if needed**

If all eight pass and route/screenshots are acceptable, stop and finalize. If CPA
or stability regresses, repair M6 release. If CPA/stability pass but route return
is still unacceptable, start the next plan lane: M5 `REJOIN_CAPTURE`/LOS route
controller, with tests before code.
