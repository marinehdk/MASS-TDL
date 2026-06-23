# COLREGs Generalized Repair Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair COLREGs avoidance behavior through the full execution chain so strict clean 12-probe reaches the maximum possible PASS count without scenario-specific tuning, threshold lowering, mocks, or skipped modules.

**Architecture:** Use the approved full-chain encounter contract. Debug every failure through `L2 route/speed -> M2 world/CPA/geometry -> M6 rule/role/direction/release -> M4 behavior FSM -> M5 trajectory/status -> L4 guidance/execution -> M7 veto/MRM -> M8 evidence`, then apply the smallest general contract fix. Start with observability where a state transition cannot be explained from trace evidence.

**Tech Stack:** C++17, ROS2 Humble, ament_cmake, GoogleTest, Python 3, pytest, Docker compose SIL stack, strict `scripts/run_colregs_clean_8probe.py` 12-probe runner.

**Spec:** `docs/superpowers/specs/2026-06-22-colregs-generalized-repair-design.md`

**Branch:** `codex/colregs-generalization-debug`

## Global Constraints

- No scenario-specific fixes.
- No threshold lowering to pass current 12 probes.
- No scorer relaxation.
- No mocks, skips, or forced PASS paths.
- No `scenario_id` branches in decision logic.
- No route geometry edits unless a scenario is proven physically invalid by chain evidence and documented separately.
- No M5-only or L4-only "make it green" repair that bypasses M6/M4/M7 responsibilities.
- If a scenario parameter, gate threshold, or geometry is coupled to another acceptance condition, stop implementation and ask the user before editing it.
- Every C++ behavior change must be built and tested in the container with `colcon`.
- Every task ends with a commit.

---

## File Structure

### Evidence and Trace

- Create: `tools/sil/colregs_chain_trace.py` - JSONL trace summarizer for the full chain.
- Create: `tools/sil/test_colregs_chain_trace.py` - pytest coverage for chain summarizer.
- Modify: `scripts/run_6_scenarios.py` - attach chain summaries to per-scenario results.
- Modify: `tools/sil/colregs_trace_evaluator.py` - preserve chain diagnosis in report JSON.
- Modify: `tools/sil/trajectory_dashboard.py` - display chain diagnosis without changing verdict math.

### M5 Planning Evidence

- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` - expand ASDR decision JSON and SAT rationale with solver/fallback/recovery facts.
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp` - keep existing status behavior stable.
- Test: `tools/sil/test_colregs_chain_trace.py` - parse M5 enriched evidence from JSONL.

### Lifecycle and L4 Evidence

- Modify: `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py` - expose valid-plan, stale-plan, autopilot, and avoidance-active state in traceable status.
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py` - expose command source and route-following override decision.
- Test: `src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py` - verify COLREGs active path does not route-follow through CPA/risk degradation.

### Encounter Lifecycle and Behavior Stability

- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp` - release certificate helper if diagnosis shows early release.
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` - emit release reason and stable encounter lifecycle evidence.
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp`
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_encounter_state_machine.cpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` - behavior latch fix only after M6/M4 churn is proven.
- Test: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp`

### Gate Evidence

- Modify: `scripts/run_colregs_clean_8probe.py` - keep strict restart run behavior; pass trace report directory and chain summary paths.
- Create: `docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md` - human-readable diagnosis report after first strict run.
- Append: `handoff/workspace_log.md` - phase summary and evidence paths.

---

## Debugging Protocol

Use this protocol for every RED scenario before any behavior code change:

1. Identify failing gate: CPA, stability, phase semantics, route return, seamanship, risk, or evidence mismatch.
2. Read chain summary for that scenario.
3. Locate first broken contract in order: L2, M2, M6, M4, M5, L4, M7, M8.
4. Compare upstream continuity against downstream transition.
5. If upstream is unstable, fix upstream; do not patch downstream.
6. If M4/M6 are stable but M5 flips, fix M5 status/solver/fallback semantics.
7. If M5 is stable but L4/lifecycle releases or route-follows early, fix L4/lifecycle handoff.
8. If all module behavior is coherent but scenario gate is physically contradictory, stop and ask the user with evidence before modifying scenario YAML or thresholds.

The first strict 12-probe goal is diagnosis completeness plus maximum PASS count without behavior tuning. The final goal is 12/12 PASS. If a lower result remains because a scenario/gate coupling is proven, the implementer must pause and ask the user before changing scenario geometry or threshold configuration.

---

## Task 0: Baseline Strict 12-Probe and Environment Check

**Files:**
- Read: `AGENTS.md`
- Read: `docs/superpowers/specs/2026-06-22-colregs-generalized-repair-design.md`
- Output: `runs/batch_<timestamp>_generalized_baseline.json`
- Output: `runs/trace_eval/<timestamp>_clean12/`

**Interfaces:**
- Consumes: current branch runtime behavior.
- Produces: baseline evidence for later task classification.

- [ ] **Step 1: Confirm worktree and branch**

Run:

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-generalization-debug"
git branch --show-current
git status --short
```

Expected:

```text
codex/colregs-generalization-debug
```

`git status --short` must be empty before runtime changes.

- [ ] **Step 2: Confirm stack isolation**

Run:

```bash
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose ps
```

Expected: either no project exists yet or only this worktree's project is present. If the project does not exist, create it using the project runbook for this branch. Do not reuse `mass-l3-sil`.

- [ ] **Step 3: Run strict clean 12-probe**

Run:

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1
python3 scripts/run_colregs_clean_8probe.py \
  --include-intelligent \
  --restart-between-runs \
  --summary-out runs/batch_$(date +%Y%m%d_%H%M%S)_generalized_baseline.json
```

Expected: runner completes all 12 scenarios. PASS count may be below 12 at this stage.

- [ ] **Step 4: Record baseline taxonomy**

Run:

```bash
python3 - <<'PY'
import json, pathlib, sys
paths = sorted(pathlib.Path("runs").glob("batch_*_generalized_baseline.json"))
if not paths:
    raise SystemExit("missing baseline batch json")
p = paths[-1]
data = json.loads(p.read_text())
results = data.get("results", [])
print(p)
print("pass", sum(1 for r in results if r.get("overall_pass")), "of", len(results))
for r in results:
    if not r.get("overall_pass"):
        print(r.get("scenario"), "first_failure=", r.get("first_failure"), "min_cpa=", r.get("min_cpa_m"))
PY
```

Expected: printed list of RED scenarios and first failing gates.

- [ ] **Step 5: Commit baseline handoff note only**

Append a handoff entry with evidence paths. Do not commit generated `runs/` files unless the repository already tracks the specific evidence path.

```bash
git add handoff/workspace_log.md
git commit -m "docs: record colregs generalized baseline evidence"
```

---

## Task 1: Add Chain Trace Summarizer

**Files:**
- Create: `tools/sil/colregs_chain_trace.py`
- Create: `tools/sil/test_colregs_chain_trace.py`

**Interfaces:**
- Consumes: JSONL records with `sim_t`, `topic`, and payload fields.
- Produces: `build_chain_summary(records: Iterable[dict]) -> dict`.
- Later tasks rely on summary keys: `route`, `m2`, `m6`, `m4`, `m5`, `lifecycle`, `l4`, `m7`, `diagnosis`.

- [ ] **Step 1: Write failing tests**

Create `tools/sil/test_colregs_chain_trace.py`:

```python
from tools.sil.colregs_chain_trace import build_chain_summary


def rec(t, topic, **fields):
    out = {"sim_t": t, "topic": topic}
    out.update(fields)
    return out


def test_detects_m5_status_flip_with_stable_upstream_inputs():
    records = [
        rec(1.0, "/l2/planned_route", route_hash="route-a"),
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True, colregs_chain_target_id="42", primary_preferred_direction="STARBOARD"),
        rec(1.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(1.0, "/l3/m5/avoidance_plan", status="NORMAL", rationale="solver_status=0", waypoints=[{"turn_radius_m": 300.0}]),
        rec(2.0, "/l3/m6/colregs_constraint", conflict_detected=True, colregs_chain_target_id="42", primary_preferred_direction="STARBOARD"),
        rec(2.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(2.0, "/l3/m5/avoidance_plan", status="DEGRADED", rationale="M5 geometric COLREG fallback (solver_status=2)", waypoints=[{"turn_radius_m": 300.0}]),
    ]
    summary = build_chain_summary(records)
    assert summary["route"]["hash_changes"] == 0
    assert summary["m6"]["conflict_toggles"] == 0
    assert summary["m4"]["behavior_toggles"] == 0
    assert summary["m5"]["status_transitions"] == ["NORMAL->DEGRADED"]
    assert summary["diagnosis"]["first_broken_stage"] == "M5"
    assert "stable upstream" in summary["diagnosis"]["reason"]


def test_detects_upstream_route_churn_before_m5_flip():
    records = [
        rec(1.0, "/l2/planned_route", route_hash="route-a"),
        rec(1.0, "/l3/m5/avoidance_plan", status="NORMAL", rationale="solver_status=0", waypoints=[]),
        rec(2.0, "/l2/planned_route", route_hash="route-b"),
        rec(2.0, "/l3/m5/avoidance_plan", status="DEGRADED", rationale="M5 geometric COLREG fallback (wrapped_heading_window)", waypoints=[{"turn_radius_m": 300.0}]),
    ]
    summary = build_chain_summary(records)
    assert summary["route"]["hash_changes"] == 1
    assert summary["diagnosis"]["first_broken_stage"] == "L2"
    assert "route hash changed" in summary["diagnosis"]["reason"]


def test_lifecycle_release_while_m6_active_is_l4_lifecycle_fault():
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True, colregs_chain_target_id="42"),
        rec(1.0, "/l3/m4/behavior_plan", behavior=2, rationale="COLREG_AVOID"),
        rec(1.0, "/l3/m5/avoidance_plan", status="DEGRADED", waypoints=[{"turn_radius_m": 300.0}]),
        rec(2.0, "/sil/lifecycle_status", avoidance_active=False, autopilot_enabled=True, valid_m5_plan=False, m5_plan_age_s=11.0),
    ]
    summary = build_chain_summary(records)
    assert summary["lifecycle"]["released_while_m6_active"] is True
    assert summary["diagnosis"]["first_broken_stage"] == "L4"
```

- [ ] **Step 2: Run tests and verify failure**

Run:

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py -q
```

Expected: FAIL with `ModuleNotFoundError` for `tools.sil.colregs_chain_trace`.

- [ ] **Step 3: Create implementation**

Create `tools/sil/colregs_chain_trace.py`:

```python
"""Full-chain COLREGs trace summarizer.

The summarizer is diagnostic only. It must not alter gate verdicts.
"""

from __future__ import annotations

from collections.abc import Iterable
from typing import Any


def _value(record: dict[str, Any], *keys: str, default: Any = None) -> Any:
    payload = record.get("msg")
    for key in keys:
        if key in record:
            return record[key]
        if isinstance(payload, dict) and key in payload:
            return payload[key]
    return default


def _transitions(values: list[Any]) -> list[str]:
    out: list[str] = []
    last = None
    have_last = False
    for value in values:
        if not have_last:
            last = value
            have_last = True
            continue
        if value != last:
            out.append(f"{last}->{value}")
            last = value
    return out


def _count_changes(values: list[Any]) -> int:
    return len(_transitions(values))


def _has_valid_waypoint(record: dict[str, Any]) -> bool:
    waypoints = _value(record, "waypoints", default=[])
    if not isinstance(waypoints, list) or not waypoints:
        return False
    first = waypoints[0]
    if not isinstance(first, dict):
        return True
    return abs(float(first.get("turn_radius_m", first.get("turnRadiusM", 0.0)) or 0.0)) > 1.0e-6


def build_chain_summary(records: Iterable[dict[str, Any]]) -> dict[str, Any]:
    rows = sorted((dict(r) for r in records), key=lambda r: float(r.get("sim_t", 0.0) or 0.0))
    route_hashes = [_value(r, "route_hash") for r in rows if r.get("topic") == "/l2/planned_route" and _value(r, "route_hash") is not None]
    m6_conflicts = [bool(_value(r, "conflict_detected", default=False)) for r in rows if r.get("topic") == "/l3/m6/colregs_constraint"]
    m6_targets = [_value(r, "colregs_chain_target_id", "target_id") for r in rows if r.get("topic") == "/l3/m6/colregs_constraint"]
    m4_behaviors = [_value(r, "behavior") for r in rows if r.get("topic") == "/l3/m4/behavior_plan"]
    m5_rows = [r for r in rows if r.get("topic") == "/l3/m5/avoidance_plan"]
    m5_statuses = [str(_value(r, "status", default="")) for r in m5_rows]
    lifecycle_rows = [r for r in rows if r.get("topic") == "/sil/lifecycle_status"]
    l4_rows = [r for r in rows if r.get("topic") in ("/sil/actuator_cmd", "/l4/guidance_cmd")]
    m7_rows = [r for r in rows if r.get("topic") in ("/l3/checker/veto", "/l3/m7/safety_alert")]

    m6_active = any(m6_conflicts)
    lifecycle_release = any(
        bool(_value(r, "autopilot_enabled", default=False))
        and not bool(_value(r, "avoidance_active", default=True))
        for r in lifecycle_rows
    )

    route_changes = _count_changes(route_hashes)
    m6_toggles = _count_changes(m6_conflicts)
    m4_toggles = _count_changes(m4_behaviors)
    m5_transitions = _transitions([s for s in m5_statuses if s])

    first_stage = "OK"
    reason = "no chain fault detected"
    if route_changes:
        first_stage = "L2"
        reason = "route hash changed before downstream transition"
    elif m6_toggles:
        first_stage = "M6"
        reason = "COLREGs conflict toggled before downstream transition"
    elif m4_toggles:
        first_stage = "M4"
        reason = "behavior changed before planner transition"
    elif lifecycle_release and m6_active:
        first_stage = "L4"
        reason = "lifecycle released while M6 conflict remained active"
    elif m5_transitions:
        first_stage = "M5"
        reason = "M5 status changed with stable upstream inputs"

    return {
        "route": {"hashes": route_hashes, "hash_changes": route_changes},
        "m2": {"present": any(r.get("topic") == "/l3/m2/world_state" for r in rows)},
        "m6": {"conflict_toggles": m6_toggles, "targets": [t for t in m6_targets if t is not None]},
        "m4": {"behavior_toggles": m4_toggles, "behaviors": m4_behaviors},
        "m5": {
            "status_transitions": m5_transitions,
            "valid_plan_samples": sum(1 for r in m5_rows if _has_valid_waypoint(r)),
            "samples": len(m5_rows),
        },
        "lifecycle": {
            "samples": len(lifecycle_rows),
            "released_while_m6_active": bool(lifecycle_release and m6_active),
        },
        "l4": {"samples": len(l4_rows)},
        "m7": {"samples": len(m7_rows)},
        "diagnosis": {"first_broken_stage": first_stage, "reason": reason},
    }
```

- [ ] **Step 4: Run tests and verify pass**

Run:

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py -q
```

Expected:

```text
3 passed
```

- [ ] **Step 5: Commit**

```bash
git add tools/sil/colregs_chain_trace.py tools/sil/test_colregs_chain_trace.py
git commit -m "test: add COLREGs full-chain trace summarizer"
```

---

## Task 2: Wire Chain Summary Into Probe Evidence

**Files:**
- Modify: `scripts/run_6_scenarios.py`
- Modify: `tools/sil/colregs_trace_evaluator.py`
- Modify: `tools/sil/trajectory_dashboard.py`
- Test: `tools/sil/test_colregs_chain_trace.py`

**Interfaces:**
- Consumes: `build_chain_summary(records)`.
- Produces: per-scenario result key `chain_summary`.

- [ ] **Step 1: Add failing test for report preservation**

Append to `tools/sil/test_colregs_chain_trace.py`:

```python
def test_chain_summary_shape_is_json_report_safe():
    summary = build_chain_summary([
        rec(1.0, "/l3/m5/avoidance_plan", status="NORMAL", waypoints=[]),
    ])
    assert set(summary) == {"route", "m2", "m6", "m4", "m5", "lifecycle", "l4", "m7", "diagnosis"}
    assert isinstance(summary["diagnosis"]["first_broken_stage"], str)
    assert isinstance(summary["diagnosis"]["reason"], str)
```

- [ ] **Step 2: Run test**

Run:

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py -q
```

Expected: PASS after Task 1; this locks the JSON shape before runner wiring.

- [ ] **Step 3: Modify `scripts/run_6_scenarios.py`**

Add import near existing trace evaluator imports:

```python
from tools.sil.colregs_chain_trace import build_chain_summary
```

In the function that already has `run_records` and builds `result`, add:

```python
chain_summary = build_chain_summary(run_records)
result["chain_summary"] = chain_summary
```

Do not use `chain_summary` to change `overall_pass`.

- [ ] **Step 4: Modify `tools/sil/colregs_trace_evaluator.py`**

Add optional field to the report data flow. If the report is a dataclass, add:

```python
chain_summary: dict[str, object] | None = None
```

In `report_from_runner_result`, set:

```python
chain_summary=result.get("chain_summary"),
```

- [ ] **Step 5: Modify dashboard display**

In `tools/sil/trajectory_dashboard.py`, add one Trace Signals row:

```python
("Chain diagnosis", chain_diag_text),
```

Build `chain_diag_text` from report JSON:

```python
chain = report.get("chain_summary") or {}
diag = chain.get("diagnosis") or {}
chain_diag_text = f"{diag.get('first_broken_stage', 'UNKNOWN')}: {diag.get('reason', 'missing')}"
```

Keep text English/ASCII.

- [ ] **Step 6: Run Python tests**

Run:

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py tests/tools/test_trajectory_dashboard.py -q
```

Expected: all selected tests pass.

- [ ] **Step 7: Commit**

```bash
git add scripts/run_6_scenarios.py tools/sil/colregs_trace_evaluator.py tools/sil/trajectory_dashboard.py tools/sil/test_colregs_chain_trace.py
git commit -m "feat: attach COLREGs chain diagnosis to probe evidence"
```

---

## Task 3: Enrich M5 Solve-Cycle Evidence

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp`
- Test: `tools/sil/test_colregs_chain_trace.py`

**Interfaces:**
- Consumes: M4 behavior, M6 constraint, solver result, fallback reason.
- Produces: ASDR JSON keys `planner_health`, `semantic_mode`, `fallback_reason`, `m4_behavior`, `m6_conflict`, `m6_target_id`, `waypoints`, `status`.

- [ ] **Step 1: Add Python parser test for enriched M5 evidence**

Append to `tools/sil/test_colregs_chain_trace.py`:

```python
def test_m5_enriched_rationale_does_not_change_status_transition_logic():
    records = [
        rec(1.0, "/l3/m5/avoidance_plan", status="DEGRADED",
            rationale="planner_health=GEOMETRIC_FALLBACK; semantic_mode=AVOIDANCE; fallback_reason=solver_failed",
            waypoints=[{"turn_radius_m": 200.0}]),
        rec(2.0, "/l3/m5/avoidance_plan", status="RECOVERY",
            rationale="planner_health=RECOVERY; semantic_mode=RECOVERY; fallback_reason=none",
            waypoints=[{"turn_radius_m": 300.0}]),
    ]
    summary = build_chain_summary(records)
    assert summary["m5"]["status_transitions"] == ["DEGRADED->RECOVERY"]
    assert summary["m5"]["valid_plan_samples"] == 2
```

- [ ] **Step 2: Run Python test**

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py -q
```

Expected: PASS. This prevents evidence enrichment from altering current parser semantics.

- [ ] **Step 3: Enrich ASDR JSON in `publish_outputs_`**

Replace the existing JSON string construction in `mid_mpc_node.cpp` with:

```cpp
  const std::string planner_health =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.status == "DEGRADED" ? "GEOMETRIC_FALLBACK" :
       (plan.waypoints.empty() ? "EMPTY_TRANSIT" : "SOLVER_CONVERGED"));
  const std::string semantic_mode =
      plan.status == "RECOVERY" ? "RECOVERY" :
      (plan.waypoints.empty() ? "TRANSIT" : "AVOIDANCE");
  const std::string json =
      std::string("{\"status\":\"") + plan.status
      + "\",\"planner_health\":\"" + planner_health
      + "\",\"semantic_mode\":\"" + semantic_mode
      + "\",\"waypoints\":"  + std::to_string(plan.waypoints.size())
      + ",\"solve_ms\":"     + std::to_string(sol.solve_duration_ms)
      + ",\"ipopt_iter\":"   + std::to_string(sol.ipopt_iterations)
      + ",\"solver_status\":" + std::to_string(static_cast<int>(sol.status))
      + "}";
```

Replace SAT reasoning chain assignment with:

```cpp
  sat.sat2.reasoning_chain =
      plan.rationale + "; planner_health=" + planner_health
      + "; semantic_mode=" + semantic_mode;
```

Do not change `AvoidancePlan.status` in this task.

- [ ] **Step 4: Build M5**

Run:

```bash
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select m5_tactical_planner"
```

Expected: build succeeds.

- [ ] **Step 5: Run M5 unit tests**

Run:

```bash
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner \
   --event-handlers console_direct+"
```

Expected: all `m5_tactical_planner` tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp tools/sil/test_colregs_chain_trace.py
git commit -m "feat(m5): expose planner health and semantic mode evidence"
```

---

## Task 4: Expose Lifecycle and L4 Execution Source

**Files:**
- Modify: `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py`
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`
- Test: `src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py`
- Test: `tools/sil/test_colregs_chain_trace.py`

**Interfaces:**
- Consumes: M5 plan validity and L4 command source.
- Produces: trace-visible booleans `avoidance_active`, `autopilot_enabled`, `valid_m5_plan`, `m5_plan_age_s`, and L4 `command_source`.

- [ ] **Step 1: Add chain parser test for lifecycle keys**

Append to `tools/sil/test_colregs_chain_trace.py`:

```python
def test_lifecycle_keys_are_used_for_release_diagnosis():
    records = [
        rec(1.0, "/l3/m6/colregs_constraint", conflict_detected=True),
        rec(1.0, "/sil/lifecycle_status", avoidance_active=True, autopilot_enabled=False, valid_m5_plan=True, m5_plan_age_s=0.2),
        rec(2.0, "/sil/lifecycle_status", avoidance_active=False, autopilot_enabled=True, valid_m5_plan=False, m5_plan_age_s=11.2),
    ]
    summary = build_chain_summary(records)
    assert summary["lifecycle"]["released_while_m6_active"] is True
```

- [ ] **Step 2: Run parser test**

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py -q
```

Expected: PASS after Task 1.

- [ ] **Step 3: Add lifecycle debug fields without changing control**

In `lifecycle_mgr.py`, keep `_compute_autopilot` logic unchanged. Add instance attributes:

```python
self._last_valid_m5_plan = False
self._last_m5_plan_age_s = 0.0
```

In `_cb_avoidance`, after `has_valid_plan = ...`, set:

```python
self._last_valid_m5_plan = bool(has_valid_plan)
```

In `_compute_autopilot`, after `is_m5_stale = ...`, set:

```python
self._last_m5_plan_age_s = float(sim_t - self._last_valid_plan_time)
```

In `_status_callback`, if the message type already supports dynamic Python attributes in this SIL context, attach:

```python
msg.avoidance_active = bool(self._avoidance_active)
msg.autopilot_enabled = bool(self._autopilot_enabled)
msg.valid_m5_plan = bool(self._last_valid_m5_plan)
msg.m5_plan_age_s = float(self._last_m5_plan_age_s)
```

If the generated message rejects those fields, do not edit message definitions inside this task. Instead emit one ASDR/debug JSON record through the existing trace writer path and document the exact field rejection in the commit message.

- [ ] **Step 4: Add L4 command-source evidence**

In `l4_guidance_adapter/node.py`, identify the final command publishing path. Add a local string before publishing:

```python
command_source = "m5_avoidance" if self._avoidance_active else "route_following"
```

If a COLREGs-active branch is already named differently, use the existing internal flag and map it to one of:

```python
"m5_avoidance"
"recovery"
"route_following"
"m7_override"
```

Add the source to the traceable log or ASDR payload emitted near the command. Do not change rudder/throttle computation in this task.

- [ ] **Step 5: Run Python tests**

Run:

```bash
python3 -m pytest \
  tools/sil/test_colregs_chain_trace.py \
  src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py -q
```

Expected: selected tests pass.

- [ ] **Step 6: Commit**

```bash
git add src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py \
        src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py \
        src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py \
        tools/sil/test_colregs_chain_trace.py
git commit -m "feat: expose lifecycle and L4 command-source evidence"
```

---

## Task 5: Generate 12-Probe Chain Diagnosis Report

**Files:**
- Create: `docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md`
- Modify: `handoff/workspace_log.md`

**Interfaces:**
- Consumes: strict 12-probe JSON and per-scenario chain summaries.
- Produces: stage-by-stage diagnosis table and user approval gate for scenario/threshold coupling.

- [ ] **Step 1: Run strict clean 12-probe with chain evidence**

Run:

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1
python3 scripts/run_colregs_clean_8probe.py \
  --include-intelligent \
  --restart-between-runs \
  --summary-out runs/batch_$(date +%Y%m%d_%H%M%S)_chain_diagnosis.json
```

Expected: runner completes all 12 scenarios and writes trace evidence.

- [ ] **Step 2: Create report**

Create `docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md` with this structure:

```markdown
# COLREGs Generalized Repair Debug Report

## Evidence

- Batch summary: `runs/<exact batch json>`
- Trace folder: `runs/trace_eval/<exact folder>`
- Branch: `codex/colregs-generalization-debug`

## Result

| Scenario | Verdict | First failing gate | First broken stage | Evidence reason | Proposed fix class |
|---|---|---|---|---|---|

## Cross-Scenario Fault Groups

| Group | Scenarios | Shared broken stage | General fix | User approval needed |
|---|---|---|---|---|

## Scenario Or Threshold Coupling Review

List any scenario where all module contracts are coherent but acceptance conditions conflict. If none, write `None found in this run.`

## Next Code Change

Name exactly one first code-change task and why it addresses the largest shared fault group.
```

Fill every table row from batch JSON and chain summaries. Do not leave placeholder text.

- [ ] **Step 3: Apply user approval gate**

If report identifies scenario geometry or gate-threshold coupling, stop here and ask:

```text
The chain diagnosis indicates scenario/gate coupling in <scenario>. Evidence: <short evidence>. May I modify <file/threshold/geometry>, or should code remain unchanged and this scenario stay RED?
```

Do not modify scenario YAML or thresholds before user approval.

- [ ] **Step 4: Commit**

```bash
git add docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md handoff/workspace_log.md
git commit -m "docs: record COLREGs 12-probe chain diagnosis"
```

---

## Task 6: Fix First Shared Lifecycle Contract Break

**Files:**
- Modify one or more based on Task 5 report:
  - `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
  - `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/colregs_release_policy.hpp`
  - `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Test one or more:
  - `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_colregs_release_policy.cpp`
  - `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_encounter_state_machine.cpp`
  - `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp`

**Interfaces:**
- Consumes: Task 5 diagnosis group.
- Produces: one general lifecycle fix with unit coverage.

- [ ] **Step 1: Select first shared fault group**

Use Task 5 report. Select only one group:

```text
M6 early release
M6 re-arm after short projection gap
M4 premature AVOIDANCE -> TRANSIT
M4 missing RECOVERY latch
```

If the largest group is not one of these four, write a new focused task before implementation and commit that plan amendment.

- [ ] **Step 2: Write failing unit test**

For M6 release/re-arm, add a test in `test_colregs_release_policy.cpp` or `test_encounter_state_machine.cpp` that follows this pattern:

```cpp
TEST(ColregsReleasePolicy, DoesNotReleaseBeforePastClearAndOpening) {
  // Arrange target still ahead/abeam with CPA floor barely satisfied.
  // Act by evaluating the release helper used by the node.
  // Assert release is false and reason contains the failed semantic gate.
  EXPECT_FALSE(release.safe);
  EXPECT_THAT(release.reason, ::testing::HasSubstr("past_clear=false"));
}
```

For M4 premature transition, add a test in `test_m4_node_lifecycle.cpp`:

```cpp
TEST(M4NodeLifecycle, KeepsRecoveryWhenColregsReleaseCertificateMissing) {
  // Arrange behavior node with previous AVOIDANCE and route XTE outside return gate.
  // Publish M6 no-conflict sample without release certificate.
  // Assert next behavior remains AVOIDANCE or RECOVERY, not TRANSIT.
  EXPECT_NE(plan.behavior, l3_msgs::msg::BehaviorPlan::BEHAVIOR_TRANSIT);
}
```

Use real helper names already present in the target file. Do not create a parallel fake state machine.

- [ ] **Step 3: Run test and verify failure**

For M6:

```bash
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m6_colregs_reasoner \
   --event-handlers console_direct+ --ctest-args -R ReleasePolicy"
```

For M4:

```bash
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m4_behavior_arbiter \
   --event-handlers console_direct+ --ctest-args -R M4NodeLifecycle"
```

Expected: new test fails for the diagnosed reason.

- [ ] **Step 4: Implement minimal lifecycle fix**

Allowed fix shapes:

- M6 release certificate: require explicit past-clear/opening/no-cross-ahead condition before deleting encounter state.
- M6 re-arm hysteresis: keep resolved encounter suppressed until target id/rule has clear separation evidence.
- M4 transition latch: require M6 release certificate or M7/M1 escalation before leaving active avoidance/recovery.

Disallowed fix shapes:

- Increasing dwell constants without evidence.
- Branching on scenario name.
- Lowering gate thresholds.
- Treating missing target as safe without M2 evidence.

- [ ] **Step 5: Build and test changed package**

Run the package-specific `colcon build` and `colcon test` command from Step 3. Expected: selected package passes.

- [ ] **Step 6: Run affected probe subset**

Run only scenarios from the fixed fault group:

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1
python3 scripts/run_colregs_clean_8probe.py \
  --include-intelligent \
  --restart-between-runs \
  --scenario <scenario-id> \
  --summary-out runs/single_<scenario-id>_$(date +%Y%m%d_%H%M%S)_lifecycle_fix.json
```

Expected: fixed gate improves or chain diagnosis moves downstream. Other gates must not regress inside the same scenario.

- [ ] **Step 7: Commit**

```bash
git add <changed source files> <changed test files> handoff/workspace_log.md
git commit -m "fix(colregs): stabilize encounter lifecycle contract"
```

---

## Task 7: Fix M5/L4 Safety Contract Break

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/guidance.py`
- Modify: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp`
- Test: `src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py`

**Interfaces:**
- Consumes: active M6/M4 constraints and M5 route-return plan.
- Produces: route return and corridor guard that cannot reduce active CPA/risk safety.

- [ ] **Step 1: Write failing L4 test**

Add to `test_guidance_adapter.py`:

```python
def test_active_colregs_avoidance_not_replaced_by_route_return_heading():
    node = L4GuidanceAdapterNode.__new__(L4GuidanceAdapterNode)
    node._avoidance_target_heading_deg = 55.0
    node._target_heading_deg = 0.0
    node._target_sog_kn = 10.0
    node._route_wps = [(63.0, 10.0), (63.02, 10.0)]
    node._current_target_wp_lat = 63.02
    node._current_target_wp_lon = 10.0
    node._avoidance_heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._heading_controller = HeadingController(Kp=1.0, max_rate_deg_s=100.0)
    node._speed_controller = SpeedController()
    node._last_behavior_plan = SimpleNamespace(rationale="COLREG_AVOID")
    waypoint = SimpleNamespace(target_speed_kn=10.0, turn_radius_m=300.0)
    node._last_avoidance_waypoint = waypoint
    node._last_avoidance_waypoints = [waypoint]

    east_deg = 350.0 / (111319.9 * math.cos(math.radians(63.0)))
    own = {
        "lat": 63.005,
        "lon": 10.0 + east_deg,
        "heading_deg": 0.0,
        "sog_kn": 10.0,
        "rot_deg_s": 0.0,
    }

    for _ in range(4):
        cmd = L4GuidanceAdapterNode._compute_avoidance_transit_command(node, own)

    assert cmd is not None
    assert node._avoidance_heading_controller.last_cmd_deg != 0.0
    assert node._avoidance_target_heading_deg == pytest.approx(55.0)
```

This uses the existing direct-node test style in the file and checks that active COLREGs heading authority remains latched while route XTE is large.

- [ ] **Step 2: Run L4 test and verify failure**

```bash
python3 -m pytest src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py -q
```

Expected: new test fails because route-following or corridor guard can override active COLREGs avoidance.

- [ ] **Step 3: Implement L4 guard**

Change the guidance decision so active COLREGs avoidance or recovery with low CPA/risk margin keeps M5/M4 authorized heading. Route-following may resume only when M6 release certificate is present or M4 behavior is `TRANSIT`.

Allowed implementation:

```python
if colregs_active and cpa_margin_m is not None and cpa_margin_m < active_cpa_floor_m:
    command_source = "m5_avoidance"
    selected_heading_deg = m5_heading_deg
else:
    selected_heading_deg = corridor_guarded_route_heading(...)
```

Use existing variable names in `guidance.py` and `node.py`. Do not introduce a new global threshold; use the active CPA floor already present in scenario/gate data or COLREGs constraint evidence.

- [ ] **Step 4: Add M5 unit test if M5 generated recovery violates active constraint**

Only if Task 5 diagnosis shows M5 recovery is the first broken stage, add a unit test in `test_mid_mpc_solver.cpp` or an existing M5 helper test:

```cpp
TEST(MidMpcRecovery, RecoveryDoesNotReduceActiveCpaMargin) {
  // Arrange active COLREGs constraint with target still not past-clear.
  // Act by building a recovery input.
  // Assert recovery is blocked or remains avoidance until release certificate.
  EXPECT_FALSE(can_start_recovery);
}
```

- [ ] **Step 5: Run tests**

```bash
python3 -m pytest src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py -q
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select m5_tactical_planner"
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner \
   --event-handlers console_direct+"
```

Expected: selected Python and M5 tests pass.

- [ ] **Step 6: Run affected probe subset**

Run scenarios in Task 5 report whose first broken stage is M5 or L4. Expected: CPA/risk/route-return gate improves without new stability failure.

- [ ] **Step 7: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/guidance.py \
        src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_solver.cpp \
        src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py \
        handoff/workspace_log.md
git commit -m "fix: preserve COLREGs safety through M5 and L4 recovery"
```

---

## Task 8: Strict 12-Probe Regression Gate

**Files:**
- Output: `runs/batch_<timestamp>_generalized_final.json`
- Output: `runs/trace_eval/<timestamp>_clean12/`
- Modify: `docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md`
- Modify: `handoff/workspace_log.md`

**Interfaces:**
- Consumes: all previous task commits.
- Produces: final acceptance evidence and remaining-risk decision.

- [ ] **Step 1: Run targeted tests for changed packages**

Run all relevant commands from changed tasks:

```bash
python3 -m pytest tools/sil/test_colregs_chain_trace.py tests/tools/test_trajectory_dashboard.py -q
python3 -m pytest src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py -q
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner"
COMPOSE_PROJECT_NAME=colregs-generalization-debug docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner \
   --event-handlers console_direct+"
```

Expected: all selected tests pass.

- [ ] **Step 2: Run strict clean 12-probe**

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1
python3 scripts/run_colregs_clean_8probe.py \
  --include-intelligent \
  --restart-between-runs \
  --summary-out runs/batch_$(date +%Y%m%d_%H%M%S)_generalized_final.json
```

Expected target: 12/12 PASS. If not 12/12, the report must show first broken stage for each remaining RED.

- [ ] **Step 3: Apply scenario/threshold approval gate**

If any remaining RED has coherent L2/M2/M6/M4/M5/L4/M7 behavior but fails because the scenario or threshold is coupled, write this exact section in the report:

```markdown
## User Approval Required

Scenario: `<scenario-id>`
Coupled setting: `<file and field>`
Observed conflict: `<evidence from chain summary and gate>`
Code behavior: `<why modules are coherent>`
Requested decision: approve scenario/threshold change or keep scenario RED.
```

Stop implementation and ask the user before editing the setting.

- [ ] **Step 4: Commit final report**

```bash
git add docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Debug_Report.md handoff/workspace_log.md
git commit -m "docs: record COLREGs generalized final gate"
```

---

## Self-Review Checklist

- Spec coverage: Tasks 1-2 implement Phase A trace contract; Task 3 covers M5 oscillation evidence; Task 4 covers lifecycle/L4 evidence; Tasks 6-7 cover lifecycle and planner/execution safety contracts; Task 8 covers strict 12-probe gate.
- No scenario-specific fix path exists in any task.
- Scenario or threshold coupling requires user approval before edits.
- Every behavior task starts with a failing test and ends with package tests plus a commit.
- Generated evidence is recorded in reports/handoff; raw `runs/` files are not committed unless already tracked.

## Execution Choice

Plan complete and saved to `docs/superpowers/plans/2026-06-22-colregs-generalized-repair.md`. Two execution options:

1. Subagent-Driven (recommended) - dispatch a fresh subagent per task, review between tasks, fast iteration.
2. Inline Execution - execute tasks in this session using executing-plans, batch execution with checkpoints.

Which approach?
