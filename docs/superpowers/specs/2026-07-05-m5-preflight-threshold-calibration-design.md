# M5 GNCPreflight Threshold Calibration + WP Anchor Contract — Design Spec (v2.3 δ)

**Date**: 2026-07-04 (revised 2026-07-05)
**Status**: Revised (user-approved 360→120 calibration; anchor contract deferred)
**Supersedes**: commit `244a9002` (anchor-contract-only draft)
**Supersedes spec**: none (incremental on v2.2 `2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md`)
**Scope**: M5 Tactical Planner — preflight threshold calibration (Phase 1) + WP anchor contract (Phase 2 deferred)
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

### 1.2 Defect B (Phase 2 — deferred): WP list anchor contract inconsistency

Three M5 WP generators have inconsistent WP[0] semantics:

| Generator | WP[0] meaning | Distance |
|---|---|---|
| `MidMpcWaypointGenerator::build_waypoints_` | own ship anchor | 0m |
| `build_geometric_fallback_plan_` | own ship anchor | 0m |
| `generate_stable_avoidance_corridor_waypoints` | real first maneuver | 150m |
| `generate_rule13_overtake_corridor_waypoints` | real first maneuver | 600m |
| `generate_return_to_route_waypoints` | real first maneuver | 500m |

For anchor-WP[0] generators, `first_distance = 0m` → optimized/fallback plans rejected regardless of threshold.

**Deferral rationale**: After Phase 1 calibration (360→120), corridor path (`generate_stable_avoidance_corridor_waypoints` WP[0]=150m > 120m) passes preflight and becomes viable fallback. Optimized path's WP[0]=anchor rejection becomes non-blocking (corridor covers). Phase 2 anchor contract is follow-up for optimized-path-first success, not a probe-pass blocker.

### 1.3 Not a v2.2 regression

v2.2 changes constraint derive (ROT∩box direction-aware); WP generation and preflight thresholds unchanged. v2.1 also 0% CONVERGED. Defect A (360m) introduced in `11d86dd8` WIP commit, predates v2.2.

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

## 3. Phase 2 Design (DEFERRED): Uniform Anchor Contract

Recorded for follow-up D-task. Not implemented in this D-ε.

### 3.1 Contract (deferred)

WP list = `[anchor, maneuver_1, ..., maneuver_N]`. WP[0] = own ship anchor. All generators conform. Preflight adds `bool wps_has_anchor` parameter; true → skip wps[0] for first_distance/segment/turn_radius.

### 3.2 Generator changes (deferred)

- `generate_stable_avoidance_corridor_waypoints`: `kDistancesM` prepend 0.0
- `generate_rule13_overtake_corridor_waypoints`: `kDistancesM` prepend 0.0
- `MidMpcWaypointGenerator`: no change (already conforms)
- `build_geometric_fallback_plan_`: no change (already conforms)
- `generate_return_to_route_waypoints`: no change (exception, has_anchor=false)

### 3.3 Preflight changes (deferred)

`validate_gnc_avoidance_plan` + `validate_canonical_route_for_gnc` add `bool wps_has_anchor=false`. 5 callers classified (return path is sole exception).

## 4. Testing (Phase 1 only)

### 4.1 Unit test updates

Update `test_gnc_avoidance_preflight.cpp` boundary tests:
- Tests asserting `required=360` update to `required=120`
- Tests constructing plans at 200m expecting reject now expect pass (200m > 120m)
- Tests constructing plans at 100m expecting reject remain reject (100m < 120m)
- High-speed-flyby branch coverage preserved (XTE check still gated by speed cap)

### 4.2 Integration: V2 probe rerun

After Phase 1 calibration, rerun V2 rule14-ho probe. Pass criteria unchanged (spec §7.2):
- NLP SOLVER_CONVERGED > 30%
- CPA min ≥ 180m

**Expected post-Phase-1 behavior**:
- Optimized path: still rejected (WP[0]=anchor=0m < 120m) — Phase 2 deferred
- Geometric fallback: still rejected (WP[0]=anchor=0m < 120m) — Phase 2 deferred
- **Corridor path**: WP[0]=150m > 120m → **passes preflight** → publishes corridor plan → L4 executes avoidance
- If NLP solver still INFEASIBLE (Layer 1 structural, Codex 🟡 Medium), corridor is the working path

Probe pass depends on corridor path producing sufficient CPA. This is the real signal we need.

## 5. Out of Scope (deferred)

- **Phase 2 WP anchor contract**: §3 above. Follow-up D-task.
- **Layer 1 NLP IPOPT INFEASIBLE**: structural solver/constraint feasibility. Codex diagnosis Q2 🟡 Medium.
- **Layer 2 TailBuilder m6_not_past_clear**: M6 ONSET vs ACTIVE contract. Codex Q3 🟢/🟡.
- **`emergency_guidance_speed_cap_mps` 3.2 m/s threshold review**: separate HAZID question (is 3.2 the right cruise/emergency boundary?). Not touched.

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
