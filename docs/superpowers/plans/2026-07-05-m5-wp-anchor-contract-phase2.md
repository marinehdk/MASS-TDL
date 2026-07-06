# M5 WP Anchor Contract (Phase 2) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Promote WP anchor contract from deferred to primary. Unify all M5 avoidance WP lists to `[anchor, maneuver_1, ..., maneuver_N]` and teach preflight to skip the anchor. Breaks the optimized-path deadloop confirmed by V2.3 probe.

**Architecture:** (1) Two corridor generators prepend `0.0` (anchor distance) to `kDistancesM`. (2) `validate_gnc_avoidance_plan` + `validate_canonical_route_for_gnc` add `bool wps_has_anchor=false`. When true, skip wps[0] for first_distance/segment/turn_radius/XTE checks. (3) 4 of 5 preflight callers pass `has_anchor=true`; return-path caller keeps `false`. Optimized/fallback generators already conform (no change).

**Tech Stack:** C++17, ament_cmake, gtest, ROS2 Humble, colcon.

**Spec:** `docs/superpowers/specs/2026-07-05-m5-preflight-threshold-calibration-design.md` §3 (commit `f855de55`)

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
**Branch:** `codex/colregs-12probe-debug`
**Base HEAD:** `f855de55` (on top of `8376f214` Phase 1 test + `a97c959b` Phase 1 calibration)

---

## File Structure

- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:176,287` — corridor `kDistancesM` prepend 0.0
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:208-272` — add `has_anchor` param + skip-wps[0] logic
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:88-91` — wrapper `validate_canonical_route_for_gnc` add `has_anchor` param + forward
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:1530,1662,1761,1827` — 4 callers pass `has_anchor=true` (return path keeps default false)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:94-139` — `append_l2_nominal_suffix_if_preflight_feasible` wrapper + internal `validate_canonical_route_for_gnc` call pass `has_anchor=true`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp` — new tests + update corridor tests for prepended anchor
- No new files.

---

## Task 1: Corridor generators prepend anchor (0.0)

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:176` (`generate_stable_avoidance_corridor_waypoints`)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp:287` (`generate_rule13_overtake_corridor_waypoints`)

- [ ] **Step 1: Read `generate_stable_avoidance_corridor_waypoints` kDistancesM**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
sed -n '175,180p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp
```
Expected:
```cpp
  static const std::vector<double> kDistancesM = {
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};
```

- [ ] **Step 2: Prepend 0.0 to stable corridor kDistancesM**

Edit old_string:
```cpp
  static const std::vector<double> kDistancesM = {
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};
```
New:
```cpp
  // Phase 2 anchor contract (spec §3.2): kDistancesM[0]=0.0 → wps[0]=anchor
  // (own ship position at plan generation). Preflight skips wps[0] via
  // has_anchor=true. Old first maneuver at 150m becomes wps[1].
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};
```

- [ ] **Step 3: Read `generate_rule13_overtake_corridor_waypoints` kDistancesM**

```bash
sed -n '286,290p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp
```
Expected:
```cpp
  static const std::vector<double> kDistancesM = {
      600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
      8400.0, 10000.0, 12000.0};
```

- [ ] **Step 4: Prepend 0.0 to rule13 overtake kDistancesM**

Edit old_string:
```cpp
  static const std::vector<double> kDistancesM = {
      600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
      8400.0, 10000.0, 12000.0};
```
New:
```cpp
  // Phase 2 anchor contract (spec §3.2): kDistancesM[0]=0.0 → wps[0]=anchor.
  // Preflight skips wps[0] via has_anchor=true.
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor
      600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
      8400.0, 10000.0, 12000.0};
```

- [ ] **Step 5: Verify both edits**

```bash
grep -B1 -A3 "anchor" src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp | head -20
```
Expected: two `0.0,  // anchor` lines in kDistancesM vectors.

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp
git commit -m "feat(m5): corridor generators prepend anchor (0.0) to kDistancesM

Phase 2 anchor contract (spec §3.2). generate_stable_avoidance_corridor_waypoints
and generate_rule13_overtake_corridor_waypoints now emit wps[0]=anchor (own ship
position) before the maneuver targets. Preflight has_anchor=true (next task)
will skip wps[0] for first_distance/segment/turn_radius checks.

Old first maneuver (150m / 600m) becomes wps[1]. Lateral cap formula yields
lateral_abs=0 at d=0 → no dogleg at anchor."
```

---

## Task 2: Preflight `has_anchor` parameter

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:208-272`

- [ ] **Step 1: Read full `validate_gnc_avoidance_plan` function**

```bash
sed -n '208,275p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
```
Confirm current signature + body. Note line numbers: 213 (size check), 220-234 (first_distance), 236-248 (XTE), 250-262 (segment loop), 264-272 (turn_radius).

- [ ] **Step 2: Add `has_anchor` parameter to signature**

Edit old_string (line 208-213):
```cpp
inline GncAvoidancePreflightResult validate_gnc_avoidance_plan(
    const WaypointLatLon& origin,
    const std::vector<WaypointLatLon>& wps,
    const std::vector<double>& speeds,
    const GncAvoidancePreflightConfig& cfg = {}) {
  if (wps.size() < 2U) {
    return {false, "invalid_avoidance_route", 0U, 2.0, static_cast<double>(wps.size())};
  }
```
New:
```cpp
inline GncAvoidancePreflightResult validate_gnc_avoidance_plan(
    const WaypointLatLon& origin,
    const std::vector<WaypointLatLon>& wps,
    const std::vector<double>& speeds,
    const GncAvoidancePreflightConfig& cfg = {},
    bool wps_has_anchor = false) {
  // Phase 2 anchor contract (spec §3.3): when wps_has_anchor=true, wps[0] is
  // the own ship anchor. Skip it for first_distance/segment/turn_radius/XTE
  // checks. Effective maneuver list is wps[anchor_offset..end].
  const std::size_t anchor_offset = wps_has_anchor ? 1U : 0U;
  if (wps.size() < 2U + anchor_offset) {
    return {false, "invalid_avoidance_route", 0U, 2.0, static_cast<double>(wps.size())};
  }
```

- [ ] **Step 3: Update first_distance check to skip anchor**

Edit old_string (line 220-234):
```cpp
  const double first_speed = speed_at_or_default(speeds, 0U, cfg);
  const bool high_speed_flyby =
      first_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6;
  const double first_required = high_speed_flyby
      ? cfg.high_speed_flyby_min_segment_m
      : cfg.emergency_wheel_over_distance_m;
  const double first_distance = gnc_distance_m(origin, wps.front(), origin.lat);
  if (first_distance + 1.0e-6 < first_required) {
    return {
        false,
        "first_maneuver_point_too_close",
        0U,
        first_required,
        first_distance};
  }
```
New:
```cpp
  // Phase 2: when has_anchor, first maneuver is wps[anchor_offset] (skip wps[0]).
  const std::size_t first_idx = anchor_offset;
  const double first_speed = speed_at_or_default(speeds, first_idx, cfg);
  const bool high_speed_flyby =
      first_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6;
  const double first_required = high_speed_flyby
      ? cfg.high_speed_flyby_min_segment_m
      : cfg.emergency_wheel_over_distance_m;
  const double first_distance = gnc_distance_m(origin, wps[first_idx], origin.lat);
  if (first_distance + 1.0e-6 < first_required) {
    return {
        false,
        "first_maneuver_point_too_close",
        first_idx,
        first_required,
        first_distance};
  }
```

- [ ] **Step 4: Update XTE check to skip anchor**

Edit old_string (line 236-248):
```cpp
  const double ref_lat = wps.front().lat;
  if (high_speed_flyby && wps.size() >= 2U) {
    const double initial_raw_xte =
        gnc_cross_track_to_segment_m(origin, wps[0U], wps[1U], ref_lat);
    if (initial_raw_xte > cfg.raw_route_rejoin_threshold_m + 1.0e-6) {
      return {
          false,
          "initial_raw_route_xte_too_large",
          0U,
          cfg.raw_route_rejoin_threshold_m,
          initial_raw_xte};
    }
  }
```
New:
```cpp
  // Phase 2: XTE segment uses wps[first_idx] and wps[first_idx+1] (skip anchor).
  const double ref_lat = wps[first_idx].lat;
  if (high_speed_flyby && wps.size() >= first_idx + 2U) {
    const double initial_raw_xte =
        gnc_cross_track_to_segment_m(origin, wps[first_idx], wps[first_idx + 1U], ref_lat);
    if (initial_raw_xte > cfg.raw_route_rejoin_threshold_m + 1.0e-6) {
      return {
          false,
          "initial_raw_route_xte_too_large",
          first_idx,
          cfg.raw_route_rejoin_threshold_m,
          initial_raw_xte};
    }
  }
```

- [ ] **Step 5: Update segment loop to skip anchor**

Edit old_string (line 250-262):
```cpp
  for (std::size_t i = 0U; i + 1U < wps.size(); ++i) {
    const double segment = gnc_distance_m(wps[i], wps[i + 1U], ref_lat);
    if (segment + 1.0e-6 < cfg.emergency_min_segment_length_m) {
      return {false, "segment_too_short", i, cfg.emergency_min_segment_length_m, segment};
    }
    const double segment_speed = std::max(
        speed_at_or_default(speeds, i, cfg),
        speed_at_or_default(speeds, i + 1U, cfg));
    if (segment_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6 &&
        segment + 1.0e-6 < cfg.high_speed_flyby_min_segment_m) {
      return {false, "flyby_segment_too_short", i, cfg.high_speed_flyby_min_segment_m, segment};
    }
  }
```
New:
```cpp
  // Phase 2: segment loop starts at first_idx (skip anchor if present).
  for (std::size_t i = first_idx; i + 1U < wps.size(); ++i) {
    const double segment = gnc_distance_m(wps[i], wps[i + 1U], ref_lat);
    if (segment + 1.0e-6 < cfg.emergency_min_segment_length_m) {
      return {false, "segment_too_short", i, cfg.emergency_min_segment_length_m, segment};
    }
    const double segment_speed = std::max(
        speed_at_or_default(speeds, i, cfg),
        speed_at_or_default(speeds, i + 1U, cfg));
    if (segment_speed > cfg.emergency_guidance_speed_cap_mps + 1.0e-6 &&
        segment + 1.0e-6 < cfg.high_speed_flyby_min_segment_m) {
      return {false, "flyby_segment_too_short", i, cfg.high_speed_flyby_min_segment_m, segment};
    }
  }
```

- [ ] **Step 6: Update turn_radius check to skip anchor**

Read current (line 264-272+):
```bash
sed -n '264,280p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
```
Then edit the turn_radius block. Old:
```cpp
  if (wps.size() >= 2U) {
    const double available =
        gnc_available_turn_radius_m(origin, wps.front(), wps[1U], ref_lat);
    const double required = required_turn_radius_m(first_speed, cfg);
    if (available + 1.0e-6 < required) {
      return {false, "first_turn_radius_too_small", 0U, required, available};
```
New:
```cpp
  // Phase 2: turn_radius uses wps[first_idx] and wps[first_idx+1] (skip anchor).
  if (wps.size() >= first_idx + 2U) {
    const double available =
        gnc_available_turn_radius_m(origin, wps[first_idx], wps[first_idx + 1U], ref_lat);
    const double required = required_turn_radius_m(first_speed, cfg);
    if (available + 1.0e-6 < required) {
      return {false, "first_turn_radius_too_small", first_idx, required, available};
```

- [ ] **Step 7: Verify full function reads correctly**

```bash
sed -n '208,290p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
```
Check: `wps_has_anchor` param present; `anchor_offset` const used in size check, first_idx, segment loop, XTE, turn_radius.

- [ ] **Step 8: Commit (no build yet — Task 5 builds after all callers updated)**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
git commit -m "feat(m5): preflight has_anchor param skips wps[0] anchor

Phase 2 anchor contract (spec §3.3). validate_gnc_avoidance_plan gains
bool wps_has_anchor=false (backward compat). When true:
- size check requires >= 2+anchor_offset elements
- first_distance uses wps[anchor_offset] not wps.front()
- XTE segment uses wps[first_idx], wps[first_idx+1]
- segment loop starts at first_idx
- turn_radius uses wps[first_idx], wps[first_idx+1]

Default false preserves existing behavior. Callers updated in next task."
```

---

## Task 3: Wrapper `validate_canonical_route_for_gnc` forward `has_anchor`

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:88-91`

- [ ] **Step 1: Read current wrapper**

```bash
sed -n '88,92p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp
```
Expected:
```cpp
GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin) {
  return validate_gnc_avoidance_plan(origin, route_waypoints(plan), plan.command_speed_mps);
}
```

- [ ] **Step 2: Add has_anchor param + forward**

Edit old:
```cpp
GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin) {
  return validate_gnc_avoidance_plan(origin, route_waypoints(plan), plan.command_speed_mps);
}
```
New:
```cpp
GncAvoidancePreflightResult validate_canonical_route_for_gnc(
    const l3_msgs::msg::AvoidancePlan& plan,
    const WaypointLatLon& origin,
    bool wps_has_anchor = false) {
  return validate_gnc_avoidance_plan(
      origin, route_waypoints(plan), plan.command_speed_mps,
      GncAvoidancePreflightConfig{}, wps_has_anchor);
}
```

- [ ] **Step 3: Update L2 suffix wrapper call (same file, line ~133)**

```bash
sed -n '130,140p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp
```
Find `const auto result = validate_canonical_route_for_gnc(candidate, origin);` and edit:
Old:
```cpp
  const auto result = validate_canonical_route_for_gnc(candidate, origin);
```
New:
```cpp
  // Phase 2: candidate = plan + L2 suffix; plan.waypoints[0] is anchor.
  const auto result = validate_canonical_route_for_gnc(candidate, origin, /*wps_has_anchor=*/true);
```

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp
git commit -m "feat(m5): validate_canonical_route_for_gnc forwards has_anchor

Phase 2 anchor contract (spec §3.5). Wrapper gains bool wps_has_anchor=false,
forwards to validate_gnc_avoidance_plan. L2 suffix feasibility check
(append_l2_nominal_suffix_if_preflight_feasible) now passes has_anchor=true
since candidate plan starts with generator-emitted anchor."
```

---

## Task 4: Callers pass `has_anchor=true` (except return path)

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:1530,1662,1761,1827`

- [ ] **Step 1: Optimized path (line 1530) — via wrapper**

```bash
sed -n '1528,1532p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```
Old:
```cpp
    const auto preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg});
```
New:
```cpp
    // Phase 2: plan.waypoints[0] is anchor (MidMpcWaypointGenerator).
    const auto preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg}, /*wps_has_anchor=*/true);
```

- [ ] **Step 2: Corridor path (line 1662) — direct validate_gnc_avoidance_plan**

```bash
sed -n '1660,1667p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```
Old:
```cpp
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds);
```
New:
```cpp
    // Phase 2: corridor wps[0] is now anchor (kDistancesM[0]=0.0).
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds,
        mass_l3::m5::GncAvoidancePreflightConfig{}, /*wps_has_anchor=*/true);
```

- [ ] **Step 3: Return path (line 1761) — KEEP has_anchor=false (exception)**

```bash
sed -n '1759,1764p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```
Confirm this caller uses `generate_return_to_route_waypoints` output (WP[0]=500m real maneuver, NOT anchor). **Do not edit** — leave default false. Add clarifying comment if no comment exists.

Old (only if no Phase 2 comment present):
```cpp
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds);
```
New:
```cpp
    // Phase 2 exception: return path WP[0] is a real 500m maneuver target
    // (generate_return_to_route_waypoints), NOT an anchor. Keep has_anchor=false.
    const auto preflight = mass_l3::m5::validate_gnc_avoidance_plan(
        {anchor.lat_deg, anchor.lon_deg}, wps, speeds);
```

- [ ] **Step 4: Full route path (line 1827) — via wrapper**

```bash
sed -n '1825,1830p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```
Old:
```cpp
    const auto full_preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg});
```
New:
```cpp
    // Phase 2: plan.waypoints[0] is anchor.
    const auto full_preflight = validate_canonical_route_for_gnc(plan, {lat0_deg, lon0_deg}, /*wps_has_anchor=*/true);
```

- [ ] **Step 5: Verify all 4 caller edits + return-path exception**

```bash
grep -n "wps_has_anchor\|has_anchor\|Phase 2.*anchor\|Phase 2 exception" src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp | head -10
```
Expected: 3 `has_anchor=true` (optimized, corridor, full route) + 1 Phase 2 exception comment (return path).

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "feat(m5): 4 preflight callers pass has_anchor (return path exception)

Phase 2 anchor contract (spec §3.4). Three callers pass has_anchor=true:
- optimized path (mid_mpc_node.cpp:1530, plan.waypoints[0]=anchor)
- degraded corridor (mid_mpc_node.cpp:1662, corridor kDistancesM[0]=0.0)
- full route (mid_mpc_node.cpp:1827, plan.waypoints[0]=anchor)

Return path (mid_mpc_node.cpp:1761) is the sole exception: keeps
has_anchor=false (default) because generate_return_to_route_waypoints
WP[0] is a real 500m maneuver target, not an anchor. Clarifying comment added."
```

---

## Task 5: Update unit tests for anchor contract

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp`

- [ ] **Step 1: Find existing corridor tests that assert wps.size() or wps[0] distance**

```bash
grep -n "generate_stable_avoidance_corridor\|generate_rule13_overtake_corridor\|kDistancesM\|wps.size\|wps.front\|wps\[0\]" src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp | head -20
```

- [ ] **Step 2: Update tests that hard-coded old kDistancesM size (11) → new size (12)**

For each test that asserts `wps.size() == 11` or similar on corridor generators, update to `wps.size() == 12` (anchor added). For tests asserting `wps[0]` distance 150m/600m, update to assert `wps[0]` = anchor lat/lon (within 1m) and `wps[1]` at 150m/600m.

- [ ] **Step 3: Add new Phase 2 anchor-contract tests**

Append to the test file (after existing corridor tests):

```cpp
// Phase 2 anchor contract: corridor generators emit wps[0]=anchor.
TEST(AvoidanceWaypointGen, StableCorridorEmitsAnchorFirst) {
  const mass_l3::m5::WaypointLatLon anchor{63.44, 10.38};
  const auto wps = generate_stable_avoidance_corridor_waypoints(
      45.0, 90.0, anchor.lat, anchor.lon, 0.0);
  ASSERT_GE(wps.size(), 2u);
  // wps[0] = anchor (own ship position)
  EXPECT_LT(mass_l3::m5::distance_m(anchor, wps[0], anchor.lat), 1.0);
  // wps[1] = first real maneuver target at 150m
  EXPECT_NEAR(mass_l3::m5::distance_m(anchor, wps[1], anchor.lat), 150.0, 5.0);
}

TEST(AvoidanceWaypointGen, Rule13OvertakeCorridorEmitsAnchorFirst) {
  const mass_l3::m5::WaypointLatLon anchor{63.44, 10.38};
  const auto wps = generate_rule13_overtake_corridor_waypoints(
      49.0, 79.0, anchor.lat, anchor.lon, 0.0);
  ASSERT_GE(wps.size(), 2u);
  EXPECT_LT(mass_l3::m5::distance_m(anchor, wps[0], anchor.lat), 1.0);
  EXPECT_NEAR(mass_l3::m5::distance_m(anchor, wps[1], anchor.lat), 600.0, 5.0);
}

// Phase 2: has_anchor=true skips wps[0] in preflight.
TEST(AvoidanceWaypointGen, PreflightHasAnchorSkipsFirstWaypoint) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const double lon_50m = 50.0 / kMetersPerDegLat;
  // wps[0]=anchor at origin, wps[1] at 50m (would fail if not skipped)
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {0.0, 0.0},  // anchor
      {0.0, lon_50m * 4.0},  // 200m maneuver (≥120m required)
      {0.0, lon_50m * 8.0},  // 400m
  };
  const auto result = validate_gnc_avoidance_plan(
      origin, wps, {3.2, 3.2, 3.2}, mass_l3::m5::GncAvoidancePreflightConfig{},
      /*wps_has_anchor=*/true);
  EXPECT_TRUE(result.feasible) << result.reason;
}

TEST(AvoidanceWaypointGen, PreflightHasAnchorStillRejectsCloseManeuver) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const double lon_50m = 50.0 / kMetersPerDegLat;
  // wps[0]=anchor, wps[1] at 50m (< 120m required) → still reject
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {0.0, 0.0},  // anchor
      {0.0, lon_50m},  // 50m maneuver (< 120m)
      {0.0, lon_50m * 4.0},  // 200m
  };
  const auto result = validate_gnc_avoidance_plan(
      origin, wps, {3.2, 3.2, 3.2}, mass_l3::m5::GncAvoidancePreflightConfig{},
      /*wps_has_anchor=*/true);
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "first_maneuver_point_too_close");
}
```

- [ ] **Step 4: Commit test updates**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp
git commit -m "test(m5): Phase 2 anchor contract tests + corridor size updates

- Update existing corridor tests for prepended anchor (kDistancesM size 11→12)
- Add StableCorridorEmitsAnchorFirst / Rule13OvertakeCorridorEmitsAnchorFirst
- Add PreflightHasAnchorSkipsFirstWaypoint (has_anchor=true passes when wps[1]>=120m)
- Add PreflightHasAnchorStillRejectsCloseManeuver (has_anchor=true rejects wps[1]<120m)"
```

---

## Task 6: Build + run M5 unit tests + full regression

**Files:** none (verification)

- [ ] **Step 1: Build M5 with tests**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner \
     --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake \
     --event-handlers console_direct+ 2>&1" | tail -15
```
Expected: `Finished <<< m5_tactical_planner`, no errors (warnings OK).

- [ ] **Step 2: Run preflight + waypoint_gen tests**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner \
     --ctest-args -R 'test_gnc_preflight|test_avoidance_waypoint_gen' 2>&1" | tail -15
```
Expected: all PASS including new Phase 2 tests.

- [ ] **Step 3: Full M5 regression**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner 2>&1" | tail -5
```
Expected: 0 failures (baseline ~360 enabled tests).

- [ ] **Step 4: Rebuild runtime (BUILD_TESTING=OFF) + force clean to ensure binary fresh**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "rm -rf /opt/ws/build/m5_tactical_planner /opt/ws/install/m5_tactical_planner && \
   source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner \
     --cmake-args -DBUILD_TESTING=OFF -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake 2>&1" | tail -5
```

- [ ] **Step 5: Restart sil-nodes + verify m5 process**

```bash
docker restart codex-gnc-validation-sil-nodes-1
sleep 18
docker exec codex-gnc-validation-sil-nodes-1 bash -c "ps aux | grep m5_mid_mpc_node | grep -v grep | awk '{print \$2, \$9}'" | head -2
```

---

## Task 7: Rerun V2 probe (Phase 1 + Phase 2)

**Files:** none (verification)

- [ ] **Step 1: Run rule14-ho probe**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
mkdir -p runs/v2.3_phase2_rule14ho
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --profile gnc \
  --sim-rate 5 \
  --restart-between-runs \
  --summary-out runs/v2.3_phase2_rule14ho/probe_$(date +%Y%m%d_%H%M%S).json \
  2>&1 | tail -40
```

- [ ] **Step 2: Verify preflight behavior change**

```bash
docker logs codex-gnc-validation-sil-nodes-1 --since <probe-start>Z 2>&1 | \
  grep -E "GNCPreflight|AvoidancePlan.*publish|CommittedRoute|DegradedCandidate" | \
  grep -v "scenario_loaded\|HB\|SAT pub\|WS pub" | head -20
```
Expected:
- No more `first_maneuver_point_too_close available=0.0` on `m5-midmpc-*` plans (optimized path now skips anchor)
- `[M5][AvoidancePlan] publish reason=... points=>0` (non-empty plan published)
- Or `[M5][CommittedRoute]` accept (optimized candidate accepted)

- [ ] **Step 3: Save evidence + check CPA**

```bash
cp runs/trace_current.jsonl runs/v2.3_phase2_rule14ho/trace_$(date +%Y%m%d_%H%M%S).jsonl
cat runs/v2.3_phase2_rule14ho/probe_*.json | python3 -m json.tool | grep -E "min_cpa|solver_stats|overall" | head -10
```

Expected signal (vs V2.3 Phase-1-only baseline CPA 1.3m):
- CPA min should improve (corridor or optimized path now publishes real avoidance WP)
- solver_stats may show CONVERGED if NLP path activates successfully

---

## Self-Review

**1. Spec coverage:**
- §3.1 contract → Task 1 (generators) + Task 2 (preflight) ✅
- §3.2 generator changes → Task 1 ✅
- §3.3 preflight changes → Task 2 ✅
- §3.4 caller classification → Task 4 ✅
- §3.5 compound caller → Task 3 ✅
- §4.2 Phase 2 tests → Task 5 ✅
- §4.3 V2 probe → Task 7 ✅

**2. Placeholder scan:** all steps have exact code. Task 5 Step 2 says "update tests that hard-coded size 11→12" — this is intentionally scan-driven because the exact tests depend on what exists; the step before (Step 1 grep) surfaces them.

**3. Type consistency:** `wps_has_anchor` parameter name consistent across spec/plan/code. `anchor_offset`, `first_idx` introduced in Task 2 used consistently in Task 4 caller expectations.

**4. Risk:** Task 4 Step 3 (return path exception) is the最容易出错 — if the caller is misidentified and actually emits anchor, has_anchor=false would misreject. Plan instructs to verify via `generate_return_to_route_waypoints` output before leaving default.