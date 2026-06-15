# L2 External Plugin Integration Design

Date: 2026-06-15
Branch: codex/l2-external-plugin-integration

## Goal

Integrate the external L2 route-planning backend as `plugin-route-l2-main` for the SIL integration flow. At simulation start, when Screen 02 activates the SIL lifecycle and Screen 03 opens the monitor, the L2 plugin must publish the initial complete route, and TDL must receive it as `/l2/planned_route` so M3/L4/HMI can start navigation from full route data.

This design covers backend/plugin integration only. No frontend changes are in scope.

## Evidence

Local TDL evidence:

- `web/src/screens/SimulationCheck.tsx` launches a run by calling cleanup, configure, then activate, and then navigates to `#/monitor/{scenarioId}`.
- `config/runtime_plugins/l2-planner-main.yaml` declares the active route-L2 plugin as `plugin-route-l2-main`.
- `docker-compose.plugins.yml` currently defines `plugin-route-l2-main` as an idle `alpine` service.
- `src/sim_workbench/external_adapters/external_adapters/tdl_ingress_node.py` accepts `route_in` payloads and publishes `/l2/planned_route`.
- `src/sim_workbench/external_adapters/external_adapters/converters.py` already defines the canonical `route_in` payload shape.

External L2 evidence copied from A4000:

- Snapshot path: `/Users/marine/Code/_a4000_snapshots/mass-simulation`
- Source path on A4000: `/home/mass/simulation`
- Primary package: `船舶动力学/gnc_ws/src/route_planning_ros2`
- Main executable: `gnc_sim_node = route_planning_ros2.gnc_sim_node:main`
- Primary output: `/route_planning/route_plan`, type `ship_interfaces/msg/RoutePlan`
- Optional compatibility output: `/route_planning/gnc_route_plan`, type `ship_interfaces/msg/GncRoutePlan` when available
- Feedback inputs: `/ship/geo_position` (`ship_interfaces/msg/GeoPosition`) and `/ship/odometry` (`nav_msgs/msg/Odometry`)

## Requirements

1. `plugin-route-l2-main` must run L2 backend code in a plugin container rather than remaining an idle stub.
2. The L2 route boundary must use the external route interface as the source of truth: `/route_planning/route_plan`.
3. TDL must keep its internal route contract unchanged: `/l2/planned_route`.
4. An adaptor must convert external `RoutePlan` into TDL `route_in` payloads and submit them to `external_tdl_ingress`.
5. The first route publication must be tied to simulation start, represented by lifecycle `ACTIVE`, not by container startup alone.
6. `GncRoutePlan` remains a fallback/compatibility path only. The main integration path uses `RoutePlan`.
7. The plugin must not publish low-level control topics such as `/sil/actuator_cmd` or `/l4/control_cmd`.
8. Integration must be testable locally before A4000 deployment.

## Design

Use scheme A: keep L2 as an external route-planning plugin, and add a narrow adaptor at the boundary.

The plugin container owns:

- copied L2 backend source from the A4000 snapshot
- external `ship_interfaces` definitions needed by `route_planning_ros2`
- a launch/entrypoint wrapper for `route_planning_ros2`
- `l2_route_plan_adaptor`, a small TDL-owned adaptor process

TDL owns:

- `external_tdl_ingress`
- conversion to canonical `l3_external_msgs/PlannedRoute`
- downstream L3 consumption of `/l2/planned_route`

The adaptor subscribes to `/route_planning/route_plan`, validates that route arrays are coherent, converts the route to `route_in`, and sends one JSON line to `external_tdl_ingress` over TCP. Because `external_tdl_ingress` publishes `/l2/planned_route` with reliable transient-local QoS, late L3 subscribers can still receive the initial route.

## Runtime Flow

```text
Screen 02
  -> POST /api/v1/lifecycle/cleanup
  -> POST /api/v1/lifecycle/configure
  -> POST /api/v1/lifecycle/activate
  -> /sil/lifecycle_status ACTIVE

plugin-route-l2-main
  -> L2 route planner starts or releases initial route on ACTIVE
  -> publishes /route_planning/route_plan

l2_route_plan_adaptor
  -> subscribes /route_planning/route_plan
  -> validates full route
  -> maps RoutePlan to route_in JSON
  -> sends to external_tdl_ingress:8765

external_tdl_ingress
  -> publishes /l2/planned_route

TDL
  -> M3/L4/HMI consume route and Screen 03 starts with full route data
```

## Interface Mapping

External `ship_interfaces/msg/RoutePlan`:

- `header.stamp` -> `route_in.stamp`
- `header.frame_id` -> `route.header.frame_id`, normalized to `WGS84` when empty or `map`
- `latitude[i]` -> `route.poses[i].pose.position.latitude`
- `longitude[i]` -> `route.poses[i].pose.position.longitude`
- `speed_limit_mps[i]` -> `speed_profile_kn[i] = speed_limit_mps / 0.514444`
- `route_id` string -> stable unsigned 32-bit hash for TDL `route_id`
- `route_type` and `navigation_mode[]` -> preserved in `rationale`

Validation rules:

- `latitude` and `longitude` must both be non-empty and equal length.
- At least two waypoints are required for the initial route.
- Optional arrays may be empty. If `speed_limit_mps` is empty or zero, use the configured default speed, initially `10.0 kn`.
- Non-finite coordinates or speeds reject the route.
- A duplicate `route_id` plus identical waypoint signature is not resent unless configured for periodic keepalive.

## Container Design

`plugin-route-l2-main` should become a local image built from the repo context, not `alpine:3.20`.

Initial container contents:

- L2 package subset under `/opt/l2_ws/src/route_planning_ros2`
- L2 `ship_interfaces` subset under `/opt/l2_ws/src/ship_interfaces`
- TDL adaptor package or script under `/opt/ws/scripts/integration`
- entrypoint that sources ROS2, sources workspace overlays, waits for ingress, then starts:
  - `ros2 run external_adapters external_tdl_ingress` remains in `sil-nodes` for the TDL side, as today
  - L2 route node in the plugin container
  - adaptor in the plugin container

The plugin must run in `ROS_DOMAIN_ID=42` for local/A4000 TDL integration unless a profile explicitly remaps it.

## Error Handling

- If the L2 route node never publishes `/route_planning/route_plan`, the adaptor logs a clear timeout and exits non-zero in strict mode.
- If route arrays are invalid, the adaptor rejects the message and does not publish partial route data.
- If `external_tdl_ingress` is unavailable, the adaptor retries with bounded backoff and reports readiness failure.
- If route updates arrive after the initial plan, the adaptor treats them as full replacement route plans.
- If `RoutePlan` and `GncRoutePlan` disagree, `RoutePlan` wins and `GncRoutePlan` is ignored unless the profile explicitly uses fallback mode.

## Testing

Unit tests:

- RoutePlan validation rejects empty, one-point, mismatched-array, and non-finite routes.
- RoutePlan conversion produces a `route_in` payload with all waypoints and speed conversion.
- Stable string `route_id` hashing is deterministic.
- Duplicate route signatures are suppressed.

Static/runtime config tests:

- `docker-compose.plugins.yml` no longer leaves `plugin-route-l2-main` as idle `alpine`.
- `config/runtime_plugins/l2-planner-main.yaml` required topics match `/route_planning/route_plan`.
- plugin service does not declare forbidden low-level control topics.

Local integration tests:

- Start local A4000-equivalent stack with plugins.
- Activate a scenario through lifecycle APIs.
- Confirm `/route_planning/route_plan` appears from `plugin-route-l2-main`.
- Confirm `/l2/planned_route` appears through `external_tdl_ingress`.
- Confirm Screen 03 route source is live L2 data, not static YAML fallback.

A4000 validation:

- Sync only touched paths to the marine TDL checkout.
- Run local gate before A4000 sync.
- On A4000, run acceptance and collect evidence from runtime probe plus route topic observation.

## Open Decision Already Resolved

Trigger semantics are lifecycle-based: `ACTIVE` is the simulation start trigger. The route must not be triggered only by plugin container startup or by frontend hash navigation.

## Out of Scope

- Frontend changes.
- Low-level hydrodynamics/control integration.
- Rewriting colleague L2 planner internals.
- Replacing TDL internal `/l2/planned_route` contract.
