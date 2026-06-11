# COLREGs Rejoin Acceptance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: use
> `superpowers:subagent-driven-development` or `superpowers:executing-plans`
> for future continuation. This file records the executed plan and the current
> acceptance route.

**Goal:** strict 8-probe acceptance is proven by A4000 scoring data plus a real
frontend browser screenshot from `#/monitor`.

**Architecture:** M6 remains the COLREGs authority. M4 owns behavior commitment
and release dwell. Bridge remains a temporary SIL guidance adapter until later
de-shadow work removes it.

## File Structure

- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`
  - Responsibility: keep committed COLREG anchor, quartering gate, inactive dwell counter, and last active COLREG directive.
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
  - Responsibility: hold committed COLREG directives across short M6 false gaps, compute windows from the committed anchor, and scale turn size from CPA risk.
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp`
  - Responsibility: verify anchored windows and false-gap dwell behavior.
- Modify: `docker/sil_topic_bridge.py`
  - Responsibility: accept same-side target-heading shrink updates for rejoin after large turns.
- Modify: `tests/docker/test_sil_topic_bridge.py`
  - Responsibility: cover Bridge same-side rejoin-window refresh.
- Revert/keep conservative: `src/l3_tdl_kernel/m6_colregs_reasoner/*`
  - Responsibility: reject projected-past release and keep release tied to finally-past-and-clear.

## Task 1: Reject M6 Projected-Past Release

- [x] Revert projected-past release in `RuleLatch`.
- [x] Revert node-level projected-past `finally_resolved`.
- [x] Keep tests asserting projection does not release without past-and-clear.

Verification:

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker exec mass-l3-sil-sil-nodes-1 bash -lc "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && /opt/ws/build/m6_colregs_reasoner/test_rule_latch"'
```

Expected/current: 13/13 RuleLatch tests pass.

## Task 2: Add M4 Commitment Release Dwell

- [x] Cache `last_active_colregs_`.
- [x] Hold effective COLREG conflict for short inactive gaps while a COLREG anchor is committed.
- [x] Use cached directive during the dwell window.
- [x] Add `StarboardDirectiveSurvivesBriefColregsFalseGap`.

Verification:

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker exec mass-l3-sil-sil-nodes-1 bash -lc "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select m4_behavior_arbiter --cmake-args -DBUILD_TESTING=ON && source /opt/ws/install/setup.bash && /opt/ws/build/m4_behavior_arbiter/test_m4_node_lifecycle"'
```

Expected/current: 14/14 M4 lifecycle tests pass.

## Task 3: Preserve CPA-Aware M4 Envelope

- [x] Keep committed anchor window based on onset heading.
- [x] Keep CPA tactical buffer for ordinary crossing/head-on probes.
- [x] Keep quartering critical-CPA gate for `colreg-rule15-ot-boundary`.
- [x] Keep bow-crossing test so `colreg-rule15-cs-edge` does not over-turn.

Verification is included in `test_m4_node_lifecycle`.

## Task 4: Bridge Rejoin Refresh

- [x] Allow same-side target update if candidate deviation is smaller than the current target deviation.
- [x] Reject opposite-side or larger-deviation updates.
- [x] Cover with bridge pytest regression.

Verification:

```bash
python3 -m pytest tests/docker/test_sil_topic_bridge.py -q
```

Expected/current: 21 passed.

## Task 5: A4000 Strict 8-Probe

- [x] Restart SIL nodes after build.
- [x] Run clean strict 8-probe on A4000.
- [x] Copy batch JSON back to local artifacts.

Command:

```bash
ssh a4000 'cd ~/Code/mass-l3 && docker restart mass-l3-sil-sil-nodes-1 >/dev/null && sleep 24 && MPLBACKEND=Agg python3 -u run_8_clean.py'
```

Expected/current:

```text
OVERALL: 8/8 PASS
```

## Task 6: Browser Screenshot Evidence

- [x] Open A4000 frontend monitor through SSH tunnel with Playwright Chromium.
- [x] Keep browser page open while `colreg-rule15-ot-boundary` runs.
- [x] Save frontend screenshot after the run, with own-ship trajectory visible.

Screenshot:

```text
artifacts/colregs_8probe_browser_20260610/colreg-rule15-ot-boundary_monitor_browser_pass_candidate.png
```

## Follow-Up Lane

Next work should extract more guidance behavior from Bridge into M4/M5 and add a
real `REJOIN_CAPTURE` controller. Do not weaken M6 release to get route-return
metrics; the strict probes showed that path causes boundary flapping.
