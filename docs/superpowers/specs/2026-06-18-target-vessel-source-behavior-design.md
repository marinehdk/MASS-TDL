# Target Vessel Source and Behavior Policy Design

## Goal

Enhance the SIL target-vessel simulator so route-driven target ships can opt in to a deterministic COLREGs rule FSM for Rule 14, Rule 15/16, and Rule 17 interactions, while preserving existing passive replay behavior, AIS realism, clean8 validation semantics, and the TDL own-ship decision boundary.

Implementation must happen only in the isolated worktree:

- Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/target-vessel-colregs-fsm`
- Branch: `codex/target-vessel-colregs-fsm`

Do not modify `main`, do not change existing clean8 default scenarios, and do not use the local main runtime stack for experiments.

## Current Evidence

- Current active target runtime is `target_vessel_node`, which publishes `/sil/target_vessel_state` and already exposes `TargetMode.INTELLIGENT`; its current behavior is still linear kinematics, with NCDM only adding OU heading noise.
- `lifecycle_bridge.py` injects `targetShips[]` into `target_vessel_node.default_targets_json`.
- `sil_topic_bridge.py` converts `/sil/target_vessel_state` into `/fusion/tracked_targets`, which M2 consumes.
- `COLREG-Consolidated-2018.pdf` establishes the rule constraints used here:
  - Rule 14: reciprocal or nearly reciprocal power-driven vessels both alter course to starboard.
  - Rule 15/16: the give-way vessel keeps out of the way and takes early, substantial action.
  - Rule 17: the stand-on vessel keeps course and speed first, then may or must act when the give-way vessel does not act appropriately.
- `MASS-TDL 目标船缺乏避碰逻辑问题深度研究报告.pdf` identifies the target-vessel gap: current target ships are trajectory sources, not behavior agents, and recommends a deterministic rule-driven MVP before VO/MPC or multi-agent approaches.

## Concept Model

Target-vessel behavior is split into two axes:

```yaml
source:
  type: route
behavior:
  policy: passive
```

`source.type` describes the target ship's nominal navigation source:

- `route`: route, waypoint, or initial heading plus SOG controlled by the simulator.
- `injected_geometry`: runtime-generated route-like encounter geometry controlled by the simulator.
- `ais_replay`: historical AIS replay; not controlled by the simulator.
- `ais_live`: live external AIS; observed only.

`behavior.policy` describes the target ship's collision-avoidance behavior:

- `passive`: no avoidance; preserves existing replay and non-cooperative baselines.
- `ncdm`: OU stochastic heading perturbation, equivalent to the existing NCDM mode.
- `colregs_rule_fsm`: deterministic COLREGs rule FSM for route-driven simulated targets.
- `intelligent_planner`: reserved for future VO/MPC/RL planning; v1 fails fast.
- `tdl_agent`: reserved for future namespaced TDL-as-target; v1 fails fast.

## Scope

v1 implements only:

```yaml
source:
  type: route
behavior:
  policy: colregs_rule_fsm
```

and the equivalent REST-injected route target:

```json
{"rule": "head_on", "mode": "intelligent"}
```

AIS constraints:

- `ais_replay` must not use `colregs_rule_fsm`; modifying historical AIS would destroy replay truth.
- `ais_live` must not use `colregs_rule_fsm`; real external targets are observed only.
- AIS can later support shadow prediction, but v1 must not control AIS-derived target motion.

## Runtime Architecture

```text
targetShips[] YAML
  -> lifecycle_bridge default_targets_json
  -> target_vessel_node
       source route / injected_geometry
       behavior colregs_rule_fsm
       subscribe /sil/own_ship_state
  -> /sil/target_vessel_state
  -> sil_topic_bridge
  -> /fusion/tracked_targets
  -> M2/M6/M4/M5/L4 own-ship chain
```

`target_vessel_node` may subscribe to `/sil/own_ship_state`.

It must not subscribe to or inspect:

- `/l3/m5/avoidance_plan`
- `/l3/m4/behavior_plan`
- `/l3/m6/colregs_constraint`
- any other TDL internal decision topic

This preserves the test condition that target ships infer the own ship's avoidance behavior only from observation, as in real navigation.

## Compatibility

Do not batch-migrate existing YAML in v1.

Legacy mapping:

```text
model: ais_replay_vessel   -> source.type=route, behavior.policy=passive
model: ncdm_vessel         -> source.type=route, behavior.policy=ncdm
model: intelligent_vessel  -> source.type=route, behavior.policy=colregs_rule_fsm
```

If both legacy `model` and new `source/behavior` fields exist, the new fields win and `target_vessel_node` logs a warning.

After v1 tests are stable, a separate migration pass may convert YAML fully to the new source/behavior model. That migration is not part of this implementation.

## Rule FSM

States:

```text
NOMINAL
MONITORING
GIVE_WAY
STAND_ON
RETURNING
```

`emergency` is a flag, not a separate state.

Rule coverage:

- Rule 14: when head-on risk is detected, the target alters course to starboard. It does not cancel its starboard turn merely because the own ship also turns.
- Rule 15/16: when the own ship is on the target's starboard side, the target is give-way and performs an early, substantial starboard alteration. Regular action is turn-first; speed reduction is emergency fallback only.
- Rule 17: when the target is stand-on, it keeps course and speed while monitoring whether the own ship acts. If the own ship does not take appropriate observed action before the action threshold, the target performs independent avoidance.
- Rule 13: v1 preserves passive behavior for the target being overtaken and does not implement complex overtaking strategy.
- Rule 19, TSS, narrow channel, and multi-target action arbitration are out of scope for v1.

Default parameters:

```yaml
behavior:
  policy: colregs_rule_fsm
  reaction_delay_s: 6.0
  min_turn_deg: 30.0
  rot_limit_deg_s: 3.0
  role_lock_s: 20.0
  target_cpa_m: 900.0
  standon_hold_tcpa_s: 180.0
  standon_action_tcpa_s: 90.0
  emergency_tcpa_s: 45.0
  observed_action_heading_delta_deg: 8.0
  observed_action_dcpa_gain_m: 150.0
  clear_dwell_s: 10.0
  return_cooldown_s: 20.0
```

## Route Semantics

For `source.type=route`:

- If `targetShip.nominalRoute` exists, the target follows its current route segment.
- If no `nominalRoute` exists, the target uses `initial.heading` or `initial.cog` plus `initial.sog` as a straight-line nominal route.
- Avoidance temporarily offsets nominal heading.
- After risk clears, the target transitions through `RETURNING`.
- For straight-line route fallback, return to the initial heading or COG.
- For waypoint route, return to the current segment heading.
- v1 does not implement lateral path recapture MPC.

## Observed Action Rule

The target determines whether the own ship has taken appropriate action only through observed motion.

Observed own-ship action is true when any condition holds:

```text
ownship heading change from encounter start >= observed_action_heading_delta_deg
OR predicted DCPA improves by >= observed_action_dcpa_gain_m
OR TCPA is moving away and DCPA >= target_cpa_m
```

This is used mainly by Rule 17 stand-on behavior:

- If the own ship acts appropriately, the target remains `STAND_ON`.
- If the own ship fails to act and TCPA enters the action threshold, the target transitions to independent avoidance.

## Multi-Target Boundary

v1 allows multiple passive targets but at most one `colregs_rule_fsm` target in a scenario.

Implementation must still use per-target state internally:

```text
mmsi -> TargetBehaviorState
```

Geometry helpers and FSM update functions must accept explicit own-state and target-state parameters rather than relying on global singleton state. This keeps the v2 multi-target path open: future work adds a target action arbiter without rewriting per-target FSM logic.

## Error Handling

Fail fast:

- `behavior.policy=colregs_rule_fsm` with `source.type != route`
- illegal FSM parameter values
- more than one `colregs_rule_fsm` target in one configured scenario
- `behavior.policy=intelligent_planner` in v1
- `behavior.policy=tdl_agent` in v1

Degrade at runtime:

- If `/sil/own_ship_state` is temporarily missing or stale, publish the target in passive nominal mode and log a warning.

## Diagnostics

v1 does not change ROS messages and does not add frontend UI.

Diagnostics are:

- `target_vessel_node` logs containing `mmsi`, `rule`, `role`, `state`, `action`, `heading_delta`, `tcpa`, and `dcpa`.
- Trace/runner verification of `/sil/target_vessel_state` heading and ROT changes.

## Test and Acceptance

All tests run from the isolated worktree.

Test layers:

1. Pure unit tests for geometry and FSM transitions.
2. Orchestrator/schema tests for source/behavior normalization, legacy fallback, reserved policy rejection, and REST intelligent injection.
3. New targeted intelligent scenarios:
   - `colreg-rule14-ho-intelligent.yaml`
   - `colreg-rule15-cs-intelligent.yaml`
   - `colreg-rule17-cr-so-target-giveway.yaml`
4. Existing clean8 regression remains separate and must not include the new intelligent scenarios.

Acceptance criteria:

- Existing passive scenarios keep prior semantics.
- `colregs_rule_fsm` route target turns starboard in Rule 14.
- `colregs_rule_fsm` route target takes give-way action in Rule 15/16 when the own ship is on the target's starboard side.
- `colregs_rule_fsm` stand-on target holds when observed own-ship action is appropriate and performs independent action only when thresholds require it.
- No target-vessel logic reads TDL internal decision topics.
- No default clean8 gate is changed by this feature.
