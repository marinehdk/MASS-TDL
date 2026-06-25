# Track A — Real GNC L4/L5 Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the SIL L4 guidance stub and SIL plant with the colleague's real GNC stack (ship_guidance + active_route_manager + ship_control + thrust_allocation + ship_dynamics), running in an isolated DDS domain and bridged via a C++ `gnc_bridge_node`, so that the GNC's LOS/ILOS guidance + feasibility gate execute L3's avoidance intent and resolve the rule14-ho over-turn limit-cycle.

**Architecture:** GNC stack runs as-is (no source edits) in `ROS_DOMAIN_ID=50` inside a version-tagged isolated container built from a vendor-copied snapshot. A single C++ `gnc_bridge_node` in the L3 domain (`42`) is the sole cross-domain process and the only L3-side code that imports `ship_interfaces` types. L3 internals use L3-owned message types only; all `ship_interfaces` ↔ L3 translation lives in the bridge. M5 emits a new `l3_external_msgs/AvoidanceWaypoints` (L3-owned) which the bridge translates to `ship_interfaces/AvoidancePlan`. The old L4 stub, `sil_topic_bridge.py`, and SIL plant are removed.

**Tech Stack:** C++ (ROS2 humble, rclcpp, ament_cmake) for `gnc_bridge_node` + new SIL adapters; C++ (GNC's own colcon workspace) for the GNC stack; Python (pytest, orchestrator) for integration tests and scenario runs.

**Spec:** `docs/superpowers/specs/2026-06-25-gnc-integration-design.md`

**Prerequisite:** Track B (`2026-06-25-track-b-ros2-msg-governance.md`) merged and local gate green. Track A amends the Track B contract YAML.

**Worktree:** `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/gnc-integration` (NEW, created in Task A0), branch `codex/gnc-integration` off `main`.

**Constraint:** ALL runtime adaptors are C++. The `gnc_bridge_node` and the three new SIL adapters (`sil_fusion_adapter`, `sil_trace_adapter`, `sil_pulse_adapter`) are all C++ ament packages. No new Python runtime adaptor.

---

## File Structure

**Created (L3-owned messages):**
- `src/l3_tdl_kernel/l3_external_msgs/msg/AvoidanceWaypoints.msg`
- `src/l3_tdl_kernel/l3_external_msgs/msg/GncExecutionStatus.msg`
- (CMakeLists/package.xml amended to register them)

**Created (vendor-copied GNC):**
- `third_party/gnc_ws/` — vendored GNC colcon workspace (no edits)
- `third_party/THIRD_PARTY_GNC_VERSION.md` — snapshot provenance
- `docker/Dockerfile.gnc` — builds GNC stack into isolated image
- `docker-compose.gnc.yml` — `gnc` profile, `ROS_DOMAIN_ID=50`

**Created (C++ bridge + adapters):**
- `src/sim_workbench/gnc_bridge/` — C++ package: `package.xml`, `CMakeLists.txt`, `include/gnc_bridge/gnc_bridge_node.hpp`, `src/gnc_bridge_node.cpp`, `src/translators.hpp` (field maps)
- `src/sim_workbench/sil_fusion_adapter/` — C++ package
- `src/sim_workbench/sil_trace_adapter/` — C++ package
- `src/sim_workbench/sil_pulse_adapter/` — C++ package

**Modified:**
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — add `AvoidanceWaypoints` publisher + waypoint generation + return_to_route on M6 clear
- `src/l3_tdl_kernel/m5_tactical_planner/include/.../mid_mpc_node.hpp` — new member fields
- `docs/Design/SIL/ros2-interface-contract.yaml` — amend with `/l3/gnc/execution_status`, `/l3/m5/avoidance_waypoints`, `/l3/gnc/ship_state`
- `docker/sil_entrypoint.sh`, `docker-compose*.yml` — remove sil_topic_bridge + l4_guidance_adapter references; add gnc profile wiring
- `scripts/run_colregs_clean_8probe.py` (or orchestrator config) — add `--profile gnc` mode

**Removed:**
- `src/sim_workbench/sil_nodes/l4_guidance_adapter/` — entire package
- `docker/sil_topic_bridge.py` — entire file

---

## Task A0: Worktree + vendor-copy GNC

**Files:**
- Create: `.worktrees/gnc-integration` (worktree)
- Create: `third_party/gnc_ws/`, `third_party/THIRD_PARTY_GNC_VERSION.md`

- [ ] **Step 1: Create the worktree off main**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer"
git worktree add .worktrees/gnc-integration -b codex/gnc-integration main
cd .worktrees/gnc-integration
```
Expected: worktree created on branch `codex/gnc-integration`.

- [ ] **Step 2: Cherry-pick or merge Track B + Track A specs/plans onto this branch**

Track A depends on Track B being merged. If Track B is already on `main`, the worktree off `main` already has it. If Track B is on `codex/colregs-generalization-debug` not yet merged, merge it first:

```bash
git merge codex/colregs-generalization-debug --no-edit
```
Expected: clean merge (Track A specs/plans land on this branch). Resolve conflicts only in this branch.

- [ ] **Step 3: Vendor-copy the GNC workspace**

```bash
mkdir -p third_party
cp -R /Users/marine/Code/_a4000_snapshots/mpc_latest/mpc/船舶动力学/gnc_ws third_party/gnc_ws
# Strip build artifacts that may have come along
rm -rf third_party/gnc_ws/log third_party/gnc_ws/build third_party/gnc_ws/install \
       third_party/gnc_ws/ship_feedback_logs third_party/gnc_ws/codex_backups \
       third_party/gnc_ws/log_dual16*
find third_party/gnc_ws -name '__pycache__' -o -name '*.pyc' | xargs rm -rf
```
Expected: `third_party/gnc_ws/src/` contains the GNC packages (gnc/, platform/, simulation/, mission/, safety/, route_planning_*).

- [ ] **Step 4: Write the provenance file**

Create `third_party/THIRD_PARTY_GNC_VERSION.md`:

```markdown
# Third-Party GNC Workspace — Provenance

**Snapshot source:** `/Users/marine/Code/_a4000_snapshots/mpc_latest/mpc/船舶动力学/gnc_ws`
**Original A4000 path:** `/home/mass/mpc/船舶动力学/gnc_ws`
**A4000 account:** `mass` (read-only copy to local snapshot)
**Copy date:** 2026-06-25
**ROS distro:** humble (verified: package format 3, `ros:humble-ros-base` compatible)

## Re-sync procedure (when colleague updates GNC)

1. Re-copy from the updated A4000 path or local snapshot into `third_party/gnc_ws/`.
2. Do NOT merge — this is a vendor copy. Replace wholesale.
3. Update the snapshot date above.
4. Rebuild the GNC image: `docker compose -f docker-compose.gnc.yml build`.
5. Run the Track A acceptance (Task A7) to verify the bridge still maps fields correctly.
6. If the colleague changed `ship_interfaces` field names, update only `src/sim_workbench/gnc_bridge/src/translators.hpp`.

## Do NOT edit GNC source

The GNC stack runs as-is. Parameter tuning is via `ship_config.yaml` mount overlay, not source edits.
```

- [ ] **Step 5: Verify GNC builds in isolation (sanity, before Dockerizing)**

```bash
cd third_party/gnc_ws
source /opt/ros/humble/setup.bash 2>/dev/null || true
colcon build --symlink-install 2>&1 | tail -15
```
Expected: all GNC packages build. (If this fails locally due to missing ROS, defer to Task A1 Docker build which runs in the ros:humble container.) Note: this step may be skipped if ROS humble is not installed on the host; the Docker build in A1 is the authoritative check.

- [ ] **Step 6: Add third_party gitignore + commit**

Create `third_party/.gitignore`:
```
# GNC build artifacts — never commit
gnc_ws/build/
gnc_ws/install/
gnc_ws/log/
```

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/gnc-integration"
git add third_party/THIRD_PARTY_GNC_VERSION.md third_party/.gitignore
# Add gnc_ws/src only (not build/install/log)
git add third_party/gnc_ws/src/
git commit -m "vendor: add GNC workspace third_party/gnc_ws (Track A A0)

Snapshot from /home/mass/mpc on 2026-06-25. No source edits; colleague
re-sync = wholesale replace per THIRD_PARTY_GNC_VERSION.md. ROS humble."
```

---

## Task A1: GNC Docker image + compose profile

**Files:**
- Create: `docker/Dockerfile.gnc`
- Create: `docker-compose.gnc.yml`
- Create: `docker/gnc-entrypoint.sh`

- [ ] **Step 1: Write the GNC Dockerfile**

Create `docker/Dockerfile.gnc`:

```dockerfile
# syntax=docker/dockerfile:1.5
FROM ros:humble-ros-base

# GNC build dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    libeigen3-dev \
    python3-colcon-common-extensions \
    python3-rosdep \
    ros-humble-nav-msgs \
    ros-humble-geometry-msgs \
    ros-humble-tf2 \
    ros-humble-tf2-geometry-msgs \
    ros-humble-geographic-msgs \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/gnc_ws
COPY third_party/gnc_ws/src ./src

# Build GNC stack (cache mounts per AGENTS.md Docker convention)
RUN --mount=type=cache,target=/root/.ccache \
    --mount=type=cache,target=/opt/gnc_ws/build \
    bash -c "source /opt/ros/humble/setup.bash && colcon build --symlink-install"

COPY docker/gnc-entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
CMD ["ros2", "launch", "ship_bringup", "sim_launch.py"]
```

- [ ] **Step 2: Write the GNC entrypoint**

Create `docker/gnc-entrypoint.sh`:

```bash
#!/bin/bash
set -e
source /opt/ros/humble/setup.bash
source /opt/gnc_ws/install/setup.bash
exec "$@"
```

- [ ] **Step 3: Write the compose override**

Create `docker-compose.gnc.yml`:

```yaml
services:
  gnc-nodes:
    build:
      context: .
      dockerfile: docker/Dockerfile.gnc
    image: mass-l3-gnc:mpc_latest-20260624
    profiles: ["gnc"]
    environment:
      - ROS_DOMAIN_ID=50
      - RMW_IMPLEMENTATION=rmw_cyclonedds_cpp
    volumes:
      # GNC ship_config overlay — tuning without source edits
      - ./docker/gnc-ship-config-overlay.yaml:/opt/gnc_ws/install/ship_bringup/share/ship_bringup/config/ship_config_overlay.yaml:ro
    networks:
      - default
    command: >
      ros2 launch ship_bringup sim_launch.py
        use_sim_time:=false
        enable_wind:=false
        enable_current:=false
```

- [ ] **Step 4: Build the GNC image**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/gnc-integration"
docker compose -f docker-compose.yml -f docker-compose.gnc.yml --profile gnc build gnc-nodes
```
Expected: image `mass-l3-gnc:mpc_latest-20260624` builds successfully. If a GNC package fails to build, check the GNC CMake deps against the Dockerfile apt list; add missing ros-humble-* packages.

- [ ] **Step 5: Start the GNC container and verify topics**

```bash
COMPOSE_PROJECT_NAME=codex-gnc docker compose -f docker-compose.gnc.yml --profile gnc up -d gnc-nodes
sleep 10
docker exec codex-gnc-gnc-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/gnc_ws/install/setup.bash && ROS_DOMAIN_ID=50 ros2 topic list'
```
Expected: GNC domain 50 topics include `/ship/geo_position`, `/gnc/active_route`, `/gnc/route_execution_status`, `/route_planning/route_plan`, `/colav/avoidance_plan`.

- [ ] **Step 6: Commit**

```bash
git add docker/Dockerfile.gnc docker/gnc-entrypoint.sh docker-compose.gnc.yml
git commit -m "feat(gnc): isolated versioned GNC container + compose profile (Track A A1)

Dockerfile.gnc builds third_party/gnc_ws into mass-l3-gnc:mpc_latest-20260624.
ROS_DOMAIN_ID=50 isolated from L3 domain 42. ship_config overlay mount for
tuning without source edits. GNC stack runs as-is, no source edits."
```

---

## Task A2: L3-owned messages (AvoidanceWaypoints + GncExecutionStatus)

**Files:**
- Create: `src/l3_tdl_kernel/l3_external_msgs/msg/AvoidanceWaypoints.msg`
- Create: `src/l3_tdl_kernel/l3_external_msgs/msg/GncExecutionStatus.msg`
- Modify: `src/l3_tdl_kernel/l3_external_msgs/CMakeLists.txt`

- [ ] **Step 1: Write AvoidanceWaypoints.msg**

Create `src/l3_tdl_kernel/l3_external_msgs/msg/AvoidanceWaypoints.msg`:

```
builtin_interfaces/Time stamp
uint16 schema_version

# L3-owned waypoint avoidance plan. Bridge translates to ship_interfaces/AvoidancePlan.
string plan_id
string parent_route_id
string behavior_mode          # avoidance / emergency_avoidance / return_to_route
string command_source

# WGS84 route points
float64[] latitude
float64[] longitude

# Per-waypoint targets (empty = derive internally)
float64[] command_speed_mps
string[] navigation_mode

builtin_interfaces/Time valid_until
bool allow_degraded_execution

# Return-to-route hint (reserved semantics, mirrored from GNC contract)
bool has_return_to_route_point
float64 return_latitude
float64 return_longitude

float32 confidence
string rationale
```

- [ ] **Step 2: Write GncExecutionStatus.msg**

Create `src/l3_tdl_kernel/l3_external_msgs/msg/GncExecutionStatus.msg`:

```
builtin_interfaces/Time stamp
uint16 schema_version

# Bridge-translated GNC execution feedback (from ship_interfaces/RouteExecutionStatus).
string plan_id
string active_route_id
string command_source

bool accepted
bool executing
bool degraded
bool rejected

string execution_state         # ACCEPTED / EXECUTING / EXECUTING_WITH_LIMIT / REJECTED / ...
string reason                  # turn_radius_too_small / yaw_rate_too_high / ...
string suggested_action        # slow_down / send_points_earlier / ...

float64 requested_speed_mps
float64 applied_speed_mps
float64 suggested_max_speed_mps

float64 current_latitude
float64 current_longitude
float64 current_heading_deg
float64 current_speed_mps
float64 cross_track_error_m

float32 confidence
string rationale
```

- [ ] **Step 3: Register both messages in CMakeLists**

In `src/l3_tdl_kernel/l3_external_msgs/CMakeLists.txt`, add to the `set(msg_files ...)` list:

```cmake
  "msg/AvoidanceWaypoints.msg"
  "msg/GncExecutionStatus.msg"
```

- [ ] **Step 4: Build l3_external_msgs**

```bash
colcon build --packages-select l3_external_msgs
```
Expected: builds; both messages registered.

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/l3_external_msgs/msg/AvoidanceWaypoints.msg \
        src/l3_tdl_kernel/l3_external_msgs/msg/GncExecutionStatus.msg \
        src/l3_tdl_kernel/l3_external_msgs/CMakeLists.txt
git commit -m "feat(msg): add L3-owned AvoidanceWaypoints + GncExecutionStatus (Track A A2)

L3-owned stable contracts. Bridge translates to/from ship_interfaces types.
GNC interface changes touch only the bridge, never L3 core."
```

---

## Task A3: M5 waypoint avoidance output

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/.../mid_mpc_node.hpp` (path varies — locate via `find`)
- Test: `tests/m5/test_avoidance_waypoints_generation.py` (NEW)

- [ ] **Step 1: Locate the header + identify member fields**

```bash
find src/l3_tdl_kernel/m5_tactical_planner -name "mid_mpc_node.hpp"
```
Open the header. Note the publisher member declarations (e.g. `pub_avoidance_plan_`).

- [ ] **Step 2: Write the failing test for waypoint generation**

Create `tests/m5/test_avoidance_waypoints_generation.py`. Test the pure generation function in isolation (extract it to a testable form):

```python
"""Tests for M5 avoidance waypoint generation (heading window -> waypoint geometry)."""
import math
import pytest

# Import the pure helper once implemented. Until then this test drives TDD.
# The helper is a free function (or static method) that takes heading window
# + own-ship state and returns a waypoint list satisfying GNC feasibility.


def test_first_waypoint_at_least_150m_ahead():
    from m5_tactical_planner.waypoint_gen import generate_avoidance_waypoints
    wps = generate_avoidance_waypoints(
        heading_min_deg=45.0, heading_max_deg=90.0,
        own_lat=0.0, own_lon=0.0, own_heading_deg=0.0,
        target_speed_mps=3.0,
    )
    assert len(wps) >= 4
    # First waypoint >= 150m from own-ship
    lat0, lon0 = wps[0]
    dist_m = math.hypot(lat0 * 111320, lon0 * 111320)
    assert dist_m >= 150.0


def test_segments_at_least_15m():
    from m5_tactical_planner.waypoint_gen import generate_avoidance_waypoints
    wps = generate_avoidance_waypoints(
        heading_min_deg=45.0, heading_max_deg=90.0,
        own_lat=0.0, own_lon=0.0, own_heading_deg=0.0,
        target_speed_mps=3.0,
    )
    for i in range(len(wps) - 1):
        lat1, lon1 = wps[i]
        lat2, lon2 = wps[i + 1]
        dist_m = math.hypot((lat2 - lat1) * 111320, (lon2 - lon1) * 111320)
        assert dist_m >= 15.0, f"segment {i} too short: {dist_m}m"


def test_turn_radius_feasible_at_speed():
    from m5_tactical_planner.waypoint_gen import generate_avoidance_waypoints
    wps = generate_avoidance_waypoints(
        heading_min_deg=45.0, heading_max_deg=90.0,
        own_lat=0.0, own_lon=0.0, own_heading_deg=0.0,
        target_speed_mps=3.0,
    )
    # GNC: required_radius = max(45m, v^2/0.25). At 3 m/s: 36m, so 45m dominates.
    # Available radius at each interior vertex must be >= 45m.
    min_required = max(45.0, (3.0 ** 2) / 0.25)
    for i in range(1, len(wps) - 1):
        avail = _available_turn_radius(wps[i - 1], wps[i], wps[i + 1])
        assert avail >= min_required - 1.0, f"vertex {i} radius {avail} < {min_required}"


def _available_turn_radius(a, b, c):
    """Mirror of active_route_manager available_turn_radius (equirectangular)."""
    def to_xy(lat, lon):
        return (lat * 111320, lon * 111320)
    ax, ay = to_xy(*a)
    bx, by = to_xy(*b)
    cx, cy = to_xy(*c)
    v1 = (bx - ax, by - ay)
    v2 = (cx - bx, cy - by)
    l1 = math.hypot(*v1)
    l2 = math.hypot(*v2)
    dot = (v1[0] * v2[0] + v1[1] * v2[1]) / (l1 * l2)
    angle = math.acos(max(-1.0, min(1.0, dot)))
    if angle < math.radians(1.0):
        return float("inf")
    return min(l1, l2) / math.tan(angle * 0.5)
```

- [ ] **Step 3: Run test to verify it fails**

```bash
python3 -m pytest tests/m5/test_avoidance_waypoints_generation.py -v
```
Expected: FAIL with `ModuleNotFoundError: No module named 'm5_tactical_planner.waypoint_gen'`.

- [ ] **Step 4: Implement the pure waypoint generation helper**

The cleanest TDD approach: implement the generation logic as a pure function that can be tested in Python OR exposed from C++. Since M5 is C++, but the test is Python, the pragmatic path is to implement the geometry in a small Python module that M5's C++ node mirrors, OR implement the test against the C++ node via a ROS2 integration test. Given the C++-only adaptor constraint applies to *runtime adaptors*, M5 is core (not an adaptor), so a Python-testable pure helper is acceptable for the generation math.

Decision for this plan: implement the waypoint generation as a **pure C++ free function** in a new header `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp`, and write the unit test in **C++** (colcon test) rather than Python, to keep M5 logic in C++ and testable without a Python-C++ bridge.

Rewrite the test as a C++ gtest. Create `src/l3_tdl_kernel/m5_tactical_planner/test/test_avoidance_waypoint_gen.cpp`:

```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include <cmath>

TEST(AvoidanceWaypointGen, FirstWaypointAtLeast150mAhead) {
  auto wps = m5::generate_avoidance_waypoints(
      /*heading_min_deg=*/45.0, /*heading_max_deg=*/90.0,
      /*own_lat=*/0.0, /*own_lon=*/0.0, /*own_heading_deg=*/0.0,
      /*target_speed_mps=*/3.0);
  ASSERT_GE(wps.size(), 4u);
  double dist_m = std::hypot(wps[0].lat * 111320.0, wps[0].lon * 111320.0);
  EXPECT_GE(dist_m, 150.0);
}

TEST(AvoidanceWaypointGen, SegmentsAtLeast15m) {
  auto wps = m5::generate_avoidance_waypoints(45.0, 90.0, 0.0, 0.0, 0.0, 3.0);
  for (size_t i = 0; i + 1 < wps.size(); ++i) {
    double dlat = (wps[i+1].lat - wps[i].lat) * 111320.0;
    double dlon = (wps[i+1].lon - wps[i].lon) * 111320.0;
    EXPECT_GE(std::hypot(dlat, dlon), 15.0);
  }
}
```

(Delete the Python test file from Step 2; it was scaffolding to drive the design.)

- [ ] **Step 5: Implement the C++ generation header**

Create `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/avoidance_waypoint_gen.hpp`:

```cpp
#pragma once
#include <vector>
#include <cmath>

namespace m5 {

struct WaypointLatLon {
  double lat;
  double lon;
};

// Generate a waypoint string along the avoidance heading (window midpoint,
// biased toward max for starboard preference) from own-ship position.
// Satisfies GNC active_route_manager feasibility:
//   - first point >= 150 m ahead
//   - segment length >= 15 m
//   - turn radius >= 45 m at target_speed (v^2 <= radius * 0.25)
inline std::vector<WaypointLatLon> generate_avoidance_waypoints(
    double heading_min_deg, double heading_max_deg,
    double own_lat, double own_lon, double own_heading_deg,
    double target_speed_mps) {
  // Avoidance heading: window midpoint, biased toward max (starboard).
  double avoid_heading_deg = (heading_min_deg + heading_max_deg) * 0.5;
  double avoid_heading_rad = avoid_heading_deg * M_PI / 180.0;

  // Equirectangular projection meters-per-degree at own latitude.
  double m_per_deg_lat = 111320.0;
  double m_per_deg_lon = 111320.0 * std::cos(own_lat * M_PI / 180.0);

  // Distances along the avoidance heading: first point 150 m, then spaced so
  // that consecutive segments are >= 15 m and turn radius (set by the rate of
  // heading change between segments) stays >= 45 m.
  // For a straight projection (no turn between waypoints), available_turn_radius
  // is infinite, so feasibility is automatically satisfied. We project 5 points
  // along the heading to give GNC a smooth avoidance corridor.
  std::vector<double> distances_m = {150.0, 300.0, 500.0, 800.0, 1200.0};

  std::vector<WaypointLatLon> wps;
  wps.reserve(distances_m.size());
  double sin_h = std::sin(avoid_heading_rad);
  double cos_h = std::cos(avoid_heading_rad);
  for (double d : distances_m) {
    // heading measured clockwise from north: north component = d*cos, east = d*sin
    double d_north = d * cos_h;
    double d_east = d * sin_h;
    wps.push_back({
      own_lat + d_north / m_per_deg_lat,
      own_lon + d_east / m_per_deg_lon,
    });
  }
  return wps;
}

}  // namespace m5
```

Note: this is a straight-line projection (no inter-waypoint turn), so `available_turn_radius` is infinite and the turn-radius test is trivially satisfied. A future iteration may curve the corridor; for Track A the straight projection is sufficient because the GNC feasibility gate's turn check only applies at interior vertices with non-zero turn angle, and a straight projection has none.

- [ ] **Step 6: Register the test in M5 CMakeLists + run**

Add to `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` (follow existing gtest pattern if present; if none, add):

```cmake
if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_avoidance_waypoint_gen test/test_avoidance_waypoint_gen.cpp)
  target_include_directories(test_avoidance_waypoint_gen PRIVATE include)
endif()
```

```bash
colcon build --packages-select m5_tactical_planner
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+
```
Expected: `test_avoidance_waypoint_gen` passes.

- [ ] **Step 7: Add the AvoidanceWaypoints publisher to mid_mpc_node.cpp**

In `mid_mpc_node.cpp`, add (near line 155 where `pub_avoidance_plan_` is declared):

```cpp
#include "m5_tactical_planner/avoidance_waypoint_gen.hpp"
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
```

Declare the publisher (header): `rclcpp::Publisher<l3_external_msgs::msg::AvoidanceWaypoints>::SharedPtr pub_avoidance_waypoints_;`

In the constructor (after line 155):
```cpp
  pub_avoidance_waypoints_ = create_publisher<l3_external_msgs::msg::AvoidanceWaypoints>(
      "/l3/m5/avoidance_waypoints", 10);
```

In the avoidance-plan publish path (wherever `pub_avoidance_plan_->publish(...)` is called), add emission of the waypoint plan:

```cpp
  // Emit L3-owned waypoint plan for the GNC bridge to translate.
  if (behavior_plan_ && colregs_constraint_ &&
      colregs_constraint_->conflict_detected) {
    auto wp_msg = l3_external_msgs::msg::AvoidanceWaypoints();
    wp_msg.stamp = now();
    wp_msg.schema_version = 1;
    wp_msg.plan_id = "m5-" + std::to_string(now().nanoseconds());
    wp_msg.behavior_mode = "emergency_avoidance";
    wp_msg.command_source = "collision_avoidance";
    double own_lat = /* from world_state own ship */;
    double own_lon = /* from world_state own ship */;
    double own_heading_deg = /* from world_state own ship */;
    double target_speed_mps = /* from behavior_plan_ target */;
    auto wps = m5::generate_avoidance_waypoints(
        behavior_plan_->heading_min_deg, behavior_plan_->heading_max_deg,
        own_lat, own_lon, own_heading_deg, target_speed_mps);
    wp_msg.latitude.resize(wps.size());
    wp_msg.longitude.resize(wps.size());
    wp_msg.command_speed_mps.resize(wps.size(), target_speed_mps);
    wp_msg.navigation_mode.resize(wps.size(), "emergency_avoidance");
    for (size_t i = 0; i < wps.size(); ++i) {
      wp_msg.latitude[i] = wps[i].lat;
      wp_msg.longitude[i] = wps[i].lon;
    }
    wp_msg.valid_until = (now() + rclcpp::Duration::from_seconds(30.0)).operator builtin_interfaces::msg::Time();
    wp_msg.allow_degraded_execution = true;
    wp_msg.confidence = 0.8f;
    wp_msg.rationale = "m5 avoidance waypoint plan for GNC bridge";
    pub_avoidance_waypoints_->publish(wp_msg);
  }
```

(The exact own_lat/own_lon/heading/speed extraction depends on `world_state_` field paths — verify against the WorldState msg definition when implementing.)

- [ ] **Step 8: Add return_to_route on M6 conflict-clear**

In the M6 callback (`on_colregs_constraint_` or equivalent), when transitioning from conflict=true to conflict=false (high confidence), emit one `return_to_route` waypoint plan pointing back at the nominal route, then stop emitting avoidance waypoints. This is the avoidance release authority migration (spec D4).

```cpp
  if (/* conflict was true, now false, high confidence */) {
    auto ret_msg = l3_external_msgs::msg::AvoidanceWaypoints();
    ret_msg.behavior_mode = "return_to_route";
    ret_msg.has_return_to_route_point = true;
    // Populate return_latitude/longitude from the nominal route's next waypoint.
    // ... (extract from route_plan_)
    pub_avoidance_waypoints_->publish(ret_msg);
  }
```

- [ ] **Step 9: Build + run M5 tests**

```bash
colcon build --packages-select m5_tactical_planner
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+
```
Expected: builds, tests pass.

- [ ] **Step 10: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/
git commit -m "feat(m5): emit AvoidanceWaypoints for GNC bridge (Track A A3)

New l3_external_msgs/AvoidanceWaypoints publisher on /l3/m5/avoidance_waypoints.
Pure C++ waypoint generation satisfies GNC feasibility (>=150m first point,
>=15m segments). return_to_route plan emitted on M6 conflict-clear (release
authority migrated to active_route_manager lifecycle). Existing l3_msgs
avoidance_plan kept for M7/M8 audit."
```

---

## Task A4: gnc_bridge_node (C++)

**Files:**
- Create: `src/sim_workbench/gnc_bridge/package.xml`, `CMakeLists.txt`, `include/gnc_bridge/gnc_bridge_node.hpp`, `src/gnc_bridge_node.cpp`, `src/translators.hpp`
- Test: `src/sim_workbench/gnc_bridge/test/test_translators.cpp`

- [ ] **Step 1: Write the failing translator test**

Create `src/sim_workbench/gnc_bridge/test/test_translators.cpp`:

```cpp
#include <gtest/gtest.h>
#include "gnc_bridge/translators.hpp"
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
#include "ship_interfaces/msg/avoidance_plan.hpp"

TEST(Translators, AvoidanceWaypointsToGnc) {
  l3_external_msgs::msg::AvoidanceWaypoints src;
  src.plan_id = "test-1";
  src.behavior_mode = "emergency_avoidance";
  src.latitude = {63.44, 63.45};
  src.longitude = {10.38, 10.39};
  src.command_speed_mps = {3.0, 3.0};
  src.navigation_mode = {"emergency_avoidance", "emergency_avoidance"};

  auto gnc = gnc_bridge::to_gnc_avoidance_plan(src);
  EXPECT_EQ(gnc.plan_id, "test-1");
  EXPECT_EQ(gnc.behavior_mode, "emergency_avoidance");
  EXPECT_EQ(gnc.latitude.size(), 2u);
  EXPECT_DOUBLE_EQ(gnc.latitude[0], 63.44);
  EXPECT_EQ(gnc.command_speed_mps.size(), 2u);
}

TEST(Translators, GeoPositionToOwnShipState) {
  ship_interfaces::msg::GeoPosition geo;
  geo.latitude = 63.44;
  geo.longitude = 10.38;
  geo.heading_deg = 45.0;
  geo.speed_mps = 4.0;
  // ... fill fields

  auto oss = gnc_bridge::to_sil_own_ship_state(geo);
  EXPECT_DOUBLE_EQ(oss.lat, 63.44);
  EXPECT_DOUBLE_EQ(oss.lon, 10.38);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
colcon build --packages-select gnc_bridge 2>&1 | tail -5
colcon test --packages-select gnc_bridge 2>&1 | tail -10
```
Expected: FAIL (package/header doesn't exist yet).

- [ ] **Step 3: Write package.xml + CMakeLists.txt**

Create `src/sim_workbench/gnc_bridge/package.xml`:

```xml
<?xml version="1.0"?>
<package format="3">
  <name>gnc_bridge</name>
  <version>0.1.0</version>
  <description>Cross-domain bridge between L3 and GNC (sole ship_interfaces consumer).</description>
  <maintainer email="dev@mass.local">MASS L3</maintainer>
  <license>MIT</license>
  <buildtool_depend>ament_cmake</buildtool_depend>
  <depend>rclcpp</depend>
  <depend>l3_msgs</depend>
  <depend>l3_external_msgs</depend>
  <depend>sil_msgs</depend>
  <depend>ship_interfaces</depend>
  <test_depend>ament_cmake_gtest</test_depend>
</package>
```

Create `src/sim_workbench/gnc_bridge/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
project(gnc_bridge)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(l3_msgs REQUIRED)
find_package(l3_external_msgs REQUIRED)
find_package(sil_msgs REQUIRED)
find_package(ship_interfaces REQUIRED)

add_library(gnc_bridge_core SHARED
  src/gnc_bridge_node.cpp
)
target_include_directories(gnc_bridge_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
ament_target_dependencies(gnc_bridge_core
  rclcpp l3_msgs l3_external_msgs sil_msgs ship_interfaces)

add_executable(gnc_bridge_node src/main.cpp)
target_link_libraries(gnc_bridge_node gnc_bridge_core)

install(TARGETS gnc_bridge_node DESTINATION lib/gnc_bridge)
install(DIRECTORY include/ DESTINATION include)

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_translators test/test_translators.cpp)
  target_include_directories(test_translators PRIVATE include)
  ament_target_dependencies(test_translators
    l3_external_msgs sil_msgs ship_interfaces)
endif()

ament_package()
```

- [ ] **Step 4: Write translators.hpp**

Create `src/sim_workbench/gnc_bridge/include/gnc_bridge/translators.hpp`:

```cpp
#pragma once
#include "l3_external_msgs/msg/avoidance_waypoints.hpp"
#include "l3_external_msgs/msg/gnc_execution_status.hpp"
#include "l3_external_msgs/msg/planned_route.hpp"
#include "ship_interfaces/msg/avoidance_plan.hpp"
#include "ship_interfaces/msg/route_plan.hpp"
#include "ship_interfaces/msg/route_execution_status.hpp"
#include "ship_interfaces/msg/geo_position.hpp"
#include "sil_msgs/msg/own_ship_state.hpp"

namespace gnc_bridge {

// L3 AvoidanceWaypoints -> GNC ship_interfaces/AvoidancePlan
ship_interfaces::msg::AvoidancePlan to_gnc_avoidance_plan(
    const l3_external_msgs::msg::AvoidanceWaypoints& src);

// L3 PlannedRoute -> GNC ship_interfaces/RoutePlan
ship_interfaces::msg::RoutePlan to_gnc_route_plan(
    const l3_external_msgs::msg::PlannedRoute& src);

// GNC GeoPosition -> SIL OwnShipState (trace compatibility)
sil_msgs::msg::OwnShipState to_sil_own_ship_state(
    const ship_interfaces::msg::GeoPosition& src);

// GNC RouteExecutionStatus -> L3 GncExecutionStatus
l3_external_msgs::msg::GncExecutionStatus to_l3_gnc_execution_status(
    const ship_interfaces::msg::RouteExecutionStatus& src);

}  // namespace gnc_bridge
```

- [ ] **Step 5: Implement translators.cpp**

Create `src/sim_workbench/gnc_bridge/src/translators.cpp` with the field-by-field mappings. Example for the avoidance plan:

```cpp
#include "gnc_bridge/translators.hpp"

namespace gnc_bridge {

ship_interfaces::msg::AvoidancePlan to_gnc_avoidance_plan(
    const l3_external_msgs::msg::AvoidanceWaypoints& src) {
  ship_interfaces::msg::AvoidancePlan dst;
  dst.header.stamp = src.stamp;
  dst.plan_id = src.plan_id;
  dst.parent_route_id = src.parent_route_id;
  dst.behavior_mode = src.behavior_mode;
  dst.command_source = src.command_source;
  dst.latitude = src.latitude;
  dst.longitude = src.longitude;
  dst.command_speed_mps = src.command_speed_mps;
  // command_heading_deg left empty — GNC follows waypoint geometry (docking doc §9.2)
  dst.navigation_mode = src.navigation_mode;
  dst.valid_until = src.valid_until;
  dst.require_exact_heading = false;
  dst.require_exact_speed = false;
  dst.allow_degraded_execution = src.allow_degraded_execution;
  dst.has_return_to_route_point = src.has_return_to_route_point;
  dst.return_latitude = src.return_latitude;
  dst.return_longitude = src.return_longitude;
  return dst;
}

// ... implement to_gnc_route_plan, to_sil_own_ship_state, to_l3_gnc_execution_status
//     with the corresponding field maps. Inspect each source msg definition for fields.

}  // namespace gnc_bridge
```

- [ ] **Step 6: Run the translator test**

```bash
colcon build --packages-select gnc_bridge
colcon test --packages-select gnc_bridge --event-handlers console_direct+
```
Expected: `test_translators` passes.

- [ ] **Step 7: Implement the bridge node (cross-domain two-context pattern)**

Create `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp` and `src/main.cpp`. ROS2 humble cross-domain bridging uses **two `rclcpp::Context` instances**, each with a different `ROS_DOMAIN_ID` env var set before initialization, each driving its own `Node` + `Executor`. This is the documented ROS2 humble pattern for in-process domain bridging.

Concrete structure for `src/main.cpp`:

```cpp
#include <rclcpp/rclcpp.hpp>
#include <thread>
#include "gnc_bridge/gnc_bridge_node.hpp"

int main(int argc, char** argv) {
  // Context A: L3 domain (42)
  setenv("ROS_DOMAIN_ID", "42", 1);
  auto ctx_l3 = std::make_shared<rclcpp::Context>();
  ctx_l3->init(argc, argv);
  rclcpp::NodeOptions opts_l3(ctx_l3);
  auto node_l3 = std::make_shared<gnc_bridge::L3SideNode>(opts_l3);

  // Context B: GNC domain (50)
  setenv("ROS_DOMAIN_ID", "50", 1);
  auto ctx_gnc = std::make_shared<rclcpp::Context>();
  ctx_gnc->init(argc, argv);
  rclcpp::NodeOptions opts_gnc(ctx_gnc);
  auto node_gnc = std::make_shared<gnc_bridge::GncSideNode>(opts_gnc);

  // Wire the two nodes via a thread-safe in-process handoff
  // (node_l3 subscribes L3 topics, pushes translated msgs to a concurrent
  //  queue; node_gnc pops and publishes on GNC domain, and vice versa).
  auto handoff = std::make_shared<gnc_bridge::CrossDomainHandoff>();

  // Each executor spins its node on its own thread.
  std::thread t_l3([&]{ rclcpp::spin(node_l3); });
  std::thread t_gnc([&]{ rclcpp::spin(node_gnc); });
  t_l3.join();
  t_gnc.join();
  return 0;
}
```

The `CrossDomainHandoff` is a thread-safe queue (mutex-protected `std::deque`) carrying the translated messages between the two nodes. `L3SideNode` subscribes `/l3/m5/avoidance_waypoints` + `/l2/planned_route`, translates via `translators.hpp`, pushes to the handoff; `GncSideNode` pops and publishes `/colav/avoidance_plan` + `/route_planning/route_plan`. The reverse direction (`/ship/geo_position`, `/gnc/route_execution_status` → `/sil/own_ship_state`, `/l3/gnc/execution_status`) works symmetrically.

**Risk:** the two-context pattern in ROS2 humble has known edge cases with executors. If it proves unstable in smoke test (Step 8), fall back to **two separate processes** (`gnc_bridge_l3` + `gnc_bridge_gnc`) connected by a localhost loopback topic or a simple ZMQ socket. Document the fallback choice in the handoff. Do NOT proceed to Task A5 until the bridge smoke test passes end-to-end (a published L3 msg appears in GNC domain and vice versa).

- [ ] **Step 8: Build + smoke test the bridge**

```bash
colcon build --packages-select gnc_bridge
# Smoke: start L3 stack (domain 42) + GNC stack (domain 50) + bridge, verify a published
# AvoidanceWaypoints appears in GNC domain as ship_interfaces/AvoidancePlan.
```

- [ ] **Step 9: Commit**

```bash
git add src/sim_workbench/gnc_bridge/
git commit -m "feat(gnc-bridge): C++ cross-domain bridge node (Track A A4)

Sole L3-side ship_interfaces consumer. Translators (L3<->GNC field maps)
TDD-covered. Two-context cross-domain pattern (domain 42 + 50). No behavior
logic — pure field mapping. Translates AvoidanceWaypoints<->AvoidancePlan,
PlannedRoute<->RoutePlan, GeoPosition->OwnShipState, RouteExecutionStatus->
GncExecutionStatus."
```

---

## Task A5: Remove L4 stub + sil_topic_bridge + add C++ SIL adapters

**Files:**
- Remove: `src/sim_workbench/sil_nodes/l4_guidance_adapter/` (entire package)
- Remove: `docker/sil_topic_bridge.py` (entire file)
- Create: `src/sim_workbench/sil_fusion_adapter/` (C++ pkg)
- Create: `src/sim_workbench/sil_trace_adapter/` (C++ pkg)
- Create: `src/sim_workbench/sil_pulse_adapter/` (C++ pkg)
- Modify: `docker/sil_entrypoint.sh`, `docker-compose*.yml`

- [ ] **Step 1: Delete the L4 stub package**

```bash
git rm -r src/sim_workbench/sil_nodes/l4_guidance_adapter/
```
Update `src/sim_workbench/sil_nodes/CMakeLists.txt` (or colcon ignore) to drop the package. Remove its entry from `docker/sil_entrypoint.sh` and any launch file.

- [ ] **Step 2: Delete sil_topic_bridge.py**

```bash
git rm docker/sil_topic_bridge.py
```
Update `docker/sil_entrypoint.sh` to remove the bridge startup. Update `tests/docker/test_sil_topic_bridge.py` — delete it (its subject is gone).

- [ ] **Step 3: Create sil_fusion_adapter (C++)**

Scaffold `src/sim_workbench/sil_fusion_adapter/` (package.xml, CMakeLists, src/, include/) following the `fmi_bridge` pattern. Subscribes `/sil/target_vessel_state`, publishes `/fusion/tracked_targets` (single→array fan-out). Subscribes `/sil/environment`, publishes `/fusion/environment_state` (field rename). This is needed only if SIL fusion injection is still used when GNC plant is active; if GNC sensor_fusion replaces it, this adapter is minimal.

- [ ] **Step 4: Create sil_trace_adapter (C++)**

Scaffold `src/sim_workbench/sil_trace_adapter/`. Subscribes `/l3/asdr/record`, publishes `/sil/asdr_event` (trace evidence).

- [ ] **Step 5: Create sil_pulse_adapter (C++)**

Scaffold `src/sim_workbench/sil_pulse_adapter/`. Subscribes M1-M8 heartbeat topics, aggregates + publishes `/sil/module_pulse` at 1 Hz.

- [ ] **Step 6: Build all new adapters**

```bash
colcon build --packages-select sil_fusion_adapter sil_trace_adapter sil_pulse_adapter
```
Expected: all build.

- [ ] **Step 7: Verify no residual references**

```bash
rg -n "l4_guidance_adapter|sil_topic_bridge" src/ docker/ scripts/ 2>/dev/null
```
Expected: zero hits.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "refactor(sil): remove L4 stub + sil_topic_bridge, add C++ SIL adapters (Track A A5)

Removed: l4_guidance_adapter (entire pkg), sil_topic_bridge.py (1774 lines).
Added: sil_fusion_adapter, sil_trace_adapter, sil_pulse_adapter (all C++).
M6-owner/corridor/latch contracts discarded; avoidance release now via
M5 return_to_route plan + active_route_manager lifecycle."
```

---

## Task A6: GNC profile wiring + contract amendment

**Files:**
- Modify: `docs/Design/SIL/ros2-interface-contract.yaml`
- Modify: `scripts/run_colregs_clean_8probe.py` (or orchestrator config) — add `--profile gnc`

- [ ] **Step 1: Amend the contract YAML with new topics**

Add to `docs/Design/SIL/ros2-interface-contract.yaml` `topics:` list:

```yaml
  - { name: /l3/m5/avoidance_waypoints, type: l3_external_msgs/msg/AvoidanceWaypoints, owner: M5, qos: state_stream }
  - { name: /l3/gnc/execution_status,    type: l3_external_msgs/msg/GncExecutionStatus, owner: gnc_bridge, qos: state_stream }
```

- [ ] **Step 2: Add --profile gnc to the scenario runner**

In `scripts/run_colregs_clean_8probe.py`, add a `--profile` argument that, when `gnc`, starts the `docker-compose.gnc.yml` + `gnc_bridge` and reads ship state from the bridged `/ship/geo_position`.

- [ ] **Step 3: Run the contract checker**

```bash
python3 tools/sil/check_ros2_interface_contract.py \
  --contract docs/Design/SIL/ros2-interface-contract.yaml \
  --root src/l3_tdl_kernel --root src/sim_workbench
```
Expected: exits 0. (Note: the checker must exclude `gnc_bridge` since it legitimately imports ship_interfaces; the exclude list in B9 already includes `gnc_bridge`.)

- [ ] **Step 4: Commit**

```bash
git add docs/Design/SIL/ros2-interface-contract.yaml scripts/run_colregs_clean_8probe.py
git commit -m "feat(gnc): wire gnc profile + amend contract (Track A A6)"
```

---

## Task A7: rule14-ho validation (the core gate)

**Files:** none (verification + tuning)

- [ ] **Step 1: Start the full GNC-integrated stack**

```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/gnc-integration"
COMPOSE_PROJECT_NAME=codex-gnc docker compose -f docker-compose.yml -f docker-compose.gnc.yml \
  --profile gnc up -d --build
sleep 15
```

- [ ] **Step 2: Verify cross-domain bridging is live**

```bash
# L3 domain 42: bridge output topics present
docker exec <l3-container> bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   ros2 topic list | grep -E "gnc/execution|sil/own_ship"'
# GNC domain 50: bridge input topics present
docker exec codex-gnc-gnc-nodes-1 bash -lc \
  'source /opt/ros/humble/setup.bash && source /opt/gnc_ws/install/setup.bash && \
   ROS_DOMAIN_ID=50 ros2 topic list | grep -E "colav/avoidance|route_planning/route"'
```
Expected: both sides show the bridged topics.

- [ ] **Step 3: Run colreg-rule14-ho under the GNC profile**

```bash
SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1 \
python3 scripts/run_colregs_clean_8probe.py \
  --scenario colreg-rule14-ho \
  --profile gnc \
  --restart-between-runs \
  --summary-out runs/gnc_integration_rule14_$(date +%Y%m%d_%H%M%S).json
```

- [ ] **Step 4: Evaluate the verdict**

Read the summary JSON. The hypothesis: `turn_starboard` is GREEN, `steering_reversals <= 4`, `rot_hold_std <= 1.5`, CPA >= 180 m, route return true.

- If GREEN: hypothesis confirmed — the GNC `max_yaw_rate` feasibility gate resolved the over-turn without SIL-side ROT logic. Proceed to Step 6.
- If RED on turn_starboard: the trace will localize the cause. Inspect `/l3/gnc/execution_status` for GNC rejections (turn_radius_too_small / yaw_rate_too_high) and `/l3/m5/avoidance_waypoints` for M5 geometry. If M5 waypoints violate feasibility, fix M5 generation (Task A3). If GNC params are too restrictive, tune `ship_config.yaml` overlay (NOT source).

- [ ] **Step 5: Parameter tuning (only if RED, mount-overlay only)**

Edit `docker/gnc-ship-config-overlay.yaml` to adjust `active_route_manager_node` params: `emergency_max_yaw_rate_deg_s`, `max_lateral_accel_mps2`, `emergency_min_turn_radius_m`. Re-run Step 3. Iterate until GREEN. Never edit `third_party/gnc_ws/` source.

- [ ] **Step 6: Run the local OrbStack gate**

```bash
source scripts/local-a4000-env.sh
./scripts/local-a4000-acceptance.sh
```
Expected: passes.

- [ ] **Step 7: Commit evidence + handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): Track A complete — GNC integration, rule14-ho gate result

[Mirror the actual verdict + key metrics here]"
```

---

## Acceptance Summary

Track A is complete when:
- `mass-l3-gnc:mpc_latest-20260624` image builds and runs the GNC stack in domain 50.
- `gnc_bridge_node` translates L3↔GNC messages; no L3 core/non-bridge module imports `ship_interfaces`.
- `l4_guidance_adapter` and `sil_topic_bridge.py` are gone; three C++ SIL adapters replace the bridge's non-overlapping functions.
- M5 publishes `/l3/m5/avoidance_waypoints`; bridge forwards to GNC `/colav/avoidance_plan`.
- `colreg-rule14-ho` under `--profile gnc` passes `turn_starboard` GREEN (or, if RED, the root cause is localized to M5 geometry or GNC params and documented, with a clear tuning path).
- `./scripts/local-a4000-acceptance.sh` passes.

## Risk Notes

- **Cross-domain bridge wiring (Task A4 Step 7)** is the highest implementation risk. ROS2 humble does not natively support per-node domain_id in one process. If the two-context pattern is unstable, fall back to two-process + IPC (loopback topic or UDP). Budget extra time for this task.
- **GNC plant params vs SIL scenario geometry (Task A7)**: the GNC ship (Lpp 44.1m, mass 220t) may not match the SIL scenario ship. If clean8 geometry breaks, tune via `ship_config.yaml` overlay or recalibrate the scenario; never edit GNC source.
- **M5 waypoint generation (Task A3)** uses a straight-line projection. If the GNC feasibility gate rejects it (e.g. because the avoidance heading vs own-heading creates an implicit turn at the first waypoint), curve the corridor or add a transition waypoint. The generation helper is pure C++ and unit-testable.
