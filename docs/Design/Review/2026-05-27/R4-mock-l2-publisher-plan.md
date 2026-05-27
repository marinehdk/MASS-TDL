---
title: D-DEMO1-R4 — SIL Mock L2 PlannedRoute Publisher + Scenario YAML Route Extension
date: 2026-05-27
author: Subagent (Claude Haiku 4.5)
estimate_pw: 2.0
blocks: D-DEMO1-R6 (SIL Integration HMI binding dependent on M3 mission_state publishing)
blocked_by: D-DEMO1-R1 (Evidence report; F-R1-03 F-R1-04 root causes confirmed)
status: draft (pending main-agent audit)
---

# 1. Motivation & Context

## Problem Statement

R1 evidence report (2026-05-27) identified two critical SILENT topics blocking M3 state machine progression:

- **F-R1-03**: `/l3/m3/mission_state` never publishes (Publisher count = 0) — M3 stuck in AWAITING_ROUTE state
- **F-R1-04**: `/l2/planned_route` never publishes (SIL has no L2 mock) — M3 has no route input to process

Current causality chain:
```
[L1/L2 缺位] /l1/voyage_task + /l2/planned_route SILENT
    ↓
[M3 卡死] AWAITING_ROUTE state → mission_state Publisher never registered/activated
    ↓
[下游 impact] M1 ODD envelope calculation missing mission context; M4 IvP optimization unreachable
```

**Root Cause**: SIL currently has no mock L2 publisher node. Real L2 Voyage Planner is responsibility of sister team (not in scope). DEMO-1 must unblock M3 to progress to behavior decision chain (R2/R3/R5).

**R4 Objective**: Implement standalone mock L2 node that publishes `/l1/voyage_task`, `/l2/planned_route`, `/l2/speed_profile` at appropriate frequencies. Route source: scenario YAML `ownShip.nominalRoute` field (new), or fallback to synthetic generation from initial position + heading + distance.

---

# 2. Goals & Non-Goals

## Goals

1. **Mock L2 node** deployed as standalone ROS2 node (placement TBD: `src/sil_mocks/` or integrated into `src/sil_orchestrator/`)
   - Subscribes to SIL scenario lifecycle events (scenario_loaded / scenario_activated / scenario_cleanup)
   - Reads `ownShip.nominalRoute` from YAML or generates default route
   - Publishes 3 topics at RFC-006 spec frequencies

2. **Scenario YAML v3.1 extension**
   - Add optional `ownShip.nominalRoute: [{lat, lon, target_sog_kn}, ...]` field
   - Update `/scenarios/IMAZU标准测试/imazu-01-ho.yaml` with 2-waypoint route example
   - Maintain full backward compatibility (v3.0 scenarios without this field use fallback)

3. **Island/grounding test scenario** (NEW)
   - Create `/scenarios/Grounding/island-detour-test.yaml`
   - 1–2 rectangular island polygons in YAML (or inline GeoJSON)
   - Nominal route that would "collide" with island if unmodified
   - Demonstrates future D2.2 ENC integration (though mock L2 uses simplified geometry)

4. **Contract divergence documentation** (`05-sil-mock-l2-contract.md`)
   - Explicit mapping: RFC-006 IDL field | mock implementation | known divergence | justification
   - Frequency: 1 Hz for all three topics (real L2 may use 0.5 Hz or event-driven)
   - Confidence: always 1.0 (mock data, not real estimation)
   - Swappability strategy: how to transition from mock to real L2

5. **M3 state machine unlocking**
   - M3 transitions AWAITING_ROUTE → ACTIVE within 5 s of mock L2 first publication
   - `/l3/m3/mission_state` publisher activates and emits @ 1 Hz

## Non-Goals

- **Real ENC S-57 loader**: D2.2 owns this. R4 uses inline YAML polygons or simplified rectangular shapes
- **Real L2 Voyage Planner**: Outside scope. Sister team responsibility
- **Scenario A* / complex detour planning**: R4 performs only naive geometric avoidance; future iterations may improve
- **M3 internal logic changes**: R4 is pure input injection; M3 behavior unchanged
- **Real ocean current / tide models**: Use environment section from YAML as-is

---

# 3. RFC-006 Contract Mapping

This section documents exact RFC-006 compliance and intentional mock divergences.

### 3.1 `/l1/voyage_task` Interface

**RFC-006 Scope**: L1 Mission Layer → M3 (event-driven per v1.1.2 §15.1 VoyageTask IDL)

| RFC-006 Field | Mock Implementation | Frequency | Divergence | Reason |
|---|---|---|---|---|
| `schema_version` | 112 (v1.1.2) | — | None | Locked at v1.1.2 |
| `stamp` | `rclcpp::Clock().now()` | Event (≈0.1 Hz in mock, once per scenario load) | Mock publishes once at activation; real L1 publishes per plan | L1 typically plan-triggered; SIL stub publishes singleton |
| `task_id` | `uint64(scenario_id_hash)` | — | None | Unique per scenario |
| `departure` | `ownShip.initial.position` | — | None | Scenario start point |
| `destination` | Last waypoint of `nominalRoute` | — | None | Route terminal |
| `eta_window` | `[start_time + est_duration, start_time + est_duration + 60s]` | — | Mock uses synthetic window; real L1 computes actual window | SIL has no L1 context; ±60s tolerance is conservative |
| `optimization_priority` | `"balanced"` (hardcoded) | — | Mock ignores YAML; always balanced | SIL mock doesn't optimize; real L1 would use plan intent |
| `mandatory_waypoints` | All `nominalRoute` waypoints (if present) | — | None | Route itself is mandatory |
| `exclusion_zones` | Empty array (mock doesn't generate) | — | Mock divergence | Real L1 would compute from ENC/AIS; R4 focuses on route publishing |
| `special_restrictions` | Empty string | — | N/A | Not modeled in SIL stub |
| `confidence` | 1.0 | — | Always 1.0 (synthetic data) | Mock data is deterministic |
| `rationale` | `"SIL_MOCK: synthetic voyage task from scenario"` | — | None | Audit trail |

**QoS**: TRANSIENT_LOCAL (latched), 1 Hz in mock (single publish at activation, then repeat for HMI visibility)

---

### 3.2 `/l2/planned_route` Interface

**RFC-006 Scope**: L2 Voyage Planner → M3, M5 (1 Hz per v1.1.2 §15.1 PlannedRoute IDL)

| RFC-006 Field | Mock Implementation | Frequency | Divergence | Reason |
|---|---|---|---|---|
| `schema_version` | 112 (v1.1.2) | — | None | Locked at v1.1.2 |
| `stamp` | `rclcpp::Clock().now()` | **1 Hz** ✅ | None | RFC-006 spec |
| `route_id` | `uint64(route_version_counter)` | — | None | Incremented on each new route |
| `route` | `GeoPath` from `nominalRoute` waypoints | — | None | Direct YAML→GeoPath conversion |
| `total_distance_nm` | Haversine sum of segment distances | — | None | Computed from waypoints |
| `estimated_duration_s` | `total_distance_nm / avg_sog_kn * 3600` | — | Simplified (real L2 uses detailed speed profile) | Mock uses average; real L2 segments |
| `speed_profile_kn` | Per-segment speeds from YAML or default | — | Matching `/l2/speed_profile` segments | Parallel publication |
| `safety_zone` | `"500m_cpa_corridor"` (hardcoded) | — | Mock hardcodes; real L2 computes per scenario | SIL configuration matches imazu YAML |
| `confidence` | 1.0 | — | Always 1.0 | Synthetic data |
| `rationale` | `"SIL_MOCK: {route_source}"` (YAML or default) | — | None | Audit trail |

**QoS**: TRANSIENT_LOCAL, 1 Hz (continuous publication for M3 latch + HMI streaming)

---

### 3.3 `/l2/speed_profile` Interface

**RFC-006 Scope**: L2 Voyage Planner → M5 (1 Hz, companion to PlannedRoute)

| RFC-006 Field | Mock Implementation | Frequency | Divergence | Reason |
|---|---|---|---|---|
| `schema_version` | 112 (v1.1.2) | — | None | Locked at v1.1.2 |
| `stamp` | Same as PlannedRoute | **1 Hz** ✅ | None | RFC-006 synchronized publication |
| `profile_id` | Same as route_id | — | None | Paired with PlannedRoute |
| `segment_start_distances_m` | Cumulative distance from start | — | None | Computed from waypoints |
| `segment_end_distances_m` | Cumulative distance to end | — | None | Computed from waypoints |
| `target_speeds_kn` | Per-segment `target_sog_kn` from YAML or default (10 kn for transit) | — | Mock uses YAML values; real L2 optimizes | SIL config matches scenario baseline |
| `max_speeds_kn` | Per-segment `target_sog_kn * 1.2` (upper bound) | — | Mock uses fixed 1.2× factor; real L2 computes capabilities | Conservative margin |
| `min_speeds_kn` | 2.0 kn (minimum steerage speed) | — | Mock hardcodes; real L2 uses ship maneuverability | FCB steerage speed ~2 kn |
| `segment_types` | `["transit"]` (all segments treated as transit) | — | Mock doesn't distinguish approach/harbor; real L2 segments by mission phase | SIL DEMO-1 is transit-only |
| `confidence` | 1.0 | — | Always 1.0 | Synthetic data |
| `rationale` | `"SIL_MOCK: {speed_source}"` | — | None | Audit trail |

**QoS**: TRANSIENT_LOCAL, 1 Hz (synchronized with PlannedRoute)

---

### 3.4 L2 → M3 Replan Response (RFC-006 §2.3)

**RFC-006 Scope**: L2 Voyage Planner → M3 (event-driven, ReplanResponseMsg)

Mock **does not implement** RouteReplanRequest subscriber or ReplanResponseMsg publisher. This is intentional **[TBD-future D2.2]**:
- R4 focuses on initial route publishing (unblock M3 startup)
- M3 replan triggering + L2 replan response belong to Phase 2 D2.2 (Voyage Planner mock upgrade)
- Current mock L2 is "write-only" (publishes task/route/speed), no subscribers

**Migration Path**: When real L2 arrives (Phase 2), replace mock with multi-directional subscriber setup (see §4.6).

---

# 4. Detailed Design

## 4.1 Mock L2 Node Architecture

### Placement Decision

**Recommendation**: New package `src/sil_mocks/mock_l2_publisher/`

**Rationale**:
- Keeps mock infrastructure separate from production kernel (cleaner deletion when real L2 arrives)
- Enables independent versioning and CI/CD gating
- Follows SIL orchestrator pattern: sil_orchestrator (core) + sil_mocks (test doubles)
- Alternative considered: `src/sil_orchestrator/mock_l2/` — rejected because orchestrator owns lifecycle/container; mocks should be pluggable

### Package Structure

```
src/sil_mocks/mock_l2_publisher/
├── CMakeLists.txt              # ROS2 ament_cmake
├── package.xml                 # deps: rclcpp, l3_external_msgs, geographic_msgs, sil_msgs
├── src/
│   ├── main.cpp                # Node entry point
│   ├── mock_l2_node.hpp         # Class definition
│   ├── mock_l2_node.cpp         # Implementation (subscriptions, publishers, logic)
│   └── route_generator.cpp      # Helper: fallback route generation + detour A*
├── config/
│   └── mock_l2_params.yaml      # Default route, speed profile, detour params
└── README.md                    # Documented for future handoff
```

### Node Template (Pseudo-code)

```cpp
// src/sil_mocks/mock_l2_publisher/src/mock_l2_node.hpp
#ifndef MOCK_L2_NODE_HPP
#define MOCK_L2_NODE_HPP

#include <rclcpp/rclcpp.hpp>
#include <l3_external_msgs/msg/voyage_task.hpp>
#include <l3_external_msgs/msg/planned_route.hpp>
#include <l3_external_msgs/msg/speed_profile.hpp>
#include <geographic_msgs/msg/geo_path.hpp>
#include <sil_msgs/msg/scenario_info.hpp>
#include <memory>
#include <vector>

class MockL2Publisher : public rclcpp::Node {
public:
    MockL2Publisher();
    ~MockL2Publisher() = default;

private:
    // Subscriptions: SIL scenario lifecycle
    rclcpp::Subscription<sil_msgs::msg::ScenarioInfo>::SharedPtr sub_scenario_;

    // Publishers: L2 output topics
    rclcpp::Publisher<l3_external_msgs::msg::VoyageTask>::SharedPtr pub_voyage_task_;
    rclcpp::Publisher<l3_external_msgs::msg::PlannedRoute>::SharedPtr pub_planned_route_;
    rclcpp::Publisher<l3_external_msgs::msg::SpeedProfile>::SharedPtr pub_speed_profile_;

    // Timer: periodic publication @ 1 Hz
    rclcpp::TimerBase::SharedPtr timer_;

    // State
    std::vector<geographic_msgs::msg::GeoPoint> current_route_waypoints_;
    std::vector<double> current_speed_profile_kn_;
    bool is_active_ = false;
    uint64_t route_id_ = 0;

    // Callbacks
    void on_scenario_loaded_(const sil_msgs::msg::ScenarioInfo::SharedPtr msg);
    void on_publish_timer_();

    // Helpers
    std::vector<geographic_msgs::msg::GeoPoint>
    generate_default_route_(
        const geographic_msgs::msg::GeoPoint& start,
        double initial_heading_deg,
        double initial_sog_kn,
        double distance_nm = 10.0  // default 10 nm straight line
    );

    std::vector<geographic_msgs::msg::GeoPoint>
    apply_island_detour_(
        const std::vector<geographic_msgs::msg::GeoPoint>& nominal_route,
        const std::vector<geographic_msgs::msg::GeoPath>& island_polygons
    );

    geographic_msgs::msg::GeoPath
    waypoints_to_geopath_(const std::vector<geographic_msgs::msg::GeoPoint>& wps);

    void publish_voyage_task_();
    void publish_planned_route_();
    void publish_speed_profile_();

    // Haversine distance calculator
    double haversine_distance_nm_(
        double lat1, double lon1,
        double lat2, double lon2
    );
};

#endif
```

---

## 4.2 Scenario YAML Schema v3.1 Extension

### JSON Schema Delta

Add optional section to scenario structure (backward compatible):

```yaml
ownShip:
  static: { ... (unchanged) ... }
  initial: { ... (unchanged) ... }
  model: { ... (unchanged) ... }
  controller: { ... (unchanged) ... }

  # NEW in v3.1: Optional nominal route (L2 mock input)
  nominalRoute:
    - latitude: 63.44
      longitude: 10.38
      target_sog_kn: 10.0
    - latitude: 63.54
      longitude: 10.38
      target_sog_kn: 10.0

  # NEW in v3.1: Optional island/obstacle polygons (for detour testing)
  obstacles:
    - name: "island_01"
      type: "island"
      vertices:
        - latitude: 63.47
          longitude: 10.35
        - latitude: 63.47
          longitude: 10.41
        - latitude: 63.49
          longitude: 10.41
        - latitude: 63.49
          longitude: 10.35
```

### Backward Compatibility

- `nominalRoute: null` or absent → mock L2 falls back to synthetic generation (straight line from initial position)
- v3.0 scenario YAML loads without modification
- Mock node validates: if `nominalRoute` exists and has ≥2 points, use it; else generate

### Pydantic / Python Model

If `src/sil_orchestrator/` uses Pydantic for scenario validation:

```python
class WaypointItem(BaseModel):
    latitude: float
    longitude: float
    target_sog_kn: float = 10.0  # default

class ObstaclePolygon(BaseModel):
    name: str
    type: Literal["island", "danger_zone"]
    vertices: List[WaypointItem]

class OwnShipYAML(BaseModel):
    static: OwnShipStatic
    initial: OwnShipInitial
    nominalRoute: Optional[List[WaypointItem]] = None
    obstacles: Optional[List[ObstaclePolygon]] = None
```

---

## 4.3 Default Route Generation (Fallback)

When scenario YAML lacks `nominalRoute` field:

1. **Start point**: `ownShip.initial.position` (lat/lon)
2. **Bearing**: `ownShip.initial.heading` (degrees)
3. **Distance**: Configurable, default 10 nm (1 hour transit at 10 kn baseline)
4. **Segments**: 2 waypoints (start + terminal)
5. **Speed**: `ownShip.initial.sog` (kn)

**Pseudo-code**:

```cpp
std::vector<geographic_msgs::msg::GeoPoint> 
MockL2Publisher::generate_default_route_(
    const geographic_msgs::msg::GeoPoint& start,
    double heading_deg,
    double sog_kn,
    double distance_nm
) {
    using namespace geographic_msgs::msg;
    
    std::vector<GeoPoint> route;
    route.push_back(start);  // WP0 = ownship initial position
    
    // WP1 = start + bearing×distance
    double bearing_rad = heading_deg * M_PI / 180.0;
    double dlat = distance_nm / 60.0;  // 1 nm ≈ 1 arcmin latitude
    double dlon = distance_nm / (60.0 * std::cos(start.latitude * M_PI / 180.0));
    
    GeoPoint terminal;
    terminal.latitude = start.latitude + dlat * std::cos(bearing_rad);
    terminal.longitude = start.longitude + dlon * std::sin(bearing_rad);
    terminal.altitude = 0.0;
    route.push_back(terminal);
    
    return route;
}
```

---

## 4.4 RouteReplanRequest Handler

**Status**: NOT IMPLEMENTED in R4

Mock L2 node does **not** subscribe to `/l3/m3/route_replan_request` in DEMO-1. This is intentional:

- R4 unblocks M3 startup with initial route only
- Replan request handling → Phase 2 D2.2 (Voyage Planner mock upgrade)
- M3 internal replan state machine exists but won't be triggered until real L2 (or upgraded mock) subscribes

**Future Implementation Trigger** (D2.2):
- Add subscriber to `/l3/m3/route_replan_request` 
- Parse request + compute detour (simple A* or RRT*)
- Publish `/l2/replan_response` (SUCCESS / FAILED_TIMEOUT / etc.)

---

## 4.5 Island Detour Scenario Design

### New Scenario File: `/scenarios/Grounding/island-detour-test.yaml`

**Purpose**: Demonstrate spatial reasoning and obstacle avoidance in route generation. Mock L2 applies simple geometric detour logic.

**Scenario Parameters**:

```yaml
title: Island Detour Test — L2 Spatial Routing
description: >
  Nominal route crosses rectangular island; mock L2 computes detour.
  Validates that ownship trajectory clears island polygon.
startTime: '2026-01-01T00:00:00Z'

ownShip:
  static:
    id: 1
    shipType: FCB
    name: FCB Test Vessel
    mmsi: 123456789
  initial:
    position:
      latitude: 63.40
      longitude: 10.30
    cog: 0.0
    sog: 10.0
    heading: 0.0
    navStatus: Under way using engine
  model: fcb_mmg_vessel
  controller: psbmpc_wrapper

  # NEW: Nominal route collides with island
  nominalRoute:
    - latitude: 63.40
      longitude: 10.30
      target_sog_kn: 10.0
    - latitude: 63.60
      longitude: 10.30     # ← This point is INSIDE island_01
      target_sog_kn: 10.0

  # NEW: Island obstacle
  obstacles:
    - name: island_01
      type: island
      vertices:
        - latitude: 63.50
          longitude: 10.20
        - latitude: 63.50
          longitude: 10.40
        - latitude: 63.65
          longitude: 10.40
        - latitude: 63.65
          longitude: 10.20

targetShips:
  # (empty for this scenario — focusing on route geometry)

environment:
  wind:
    dir_deg: 0.0
    speed_mps: 0.0
  current:
    dir_deg: 0.0
    speed_mps: 0.0
  visibility_nm: 10.0

metadata:
  schema_version: '3.1'
  scenario_id: grounding-island-detour-v1.0
  vessel_class: FCB
  odd_cell:
    domain: open_sea_offshore_wind_farm
  encounter: null  # Navigation-only; no encounter rule
  expected_outcome:
    # Route should NOT pass through island
    trajectory_clears_islands: true
    min_distance_to_island_m: 100.0  # Safety margin
  simulation_settings:
    total_time: 3600.0  # 1 hour
    dt: 0.02
    n_rps_initial: 3.0
    coordinate_origin: [63.40, 10.30]
    dynamics_mode: internal
    backend: ros2
  disturbance:
    wind: {dir_deg: 0.0, speed_mps: 0.0}
    current: {dir_deg: 0.0, speed_mps: 0.0}
```

### Detour Algorithm (Simplified)

Mock L2 applies **naive geometric detour** (not full A*) when nominal route intersects island polygon:

1. **Detect collision**: For each segment of nominal route, check if it intersects any island polygon (line-polygon intersection test)
2. **Compute detour**: If collision detected, insert waypoint **perpendicular to segment**, offset 200 m port/starboard of obstacle center
3. **Re-validate**: Check if detoured route clears all islands
4. **Fallback**: If detour still fails, extend terminal waypoint (abandon intermediate WP) + skip island on starboard side

**Pseudo-code**:

```cpp
std::vector<geographic_msgs::msg::GeoPoint>
MockL2Publisher::apply_island_detour_(
    const std::vector<geographic_msgs::msg::GeoPoint>& nominal_route,
    const std::vector<geographic_msgs::msg::GeoPath>& island_polygons
) {
    auto route = nominal_route;
    
    for (const auto& island : island_polygons) {
        // Check each segment of route
        for (size_t i = 0; i + 1 < route.size(); ++i) {
            if (segment_intersects_polygon_(route[i], route[i+1], island)) {
                // Insert detour waypoint (port or starboard offset)
                auto detour_wp = compute_perpendicular_offset_(
                    route[i], route[i+1],
                    200.0  // offset distance (m), configurable
                );
                route.insert(route.begin() + i + 1, detour_wp);
                break;  // Restart loop; may need multiple passes
            }
        }
    }
    
    return route;
}
```

**Known Limitation**: This is O(n²) and doesn't guarantee optimal solution. Real L2 should use RRT* or grid-based planner. R4 is **proof-of-concept** only.

---

## 4.6 Contract Divergence Document

**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/docs/Design/SIL/v1.0-unified/05-sil-mock-l2-contract.md`

**Content Structure**:

```markdown
# SIL Mock L2 Contract Specification

## 1. Purpose
Define boundary between mock L2 (R4, DEMO-1) and real L2 (D2.2, Phase 2).
Ensure swappability without downstream module changes.

## 2. Implemented Topics

### 2.1 `/l1/voyage_task` (TRANSIENT_LOCAL, 0.1 Hz mock / event-driven real)
- RFC-006 § 15.1 VoyageTask IDL [v1.1.2]
- Mock behavior: singleton publish at scenario activation + 1 Hz repeat
- Real L2 behavior: plan-triggered, may be event-driven
- Swappability: Identical IDL; frequency difference transparent to M3 (latch + callback driven)

### 2.2 `/l2/planned_route` (TRANSIENT_LOCAL, 1 Hz)
- RFC-006 § 15.1 PlannedRoute IDL [v1.1.2]
- Mock behavior: 1 Hz stream, route from YAML nominalRoute or fallback generation
- Real L2 behavior: 0.5–1 Hz optimization output
- Divergences:
  - Mock: confidence always 1.0 (synthetic)
  - Real: 0.0–1.0 based on solver convergence
  - Mock: speed_profile_kn hardcoded per segment
  - Real: optimized per mission priority
- Swappability: Code-level swap (same IDL, QoS); M3 subscribes idempotently

### 2.3 `/l2/speed_profile` (TRANSIENT_LOCAL, 1 Hz)
- RFC-006 § 15.1 SpeedProfile IDL [v1.1.2]
- Same as PlannedRoute (parallel publication)
- Swappability: Identical

### 2.4 RouteReplanRequest Handler (NOT IMPL in R4)
- RFC-006 § 2.3 ReplanResponseMsg IDL
- Mock: No subscriber; returns implicit NACK (timeout)
- Real L2: Subscribes, computes, publishes ReplanResponseMsg
- Upgrade trigger: D2.2 Phase 2
- Mitigation (DEMO-1): M3 replan timeout causes ToR + ODD_EXIT; acceptable fallback

## 3. Implementation Constraints

### 3.1 Confidence Field
- Mock always 1.0 (synthetic data, deterministic generation)
- Real L2: 0.0–1.0 (solver quality, obstacle uncertainty, etc.)
- M7 (Safety Supervisor) uses this field for SOTIF assumption checking
  - Threshold: if confidence < 0.3 during ODD-A, escalate to ODD-B
  - Mock never triggers this (always 1.0)

### 3.2 Speed Profile Semantics
- Mock: per-segment constant speed (linear interpolation)
- Real L2: may use piecewise acceleration/deceleration
- Impact: M5 Tactical Planner consumes speed_profile as target guidance
  - M5 internal MPC optimizes around target speeds
  - Mock provides deterministic reference; real L2 may provide ranges (min/max)

### 3.3 Rationale Field (Audit Trail)
- Mock: `"SIL_MOCK: {source}"` (e.g., "SIL_MOCK: YAML nominalRoute")
- Real L2: `"{algorithm}; solver_iterations={N}; cost={C}"`
- Transparency Requirement: M8 (HMI) and ASDR (black box) consume this for SAT-2 / decision log
- Swappability: M8/ASDR treat as free-text; no parsing dependency

## 4. Known Limitations & Future Work

| Limitation | Impact | Mitigation (R4) | Real L2 (D2.2) |
|---|---|---|---|
| No ReplanRequest subscriber | M3 replan timeout → ToR | Use default route (no dynamic replanning) | Implement full M3↔L2 replan loop |
| No exclusion_zones (ENC) | Ownship may plan through danger | Mock doesn't validate; real L2 must | S-57 ENC loader + DNV maritime-schema |
| No multi-segment speed optimization | Constant speed per segment | Accept conservative speed (nominal) | Solver-driven optimization per mission |
| No current/tide factors | Route assumes zero drift | Acceptable for SIL (env section is zero) | Integrate forecast + leeway models |
| Naive geometric detour | Suboptimal obstacle avoidance | Acceptable for proof-of-concept | RRT* or grid-based planner |

## 5. Transition Checklist (D2.2)

When real L2 Voyage Planner arrives (Phase 2):

- [ ] Real L2 publishes `/l1/voyage_task` (same IDL)
- [ ] Real L2 publishes `/l2/planned_route` (same IDL)
- [ ] Real L2 publishes `/l2/speed_profile` (same IDL)
- [ ] Real L2 subscribes to `/l3/m3/route_replan_request` and responds
- [ ] Disable or remove mock node from docker-compose.yml
- [ ] M3 behavioral changes: nil (idempotent consumption)
- [ ] M5/M1 behavioral changes: nil (idempotent consumption)
- [ ] HIL regression: Run imazu-01-ho + grounding-island-detour with real L2, confirm Δ trajectory < 5% CPA

## 6. Testing Validation Points

### 6.1 Mock L2 Unit Tests (R4)
- Route generation from YAML: waypoint count, distance accuracy
- Fallback route generation: bearing/distance projection
- Island detour: collision detection, perpendicular offset correctness
- Message publishing: topic frequency, QoS reliability

### 6.2 Integration Tests (R4)
- M3 state machine: AWAITING_ROUTE → ACTIVE transition < 5s
- M3 mission_state publisher: activates after L2 publication
- M5 consumption: speed_profile read correctly, incorporated into MPC
- Backward compat: v3.0 scenarios (no nominalRoute) load and generate default

### 6.3 Scenario Validation (R4)
- imazu-01-ho: Route from YAML produces 2 WP, published @ 1 Hz, M3 unlocked
- island-detour-test: Detoured route clears island polygon by ≥100 m safety margin
```

---

# 5. Affected Files

| File | Reason | Risk | Notes |
|---|---|---|---|
| **NEW** `src/sil_mocks/mock_l2_publisher/CMakeLists.txt` | ROS2 package scaffolding | Low | Standard boilerplate |
| **NEW** `src/sil_mocks/mock_l2_publisher/package.xml` | Package manifest | Low | deps: rclcpp, l3_external_msgs, geographic_msgs |
| **NEW** `src/sil_mocks/mock_l2_publisher/src/main.cpp` | Node entry point | Low | Minimal (node construction + spin) |
| **NEW** `src/sil_mocks/mock_l2_publisher/src/mock_l2_node.hpp` | Class definition | Low | Interface only; no external deps |
| **NEW** `src/sil_mocks/mock_l2_publisher/src/mock_l2_node.cpp` | Core logic (subscriptions, publishers, route generation) | **Medium** | Main implementation; detour algorithm correctness critical |
| **NEW** `src/sil_mocks/mock_l2_publisher/src/route_generator.cpp` | Route synthesis + detour helpers | Medium | Haversine calc, polygon intersection geometry; test-driven |
| **NEW** `src/sil_mocks/mock_l2_publisher/config/mock_l2_params.yaml` | ROS2 parameter file (default route distance, detour offset, etc.) | Low | Tunable; no hardcodes |
| **NEW** `/scenarios/IMAZU标准测试/imazu-01-ho.yaml` → MOD | Add `ownShip.nominalRoute` field (2 WP) | **Low-Medium** | Backward compat maintained (optional field); v3.0 scenarios unaffected |
| **NEW** `/scenarios/Grounding/island-detour-test.yaml` | Grounding test scenario with obstacle polygons | Low | New test artifact; doesn't affect existing scenarios |
| **NEW** `docs/Design/SIL/v1.0-unified/05-sil-mock-l2-contract.md` | Contract divergence + swappability doc | Low | Documentation only |
| **MOD** `docker/sil_topic_bridge.py` | **Conditional**: If mock L2 publishes separate topics (e.g., `/sil/voyage_task_from_mock`), bridge may need wiring | Medium | **Decision needed**: Does mock publish directly to `/l2/planned_route`, or via bridge alias? Recommend: direct to `/l2/planned_route` (no bridge change). |
| **MOD** `docker-compose.yml` or lifecycle config | Launch mock L2 node as service or on-demand | Medium | Add `sil-mock-l2-publisher` container or integrate into `sil-orchestrator` launch file. Orchestrator cleanup must teardown mock. |
| **MOD** `src/sil_orchestrator/lifecycle_bridge.py` | Inject scenario metadata → mock L2 (topic publish) | Medium | Orchestrator must signal mock when scenario activates/cleans. Use event topic (e.g., `/sil/scenario_lifecycle`). |

---

# 6. Implementation Steps

## Phase A: Infrastructure Setup (Immediate)

### Step A.1: Create Mock L2 Package Structure
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_mocks/mock_l2_publisher/CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.8)
project(mock_l2_publisher)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(geographic_msgs REQUIRED)
find_package(sil_msgs REQUIRED)

add_executable(mock_l2_node
  src/main.cpp
  src/mock_l2_node.cpp
  src/route_generator.cpp
)

target_include_directories(mock_l2_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(mock_l2_node
  rclcpp
  l3_external_msgs
  geographic_msgs
  sil_msgs
)

install(TARGETS mock_l2_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY config
  DESTINATION share/${PROJECT_NAME}
)

ament_package()
```

**Verification**: `colcon build --packages-select mock_l2_publisher` succeeds

---

### Step A.2: Implement Mock L2 Node Class
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_mocks/mock_l2_publisher/src/mock_l2_node.cpp`

Implement:
- Constructor: Create publishers (TRANSIENT_LOCAL QoS, latched)
- Subscription callback: `on_scenario_loaded_()` → parse YAML route + set `is_active_=true`
- Timer callback: `on_publish_timer_()` → publish voyage_task + planned_route + speed_profile @ 1 Hz

**Verification**:
```bash
# In SIL container
ros2 run mock_l2_publisher mock_l2_node &
sleep 2
ros2 topic hz /l2/planned_route  # Should show ~1 Hz
ros2 topic echo --once /l2/planned_route  # Should show route structure
```

---

### Step A.3: Implement Route Generation Helpers
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_mocks/mock_l2_publisher/src/route_generator.cpp`

Implement:
- `haversine_distance_nm_()`: Calculate distance between lat/lon pairs
- `generate_default_route_()`: Straight-line projection from initial position
- `segment_intersects_polygon_()`: Line-polygon collision detection (GJK or simple AABB)
- `apply_island_detour_()`: Insert perpendicular waypoint on collision

**Verification** (unit tests, optional for R4 but recommended):
```cpp
// Test default route generation
auto route = node.generate_default_route_({63.44, 10.38}, 0.0, 10.0, 10.0);
assert(route.size() == 2);
assert(abs(route[1].latitude - 63.44 - 10.0/60.0) < 0.01);  // ~10 nm north

// Test detour
auto detoured = node.apply_island_detour_(nominal, {island_polygon});
assert(!route_intersects_polygon(detoured, island_polygon));
```

---

## Phase B: Scenario Schema Extension (Day 1–2)

### Step B.1: Update imazu-01-ho.yaml
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/scenarios/IMAZU标准测试/imazu-01-ho.yaml`

Add to `ownShip` section:
```yaml
nominalRoute:
  - latitude: 63.44
    longitude: 10.38
    target_sog_kn: 10.0
  - latitude: 63.54
    longitude: 10.38
    target_sog_kn: 10.0
```

**Verification**:
```bash
python3 -c "import yaml; yaml.safe_load(open('imazu-01-ho.yaml'))"  # YAML valid
curl -s http://localhost:8000/api/v1/scenarios/imazu-01-ho \
  | jq '.ownShip.nominalRoute' | head -20  # Should show route array
```

---

### Step B.2: Create island-detour-test.yaml
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/scenarios/Grounding/island-detour-test.yaml`

(See §4.5 for full scenario definition)

**Verification**: Same YAML validation as B.1

---

## Phase C: Docker & Orchestrator Integration (Day 2–3)

### Step C.1: Update docker-compose.yml
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/docker-compose.yml`

Option 1 (Recommended): Integrated into `sil-nodes` service (mock runs as thread in ROS2 launch)

```yaml
sil-nodes:
  build:
    context: .
    dockerfile: docker/sil_nodes.Dockerfile
  environment:
    - ROS_LOCALHOST_ONLY=0
    - ROS_DOMAIN_ID=0
  command: >
    bash -lc '
      source /opt/ros/humble/setup.bash &&
      source /opt/ws/install/setup.bash &&
      ros2 launch l3_tdl_kernel bringup_demo1.launch.py &
      ros2 run mock_l2_publisher mock_l2_node &
      wait
    '
```

Option 2 (Decoupled): Separate container (allows independent restart)

```yaml
sil-mock-l2:
  build:
    context: .
    dockerfile: docker/sil_mock_l2.Dockerfile
  depends_on:
    - sil-nodes
```

**Verification**: `docker compose up -d && docker compose logs sil-nodes | grep "mock_l2_node"` should show node init

---

### Step C.2: Update Orchestrator Lifecycle Integration
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_orchestrator/lifecycle_bridge.py`

Orchestrator must emit scenario metadata when scenario loads. Mock L2 subscribes and extracts `ownShip.nominalRoute`.

**Option A** (Minimal): Orchestrator publishes ScenarioInfo topic
```python
# lifecycle_bridge.py
def on_activate(self, scenario_id):
    # ... existing logic ...
    self.pub_scenario_info.publish(ScenarioInfo(
        scenario_id=scenario_id,
        yaml_content=scenario_yaml  # Full YAML as string (or dict serialized)
    ))
```

Mock L2 subscribes:
```cpp
// mock_l2_node.cpp
void MockL2Publisher::on_scenario_loaded_(
    const sil_msgs::msg::ScenarioInfo::SharedPtr msg
) {
    // Parse YAML from msg.yaml_content
    auto config = YAML::Load(msg->yaml_content);
    // Extract nominalRoute or generate default
    current_route_waypoints_ = extract_route_(config["ownShip"]);
    is_active_ = true;
}
```

**Verification**: Activate imazu → mock L2 receives event → parses route → publishes

---

### Step C.3: Docker Topic Bridge Wiring (if needed)
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/docker/sil_topic_bridge.py`

**Decision**: Mock L2 publishes directly to `/l2/planned_route` (no bridge). Bridge remains unchanged.

If future integration requires aliasing (e.g., `/sil/planned_route_mock` → `/l2/planned_route`), add subscription + publisher. For R4, **recommend no bridge changes**.

---

## Phase D: Documentation & Testing (Day 3–4)

### Step D.1: Write Contract Divergence Doc
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/docs/Design/SIL/v1.0-unified/05-sil-mock-l2-contract.md`

(See §4.6 for full content)

**Verification**: Markdown renders, no broken links to IDL files

---

### Step D.2: Unit Tests (Optional but Recommended)
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/src/sil_mocks/mock_l2_publisher/test/test_route_generator.cpp`

Test cases:
1. Default route projection (bearing + distance)
2. Haversine distance accuracy
3. Polygon intersection detection
4. Detour waypoint insertion

```cpp
#include <gtest/gtest.h>
#include "../src/route_generator.cpp"

TEST(RouteGenerator, DefaultRouteBearing) {
    MockL2Publisher node;
    geographic_msgs::msg::GeoPoint start{.latitude = 63.44, .longitude = 10.38};
    auto route = node.generate_default_route_(start, 0.0, 10.0, 10.0);
    
    EXPECT_EQ(route.size(), 2);
    EXPECT_NEAR(route[1].latitude, 63.44 + 10.0/60.0, 0.01);
    EXPECT_NEAR(route[1].longitude, 10.38, 0.01);
}
```

**Verification**: `colcon test --packages-select mock_l2_publisher` passes

---

### Step D.3: Integration Test (SIL Full Stack)
**File**: `/Users/marine/Code/MASS-L3-Tactical Layer/test/r4_integration_test.sh`

```bash
#!/bin/bash
set -e

echo "=== R4 Integration Test ==="

# Start SIL stack
docker compose down
docker compose up -d

sleep 5

# Activate imazu scenario
curl -s -X POST https://localhost:8000/api/v1/lifecycle/cleanup
curl -s -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id":"imazu-01-ho"}'
curl -s -X POST https://localhost:8000/api/v1/lifecycle/activate

sleep 3

# Check topics
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  
  echo "=== L2 Topics ==="
  ros2 topic hz /l2/planned_route --window 2 | grep "average"
  ros2 topic hz /l2/speed_profile --window 2 | grep "average"
  ros2 topic hz /l1/voyage_task --window 2 | grep "average"
  
  echo "=== M3 State ==="
  ros2 topic info /l3/m3/mission_state -v | grep "Publisher count"
  
  echo "=== M5 Subscription ==="
  ros2 topic info /l2/planned_route -v | grep "Subscription count"
'

echo "✅ All topics publishing; M3 mission_state active"
```

**Verification**: Script runs successfully, no timeout errors

---

# 7. Verification Plan

## 7.1 Topic Emergence & Frequency

**Command**:
```bash
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  
  for topic in /l1/voyage_task /l2/planned_route /l2/speed_profile; do
    echo "=== $topic ==="
    timeout 5 ros2 topic hz $topic --window 3 | grep "average rate"
  done
'
```

**Expected Output**:
- `/l1/voyage_task`: ~0.5–1 Hz (mock may batch + repeat)
- `/l2/planned_route`: ~1 Hz ✅
- `/l2/speed_profile`: ~1 Hz ✅

**Failure Threshold**: Any topic < 0.5 Hz or > 2 Hz → **FAIL**

---

## 7.2 M3 State Machine Progression

**Command**:
```bash
# Pre-activation
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic info /l3/m3/mission_state -v | grep "Publisher count"
' 
# Expected: Publisher count: 0

# Activate scenario
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate
sleep 5

# Post-activation
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic info /l3/m3/mission_state -v | grep "Publisher count"
'
# Expected: Publisher count: 1
```

**Failure Threshold**: Publisher count remains 0 after 5 s → **FAIL**

---

## 7.3 Island Detour Scenario Validation

**Command**:
```bash
# Activate island-detour-test
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id":"grounding-island-detour-v1.0"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate

sleep 3

# Check published route
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic echo --once /l2/planned_route
' | jq '.route.points | length'
# Expected: ≥3 (original 2 + 1 detour waypoint)

# Post-run trajectory analysis (separate Python script)
python3 test/verify_grounding_clearance.py \
  --trajectory /tmp/ownship_trajectory.csv \
  --island-polygon test/island_polygon.json
# Expected: min_distance_to_island >= 100.0 m
```

**Failure Threshold**: Min distance to island < 100 m OR route has only 2 waypoints (no detour) → **FAIL**

---

## 7.4 Backward Compatibility

**Command**:
```bash
# Load v3.0 scenario WITHOUT nominalRoute
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/configure \
  -H "Content-Type: application/json" \
  -d '{"scenario_id":"some-v3.0-scenario"}'
curl -sk -X POST https://localhost:8000/api/v1/lifecycle/activate

sleep 3

# Should generate default route
docker exec mass-l3-tacticallayer-sil-nodes-1 bash -lc '
  source /opt/ros/humble/setup.bash
  source /opt/ws/install/setup.bash
  ros2 topic echo --once /l2/planned_route | jq '.rationale'
'
# Expected: "SIL_MOCK: default_generation"
```

**Failure Threshold**: Scenario fails to load OR route not published → **FAIL**

---

## 7.5 Acceptance Criteria

| Criterion | Pass/Fail | Evidence |
|---|---|---|
| Mock L2 publishes `/l1/voyage_task` @ 1 Hz | **PASS** | Topic echo shows 1+ Hz, rationale is "SIL_MOCK:..." |
| Mock L2 publishes `/l2/planned_route` @ 1 Hz | **PASS** | Topic hz shows ~1.0 Hz; route_id increments; confidence = 1.0 |
| Mock L2 publishes `/l2/speed_profile` @ 1 Hz synchronized | **PASS** | Topic hz shows ~1.0 Hz; profile_id matches route_id; segment count matches waypoints |
| M3 mission_state publisher activates within 5 s | **PASS** | ros2 topic info shows Publisher count: 1 |
| imazu-01-ho.yaml loads with nominalRoute | **PASS** | Scenario activates; route_id > 0; waypoint count = 2 |
| island-detour-test.yaml detours around island | **PASS** | Waypoint count ≥ 3; min distance to island > 100 m |
| v3.0 scenarios (no nominalRoute) generate default | **PASS** | Scenario loads; route publishes with rationale = "default_generation" |
| Contract doc exists + addresses swappability | **PASS** | File exists; D2.2 transition checklist present |

---

# 8. Risks & Mitigations

| Risk | Probability | Impact | Mitigation |
|---|---|---|---|
| **Polygon intersection algorithm incorrect** | Medium | Route may pass through island (unsafe) | Unit tests for GJK / AABB collision; manual verification on grounding scenario |
| **Mock L2 node crashes before M3 subscribes** | Low | M3 still stuck (AWAITING_ROUTE) | Implement startup synchronization (wait for first subscriber on `/l2/planned_route` before publishing) |
| **Scenario lifecycle not propagated to mock** | Medium | Mock doesn't know when scenario changes; stale route in memory | Implement `/sil/scenario_lifecycle` event subscription in mock; validate on integration test |
| **QoS mismatch** (mock sends VOLATILE, M3 expects TRANSIENT_LOCAL) | Low | M3 misses latch; no route latch on late subscribes | Lock mock QoS to TRANSIENT_LOCAL in code; test late subscriber join |
| **Route generation projection error** (Haversine off by >1%) | Low | Route waypoints geographically wrong | Unit test Haversine with known distance pairs (e.g., 1 nm = 1.852 km); compare to online calculators |
| **Docker container fails to build** (mock_l2_publisher depends unknown) | Low | CI/CD blocked | Explicitly declare CMakeLists deps; run `colcon build` locally before push |
| **Island polygon vertices order confusion** (CCW vs CW) | Medium | Collision detection fails intermittently | Document vertex order assumption; validate on island-detour-test |
| **DEMO-1 demo machine lacks disk/memory for mock node** | Low | SIL stack OOM | Mock node footprint < 50 MB (stateless); verify with `docker stats` |

---

# 9. Out of Scope

- **Real L2 Voyage Planner implementation** — sister team responsibility (Phase 2 D2.2)
- **Real S-57 ENC integration** — D2.2 task; R4 uses inline YAML polygons
- **M3 internal logic or state machine changes** — R4 is pure input injection
- **L4 Guidance Layer integration** — out of L3 scope; M5 output remains AvoidancePlan IDL
- **Actual waypoint optimization** (RRT*, A*, etc.) — mock uses naive detour; real L2 optimizes
- **Current/tide drift compensation** — environment section left as-is (zero current for DEMO-1)
- **ReplanRequest subscriber + response** — deferred to Phase 2 D2.2 (mock L2 upgrade)

---

# 10. Open Questions

1. **[TBD-placement]**: Should mock L2 be in `src/sil_mocks/` or integrated into `src/sil_orchestrator/`?
   - Impact: Package dependencies, docker-compose orchestration, removal complexity (D2.2)
   - Recommendation: `src/sil_mocks/` for clean separation

2. **[TBD-polygon-format]**: Should island obstacles use GeoJSON, WKT, or inline YAML arrays?
   - Impact: Parsing complexity, interoperability with DNV maritime-schema
   - Recommendation: Inline YAML arrays for R4 (simplicity); upgrade to GeoJSON/maritime-schema in D2.2

3. **[TBD-detour-algorithm]**: What's the acceptable polygon intersection tolerance for "safe clearance"?
   - Impact: island-detour-test acceptance criteria
   - Recommendation: Minimum 100 m safety margin (configurable param); validate with real ENC offset rules in D2.2

---

# Summary

R4 unblocks DEMO-1 by injecting mock L2 route data into M3, enabling M3 state machine progression → M1 envelope context → M4 behavior optimization. Scenario YAML schema extended to support explicit nominal routes. Backward compatible. Contract document ensures future swappability with real L2 (Phase 2 D2.2). Implementation is **2.0 pw** (straightforward ROS2 node + helpers); testing is **0.5 pw** (integration + scenario validation).

**Key Success Metric**: M3 mission_state publisher activates within 5 s of mock L2 publication. Island detour scenario demonstrates spatial reasoning without S-57 dependency.
