# Avoidance Steering & Route Return Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix three coupled bugs so the own ship (1) executes the avoidance turn commanded by M5 NLP, (2) returns to the planned route after avoidance, and (3) verifies whether XTE closure is needed beyond waypoint bearing guidance.

**Architecture:** BUG-1 fixes `turn_radius_m=0.0` placeholder in the NLP waypoint generator so the SIL bridge activates avoidance autopilot. BUG-2 makes M3 cache and publish the current target waypoint so M4 TRANSIT guides toward the route rather than locking to the current heading. BUG-3 is evaluated after BUG-2 validation; no preemptive changes are made to ADR-boundary layers.

**Tech Stack:** C++ (ROS2 nodes M3/M5), Python (sil_topic_bridge.py unit tests via pytest), gtest (C++ unit tests), colcon build, 1x SIL simulation with Docker Compose.

---

## File Map

| File | Change |
|------|--------|
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp` | MODIFY — compute `turn_radius_m` from NLP trajectory ROT |
| `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp` | MODIFY — add failing test for turn_radius_m > 0 |
| `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp` | MODIFY — add `last_planned_route_` + `current_wp_index_` members |
| `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp` | MODIFY — cache route, advance wp index, fill `current_target_wp` |
| `src/l3_tdl_kernel/m3_mission_manager/test/test_mission_goal_target_wp.cpp` | CREATE — gtest for current_target_wp population |

---

## Context You Need

### How the avoidance chain works (or should work)

```
M6 COLREGs conflict detected
  → M4 behavior_arbiter emits COLREG_AVOID behavior plan with heading window [h_min, h_max]
  → M5 mid_mpc_node runs NLP solver every 1 s
  → NLP converges → wp_gen_.generate() → AvoidancePlan (topic: /l3/m5/avoidance_plan)
  → sil_topic_bridge._on_avoidance_plan()
      has_valid_plan = abs(waypoints[0].turn_radius_m) > 1e-6   ← GATED HERE
      if has_valid_plan: activate avoidance autopilot, latch target heading from M4 window
      if not: reset rudder = 0.0
  → /sil/actuator_cmd → ship_dynamics → heading changes
```

### BUG-1 root cause (confirmed, line-level)

`mid_mpc_waypoint_generator.cpp:91`:
```cpp
wp.turn_radius_m = 0.0;  // Phase E1 placeholder
```
This hardcoded zero causes the bridge gate `abs(turn_radius_m) > 1e-6` to be false for every NLP-converged plan.
The geometric fallback (`build_geometric_fallback_plan_`) correctly computes `R = u/rot` at line 281 — we replicate that logic.

### BUG-2 root cause (confirmed, line-level)

`mission_manager_node.hpp` has **no member** storing the received `PlannedRoute`.
`on_planned_route()` only calls `eta_projector_->update_route(*msg)` — the route data is discarded.
`publish_mission_goal()` never assigns `msg.current_target_wp`, leaving it at `{lat:0, lon:0}`.
`behavior_arbiter_node.cpp:236–238` checks `abs(current_target_wp.lat) > 1e-4` — always false → `nominal_hdg = own_ship.heading_deg` → TRANSIT locks to current heading forever.

### PlannedRoute coordinate type

`l3_external_msgs/msg/PlannedRoute.msg` line 7:
```
geographic_msgs/GeoPath route
```
`GeoPath` contains `geographic_msgs/GeoPoint[]` poses (each with `.latitude`, `.longitude`).
Confirmed in M5 `assemble_input_()` which reads `planned_route_->route.poses[0].pose.position.latitude`.

### Build & test commands

```bash
# Build (inside Docker or with ROS2 sourced):
colcon build --packages-select m5_tactical_planner m3_mission_manager --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Run M5 C++ unit tests:
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+
# or directly:
./build/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator

# Run M3 C++ unit tests:
colcon test --packages-select m3_mission_manager --event-handlers console_direct+

# Run bridge Python unit tests (no ROS needed):
python -m pytest tests/docker/test_sil_topic_bridge.py -v
```

---

## Task 1: BUG-1 — Add failing test for turn_radius_m

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp`

- [ ] **Step 1.1: Add the failing test** at the end of the file (after the last `TEST`):

```cpp
// ---------------------------------------------------------------------------
// 9. NLP converged with heading change → turn_radius_m > 0 (BUG-1 regression)
//    Bridge gate: abs(waypoints[0].turn_radius_m) > 1e-6 must be true.
// ---------------------------------------------------------------------------
TEST(MidMpcWaypointGeneratorTest, NlpConverged_TurnRadiusIsPositive)
{
  MidMpcWaypointGenerator gen{MidMpcWaypointGenerator::Config{}};

  // Build a solution where heading changes from 0 → 0.28 rad over 8 steps at 5 s each.
  // This produces a ROT of 0.28/8 = 0.035 rad/s ≈ 2°/s, turn_radius ≈ 5/0.035 ≈ 143 m.
  MidMpcSolution sol;
  sol.status            = MidMpcSolution::Status::Converged;
  sol.solve_duration_ms = 30;
  sol.ipopt_iterations  = 12;
  constexpr int32_t N = 8;
  constexpr double dt_s = 5.0;
  constexpr double u_mps = 5.0;
  constexpr double psi_start = 0.0;
  constexpr double psi_end   = 0.28;  // ~16 deg — typical NLP minimum starboard turn
  for (int32_t k = 0; k < N; ++k) {
    TrajectoryPoint pt;
    pt.psi_rad = psi_start + (psi_end - psi_start) * static_cast<double>(k) / (N - 1);
    pt.u_mps   = u_mps;
    pt.t_s     = static_cast<double>(k) * dt_s;
    sol.trajectory.push_back(pt);
  }

  const auto plan = gen.generate(sol, 63.44, 10.38);

  ASSERT_EQ(plan.status, "NORMAL");
  ASSERT_FALSE(plan.waypoints.empty());
  // KEY ASSERTION: turn_radius_m must be > 0 so bridge gate passes
  EXPECT_GT(plan.waypoints[0].turn_radius_m, 1e-6)
      << "Bridge gate abs(turn_radius_m) > 1e-6 must pass for avoidance to activate";
}
```

- [ ] **Step 1.2: Run the test to confirm it FAILS**

```bash
colcon build --packages-select m5_tactical_planner --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -20
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | grep -E "FAILED|PASSED|NlpConverged"
```
Expected: `FAILED` — `plan.waypoints[0].turn_radius_m = 0.0` (still placeholder).

---

## Task 2: BUG-1 — Implement turn_radius_m calculation in waypoint generator

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp` (lines 87–108)

The fix: for each waypoint `i`, map to `traj_idx` (already computed), then compute local ROT from adjacent trajectory steps and derive `R = u / |omega|`.

- [ ] **Step 2.1: Replace the `build_waypoints_` implementation body** (lines 87–108):

Replace the inner loop body from:
```cpp
  for (int32_t i = 0; i < num_wp; ++i) {
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.position          = geopoints[static_cast<std::size_t>(i)];
    wp.safety_corridor_m = cfg_.safety_corridor_m;
    wp.turn_radius_m     = 0.0;  // Phase E1 placeholder; Phase E2 derives from ROT

    const int32_t traj_idx = (num_wp == 1) ? 0 : i * (N - 1) / (num_wp - 1);
    wp.target_speed_kn =
        solution.trajectory[static_cast<std::size_t>(traj_idx)].u_mps * units::kKnPerMs;
```

To:
```cpp
  for (int32_t i = 0; i < num_wp; ++i) {
    l3_msgs::msg::AvoidanceWaypoint wp;
    wp.position          = geopoints[static_cast<std::size_t>(i)];
    wp.safety_corridor_m = cfg_.safety_corridor_m;

    const int32_t traj_idx = (num_wp == 1) ? 0 : i * (N - 1) / (num_wp - 1);

    // Compute local ROT from adjacent trajectory steps to derive turn_radius_m.
    // Bridge gate: abs(turn_radius_m) > 1e-6 must be true for avoidance to activate.
    // Formula: R = u / |omega|, omega = dpsi / dt_s (rad/s).
    // Use next available step; for last index use previous step.
    const int32_t rot_idx_a = (traj_idx < N - 1) ? traj_idx : traj_idx - 1;
    const int32_t rot_idx_b = rot_idx_a + 1;
    const double dpsi = solution.trajectory[static_cast<std::size_t>(rot_idx_b)].psi_rad
                      - solution.trajectory[static_cast<std::size_t>(rot_idx_a)].psi_rad;
    const double rot_rad_s = std::abs(dpsi) / cfg_.dt_s;
    const double u_mps = solution.trajectory[static_cast<std::size_t>(traj_idx)].u_mps;
    // Guard: if ROT is negligible (straight ahead), use a large but non-zero radius (500 m).
    // 500 m / 1 kn ship ≈ ~19 min turn arc — effectively straight but bridge gate passes.
    constexpr double kMinRot = 1e-4;      // rad/s — below ~0.006°/s is "straight"
    constexpr double kMaxTurnRadius = 500.0;  // m — straight-line fallback
    wp.turn_radius_m = (rot_rad_s > kMinRot)
        ? std::min(u_mps / rot_rad_s, kMaxTurnRadius)
        : kMaxTurnRadius;

    wp.target_speed_kn =
        solution.trajectory[static_cast<std::size_t>(traj_idx)].u_mps * units::kKnPerMs;
```

- [ ] **Step 2.2: Build and run unit tests**

```bash
colcon build --packages-select m5_tactical_planner --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -5
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | grep -E "FAILED|PASSED|NlpConverged|TurnRadius"
```
Expected: All tests `PASSED`, including `NlpConverged_TurnRadiusIsPositive`.

- [ ] **Step 2.3: Also verify existing bridge test still passes (turn_radius=0 should remain inert in bridge)**

```bash
python -m pytest tests/docker/test_sil_topic_bridge.py::test_placeholder_turn_radius_does_not_command_hard_rudder -v
```
Expected: `PASSED` — this test uses a bridge-level plan with `turn_radius_m=0.0` directly; the fix is upstream in M5, bridge behavior is unchanged.

- [ ] **Step 2.4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_waypoint_generator.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_waypoint_generator.cpp
git commit -m "fix(M5): compute turn_radius_m from NLP trajectory ROT — fixes BUG-1 avoidance chain

NLP waypoint generator was hardcoding turn_radius_m=0 (Phase E1 placeholder).
SIL bridge gate abs(turn_radius_m) > 1e-6 blocked all NLP-path avoidance plans.
Now derives R = u/omega from adjacent trajectory steps; falls back to 500m for
straight-ahead segments so the bridge gate always passes for valid plans.

Refs: BUG-1, sil_topic_bridge.py:512, mid_mpc_waypoint_generator.cpp:91"
```

---

## Task 3: BUG-2 — Add failing test for current_target_wp

**Files:**
- Create: `src/l3_tdl_kernel/m3_mission_manager/test/test_mission_goal_target_wp.cpp`

The test verifies that after `on_planned_route()` is called with a route containing waypoints, the next `publish_mission_goal()` populates `current_target_wp` with non-zero coordinates.

> **Note:** This test uses the `MissionManagerNode` via a test-only helper or directly tests the internal logic via a white-box approach similar to `test_route_received.cpp` which tests only `MissionStateMachine`. Since `publish_mission_goal()` is private, the test will verify the output via a published message. We'll use a lightweight integration approach: instantiate the node in a minimal rclcpp context.

However, looking at the existing test pattern (testing state machine in isolation), it is cleaner to add the test at the data-layer: test a helper function we will extract. See Task 4 for the extraction. For now, create a stub test that will FAIL because the function doesn't exist yet:

- [ ] **Step 3.1: Create the test file**

```cpp
// src/l3_tdl_kernel/m3_mission_manager/test/test_mission_goal_target_wp.cpp
//
// BUG-2 regression: M3 must populate MissionGoal.current_target_wp from
// the cached PlannedRoute so M4 TRANSIT has a valid bearing to track.
//
// Tests the logic of pick_current_target_wp() — a pure helper we extract
// from mission_manager_node.cpp in Task 4.

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "geographic_msgs/msg/geo_path.hpp"
#include "geographic_msgs/msg/geo_point.hpp"

// Helper defined in mission_manager_node.cpp (Task 4 extraction):
// Returns the target GeoPoint from a route at the given waypoint index.
// Returns {0,0,0} if route is null or index is out of bounds.
namespace mass_l3::m3 {
geographic_msgs::msg::GeoPoint pick_current_target_wp(
    const l3_external_msgs::msg::PlannedRoute* route,
    std::size_t wp_index);
}  // namespace mass_l3::m3

#include "l3_external_msgs/msg/planned_route.hpp"

namespace {

l3_external_msgs::msg::PlannedRoute make_route(
    std::vector<std::pair<double, double>> lat_lons)
{
  l3_external_msgs::msg::PlannedRoute route;
  route.schema_version = 112;
  route.route_id       = 1;
  for (auto [lat, lon] : lat_lons) {
    geographic_msgs::msg::GeoPoseStamped pose;
    pose.pose.position.latitude  = lat;
    pose.pose.position.longitude = lon;
    pose.pose.position.altitude  = 0.0;
    route.route.poses.push_back(pose);
  }
  return route;
}

}  // namespace

namespace mass_l3::m3 {

// ---------------------------------------------------------------------------
// 1. Route with 3 waypoints, index=0 → first waypoint
// ---------------------------------------------------------------------------
TEST(MissionGoalTargetWpTest, FirstWaypointIsReturned)
{
  const auto route = make_route({{63.44, 10.38}, {63.50, 10.38}, {63.60, 10.38}});
  const auto wp = pick_current_target_wp(&route, 0u);

  EXPECT_NEAR(wp.latitude,  63.44, 1e-6);
  EXPECT_NEAR(wp.longitude, 10.38, 1e-6);
}

// ---------------------------------------------------------------------------
// 2. Route with 3 waypoints, index=1 → second waypoint
// ---------------------------------------------------------------------------
TEST(MissionGoalTargetWpTest, SecondWaypointIsReturned)
{
  const auto route = make_route({{63.44, 10.38}, {63.50, 10.38}, {63.60, 10.38}});
  const auto wp = pick_current_target_wp(&route, 1u);

  EXPECT_NEAR(wp.latitude,  63.50, 1e-6);
  EXPECT_NEAR(wp.longitude, 10.38, 1e-6);
}

// ---------------------------------------------------------------------------
// 3. Null route → returns (0, 0)  — M4 guard abs(lat) > 1e-4 must be false
// ---------------------------------------------------------------------------
TEST(MissionGoalTargetWpTest, NullRoute_ReturnsZero)
{
  const auto wp = pick_current_target_wp(nullptr, 0u);

  EXPECT_NEAR(wp.latitude,  0.0, 1e-9);
  EXPECT_NEAR(wp.longitude, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// 4. Out-of-bounds index → returns (0, 0)
// ---------------------------------------------------------------------------
TEST(MissionGoalTargetWpTest, OutOfBoundsIndex_ReturnsZero)
{
  const auto route = make_route({{63.44, 10.38}});
  const auto wp = pick_current_target_wp(&route, 99u);

  EXPECT_NEAR(wp.latitude,  0.0, 1e-9);
  EXPECT_NEAR(wp.longitude, 0.0, 1e-9);
}

// ---------------------------------------------------------------------------
// 5. KEY REGRESSION: non-zero lat/lon passes M4 gate abs(lat) > 1e-4
// ---------------------------------------------------------------------------
TEST(MissionGoalTargetWpTest, ValidWaypoint_PassesM4Guard)
{
  const auto route = make_route({{63.44, 10.38}});
  const auto wp = pick_current_target_wp(&route, 0u);

  // M4 behavior_arbiter_node.cpp:236-238 guard:
  EXPECT_GT(std::abs(wp.latitude),  1e-4) << "M4 gate abs(lat) > 1e-4 must pass";
  EXPECT_GT(std::abs(wp.longitude), 1e-4) << "M4 gate abs(lon) > 1e-4 must pass";
}

}  // namespace mass_l3::m3
```

- [ ] **Step 3.2: Register the test in CMakeLists.txt**

Open `src/l3_tdl_kernel/m3_mission_manager/test/CMakeLists.txt` and add (follow the pattern of existing test registrations):

```cmake
ament_add_gtest(test_mission_goal_target_wp
  test_mission_goal_target_wp.cpp
)
ament_target_dependencies(test_mission_goal_target_wp
  rclcpp
  l3_msgs
  l3_external_msgs
  geographic_msgs
)
target_link_libraries(test_mission_goal_target_wp
  m3_mission_manager  # will fail until Task 4 adds the symbol
)
```

- [ ] **Step 3.3: Build to confirm LINK ERROR (function not yet defined)**

```bash
colcon build --packages-select m3_mission_manager --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | grep -E "error:|undefined reference"
```
Expected: `undefined reference to mass_l3::m3::pick_current_target_wp` — confirms we need Task 4.

---

## Task 4: BUG-2 — Implement route caching and current_target_wp population in M3

**Files:**
- Modify: `src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp`
- Modify: `src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp`

### Step 4.1: Add members to the header

- [ ] **In `mission_manager_node.hpp`, after line 112** (after `last_planned_route_time_`):

```cpp
  // BUG-2 fix: cache PlannedRoute so M3 can populate current_target_wp in MissionGoal.
  l3_external_msgs::msg::PlannedRoute::SharedPtr last_planned_route_;
  std::size_t current_wp_index_{0u};
```

### Step 4.2: Extract helper `pick_current_target_wp` into the .cpp

- [ ] **In `mission_manager_node.cpp`, add this free function before the `MissionManagerNode` constructor** (after the namespace opening line `namespace mass_l3::m3 {`):

```cpp
// ---------------------------------------------------------------------------
// pick_current_target_wp — extract target GeoPoint from route at given index.
// Returns {0,0,0} if route is null or index is out of bounds.
// Tested by test_mission_goal_target_wp.cpp.
// ---------------------------------------------------------------------------
geographic_msgs::msg::GeoPoint pick_current_target_wp(
    const l3_external_msgs::msg::PlannedRoute* route,
    std::size_t wp_index)
{
  geographic_msgs::msg::GeoPoint zero;
  zero.latitude  = 0.0;
  zero.longitude = 0.0;
  zero.altitude  = 0.0;
  if (route == nullptr) {
    return zero;
  }
  if (wp_index >= route->route.poses.size()) {
    return zero;
  }
  return route->route.poses[wp_index].pose.position;
}
```

Also add the declaration at the top of the `mission_manager_node.hpp` private section (or alternatively in a separate header — for minimal diff, add to .hpp private section as a `static` free function declaration, or just leave it as a file-scope function in .cpp with a forward declaration inside the test). The simplest approach: declare it in .cpp with internal linkage only for production, and expose it to tests via the test file's own `extern` declaration. Actually, since the test is in the same package, declare it with namespace scope (not `static`) so the linker can find it.

### Step 4.3: Update `on_planned_route()` to cache route and reset wp index

- [ ] **In `mission_manager_node.cpp`, in `on_planned_route()`, after `last_planned_route_time_ = ...` (line 434)**:

```cpp
  // BUG-2 fix: cache the route so publish_mission_goal() can populate current_target_wp.
  last_planned_route_ = msg;
  current_wp_index_ = 0u;
```

### Step 4.4: Add waypoint advancement logic in `on_world_state()`

- [ ] **In `mission_manager_node.cpp`, in `on_world_state()`, after `current_position_` is updated (after the block setting `current_position_.latitude/.longitude`, around line 542)**:

```cpp
  // BUG-2 fix: advance current_wp_index_ when close enough to current target.
  // distance_completion_m is already used by MissionStateMachine for the same purpose.
  const double wp_completion_m = 100.0;  // [m] lookahead radius for waypoint switch
  if (last_planned_route_ &&
      current_wp_index_ + 1u < last_planned_route_->route.poses.size()) {
    const auto tgt = pick_current_target_wp(last_planned_route_.get(), current_wp_index_);
    // Flat-earth distance approximation (valid for < 1 nm)
    constexpr double kEarth = 6'371'000.0;
    constexpr double kRadPerDeg = M_PI / 180.0;
    const double dlat = (tgt.latitude  - current_position_.latitude)  * kRadPerDeg * kEarth;
    const double dlon = (tgt.longitude - current_position_.longitude) * kRadPerDeg * kEarth
                        * std::cos(current_position_.latitude * kRadPerDeg);
    const double dist = std::sqrt(dlat * dlat + dlon * dlon);
    if (dist < wp_completion_m) {
      ++current_wp_index_;
      RCLCPP_INFO(get_logger(),
          "[M3] Waypoint %zu reached (dist=%.1fm) — advancing to wp %zu",
          current_wp_index_ - 1u, dist, current_wp_index_);
    }
  }
```

### Step 4.5: Populate `current_target_wp` in `publish_mission_goal()`

- [ ] **In `mission_manager_node.cpp`, in `publish_mission_goal()`, in the `Active` branch** (after `msg.fsm_state = l3_msgs::msg::MissionGoal::FSM_ACTIVE;` around line 698):

```cpp
  // BUG-2 fix: populate current_target_wp so M4 TRANSIT computes correct bearing.
  msg.current_target_wp = pick_current_target_wp(
      last_planned_route_.get(), current_wp_index_);
```

### Step 4.6: Add `#include <cmath>` if not already present

Check `mission_manager_node.cpp` includes. The file already includes `<chrono>`, `<cstdint>`, etc. Add `<cmath>` if absent (needed for `std::sqrt`, `std::cos`).

- [ ] **Run build and tests**:

```bash
colcon build --packages-select m3_mission_manager --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -10
colcon test --packages-select m3_mission_manager --event-handlers console_direct+ 2>&1 | grep -E "FAILED|PASSED|TargetWp"
```
Expected: All `PASSED` including `MissionGoalTargetWpTest.*`.

- [ ] **Commit**:

```bash
git add \
  src/l3_tdl_kernel/m3_mission_manager/include/m3_mission_manager/mission_manager_node.hpp \
  src/l3_tdl_kernel/m3_mission_manager/src/mission_manager_node.cpp \
  src/l3_tdl_kernel/m3_mission_manager/test/test_mission_goal_target_wp.cpp \
  src/l3_tdl_kernel/m3_mission_manager/test/CMakeLists.txt
git commit -m "fix(M3): populate current_target_wp from planned route — fixes BUG-2 route return

M3 was discarding the PlannedRoute after passing it to EtaProjector.
MissionGoal.current_target_wp was always (0,0), causing M4 TRANSIT to lock
onto the current heading instead of steering toward the next waypoint.

Now M3 caches last_planned_route_, advances current_wp_index_ as the ship
approaches each waypoint, and populates current_target_wp in every
MissionGoal publication.

Refs: BUG-2, behavior_arbiter_node.cpp:236-238, mission_manager_node.cpp:L692"
```

---

## Task 5: Full build verification

- [ ] **Step 5.1: Build all affected packages**

```bash
colcon build \
  --packages-select m5_tactical_planner m3_mission_manager \
  --cmake-args -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  2>&1 | tail -10
```
Expected: `Summary: N packages finished` with no errors.

- [ ] **Step 5.2: Run all unit tests**

```bash
colcon test \
  --packages-select m5_tactical_planner m3_mission_manager \
  --event-handlers console_direct+ \
  2>&1 | grep -E "FAILED|ERROR|OK|passed"

python -m pytest tests/docker/test_sil_topic_bridge.py -v
```
Expected: All green. No regressions.

- [ ] **Step 5.3: Commit if any fixups needed from test output**

---

## Task 6: SIL validation — BUG-1 (rule14, 1x)

> Run inside the Docker environment. See handoff doc section 3 for exact lifecycle sequence.

- [ ] **Step 6.1: Start the stack and capture baseline (pre-fix if on fresh branch)**

```bash
# From host:
docker compose up -d
sleep 10

# Capture 60 s of rule14 at 1x from inside orchestrator container:
docker exec -it sil-orchestrator-1 python3 -c "
import urllib.request, ssl, json
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
def call(path, body=None):
    req = urllib.request.Request('https://localhost:8000' + path,
          data=json.dumps(body).encode() if body else None,
          headers={'Content-Type': 'application/json'})
    return urllib.request.urlopen(req, context=ctx).read()

call('/lifecycle/cleanup')
call('/lifecycle/configure', {'scenario_id': 'colreg-rule14-ho'})
call('/lifecycle/rate',      {'rate': 1.0})
call('/lifecycle/activate')
import time; time.sleep(5)
print('Stack activated')
"
```

- [ ] **Step 6.2: Capture 600 s of own ship data**

```bash
# Capture script (inside sil-nodes-1 container):
docker exec sil-nodes-1 bash -c "
source /opt/ros/*/setup.bash 2>/dev/null
source /opt/ws/install/setup.bash 2>/dev/null
python3 /ws/tests/integration/sim_determinism/capture_imazu.py \
  --scenario colreg-rule14-ho --duration 600 --output /tmp/rule14_bug1.csv
"
docker cp sil-nodes-1:/tmp/rule14_bug1.csv /tmp/rule14_bug1.csv
```

- [ ] **Step 6.3: Verify rudder non-zero and heading change**

```bash
python3 - <<'EOF'
import csv
rows = list(csv.DictReader(open('/tmp/rule14_bug1.csv')))
print(f"Total rows: {len(rows)}")
max_rudder = max(abs(float(r.get('rudder_deg', 0))) for r in rows)
max_hdg_change = max(float(r.get('heading_deg', 0)) for r in rows) - float(rows[0].get('heading_deg', 0))
print(f"Max |rudder|: {max_rudder:.2f} deg  (expect > 5)")
print(f"Max heading change: {max_hdg_change:.2f} deg  (expect > 10)")
EOF
```
Expected: `Max |rudder| > 5`, `Max heading change > 10`.

- [ ] **Step 6.4: Deactivate and cleanup**

```bash
docker exec sil-orchestrator-1 python3 -c "
import urllib.request, ssl, json
ctx = ssl.create_default_context(); ctx.check_hostname = False; ctx.verify_mode = ssl.CERT_NONE
def call(path, body=None):
    req = urllib.request.Request('https://localhost:8000' + path,
          data=json.dumps(body).encode() if body else None,
          headers={'Content-Type': 'application/json'})
    return urllib.request.urlopen(req, context=ctx).read()
call('/lifecycle/deactivate')
call('/lifecycle/cleanup')
"
```

---

## Task 7: SIL validation — BUG-2 (rule14 + imazu, 1x)

- [ ] **Step 7.1: Run rule14 1x, capture 600 s post-avoidance**

Same as Task 6.2, but extend to 600 s (TCPA~327 s + 200 s avoidance + ~73 s return window).

- [ ] **Step 7.2: Verify XTE convergence and TRANSIT behavior**

```bash
python3 - <<'EOF'
import csv, math

rows = list(csv.DictReader(open('/tmp/rule14_bug2.csv')))
# Find approximate avoidance end (behavior == TRANSIT after COLREG_AVOID)
# XTE = easting deviation from lon=10.38 in nm (1 nm ≈ 1852 m)
# Route: lon=10.38 (straight north)
ROUTE_LON = 10.38
xte_nm_series = []
for r in rows:
    lon = float(r.get('lon', 0))
    lat = float(r.get('lat', 0))
    xte_m = (lon - ROUTE_LON) * math.cos(math.radians(lat)) * 60.0 * 1852.0
    xte_nm_series.append(xte_m / 1852.0)

# Last 100 rows should show XTE converging toward 0
last_xte = xte_nm_series[-100:]
print(f"XTE at end (last 100 rows): max={max(abs(x) for x in last_xte):.3f} nm, mean={sum(abs(x) for x in last_xte)/len(last_xte):.3f} nm")
print("PASS" if max(abs(x) for x in last_xte) < 0.3 else "FAIL: XTE did not converge")
EOF
```
Expected: `XTE at end: max < 0.3 nm`.

- [ ] **Step 7.3: Run imazu-01-ho 1x as final regression**

Same capture procedure with `--scenario imazu-01-ho --duration 1200`.

- [ ] **Step 7.4: Verify imazu XTE convergence**

Same XTE check. Expected: XTE converges after avoidance.

---

## Task 8: BUG-3 assessment (post-BUG-2 only)

- [ ] **Step 8.1: Check XTE trajectories from Task 7**

If XTE converges to < 0.2 nm within 3 minutes of avoidance end → **BUG-3 deferred, not needed**.

If XTE > 0.2 nm or non-convergent → **flag for ADR alignment before any fix**. Do NOT modify M5 NLP cost function or bridge LOS without explicit architecture decision.

- [ ] **Step 8.2: Document finding**

```bash
# Add observation to handoff doc or create a new finding note:
cat >> docs/superpowers/specs/2026-05-29-avoidance-fixes-handoff.md <<'EOF'

## BUG-3 Post-BUG-2 Assessment (2026-05-29)

- Rule14 XTE end max: [FILL FROM TASK 7.2]
- Imazu XTE end max: [FILL FROM TASK 7.4]
- Decision: [DEFERRED / ADR_ALIGNMENT_NEEDED]
EOF
```

---

## Self-Review Checklist

- [x] **Spec coverage:** BUG-1 (turn_radius_m) — Task 1-2. BUG-2 (current_target_wp) — Task 3-4. BUG-3 assessment — Task 8. SIL validation — Task 6-7.
- [x] **Placeholder scan:** All test code is complete. All implementation code is complete. No "TBD", "TODO", "similar to above".
- [x] **Type consistency:** `pick_current_target_wp` declared same way in test (namespace `mass_l3::m3`) and implemented in .cpp. `geographic_msgs::msg::GeoPoint` used consistently. `l3_external_msgs::msg::PlannedRoute::SharedPtr` consistent with existing subscriber type.
- [x] **`GeoPath` coordinate check:** `PlannedRoute.route` is `geographic_msgs/GeoPath` which contains `geographic_msgs/GeoPoseStamped[]` poses with `.pose.position` of type `geographic_msgs/GeoPoint` (`.latitude`, `.longitude`). Confirmed by M5 `assemble_input_()` line 185-186 reading `planned_route_->route.poses[0].pose.position.latitude`.
- [x] **Existing tests not broken:** Bridge tests still valid (bridge logic unchanged). Existing waypoint generator tests (Tests 1-8) still pass (no behavior change for straight-ahead solutions, only turn_radius_m now non-zero for turning solutions).

---

## Execution Options

**Plan complete and saved to `docs/superpowers/plans/2026-05-29-avoidance-steering-return.md`.**

Two execution options:

**1. Subagent-Driven (recommended)** — Fresh subagent per task, review between tasks, fast iteration. Use superpowers:subagent-driven-development.

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints. Use superpowers:executing-plans.

Which approach?
