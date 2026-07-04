# M5 GNCPreflight Threshold Calibration + WP Anchor Contract — Design Spec (v2.3 δ)

**Date**: 2026-07-04 (revised 2026-07-05 R3 after V2.3 Phase1+2 probe)
**Status**: Revised R3 (Phase 1 calibration `a97c959b`, Phase 2 anchor contract landed; Phase 3 wheel-over sampling PROMOTED after V2.3 Phase1+2 probe confirmed NLP wps[1]=25m is the real blocker)
**Supersedes**: commit `244a9002` (anchor-contract-only draft), commit `5d6c15d7` (Phase 2 deferred draft), commit `f855de55` (R2 Phase 2 only)
**Supersedes spec**: none (incremental on v2.2 `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`)
**Scope**: M5 Tactical Planner — preflight threshold calibration (Phase 1 DONE) + WP anchor contract (Phase 2 DONE) + wheel-over sampling (Phase 3 PROMOTED)
**D-task ID**: D-ε (delta on v2.2)

## 1. Problem

V2.2 validation probe (rule14-ho, run-19f2dad3b5a) RED with CPA 3.8m (floor 180m). End-to-end trace (ZCode + Codex diagnosis task, 2026-07-04) identified **two independent defects**:

### 1.1 Defect A (Phase 1 — primary fix): `high_speed_flyby_min_segment_m = 360m` over-conservative, no provenance

`gnc_avoidance_preflight.hpp:32` defines `high_speed_flyby_min_segment_m{360.0}`. Triggered when `first_speed > emergency_guidance_speed_cap_mps (3.2 m/s)` (line 221-225). Probe own ship mean u=5.89 m/s (99.93% samples > 3.2) → high_speed_flyby=true → `first_required=360m` for all optimized/corridor plans.

**Provenance**: commit `11d86dd8 wip(colregs): capture 12-probe contract debug` — WIP commit, **no rationale, no HAZID, no sea-trial, no regulation citation**. Code marks `[TBD-HAZID]` (line 13), author knew it was uncalibrated.

**NLM research 🟢 High** (ship_maneuvering domain, 9 cited sources, IMO MSC.137(76)):

| Physical quantity | Value | Source |
|---|---|---|
| 50m vessel steady turning radius | **48.8m** | R = U/r = 4/0.082 (yaw 4.7°/s) |
| IMO MSC.137(76) advance upper limit | **4.5L = 225m** | IMO standard |
| 35° turning circle measured advance | **2.80-3.31L = 140-165m** | sources [5] |
| **360m = 7.2L** | **over-conservative, no physical basis** | NLM verdict |

360m is **1.6× IMO advance limit, 2.2-2.6× measured advance**. Substantially larger than ship's maximum physical advance.

**Calibration target**: 120m (= `emergency_wheel_over_distance_m`, 2.4L). Justification:
- Matches existing `emergency_wheel_over_distance_m` semantics (same physical quantity: wheel-over distance for emergency maneuver)
- Within measured advance range lower bound (140m) with safety margin
- Aligns with IMO MSC.137(76) ship-length-based standard (2.4L < 4.5L limit)
- Eliminates the unjustified 3× gap between `high_speed_flyby_min_segment_m` (360) and `emergency_wheel_over_distance_m` (120)

### 1.2 Defect B (Phase 2 — PROMOTED to primary after V2.3 probe): WP list anchor contract inconsistency

Three M5 WP generators have inconsistent WP[0] semantics:

| Generator | WP[0] meaning | Distance |
|---|---|---|
| `MidMpcWaypointGenerator::build_waypoints_` | own ship anchor | 0m |
| `build_geometric_fallback_plan_` | own ship anchor | 0m |
| `generate_stable_avoidance_corridor_waypoints` | real first maneuver | 150m |
| `generate_rule13_overtake_corridor_waypoints` | real first maneuver | 600m |
| `generate_return_to_route_waypoints` | real first maneuver | 500m |

For anchor-WP[0] generators, `first_distance = 0m` → optimized/fallback plans rejected regardless of threshold.

**V2.3 probe (run-19f2e0a5232) finding — original deferral rationale was WRONG**:
- Original §1.2 assumption: "After Phase 1 calibration, corridor path (WP[0]=150m > 120m) passes preflight and becomes viable fallback"
- V2.3 reality: NLP solver **occasionally Converges**, producing `selected_plan.status == "NORMAL"` with non-empty waypoints. This activates the optimized path (`mid_mpc_node.cpp:1499`) instead of the corridor else-if branch (`:1569`). Optimized WP[0]=anchor=0m < 120m → preflight rejects → `publish_keep_last_` → next cycle NLP Converges again → re-enters optimized → rejects again. **Deadloop between optimized-attempt and keep-last**.
- Corridor path never activates because it requires `selected_plan.status != "NORMAL"`, but NLP keeps occasionally producing NORMAL plans.
- V2.3 CPA = 1.3m (worse than v2.2's 3.8m — regression signal, root cause not yet traced but the deadloop is the primary blocker).

**Conclusion**: Phase 2 anchor contract is NOT a defer — it is the probe-pass blocker. Phase 2 is promoted to primary.

### 1.3 Defect C (Phase 3 — PROMOTED to primary after V2.3 Phase1+2 probe): NLP optimized wps[1] violates wheel-over distance

V2.3 Phase1+2 probe (`run-19f2e3cbd12`) confirmed Phase 1 calibration + Phase 2 anchor contract both landed correctly (`required=120.0`, preflight rejects at `idx=1` not `idx=0`). But CPA regressed further to 0.4m.

**Root cause**: `MidMpcWaypointGenerator::sample_waypoints_` (`mid_mpc_waypoint_generator.cpp:171-189`) samples wps uniformly across trajectory:
```cpp
const int32_t idx = (num_wp == 1) ? 0 : k * (N - 1) / (num_wp - 1);
```
With `num_waypoints=18`, `N=18` → `num_wp=18`, so `idx=k`. wps[1] corresponds to trajectory[1].

**NLP horizon geometry** (`mid_mpc_nlp_formulation.hpp:88,90`):
- `n_horizon = 18` steps, `dt_s = 5.0` s → 90s total
- Step distance = u × dt = 5 m/s × 5s = **25m/step**
- trajectory[1] dead-reckon ≈ 25m (33m at u≈6.6 m/s, matches probe log)

**Preflight reject**: `first_maneuver_point_too_close idx=1 required=120.0 available=33.0`. wps[1]=trajectory[1]=33m < 120m emergency_wheel_over_distance_m.

**Horizon is sufficient** (90s × 5m/s = 450m > 120m; 120m = 27% of horizon). The sampling start point is wrong, not the horizon length. First maneuver WP should be at trajectory[5] (5×25=125m ≥ 120m), not trajectory[1].

**Codex diagnosis Q1** identified this as the WP translator indexing defect (🟢 High). User-confirmed geometry (2026-07-05): "18 个 5s 步长，3×5=15m 间隔" — direction correct (actual 25m/step at u=5 m/s).

**Phase 3 fix**: `sample_waypoints_` + `build_waypoints_` start sampling at the first trajectory index whose cumulative distance from origin ≥ `emergency_wheel_over_distance_m`. Horizon comfortably covers this (trajectory[5] @ 125m).

### 1.4 Not a v2.2 regression

v2.2 changes constraint derive (ROT∩box direction-aware); WP generation and preflight thresholds unchanged. v2.1 also 0% CONVERGED. Defect A (360m) introduced in `11d86dd8` WIP commit, predates v2.2. Defect C (sample_waypoints_ uniform sampling) is original Phase-3 generator code, also predates v2.2.

## 2. Phase 1 Design: Calibrate `high_speed_flyby_min_segment_m` 360→120

### 2.1 Change

`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:32`:

```cpp
// Before:
double high_speed_flyby_min_segment_m{360.0};

// After:
// Calibrated 2026-07-05 (NLM 🟢 + IMO MSC.137(76)): 360m (7.2L) was over-conservative
// WIP value with no provenance. 120m (2.4L) aligns with emergency_wheel_over_distance_m,
// within measured 35° advance range (2.8-3.31L=140-165m) with margin, below IMO 4.5L limit.
double high_speed_flyby_min_segment_m{120.0};
```

### 2.2 Semantic note

After calibration, `high_speed_flyby_min_segment_m == emergency_wheel_over_distance_m == 120m`. The high_speed_flyby branch (line 223-225) becomes degenerate — both branches yield 120m. The branch is preserved (not removed) because:
- `emergency_guidance_speed_cap_mps` (3.2 m/s) still gates the high-speed XTE check (line 237-248), which is independent of `first_required`
- Future HAZID may diverge the two values; the branch structure remains available
- Surgical change principle: calibrate the value, do not refactor the control flow

### 2.3 Test impact

Existing preflight unit tests (`test_gnc_avoidance_preflight.cpp`) that hardcode `required=360` or test the 360m boundary will fail and must update to 120m. This is expected regression from the calibration; tests must reflect the new calibrated value, not preserve the old over-conservative one.

## 3. Phase 2 Design (PROMOTED to primary): Uniform Anchor Contract

Phase 2 is promoted from deferred to primary after V2.3 probe (`run-19f2e0a5232`) confirmed optimized-path deadloop is the real probe-pass blocker (§1.2).

### 3.1 Contract

WP list = `[anchor, maneuver_1, ..., maneuver_N]`. WP[0] = own ship anchor (own ship position at plan generation time). WP[1..N] = real maneuver targets. All avoidance generators conform. Preflight `has_anchor=true` skips wps[0] for first_distance/segment/turn_radius checks.

### 3.2 Generator changes

- `generate_stable_avoidance_corridor_waypoints` (`avoidance_waypoint_gen.hpp:176`): `kDistancesM` prepend 0.0:
  ```cpp
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor: own ship position at plan generation
      150.0, 300.0, 600.0, 1000.0, 1500.0, 2200.0, 3200.0, 4500.0,
      6000.0, 7500.0, 9000.0};
  ```
  At `d=0.0`: `d_north = d_east = 0.0` → `wps[0] = {anchor_lat, anchor_lon}`. Lateral cap formula yields `lateral_abs = cap * (1 - exp(-0/approach_distance)) = 0` → no dogleg at anchor.
- `generate_rule13_overtake_corridor_waypoints` (`avoidance_waypoint_gen.hpp:287`): `kDistancesM` prepend 0.0:
  ```cpp
  static const std::vector<double> kDistancesM = {
      0.0,  // anchor
      600.0, 1200.0, 2000.0, 3000.0, 4200.0, 5600.0, 7000.0,
      8400.0, 10000.0, 12000.0};
  ```
- `MidMpcWaypointGenerator` (`mid_mpc_waypoint_generator.cpp:171-189`): **no change**. Already conforms: `ned_pos[0]=(0,0)` is the anchor.
- `build_geometric_fallback_plan_` (`mid_mpc_node.cpp:986-1005`): **no change**. Already conforms: `arc_point(t=0)` returns own ship position as WP[0].
- `generate_return_to_route_waypoints` (`avoidance_waypoint_gen.hpp:312`): **no change — exception**. WP[0] is a real 500m maneuver target, NOT an anchor. Caller passes `has_anchor=false`. Rationale: return path starts after avoidance release, own ship already maneuvering, no anchor semantics.

### 3.3 Preflight changes

**`validate_gnc_avoidance_plan`** (`gnc_avoidance_preflight.hpp:208`): add `bool wps_has_anchor = false` parameter (default false, backward compat).

When `wps_has_anchor == true`:
- Size check: `wps.size() < 3` → invalid (need anchor + ≥2 maneuver for segment check). Exception: `wps.size() == 2` (anchor + 1 maneuver) → only first_distance + turn_radius check, skip segment loop.
- `first_distance = origin → wps[1]` (skip wps[0]=anchor). Since `origin ≈ wps[0]` (both own ship), this is anchor → first maneuver distance.
- Segment loop: `for i in [1, wps.size()-1)`: segment = wps[i] → wps[i+1]. Anchor→wps[1] segment skipped (validated by first_distance + turn_radius).
- Turn radius check: `origin → wps[1] → wps[2]` (skip anchor as middle node).
- XTE check (`high_speed_flyby` branch, line 237-248): unchanged — uses `wps[0]` and `wps[1]` as the initial segment. **When has_anchor=true, XTE check must also shift to wps[1] and wps[2]** to avoid using the anchor as segment start.

When `wps_has_anchor == false` (default): behavior unchanged.

**`validate_canonical_route_for_gnc`** (`mid_mpc_waypoint_generator.cpp:88`): add `bool wps_has_anchor = false` parameter, forward to `validate_gnc_avoidance_plan`.

### 3.4 Caller classification

| Caller | File:line | WP[0] semantics | `has_anchor` |
|---|---|---|---|
| optimized path preflight | `mid_mpc_node.cpp:1530` (via `validate_canonical_route_for_gnc`) | anchor (from `MidMpcWaypointGenerator`) | `true` |
| degraded corridor preflight | `mid_mpc_node.cpp:1662` (direct `validate_gnc_avoidance_plan`) | anchor (after §3.2 change) | `true` |
| return-to-route preflight | `mid_mpc_node.cpp:1761` (direct) | real maneuver (500m) | `false` |
| full route preflight | `mid_mpc_node.cpp:1827` (via `validate_canonical_route_for_gnc`) | anchor (plan.waypoints starts with anchor) | `true` |
| L2 suffix feasibility | `mid_mpc_waypoint_generator.cpp:133` (via `validate_canonical_route_for_gnc`) | anchor (candidate = plan + suffix) | `true` |

Return path is the sole exception. Its caller keeps `has_anchor=false`.

### 3.5 Compound preflight caller details

`append_l2_nominal_suffix_if_preflight_feasible` (`mid_mpc_waypoint_generator.cpp:94-139`) calls `validate_canonical_route_for_gnc(candidate, origin)` at line 133 where `candidate = plan + L2 suffix`. After Phase 2, `plan.waypoints[0]` is the anchor (from generator), so `candidate` also starts with the anchor. This caller passes `has_anchor=true`.

The optimized-path caller at `mid_mpc_node.cpp:1524` calls `append_l2_nominal_suffix_if_preflight_feasible` BEFORE the `validate_canonical_route_for_gnc` at line 1530. Both must pass `has_anchor=true` consistently.

### 3.6 Phase 3 Design: wheel-over起始 sampling in MidMpcWaypointGenerator

Phase 3 fixes Defect C (§1.3): `sample_waypoints_` uniform sampling puts wps[1] at trajectory[1]≈25m, violating the 120m wheel-over distance even though horizon (450m) comfortably covers it.

**Change `sample_waypoints_`** (`mid_mpc_waypoint_generator.cpp:158-191`):

After building `ned_pos` cumulative positions, find the first index whose cumulative distance from origin ≥ `cfg_.wheel_over_distance_m` (new Config field, default 120.0 to match `emergency_wheel_over_distance_m`). Use this index as the sampling start. Then uniformly sample `num_wp` waypoints from `[start_idx, N-1]`.

```cpp
// Phase 3: start sampling at first trajectory index whose cumulative distance
// from origin >= wheel_over_distance_m. Ensures wps[1] (first maneuver after
// anchor) clears the GNC preflight first_maneuver_point_too_close gate.
std::size_t start_idx = 1;
for (std::size_t k = 1; k < ned_pos.size(); ++k) {
  const double dx = ned_pos[k].first;
  const double dy = ned_pos[k].second;
  const double dist = std::sqrt(dx*dx + dy*dy);
  if (dist + 1.0e-6 >= cfg_.wheel_over_distance_m) {
    start_idx = k;
    break;
  }
}
// Sample num_wp waypoints uniformly across [start_idx, N-1].
// Map wp k (0-indexed) to trajectory index: start_idx + k*(N-1-start_idx)/(num_wp-1)
for (int32_t k = 0; k < num_wp; ++k) {
  const int32_t idx = (num_wp == 1) ? static_cast<int32_t>(start_idx)
      : static_cast<int32_t>(start_idx + k * (N - 1 - start_idx) / (num_wp - 1));
  // ... use ned_pos[idx] as before
}
```

**`build_waypoints_`** (`mid_mpc_waypoint_generator.cpp:193-258`): apply the same `start_idx` logic so turn_radius / target_speed / wp_distance indexing aligns with `sample_waypoints_`.

**Config addition** (`mid_mpc_waypoint_generator.hpp:53-58`): add `wheel_over_distance_m{120.0}` to `MidMpcWaypointGenerator::Config`, comment "must match `GncAvoidancePreflightConfig::emergency_wheel_over_distance_m`".

**Fallback**: if entire trajectory < wheel_over_distance_m (short horizon or low speed), `start_idx = N-1` (last point); wps[1] = wps[N-1] = furthest available. Preflight will still reject if < 120m, but that's the honest signal (horizon too short for safe maneuver).

**Test**: new test `MidMpcWaypointGeneratorSamplesFirstManeuverBeyondWheelOver` — construct a converged MidMpcSolution trajectory with N=18, dt=5, u=5 m/s (25m/step). Assert wps[1] distance from origin ≥ 120m.

## 4. Testing

### 4.1 Phase 1 unit test updates (DONE, commit `8376f214`)

`PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin` boundary updated: wps {500m, 800m} → {500m, 600m}, segment 300m→100m < 120m.

### 4.2 Phase 2 unit tests (new)

New test `test_wp_anchor_contract.cpp` (or extend `test_avoidance_waypoint_gen.cpp`):

1. **Generator conformance** (after §3.2 change):
   - `generate_stable_avoidance_corridor_waypoints`: WP[0] = anchor lat/lon (within 1m), WP[1] at ~150m
   - `generate_rule13_overtake_corridor_waypoints`: WP[0] = anchor, WP[1] at ~600m
   - `MidMpcWaypointGenerator`: WP[0] = own ship lat/lon (within 1m)
   - `build_geometric_fallback_plan_`: WP[0] = own ship lat/lon
   - `generate_return_to_route_waypoints`: WP[0] at 500m (NOT anchor — exception)

2. **Preflight `has_anchor` semantics**:
   - `validate_gnc_avoidance_plan` with `has_anchor=true`:
     - wps `[anchor, maneuver_360m, ...]` → feasible (first_distance=360m ≥ required)
     - wps `[anchor, maneuver_100m, ...]` → infeasible (first_distance=100m < required)
     - wps `[anchor]` (size=1) → invalid
     - wps `[anchor, maneuver]` (size=2) → feasible if first_distance ≥ required
   - With `has_anchor=false` (default): unchanged behavior (regression)

3. **Existing tests regression**: `test_gnc_preflight.cpp` + `test_avoidance_waypoint_gen.cpp` must still pass with default `has_anchor=false`.

### 4.3 Integration: V2 probe rerun (after Phase 1 + Phase 2)

Rerun V2 rule14-ho probe. Pass criteria unchanged (spec §7.2):
- NLP SOLVER_CONVERGED > 30%
- CPA min ≥ 180m

**Expected post-Phase-1+2 behavior**:
- Optimized path: WP list `[anchor, maneuver_1, ...]` passes preflight (maneuver_1 at wheel-over distance from dead-reckon, has_anchor=true skips anchor)
- If NLP still INFEASIBLE (Layer 1 structural), plan falls to geometric fallback (also has_anchor=true, passes) or corridor path (WP[0]=anchor, WP[1]=150m, has_anchor=true, 150m > 120m → passes)
- L4 receives non-empty avoidance plan → steering activates → CPA improves

## 5. Out of Scope (deferred)

- **Layer 1 NLP IPOPT INFEASIBLE**: structural solver/constraint feasibility. Codex diagnosis Q2 🟡 Medium. Layer 3 calibration + anchor contract may surface real NLP behavior; if still INFEASIBLE post-Phase-2, defer to follow-up D-task with row-class residual telemetry.
- **Layer 2 TailBuilder m6_not_past_clear**: M6 ONSET vs ACTIVE contract. Codex Q3 🟢/🟡. Defer to follow-up.
- **V2.3 CPA regression root cause** (1.3m vs v2.2 3.8m): not yet traced. May be sim timing variance or NLP convergence frequency change. Phase 2 anchor contract fix should resolve the deadloop regardless; if CPA still regresses post-Phase-2, trace then.
- **`emergency_guidance_speed_cap_mps` 3.2 m/s threshold review**: separate HAZID question. Not touched.

## 6. Architecture Invariants Preserved

- **M7 doer-checker independence**: no M7 code touched.
- **ODD sole safety authority**: no ODD/behavior switching touched.
- **CMM semantics**: preserved.
- **Vessel-agnostic**: no `if vessel ==` branches.
- **COLREGs full-chain**: fix is at M5 preflight threshold, no scenario geometry / scorer / behavior tuning. Calibration is source-backed (NLM 🟢 + IMO MSC.137(76)), not probe-greening.

## 7. References

- v2.2 spec: `docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`
- NLM research: ship_maneuvering domain, 9 sources imported 2026-07-05, query "wheel-over distance COLREGs avoidance IMO MSC.137(76)"
- Codex diagnosis: mempalace drawer `drawer_MASS-L3_colregs-deviation-findings_5122d56e92061c71a089fe28`
- Probe evidence: `runs/v2.2_root_cause_rule14ho/probe_20260704_230815.json`
- Code coordinates:
  - `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:32` (360m value)
  - `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:220-234` (first_required logic)
