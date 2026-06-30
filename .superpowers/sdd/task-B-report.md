# Task B report — Mid-MPC primary route generation

status: DONE_WITH_CONCERNS

summary:
- Added RED/green unit tests for Mid-MPC tail-gate acceptance, CPA-worsening rejection, and stand-on biased-offset rejection.
- Wired M6-owned `primary_role` and `primary_preferred_direction` into M5 `MidMpcInput` acceptance; M5 consumes those semantics instead of re-judging COLREG role/side.
- Added tail-gate helper returning `nlp_tail_gate_failed` semantics for accepted/rejected NLP candidates, using M2 CPA covariance (`cpa_m - 3σ >= cpa_safe_m`) and risk opening.
- Wired MidMPC node horizon parameters from `mid_mpc.horizon_s`, `mid_mpc.n_steps`, `mid_mpc.dt_s`; set spec baseline default to N=18, dt=5s.
- Changed waypoint generator default/dense sampling to NLP step resolution and updated existing waypoint tests.

RED:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-b docker compose --project-directory /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && rm -rf /opt/ws/build/m5_tactical_planner /opt/ws/install/m5_tactical_planner && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && colcon test --packages-select m5_tactical_planner --ctest-args -R "test_midmpc_tail_gate|test_stand_on_reject" --event-handlers console_direct+'`
  - outcome: expected FAIL during compile: missing `accept_tail_gate`, `MidMpcInput::colregs_primary_role`, and `TargetState::cpa_sigma_m`.
- Host `colcon test ...` was attempted first and failed with `command not found: colcon`; Docker path was used for valid RED/GREEN evidence.

GREEN:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-b docker compose --project-directory /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && rm -rf /opt/ws/build/m5_tactical_planner /opt/ws/install/m5_tactical_planner && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && colcon test --packages-select m5_tactical_planner --ctest-args -R "test_midmpc_tail_gate|test_stand_on_reject|test_mid_mpc_waypoint_generator|test_avoidance_plan_contract|test_mid_mpc_nlp_formulation" --event-handlers console_direct+'`
  - outcome: PASS, 5/5 CTest targets green (`test_mid_mpc_nlp_formulation`, `test_mid_mpc_waypoint_generator`, `test_midmpc_tail_gate`, `test_stand_on_reject`, `test_avoidance_plan_contract`).

commit: 6c5bd0e4, ce8063be

concerns:
- Tail-gate helper implements the task-required tested semantics plus CPA uncertainty/opening/turn feasibility, but deeper deterministic tail checks (full future hold/rejoin geometry and explicit crossing-ahead against propagated target motion) remain approximate/not fully modeled in this slice.
- Docker build emits existing Boost deprecation note from `constraint_compiler.cpp`; tests still pass.


## Review Fix 1

status: DONE_WITH_CONCERNS

findings fixed:
- `mid_mpc.horizon_s` now affects resolved NLP horizon when `mid_mpc.n_steps` remains at the config/default step count; explicit non-default `n_steps` still wins and remains capped at 120.
- Tail gate preserves raw M2 target CPA/covariance in `tail_gate_targets` before optimizer CPA/TCPA weighting, so release checks use raw M2 CPA/sigma rather than cost-weighted values.
- Tail gate now rejects candidate tails that cross ahead of the primary target, exceed decel feasibility, or require an instantaneous first-step heading jump from current own-ship heading.
- Stand-on active-conflict degraded publish path clears fallback corridor/terminal-hold fields instead of publishing `MID_MPC_TERMINAL_HOLD` for stand-on hold.
- Existing `nlp_tail_gate_failed` contract remains true for degraded non-return plans, but the stand-on hold suppression publishes no failed tail because no tail is emitted.

RED:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-b docker compose --project-directory /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && rm -rf /opt/ws/build/m5_tactical_planner /opt/ws/install/m5_tactical_planner && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && colcon test --packages-select m5_tactical_planner --ctest-args -R "test_midmpc_tail_gate|test_stand_on_reject|test_mid_mpc_nlp_formulation" --event-handlers console_direct+'`
  - outcome: expected FAIL during compile on missing `resolve_mid_mpc_horizon_config` after RED tests were added.
- Same focused command after initial implementation:
  - outcome: expected FAIL in two tests: `TailGate.RejectsTrajectoryThatCrossesAheadOfPrimaryTarget` and `StandOnReject.DegradedPublishDoesNotMarkTerminalHoldForStandOnConflict`; fixed by tightening crossing fixture/guard and applying stand-on publish contract unconditionally for active stand-on conflict.

GREEN:
- `COMPOSE_PROJECT_NAME=codex-colregs-12probe-b docker compose --project-directory /Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug run --rm --no-deps sil-nodes bash -lc 'source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -Dcasadi_DIR=/usr/local/lib/python3.10/dist-packages/casadi/cmake && colcon test --packages-select m5_tactical_planner --ctest-args -R "test_midmpc_tail_gate|test_stand_on_reject|test_mid_mpc_waypoint_generator|test_avoidance_plan_contract|test_mid_mpc_nlp_formulation" --event-handlers console_direct+'`
  - outcome: PASS, 5/5 CTest targets green (`test_mid_mpc_nlp_formulation`, `test_mid_mpc_waypoint_generator`, `test_midmpc_tail_gate`, `test_stand_on_reject`, `test_avoidance_plan_contract`).

commit: cf631de5

concerns:
- No full ROS publish/subscriber runtime test was added; stand-on terminal-hold suppression is covered through the extracted publish-contract helper used directly by `publish_avoidance_waypoints_`.
- No-crossing-ahead gate is an explicit local route-frame target-propagation check, not a full multi-target encounter simulator.
- Docker build still emits the pre-existing Boost deprecation note from `constraint_compiler.cpp`; tests pass.
