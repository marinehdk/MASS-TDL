
## 2026-07-01 — Task K implementer report

status: DONE_WITH_CONCERNS
summary:
- Wired M7 `/l3/m5/avoidance_plan` runtime policing: extracts canonical speed/route geometry/NLP status, derives route heading/DCPA from available plan/world data, invokes HC-1~6, and publishes non-INFO SafetyAlert.
- Added NLP status monitoring: FaultMonitor observes any non-converged/tail-gate failure; AssumptionMonitor escalates SOTIF after 3 consecutive non-converged statuses or immediate tail-gate failure.
- Added `l3_risk_model` independence allowlist rationale and corrected sliding-window comment to 100 samples @ 4 Hz = 25 s.
- Fixed HC-4 diagnostic coverage aggregation so all self-checks pass existing >=90% DC contract instead of false-failing runtime policing.

RED:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-debug docker compose run --rm sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-up-to m7_safety_supervisor --cmake-args -DBUILD_TESTING=ON && source /opt/ws/install/setup.bash && colcon test --packages-select m7_safety_supervisor --event-handlers console_direct+'`
  - Expected failure after rebuilding Slice A message deps: compile failed because `AssumptionMonitor::check_nlp_convergence`, `AssumptionId::kNlpConvergence`, and `FaultMonitor::observe_nlp_status` did not exist.

GREEN:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-debug docker compose run --rm sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-up-to m7_safety_supervisor --cmake-args -DBUILD_TESTING=ON >/tmp/m7-build.log && source /opt/ws/install/setup.bash && cd /opt/ws/build/m7_safety_supervisor && ctest -R "test_(nlp_status_monitor|hard_constraint_runtime|hard_constraint_dc)$" --output-on-failure'`
  - PASS: 3/3 tests (`test_hard_constraint_runtime`, `test_nlp_status_monitor`, `test_hard_constraint_dc`).
- `bash tools/ci/check-doer-checker-independence.sh`
  - PASS: OK; reports `l3_risk_model` allowlist as deterministic geometry library, no doer reasoning path shared.

full gate:
- `colcon test --packages-select m7_safety_supervisor` in Docker/ROS still fails on pre-existing unrelated test `MrmSelectorTest.ChangeWithin30s_KeepsLastMrm` (`Expected dec2.confidence < dec1.confidence, actual 0.5 vs 0.5`). Slice K targeted tests pass.

## 2026-07-01 — Review Fix 1

status: DONE
summary:
- Fixed reviewer Important finding: M7 runtime ROT policing now checks commanded avoidance route ROT derived from canonical `AvoidancePlan` geometry, not measured own-ship `WorldState.r_dot_deg_s`.
- Added regression `HardConstraintRuntime.CommandedRotViolationTriggersAlertWhenMeasuredRotIsSafe` proving unsafe commanded ROT raises HC-6 even when measured ROT is safe.

RED:
- Reviewer identified the missing test/bug: prior runtime path set `last_avoidance_rot_` from measured world ROT, so a commanded route turn could evade HC-6 when current measured ROT was still safe.

GREEN:
- `rtk docker compose run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --symlink-install --packages-select l3_msgs l3_external_msgs m7_safety_supervisor --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && colcon test --packages-select m7_safety_supervisor --event-handlers console_direct+ --ctest-args -R test_hard_constraint_runtime --output-on-failure && colcon test-result --verbose'`
  - PASS: `test_hard_constraint_runtime` 2/2 tests, 0 failures; package result 3 tests, 0 errors, 0 failures, 0 skipped.
- `bash tools/ci/check-doer-checker-independence.sh`
  - PASS: `Doer-Checker independence: OK`; optional `cloc`/`lizard`/`syft` checks skipped because tools absent.

concerns:
- Full M7 suite still not rerun after this focused fix; prior full-suite failure was unrelated `MrmSelectorTest.ChangeWithin30s_KeepsLastMrm`.
