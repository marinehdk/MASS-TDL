## 2026-07-01 — Task C implementer report

status: DONE_WITH_CONCERNS
summary:
- Added TailBuilder public API and RouteFrame utility for terminal hold + rejoin tails.
- Implemented give-way hold/rejoin generation with `MID_MPC_TERMINAL_HOLD` and `REJOIN_TO_L2` labels.
- Stand-on returns no tail.
- Rejoin gate consumes M6 `past_clear` / `encounter_state` only; target bearing is ignored and no abaft/past-clear semantics are recalculated in M5.
- CPA release floor uses M2 CPA/covariance snapshots (`cpa_m - 3σ >= cpa_release_m`).
- Added focused unit tests for give-way starboard hold/rejoin, stand-on no tail, M6 past-clear gating, and l_hold too small reject.

RED:
- Stalled implementer wrote headers/tests/CMake first; before implementation, `test_tail_builder` could not link because `tail_builder.cpp` / RouteFrame definitions were missing.

GREEN:
- `rtk docker compose run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --symlink-install --packages-select l3_msgs m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && source install/setup.bash && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ --ctest-args -R test_tail_builder --output-on-failure && colcon test-result --verbose'`
  - PASS: `test_tail_builder` 4/4, package result 5 tests, 0 errors, 0 failures, 0 skipped.

concerns:
- TailBuilder is a standalone library in Slice C; runtime integration is left to later committed-route manager/adapters slices.
- Route frame uses local metric `x_m/y_m` TailBuilder-local `GeoWP`; WGS84 conversion/integration remains caller responsibility.
- Per-waypoint checks are covered at Slice C unit seam for M6 side, M6 release, CPA release floor, route continuity, and labels; full ship-domain/no-crossing runtime proof remains for integration slices.

## 2026-07-01 — Review Fix 1

status: DONE_WITH_CONCERNS
commit: 2f163a76

findings fixed:
- Critical 1: `TailBuilder::build()` now rejects empty or invalid M2 target snapshots with `missing_m2_targets` before accepting M6 release/clear. CPA evidence must be finite and have nonnegative sigma.
- Critical 2: generated hold/rejoin tails now pass explicit per-waypoint validation before emission: M2 CPA release floor, ship-domain floor, source-label/waypoint cardinality, finite nonnegative speeds, route projection, M6-side preservation until final rejoin tolerance, final route rejoin, non-reverse station, 50-150 m waypoint spacing, and min-turn-radius continuity. Unsafe tails return nonempty `reject_reason` instead of silently emitting.
- Important 3: TailBuilder now rejects route frames with zero-length segments or >45 degree heading corners (`route_frame_sharp_corner`) rather than sampling across abrupt polyline normal jumps.
- Important 4: Added unit coverage for empty M2 targets, CPA release-floor rejection, ship-domain floor rejection, sharp route-corner rejection, and invalid waypoint-speed rejection. Existing M6 authority test still proves TailBuilder consumes M6 release/past_clear and does not self-compute abaft/past-clear semantics.

RED:
- `docker compose -f /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/docker-compose.yml run --rm sil-nodes bash -lc 'cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && /opt/ws/build/m5_tactical_planner/test_tail_builder --gtest_filter=TailBuilder.*'`
  - Expected FAIL before production changes: `rejects_empty_m2_target_snapshot`, `rejects_route_frame_with_sharp_corner`, and `rejects_unsafe_tail_waypoint_speed` failed because unchecked tails were emitted.
- `docker compose -f /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/docker-compose.yml run --rm sil-nodes bash -lc 'cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && /opt/ws/build/m5_tactical_planner/test_tail_builder --gtest_filter=TailBuilder.rejects_target_below_ship_domain_floor'`
  - Expected FAIL before ship-domain floor implementation: tail emitted with `cpa_m - sigma < cpa_safe_m`.

GREEN:
- `git -C /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug diff --check -- src/l3_tdl_kernel/m5_tactical_planner/src/tail_builder/tail_builder.cpp src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_tail_builder.cpp`
  - PASS: no whitespace errors.
- `docker compose -f /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/docker-compose.yml run --rm sil-nodes bash -lc 'cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && /opt/ws/build/m5_tactical_planner/test_tail_builder --gtest_filter=TailBuilder.* && /opt/ws/build/m5_tactical_planner/test_midmpc_tail_gate --gtest_filter=* && /opt/ws/build/m5_tactical_planner/test_mid_mpc_waypoint_generator --gtest_filter=*'`
  - PASS: TailBuilder 9/9, TailGate 6/6, MidMpcWaypointGenerator 11/11. Build emitted existing Boost deprecation note from `boost/detail/no_exceptions_support.hpp`.

concerns:
- Route-frame continuity fix is intentionally conservative: TailBuilder rejects sharp route corners rather than smoothing normals. Broader route smoothing remains out of Slice C.
- Numeric no-crossing-ahead beyond M6 side preservation remains limited at this seam because TailBuilder does not own target-relative side semantics; runtime integration/M7 checks must still police full encounter geometry.
