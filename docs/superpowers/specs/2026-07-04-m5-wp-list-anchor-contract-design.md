# M5 AvoidancePlan WP List Anchor Contract — Design Spec (v2.3 δ)

**Date**: 2026-07-04
**Status**: Draft (pending user review)
**Supersedes**: none (incremental on v2.2 `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`)
**Scope**: M5 Tactical Planner — WP list uniform anchor contract + preflight skip-anchor
**D-task ID**: D-ε (delta on v2.2)

## 1. Problem

V2.2 validation probe (rule14-ho, run-19f2dad3b5a) RED with CPA 3.8m (floor 180m). Root cause traced end-to-end (ZCode trace + Codex diagnosis task, 2026-07-04):

**Three M5 avoidance WP generators have inconsistent WP[0] semantics**:

| Generator | WP[0] meaning | Distance from origin |
|---|---|---|
| `MidMpcWaypointGenerator::build_waypoints_` | own ship anchor (`ned_pos[0]=(0,0)`) | 0m |
| `build_geometric_fallback_plan_` | own ship anchor (`arc_point(t=0)`) | 0m |
| `generate_stable_avoidance_corridor_waypoints` | real first maneuver target (`kDistancesM[0]=150m`) | 150m |
| `generate_rule13_overtake_corridor_waypoints` | real first maneuver target (`kDistancesM[0]=600m`) | 600m |
| `generate_return_to_route_waypoints` | real first maneuver target (500m) | 500m |

`validate_gnc_avoidance_plan` (gnc_avoidance_preflight.hpp:226) computes `first_distance = origin → wps[0]` and rejects when `< emergency_wheel_over_distance_m` (360m). For anchor-WP[0] generators, `first_distance = 0m` → all optimized/fallback plans rejected with `first_maneuver_point_too_close available=0.0`.

**Not a v2.2 regression**: v2.2 changes constraint derive (ROT∩box direction-aware), WP generation unchanged. v2.1 also 0% CONVERGED (NLP solver Phase-3 stub historically never converged; when it does converge post-v2.2, this latent indexing defect surfaces).

## 2. Design: Uniform Anchor Contract

### 2.1 Contract

All M5 avoidance plan WP lists follow `[anchor, maneuver_target_1, ..., maneuver_target_N]`:
- `WP[0]` = current-pos anchor (own ship position at plan generation time)
- `WP[1..N]` = real maneuver targets (distance from anchor > 0, typically ≥ wheel-over distance)
- `WP list` length ≥ 2 (anchor + ≥1 maneuver)

### 2.2 Generator Changes

**`generate_stable_avoidance_corridor_waypoints`** (avoidance_waypoint_gen.hpp:176):
`kDistancesM` prepend 0.0:
```cpp
static const std::vector<double> kDistancesM = {
    0.0,  // anchor: own ship position at plan generation
    150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
    6000.0, 7500.0, 9000.0};
```
At `d=0.0`: `d_north = d_east = 0.0` → `wps[0] = {anchor_lat, anchor_lon}`. Lateral cap formula naturally yields `lateral_abs = cap * (1 - exp(0)) = 0` at d=0, so no dogleg at anchor.

**`generate_rule13_overtake_corridor_waypoints`** (avoidance_waypoint_gen.hpp:287):
`kDistancesM` prepend 0.0:
```cpp
static const std::vector<double> kDistancesM = {
    0.0,  // anchor
    600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
    8400.0, 10000.0, 12000.0};
```

**`MidMpcWaypointGenerator`** (mid_mpc_waypoint_generator.cpp:171-189): **no change**. Already conforms: `ned_pos[0]=(0,0)` is the anchor, `ned_pos[k>0]` are dead-reckoned future positions.

**`build_geometric_fallback_plan_`** (mid_mpc_node.cpp:986-1005): **no change**. Already conforms: `arc_point(t=0)` returns own ship position as WP[0].

**`generate_return_to_route_waypoints`** (avoidance_waypoint_gen.hpp:312): **no change**. This is the exception — WP[0] is a real 500m maneuver target, NOT an anchor. Return path caller passes `has_anchor=false`. Rationale: return path starts after avoidance release, own ship is already maneuvering; there is no "current pos anchor" semantics. Documented as known exception in §2.4.

### 2.3 Preflight Changes

**`validate_gnc_avoidance_plan`** (gnc_avoidance_preflight.hpp:208): add `bool wps_has_anchor = false` parameter.

When `wps_has_anchor == true`:
- Size check: `wps.size() < 3` → invalid (need anchor + ≥2 maneuver for segment check). When `wps.size() == 2` (anchor + 1 maneuver): only first_distance check, skip segment/turn_radius.
- `first_distance = origin → wps[1]` (skip wps[0]=anchor). Since `origin ≈ wps[0]` (both are own ship), this is effectively `wps[0] → wps[1]` = anchor → first maneuver.
- Segment check loop: `for i in [1, wps.size()-1)`: `segment = wps[i] → wps[i+1]`. Anchor→wps[1] segment skipped (it's the maneuver-initiation segment, validated by first_distance + turn_radius).
- Turn radius check: `origin → wps[1] → wps[2]` (skip anchor as middle node).

When `wps_has_anchor == false` (default): behavior unchanged (backward compat).

**`validate_canonical_route_for_gnc`** (mid_mpc_waypoint_generator.cpp:88): add `bool wps_has_anchor = false` parameter, forward to `validate_gnc_avoidance_plan`.

### 2.4 Caller Classification

| Caller | File:line | WP[0] semantics | `has_anchor` |
|---|---|---|---|
| optimized path preflight | mid_mpc_node.cpp:1530 | anchor (from generator) | `true` |
| degraded corridor preflight | mid_mpc_node.cpp:1662 | anchor (after §2.2 change) | `true` |
| return-to-route preflight | mid_mpc_node.cpp:1761 | real maneuver (500m) | `false` |
| full route preflight | mid_mpc_node.cpp:1827 | anchor (plan.waypoints starts with anchor) | `true` |
| L2 suffix feasibility | mid_mpc_waypoint_generator.cpp:133 | anchor (candidate = plan + suffix) | `true` |

**Return path is the sole exception**. `generate_return_to_route_waypoints` semantically has no anchor (post-avoidance release, ship already maneuvering). Its caller keeps `has_anchor=false`.

## 3. Testing

### 3.1 Unit Test: Anchor Contract

New test `test_wp_anchor_contract.cpp` (or extend existing mid_mpc_waypoint_generator test):

1. **Generator conformance**: for each of the 5 generators (after change), assert WP list matches contract:
   - `MidMpcWaypointGenerator`: WP[0] = own ship lat/lon (within 1m)
   - `build_geometric_fallback_plan_`: WP[0] = own ship lat/lon
   - `generate_stable_avoidance_corridor_waypoints`: WP[0] = anchor lat/lon, WP[1] at 150m
   - `generate_rule13_overtake_corridor_waypoints`: WP[0] = anchor, WP[1] at 600m
   - `generate_return_to_route_waypoints`: WP[0] at 500m (NOT anchor — exception documented)

2. **Preflight skip-anchor**: `validate_gnc_avoidance_plan` with `has_anchor=true`:
   - WP list `[anchor, maneuver_360m, ...]` → feasible (first_distance=360m ≥ required)
   - WP list `[anchor, maneuver_100m, ...]` → infeasible (first_distance=100m < required)
   - WP list `[anchor]` (size=1) → invalid
   - WP list `[anchor, maneuver]` (size=2) → feasible if first_distance ≥ required
   - WP list with `has_anchor=false` → unchanged behavior

3. **Regression**: existing preflight tests (test_gnc_avoidance_preflight.cpp) must still pass with default `has_anchor=false`.

### 3.2 Integration: V2 Probe

After L3 fix, rerun V2 rule14-ho probe. **Pass criteria unchanged** (spec §7.2):
- NLP SOLVER_CONVERGED > 30%
- CPA min ≥ 180m

**Expected post-fix behavior**:
- NLP optimized plan WP list `[anchor, maneuver_1, ...]` passes preflight (maneuver_1 at wheel-over distance)
- If NLP still INFEASIBLE (Layer 1 structural), plan falls to corridor path; corridor WP list `[anchor, 150m, 300m, ...]` — but 150m < 360m required → corridor still rejects. This is Layer 2+ surface; document and defer per scope decision.

## 4. Out of Scope (deferred to follow-up D-tasks)

- **Layer 1 NLP IPOPT INFEASIBLE**: structural solver/constraint feasibility. Requires row-class residual telemetry before fix decision. Codex diagnosis Q2 🟡 Medium.
- **Layer 2 TailBuilder m6_not_past_clear**: design contract on M6 ONSET vs ACTIVE. Requires spec §13.4 clarification. Codex diagnosis Q3 🟢/🟡.
- **Corridor first-distance threshold**: `kDistancesM[0]=150m` vs `emergency_wheel_over_distance_m=360m` mismatch. After anchor contract, corridor WP[1]=150m still fails preflight. This may need corridor kDistancesM adjustment OR separate corridor preflight threshold. Defer until L3 fix lands and probe shows real corridor behavior.

## 5. Architecture Invariants Preserved

- **M7 doer-checker independence**: no M7 code touched.
- **ODD sole safety authority**: no ODD/behavior switching logic touched.
- **CMM semantics**: `current_state()`/`rationale()`/`forecast()` semantics preserved.
- **Vessel-agnostic**: no `if vessel ==` branches.
- **COLREGs full-chain**: fix is at M5 WP generation layer, no threshold/scenario/geometry tuning.

## 6. References

- v2.2 spec: `docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`
- Codex diagnosis: mempalace drawer `drawer_MASS-L3_colregs-deviation-findings_5122d56e92061c71a089fe28` (2026-07-04)
- Probe evidence: `runs/v2.2_root_cause_rule14ho/probe_20260704_230815.json`
- Code coordinates:
  - `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:171-189` (generator WP[0])
  - `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:986-1005` (fallback WP[0])
  - `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:176,287` (corridor kDistancesM)
  - `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:208-274` (preflight)
