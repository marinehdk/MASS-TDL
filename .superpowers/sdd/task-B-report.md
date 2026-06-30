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

commit: TBD

concerns:
- Tail-gate helper implements the task-required tested semantics plus CPA uncertainty/opening/turn feasibility, but deeper deterministic tail checks (full future hold/rejoin geometry and explicit crossing-ahead against propagated target motion) remain approximate/not fully modeled in this slice.
- Docker build emits existing Boost deprecation note from `constraint_compiler.cpp`; tests still pass.
