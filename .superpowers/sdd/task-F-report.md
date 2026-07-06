# Task F Report: GNC feasibility preflight

## RED
- `colcon build --packages-select m5_tactical_planner --cmake-force-configure --cmake-args -DBUILD_TESTING=ON --event-handlers console_direct+` inside `codex-gnc-validation-sil-nodes-1` failed as expected: `fatal error: m5_tactical_planner/gnc_preflight.hpp: No such file or directory`.
- Host `colcon` was unavailable; container uses ROS Humble and mounts this worktree at `/opt/ws/src`.

## GREEN
- `colcon build --packages-select m5_tactical_planner --cmake-force-configure --cmake-args -DBUILD_TESTING=ON --event-handlers console_direct+` inside `codex-gnc-validation-sil-nodes-1`: PASS.
- `colcon test --packages-select m5_tactical_planner --ctest-args -R test_gnc_preflight --output-on-failure && colcon test-result --test-result-base build/m5_tactical_planner --verbose`: PASS, 17 tests, 0 failures.
- Nearby: `colcon test --packages-select m5_tactical_planner --ctest-args -R "test_(gnc_preflight|tail_builder|committed_route|degraded_candidate_adapter)" --output-on-failure && colcon test-result --test-result-base build/m5_tactical_planner --verbose`: PASS, 47 tests, 0 failures.

## Changes
- Added `m5_tactical_planner/gnc_preflight.hpp` and `src/gnc_preflight.cpp`.
- Added `test/unit/test_gnc_preflight.cpp`.
- Wired preflight source and test target into M5 CMake.
- Extended `GncExecutionOdd` with preflight-owned constraints: max decel, first-changed distance, and min update interval.

## Commit
- `bd7381e7` (`feat(m5): GNC feasibility preflight (Slice F, spec v2 §11)`).

## Concerns
- Host lacks `colcon`; verification used existing `codex-gnc-validation-sil-nodes-1` container mounted to this worktree.
- Preflight is a P4 M5 doer self-check only; protected COLREG exception consumes only explicit flag/side/metadata and does not recalculate COLREG role/side/past-clear semantics.


## Review Fix: 2D lateral delta + ODD bound validation

### RED
- `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON --event-handlers console_direct+ && colcon test --packages-select m5_tactical_planner --ctest-args -R test_gnc_preflight --output-on-failure && colcon test-result --test-result-base build/m5_tactical_planner --verbose'`: expected FAIL after adding review regressions. New failing tests: `rejects_lateral_delta_too_large_in_x_axis`, `rejects_nonfinite_max_decel_odd`, `rejects_nonfinite_turn_radius_odd`, `rejects_nonpositive_lateral_offset_odd`.

### GREEN
- Same build + `test_gnc_preflight`: PASS, 48 tests, 0 failures.
- Nearby regression: `colcon test --packages-select m5_tactical_planner --ctest-args -R "test_(gnc_preflight|tail_builder|committed_route|degraded_candidate_adapter)" --output-on-failure && colcon test-result --test-result-base build/m5_tactical_planner --verbose`: PASS, 51 tests, 0 failures.

### Fix
- `max_lateral_delta` now compares Euclidean waypoint displacement against `odd.max_lateral_offset_m`, so corresponding waypoint shifts in x or y are rejected unless a valid protected COLREG exception is active.
- `validate()` now rejects nonfinite/nonpositive `GncExecutionOdd` positive bounds before using them in segment, turn, yaw-rate, lateral-accel, decel, first-change, or update-interval checks.

### Commit
- `0e2311e0` (`fix(m5): harden GNC preflight review gaps`).
