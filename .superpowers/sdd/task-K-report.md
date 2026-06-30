
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

concerns:
- Host lacks `colcon`; all ROS build/test commands were run in Docker via `sil-nodes`.
- Worktree had pre-existing unrelated dirty docs/handoff/untracked files before Slice K; commit should stage only Slice K files.
