# SIL Bridge Avoidance Execution Path Refactor — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for every code task (failing test first), and superpowers:executing-plans (or subagent-driven-development) to work task-by-task. Steps use checkbox (`- [ ]`) syntax. Spec: [`docs/superpowers/specs/2026-06-03-bridge-avoidance-execution-refactor-design.md`](../specs/2026-06-03-bridge-avoidance-execution-refactor-design.md).

**Goal:** On a clean cold start of `colreg-rule14-ho` @10×, the own ship executes a **bounded starboard avoidance turn (≈30–60°)**, deterministically **5/5 cold cycles**, then resumes a steady course — independent of M3 mission/route state. Achieved by a contained refactor of the avoidance execution path in `docker/sil_topic_bridge.py`: decouple from mission FSM, continuously track the M4 heading window, and release on threat geometry instead of a fixed 5 s timer.

**Architecture:** All changes are in one file, `docker/sil_topic_bridge.py`, plus its pytest. The bridge subscribes to M4 `/l3/m4/behavior_plan` (heading window + behavior enum 0=TRANSIT/nonzero=COLREG_AVOID), M5 `/l3/m5/avoidance_plan` (waypoints w/ turn_radius_m), M3 `/l3/m3/mission_goal` (FSM), M1 `/l3/m1/odd_state`, M2 ThreatState, `/sil/own_ship_state`, `/sil/target_vessel_state`; publishes `/sil/actuator_cmd` (BEST_EFFORT) which the ship_dynamics MMG model integrates. The avoidance state machine: ARM (`_on_avoidance_plan`) → continuous TARGET (`_on_behavior_plan`) → PUBLISH (`_autopilot_step`/`_compute_avoidance_autopilot`, 2 Hz, P-controller w/ 10°/s rate limit) → geometry RELEASE.

**Tech Stack:** Python 3.10, rclpy (ROS2 Humble), pytest, Docker Compose (sil-nodes), A4000 Ubuntu server (`ssh a4000`, repo `~/Code/mass-l3` on GitLab `l3-tdl`). Bridge source is **volume-mounted** (`./docker:/opt/ws/docker`), so deploy = scp host file + `docker compose restart sil-nodes` (no image rebuild).

**Root cause (confirmed, deterministic — full evidence in spec §0):** M4 decision ✓, M5 plan 500m NORMAL ✓, but (1) arm was suppressed by `_m3_activated_once`/mission-FSM coupling (dead-stick), and (2) the avoidance target is latched once from a transient M4 window then never refreshed (under-turn). MPC is NOT the blocker.

---

## Current state inherited (DO NOT re-derive)

- **Branch / worktree:** `feat/d-demo1-bridge-deadstick` at `.worktrees/demo1-bridge-deadstick/`. Already contains, **uncommitted**, in `docker/sil_topic_bridge.py`:
  - **Fix#1 (decouple):** removed `_m3_activated_once` arm guard in `_on_avoidance_plan`; removed the avoidance teardown from `_on_mission_goal` `fsm_state<3` branch (kept only the `current_target_wp` reset).
  - **G1 (continuous tracking):** `_on_behavior_plan` now refreshes `_avoidance_target_heading_deg` every message while `_avoidance_active ∧ behavior!=0 ∧ not _latch_release_triggered` (dropped the `is None` gate).
  - All temporary debug `print()`s removed; `python3 -c "import ast; ast.parse(...)"` passes.
- **Verified on A4000 (clean backend):** 3/5 GOOD (correct starboard) but **over-turns ~180°**, 2/5 LOCK, 0 WRONGWAY. → this plan adds the **release/bounding (G3)** and the **arm-robustness** fix.
- **Parked / out of scope:** Line-B M5 MPC sign-fix in `git stash@{0}` on `main` — do NOT touch.
- **Reusable gate:** `scripts/_dbg_variance.py` (untracked) — 5-cold-cycle classifier (GOOD = signed_dev>+15, LOCK = |dev|<5, WRONGWAY = dev<−15). Run on A4000 with `source scripts/a4000-env.sh`.
- **ThreatState has ONLY string fields** (`cpa_status` ∈ {closing,sustained,cleared}, `target_relative_position` ∈ {ahead,astern,port,...}) — **no numeric CPA/TCPA**. The release must compute DCPA/TCPA **bridge-locally** from own/target kinematics (see Task 3) and flag M2.

---

## ⚠️ Guardrails (read before every task)

- **Branch discipline:** work only in `.worktrees/demo1-bridge-deadstick/` on `feat/d-demo1-bridge-deadstick`. NEVER commit to `main`.
- **One file:** only `docker/sil_topic_bridge.py` + `tests/docker/test_sil_topic_bridge.py` (and `tests/unit/test_w6_latch_release.py` if latch tests need updating). Do NOT edit M2/M4/M5/M6/M7 source — flag cross-module gaps per project rule §7.
- **Do NOT re-couple** avoidance to mission FSM / `_m3_activated_once` (that was the dead-stick).
- **Do NOT loosen** `TURN_NET_MIN_DEG`/`RTF_BAND` in `web/e2e/mvp_consistency.spec.ts` or acceptance thresholds to force green.
- **Preserve:** transit/route-return (`_compute_transit_autopilot`), degenerate-window (`h_span>300`) → M5 waypoint-rudder fallback, avoidance-pre-empts-transit in `_autopilot_step`, ENVELOPE_*/M7-MRC stand-down gating.
- **A4000 shared box:** only restart `sil-nodes`; never touch jitsi/fat-system containers or ports 8000/8765. Use remapped 18000/18765/5173. A concurrent `docker build` corrupts results — ensure none is running (`pgrep -af "docker build|colcon|cc1plus"`) before any integration run.
- **Deploy = scp + restart** (mounted source): `scp docker/sil_topic_bridge.py a4000:Code/mass-l3/docker/sil_topic_bridge.py && ssh a4000 'cd ~/Code/mass-l3 && docker compose -f docker-compose.yml -f docker-compose.a4000.yml restart sil-nodes && sleep 14'`. Verify in-container: `docker exec <CID> grep -c <marker> /opt/ws/docker/sil_topic_bridge.py`.

---

## File Map

| File | Change |
|------|--------|
| `docker/sil_topic_bridge.py` | MODIFY — formalize Fix#1+G1 (inherited); add G3 geometry release + turn bounding; fix arm-intermittency |
| `tests/docker/test_sil_topic_bridge.py` | MODIFY — add failing unit tests for continuous tracking, decouple, geometry release, deterministic arm |
| `tests/unit/test_w6_latch_release.py` | REVIEW — update if the 5 s-timer latch-release semantics change (release is now geometry-triggered) |

---

## Task 0: Baseline + data-dependency probe (A4000, no code change)

**Steps:**
- [ ] Ensure no build running: `ssh a4000 'pgrep -af "docker build|colcon|cc1plus" || echo none'`.
- [ ] Confirm the inherited worktree diff is clean: `cd .worktrees/demo1-bridge-deadstick && git diff --stat docker/sil_topic_bridge.py` (Fix#1+G1 only, no debug prints).
- [ ] Deploy inherited file, restart, verify in-container markers present (`DECOUPLE`, `G1 (continuous target tracking`).
- [ ] Run baseline: `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && N_CYCLES=5 RUN_WALL=24 python3 /tmp/_dbg_variance.py'` → expect the known baseline (~3/5 GOOD over-turning ~180°, ~2/5 LOCK). Confirms starting point.
- [ ] Inspect the ThreatState topic the bridge actually uses and what numeric kinematics the bridge already caches: confirm `_on_target_vessel_state` / `_last_ownship_raw` give position+velocity+heading for own and target. Decide DCPA/TCPA computation inputs.

**Accept:** baseline reproduced (over-turn + partial LOCK); confirmed ThreatState has no numeric CPA/TCPA and the bridge has own+target kinematics to compute them locally.

---

## Task 1: Lock in Fix#1 + G1 with unit tests (TDD)

**Files:** `tests/docker/test_sil_topic_bridge.py`

**Steps:**
- [ ] Read the existing test harness to learn how `SilTopicBridge` is instantiated/mocked (it may use a fake Node / direct method calls).
- [ ] Write FAILING tests (then confirm they pass against the inherited Fix#1+G1 code):
  - `test_avoidance_target_refreshes_with_m4_window`: arm avoidance; feed `_on_behavior_plan` window `[16,34]` then `[16,73]`; assert `_avoidance_target_heading_deg` updates from ~31° to ~63° (continuous tracking, not frozen).
  - `test_mission_goal_inactive_does_not_teardown_avoidance`: with `_avoidance_active=True`, call `_on_mission_goal` with `fsm_state=1` repeatedly; assert `_avoidance_active` stays True and target unchanged.
  - `test_arm_not_gated_on_m3`: with `_m3_activated_once=False` (or removed), a valid M5 plan past `_MIN_ARM_SIM_T` arms avoidance.
- [ ] Run: `cd .worktrees/demo1-bridge-deadstick && python3 -m pytest tests/docker/test_sil_topic_bridge.py -q`.

**Accept:** the three tests exist and pass; pre-existing bridge tests still pass.

---

## Task 2: Geometry release + turn bounding (G3/G4) — TDD

**Files:** `docker/sil_topic_bridge.py`, `tests/docker/test_sil_topic_bridge.py`, review `tests/unit/test_w6_latch_release.py`

**Steps:**
- [ ] FAILING test `test_release_on_geometry_not_timer`: arm + set a large target; simulate threat resolved (`TCPA<0 ∧ DCPA≥cpa_safe`); assert release triggers **before** the 5 s timer would, and that with the threat still closing (`TCPA>0`) release does NOT trigger even after >5 s.
- [ ] FAILING test `test_turn_bounded`: drive a sequence where DCPA recovers at ~40°; assert the commanded target stops growing and release begins (turn does not run to 180°).
- [ ] Implement a bridge-local CPA/TCPA helper from `_last_ownship_raw` + last target-vessel state (relative position & velocity → `tcpa = -(r·v)/|v|²`, `dcpa = |r + v·tcpa|`). Add `cpa_safe` from the same source the planner uses (scenario/ODD param; reuse existing constant if present — do NOT invent a new magic number, trace it).
- [ ] Replace the **release TRIGGER** in `_on_threat_state` / `_on_mission_goal` cond-2 with: trigger `_trigger_latch_release()` when `_latch_hold_elapsed()` AND (`TCPA<0 ∧ DCPA≥cpa_safe`) — OR M4 behavior==TRANSIT (fast path). Keep `_compute_latch_offset` decay (≤5 s) ONLY as post-trigger rudder smoothing.
- [ ] Keep the degenerate-window fallback and the waypoint-radius rudder path intact.
- [ ] If `cpa_safe`/numeric CPA cannot be sourced cleanly, add the bridge-local computation and add a `# [TBD-HAZID][cross-module] M2 ThreatState should publish numeric cpa_m/tcpa_s` comment + note it in the report (do not edit M2).
- [ ] Run pytest; iterate to green.

**Accept:** release is geometry-triggered (tests prove timer-independence); turn-bounding test passes; latch-release unit tests updated to the new semantics and green.

---

## Task 3: Deterministic arm (fix the 2/5 cold-start LOCK)

**Files:** `docker/sil_topic_bridge.py`, `tests/docker/test_sil_topic_bridge.py`

**Steps:**
- [ ] Reproduce/diagnose: temporarily re-add a single `[ARMDBG] valid=... active=... sim_t=...` print (as used in the spike), deploy, run 5 cold cycles, capture which branch the 2 LOCK cycles take (candidates: transient `turn_radius=0` plan → `has_valid_plan=False`; `_avoidance_active` stuck True from a prior cycle not reset by `_on_lifecycle_status`; sim_t-guard never satisfied due to clock reset timing). **Remove the print before committing.**
- [ ] FAILING test reproducing the identified cause (e.g. `test_arm_recovers_after_transient_invalid_plan` or `test_active_flag_reset_on_scenario_cleanup`).
- [ ] Implement the minimal fix for the identified cause only.
- [ ] Run pytest to green.

**Accept:** root cause of the 2/5 LOCK identified with evidence; minimal fix + test; no debug prints remain (`grep -c "ARMDBG\|DEADSTICK\|DELAYED-LATCH" docker/sil_topic_bridge.py` == 0).

---

## Task 4: Integration gate (A4000) — AC-2

**Steps:**
- [ ] No build running; deploy final file; `docker compose restart sil-nodes`; verify in-container markers.
- [ ] Run `N_CYCLES=5 RUN_WALL=24 python3 /tmp/_dbg_variance.py` (copy the worktree's `scripts/_dbg_variance.py` to `/tmp` on A4000 first if not present).
- [ ] If any cycle fails, return to the relevant task (do NOT tune the probe).

**Accept (AC-2):** **5/5 GOOD**, 0 LOCK, 0 WRONGWAY; `signed_dev ∈ [+25°,+75°]` every cycle; spread (max−min) ≤ ~20°.

---

## Task 5: Existing harness regression — AC-3 / AC-4 / AC-5

**Steps:**
- [ ] `ssh a4000 'cd ~/Code/mass-l3 && source scripts/a4000-env.sh && ./scripts/a4000-acceptance.sh'` → A_turn PASS (net starboard turn in band), A_rtf in `[7,12]` @10×.
- [ ] `cd web && RATE=10 ORCH_PORT=18000 FOX_PORT=18765 npx playwright test e2e/mvp_consistency.spec.ts` on A4000 → A_turn green, A_rtf in band.
- [ ] AC-4 sanity: confirm from a single run trace that the maneuver is starboard (port-to-port), min CPA ≥ cpa_safe, and the ship resumes steady course after the target clears (no permanent AVOID lock, no spiral).
- [ ] AC-5 regression: a non-avoidance/transit scenario still follows route/holds heading; degenerate-window still falls back to waypoint rudder.

**Accept:** acceptance script + mvp_consistency green without threshold changes; AC-4/AC-5 hold.

---

## Task 6: Cleanup, commit, document

**Steps:**
- [ ] Remove temporary debug scripts that aren't intended to live in-repo (`scripts/_dbg_*.py`) OR keep only `scripts/_dbg_variance.py` if it's worth retaining as a regression tool — decide and state in the commit.
- [ ] `git diff` final review: only `docker/sil_topic_bridge.py` + tests changed; no debug prints; no stray files.
- [ ] Commit on `feat/d-demo1-bridge-deadstick` with a message describing the 3 fixes (decouple, continuous tracking, geometry release) + arm fix; end with the `Co-Authored-By: Claude Opus 4.8` trailer.
- [ ] Sync per CLAUDE.md §13 (local = GitHub origin/main path via PR, and GitLab `l3-tdl`) — push the branch; do NOT merge to main without review.
- [ ] Update `docs/Design/TDL-Kernel/M*/...progress` and add a short report under the D-task folder noting: root cause, the 3 fixes, AC results, and the two flagged TODOs (M2 numeric ThreatState CPA/TCPA; M3 route keystone; COLREGs release-threshold cert verification).
- [ ] Update memory note [[l3-avoidance-cold-start-deterministic-deadstick]] with the final resolution.

**Accept:** clean commit on the feature branch, branch pushed, docs/memory updated, all ACs recorded.

---

## Complete test & acceptance summary

| Level | What | Pass bar |
|---|---|---|
| Unit (pytest) | `tests/docker/test_sil_topic_bridge.py` (continuous tracking, decouple, geometry release, deterministic arm) + `tests/unit/test_w6_latch_release.py` | all green |
| Integration (A4000) | `scripts/_dbg_variance.py` 5 cold cycles @10× | **5/5 GOOD, dev∈[+25,+75]°, spread≤20°, 0 LOCK, 0 WRONGWAY** |
| Harness | `scripts/a4000-acceptance.sh` + `web/e2e/mvp_consistency.spec.ts` | A_turn green, A_rtf∈[7,12] (no threshold edits) |
| COLREGs sanity | single-run trace | starboard port-to-port, minCPA≥cpa_safe, resumes steady course |
| Regression | transit/route, degenerate fallback, pre-empt, ENVELOPE gating | unchanged |

**Order:** Task 0 (baseline) → 1 (lock in inherited, TDD) → 2 (release/bound, TDD) → 3 (arm determinism, TDD) → 4 (integration gate) → 5 (harness regression) → 6 (cleanup/commit/doc). Each code task is failing-test-first.
