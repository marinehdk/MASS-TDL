# M5 Wheel-Over Sampling (Phase 3) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development or superpowers:executing-plans.

**Goal:** Fix `MidMpcWaypointGenerator::sample_waypoints_` + `build_waypoints_` to start sampling at the first trajectory index whose cumulative distance ≥ wheel-over distance (120m). Eliminates wps[1]=25m preflight reject.

**Architecture:** Add `wheel_over_distance_m{120.0}` to generator Config. Both sampling methods compute `start_idx` from cumulative NED positions, then sample uniformly across `[start_idx, N-1]`. Anchor (wps[0]) behavior unchanged.

**Spec:** `docs/superpowers/specs/2026-07-05-m5-preflight-threshold-calibration-design.md` §3.6 (commit `07521488`)

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
**Branch:** `codex/colregs-12probe-debug`
**Base HEAD:** `07521488`

---

## Task 1: Add `wheel_over_distance_m` to generator Config + refactor sampling

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp` (Config struct + ctor)
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp:158-258` (sample_waypoints_ + build_waypoints_)

### Step 1: Read current Config + ctor

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug"
sed -n '40,75p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp
```

Confirm Config has `num_waypoints`, `dt_s`, etc.

### Step 2: Add `wheel_over_distance_m` to Config

Edit Config struct — add after `dt_s`:
```cpp
    double dt_s{5.0};
    // Phase 3 (spec §3.6): start sampling at first trajectory index whose
    // cumulative distance from origin >= this. Must match
    // GncAvoidancePreflightConfig::emergency_wheel_over_distance_m.
    double wheel_over_distance_m{120.0};
```

### Step 3: Read current `sample_waypoints_` + `build_waypoints_`

```bash
sed -n '158,260p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp
```

Both functions currently:
1. Build `ned_pos` cumulative positions starting with `(0,0)`.
2. Sample uniformly: `idx = k * (N-1) / (num_wp-1)`.

### Step 4: Refactor `sample_waypoints_` to start at wheel-over distance

In `sample_waypoints_`, AFTER building `ned_pos` (the `ned_pos.emplace_back(0.0, 0.0)` + cumulative loop), BEFORE the sampling loop:

Add `start_idx` computation:
```cpp
  // Phase 3 (spec §3.6): start sampling at first trajectory index whose
  // cumulative distance from origin >= wheel_over_distance_m. Ensures wps[1]
  // (first maneuver after anchor) clears the GNC preflight gate.
  std::size_t start_idx = static_cast<std::size_t>(std::max(1, N - 1));
  for (std::size_t k = 1; k < ned_pos.size(); ++k) {
    const double dx = ned_pos[k].first;
    const double dy = ned_pos[k].second;
    if (std::sqrt(dx * dx + dy * dy) + 1.0e-6 >= cfg_.wheel_over_distance_m) {
      start_idx = k;
      break;
    }
  }
```

Then replace the sampling loop. Old:
```cpp
  for (int32_t k = 0; k < num_wp; ++k) {
    const int32_t idx = (num_wp == 1) ? 0 : k * (N - 1) / (num_wp - 1);
    const auto& pos = ned_pos[static_cast<std::size_t>(idx)];
    result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, pos.first, pos.second));
  }
```

New:
```cpp
  // Phase 3: sample uniformly across [start_idx, N-1] instead of [0, N-1].
  // num_wp waypoints map to indices start_idx + k*(N-1-start_idx)/(num_wp-1).
  const int32_t span = static_cast<int32_t>(N - 1) - static_cast<int32_t>(start_idx);
  for (int32_t k = 0; k < num_wp; ++k) {
    const int32_t idx = (num_wp == 1)
        ? static_cast<int32_t>(start_idx)
        : static_cast<int32_t>(start_idx) + k * span / (num_wp - 1);
    const auto& pos = ned_pos[static_cast<std::size_t>(idx)];
    result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, pos.first, pos.second));
  }
```

**Note**: `result[0]` is still near origin IF `start_idx` corresponds to a position near origin. But after Phase 3, `start_idx` is the first position ≥ 120m, so `result[0]` is at ≥120m. **However**, the Phase 2 anchor contract expects `wps[0]` = anchor (origin). So we must STILL emit anchor as wps[0].

**REVISED Step 4**: Keep emitting origin as wps[0] (anchor), then sample `num_wp - 1` maneuvers from `[start_idx, N-1]`.

Old sampling loop produces `num_wp` points uniformly across `[0, N-1]`. After Phase 2 anchor contract, wps[0] is the origin (anchor). After Phase 3, wps[1..num_wp-1] should be maneuvers sampled from `[start_idx, N-1]`.

Replace the sampling loop with:
```cpp
  // Phase 2 anchor contract: wps[0] = origin (own ship anchor).
  // Phase 3: wps[1..num_wp-1] sampled uniformly across [start_idx, N-1].
  result.reserve(static_cast<std::size_t>(num_wp));
  result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, 0.0, 0.0));
  const int32_t maneuver_wp = num_wp - 1;  // remaining waypoints after anchor
  const int32_t span = static_cast<int32_t>(N - 1) - static_cast<int32_t>(start_idx);
  for (int32_t k = 0; k < maneuver_wp; ++k) {
    const int32_t idx = (maneuver_wp == 1)
        ? static_cast<int32_t>(start_idx)
        : static_cast<int32_t>(start_idx) + k * span / (maneuver_wp - 1);
    const auto& pos = ned_pos[static_cast<std::size_t>(idx)];
    result.push_back(ned_to_geopoint_(own_ship_lat, own_ship_lon, pos.first, pos.second));
  }
```

Wait — but `sample_waypoints_` does NOT currently emit anchor explicitly. The anchor (wps[0]=origin) came from `ned_pos[0]=(0,0)` being sampled at k=0 (idx=0). With Phase 3 `start_idx > 0`, the k=0 sample would be at `start_idx` (≥120m), losing the anchor.

**So Phase 3 requires explicit anchor emission.** The revised loop above does this correctly.

### Step 5: Apply same `start_idx` logic to `build_waypoints_`

`build_waypoints_` currently builds `ned_pos` identically and samples with the same `idx` formula. Apply the same refactor: compute `start_idx`, emit anchor explicitly, sample maneuvers from `[start_idx, N-1]`.

The turn_radius / target_speed / wp_distance indexing must align: they use `traj_idx` derived from the same formula. Update consistently.

### Step 6: Build + test

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "rm -rf /opt/ws/build/m5_tactical_planner /opt/ws/install/m5_tactical_planner && \
   source /opt/ros/humble/setup.bash && \
   cd /opt/ws && colcon build --packages-select m5_tactical_planner \
     --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake 2>&1" | tail -5

docker exec codex-gnc-validation-sil-nodes-1 bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner 2>&1" | tail -5
```

Expected: build OK, 0 test failures (existing tests + new Phase 2 tests still pass; the Phase 2 `PreflightHasAnchorSkipsFirstWaypoint` is synthetic wps, not generator, so unaffected).

**Note**: `test_mid_mpc_waypoint_generator.cpp` may have tests asserting specific wps positions under uniform sampling — those may need index updates. Inspect + update if they fail.

### Step 7: Add Phase 3 test

In `test_mid_mpc_waypoint_generator.cpp` (or `test_avoidance_waypoint_gen.cpp` if that's where generator tests live — verify):

```cpp
// Phase 3 (spec §3.6): wps[1] (first maneuver after anchor) must be at
// >= wheel_over_distance_m from origin.
TEST(MidMpcWaypointGenerator, SamplesFirstManeuverBeyondWheelOver) {
  MidMpcWaypointGenerator::Config cfg;
  cfg.num_waypoints = 10;
  cfg.dt_s = 5.0;
  cfg.wheel_over_distance_m = 120.0;
  MidMpcWaypointGenerator gen(cfg);

  MidMpcSolution sol;
  sol.status = MidMpcSolution::Status::Converged;
  sol.trajectory.resize(18);
  double t = 0.0;
  for (auto& pt : sol.trajectory) {
    pt.t_s = t;
    pt.u_mps = 5.0;     // 25m/step
    pt.psi_rad = 0.0;   // straight north
    t += 5.0;
  }

  const auto plan = gen.generate(sol, /*own_ship_lat=*/63.44, /*own_ship_lon=*/10.38);
  ASSERT_EQ(plan.status, "NORMAL");
  ASSERT_GE(plan.waypoints.size(), 2u);
  // wps[0] = anchor (own ship)
  // wps[1] = first maneuver, must be >= 120m from origin
  const auto& wp0 = plan.waypoints[0].position;
  const auto& wp1 = plan.waypoints[1].position;
  // anchor within 1m of own ship
  EXPECT_LT(std::hypot((wp0.latitude - 63.44) * 111000.0,
                       (wp0.longitude - 10.38) * 111000.0), 1.0);
  // first maneuver >= 120m from own ship
  const double d1_m = std::hypot((wp1.latitude - 63.44) * 111000.0,
                                 (wp1.longitude - 10.38) * 111000.0);
  EXPECT_GE(d1_m, 120.0 - 1.0);
}
```

### Step 8: Commit

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_waypoint_generator.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp
git commit -m "feat(m5): wheel-over起始 sampling in MidMpcWaypointGenerator (Phase 3)

Phase 3 (spec §3.6). sample_waypoints_ + build_waypoints_ now compute start_idx
= first trajectory index whose cumulative distance >= wheel_over_distance_m
(default 120m). Anchor (wps[0]) emitted explicitly; maneuvers sampled from
[start_idx, N-1].

Fixes V2.3 Phase1+2 probe blocker: wps[1] was at trajectory[1]≈25m < 120m
required. NLP horizon (450m) sufficient; sampling start was wrong.

Config adds wheel_over_distance_m{120.0} (matches emergency_wheel_over_distance_m).
New test SamplesFirstManeuverBeyondWheelOver."
```

---

## Self-Review

**Spec coverage**: §3.6 → Task 1 ✅
**Placeholder**: Step 5 says "apply same logic" — concrete enough given Step 4 detail.
**Risk**: existing `test_mid_mpc_waypoint_generator.cpp` tests asserting uniform sampling positions may break. Step 6 handles via inspect+update.
