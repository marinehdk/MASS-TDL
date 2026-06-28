# M5 Return-to-Route Avoidance-Channel Leak Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stop M5 from emitting VALID return-to-route geometry on the avoidance channel during M4 RECOVERY, so GNC's avoidance hold expires and the avoidance window stops stretching ~3× past its intended duration.

**Architecture:** In `MidMpcNode::publish_avoidance_waypoints_`, change the return-to-route branch to emit an EMPTY avoidance plan (empty latitude/longitude arrays) instead of VALID return-to-route waypoints. GNC's existing `basic_route_valid` rejects plans with <2 waypoints without calling `mark_avoidance_active`, so the 60 s hold expires naturally and GNC resumes the L2 nominal route. Return-to-route geometry is owned by L2/L3 nominal route, not the avoidance channel.

**Tech Stack:** C++17, ROS2 (rclcpp), GTest, colcon, Docker (mass-l3-gnc stack), Python probe/oracle tooling.

**Spec:** `docs/superpowers/specs/2026-06-28-m5-return-route-avoidance-channel-leak-design.md`

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug` on branch `codex/colregs-12probe-debug`.

---

## Background (read before Task 1)

`MidMpcNode::publish_avoidance_waypoints_` (`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:721`) builds an `AvoidanceWaypoints` message. It unconditionally sets `command_source = "collision_avoidance"` (line 735) for every branch. The return-to-route branch (line 834, `else if (last_emitted_conflict_active_ || return_republish_active)`) emits VALID waypoints with `behavior_mode = "return_to_route"` (line 881) through the same `/l3/m5/avoidance_waypoints` publisher.

The bridge forwards this to GNC `/colav/avoidance_plan`. GNC `ActiveRouteManager::avoidance_plan_callback` (`third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp:196`) calls `mark_avoidance_active()` (line 216) on **every accepted plan regardless of behavior_mode**, re-arming a 60 s hold each publish. So during RECOVERY, GNC stays in `avoidance_is_active()`, DEFERRING nominal routes for the whole 1776 s avoidance window. This breaches the seamanship gate's `integrated_abs_xte_m_s` limit (300,000 m·s).

GNC's `basic_route_valid` (line 226) requires `latitude.size() >= 2`. An avoidance plan with empty arrays fails this check, is rejected before `mark_avoidance_active`, and does not re-arm the hold. This is existing GNC semantics — no GNC change needed.

The `return_to_route_emit_until_` 30 s republish window (line 39, `kReturnToRouteRepublishWindow_s`) stays, but each republish will now be EMPTY, reinforcing the release.

The return-to-route branch currently spans lines 834-935 (the `else if` clause). The VALID waypoint population is at lines 885-889 (latitude/longitude/command_speed_mps/navigation_mode resize + fill). The fix removes the VALID population and keeps only the metadata fields for traceability.

---

## File Structure

- **Modify** `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — return-to-route branch of `publish_avoidance_waypoints_` (~line 885-935): emit EMPTY arrays.
- **Modify** `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp` — add a regression test asserting return-to-route geometry generation stays correct (guards the `generate_return_to_route_waypoints` pure function), and document that the node-level publish contract (EMPTY in RECOVERY) is verified via integration probe.

No new files. No change to GNC source, bridge, `generate_return_to_route_waypoints`, `return_to_route_emit_until_` window, seamanship thresholds, or oracle.

---

## Task 1: Add regression test for return-to-route geometry generation

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp`

- [ ] **Step 1: Read the existing return-to-route test to follow its pattern**

Run: `grep -n "return_to_route\|ReturnRoute\|return_route" src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp | head -20`

Note the test fixture / helper names used (e.g. how `generate_return_to_route_waypoints` is called, what lat0/lon0/bearing/xte inputs it takes).

- [ ] **Step 2: Add a regression test asserting return-to-route waypoints are generated and non-empty**

Append inside the existing test suite (follow the namespace and TEST pattern from Step 1). This guards that the pure geometry generator still produces VALID waypoints — the fix changes the *publish* path, not the *generation*:

```cpp
TEST(AvoidanceWaypointGen, ReturnToRouteGeometryRemainsValidForL2Nominal) {
  // Regression guard: generate_return_to_route_waypoints must still produce
  // valid non-empty geometry. The Class B fix changes the M5 PUBLISH path to
  // emit an EMPTY avoidance plan during RECOVERY (so GNC releases its avoidance
  // hold), but the return-to-route geometry itself is owned by L2 nominal route
  // and this generator stays the source of that geometry.
  const double lat0 = 63.44;
  const double lon0 = 10.38;
  const double bearing_rad = 0.0;
  const double xte_m = 200.0;  // own ship offset 200 m from route
  const auto wps = mass_l3::m5::generate_return_to_route_waypoints(
      lat0, lon0, bearing_rad, xte_m);
  ASSERT_GE(wps.size(), 2u);
  for (const auto& p : wps) {
    EXPECT_TRUE(std::isfinite(p.latitude)) << "return waypoint lat must be finite";
    EXPECT_TRUE(std::isfinite(p.longitude)) << "return waypoint lon must be finite";
  }
}
```

If `generate_return_to_route_waypoints` returns a different type (e.g. a struct with `.latitude`/`.longitude` vectors rather than a vector of points), adjust the field access to match the actual return type found in Step 1. Add `#include <cmath>` for `std::isfinite` if not already present.

- [ ] **Step 3: Build and run the new test**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -4 && \
   find build/m5_tactical_planner -name 'test_avoidance_waypoint_gen' -type f -exec {} --gtest_filter='AvoidanceWaypointGen.ReturnToRouteGeometryRemainsValidForL2Nominal' \; 2>&1 | tail -10"
```
Expected: `1 PASSED`. If the test fails because of a return-type mismatch, fix the field access to match the actual type and rerun.

---

## Task 2: Change M5 return-to-route publish to emit EMPTY avoidance plan

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` (return-to-route branch ~line 885-935)

- [ ] **Step 1: Read the full return-to-route branch to identify the exact lines to change**

Run: `sed -n '879,935p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

Identify the block that populates `wp.latitude`, `wp.longitude`, `wp.command_speed_mps`, `wp.navigation_mode` (the VALID waypoint population, roughly lines 885-889 plus any subsequent per-point fill loop).

- [ ] **Step 2: Replace the VALID waypoint population with EMPTY arrays**

The current return-to-route branch sets metadata fields (behavior_mode, parent_route_id, plan_id, has_return_to_route_point) then populates VALID waypoint arrays. Change it so the waypoint arrays stay EMPTY. Keep all metadata fields for traceability.

Find the exact block that begins after `wp.has_return_to_route_point = true;` (line ~884) and populates the arrays. Replace the array-population portion. The resulting branch should look like:

```cpp
    // conflict -> clear transition: repeat return_to_route briefly so the GNC
    // route-update guard cannot drop the only lifecycle-release message.
    //
    // Class B fix: emit an EMPTY avoidance plan (no waypoints) during RECOVERY.
    // GNC ActiveRouteManager::basic_route_valid requires latitude.size() >= 2;
    // an empty plan is rejected before mark_avoidance_active, so the 60 s
    // avoidance hold expires naturally and GNC resumes the L2 nominal route.
    // Return-to-route geometry is owned by L2/L3 nominal route, not the
    // avoidance channel. Previously VALID waypoints here kept re-arming GNC's
    // avoidance hold through the entire RECOVERY window, stretching the
    // avoidance duration ~3x and breaching the seamanship integrated_xte gate.
    wp.behavior_mode             = "return_to_route";
    wp.parent_route_id           = "nominal";
    wp.plan_id                   = return_route_anchor_->plan_id;
    wp.has_return_to_route_point = true;
    // return_latitude / return_longitude hint retained for trace if populated
    // by existing code above; do not add new population.
    // latitude / longitude / command_speed_mps / navigation_mode left EMPTY.
    wp.allow_degraded_execution  = true;
    wp.rationale                 =
        "M4 RECOVERY — release GNC avoidance hold (empty plan); route owned by L2 nominal";
```

**Critical:** Do NOT delete the `avoidance_corridor_anchor_.reset();` (line ~865) or the `return_route_anchor_` setup logic (lines 859-877) — those manage anchor state. Only remove/omit the `wp.latitude.resize(...)` / `wp.longitude.resize(...)` / per-point fill that populates the VALID arrays. If the existing code fills `wp.return_latitude` / `wp.return_longitude` from the anchor, keep that (it is a hint field, not a route array).

- [ ] **Step 3: Build M5 to verify it compiles**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON 2>&1 | tail -6"
```
Expected: `Finished <<< m5_tactical_planner` with no errors.

- [ ] **Step 4: Run the full M5 unit test suite**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "find /opt/ws/build/m5_tactical_planner -name 'test_avoidance_waypoint_gen' -type f -exec {} --gtest_color=no \; 2>&1 | tail -6"
```
Expected: all tests PASS including the new `ReturnToRouteGeometryRemainsValidForL2Nominal` (target 44/44: 43 existing + 1 new).

- [ ] **Step 5: Run M6 unit tests (regression guard — M6 untouched)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m6_colregs_reasoner --event-handlers console_direct+ 2>&1 | tail -5"
```
Expected: 21/21 PASS (M6 unchanged from Class A fix).

- [ ] **Step 6: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp
git commit -m "fix(m5): emit empty avoidance plan during RECOVERY to release GNC hold

M5 published VALID return_to_route waypoints via /l3/m5/avoidance_waypoints
during M4 RECOVERY. GNC ActiveRouteManager calls mark_avoidance_active on every
accepted avoidance plan regardless of behavior_mode, so the return_to_route
geometry re-armed the 60s hold each publish. GNC stayed DEFERRED avoidance_active
for 71% of the run (ho-port), stretching the avoidance window to ~1776s and
breaching the seamanship integrated_abs_xte gate (352k > 300k m·s).

Emit an EMPTY avoidance plan (no waypoints) during RECOVERY instead. GNC
basic_route_valid rejects latitude.size()<2 before mark_avoidance_active, so the
hold expires naturally and GNC resumes the L2 nominal route. Return-to-route
geometry is owned by L2 nominal route, not the avoidance channel. The
return_to_route_emit_until 30s republish window now reinforces the release.

No change to GNC source, bridge, generate_return_to_route_waypoints, seamanship
thresholds, or oracle.

Spec: docs/superpowers/specs/2026-06-28-m5-return-route-avoidance-channel-leak-design.md"
```

---

## Task 3: Rebuild GNC stack image and verify on ho-port (primary Class B case)

**Files:** none (runtime verification)

- [ ] **Step 1: Rebuild the GNC stack from the worktree**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
bash scripts/gnc-profile-start.sh --down
bash scripts/gnc-profile-start.sh up
```
Expected: both stacks up, 6 containers running. Wait ~45 s for lifecycle to settle.

- [ ] **Step 2: Health check**

```bash
curl -sk https://127.0.0.1:18000/api/v1/health
```
Expected: `{"status":"ok"}`.

- [ ] **Step 3: Run rule14-ho-port strict single-probe**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
source scripts/local-a4000-env.sh
TS=$(date +%Y%m%d_%H%M%S)
PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc \
  --scenario colreg-rule14-ho-port \
  --restart-between-runs \
  --restart-settle 24 \
  --sim-rate 10 \
  --summary-out runs/rule14_ho_port_after_classb_fix_${TS}.json \
  --trace-report-dir runs/trace_eval/${TS}_rule14_ho_port_after_classb_fix
```
Expected: completes (exit 0), trace dir with `colreg-rule14-ho-port.trace_current.jsonl` + verdict JSON.

- [ ] **Step 4: Verify the three acceptance signals**

```bash
TD=$(ls -dt runs/trace_eval/*rule14_ho_port_after_classb_fix | head -1)
python3 - <<PY
import json
d=json.load(open("$TD/colreg-rule14-ho-port.json"))
v=d['verdict']; cs=d.get('chain_summary',{}); diag=cs.get('diagnosis',{})
l4=cs.get('l4',{})
print("overall=", v['overall_pass'], "colregs=", v['colregs_pass'], "first_fail=", d.get('first_failure'))
print("diag stage=", diag.get('first_broken_stage'), "gate=", diag.get('failing_gate'))
print("GNC exec counts:", l4.get('gnc_execution_state_counts'))
print("DEFERRED avoidance_active:", l4.get('gnc_execution_state_counts',{}).get('DEFERRED'))
PY
```
Expected:
- `DEFERRED avoidance_active` drops sharply (was 2984; target near 0 outside M4 AVOID window).
- `integrated_abs_xte_m_s` from the summary JSON (`runs/rule14_ho_port_after_classb_fix_*.json` → `colreg-rule14-ho-port/domain_gates`) drops below 300,000.
- `returned_to_route` stays True (regression guard).

- [ ] **Step 5: Run module oracle (Layer-2 regression guard)**

```bash
TD=$(ls -dt runs/trace_eval/*rule14_ho_port_after_classb_fix | head -1)
python3 scripts/run_colregs_module_oracle.py \
  --trace $TD/colreg-rule14-ho-port.trace_current.jsonl \
  --scenario colreg-rule14-ho-port \
  --out runs/module_oracle_rule14_ho_port_after_classb_fix.json 2>&1 | tail -12
```
Expected: 6/6 GREEN (no M5/M6 decision regression).

---

## Task 4: Regression — Rule14 + Rule15 cohorts

**Files:** none (runtime verification)

- [ ] **Step 1: Run Rule14 cohort**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
source scripts/local-a4000-env.sh
TS=$(date +%Y%m%d_%H%M%S)
PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc \
  --scenario colreg-rule14-ho \
  --scenario colreg-rule14-ho-port \
  --scenario colreg-rule14-ho-intelligent \
  --restart-between-runs \
  --restart-settle 24 \
  --sim-rate 10 \
  --summary-out runs/rule14_cohort_after_classb_fix_${TS}.json \
  --trace-report-dir runs/trace_eval/${TS}_rule14_cohort_after_classb_fix
```

- [ ] **Step 2: Run Rule15 cohort (cs, cs-2, cs-intelligent — the Class B members; cs-edge/ot-boundary are Class C/A, run separately)**

```bash
TS=$(date +%Y%m%d_%H%M%S)
PROBE_STUCK_LIMIT=220 python3 scripts/run_colregs_clean_8probe.py \
  --profile gnc \
  --scenario colreg-rule15-cs \
  --scenario colreg-rule15-cs-2 \
  --scenario colreg-rule15-cs-intelligent \
  --restart-between-runs \
  --restart-settle 24 \
  --sim-rate 10 \
  --summary-out runs/rule15_cohort_after_classb_fix_${TS}.json \
  --trace-report-dir runs/trace_eval/${TS}_rule15_cohort_after_classb_fix
```

- [ ] **Step 3: Run module oracle on each and check Layer-2 + DEFERRED drop**

```bash
for cohort in rule14_cohort_after_classb_fix rule15_cohort_after_classb_fix; do
  TD=$(ls -dt runs/trace_eval/*${cohort} | head -1)
  for sid in $(ls $TD/*.trace_current.jsonl 2>/dev/null | xargs -n1 basename | sed 's/.trace_current.jsonl//'); do
    python3 scripts/run_colregs_module_oracle.py \
      --trace $TD/$sid.trace_current.jsonl --scenario $sid \
      --out runs/module_oracle_${sid}_after_classb_fix.json 2>&1 | tail -3
    python3 -c "
import json
d=json.load(open('$TD/$sid.json'))
cs=d.get('chain_summary',{}); l4=cs.get('l4',{})
deferred=l4.get('gnc_execution_state_counts',{}).get('DEFERRED',0)
accepted=l4.get('gnc_execution_state_counts',{}).get('ACCEPTED',0)
print('  $sid: DEFERRED='+str(deferred)+' ACCEPTED='+str(accepted))
"
    echo "---"
  done
done
```
Expected:
- Layer-2: 6/6 GREEN on every scenario (no regression).
- DEFERRED avoidance_active drops sharply on ho-port, ho-intelligent, cs, cs-2, cs-intelligent (was 71%+; target near 0 outside AVOID window).
- `returned_to_route` stays True where it was True.

- [ ] **Step 4: Record seamanship integrated_xte for each Class B scenario**

```bash
for cohort in rule14_cohort_after_classb_fix rule15_cohort_after_classb_fix; do
  SUM=$(ls -t runs/${cohort}_*.json | head -1)
  python3 -c "
import json
d=json.load(open('$SUM'))
for sid,s in d.items():
    if not isinstance(s,dict): continue
    dg=s.get('domain_gates',{})
    ixte=dg.get('integrated_abs_xte_m_s')
    seam=dg.get('seamanship_gate_ok')
    print(f'{sid}: integrated_xte={ixte} seamanship_ok={seam}')
" 2>&1 | head
done
```
Expected: `integrated_abs_xte_m_s` below 300,000 with `seamanship_gate_ok=True` on ho-port, cs, cs-2, cs-intelligent where route recovery holds. ho-intelligent may still fail other gates from intelligent-target amplification — record, do not tune.

---

## Task 5: Record evidence and update handoff

**Files:**
- Modify: `handoff/workspace_log.md` (append entry)

- [ ] **Step 1: Append a handoff entry**

Append to `handoff/workspace_log.md`:

```markdown
## [2026-06-28] ZCode / commit <TASK2_SHA> / Class B fix: M5 empty avoidance plan in RECOVERY

### Task Goal
Stop M5 from emitting VALID return-to-route geometry on the avoidance channel during M4 RECOVERY, which re-armed GNC's avoidance hold and stretched the avoidance window ~3x past its intended duration (breaching seamanship integrated_xte gate).

### Core Changes
- `publish_avoidance_waypoints_` return-to-route branch: emit an EMPTY avoidance plan (empty latitude/longitude arrays) instead of VALID waypoints. GNC basic_route_valid rejects latitude.size()<2 before mark_avoidance_active, so the 60s hold expires naturally and GNC resumes the L2 nominal route.
- Added regression test `ReturnToRouteGeometryRemainsValidForL2Nominal` guarding `generate_return_to_route_waypoints` pure function.
- No change to GNC source, bridge, generate_return_to_route_waypoints, return_to_route_emit_until window, seamanship thresholds, or oracle.

### Current Status
- Layer-2 module oracle: 6/6 GREEN on all Class B scenarios (no regression).
- GNC DEFERRED avoidance_active dropped sharply outside M4 AVOID window.
- Seamanship integrated_abs_xte below 300,000 on Class B scenarios.
- returned_to_route stays True.

### Handoff Notes
- Evidence: runs/trace_eval/<TS>_rule14_ho_port_after_classb_fix/, runs/trace_eval/<TS>_rule14_cohort_after_classb_fix/, runs/trace_eval/<TS>_rule15_cohort_after_classb_fix/.
- Still open (separate specs): ot-boundary Rule13/15 classification (Class A sub-problem), cs-edge GNC speed envelope (Class C).
```

Replace `<TASK2_SHA>` with the actual commit SHA from Task 2 Step 6.

- [ ] **Step 2: Commit the handoff**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add handoff/workspace_log.md docs/superpowers/specs/2026-06-28-m5-return-route-avoidance-channel-leak-design.md
git commit -m "docs(handoff): record M5 empty avoidance plan in RECOVERY fix (Class B)"
```

---

## Self-Review

**Spec coverage:** Each spec section maps to a task:
- "M5 emits EMPTY avoidance plan during RECOVERY" → Task 2.
- "Why not Option B/C" → Task 2 keeps the republish window and metadata, only empties arrays (Option A).
- "Unit tests" → Task 1 (geometry regression guard).
- "Module oracle (Layer-2)" → Task 3 Step 5 + Task 4 Step 3.
- "Integration test (Layer-3)" → Task 3 Step 4 + Task 4.
- "Acceptance Criteria" → Task 3 Step 4 (DEFERRED drop, integrated_xte < 300k, returned_to_route True), Task 4 Step 3 (6/6 GREEN regression).

**Placeholder scan:** No TBD/TODO. `<TASK2_SHA>` in Task 5 is a runtime fill-in with explicit instruction. Step 1 of Task 1 and Task 2 are "read first" steps because the exact line numbers shift with the Class A commit on the same file — they instruct the engineer to confirm lines via grep/sed before editing, with the target block described unambiguously. All code blocks contain complete code.

**Type consistency:** `generate_return_to_route_waypoints` signature matches between Task 1 test and the spec. `wp.behavior_mode` / `wp.plan_id` / `wp.has_return_to_route_point` match the existing field names at mid_mpc_node.cpp:881-884. `basic_route_valid` `latitude.size() >= 2` matches active_route_manager_node.cpp:228.
