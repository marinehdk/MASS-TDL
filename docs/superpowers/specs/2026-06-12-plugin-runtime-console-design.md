# Screen 02 Plugin Runtime Console Design

Date: 2026-06-12
Branch: `codex/plugin-runtime-console`
Status: Spec for review

## 1. Purpose

Screen 02 `仿真检查` becomes the pre-simulation runtime control surface. It must verify the L3-TDL core stack and manage external module plugins before a scenario can enter Screen 03 `仿真运行`.

The screen is not a display-only integration panel. It must let the operator select plugin implementations, start/stop/restart external plugin containers, restart core TDL containers, run readiness gates, and persist evidence for the exact runtime combination used by a simulation run.

This design replaces the small bottom-right `External Integration` panel with a full `B` layout: a category-driven runtime console. Existing Screen 02 pieces can be reused where useful, but the new information architecture prioritizes extensibility over preserving the old three-column layout.

## 2. Sources

- `AGENTS.md`: local-first gate, A4000 narrow sync, Screen 02 external integration ownership.
- `docs/Design/SIL/external-module-adapter-spec.md`: current external profile/probe contract and Screen 02 external gate.
- `docs/Design/SIL/v1.0-unified/01-sil-architecture.md`: Screen 02 is pre-flight and GO/NO-GO location.
- Current UI screenshot from 2026-06-12: external integration control is too small and hidden in the bottom-right rail.
- Visual reference UI: `docs/superpowers/specs/2026-06-12-plugin-runtime-console-reference-ui.html`.

## 3. Goals

1. Provide a single Screen 02 runtime console for internal TDL core services and external plugin services.
2. Support two prominent modes: `内测` and `集成`.
3. Let the operator select exactly one active plugin per plugin role.
4. Let the operator start, stop, and restart external plugin containers.
5. Let the operator restart individual TDL core containers and stop/start the core stack as a group.
6. Run readiness gates across core services, plugin containers, ROS2 topics, freshness, version metadata, and safety boundaries.
7. Block transition to Screen 03 when any required gate fails.
8. Write evidence containing selected mode, selected plugin versions, container IDs/images, ROS2 topic status, and gate results.
9. Keep local OrbStack and A4000 behavior aligned: same manifest/profile model, same UI, same API shape, different runtime backend target.

## 4. Non-Goals

- Do not implement new hydrodynamics, L2 route planning, or fusion algorithms.
- Do not allow external modules to publish low-level control commands into TDL.
- Do not create a multi-plugin ensemble mode in the first implementation.
- Do not replace Screen 03 runtime monitoring.
- Do not make A4000 the first test target. Local OrbStack remains the first gate.

## 5. Runtime Model

### 5.1 Service Classes

`core_service`

- Fixed TDL infrastructure.
- Current services:
  - `sil-orchestrator`
  - `sil-nodes`
  - `foxglove-bridge`
  - `martin-tile-server`
- Single-container `Restart` is allowed.
- Single-container `Stop` is not allowed from Screen 02.
- Core stack `Start`, `Restart`, and `Stop` are allowed as group actions.
- `Stop Core Stack` requires explicit confirmation.

`plugin_service`

- External module managed as a container service.
- Current plugin roles:
  - `hydrodynamics`
  - `route_l2`
  - `fusion`
- Each role has exactly one active plugin at a time.
- Plugin service actions:
  - `Start`
  - `Stop`
  - `Restart`
  - `Switch`
- `Switch` means: stop current active plugin for the role, start selected plugin, run probe, update active profile only if start succeeds.

### 5.2 Single-Instance Rule

Each plugin role is single-instance and mutually exclusive:

| Role | First implementation rule |
|---|---|
| `hydrodynamics` | One active container only |
| `route_l2` | One active container only |
| `fusion` | One active container only |

Multi-instance comparison and ensemble execution are excluded from this version. This avoids ROS2 topic collisions, ambiguous data provenance, and unsafe mixed-source runtime states.

## 6. Modes

### 6.1 `内测`

Internal development mode.

- Uses TDL internal or mock services.
- External plugin roles may resolve to internal mock plugins.
- External A4000 module gates are not required.
- Core service health, lifecycle readiness, scenario consistency, and safety gates still apply.

### 6.2 `集成`

External integration mode.

- Uses selected plugin containers for hydrodynamics, L2 route planning, and fusion.
- All selected plugin containers must be running and healthy.
- Required ROS2 topics must exist, match expected message types, and meet freshness thresholds.
- Version metadata must be readable.
- Low-level control topics from external plugins are forbidden.
- GO is blocked when any required plugin role fails.

## 7. Information Architecture

Screen 02 uses the `B` category-driven layout.

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ 仿真检查 · 容器运行台             [ 内测 | 集成 ]   compose project: ...       │
├──────────────┬───────────────────────────────────────────────┬───────────────┤
│ 检查分类      │ 主工作区                                      │ 详情/日志       │
│              │                                               │               │
│ 01 运行模式   │ Summary cards                                 │ 当前失败原因     │
│ 02 TDL核心容器│ TDL core service cards                         │               │
│ 03 外部插件容器│ Plugin role cards + Start/Restart/Stop/Switch  │ 分类日志         │
│ 04 ROS2链路   │ Gate mapping / topic freshness                 │               │
│ 05 安全边界   │ Evidence + selected profile                    │ 操作按钮         │
│ 06 放行结论   │                                               │               │
├──────────────┴───────────────────────────────────────────────┴───────────────┤
│ core compose / plugin compose / evidence path / shortcuts                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

### 7.1 Left Rail

The left rail is a category navigator, not the old linear gate-only list.

Required categories:

1. `运行模式`
2. `TDL 核心容器`
3. `外部插件容器`
4. `ROS2 数据链路`
5. `安全边界`
6. `放行结论`

Each category shows:

- ordinal
- label
- aggregate status
- short reason
- selected state

### 7.2 Main Work Area

The main area shows the selected category.

Default category: `外部插件容器` in `集成` mode, `TDL 核心容器` in `内测` mode.

Required top summary cards:

- `TDL Core`: `4/4`, `3/4`, or `0/4`
- `Plugins`: active plugin count and gate status
- `ROS Topics`: required topic count and freshness status
- `Verdict`: `GO`, `NO-GO`, `CHECKING`, or `IDLE`

### 7.3 Right Rail

The right rail shows context for the selected category:

- current failure reason
- operator action summary
- latest logs
- evidence path
- retry controls

This rail reuses the current `ActionLogs` concept but scopes logs to the selected category and runtime action.

## 8. Reference UI Requirements

The committed reference UI file defines the expected visual structure:

`docs/superpowers/specs/2026-06-12-plugin-runtime-console-reference-ui.html`

Frontend implementation does not need pixel-perfect parity, but must preserve:

1. Prominent `内测 / 集成` segmented control in the top bar.
2. Left category navigator.
3. Separate `TDL 核心容器` and `外部插件容器` sections.
4. Core service cards showing status and `Restart`.
5. External plugin cards showing selected plugin, active container, version/image, health, topics, and `Start / Restart / Stop`.
6. Right rail for current failure reason and logs.
7. Bottom evidence/status strip.

Text inside compact controls must fit on desktop and typical laptop widths. Do not use tiny bottom-right controls for mode/profile selection.

## 9. Frontend Component Design

### 9.1 Components

`SimulationCheck`

- Owns screen layout.
- Holds selected category.
- Coordinates GO-path blocking.
- Delegates runtime UI to focused components.

`RuntimeModeSwitch`

- Top-bar segmented control.
- Switches between `internal` and `integration`.
- Switching mode reruns status fetch and marks readiness stale until probe completes.

`CheckCategoryNav`

- Left rail.
- Displays category status aggregation.
- Replaces the old left-only GateSequencer as the primary navigation.

`CoreServicePanel`

- Shows fixed core services.
- Exposes `Restart` per service.
- Exposes `Start Core Stack`, `Restart Core Stack`, `Stop Core Stack`.
- `Stop Core Stack` opens confirmation.

`PluginRolePanel`

- Shows one card per plugin role.
- Select dropdown lists available plugins for that role.
- Shows active plugin, container status, image/version, health, topic freshness.
- Exposes `Start`, `Restart`, `Stop`, and `Switch`.

`ReadinessGatePanel`

- Shows gate mapping for current category.
- Reuses existing preflight gate concepts where practical.

`RuntimeActionLog`

- Right rail log.
- Shows recent runtime actions, probe messages, and failure cause.

`EvidenceStrip`

- Bottom strip.
- Shows compose project, active profile, evidence path, and shortcuts.

### 9.2 Existing UI Reuse

Reusable:

- `DiagnosticCanvas` as category detail/diagnostic region.
- `ActionLogs` concepts and styling.
- existing gate statuses and GO/NO-GO colors.
- existing RTK Query API style in `web/src/api/silApi.ts`.

Replace or substantially reshape:

- `ExternalIntegrationPanel`.
- left rail as only `GateSequencer`.
- bottom-right profile selector.

## 10. Backend API Design

All endpoints live under `/api/v1/runtime` or extend `/api/v1/integration`. Prefer `/api/v1/runtime` for container lifecycle operations.

### 10.1 Read APIs

`GET /api/v1/runtime/summary`

Returns aggregate mode, core, plugins, topics, safety, and verdict.

`GET /api/v1/runtime/core-services`

Returns fixed core services:

```json
{
  "services": [
    {
      "id": "sil-orchestrator",
      "class": "core_service",
      "container_name": "mass-l3-sil-sil-orchestrator-1",
      "status": "running",
      "health": "healthy",
      "image": "mass-l3-sil-sil-orchestrator",
      "version": "local",
      "allowed_actions": ["restart"]
    }
  ]
}
```

`GET /api/v1/runtime/plugins`

Returns plugin role state:

```json
{
  "roles": [
    {
      "role": "route_l2",
      "active_plugin": "l2-planner-main",
      "single_instance": true,
      "plugins": [
        {
          "id": "l2-planner-main",
          "label": "L2 Planner Main",
          "service": "plugin-route-l2-main",
          "container_name": "mass-l3-plugin-route",
          "status": "running",
          "health": "degraded",
          "image": "mass-l2-planner:main",
          "revision": "unknown",
          "required_topics": [
            {
              "name": "/route_planning/route_plan",
              "type": "ship_interfaces/msg/RoutePlan",
              "status": "missing"
            }
          ]
        }
      ]
    }
  ]
}
```

`GET /api/v1/runtime/evidence/latest`

Returns path and summary for latest runtime evidence.

### 10.2 Action APIs

`POST /api/v1/runtime/core/{service_id}/restart`

- Only allowed for known core services.
- Does not allow stop.

`POST /api/v1/runtime/core/start`

- Starts core stack.

`POST /api/v1/runtime/core/restart`

- Restarts core stack.

`POST /api/v1/runtime/core/stop`

- Stops core stack.
- Requires body `{ "confirm": "STOP_CORE_STACK" }`.

`POST /api/v1/runtime/plugins/{role}/select`

Body:

```json
{ "plugin_id": "l2-planner-main" }
```

This changes desired selection but does not start runtime until `start` or `switch`.

`POST /api/v1/runtime/plugins/{role}/start`

Starts selected plugin for role. If another plugin in the role is active, reject and require `switch`.

`POST /api/v1/runtime/plugins/{role}/switch`

Body:

```json
{ "plugin_id": "tdl-mock-route" }
```

Sequence:

1. stop current role service if running.
2. start requested service.
3. probe role.
4. set active plugin only if start succeeds.
5. return action log and gate state.

`POST /api/v1/runtime/plugins/{role}/restart`

Restarts active plugin for role.

`POST /api/v1/runtime/plugins/{role}/stop`

Stops active plugin for role.

`POST /api/v1/runtime/probe`

Runs all readiness gates and writes evidence.

## 11. Runtime Backend

### 11.1 Local Runtime Backend

Local first implementation controls Docker Compose services in OrbStack.

Expected compose files:

- `docker-compose.yml`
- `docker-compose.a4000.yml`
- `docker-compose.plugins.yml`

Runtime backend responsibilities:

- inspect compose project
- start/restart/stop allowed services
- inspect container health
- inspect image labels and revisions
- run ROS2 topic checks in declared domain
- enforce role single-instance
- write evidence JSON

### 11.2 A4000 Runtime Backend

A4000 uses the same manifest/profile/API model.

Differences:

- target host is A4000.
- compose project may use host-specific overrides.
- first implementation may call the API on A4000 locally from the orchestrator container.
- no broad repo sync, no `git pull/reset`, no `rsync --delete`.

The UI should not expose whether runtime is local or A4000 beyond a visible runtime target label.

## 12. Plugin Manifest

Each plugin is declared by manifest, not hardcoded in React.

Proposed path:

`config/runtime_plugins/*.yaml`

Manifest shape:

```yaml
id: l2-planner-main
role: route_l2
label: L2 Route Planner Main
runtime: compose
compose:
  service: plugin-route-l2-main
  project: mass-l3-sil
image:
  expected: mass-l2-planner:main
  revision_label: org.opencontainers.image.revision
ros:
  domain_id: 10
  required_topics:
    /route_planning/route_plan: ship_interfaces/msg/RoutePlan
  forbidden_topics:
    - /sil/actuator_cmd
    - /l4/control_cmd
freshness:
  route_ms: 2000
health:
  required: true
evidence:
  include_logs_tail_lines: 80
```

Allowed roles:

- `hydrodynamics`
- `route_l2`
- `fusion`
- `internal_mock`

`internal_mock` is only for built-in replacements used in `内测`.

## 13. Runtime Profile

The existing integration profile remains useful, but plugin runtime needs an explicit active plugin selection.

Proposed path:

`config/runtime_profiles/*.yaml`

Example:

```yaml
name: integration-local
mode: integration
target: local
tdl_domain_id: 42
plugin_roles:
  hydrodynamics: hydro-fossen
  route_l2: l2-planner-main
  fusion: yougc-fusion
safety:
  single_instance_per_role: true
  forbid_low_level_control: true
  require_version_metadata: true
```

The current `config/integration_profiles/*.yaml` can either be extended or wrapped by runtime profiles. Recommended first implementation: add `runtime_profiles` and keep existing integration profiles as compatibility input for adapter topic gates.

## 14. Readiness Gates

Required gates:

| Gate | Applies To | Pass Criteria |
|---|---|---|
| Mode selected | All | `internal` or `integration` explicitly selected |
| Core stack running | All | all four core services running; required healthchecks pass |
| Core ROS readiness | All | TDL ROS domain responds; M1-M8 lifecycle/pulse available |
| Plugin role selection | Integration | one selected plugin per required role |
| Plugin container state | Integration | selected plugin container running; health status acceptable |
| Plugin version | Integration | image tag and revision label readable or manifest explicitly permits local unknown |
| Required topics | Integration | all required topics exist with expected message type |
| Freshness | Integration | topic samples within role threshold |
| Forbidden topics | Integration | selected plugins do not publish forbidden low-level control topics |
| Evidence write | All | readiness report written under `runs/` |

GO to Screen 03 requires all required gates to pass.

## 15. Evidence

Evidence file path:

`runs/runtime_probe_<timestamp>.json`

Minimum contents:

```json
{
  "timestamp": "2026-06-12T16:44:01+08:00",
  "target": "local",
  "mode": "integration",
  "profile": "integration-local",
  "core_services": [],
  "plugin_roles": [],
  "ros_topics": [],
  "forbidden_topic_scan": [],
  "gates": [],
  "verdict": "NO-GO"
}
```

When Screen 03 starts, run metadata must reference the runtime evidence file used for GO.

## 16. Safety and Isolation

1. External plugins may provide route, target, ownship, and environment data.
2. External plugins must not provide direct actuator or low-level control commands.
3. Only TDL output route/avoidance plan may be sent to external GNC/L4.
4. Plugin role single-instance is mandatory.
5. Core services and plugin services must use explicit allowlists for lifecycle actions.
6. Stop actions must not run arbitrary shell from profile data.
7. Runtime backend must resolve service IDs from trusted manifests, not raw user input.

## 17. Error Handling

| Error | UI Behavior | Backend Behavior |
|---|---|---|
| Core service down | category `TDL 核心容器` red; GO blocked | return service status and restart action |
| Plugin start fails | plugin card red; log failure | keep old active selection when possible |
| Plugin switch partial failure | show old/new state clearly | attempt rollback if old service was stopped and can restart |
| Topic missing | plugin or ROS category yellow/red | include `ros2 topic info` output in detail |
| Version unknown | yellow if allowed by manifest; red if required | include image inspect result |
| Stop core stack requested | modal confirmation | require exact confirmation token |
| A4000 unreachable | runtime target red | no local state mutation |

## 18. Testing Strategy

### 18.1 Frontend

- Mode switch visibility and selection.
- Category nav status aggregation.
- Core service cards render fixed services.
- Core single-service `Restart` allowed; single-service `Stop` absent.
- `Stop Core Stack` requires confirmation.
- Plugin role cards enforce one active plugin per role.
- Switch action sequence updates UI state.
- GO blocked on failing runtime probe.
- Evidence path displayed after successful probe.

### 18.2 Backend

- manifest parser validates role, service, required topics, forbidden topics.
- runtime profile parser validates one plugin per role.
- core service API rejects unknown service and stop-single-service attempts.
- plugin switch stops old, starts new, probes, and records state.
- low-level forbidden topic scan fails when forbidden publisher exists.
- evidence writer includes selected image/revision/container IDs.

### 18.3 Local Container Gate

Local acceptance must run before A4000 sync:

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```

New runtime console tests extend this with plugin compose dry-run and local OrbStack control checks.

### 18.4 A4000 Gate

After local pass, sync touched paths only, then run A4000 acceptance and runtime probe. A4000 validation must use the same UI/API semantics as local.

## 19. Acceptance Criteria

1. Screen 02 shows a prominent `内测 / 集成` switch in the top bar.
2. Screen 02 shows category navigation, not only linear gate cards.
3. TDL core container section shows four current core services.
4. Core services support per-service `Restart`.
5. Core services do not expose per-service `Stop`.
6. Core stack supports group stop with confirmation.
7. External plugin section shows `hydrodynamics`, `route_l2`, and `fusion`.
8. Each plugin role allows one active plugin only.
9. Plugin `Switch` performs stop old, start new, probe.
10. Runtime probe blocks Screen 03 when required gates fail.
11. Runtime evidence file records exact core/plugin/runtime state.
12. Local OrbStack test passes before A4000 sync.
13. A4000 test passes before GitHub/GitLab push.

## 20. Implementation Boundaries

Recommended incremental implementation:

1. Add manifest/profile parser and runtime read APIs.
2. Replace Screen 02 layout with static data from APIs.
3. Add core service restart and stack actions.
4. Add plugin start/stop/restart/switch actions.
5. Add probe/evidence gate integration.
6. Add local OrbStack plugin compose acceptance.
7. Add A4000 runtime target validation.

Do not implement external algorithm containers until runtime control scaffolding can manage placeholder plugin containers safely.
