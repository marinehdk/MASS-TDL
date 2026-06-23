# COLREGs Generalized Repair Design Spec

## Metadata

| Field | Value |
|---|---|
| Date | 2026-06-22 |
| Branch | `codex/colregs-generalization-debug` |
| Worktree | `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-generalization-debug` |
| Scope | Generalized COLREGs avoidance-chain repair after strict 12-probe 5/12 state |
| Chosen approach | Full-chain encounter contract: L2 -> M2 -> M6 -> M4 -> M5 -> L4 -> M7 -> M8 |

## Context

The current COLREGs state is not a single-scenario tuning problem. The latest strict 12-probe evidence from the behavior-fix line was 5/12 PASS, with failures spanning CPA margin, phase semantics, stability, risk gate, route return, and seamanship. A repair that changes one scenario geometry, one gate threshold, or one module output can make another probe fail because the defect crosses module boundaries.

The simulation review guide defines a five-stage avoidance story:

1. Transit and discovery.
2. Risk trigger and rule assessment.
3. Arbitration and maneuvering.
4. Monitoring and safety veto.
5. Clear and return.

The software chain for those stages is:

```text
L2 route/speed
  -> M2 world model, CPA/TCPA, target identity, geometry
  -> M6 COLREGs rule, role, direction, timing, release
  -> M4 behavior FSM and arbitration envelope
  -> M5 planner trajectory, solver/fallback/recovery status
  -> L4 guidance and execution
  -> M7 independent veto/MRM
  -> M8 evidence and UI
```

## Observed Fault Classes

The next repair must treat these as linked symptoms, not independent scenario bugs:

| Fault class | Seen in current probes | System meaning |
|---|---|---|
| CPA margin below floor | Rule 13/15 variants | Avoidance magnitude or route-return pressure can still violate hard safety margin. |
| Phase semantics failure | Rule 15 edge/boundary cases | CPA can pass while Rule 8/13/15 semantics fail: no crossing ahead, ample time, past-and-clear. |
| Stability oscillation | Intelligent target/head-on variants | Encounter or behavior lifecycle can re-arm/release repeatedly under short target or projection changes. |
| Risk gate failure | Target-giveway/overtake variants | Dynamic-domain exposure is not yet a first-class planning constraint. |
| Route return/seamanship failure | Boundary and recovery variants | Recovery can compete with active COLREGs clearance instead of being subordinate to it. |
| M5 `NORMAL`/`DEGRADED` oscillation | Observed in UI during simulation | UI status conflates planner health, fallback mode, behavior mode, valid waypoint state, and L4/lifecycle takeover. |

## Source Evidence

Repository evidence checked in this worktree:

- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:377-409`: M5 publishes `NORMAL`, `DEGRADED`, or `RECOVERY` based on M4 behavior, solver result, heading-window feasibility, geometric fallback, and target-reaching checks.
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:449-459`: geometric fallback publishes `status = "DEGRADED"` and `confidence = 0.6`.
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:526-533`: recovery publishes `status = "RECOVERY"` and route-return rationale.
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:588-613`: M5 ASDR/SAT evidence currently records status, waypoint count, solve time, IPOPT iteration count, and rationale, but not enough upstream/downstream context to explain oscillation.
- `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py:398-424`: lifecycle treats an M5 plan as valid when it has waypoints and nonzero turn radius, and releases avoidance when no valid plan remains.
- `src/sim_workbench/sil_lifecycle/sil_lifecycle/lifecycle_mgr.py:445-448`: lifecycle enables autopilot when M5 is stale for more than 10 seconds or M4 is in fallback rationale.
- `/Users/marine/Desktop/COLREGs/MASS-TDL 避碰决策可视化与仿真追踪指南.md`: user-facing five-stage debugging guide and UI-to-module mapping.

## Non-Goals

- No scenario-specific fixes.
- No threshold lowering to pass current 12 probes.
- No scorer relaxation.
- No mocks, skips, or forced PASS paths.
- No `scenario_id` branches in decision logic.
- No route geometry edits unless a scenario is proven physically invalid by chain evidence and documented separately.
- No M5-only or L4-only "make it green" repair that bypasses M6/M4/M7 responsibilities.

## Design Principle

Every fix must be contract-driven:

```text
For any scenario failure:
  identify active encounter
  identify broken stage contract
  prove upstream input continuity
  prove module internal state transition
  prove output message semantics
  prove downstream consumer behavior
  only then change code
```

The implementation plan that follows this spec must therefore start with trace/evidence upgrades where current evidence is insufficient. If the system cannot explain a transition, the first fix is observability, not behavior tuning.

## Stage Contracts

### Stage 1: Transit And Discovery

Responsible modules: L2, M2, M1.

Required inputs:

- L2 planned route with stable route identity/hash.
- L2 speed profile with stable target speed.
- Own-ship state and target detections.
- M1 ODD envelope and safety thresholds.

Required outputs:

- M2 world state with target identity, range, bearing, CPA, TCPA, confidence, and geometry pre-classification.
- No COLREGs action unless risk trigger is met.

State-holding requirement:

- M2 must preserve target identity across short perception jitter.
- L2 must not republish semantically identical routes in a way that resets downstream state.

Failure signals:

- Target appears/disappears around trigger range.
- Route hash changes without real route change.
- CPA/TCPA jumps enough to reclassify encounter.

### Stage 2: Risk Trigger And Rule Assessment

Responsible modules: M2, M6, M1.

Required inputs:

- M2 target geometry and CPA/TCPA.
- M1 ODD thresholds.
- Previous M6 encounter lifecycle state.

Required outputs:

- M6 `COLREGsConstraint` with rule, role, preferred direction, timing phase, target id, release reason, and confidence.
- Stable encounter id for the same vessel/rule episode.

State-holding requirement:

- M6 must not re-arm an encounter solely because one projection sample briefly resolves.
- Rule 13/14/15 release must be tied to past-and-clear semantics, not only instant CPA.

Failure signals:

- Repeated conflict/no-conflict toggles for one target.
- Rule or role changes without corresponding M2 geometry change.
- Release before no-cross-ahead, ample-time, or past-clear condition.

### Stage 3: Arbitration And Maneuvering

Responsible modules: M6, M4, M5, L4.

Required inputs:

- M6 rule/role/direction constraints.
- M4 current behavior state and transition dwell.
- M5 current solver/fallback/recovery state.
- L4 execution state.

Required outputs:

- M4 behavior plan: `TRANSIT`, `AVOIDANCE`, `RECOVERY`, or MRC behavior with stable rationale.
- M5 trajectory or explicit empty plan whose meaning is unambiguous.
- L4 command that honors M4/M5 authority.

State-holding requirement:

- M4 owns behavior mode; M5 planner health must not directly flip M4 behavior.
- M5 `status` must distinguish planner health (`SOLVER_CONVERGED`, `GEOMETRIC_FALLBACK`, `RECOVERY`, `EMPTY_TRANSIT`) from behavior mode (`TRANSIT`, `AVOIDANCE`, `RECOVERY`).
- L4 route-following/corridor guard must not override CPA/risk hard safety during active COLREGs constraints.

Failure signals:

- M5 `NORMAL`/`DEGRADED` flips while M4/M6 inputs are stable.
- L4 returns toward route before M6 release.
- M4 exits AVOIDANCE because M5 emits an empty or transient plan.

### Stage 4: Monitoring And Safety Veto

Responsible modules: M2, M5, M7, M1.

Required inputs:

- Current and forecast target states.
- M5 selected trajectory and alternatives.
- M7 independent risk model.

Required outputs:

- M7 veto/MRM only when independent safety criteria require it.
- M1 ODD escalation if risk exceeds envelope.

State-holding requirement:

- M7 must stay independent and simpler than the doer path.
- Dynamic-domain risk must be visible before it becomes a final gate failure.

Failure signals:

- M7 veto appears without visible M2/M5 risk cause.
- Risk gate fails post-run but no runtime module exposed risk trend.

### Stage 5: Clear And Return

Responsible modules: M6, M4, M5, L4.

Required inputs:

- M6 release certificate: past-and-clear, no crossing ahead, ample time, CPA/risk floor.
- M4 recovery latch and route-return policy.
- M5 recovery trajectory with CPA/risk preservation.

Required outputs:

- M4 `RECOVERY` before final `TRANSIT` when XTE or heading requires controlled return.
- M5 recovery plan that decays XTE without violating active COLREGs clearance.
- L4 execution that follows recovery until completion criteria are met.

State-holding requirement:

- Recovery cannot start from "instant conflict false" alone.
- Return-to-route must remain subordinate to active COLREGs clearance.

Failure signals:

- Route return starts while target is not past-and-clear.
- XTE improves but CPA/risk or phase semantics regress.
- Recovery/avoidance toggles repeatedly.

## M5 Normal/Degraded Oscillation Investigation Model

The M5 popup observed in simulation shows `NORMAL` and `DEGRADED` alternating with different speeds, waypoint counts, horizons, and confidence. This must be debugged as a chain fault.

Required trace fields per M5 solve cycle:

- `sim_t`, `stamp`, `route_hash`, `speed_profile_hash`.
- M4 behavior enum, heading window, speed window, rationale.
- M6 conflict flag, target id, rule id, role, preferred direction, timing phase, release reason.
- Solver status, solve duration, IPOPT iterations, trajectory size.
- Fallback reason: `wrapped_heading_window`, `solver_failed`, `m4_geometric`, `nlp_misses_colregs_target`, or none.
- Published plan status and semantic mode.
- Waypoint count, first waypoint turn radius, horizon, confidence.
- Lifecycle valid-plan boolean, last valid plan age, autopilot enabled, avoidance active.
- L4 selected heading/rudder/throttle source.

Acceptance for this investigation:

- A single replay can explain every `NORMAL`/`DEGRADED` transition from source data.
- If M4/M6/L2 inputs are stable but M5 status flips, the fix belongs in M5 solver/fallback status handling or hysteresis.
- If L2 route hash or M6 encounter id changes at the same time, the fix belongs upstream.
- If L4/lifecycle releases despite M4/M6 still active, the fix belongs in execution handoff.

## Chosen Approach: Full-Chain Encounter Contract

Implement the next repair in phases, but keep one invariant: each phase must preserve the full chain evidence.

### Phase A: Trace Contract

Add missing evidence before behavior changes. Strict 12-probe and frontend runs must archive enough data to reconstruct all stage contracts and M5 oscillation.

Deliverables:

- Per-scenario chain timeline summary.
- M5 solve-cycle ASDR/SAT expansion or trace enrichment.
- Route hash and speed-profile hash in trace.
- M6 encounter lifecycle/release evidence.
- Lifecycle/L4 execution source evidence.

### Phase B: Encounter Lifecycle Stability

Stabilize M6/M4 encounter state where evidence proves re-arm/release churn.

Deliverables:

- M6 release certificate with explicit semantic gates.
- M4 behavior latch rules tied to M6 certificate and M7/M1 escalation.
- Unit tests for short projection gaps and intelligent target dynamics.

### Phase C: Planner/Execution Safety Contract

Make M5/L4 honor CPA/risk/phase as hard constraints during avoidance and recovery.

Deliverables:

- M5 status semantics split: planner health vs behavior mode.
- CPA/risk-aware recovery path.
- L4 corridor/route guard cannot reduce CPA/risk below active floor.
- Unit tests for route-return versus CPA/risk conflict.

### Phase D: Gate Unification

Ensure CLI runner, trace evaluator, dashboard, and frontend evidence agree.

Deliverables:

- One verdict formula for strict 12-probe.
- PNG dashboard and batch summary cannot disagree.
- Failure taxonomy table generated from evidence.

## Verification Requirements

Minimum verification before promotion:

- Targeted unit tests for every changed module.
- Replay or trace-level tests for each broken stage contract.
- Strict clean 12-probe with restart between runs.
- Per-scenario dashboard PNG in English/ASCII with verdict matching batch summary.
- No scenario geometry edits unless separately justified as invalid scenario setup.
- No local OrbStack promotion until strict 12-probe passes or the remaining failure is documented as non-code scenario invalidity.

## Open Questions For Implementation Plan

These are not placeholders for implementation; they are decision points for the next plan:

- Whether M5 status should add new enum/string values or preserve message compatibility by adding rationale fields first.
- Whether route hash belongs in L2 message, M5 ASDR, trace writer, or all three.
- Whether L4 lifecycle takeover state should be traced via existing `/sil/lifecycle_status` or a new execution-source topic.
- Whether M6 release certificate needs a message extension or can initially be carried in `COLREGsConstraint.rationale`.

## Success Criteria

The next implementation is successful when:

- Each strict 12-probe failure has a stage-contract diagnosis before code change.
- M5 `NORMAL`/`DEGRADED` transitions are explainable and no longer mask behavior-mode changes.
- Recovery never violates active COLREGs semantics.
- L2/M2/M6/M4/M5/L4/M7 each show active, coherent input/output during avoidance.
- Strict 12-probe reaches all GREEN without threshold lowering, mocks, skips, or scenario-specific branches.
