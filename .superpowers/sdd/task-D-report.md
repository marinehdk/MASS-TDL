# Task D Report: Committed route manager

## Status
DONE_WITH_CONCERNS

## Summary
- Added standalone `CommittedAvoidanceRoute` manager with local `GeoWP`, candidate/state structs, 8-state lifecycle enum, route hash/revision tracking, prefix freeze, keep-last state, and safety concern event string for future M7 integration.
- Added heartbeat refresh that updates `valid_until_s` / `plan_id` / stale timestamp without changing revision or route hash.
- Added DegradedHold gates for stale committed route >45s, NLP consecutive failures >=3, target heading delta >15 deg, CPA drift >20%, and current CPA below hard floor.
- Registered `src/committed_route/committed_route.cpp` in `m5_shared_lib` and added `test_committed_route` gtest.

## RED Evidence
- Command: `docker compose -f /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/docker-compose.yml run --rm sil-nodes bash -lc 'cd /opt/ws && colcon build --build-base /tmp/m5_slice_d_build --install-base /tmp/m5_slice_d_install --packages-up-to m5_tactical_planner'`
- Outcome: FAIL as expected before production code.
- Failure: `fatal error: m5_tactical_planner/committed_route/committed_route.hpp: No such file or directory` from `test/unit/test_committed_route.cpp`.

## GREEN Evidence
- Command: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/${ROS_DISTRO:-jazzy}/setup.bash && rm -rf /tmp/m5_slice_d_existing_build /tmp/m5_slice_d_existing_install && cd /opt/ws && colcon build --build-base /tmp/m5_slice_d_existing_build --install-base /tmp/m5_slice_d_existing_install --packages-up-to m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && cd /tmp/m5_slice_d_existing_build/m5_tactical_planner && ctest -R "test_(committed_route|tail_builder|avoidance_plan_contract)" --output-on-failure'`
- Outcome: PASS.
- Tests: `test_tail_builder`, `test_committed_route`, `test_avoidance_plan_contract`; 3/3 passed, 0 failed.

## Changed Paths
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/committed_route/committed_route.hpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/committed_route/committed_route.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_committed_route.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt`
- `.superpowers/sdd/task-D-report.md`

## Commit
57969d1f

## Concerns
- Unit-level only: safety concern is recorded as `safety_concern_event`; no M7 DDS publication/wiring in this Slice D, per hard constraint.
- Uses standalone `double` seconds rather than `rclcpp::Time` to keep manager library light while preserving exact seconds semantics in tests.
- Existing worktree had unrelated dirty/untracked files before this task; commit should stage only Slice D files.


## Review Fix: Critical/Important Findings

### Summary
- Fixed heartbeat so DegradedHold persists: heartbeat now refuses to refresh state/timers or clear `safety_concern_event` while in DegradedHold.
- Fixed frozen prefix monotonicity: later revisions must preserve the existing committed prefix exactly; candidate `frozen_prefix_count` can extend but cannot shrink the existing prefix, and conflicts are rejected without revision/hash change.
- Fixed §9.12 risk triggers for failed NLP candidates: hard CPA, heading delta, and CPA drift now enter DegradedHold immediately even when `nlp_ok=false`; ordinary first/second NLP failures still keep last route, third failure enters DegradedHold.
- Added successful-candidate preflight: non-empty geometry, finite valid_until/x/y/speed, and source label contract are required before commit/revision/hash mutation.
- Encoded five allowed `GeoWP.nav_mode` labels: `MID_MPC_OPTIMIZED`, `MID_MPC_TERMINAL_HOLD`, `REJOIN_TO_L2`, `L2_NOMINAL_SUFFIX`, `DEGRADED_CORRIDOR`.
- Added regression coverage for all reviewer Critical/Important findings plus valid revised-route recovery from DegradedHold.

### RED Evidence
- Command: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/${ROS_DISTRO:-jazzy}/setup.bash && cd /opt/ws && colcon build --build-base /tmp/m5_slice_d_review_red_build --install-base /tmp/m5_slice_d_review_red_install --packages-up-to m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && cd /tmp/m5_slice_d_review_red_build/m5_tactical_planner && ctest -R "test_committed_route" --output-on-failure'`
- Outcome: FAIL as expected, 5/10 tests failed: heartbeat cleared DegradedHold, frozen prefix shrank, failed NLP skipped risk triggers, empty/invalid candidate committed, invented label committed.
- Command: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/${ROS_DISTRO:-jazzy}/setup.bash && cd /opt/ws && colcon build --build-base /tmp/m5_slice_d_review_recovery_red_build --install-base /tmp/m5_slice_d_review_recovery_red_install --packages-up-to m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && cd /tmp/m5_slice_d_review_recovery_red_build/m5_tactical_planner && ctest -R "test_committed_route" --output-on-failure'`
- Outcome: FAIL as expected, 1/11 failed: valid revised route could not exit DegradedHold due to an over-strict guard.

### GREEN Evidence
- Command: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/${ROS_DISTRO:-jazzy}/setup.bash && cd /opt/ws && colcon build --build-base /tmp/m5_slice_d_review_final_build --install-base /tmp/m5_slice_d_review_final_install --packages-up-to m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && cd /tmp/m5_slice_d_review_final_build/m5_tactical_planner && ctest -R "test_(committed_route|tail_builder|avoidance_plan_contract)" --output-on-failure'`
- Outcome: PASS.
- Tests: `test_tail_builder`, `test_committed_route`, `test_avoidance_plan_contract`; 3/3 passed, 0 failed.

### Changed Paths
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/committed_route/committed_route.hpp`
- `src/l3_tdl_kernel/m5_tactical_planner/src/committed_route/committed_route.cpp`
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_committed_route.cpp`
- `.superpowers/sdd/task-D-report.md`

### Commit
- Pending at report append time.

### Concerns
- Unit-level only; no M7 DDS publication/wiring added in this review fix.
- Pre-existing unrelated dirty/untracked files remain in the worktree; commit stages only Slice D files and this report.
