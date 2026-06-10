# Bridge De-shadow Strict 8-Probe Design

## Goal

Make the COLREGs avoidance behavior match the architecture design and pass the strict 8-probe COLREGs suite under `scenarios/COLREGs测试/`. The goal is not only to remove logic from `docker/sil_topic_bridge.py`; it is to eliminate behavior defects such as conflict chatter, behavior flapping, weak or missing avoidance, excessive deviation, circling, U-turns, and misleading frontend state.

## Sources And Confidence

- Architecture design report: M5 Mid-MPC publishes `AvoidancePlan` to L4, BC-MPC publishes `ReactiveOverrideCmd`, and L4 is the final `(psi, u, ROT)` generator.
- Architecture design report: `COLREGs_ConstraintMsg` carries `primary_role`, `primary_preferred_direction`, constraints, and `conflict_detected`; it is the M6-to-M4/M5 contract.
- Architecture design report: scoring includes CPA target, rule compliance, action magnitude penalty, phase score, and trajectory plausibility.
- NLM local answer, high confidence: Rule 8 requires clear early action and avoids a succession of small changes; Rule 13/15 duties must remain stable through the encounter until finally past and clear; HMI should explain decisions, not infer tactical authority.

Confidence: Medium. The architecture and code deviations are local, current sources. Strict 8-probe final pass depends on A4000/SIL runtime availability and must be verified by execution, not inferred.

## Current Deviations

1. Bridge still owns tactical lifecycle and control shadows.
   `docker/sil_topic_bridge.py` still contains avoidance arm/release state, heading/speed controllers, route-return logic, and fallback heading derivation. The recent M6 conflict guard reduces one failure mode but does not make the bridge a thin translator.

2. M4 is not direction-aware enough.
   `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` reads `constraints[].numeric_value` and assumes a starboard-positive turn. It does not treat `primary_preferred_direction` as the first-class M6 authority. This leaves the architecture vulnerable to wrong behavior for `PORT`, `REDUCE_SPEED`, and `HOLD`, and makes boundary cases depend on ad hoc numeric interpretation.

3. M5 fallback infers COLREGs intent from M4 heading windows.
   `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp` and `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` compute fallback magnitude from window edges and hardcoded CPA manipulation. M5 should consume M6 direction/min alteration explicitly, then produce task-level waypoints/speed adjustments.

4. L4 is still partly embedded in Bridge/SIL adapters.
   The design says L4 owns LOS+WOP and final actuator command generation. Current bridge and simulator helpers contain local `HeadingController`/`SpeedController` logic. That logic is acceptable as a temporary SIL stub only if it is explicitly moved behind an L4 boundary.

5. Frontend transparency can still show inferred state.
   M8/frontend must display M6 rationale, M4 behavior, M5 plan, and L4 command. It must not treat bridge-inferred avoidance state as the source of truth.

## Required Architecture

The authority chain is:

```text
M2 WorldState
  -> M6 COLREGsConstraint: role, phase, conflict, preferred direction, min alteration
  -> M4 BehaviorPlan: behavior mode and allowed heading/speed windows
  -> M5 AvoidancePlan / ReactiveOverrideCmd: waypoints and speed profile, or emergency override
  -> L4 Guidance: final heading, speed, ROT, actuator-facing command
  -> Bridge: topic translation only
  -> M8/frontend: transparent display of the same chain
```

No downstream layer may re-decide upstream authority:

- Bridge must not decide conflict, role, avoidance lifecycle, release, heading target, speed target, or actuator control in final architecture.
- M5 must not reclassify COLREGs encounters; it uses M6/M4 constraints.
- M4 must not invent direction when M6 provides `primary_preferred_direction`.
- M8 must not infer tactical truth from bridge state.

## Implementation Strategy

Use staged migration. Do not delete all bridge logic at once. Each stage must preserve or improve strict 8-probe behavior before the next bridge shadow is removed.

### P0 Evidence Gate

Run each strict 8-probe scenario from a clean SIL process and capture trace rows for:

- `/l3/m6/colregs_constraint`
- `/l3/m4/behavior_plan`
- `/l3/m5/avoidance_plan`
- L4/SIL actuator command or bridge command during the transition period
- own/target state and CPA/TCPA

Classify each failure by first toggling layer:

- M6-first: `conflict_detected`, role, phase, or direction toggles before M4 toggles.
- M4-first: M6 stable but `BehaviorPlan.behavior` or heading window flaps.
- M5-first: M4 stable but avoidance plan heading/waypoints flip or become empty.
- L4/Bridge-first: M5 stable but command output flips, releases, or oversteers.

Implementation must not tune blindly before this classification.

### P1 M6 Authority Stabilization

M6 remains the only COLREGs conflict/role/direction authority. Onset-latched duty must preserve role, encounter type, preferred direction, and min alteration until the encounter is finally past and clear. Boundary cases near Rule 13/15 must not flicker between give-way/free/stand-on while the own ship is still obligated to act.

Acceptance:

- `conflict_detected` toggles at most twice in each strict probe.
- The primary role and preferred direction remain stable while `conflict_detected=true`, except for documented Rule 17 escalation.
- Release requires past-and-clear or an explicitly documented safety fallback, not CPA opening caused by the own-ship avoidance turn.

### P2 M4 Direction-Aware Behavior

M4 converts M6 authority into behavior and allowed windows:

- `STARBOARD`: allowed heading starts at own/route heading plus min alteration and extends to a bounded starboard corridor.
- `PORT`: allowed heading starts at own/route heading minus min alteration and extends to a bounded port corridor.
- `REDUCE_SPEED`: heading corridor remains route/own-heading centered; speed max is reduced.
- `HOLD`: no `COLREG_AVOID` activation unless M6 reports an in-extremis conflict requiring action.

M4 must stop encoding a permanent starboard assumption in helpers, hard constraints, fallback rationale, and safety concern messages. If a scenario requires starboard, that must come from M6 direction or rule assessment, not from bridge/M4 defaults.

Acceptance:

- Unit tests cover `STARBOARD`, `PORT`, `REDUCE_SPEED`, and `HOLD`.
- Existing Rule 14/15 starboard scenarios remain green.
- M4 behavior toggles stay under strict thresholds in 8-probe.

### P3 M5 Explicit COLREGs Fallback

M5 owns maneuver magnitude and route-compatible trajectory, but consumes M6/M4 authority explicitly:

- Add internal fields for `colregs_preferred_direction`, `colregs_min_alteration_rad`, and `colregs_conflict_active`.
- Populate them from `COLREGsConstraint.primary_preferred_direction` and `constraints[].numeric_value`.
- Replace fallback target inference from window edges with a helper that accepts explicit direction and min alteration.
- Preserve route-return behavior by keeping fallback waypoints finite, monotonic, and compatible with L4 route-following.

Acceptance:

- Unit tests prove starboard and port fallback target signs.
- `REDUCE_SPEED` does not create a fake heading turn.
- Empty plan remains published when M4 is `TRANSIT`.
- Strict 8-probe CPA/stability improves or remains green per scenario; `ot-boundary` must pass.

### P4 L4 SIL Guidance Boundary

Move bridge-local guidance/control into an L4 SIL adapter or explicitly existing simulator guidance node. The L4 boundary consumes `AvoidancePlan`/`ReactiveOverrideCmd` and own state, then produces final heading/speed/ROT or actuator-facing command.

Acceptance:

- Bridge no longer owns `HeadingController`, `SpeedController`, target heading lifecycle, or route-return control.
- L4 adapter has unit tests for normal route following, avoidance plan following, reactive override validity, and stale-plan release.
- Strict 8-probe stays green after the bridge control path is disabled.

### P5 M8/Frontend Transparency

Frontend-visible state must come from M6/M4/M5/L4 outputs:

- M6: active rules, role, phase, preferred direction, rationale chain.
- M4: behavior and allowed windows.
- M5: planned waypoints/speed adjustments and active constraints.
- L4: commanded heading/speed/ROT or actuator command.

Bridge traces may be shown only as transport/debug data, not as tactical authority.

Acceptance:

- Frontend shows avoidance state when M6/M4/M5 are active.
- Frontend clears avoidance state after M6/M4/M5 resolve.
- Displayed direction and rationale match the executed behavior in strict probes.

### P6 Bridge De-shadow Completion

Remove bridge shadow logic only after P1-P5 pass:

- Remove M5-plan arm/release fallback.
- Remove bridge geometry release and mission/target release logic.
- Remove bridge heading target derivation and hardcoded deviation clamps.
- Remove bridge controllers after L4 adapter owns command generation.

Acceptance:

- `docker/sil_topic_bridge.py` is a ROS/sim translation and trace adapter.
- No tactical branch in bridge can affect avoidance behavior.
- Strict 8-probe and route-return remain green.

## Verification

Local fast tests:

```bash
pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
colcon test --packages-select m6_colregs_reasoner m4_behavior_arbiter m5_tactical_planner --event-handlers console_direct+
colcon test-result --verbose
```

Strict SIL gate:

```bash
python scripts/run_6_scenarios.py --clean-restart --scenarios-dir scenarios/COLREGs测试
```

If the script does not support those flags in the current checkout, use the existing A4000 clean-restart harness documented in `handoff/workspace_log.md`, restarting SIL nodes between scenarios with a settle delay.

The implementation is complete only when the authoritative strict 8-probe run passes and frontend/HMI traces show the M6/M4/M5/L4 chain rather than bridge-inferred tactical state.

## Non-Goals For This Iteration

- Full production L4 certification implementation.
- Full M1 Parameter Store/CapabilityManifest migration for every constant.
- Full multi-target COLREGs game-theoretic planner.
- Deleting all bridge logic before the strict probes are green.
