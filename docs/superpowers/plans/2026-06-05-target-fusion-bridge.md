# Target Fusion Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create a C++ bridge node to adapt external sensor fusion target messages into the internal format, and upgrade internal target schemas to support covariance matrix separation and ship dimensions.

**Architecture:** A new ROS2 node (`fusion_target_bridge`) subscribes to `/external/tracked_targets`, converts units and scales covariance, and publishes to `/fusion/tracked_targets`. Existing `m2_world_model` updates its covariance field references to match the new schema.

**Tech Stack:** C++17, ROS2 (rclcpp), ament_cmake, GTest.

---

### Task 1: Upgrade Message Schema

**Files:**
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/TrackedTarget.msg`

- [ ] **Step 1: Modify TrackedTarget.msg**

```text
# (In src/l3_tdl_kernel/l3_msgs/msg/TrackedTarget.msg)
# Remove: float64[9] covariance
# Add the following:
uint32 mmsi
float64 length
float64 beam
float64[4] position_covariance
float64[4] velocity_covariance
```

- [ ] **Step 2: Rebuild l3_msgs to verify schema compiles**

Run: `colcon build --packages-select l3_msgs`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add src/l3_tdl_kernel/l3_msgs/msg/TrackedTarget.msg
git commit -m "feat(l3_msgs): upgrade TrackedTarget schema with dimensions and separate covariance"
```

### Task 2: Adapt M2 World Model

**Files:**
- Modify: `src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/src/cpa_tcpa_calculator.cpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/test/test_cpa_tcpa_calculator.cpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/test/test_world_state_aggregator.cpp`

- [ ] **Step 1: Fix CpaTcpaCalculator compile errors**

In `src/l3_tdl_kernel/m2_world_model/src/cpa_tcpa_calculator.cpp`, replace `target.covariance` references. Use `target.position_covariance` for position variance, and `target.velocity_covariance` for velocity variance.

- [ ] **Step 2: Fix WorldStateAggregator compile errors**

In `src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp`, update any `.covariance` array mapping logic to populate `.position_covariance` and `.velocity_covariance`.

- [ ] **Step 3: Update M2 Tests**

In `test_cpa_tcpa_calculator.cpp` and `test_world_state_aggregator.cpp`, update the mock `TrackedTarget` initialization to use `position_covariance` and `velocity_covariance` instead of `covariance`. Add assertions for `mmsi`, `length`, and `beam`.

- [ ] **Step 4: Run M2 tests to verify**

Run: `colcon test --packages-select m2_world_model && colcon test-result --all`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m2_world_model
git commit -m "refactor(m2): adapt to separated covariance fields in TrackedTarget"
```

### Task 3: Create Fusion Target Bridge Node

**Files:**
- Create: `src/l3_tdl_kernel/fusion_target_bridge/CMakeLists.txt`
- Create: `src/l3_tdl_kernel/fusion_target_bridge/package.xml`
- Create: `src/l3_tdl_kernel/fusion_target_bridge/src/fusion_target_bridge_node.cpp`

- [ ] **Step 1: Scaffold ROS2 Package**

Create `package.xml` depending on `rclcpp`, `l3_external_msgs`, `l3_msgs`. Note: assume `nmea_interfaces` is locally available in the workspace or system. (If not, mock it for now). Create `CMakeLists.txt` defining the `fusion_target_bridge_node` executable.

- [ ] **Step 2: Implement Bridge Node Logic**

In `src/fusion_target_bridge_node.cpp`:
- Subscribe to `/external/tracked_targets` (`nmea_interfaces::msg::TrackedTargetArray` if available, otherwise just implement the structure mapping conceptually if the header isn't present in `src/`).
- Iterate over targets.
- Map `track_id` -> `target_id`.
- Map `mmsi`, `length`, `beam`.
- Map `position_covariance` and `velocity_covariance` directly.
- Convert `sog` (m/s) to `sog_kn`: `internal_target.sog_kn = external_target.sog * 1.94384;`
- Publish `l3_external_msgs::msg::TrackedTargetArray` to `/fusion/tracked_targets`.

- [ ] **Step 3: Build Bridge Package**

Run: `colcon build --packages-select fusion_target_bridge`
Expected: PASS

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/fusion_target_bridge
git commit -m "feat(bridge): implement nmea_interfaces to l3_external_msgs translation node"
```

### Task 4: Add Bridge Node Tests

**Files:**
- Create: `src/l3_tdl_kernel/fusion_target_bridge/test/test_fusion_target_bridge.cpp`

- [ ] **Step 1: Write Unit Test**

Write a GTest verifying that an input message with `sog = 10.0` m/s results in an output message with `sog_kn = 19.4384`. Verify `position_covariance` and `velocity_covariance` pass through correctly.

- [ ] **Step 2: Run test**

Run: `colcon test --packages-select fusion_target_bridge && colcon test-result --all`
Expected: PASS

- [ ] **Step 3: Commit**

```bash
git add src/l3_tdl_kernel/fusion_target_bridge/test src/l3_tdl_kernel/fusion_target_bridge/CMakeLists.txt
git commit -m "test(bridge): add unit tests for kinematic unit conversion"
```
