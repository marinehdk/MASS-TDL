# TDL Bridge De-shadowing Core Design

Status: approved scope, staged implementation
Date: 2026-06-11
Owner: backend COLREGs / avoidance remediation

## Goal

Remove tactical decision, guidance, control, and safety-gate logic from
`docker/sil_topic_bridge.py` step by step, while preserving current SIL green
behavior at each cutover. Final state: `sil_topic_bridge.py` is a thin SIL I/O
adapter and trace relay; M2/M6/M4/M5/L4/M7 each own their designed
responsibility.

This is now the core backend repair track because recent fixes are fighting
coupling rather than removing it. The bridge stack helped patch real failures,
but it also hid root cause across M6 latch, M4 behavior, M5 plan validity, L4
guidance, route return, and M7 gating.

## Source Baseline

- Architecture report: M5 publishes `AvoidancePlan` and
  `ReactiveOverrideCmd`; L4 is final `(psi, u, ROT)` generator.
- Architecture report SIL chapter: production ROS2 nodes M1-M8 run inside SIL;
  FMI/DDS mediation is ship-dynamics boundary, not tactical bridge authority.
- TDL kernel overview: `sil_topic_bridge.py` is documented as a transitional
  band-aid layer, not production architecture.
- M8 spec: autopilot, latch, CPA/TCPA, heading clamp, XTE, fault injection, and
  COLREGs reasoning are explicitly outside M8/bridge responsibility.
- Current code: `SilTopicBridge` still owns `_check_geometry_release`,
  `_trigger_latch_release`, `_on_avoidance_plan`, `_compute_avoidance_autopilot`,
  `_compute_transit_autopilot`, and `/sil/actuator_cmd` publication.
- Current code: M2 already has `CpaTcpaCalculator` and `WorldState.targets`
  carries `cpa_m` / `tcpa_s`.
- Current code: M6 rules are split into rule-specific files such as
  `rule13_overtaking.cpp`, `rule14_head_on.cpp`, `rule15_crossing.cpp`, and
  `rule17_stand_on.cpp`.
- Current code: M6 already evaluates all targets and carries `target_id` in
  rule evaluations, but the published `COLREGsConstraint` is still dominated by
  one primary target.

Confidence: Medium. Local architecture/code evidence is current. Final
acceptance still requires clean A4000 execution.

## Decision

`bridge` means `docker/sil_topic_bridge.py` in this remediation. It is
allowed to stay temporarily only as a SIL namespace/type adapter, trace writer,
and launch compatibility shim.

Bridge must not own:

- COLREGs conflict, role, phase, preferred direction, or release.
- Avoidance arm/latch/teardown.
- M4 behavior decisions or heading/speed windows.
- M5 waypoint selection, fallback heading, or trajectory validity.
- L4 LOS/WOP, route capture, XTE, rudder, throttle, or speed control.
- M7 veto/hard-stop decision.
- M2 CPA/TCPA/geometric world-state authority.

The existing rule-per-file structure in M6 is acceptable for COLREGs rule
evaluation. It is not acceptable to add scenario-specific Python handlers or
one bridge workaround per rule. Multi-ship coverage must come from a
target-indexed M6 assessment and M4/M5 arbitration, not from more bridge
branches.

## Target Authority Chain

```text
SIL sensors / fusion feed
  -> M2 World Model
     owns tracks, CPA/TCPA, target geometry, confidence
  -> M6 COLREGs Reasoner
     owns per-target rule assessment, role, phase, direction, release
  -> M4 Behavior Arbiter
     owns behavior mode and allowed envelopes from M1/M6/M7/M3
  -> M5 Tactical Planner
     owns route-compatible AvoidancePlan and ReactiveOverrideCmd
  -> L4 SIL Guidance Adapter
     owns LOS/WOP, route capture, heading/speed/ROT, rudder/throttle mapping
  -> M7 Safety Gate
     can veto or force safe command before simulator actuation
  -> SIL dynamics
  -> M8/frontend
     displays the same chain, no tactical inference
```

## Module Responsibilities

| Module | Owns | Must not own |
|---|---|---|
| M2 World Model | own/target state, CPA/TCPA, target geometry, track confidence | COLREGs duty, maneuver choice |
| M6 COLREGs Reasoner | rule assessment, per-target latch, role, phase, direction, release | path generation, rudder/throttle |
| M4 Behavior Arbiter | behavior mode, priority arbitration, heading/speed envelopes | COLREGs reclassification, L4 tracking |
| M5 Tactical Planner | valid avoidance waypoints, speed profile, reactive override | final control, independent COLREGs role |
| L4 SIL Guidance Adapter | LOS/WOP, route capture, actuator-facing command generation | COLREGs duty, M5 plan validity |
| M7 Safety Supervisor/Gate | hard veto and MRC-safe command gate | nominal maneuver planning |
| M8 HMI/Transparency | publish/display explanation and state | tactical inference |
| `sil_topic_bridge.py` | namespace/type relay, trace, launch compatibility | decision, guidance, release, control |

## Single-Target Avoidance Logic

1. M2 computes range, bearing, CPA/TCPA, confidence, and publishes
   `WorldState.targets[]`.
2. M6 evaluates all COLREGs rules for the target, then latches duty until
   finally past-and-clear. It publishes role, phase, preferred direction,
   active rules, target ID, confidence, and rationale.
3. M4 converts M6 authority into `BehaviorPlan`:
   - `STARBOARD` -> starboard allowed heading corridor.
   - `PORT` -> port allowed heading corridor.
   - `REDUCE_SPEED` -> speed envelope change, no fake heading turn.
   - `HOLD` -> transit/stand-on unless M7 or in-extremis path escalates.
4. M5 consumes M4/M6 contracts and emits a valid `AvoidancePlan` with finite
   waypoints and speed adjustments, or explicit degraded/no-plan state.
5. L4 adapter follows the active route or M5 avoidance route using LOS/WOP,
   handles route return, and outputs the actuator-facing command.
6. M7 gate suppresses unsafe command or forces MRC command before the simulator.
7. Bridge relays command and trace only.

## Multi-Target Avoidance Logic

Multi-ship support cannot be covered by one Python file per rule or one
scenario-specific branch per encounter. Required model:

- M2 keeps all active targets with stable `target_id`, CPA/TCPA, uncertainty,
  and freshness.
- M6 evaluates each target independently and publishes enough per-target data
  for audit. `primary_*` may remain for backward compatibility, but the
  internal and next contract must preserve all active target constraints.
- M4 ranks constraints by urgency and authority:
  M7/M1 safety > in-extremis > give-way conflict > stand-on monitoring >
  mission efficiency.
- M5 solves or degrades against the union of active constraints. If constraints
  conflict, it must publish explicit degraded rationale rather than letting L4
  or bridge invent a maneuver.
- L4 follows the selected route/override only; it does not choose which target
  matters.

This supports later dense traffic by extending target arbitration and constraint
aggregation, not by duplicating handler files.

## Migration Stages

### Stage 0: Baseline Freeze

Freeze current bridge behavior with trace rows and clean probe output. The
current bridge patch stack remains a regression baseline, not a target design.

Artifacts:

- Current bridge unit tests.
- M2/M4/M5/M6 local package tests.
- Clean A4000 8-probe trace bundle.
- Route-return status and stability KPIs.

### Stage 1: M2/M6/M4/M5 Contract Authority

Keep bridge as temporary actuator adapter. Remove new tactical growth from
bridge. M2/M6/M4/M5 must expose the authority chain directly and stably enough
that bridge no longer needs to infer target heading or release.

Already partially landed:

- M6 latch release/onset hardening.
- M4 direction-aware COLREGs directive helper.
- M5 explicit COLREGs fallback direction/magnitude.
- Bridge regression guard so empty M5 plan does not clear active M6 authority.

Remaining:

- Confirm no bridge-only CPA/TCPA is needed for release.
- Confirm M4 behavior lifecycle can replace bridge `_avoidance_active`.
- Confirm M5 plan carries enough validity and route-return signal for L4.

### Stage 2: L4 SIL Guidance Adapter

Create an explicit L4-style SIL adapter under `src/sim_workbench/`, because the
repo is L3-focused but SIL needs an architecture-faithful downstream fixture.

The adapter consumes:

- `/l2/planned_route`
- `/l3/m3/mission_goal`
- `/l3/m4/behavior_plan`
- `/l3/m5/avoidance_plan`
- `/m5/reactive_override_cmd` or remapped `/l3/m5/reactive_override_cmd`
- `/fusion/own_ship_state`
- optional `/l3/m7/safety_alert`

The adapter publishes:

- `/l4/guidance_cmd` for trace/debug if a message exists or is added.
- `/sil/actuator_cmd` for current simulator compatibility.

This adapter owns bridge-local heading/speed controllers, XTE, route capture,
lookahead waypoint selection, stale-plan timeout, and reactive override priority.

### Stage 3: M7 Hard Gate

M7 alerts and checker vetoes must affect command output, not only trace. The
gate may live inside the L4 SIL adapter or a small downstream gate node, but it
must be explicit and tested. Bridge must not continue publishing actuator
commands after an active hard veto.

### Stage 4: Bridge Thin Mode Cutover

Run bridge and L4 adapter in dual mode first:

- Bridge still traces all old shadow decisions.
- L4 adapter publishes the real command.
- A comparator records old-vs-new command deltas.
- A kill switch can restore bridge command path for one rollback window.

After clean acceptance:

- Disable bridge actuator command publication.
- Delete bridge release paths.
- Delete bridge target-heading derivation.
- Delete bridge controllers.
- Keep only relays and trace.

### Stage 5: Multi-Target Expansion

After single-target 8-probe is stable, add multi-target probes:

- one give-way crossing + one stand-on monitor target;
- two simultaneous give-way targets with same safe side;
- conflicting constraints requiring speed reduction/degraded plan;
- stale/disappearing target while another target remains active.

Acceptance requires target IDs and rationale to remain inspectable from M2
through M8.

## Completion Criteria

The task is complete only when all are true:

- `docker/sil_topic_bridge.py` has no decision, release, guidance, or control
  branch that can change avoidance behavior.
- M2/M6/M4/M5/L4/M7 ownership is explicit in code and tests.
- Single-target clean COLREGs 8-probe passes with route-return green.
- Route-return has no circling, no U-turn, no sustained wrong-side target
  tracking.
- Stability gate passes or has a documented scorer change with local tests.
- Multi-target smoke probes show per-target constraint trace and no bridge
  tactical fallback.
- Frontend/HMI renders M6/M4/M5/L4/M7 source-of-truth state, not bridge-inferred
  state.

## Guardrails

- Do not add new rule-specific behavior to `sil_topic_bridge.py`.
- Do not delete bridge logic until the target module owns equivalent behavior
  and the clean probe remains green.
- Do not hand-edit generated scenario YAML; update generator when scenario
  geometry changes are needed.
- On A4000, use surgical patch/scp deployment and restart; do not use
  `git pull`, `git reset`, or broad repo sync in dirty worktrees.
