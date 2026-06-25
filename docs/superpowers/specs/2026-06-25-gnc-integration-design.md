# GNC Integration — Real L4/L5 Stack Replacing SIL Stub (Track A)

Date: 2026-06-25
Target worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-generalization-debug`
Branch: `codex/colregs-generalization-debug`
Status: Draft spec for review
Sequencing: This is Track A, executed **after** Track B (`2026-06-25-ros2-msg-governance-impl-design.md`) merges and passes the local gate. Track A consumes the clean canonical `/l3/...` bus produced by Track B.

## 1. Purpose

Replace the SIL L4 guidance adapter stub (`src/sim_workbench/sil_nodes/l4_guidance_adapter/`) and the SIL plant with the colleague's real GNC stack (C++ ROS2 nodes), running in an isolated DDS domain and bridged into the L3 bus. The driving goal is to let the real GNC's LOS/ILOS guidance + MPC control + `active_route_manager` feasibility gate execute L3's avoidance intent, and in doing so resolve the rule14-ho over-turn limit-cycle that the SIL stub's single-loop PD controller could not.

## 2. Scope

### 2.1 In Scope

- Run the real GNC stack (ship_guidance + active_route_manager + ship_control + thrust_allocation + ship_dynamics + sensor_fusion + env_engines + mission_supervisor + safety_supervisor) in an isolated DDS domain (`ROS_DOMAIN_ID=50`) inside a dedicated compose project.
- Build a `gnc_bridge_node` that is the sole cross-domain bridge, translating L3 canonical messages to GNC `ship_interfaces` messages and vice versa, on canonical `/l3/...` topics per the Track B contract.
- Change M5 Mid-MPC to also emit a waypoint-geometry avoidance plan consumable by `active_route_manager` (the `ship_interfaces/AvoidancePlan` contract), while keeping the existing `l3_msgs/AvoidancePlan` for M7/M8 audit.
- Remove the `l4_guidance_adapter` SIL stub entirely.
- Remove `docker/sil_topic_bridge.py` entirely, migrating its non-overlapping SIL-adapter functions (target-array fan-out, environment field rename, ASDR trace, module pulse) into small purpose-built adapters.
- Migrate the avoidance release authority from the old L4 M6-conflict-clear latch to the GNC `active_route_manager` lifecycle (`valid_until` / 60s-hold / explicit `return_to_route` plan).
- Validate that rule14-ho over-turn is resolved by the GNC `max_yaw_rate_deg_s` feasibility gate (1.2 normal / 2.0 emergency deg/s), not by re-introducing a SIL-side ROT inner loop.

### 2.2 Out of Scope

- Modifying the colleague's GNC source code. The GNC stack runs as-is from the snapshot. If a GNC parameter must change, it changes via the GNC's own `ship_config.yaml` mount, not by editing GNC source.
- Track B's message governance work (must be merged first).
- ROS2 `.action` migration.
- M4 behavior arbiter heading-window redesign. M4 keeps emitting `heading_min/max_deg`; M5 is the conversion point (M5 reads the window and emits waypoints).
- COLREG scenario geometry or gate threshold changes.
- The GNC's own internal mission_supervisor / safety_supervisor / captain_decision logic. These run inside the GNC domain and are not bridged into L3's M1/M7 authority. L3's M1 remains the sole ODD/safety authority; the GNC safety_supervisor is scoped to the GNC plant only.

## 3. Evidence Base

Real GNC snapshot: `/Users/marine/Code/_a4000_snapshots/mpc_latest/mpc/船舶动力学/gnc_ws/src/` (colcon workspace, ROS humble, package format 3).

Key node contracts verified by reading source:

### 3.1 active_route_manager_node (`gnc/ship_guidance/src/active_route_manager_node.cpp`, 623 lines)

- Subscribes: `/route_planning/route_plan` (RoutePlan), `/colav/avoidance_plan` (AvoidancePlan), `/ship/geo_position` (GeoPosition).
- Publishes: `/gnc/active_route` (RoutePlan, transient_local latched), `/gnc/route_execution_status` (RouteExecutionStatus).
- Feasibility gate (`evaluate_avoidance_plan`, lines 270-398): rejects or degrades plans that violate `min_segment_length` (emergency 15m), turn radius (`required_radius = max(static_min[emergency 45m], v²/0.25, v/yaw_rate_limit[emergency 2.0 deg/s])`), or decel distance.
- Avoidance lifecycle: `mark_avoidance_active` holds the avoidance route until `valid_until` or `now + 60s`; during this window nominal RoutePlan is DEFERRED.
- GNC does NOT track `command_heading_deg` (intent-only per docking doc §9.2); it follows waypoint geometry via ship_guidance LOS/ILOS.

### 3.2 ship_guidance_node (`gnc/ship_guidance/src/ship_guidance_node.cpp`)

- LOS/ILOS guidance with adaptive lookahead, arc smoothing, corridor tracking, turn-segment speed gates.
- Subscribes `nav_msgs/Odometry` + `nav_msgs/Path`; publishes `/control/heading_setpoint` + `/control/speed_setpoint` (Float64).
- Zero COLREGs/avoidance awareness (verified: no `colreg/avoid/target/heading_window` references). It is a pure path follower.

### 3.3 ship_control_node (`gnc/ship_control/src/ship_control_node.cpp`)

- PID + sliding-mode + NDO + MPC integrated guidance.
- Subscribes `/control/heading_setpoint` + `/control/speed_setpoint`; publishes `/cmd_tau` (WrenchStamped force/torque).
- `heading_cmd_rate_limit_deg_s` (yaml 1.0) provides a second ROT-rate limit beyond the feasibility gate.

### 3.4 GNC launch (`platform/ship_bringup/launch/sim_launch.py`)

Self-contained: starts env engines + sensor_fusion + guidance + control + thrust_allocation + ship_dynamics + mission_supervisor + safety_supervisor from its own `ship_config.yaml`. Can run as an isolated stack.

### 3.5 L3 SIL stub being replaced (`src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`)

- Single-loop PD heading controller + corridor guard + avoidance heading latch.
- M6-conflict-clear release authority (lines 331-394), avoidance_target_heading_deg latch with 5° hysteresis (lines 363-393), RUDDER_SIGN=-1, corridor guard.
- The cascade ROT inner-loop (commits 530e9265..5a212ed8) attempted to fix rule14-ho over-turn but did not meet thresholds (rot_hold_std 1.7 > 1.5, steering_reversals 10 > 4). This entire approach is abandoned in favor of the GNC feasibility gate.

### 3.6 sil_topic_bridge.py (1774 lines, to be removed)

Six function classes, migrated as follows:

| Function | Migration target |
|---|---|
| SIL→L3 own_ship translation (`/sil/own_ship_state`→`/fusion/own_ship_state`) | `gnc_bridge_node` (real plant ship state comes from GNC `/ship/geo_position`) |
| SIL→L3 target fan-out (`/sil/target_vessel_state`→`/fusion/tracked_targets`) | new `sil_fusion_adapter` |
| SIL→L3 env translation | new `sil_fusion_adapter` |
| avoidance→actuator translation (`/l3/m5/avoidance_plan`→`/sil/actuator_cmd`) | **removed** — GNC consumes avoidance directly; SIL actuator chain abandoned |
| ASDR trace (`/l3/asdr/record`→`/sil/asdr_event`) | new `sil_trace_adapter` |
| module pulse aggregation | new `sil_pulse_adapter` |

## 4. Design Decisions

### 4.1 D1: Isolated DDS domain + single bridge node

The GNC stack runs in `ROS_DOMAIN_ID=50` in a dedicated compose project (`COMPOSE_PROJECT_NAME=codex-gnc`). A single `gnc_bridge_node` running in the L3 domain (`ROS_DOMAIN_ID=42`) is the only cross-domain process, using ROS2 composition or a typed-bridge approach. This isolates GNC upgrades, keeps the L3 bus canonical, and matches the 2026-06-12 external-module-adapter-spec pattern.

### 4.2 D2: Real plant, abandoned SIL plant and SIL actuator chain

The GNC `ship_dynamics` node is the plant when the GNC profile is active. The SIL `ship_dynamics` (in `sil_nodes`) and the `/sil/actuator_cmd` chain are stopped. This means the GNC's own `/cmd_tau`→thrust_allocation→ship_dynamics loop is the execution path; L3 never touches actuator-level commands. Consequence: the SIL scoring/trace that read `/sil/own_ship_state` and `/sil/actuator_cmd` must read the bridged `/ship/geo_position` instead (the bridge republishes it as `/sil/own_ship_state` for trace compatibility).

### 4.3 D3: M5 emits waypoints, not heading window, for the GNC

M5 Mid-MPC gains a second publisher producing `ship_interfaces/AvoidancePlan` (waypoint geometry) on a canonical topic, derived from M4's heading window + own-ship state, satisfying the GNC feasibility constraints. The existing `l3_msgs/AvoidancePlan` (heading window + intent) remains for M7/M8 audit on `/l3/m5/avoidance_plan`. The bridge forwards the waypoint plan to GNC `/colav/avoidance_plan`.

This avoids putting heading→waypoint semantic conversion in the bridge (which would make the bridge behavior-rich, violating the external-adapter-spec "no durable logic in bridge" rule). M5 is the natural waypoint generator.

### 4.4 D4: Avoidance release authority migrates to active_route_manager

The old L4 M6-conflict-clear latch is not re-implemented in the bridge or in M5. Instead:
- While M6 reports conflict, M5 keeps emitting the avoidance waypoint plan with a rolling `valid_until` (e.g. now + 30s), so `active_route_manager` holds the avoidance route.
- When M6 reports conflict-clear (high confidence), M5 emits one `return_to_route` plan (waypoints back to the nominal route), and `active_route_manager` completes the avoidance lifecycle naturally.
- This makes M6 the release authority (preserving the Track B-era invariant) via M5's plan emission, not via a latch in the executor.

### 4.5 D5: L4 stub fully removed; no fallback in this spec

The `l4_guidance_adapter` package is deleted. There is no profile-level fallback to the SIL L4 stub in this spec; the GNC profile is the only execution path once Track A merges. If a fallback is later required, it is a separate spec. The old cascade ROT commits (530e9265..5a212ed8) remain in history for reference but their code is deleted.

### 4.6 D6: Bridge topics are canonical and registered in the Track B contract

All new topics introduced by the bridge are `/l3/...` canonical and added to `ros2-interface-contract.yaml` (amended as part of Track A):
- `/l3/gnc/execution_status` (`ship_interfaces/RouteExecutionStatus` or a wrapped `l3_msgs` type) — consumed by M4/M5/M7 to react to GNC accept/reject/degrade.
- `/l3/m5/avoidance_waypoints` (`ship_interfaces/AvoidancePlan`) — M5's waypoint plan output, bridged to GNC. (Exact name TBD in plan; must be canonical and single-type.)

## 5. Architecture

```
┌─ L3 domain (ROS_DOMAIN_ID=42) ───────────────────────────┐   ┌─ GNC domain (ROS_DOMAIN_ID=50) ──────────┐
│ M1-M8 (l3_msgs, clean /l3/ bus from Track B)             │   │ ship_guidance_node (L4 LOS/ILOS)          │
│ M4 → BehaviorPlan (heading_min/max_deg)                  │   │ active_route_manager_node (feasibility)   │
│ M5 Mid-MPC:                                              │   │ ship_control_node (L5 MPC, /cmd_tau)      │
│   - /l3/m5/avoidance_plan (l3_msgs, audit)               │   │ thrust_allocation_node                    │
│   - /l3/m5/avoidance_waypoints (ship_interfaces, NEW)    │   │ ship_dynamics_node (REAL plant)           │
│                                                          │   │ sensor_fusion_node                        │
│   ┌─ gnc_bridge_node ──────────────────────────────┐     │   │ env_engines (wind/wave/current)           │
│   │ sub /l3/m5/avoidance_waypoints                │─────┼──→│ sub /colav/avoidance_plan                 │
│   │   → forward to /colav/avoidance_plan (GNC dom) │     │   │ sub /route_planning/route_plan            │
│   │ sub /l2/planned_route                          │─────┼──→│   (RoutePlan)                             │
│   │   → /route_planning/route_plan (GNC dom)       │     │   │                                           │
│   │ pub /sil/own_ship_state (trace compat)         │←────┼──│ pub /ship/geo_position (GeoPosition)      │
│   │ pub /l3/gnc/execution_status (NEW canonical)   │←────┼──│ pub /gnc/route_execution_status           │
│   │   → M4/M5/M7 consume accept/reject/degrade     │     │   │                                           │
│   └────────────────────────────────────────────────┘     │   │ GNC's own mission_supervisor /            │
│                                                          │   │ safety_supervisor (scoped to GNC plant)   │
│ sil_fusion_adapter (NEW): /sil/target_vessel_state       │   └───────────────────────────────────────────┘
│   → /fusion/tracked_targets (array fan-out)              │
│ sil_trace_adapter (NEW): /l3/asdr/record → /sil/asdr_event │
│ sil_pulse_adapter (NEW): M1-M8 heartbeat → /sil/module_pulse │
│                                                          │
│ [REMOVED] l4_guidance_adapter (entire package)           │
│ [REMOVED] sil_topic_bridge.py (entire file)              │
│ [STOPPED when GNC profile active] SIL ship_dynamics plant │
└──────────────────────────────────────────────────────────┘
```

## 6. Implementation Tasks

### Task A1: GNC integration worktree + compose profile

- Worktree `.worktrees/gnc-integration` off `main`, branch `codex/gnc-integration`.
- Copy the GNC `gnc_ws` from the snapshot into a vendored path under the L3 repo (e.g. `third_party/gnc_ws/`) or mount it read-only. Decision deferred to plan: vendor-copy vs mount. Vendor-copy is safer for reproducibility.
- Add a `docker-compose.gnc.yml` override + `gnc` compose profile that builds and starts the GNC stack in `ROS_DOMAIN_ID=50`, `COMPOSE_PROJECT_NAME=codex-gnc`.
- GNC `ship_config.yaml` mounted from the snapshot (do not edit GNC source).
- Acceptance: `docker compose --profile gnc up` starts the GNC stack; `ros2 topic list` in domain 50 shows `/ship/geo_position`, `/gnc/active_route`, `/gnc/route_execution_status`.

### Task A2: gnc_bridge_node

New package under `src/sim_workbench/gnc_bridge/` (Python, same pattern as the old L4 adapter for ROS plumbing simplicity, or C++ if perf requires — decision in plan).

Cross-domain bridging (the node joins both domains via two executors or two nodes):
- L3→GNC: `/l3/m5/avoidance_waypoints` (ship_interfaces/AvoidancePlan) → `/colav/avoidance_plan`; `/l2/planned_route` (l3_external_msgs/PlannedRoute) → `/route_planning/route_plan` (ship_interfaces/RoutePlan, field map).
- GNC→L3: `/ship/geo_position` (GeoPosition) → `/sil/own_ship_state` (sil_msgs/OwnShipState, field map for trace compatibility) and a canonical `/l3/gnc/ship_state` if M2/M3 need GNC-frame state; `/gnc/route_execution_status` (RouteExecutionStatus) → `/l3/gnc/execution_status` (canonical, consumed by M4/M5/M7).

No behavior logic in the bridge — pure field mapping and domain forwarding. ASDR record on bridge start/stop and on any GNC reject.

Acceptance: unit tests for each field map; integration test that a published L3 plan appears in GNC domain and a GNC status appears in L3 domain.

### Task A3: M5 waypoint avoidance output

File: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

Add a publisher for `ship_interfaces/AvoidancePlan` on canonical `/l3/m5/avoidance_waypoints`. Generation logic:
- Read M4 `BehaviorPlan.heading_min_deg/heading_max_deg` + own-ship lat/lon/heading + target speed.
- Select avoidance heading (window midpoint, biased toward max for starboard preference).
- Project a waypoint string (4-6 points) along that heading from own-ship position: first point ≥150m ahead, segment lengths ≥15m (emergency), turn radius ≥45m at the chosen speed (v² ≤ radius × 0.25).
- `navigation_mode[] = "emergency_avoidance"`, `behavior_mode = "emergency_avoidance"`, `valid_until = now + 30s` (rolling), `allow_degraded_execution = true`.
- On M6 conflict-clear (high confidence): emit one `return_to_route` plan with waypoints back to the nominal route, then stop emitting avoidance waypoints.
- Keep the existing `/l3/m5/avoidance_plan` (l3_msgs) for M7/M8 audit.

Acceptance:
- Unit test: given a heading window + own-ship state, the generated waypoints satisfy GNC feasibility (segment ≥15m, turn radius ≥45m at target speed, first point ≥150m ahead).
- Unit test: M6 conflict-clear triggers exactly one `return_to_route` plan.
- The existing l3_msgs avoidance plan still publishes for audit.

### Task A4: Remove l4_guidance_adapter + sil_topic_bridge.py + new SIL adapters

- Delete `src/sim_workbench/sil_nodes/l4_guidance_adapter/` entirely. Update `sil_nodes.Dockerfile`/colcon excludes, launch files, tests.
- Delete `docker/sil_topic_bridge.py`. Update `docker/sil_entrypoint.sh` and any compose/launch references.
- Add `src/sim_workbench/sil_fusion_adapter/`: `/sil/target_vessel_state` → `/fusion/tracked_targets` (single→array), `/sil/environment` → `/fusion/environment_state` (field rename). (Only needed if SIL fusion injection is still used when GNC plant is active; if GNC sensor_fusion replaces it entirely, this adapter may be minimal or unused — decision in plan.)
- Add `src/sim_workbench/sil_trace_adapter/`: `/l3/asdr/record` → `/sil/asdr_event` (trace evidence).
- Add `src/sim_workbench/sil_pulse_adapter/`: M1-M8 heartbeat aggregation → `/sil/module_pulse`.

Acceptance:
- `rg -l "l4_guidance_adapter|sil_topic_bridge" src/ docker/` returns zero hits.
- SIL scenario trace still produces ASDR + module pulse + scoring evidence via the new adapters.
- No `/sil/actuator_cmd` publisher remains (GNC plant does not consume it).

### Task A5: GNC profile wiring + SIL plant stop

- `docker-compose.gnc.yml`: when `gnc` profile active, do not start the SIL `ship_dynamics` node; start the GNC stack instead.
- Scenario runner / orchestrator: add a `gnc` mode that expects ship state from the bridged `/ship/geo_position` instead of native SIL plant.
- Scoring/trace: read `/sil/own_ship_state` (now bridged from GNC) for continuity.
- **No legacy SIL fallback path is maintained in Track A.** The GNC profile becomes the default execution path. This is consistent with D5 (L4 stub fully removed). If a legacy SIL fallback is later required, it is a separate spec that re-adds a minimal SIL plant + a minimal L4 executor; Track A does not preserve that path.

### Task A6: rule14-ho over-turn validation

- Run `colreg-rule14-ho` under the GNC profile.
- Trace metrics: `steering_reversals` (threshold ≤4), `rot_hold_std` (threshold ≤1.5), `turn_starboard` (port+stbd within limits), CPA ≥180m, route return.
- Hypothesis: the GNC `max_yaw_rate_deg_s` feasibility gate (2.0 emergency) + ship_guidance LOS rate limiting + ship_control `heading_cmd_rate_limit_deg_s` naturally cap ROT, resolving the limit cycle without a SIL-side cascade.
- If over-turn persists: tune via GNC `ship_config.yaml` parameters (e.g. `emergency_max_yaw_rate_deg_s`, `max_lateral_accel_mps2`, MPC `q/r_weight`), NOT by re-introducing SIL-side ROT logic. Parameter changes are mount-only; GNC source stays untouched.

Acceptance: rule14-ho `turn_starboard` gate passes GREEN (or, if it does not, a clear root-cause trace shows the failure is in M5 waypoint geometry or GNC params, not in a SIL-side controller).

## 7. Acceptance

### 7.1 Build

```bash
source scripts/local-a4000-env.sh
colcon build --packages-select l3_msgs l3_external_msgs sil_msgs \
  m5_tactical_planner gnc_bridge sil_fusion_adapter sil_trace_adapter sil_pulse_adapter
docker compose -f docker-compose.yml -f docker-compose.gnc.yml --profile gnc build
```

### 7.2 Static contract (amended)

```bash
python3 tools/sil/check_ros2_interface_contract.py \
  --contract docs/Design/SIL/ros2-interface-contract.yaml
```

Must pass after amending the contract with `/l3/gnc/execution_status`, `/l3/m5/avoidance_waypoints`, and the new adapter topics.

### 7.3 Runtime

```bash
COMPOSE_PROJECT_NAME=codex-gnc docker compose --profile gnc up -d
# GNC domain 50 topics:
docker exec <gnc-container> bash -lc 'ROS_DOMAIN_ID=50 source /opt/ros/humble/setup.bash && ros2 topic list'
# L3 domain 42 bridge topics:
docker exec <l3-container> bash -lc 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && ros2 topic info -v /l3/gnc/execution_status'
```

Pass conditions:
- GNC stack healthy in domain 50: `/ship/geo_position`, `/gnc/active_route`, `/gnc/route_execution_status` publishing.
- L3 bridge forwarding: `/l3/gnc/execution_status` and `/sil/own_ship_state` (bridged) appear in domain 42.
- No `/sil/actuator_cmd` publisher.
- No `l4_guidance_adapter` or `sil_topic_bridge` node.

### 7.4 Scenario (behavior acceptance — this is the real gate)

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --profile gnc \
  --restart-between-runs \
  --summary-out runs/gnc_integration_rule14_$(date +%Y%m%d_%H%M%S).json
```

Pass conditions:
- rule14-ho `turn_starboard` GREEN.
- `steering_reversals` ≤4, `rot_hold_std` ≤1.5.
- CPA ≥180m, route return true.
- Trace complete: `/l3/m5/avoidance_waypoints`, `/l3/gnc/execution_status`, `/ship/geo_position` (bridged), ASDR.

### 7.5 Local gate

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

## 8. Risk and Rollback

- **Risk: GNC plant params mismatch L3 scenario geometry.** The GNC ship is Lpp=44.1m, mass 220t; the SIL scenario ship may differ. If clean8 geometry breaks, the fix is scenario calibration or GNC `ship_config.yaml` mount tuning, not GNC source edits. Validate early in Task A6.
- **Risk: M5 waypoint generation violates GNC feasibility and gets rejected.** Mitigation: Task A3 unit tests assert feasibility constraints; `active_route_manager` returns `RouteExecutionStatus` with `suggested_action` that M5 can use to adjust.
- **Risk: removing sil_topic_bridge breaks SIL-only scenarios that don't use GNC.** Mitigation: Task A4 new adapters cover the non-overlapping functions; the `gnc` profile is the new default. If a SIL-only path is required, it is a separate spec.
- **Risk: rule14-ho over-turn persists even with GNC.** Mitigation: Task A6 hypothesis is falsifiable; if it fails, the trace will localize the cause (M5 geometry vs GNC params) and the fix is targeted parameter tuning, not architecture rework.
- **Rollback**: Track A is large; each task is independently revertible in isolation, but the full Track A does not preserve a working legacy SIL fallback (per D5 and Task A5). Clean rollback is `git revert` of the Track A merge commit, restoring the pre-Track-A SIL stub + bridge state. If a long-term SIL/GNC dual-mode is required, it is a separate spec.

## 9. Non-Goals

- Editing GNC source code.
- M4 heading-window redesign.
- `.action` migration.
- A legacy SIL fallback profile (separate spec if required).
- COLREG scenario geometry changes.
- Bridging GNC's internal mission_supervisor/safety_supervisor into L3 M1/M7 authority (L3 M1 remains sole safety authority).

## 10. Open Decisions

1. **Bridge implementation language**: Python (matches old L4 adapter pattern, faster to iterate) vs C++ (perf, native ship_interfaces). Proposed: Python unless profiling shows a bottleneck.
2. **GNC workspace vendoring**: vendor-copy into `third_party/gnc_ws/` (reproducible, version-controlled) vs read-only mount from snapshot (lighter, drifts). Proposed: vendor-copy at a pinned snapshot commit.
3. **`/l3/m5/avoidance_waypoints` message type**: use `ship_interfaces/AvoidancePlan` directly in L3 domain (requires L3 to build ship_interfaces), or define an equivalent `l3_external_msgs/AvoidanceWaypoints` and map in the bridge. Proposed: ship_interfaces directly, since Track A already vendors the GNC workspace; adding a duplicate IDL is churn.
4. **SIL-only scenario support**: confirm that after Track A, all SIL scenarios run under the GNC profile (no legacy SIL plant path needed). If some scenarios need legacy SIL, raise a separate fallback spec.

## 11. Dependencies

- Track B merged and local gate green (clean `/l3/...` bus, contract YAML + checker in place).
- Real GNC snapshot available locally at `/Users/marine/Code/_a4000_snapshots/mpc_latest/mpc/`.
- A4000 access for final validation (GNC stack must also run on A4000 for the promotion gate).
