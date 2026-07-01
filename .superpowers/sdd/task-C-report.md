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
