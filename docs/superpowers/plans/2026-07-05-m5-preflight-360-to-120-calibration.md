# M5 GNCPreflight 360→120 Calibration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Calibrate `high_speed_flyby_min_segment_m` from 360m (7.2L, over-conservative, no provenance) to 120m (2.4L, aligned with `emergency_wheel_over_distance_m`, within IMO MSC.137(76) measured advance range).

**Architecture:** Single-value calibration in `gnc_avoidance_preflight.hpp` + one unit test update. No control-flow change. After calibration, corridor path (WP[0]=150m > 120m) passes preflight, becoming viable avoidance path for V2 probe.

**Tech Stack:** C++17, ament_cmake, gtest, ROS2 Humble, colcon.

**Spec:** `docs/superpowers/specs/2026-07-05-m5-preflight-threshold-calibration-design.md` (commit `5d6c15d7`)

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
**Branch:** `codex/colregs-12probe-debug`
**Base HEAD:** `5d6c15d7`

---

## File Structure

- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:31-32` — calibration value
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp:272-281` — `PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin` boundary update
- No new files.

---

## Task 1: Calibrate `high_speed_flyby_min_segment_m` 360→120

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp:31-32`

- [ ] **Step 1: Read current definition for exact context**

```bash
sed -n '28,33p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
```
Expected output (current):
```cpp
  double max_decel_mps2{0.20};  // v2.2 §4.7: aligned with GNC ship_config 0.20 baseline
  double emergency_guidance_speed_cap_mps{3.2};
  double emergency_wheel_over_distance_m{120.0};
  double high_speed_flyby_min_segment_m{360.0};
  double raw_route_rejoin_threshold_m{60.0};
```

- [ ] **Step 2: Apply calibration edit**

Edit `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp`:

Old:
```cpp
  double high_speed_flyby_min_segment_m{360.0};
```
New:
```cpp
  // Calibrated 2026-07-05 (NLM 🟢 ship_maneuvering + IMO MSC.137(76)): 360m
  // (7.2L) was over-conservative WIP value (commit 11d86dd8, no provenance).
  // 120m (2.4L) aligns with emergency_wheel_over_distance_m, within measured
  // 35° turning advance range (2.8-3.31L=140-165m) with margin, below IMO
  // MSC.137(76) advance limit (4.5L=225m). See spec
  // docs/superpowers/specs/2026-07-05-m5-preflight-threshold-calibration-design.md.
  double high_speed_flyby_min_segment_m{120.0};
```

- [ ] **Step 3: Verify edit applied**

```bash
grep -A 6 "high_speed_flyby_min_segment_m{120" src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
```
Expected: 6 comment lines + the calibrated value.

- [ ] **Step 4: Commit**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/gnc_avoidance_preflight.hpp
git commit -m "fix(m5): calibrate high_speed_flyby_min_segment_m 360→120 (NLM 🟢 + IMO MSC.137(76))

360m (7.2L) was over-conservative WIP value (commit 11d86dd8, no provenance,
marked [TBD-HAZID]). NLM 🟢 research confirmed:
- 50m vessel steady turning radius R=U/r=48.8m
- IMO MSC.137(76) advance limit 4.5L=225m
- measured 35° turning advance 2.8-3.31L=140-165m
- 360m=7.2L is 1.6× IMO limit, 2.2-2.6× measured

120m (2.4L) aligns with emergency_wheel_over_distance_m, within measured
range with margin, below IMO limit. After calibration, corridor path
(WP[0]=150m > 120m) passes preflight → viable avoidance path.

Not v2.2 regression (360m predates v2.2). Spec §1.1 references NLM sources.

Spec: docs/superpowers/specs/2026-07-05-m5-preflight-threshold-calibration-design.md"
```

---

## Task 2: Update unit test boundary for new threshold

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp:272-281`

**Rationale:** `PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin` constructs wps at {500m, 800m} (segment=300m) expecting reject under old 360m threshold. With new 120m threshold, 300m > 120m → test would pass (EXPECT_FALSE fails). Update wps to {500m, 600m} (segment=100m < 120m) to preserve reject semantics.

- [ ] **Step 1: Read current test for exact context**

```bash
sed -n '272,282p' src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp
```
Expected (current):
```cpp
TEST(AvoidanceWaypointGen, PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {500.0 / kMetersPerDegLat, 0.0},
      {800.0 / kMetersPerDegLat, 0.0},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {7.2, 7.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "flyby_segment_too_short");
}
```

- [ ] **Step 2: Update wps to preserve reject under 120m threshold**

Edit `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp`:

Old:
```cpp
TEST(AvoidanceWaypointGen, PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin) {
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {500.0 / kMetersPerDegLat, 0.0},
      {800.0 / kMetersPerDegLat, 0.0},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {7.2, 7.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "flyby_segment_too_short");
}
```
New:
```cpp
TEST(AvoidanceWaypointGen, PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin) {
  // Calibrated 2026-07-05: high_speed_flyby_min_segment_m 360→120. Segment
  // must be < 120m to trigger flyby_segment_too_short; 100m exercises the gate.
  const mass_l3::m5::WaypointLatLon origin{0.0, 0.0};
  const std::vector<mass_l3::m5::WaypointLatLon> wps = {
      {500.0 / kMetersPerDegLat, 0.0},
      {600.0 / kMetersPerDegLat, 0.0},
  };
  const auto result = validate_gnc_avoidance_plan(origin, wps, {7.2, 7.2});
  EXPECT_FALSE(result.feasible);
  EXPECT_EQ(result.reason, "flyby_segment_too_short");
}
```

- [ ] **Step 3: Verify edit**

```bash
grep -A 4 "PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin" src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp | head -8
```
Expected: comment + `{500.0...}, {600.0...}`.

- [ ] **Step 4: Commit test update**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
git add src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_waypoint_gen.cpp
git commit -m "test(m5): update flyby-segment boundary for 360→120 calibration

PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin wps {500m, 800m}
(segment=300m) expected reject under old 360m threshold. With 120m threshold,
300m > 120m passes — update wps to {500m, 600m} (segment=100m < 120m) to
preserve reject semantics under calibrated threshold."
```

---

## Task 3: Build + run M5 unit tests in container

**Files:** none (verification only)

- [ ] **Step 1: Rebuild M5 with calibrated value**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner \
     --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake \
     --event-handlers console_direct+ 2>&1" | tail -15
```
Expected: `Finished <<< m5_tactical_planner` with no errors.

- [ ] **Step 2: Run preflight + waypoint_gen unit tests**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ \
     --ctest-args -R 'test_gnc_preflight|test_avoidance_waypoint_gen' 2>&1" | tail -30
```
Expected: all tests PASS. Specific tests to verify:
- `PreflightRejectsHighSpeedFlyBySegmentBelowWheelOverMargin` PASS (updated wps, segment=100m < 120m)
- `Rule13OvertakeCorridorKeepsHighSpeedFlyBySegmentsLongEnough` PASS (rule13 kDistancesM[0]=600m > 120m, all segments ≥ 600m)
- `PreflightRejectsFirstManeuverPointInsideWheelOverDistance` PASS (50m < 120m, unchanged)
- `PreflightRejectsHighSpeedInitialRawRouteXte` PASS (XTE check independent of threshold)

- [ ] **Step 3: Run full M5 test suite (regression check)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner 2>&1" | tail -10
```
Expected: `0 failures`. If failures, investigate whether pre-existing (v2.2 baseline was 449/449 PASS) or calibration-induced.

- [ ] **Step 4: If all tests pass, rebuild runtime binary (BUILD_TESTING=OFF)**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner \
     --cmake-args -DBUILD_TESTING=OFF -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake 2>&1" | tail -5
```
Expected: `Finished <<< m5_tactical_planner`.

- [ ] **Step 5: Restart sil-nodes container to load new binary**

```bash
docker restart codex-gnc-validation-sil-nodes-1
sleep 18
docker exec codex-gnc-validation-sil-nodes-1 bash -c "ps aux | grep m5_mid_mpc_node | grep -v grep" | head -2
```
Expected: m5_mid_mpc_node process running with new binary timestamp.

---

## Task 4: Rerun V2 rule14-ho probe

**Files:** none (verification only)

- [ ] **Step 1: Run rule14-ho probe with restart between runs**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
mkdir -p runs/v2.3_calibration_rule14ho
SIL_ORCH_BASE_URL=https://127.0.0.1:18000/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --profile gnc \
  --sim-rate 5 \
  --restart-between-runs \
  --summary-out runs/v2.3_calibration_rule14ho/probe_$(date +%Y%m%d_%H%M%S).json \
  2>&1 | tail -40
```

- [ ] **Step 2: Check probe result against spec §7.2 pass criteria**

Expected signal to improve vs v2.2 baseline (CPA 3.8m, 0% CONVERGED):
- CPA min: should increase from 3.8m (corridor path now publishes avoidance WP)
- M5 Solver states: still EMPTY/VALID for optimized (WP[0]=anchor deferred), but corridor path should activate
- Behavior: `m5-colregs-*` plan_id in logs (corridor) instead of only `keep_last:optimized_preflight_failed`
-GNCPreflight no longer rejects corridor with `first_maneuver_point_too_close available=0`

**Pass criteria** (spec §7.2):
- NLP SOLVER_CONVERGED > 30% — unlikely (Layer 1 NLP structural + Layer 2 WP[0]=anchor still block optimized)
- CPA min ≥ 180m — **possible** if corridor path produces sufficient lateral offset

- [ ] **Step 3: Save probe evidence**

```bash
cp runs/trace_current.jsonl runs/v2.3_calibration_rule14ho/trace_$(date +%Y%m%d_%H%M%S).jsonl
ls -la runs/v2.3_calibration_rule14ho/
```

- [ ] **Step 4: Inspect M5 logs for corridor path activation**

```bash
docker logs codex-gnc-validation-sil-nodes-1 --since <probe-start-time>Z 2>&1 | \
  grep -E "m5-colregs-|DegradedCandidate|GNCPreflight.*avoidance|CommittedRoute" | \
  grep -v "scenario_loaded\|HB\|SAT pub\|WS pub" | head -30
```
Expected signal:
- `[M5][AvoidancePlan] publish reason=... points=>0` (non-empty corridor plan published)
- No more `[M5][KeepLast] no prior committed route; publishing empty DEGRADED heartbeat reason=optimized_preflight_failed`
- `[M5][GNCPreflight]` for `m5-colregs-*` plan should pass (150m > 120m)

---

## Self-Review

**1. Spec coverage:**
- §2.1 calibration change → Task 1 ✅
- §4.1 unit test update → Task 2 ✅
- §4.2 V2 probe rerun → Task 4 ✅

**2. Placeholder scan:** none. All steps have exact code/commands.

**3. Type consistency:** `high_speed_flyby_min_segment_m` field name consistent across spec/plan/test. wps coordinates use `kMetersPerDegLat` consistently.

**4. Scope:** Phase 1 only. Phase 2 (anchor contract) explicitly deferred in spec §3, no task here.
