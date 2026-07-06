# TDL -> GNC Avoidance Interface Contract

Date: 2026-06-27

Status: source-derived draft for COLREGs 12-probe debugging

Scope: TDL integration with the GNC waypoint-following stack for COLREGs avoidance and return-to-route. This document records the executable constraints found in source. It is not a GNC behavior redesign.

## Source Baseline

Primary source files:

- `src/l3_tdl_kernel/l3_external_msgs/msg/AvoidanceWaypoints.msg`
- `src/l3_tdl_kernel/l3_external_msgs/msg/GncExecutionStatus.msg`
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`
- `src/sim_workbench/gnc_bridge/src/translators.cpp`
- `third_party/gnc_ws/src/platform/ship_interfaces/msg/AvoidancePlan.msg`
- `third_party/gnc_ws/src/platform/ship_interfaces/msg/RoutePlan.msg`
- `third_party/gnc_ws/src/platform/ship_interfaces/msg/RouteExecutionStatus.msg`
- `third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp`
- `third_party/gnc_ws/src/gnc/ship_guidance/src/coordinate_transform_node.cpp`
- `third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp`
- `docker/gnc-ship-config-overlay.yaml`

## Runtime Chain

TDL publishes avoidance geometry as L3-owned waypoints:

```text
M5 /l3/m5/avoidance_waypoints
  -> gnc_bridge
  -> GNC /colav/avoidance_plan
  -> ActiveRouteManager /gnc/active_route
  -> coordinate_transform /ship/waypoints
  -> ship_guidance psi_cmd/u_cmd
  -> GNC RouteExecutionStatus
  -> gnc_bridge /l3/gnc/execution_status
```

The bridge is a field mapper. It does not add behavior logic or fix infeasible route geometry. TDL must publish executable waypoint geometry.

## Message Contract

### L3 output: `l3_external_msgs/AvoidanceWaypoints`

Publisher: M5 on `/l3/m5/avoidance_waypoints`.

Required fields for GNC execution:

| Field | Required contract |
| --- | --- |
| `stamp` | L3 source timestamp. Bridge rebases `valid_until` TTL to GNC domain time. |
| `schema_version` | Must be present. Current source uses `1`. |
| `plan_id` | Must be stable enough for traceability. Current M5 uses nanosecond-derived IDs, so every publish is a new route. |
| `parent_route_id` | Nominal route identity, currently `"nominal"`. |
| `behavior_mode` | `"emergency_avoidance"` during active COLREG conflict; `"return_to_route"` during recovery. |
| `command_source` | Current M5 uses `"collision_avoidance"`. |
| `latitude[]`, `longitude[]` | WGS84 route points. Must have equal length and at least 2 points after bridge. |
| `command_speed_mps[]` | Empty means GNC derives speed. If present, length must equal waypoint count. |
| `navigation_mode[]` | Empty means route type fallback. If present, length must equal waypoint count. |
| `valid_until` | Freshness bound. Current M5 publishes `now + 30s`. Expired plans are rejected by GNC. |
| `allow_degraded_execution` | If true, GNC may cap speed instead of rejecting turn/decel infeasibility. |
| `has_return_to_route_point` | Recovery hint. Current return-to-route sets true. |
| `return_latitude`, `return_longitude` | Return point hint. Current return-to-route uses second generated waypoint. |
| `confidence`, `rationale` | Required traceability fields for L3 contract. |

### Bridge mapping

`gnc_bridge::to_gnc_avoidance_plan()` maps L3 fields directly to `ship_interfaces/AvoidancePlan`.

Important bridge semantics:

- `command_heading_deg` is intentionally left empty.
- `require_exact_heading = false`.
- `require_exact_speed = false`.
- `allow_degraded_execution` is copied from L3.
- `valid_until` is rebased from L3 stamp to GNC wall/sim time before publish.

Implication: GNC follows waypoint geometry, not an explicit heading command. TDL must not treat heading window output as directly commanded unless it is encoded in route geometry.

### GNC input: `ship_interfaces/AvoidancePlan`

GNC accepts, degrades, or rejects an `AvoidancePlan`. Array rules:

- `latitude.size() >= 2`
- `latitude.size() == longitude.size()`
- `command_speed_mps` empty or same length as `latitude`
- `navigation_mode` empty or same length as `latitude`
- `command_heading_deg` empty or same length as `latitude`

### GNC feedback: `RouteExecutionStatus` -> `GncExecutionStatus`

GNC publishes `ship_interfaces/RouteExecutionStatus`; bridge maps it to `/l3/gnc/execution_status`.

TDL must consume at least:

| Field | Required TDL response |
| --- | --- |
| `accepted=true, degraded=false, rejected=false` | Plan passed GNC feasibility gate. Continue monitoring COLREG effect. |
| `degraded=true` | Honor `suggested_max_speed_mps`; classify as executable-with-limit, not full success. |
| `rejected=true` | M5 must replan or degrade before next publish; M7/M8 should record a GNC execution contract failure. |
| `execution_state` | Use for phase triage: `ACCEPTED`, `EXECUTING_WITH_LIMIT`, `REJECTED`, `DEFERRED`. |
| `reason` | Machine-readable root cause, e.g. `segment_too_short`, `turn_radius_too_small`, `yaw_rate_limited`, `decel_distance_tight`, `plan_expired`. |
| `suggested_action` | Direct repair hint, e.g. `slow_down`, `slow_down_or_smooth_turn`, `send_points_earlier`, `increase_segment_length`. |
| `requested_speed_mps`, `applied_speed_mps`, `suggested_max_speed_mps` | M5 speed cap feedback. |
| `required_turn_radius_m`, `estimated_available_turn_radius_m` | Route geometry feasibility evidence. Note: current L3 bridge status message does not expose these two fields, although GNC source publishes them. |
| `required_decel_distance_m`, `available_decel_distance_m` | Speed transition feasibility evidence. Note: current L3 bridge status message does not expose these two fields. |
| `cross_track_error_m`, `current_heading_deg`, `current_speed_mps` | Execution-state evidence for L4/GNC handoff and M8 trace. |

Contract gap: `l3_external_msgs/GncExecutionStatus` currently omits GNC radius/decel evidence fields that exist in `ship_interfaces/RouteExecutionStatus`. For full TDL triage, extend the L3 message or trace raw GNC status.

## ActiveRouteManager Feasibility Gate

Configured in `docker/gnc-ship-config-overlay.yaml`:

| Parameter | Normal | Emergency avoidance |
| --- | ---: | ---: |
| `max_command_speed_mps` | 8.0 | 8.0 |
| min segment length | 30.0 m | 15.0 m |
| static min turn radius | 80.0 m | 45.0 m |
| max lateral acceleration | 0.25 m/s^2 | 0.25 m/s^2 |
| max yaw rate | 1.2 deg/s | 2.0 deg/s |
| max deceleration | 0.08 m/s^2 | 0.08 m/s^2 |
| default avoidance hold | 60.0 s | 60.0 s |

Emergency mode is selected if either `behavior_mode` or any `navigation_mode[]` is one of:

- `emergency_avoidance`
- `emergency_avoid`
- `collision_avoidance`
- `avoidance`

### Segment length

Every adjacent waypoint segment must satisfy:

```text
segment_length >= min_segment_length
```

Otherwise GNC rejects with `segment_too_short` and suggests `increase_segment_length`.

### Turn radius and yaw-rate feasibility

For every interior waypoint:

```text
R_available = min(L_before, L_after) / tan(turn_angle / 2)

R_required = max(
  static_min_turn_radius,
  speed^2 / max_lateral_accel,
  speed / yaw_rate_limit_rad_s
)
```

If `R_available < R_required`:

- If `require_exact_speed == true` or `allow_degraded_execution == false`, GNC rejects with `turn_radius_too_small` or `yaw_rate_too_high`.
- Otherwise GNC accepts with `EXECUTING_WITH_LIMIT`, caps route speeds, and reports `turn_speed_limited` or `yaw_rate_limited`.

Emergency avoidance examples:

| Speed | Required radius driver | Required radius |
| ---: | --- | ---: |
| 3.0 m/s | yaw-rate | 86.0 m |
| 3.2 m/s | yaw-rate | 91.7 m |
| 4.2 m/s | yaw-rate | 120.3 m |
| 6.0 m/s | yaw-rate | 171.9 m |
| 8.0 m/s | lateral acceleration | 256.0 m |

For a 60 deg corner:

```text
min_segment_for_radius = R_required * tan(30 deg)
```

At 3.2 m/s this is about 53 m; at 6.0 m/s about 99 m; at 8.0 m/s about 148 m. These are mathematical floors, not comfort margins.

### Deceleration distance

For each segment with `v0 > v1`:

```text
required_decel_distance = (v0^2 - v1^2) / (2 * max_decel_mps2)
```

If available segment distance is shorter:

- strict mode rejects with `decel_distance_not_enough`;
- degraded mode accepts with `decel_distance_tight` and suggests `send_points_earlier`.

Examples with `max_decel_mps2 = 0.08`:

| Speed drop | Required distance |
| --- | ---: |
| 8.0 -> 3.2 m/s | 336 m |
| 6.0 -> 3.2 m/s | 161 m |
| 4.2 -> 3.2 m/s | 46 m |

TDL must publish slow-down points early enough. A late low-speed waypoint is not executable even if its final speed is safe.

## CoordinateTransform Constraints

`coordinate_transform_node` consumes `/gnc/active_route` and publishes `/ship/waypoints`.

Array validation:

- WGS84 lat/lon must be finite and in valid ranges.
- `speed_limit_mps` mismatch is accepted with warning and ignored.
- `navigation_mode` mismatch is accepted with warning and ignored.
- Duplicate route geometry and metadata are ignored.

Route update guard:

| Parameter | Normal route | Emergency avoidance |
| --- | ---: | ---: |
| `enable_route_update_guard` | true | true |
| min update interval | 2.0 s | 2.0 s |
| first changed point future distance | 500.0 m | 0.0 m |
| max dynamic lateral delta | 100.0 m | 1500.0 m |

Emergency avoidance relaxes the future-distance and lateral-delta guard. This makes rolling avoidance routes easy to accept, but also creates a TDL-side risk: if M5 republishes a new route anchored at current own-ship position every cycle, GNC may keep chasing a moving route instead of executing a stable COLREG maneuver.

Geometry processing:

- `enable_arc_smoothing` is false in the overlay.
- Even if smoothing is enabled, `emergency_avoidance` bypasses arc smoothing.
- FAP insertion is bypassed for `emergency_avoidance`.

Implication: emergency avoidance geometry is raw planner geometry. GNC will not smooth or reshape it for M5.

## ShipGuidance Constraints

`ship_guidance_node` follows `/ship/waypoints` and outputs heading/speed commands.

Important overlay values:

| Constraint | Value |
| --- | ---: |
| guidance period | 0.5 s |
| intermediate capture radius | 15 m |
| corridor half width | 30 m |
| corridor soft width | 60 m |
| corridor reacquire width | 90 m |
| turn segment speed cap | 4.2 m/s |
| turn segment release XTE | 35 m |
| turn segment release heading error | 6 deg |
| turn segment release cross-track rate | 0.40 m/s |
| turn segment release yaw rate | 1.0 deg/s |
| external route turn speed cap | 4.2 m/s |
| dynamic path attach current position | true |
| dynamic path attach min distance | 20 m |
| rejoin speed cap | 3.0 m/s |
| severe rejoin speed cap | 1.5 m/s |
| rejoin heading error | 15 deg |
| severe rejoin heading error | 30 deg |
| rejoin cross-track | 60 m |
| emergency avoidance speed cap | 3.2 m/s |
| emergency wheel-over distance | 120 m |
| emergency switch max XTE | 90 m |
| heading-align enter/exit | 30 deg / 8 deg |
| heading-align speed | 0.6 m/s |
| far-XTE rejoin threshold/release | 65 m / 25 m |
| XTE hard limit | 100 m |

Emergency avoidance effects in guidance:

- Non-final emergency waypoint uses `fly_by` switching.
- Emergency wheel-over distance is 120 m.
- Emergency switch XTE limit is 90 m.
- Emergency speed is capped to 3.2 m/s unless blocked by stronger gates.
- Cruise minimum speed floor is disabled in emergency avoidance.
- Heading-align rejoin logic is skipped during emergency avoidance.

Implication: emergency avoidance is intentionally low-speed, raw-geometry, fly-by route following. TDL should not expect immediate high-speed sharp turns.

## Current M5 Behavior Against Contract

Current M5 conflict-active behavior:

- Publishes every cycle while M6 reports conflict.
- Sets `plan_id = "m5-" + now.nanoseconds()`, making every publish a new route.
- Sets `behavior_mode = "emergency_avoidance"`.
- Generates 5 collinear waypoints from current own-ship position at 150, 300, 500, 800, 1200 m.
- Sets all command speeds to current own-ship speed.
- Sets all navigation modes to `emergency_avoidance`.
- Sets `valid_until = now + 30s`.
- Allows degraded execution.

Current return-to-route behavior:

- Emits briefly after conflict clear.
- Sets `behavior_mode = "return_to_route"`.
- Sets `has_return_to_route_point = true`.
- Generates points at current own-ship position, 600 m along planned route, and 1200 m along planned route.
- Sets `navigation_mode = "emergency_avoidance"` even though behavior mode is `return_to_route`.
- Allows degraded execution.

Observed contract risks:

1. Rolling route anchor risk: avoidance route is regenerated from current own-ship position. GNC can accept it while global COLREG effect remains weak because the route target keeps moving with the vessel.
2. Plan identity churn: nanosecond plan IDs make traceability and duplicate suppression harder; every publish appears as a new command.
3. Speed mismatch: command speed is current own speed. If current speed is high, ActiveRouteManager may degrade/cap speed and ship_guidance caps emergency speed to 3.2 m/s anyway.
4. Return mode ambiguity: `behavior_mode=return_to_route` plus `navigation_mode=emergency_avoidance` causes GNC to keep emergency handling. This may be intentional for relaxed guard, but it must be explicit.
5. Missing feedback closure: TDL publishes plans but does not yet use all GNC feasibility evidence fields to replan or classify probe verdicts.

## TDL Integration Requirements

### M5 route generation

M5 must generate a GNC-executable route, not only a COLREG-desirable heading.

Minimum pre-publish checks:

```text
lat/lon count >= 2
lat/lon/speed/navigation_mode lengths match
valid_until > now + bridge/drain margin
all segment lengths >= emergency_min_segment_length
all interior turns satisfy R_available >= R_required at commanded speed
all speed drops satisfy decel distance
commanded speeds <= expected guidance cap for mode
first maneuver point far enough for wheel-over and decel
```

Recommended emergency default:

- Use `command_speed_mps <= 3.2` unless a source-backed maneuver envelope proves higher speed is feasible.
- Keep practical segment lengths at least 100-200 m for sharp turns, even though the mathematical emergency floor is 15 m.
- Put low-speed command before the turn, not at the turn vertex.
- Keep route geometry stable during an active encounter.

### Route anchoring

During one active COLREG encounter, M5 should prefer a stable maneuver corridor:

- anchor route at encounter onset or first valid avoidance solution;
- keep `plan_id` stable or use `plan_id:version` semantics;
- update only on material changes: rule/direction change, target risk change, infeasible GNC feedback, or large deviation from route;
- avoid regenerating all points from current own-ship position every cycle.

Rolling replans are allowed only when explicitly classified as receding-horizon control and tested against GNC's route update guard and guidance response. If rolling mode remains, M5 must prove that the route's global COLREG geometry is not diluted by anchoring to current position.

### Speed contract

TDL must treat speed as an executable constraint:

- `command_speed_mps` should be the intended GNC waypoint speed limit, not merely own current speed.
- For emergency avoidance, use 3.0-3.2 m/s until closed-loop evidence supports a higher cap.
- If GNC returns `suggested_max_speed_mps`, M5 should apply that cap to subsequent routes.
- If M5 requests a speed drop, the route must include sufficient decel distance.

### Status feedback contract

TDL must not classify a route as L4/GNC-success only because `/l3/m5/avoidance_waypoints` was published.

Required status handling:

- `REJECTED`: M5 replan or reduce speed/increase radius; M7/M8 records execution failure.
- `EXECUTING_WITH_LIMIT`: accept as degraded; M5 honors cap; trace marks GNC-limited execution.
- `DEFERRED avoidance_active`: nominal route was deferred while avoidance active; this is expected during active avoidance but a fault if it persists after M6 clear beyond valid hold.
- `ACCEPTED feasible`: only means GNC feasibility gate passed; COLREG CPA/phase gates still decide rule success.

### M7/M8 evidence

M7 and M8 should preserve a separate "GNC feasibility" artifact:

- latest GNC execution state,
- reason/action,
- requested/applied speed,
- radius and decel evidence where available,
- current XTE/heading/speed,
- plan ID/version.

This keeps L4/GNC handoff faults distinct from M2/M6/M4/M5 decision faults.

## Interface Document Output Contract

For TDL implementation work, create or update a machine-checkable contract helper around these formulas:

```text
required_turn_radius(speed, mode)
available_turn_radius(prev, current, next)
required_decel_distance(v0, v1)
validate_gnc_avoidance_plan(plan, mode_constraints)
```

This helper should live on the M5 side, not inside GNC, and should have unit tests that mirror the GNC formulas. It should not share GNC runtime data structures with M7's checker path.

## Recommended Work Order

1. Extend trace/report visibility for `/l3/gnc/execution_status`, including radius/decel evidence if message fields are added.
2. Add M5 unit tests for GNC preflight formulas using current overlay parameters.
3. Refactor M5 avoidance waypoint generation from rolling current-position route to stable encounter-anchored route or explicitly tested receding-horizon route.
4. Cap emergency avoidance command speeds to the executable envelope and use GNC `suggested_max_speed_mps` feedback.
5. Clarify return-to-route `navigation_mode`: keep emergency semantics only if the relaxed guard is intentional; otherwise introduce explicit return mode handling.
6. Re-run per-module oracle, same-rule cohort, then strict clean-12.

## Acceptance Criteria

TDL->GNC integration is acceptable when:

- every RED COLREG probe has GNC status classified separately from COLREG decision status;
- M5 emits no route that fails its own GNC preflight validator;
- GNC `REJECTED` produces deterministic M5 recovery behavior or M7/M8 fault evidence;
- GNC `EXECUTING_WITH_LIMIT` is not counted as full route-execution success unless COLREG phase gates still pass with the applied cap;
- avoidance route identity and anchoring are stable enough to reproduce maneuver onset and heading response across strict restart runs;
- no scenario-id special cases, scorer threshold tuning, or vessel-specific branches are introduced in TDL decision-core code.

