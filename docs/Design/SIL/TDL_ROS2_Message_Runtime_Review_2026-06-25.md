# MASS-L3 TDL ROS 2 Message Runtime Review

> Date: 2026-06-25<br>
> Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime`<br>
> Runtime: `mass-l3-sil` compose project, mounted from `main-runtime`<br>
> Input report: `/Users/marine/Desktop/COLREGs/集成系统对接/TDL_ROS2_Message_Analysis_Report.md`

## 0. Executive Summary

This review updates the external ROS 2 message analysis report against current source and live DDS runtime. Main conclusion: the original package-level inventory is mostly correct, but the runtime reality is more nuanced than a simple "topic namespace split" finding.

Current system already has a mostly canonical `/l3/...` production bus for M1-M8, L2/Fusion ingress, HMI bridge, M7, L4 adapter, and SIL trace bridge. Remaining high-risk interface issues are concentrated in four places:

1. **Residual legacy names in source/remap boundary**: M5 MID-MPC source still publishes `/m5/avoidance_plan`, `/m5/asdr_record`, `/m5/sat_data`, while live runtime remaps them to `/l3/...`. BC-MPC source still uses `/m2/world_state`, `/m5/avoidance_plan`, `/m5/reactive_override_cmd`.
2. **External override topic split**: M7 subscribes `/l3/override/active`, but M1 and M8 still subscribe `/override/active_signal`. No publisher existed for either topic during the checked runtime.
3. **Type collision on `/l3/fsm_state`**: live DDS exposes both `l3_msgs/msg/FsmState` and `sil_msgs/msg/LifecycleStatus` on the same topic name. `fsm_aggregator` publishes `l3_msgs/FsmState`, while `sil_topic_bridge` subscribes `sil_msgs/LifecycleStatus`; this is a real topic contract defect.
4. **Multiple writers for transparency/audit helper buses**: `/sil/sotif_metrics` has M7 and M8 publishers with different QoS; `/l3/asdr/record` has nine publishers by design/implementation, but this should be documented as an event fan-in bus, not a single authority topic.

The single scenario run `colreg-rule14-ho` produced valid runtime message evidence. It failed the COLREG behavioral gate (`turn_starboard` RED), so it must not be cited as functional acceptance. It is still valid for ROS 2 message-flow evidence because the run produced complete trace data: 21,842 trace records, including `/sil/own_ship_state`, `/l3/m4/behavior_plan`, `/l3/m5/avoidance_plan`, `/l3/m6/colregs_constraint`, `/sil/actuator_cmd`, and `/l3/asdr/record`.

## 1. Evidence Used

### 1.1 Source Evidence

Commands run from `main-runtime`:

```bash
codegraph init -i
codegraph status .
find src -maxdepth 4 \( -name '*.msg' -o -name '*.srv' -o -name '*.action' \) | sort
rg -n "create_(publisher|subscription)<|create_publisher\(|create_subscription\(|/l3/|/sil/|/m[1-8]/|/override/|/checker/|/reflex/" src docker plugins
```

Key source references:

| Evidence | Source |
|---|---|
| `l3_msgs` registers 37 `.msg`, 0 `.srv`, 0 `.action` | `src/l3_tdl_kernel/l3_msgs/CMakeLists.txt` |
| `l3_external_msgs` registers 13 `.msg`, 0 `.srv`, 0 `.action` | `src/l3_tdl_kernel/l3_external_msgs/CMakeLists.txt` |
| `sil_msgs` registers 11 `.msg`, 2 `.srv`, 0 `.action` | `src/sim_workbench/sil_msgs/CMakeLists.txt` |
| top-level `ship_interfaces` registers 1 `.msg` | `src/ship_interfaces/CMakeLists.txt` |
| plugin `ship_interfaces` registers 11 `.msg`, 2 `.srv` | `plugins/l2_external/ros2_ws/src/platform/ship_interfaces/CMakeLists.txt` |
| M5 MID-MPC source still publishes legacy `/m5/...` names | `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:155` |
| BC-MPC source still subscribes/publishes legacy `/m2` and `/m5` names | `src/l3_tdl_kernel/m5_tactical_planner/src/bc_mpc/bc_mpc_node.cpp:34` |
| M8 subscribes legacy override topic | `src/l3_tdl_kernel/m8_hmi_transparency_bridge/src/hmi_transparency_bridge_node.cpp:191` |
| `sil_topic_bridge` subscribes `/l3/fsm_state` as `LifecycleStatus` | `docker/sil_topic_bridge.py:576` |
| M7 publishes `/sil/sotif_metrics` best-effort | `src/l3_tdl_kernel/m7_safety_supervisor/src/sotif/sotif_metrics_publisher.cpp:17` |

### 1.2 Runtime Evidence

Runtime source-mount verification:

```bash
docker inspect -f '{{ index .Config.Labels "com.docker.compose.project.working_dir" }} {{ index .Config.Labels "com.docker.compose.project.config_files" }}' \
  mass-l3-sil-sil-nodes-1 \
  mass-l3-sil-sil-orchestrator-1 \
  mass-l3-sil-foxglove-bridge-1
```

Result: all three containers point at `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/main-runtime` and use:

```text
docker-compose.yml
docker-compose.a4000.yml
docker-compose.plugins.yml
```

Runtime DDS commands:

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic list -t | sort'

docker exec mass-l3-sil-sil-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 node list | sort'

docker exec mass-l3-sil-sil-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic info -v /l3/fsm_state'
```

### 1.3 Scenario Evidence

Command:

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --restart-between-runs \
  --restart-container mass-l3-sil-sil-nodes-1 \
  --summary-out runs/ros2_msg_review_rule14_20260625_135259.json \
  --trace-report-dir runs/trace_eval/ros2_msg_review_rule14_20260625_135259
```

Artifacts:

```text
runs/ros2_msg_review_rule14_20260625_135259.json
runs/trace_eval/ros2_msg_review_rule14_20260625_135259/batch_summary.json
runs/trace_eval/ros2_msg_review_rule14_20260625_135259/manifest.json
runs/trace_eval/ros2_msg_review_rule14_20260625_135259/colreg-rule14-ho.json
runs/trace_eval/ros2_msg_review_rule14_20260625_135259/colreg-rule14-ho.trace_current.jsonl
runs/trace_eval/ros2_msg_review_rule14_20260625_135259/colreg-rule14-ho_trajectory_dashboard.png
```

Runtime result:

| Item | Result |
|---|---:|
| `run_id` | `run-19efd577c82` |
| valid own-ship samples | 6,346 |
| valid sim duration | 624.04 s |
| raw trace rows | 21,842 |
| `/l3/asdr/record` trace rows | 8,434 |
| `/sil/own_ship_state` trace rows | 6,346 |
| `/l3/m4/behavior_plan` trace rows | 2,544 |
| `/l3/m6/colregs_constraint` trace rows | 1,268 |
| `/sil/scoring` trace rows | 1,250 |
| `/sil/actuator_cmd` trace rows | 751 |
| `/l3/m3/mission_goal` trace rows | 628 |
| `/l3/m5/avoidance_plan` trace rows | 621 |
| scenario verdict | RED, `turn_starboard` failed |

## 2. Message Package Inventory

| Package | Current interfaces | Runtime/design role | Review result |
|---|---:|---|---|
| `l3_msgs` | 37 msg / 0 srv / 0 action | M1-M8 internal bus | Inventory correct. Still no Action contract. |
| `l3_external_msgs` | 13 msg / 0 srv / 0 action | L1/L2/Fusion/X-axis/Y-axis/Override/L4 boundary | Inventory correct. Boundary naming mostly good, with override split unresolved. |
| `sil_msgs` | 11 msg / 2 srv / 0 action | SIL scenario/runtime/harness messages | Inventory corrected from "10" comments in CMake to actual 11 msg. Must stay outside M1-M8 production path except adapters/bridges. |
| `src/ship_interfaces` | 1 msg / 0 srv / 0 action | legacy/simple GNC route bridge | Overlaps with `l3_external_msgs/PlannedRoute`; keep adapter-boundary only. |
| plugin `ship_interfaces` | 11 msg / 2 srv / 0 action | external L2/platform plugin boundary | Keep isolated under plugin adapter; do not import into core M1-M8. |

No `.action` files were found in current checked source for these packages. The original report's "long task still uses topic pair" concern remains valid.

## 3. Live Topic Topology

### 3.1 Canonical Production Bus Works for Main M1-M8 Flow

Live runtime confirms these core links are present:

| Topic | Type | Publisher(s) | Subscriber(s) observed |
|---|---|---|---|
| `/l3/m1/odd_state` | `l3_msgs/ODDState` | M1 | M2/M3/M4/M6/M7/M8/L4/bridge |
| `/l3/m2/world_state` | `l3_msgs/WorldState` | M2 | M3/M4/M5/M6/M7/M8/L4/bridge |
| `/l3/m3/mission_goal` | `l3_msgs/MissionGoal` | M3 | M4/M8/L4/bridge |
| `/l3/m4/behavior_plan` | `l3_msgs/BehaviorPlan` | M4 | M5/M7/M8/L4/bridge/FSM |
| `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | M5 | M7/M8/L4/bridge/FSM |
| `/l3/m6/colregs_constraint` | `l3_msgs/COLREGsConstraint` | M6 | M4/M5/M7/M8/bridge |
| `/l3/m7/safety_alert` | `l3_msgs/SafetyAlert` | M7 | M1/M8/L4/FSM |
| `/l3/m8/ui_state` | `l3_msgs/UIState` | M8 | SIL bridge |

Runtime `ros2 node info /m5_mid_mpc_node` confirms live remap already exposes M5 as `/l3/m5/avoidance_plan`, even though source code still says `/m5/avoidance_plan`.

### 3.2 Remaining Topic Contract Defects

| ID | Defect | Evidence | Risk | Fix direction |
|---|---|---|---|---|
| ROS2-001 | `/l3/fsm_state` has two types | `ros2 topic list -t` shows `[l3_msgs/msg/FsmState, sil_msgs/msg/LifecycleStatus]`; `fsm_aggregator` publishes `l3_msgs/FsmState`; `sil_topic_bridge` subscribes `LifecycleStatus` | subscribers can silently miss data or confuse HMI/tooling | Change `sil_topic_bridge` subscription type to `l3_msgs/FsmState` or move lifecycle mirror to another topic. |
| ROS2-002 | override namespace split | M7 listens `/l3/override/active`; M1/M8 listen `/override/active_signal`; runtime has no publisher | override state can reach only part of system depending on publisher name | Freeze canonical topic as `/l3/override/active`; update M1/M8 and any external publisher adapter. |
| ROS2-003 | M5 source still uses legacy names | MID source uses `/m5/avoidance_plan`; BC source uses `/m2/world_state`, `/m5/avoidance_plan`, `/m5/reactive_override_cmd` | launch remap hides defect for MID only; BC is fragile/off-canonical | Move canonical names into source or shared topic constants; only remap external/system boundaries. |
| ROS2-004 | `/sil/sotif_metrics` has two publishers with different QoS | runtime: M7 best-effort volatile, M8 reliable transient-local | HMI may receive mixed/stale metrics from two authorities | Pick one authority. Recommended: M7 authoritative publisher; M8 consumes and republishes only if explicitly marked HMI mirror. |
| ROS2-005 | `/l3/asdr/record` has nine publishers | runtime topic info: M1/M2/M3/M4/M5/M6/M7/M8/L4 all publish | acceptable if event fan-in; unsafe if interpreted as single-state source | Document as append-only audit event bus with `source_module`, `event_id`, `decision_id`, `stamp`, `schema_version`. |

## 4. QoS Observations

Observed runtime QoS is mixed but mostly consistent with state/event split:

| Topic | Observed QoS | Review |
|---|---|---|
| `/l3/m5/avoidance_plan` | publisher reliable depth 10; some subscribers best-effort depth 5 | tolerable for SIL/HMI observers; safety consumers should stay reliable. |
| `/l3/m4/behavior_plan` | publisher reliable depth 1 | good for latest-state arbitration output. |
| `/l3/m6/colregs_constraint` | publisher reliable depth 5 | good for M4/M5/M7 consumers. |
| `/l3/m7/safety_alert` | reliable transient-local in M8 subscription; M7 publisher reliable volatile | consider transient-local only if late joiners must see last alert. |
| `/sil/actuator_cmd` | L4 publishes best-effort depth 5; ship dynamics subscribes best-effort depth 1 | reasonable for high-rate actuator stream; stale replay avoided. |
| `/sil/sotif_metrics` | M7 best-effort volatile; M8 reliable transient-local | inconsistent authority/QoS; should be split or normalized. |

Recommended QoS contract:

| Semantic class | QoS |
|---|---|
| high-rate sensor/runtime stream | `best_effort`, `volatile`, small depth |
| latest state needed by late joiners | `reliable`, `transient_local`, bounded depth |
| safety event/audit event | `reliable`, bounded depth, explicit `event_id`/dedup |
| actuator/override command | `reliable` or checked best-effort with short TTL, `volatile`, no stale replay |
| HMI-only mirror | best-effort allowed, never safety authority |

## 5. Scenario Review Result

`colreg-rule14-ho` was run to exercise the message graph, not to tune the scenario. Result was RED:

```text
OVERALL: 0/1 PASS
RED reason: stability turn_starboard
min CPA: 366.2 m, CPA floor: 180 m, CPA pass: true
returned_to_route: true
max route XTE: 462.8 m / pass limit 550 m
M5 solver states: EMPTY=37, VALID=584
trace records: 21,842
```

Message-flow evidence from this run is strong:

- M2/M4/M5/M6/M3/ASDR/SIL actuator streams all appeared in trace.
- `/l3/m5/avoidance_plan` had 621 trace rows, matching runtime canonical topic, not legacy `/m5/avoidance_plan`.
- `/l3/m6/colregs_constraint` had 1,268 trace rows and fed M4/M5/M7/HMI/bridge subscribers.
- `/sil/actuator_cmd` had one runtime publisher, `l4_guidance_adapter`, matching current de-shadowed control path.

Functional acceptance evidence is negative:

- Scenario failed behavioral stability because `turn_starboard` was RED.
- This document must not be used as COLREG PASS evidence.

## 6. Updated Recommendations

### P0: Fix Contract Defects Before Expanding Interfaces

1. Fix `/l3/fsm_state` type collision.
2. Normalize override topic to `/l3/override/active`.
3. Remove or remap legacy M5/BC-MPC topic names in source.
4. Make `/sil/sotif_metrics` single-authority or split into `/l3/m7/sotif_metrics` and `/sil/sotif_metrics`.

### P1: Freeze Topic Contract File

Add one machine-readable contract under `docs/Design/SIL/` or `config/topics/`:

```yaml
topics:
  /l3/m5/avoidance_plan:
    type: l3_msgs/msg/AvoidancePlan
    authority: M5
    semantic: latest_state
    qos: reliable_volatile_depth_10
    allowed_publishers: [m5_mid_mpc_node]
  /l3/fsm_state:
    type: l3_msgs/msg/FsmState
    authority: fsm_aggregator
    semantic: hmi_state
    qos: best_effort_volatile_depth_10
    allowed_publishers: [fsm_aggregator]
```

Then add CI/runtime smoke checks:

```bash
ros2 topic list -t
ros2 topic info -v /l3/fsm_state
ros2 node info /m8_hmi_transparency_bridge
```

Fail the check if a canonical topic has more than one type, unexpected publisher, or missing required subscriber.

### P2: Keep Message Package Boundaries

Rules to freeze:

- M1-M8 production code may depend on `l3_msgs` and selected `l3_external_msgs`.
- M1-M8 production code must not directly depend on `sil_msgs`.
- `sil_msgs` belongs to SIL nodes and adapter/bridge code.
- plugin `ship_interfaces` belongs to plugin adapter boundary.
- duplicated names such as `OwnShipState`, `EnvironmentState`, and `RoutePlan` must be package-qualified in docs and tests.

### P3: Add Action Only Where Long-Task Semantics Are Real

The original report recommends ROS 2 Actions for long tasks. Runtime review supports this direction, but only for real goal/feedback/cancel/result workflows:

| Candidate | Current mechanism | Recommendation |
|---|---|---|
| L2 route replan | `RouteReplanRequest` + `ReplanResponse` topic pair | keep topic-pair short term; add `correlation_id`, timeout, result status; migrate to Action only after L2 adapter accepts cancel/feedback. |
| MRC / emergency recovery | event/state topics | Action likely useful if ROC/operator needs progress/cancel/result. |
| operator handover / ToR | `ToRRequest` topic | Action useful only if responsibility transfer state machine becomes bidirectional. |

Do not add `.action` files only for architecture neatness. Add them when runtime cancellation/progress semantics exist.

## 7. Acceptance Checklist For Next ROS 2 Contract Pass

Required evidence:

```bash
docker inspect mass-l3-sil-sil-nodes-1
ros2 topic list -t
ros2 node list
ros2 topic info -v /l3/fsm_state
ros2 topic info -v /l3/override/active
ros2 topic info -v /sil/sotif_metrics
ros2 node info /m5_mid_mpc_node
ros2 node info /m8_hmi_transparency_bridge
python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho --restart-between-runs --restart-container mass-l3-sil-sil-nodes-1
```

Pass conditions:

- no canonical topic has multiple ROS 2 types;
- every canonical topic has exactly the expected authority publisher count;
- no M1-M8 production node directly consumes `sil_msgs`;
- HMI/SIL mirrors are explicitly named as mirrors;
- one scenario run produces valid trace artifacts;
- if the scenario is used for behavior acceptance, scenario verdict must be GREEN. The 2026-06-25 run in this review is not behavior acceptance because it is RED.

## 8. Final Position

The report's broad architecture direction is right: keep `l3_msgs` as core, keep `l3_external_msgs` as boundary, keep `sil_msgs` out of production core, and freeze a topic contract. Runtime evidence changes the priority order:

1. fix `/l3/fsm_state` type collision first;
2. fix override namespace split second;
3. remove legacy M5 source topic names third;
4. only then consider ROS 2 Actions for replan/MRC/ToR.

These are contract fixes, not scenario geometry fixes.
