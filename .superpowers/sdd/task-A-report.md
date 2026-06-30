# Task A Report — M5 AvoidancePlan interface unification

## Status

CODE GREEN and committed.

## Summary of code changes

- Extended `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/src/l3_tdl_kernel/l3_msgs/msg/AvoidancePlan.msg` to schema v114 while preserving `stamp`, `schema_version`, `confidence`, and `rationale`.
- Added exactly five segment source labels: `MID_MPC_OPTIMIZED`, `MID_MPC_TERMINAL_HOLD`, `REJOIN_TO_L2`, `L2_NOMINAL_SUFFIX`, `DEGRADED_CORRIDOR`.
- Added committed-route execution/audit fields: `route_hash`, `stale_committed_at`, `nlp_solver_status`, `nlp_kkt_residual`, `nlp_tail_gate_failed`, route arrays, validity, degraded execution, and return-to-route hints.
- Added M5 contract test at `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_avoidance_plan_contract.cpp`; RED verified before implementation via missing-field compile failure.
- Updated M5 Mid-MPC node so `/l3/m5/avoidance_plan` is the event-driven committed-route execution truth and `/l3/m5/avoidance_waypoints` remains compatibility shadow.
- Added route hash over route geometry plus execution-critical metadata and 60 s heartbeat/`valid_until` refresh; no shortening of validity semantics.
- Migrated `gnc_bridge` to subscribe to `/l3/m5/avoidance_plan` and translate `l3_msgs/AvoidancePlan` to `ship_interfaces/AvoidancePlan`; added 60 s heartbeat watchdog warning.
- Updated frontend telemetry normalization in `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web/src/store/telemetryStore.ts` to prefer committed route arrays and expose segment source + NLP audit fields.

## Tests

- RED: `test_avoidance_plan_contract` compile failed before schema implementation because `segment_source`, route arrays, route hash, and NLP status fields/constants did not exist.
- GREEN: Docker `sil-nodes` build/test with regenerated `l3_msgs`:
  - `colcon test --packages-select m5_tactical_planner gnc_bridge`
  - Result: `249 tests, 0 errors, 0 failures, 59 skipped`.
- Frontend:
  - `npm --prefix "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web" ci`
  - `npm --prefix "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web" run build`
  - Result: build passed. Existing Vite warnings remain: Foxglove serialization uses eval; bundle chunk exceeds 500 kB.

## Commit hash(es)

- `bbff9fea` — `feat(m5): unify AvoidancePlan as execution truth`

## Self-review notes and concerns

- M6 authority preserved: no new M5 COLREGs side/role/past-clear judgment; M5 consumes existing behavior/constraint signals and route geometry.
- `AvoidancePlan` now uses schema v114 as requested; no monotonic bump beyond v114 was needed.
- Segment label constants intentionally use the exact requested names without `SEG_` prefix.
- NLP audit fields are Slice A plumbing. Current committed-route publisher marks geometric/shadow-generated route snapshots as `NLP_NONCONVERGED`, `nlp_kkt_residual=0.0`, and `nlp_tail_gate_failed` true except return-to-route; Slice B/K should wire richer solver diagnostics.
- `stale_committed_at` field exists in the schema but no keep-last stale-entry semantics are implemented in Slice A.
- Local Docker containers are ephemeral, so ROS tests rebuilt `l3_msgs` in the same container process before M5/GNC verification.
- Unrelated pre-existing dirty/untracked files remained unstaged and were not committed.
## Review Fix

Status: DONE

Fixes:
- Reworked M5 `/l3/m5/avoidance_plan` emission so the canonical `AvoidancePlan` route snapshot is built first, with per-waypoint `segment_source`, explicit non-stale `stale_committed_at=0`, route hash publication via existing publisher path, and legacy `/l3/m5/avoidance_waypoints` derived as a compatibility shadow.
- Added L2 nominal suffix append support when a planned route is available, labelled `L2_NOMINAL_SUFFIX`, while preserving M6-exclusive COLREGs semantics and avoiding scenario/threshold tuning.
- Extended `ship_interfaces/msg/AvoidancePlan.msg` with schema/source timestamp, confidence/rationale, segment source, route hash, stale timestamp, and NLP audit fields; `gnc_bridge` translator now preserves them.
- Added HMI audit visibility by exposing segment source plus route/NLP audit fields in avoidance route GeoJSON properties.

RED/GREEN tests:
- RED: `npm --prefix /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web test -- --run src/map/__tests__/AvoidanceRouteLayer.test.tsx` failed with `buildGeoJSON is not a function` before production web change.
- GREEN: `docker compose run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --symlink-install --packages-select l3_msgs ship_interfaces m5_tactical_planner gnc_bridge --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && source install/setup.bash && colcon test --packages-select l3_msgs ship_interfaces m5_tactical_planner gnc_bridge --event-handlers console_direct+ --ctest-args -R "test_avoidance_plan_contract|test_translators" && colcon test-result --verbose'` passed: 4 packages built, 15 tests, 0 failures.
- GREEN: `npm --prefix /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web test -- --run src/map/__tests__/AvoidanceRouteLayer.test.tsx` passed: 6 tests.
- GREEN: `npm --prefix /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug/web run build` passed; existing Vite warnings about foxglove eval and chunk size remain.

Commit: 3907fa19 (fix(m5): preserve avoidance plan audit handoff).

Concerns:
- Slice D still owns real keep-last/stale transition semantics; Slice A only publishes explicit non-stale zero timestamps.
- `l3_msgs` and `ship_interfaces` have no package-local tests; verification coverage is through build plus `m5_tactical_planner`/`gnc_bridge` consumers.
