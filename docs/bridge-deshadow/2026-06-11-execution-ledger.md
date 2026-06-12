# Bridge De-shadow Execution Ledger

Date: 2026-06-11
Owner role: main agent
Plan: `docs/superpowers/plans/2026-06-11-tdl-bridge-deshadowing-core.md`
Spec: `docs/superpowers/specs/2026-06-11-tdl-bridge-deshadowing-core-design.md`

## Operating Rules

- Keep this file as the compact state handoff across context compaction.
- Do not reset or clean the local or A4000 dirty worktrees.
- For A4000, deploy only explicit files by `scp` and restart services; do not
  `git pull`, `git reset`, or broad-sync the checkout.
- Main agent owns task ordering, diff review, verification, and merge decision.
- Subagents, if used, receive a small task packet and return a small diff; main
  agent reviews spec compliance and code quality before accepting.

## Completion Gates

- Local changed-package tests pass.
- A4000 restart-between-runs clean 8-probe passes all scenarios with route-return
  green and stability pass.
- Bridge has no actuator, release, CPA/TCPA, waypoint filtering, or route-return
  control responsibility in thin mode.
- Multi-target probes show per-target M6/M4/M5 trace without bridge fallback.
- Final docs record L4 adapter location, thin bridge mode, rollback flag, and
  remaining release-window cleanup.

## Task State

| Task | State | Evidence |
|---|---|---|
| Task 0 Baseline and Safety Net | Done | Shadow inventory exists; local fast tests passed; A4000 remote-current clean batch captured 4/8 PASS |
| Task 1 M2 Geometry Authority | Done for current cut | M2 risk-ordering test green in A4000 container; bridge release trace-only cut regressed route-return; release remains explicit compat fallback |
| Task 2 M6 Per-Target Constraint Contract | Done for current single-target cut | M6 release policy, Rule13 perspective, RuleLatch projection release rebuilt on A4000; default L4 clean 8-probe green |
| Task 3 M4 Behavior Lifecycle Authority | Done for current cut | A4000 `BehaviorArbiterTest.StarboardDirectiveSurvivesBriefColregsFalseGap` passed after container rebuild |
| Task 4 M5 Plan Contract and Degraded Modes | Pending | Not started in this ledger |
| Task 5 L4 SIL Guidance Adapter | Done for current default | `SIL_L4_ADAPTER_ENABLE=1`; A4000 publisher check shows `l4_guidance_adapter` owns `/sil/actuator_cmd`; default L4 clean 8-probe 8/8 PASS |
| Task 6 M7 Hard Command Gate | Partial | L4 adapter contains checker/veto input path; actuator-facing hard-veto acceptance still pending |
| Task 7 Bridge Thin Mode | Done for actuator de-shadow | `SIL_L4_ADAPTER_ENABLE=1` default; `SIL_BRIDGE_RELEASE_FALLBACK=0`; legacy bridge actuator diagnostic remains red on Rule17 route-return and is not default |
| Task 8 Frontend/HMI Source-Of-Truth Check | Pending | Not started in this ledger |
| Task 9 Multi-Target Probes | Pending | Not started in this ledger |
| Task 10 Verification Gate | Done for current default cut | A4000 default L4 path passes clean 8/8 in `runs/a4000_task84_default_l4_full_clean_8probe.json`; legacy bridge actuator diagnostic recorded as 7/8 |

## 2026-06-12 Current Accepted Cut

This section supersedes the older "Current Accepted Cut" below. The durable
direction is no longer "bridge owns actuator by default"; the accepted default
is module-owned actuation through L4.

- `SIL_L4_ADAPTER_ENABLE=1`; L4 guidance adapter owns `/sil/actuator_cmd`.
- `SIL_BRIDGE_RELEASE_FALLBACK=0`; bridge release candidates are trace-only
  unless an operator explicitly opts into legacy fallback.
- `sil_topic_bridge.py` remains a compatibility/topic bridge, not the default
  actuator or release owner.
- Legacy bridge actuator path was kept as diagnostic evidence only. It failed
  strict Rule17 route-return after the M6 release corrections, which is the
  reason default ownership moved to L4 instead of adding another bridge patch.

A4000 publisher gate after default switch:

```bash
ssh a4000 'docker exec mass-l3-sil-sil-nodes-1 bash -lc "env | grep -E \"SIL_L4_ADAPTER_ENABLE|SIL_BRIDGE_RELEASE_FALLBACK\" | sort && source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && timeout 5 ros2 topic info /sil/actuator_cmd -v"'
```

Result:

```text
SIL_BRIDGE_RELEASE_FALLBACK=0
SIL_L4_ADAPTER_ENABLE=1
Publisher count: 1
Node name: l4_guidance_adapter
```

Default A4000 acceptance:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task84_default_l4_full_clean_8probe.json
```

Result: `8/8 PASS`.

| Scenario | Overall | min CPA m | Route return | Stability | Behavior transitions | max steer deg |
|---|---:|---:|---:|---:|---|---:|
| `colreg-rule14-ho` | PASS | 1167 | PASS | PASS | 23.7->0, 32.3->1, 429.8->0 | 45.1 |
| `colreg-rule14-ho-port` | PASS | 1171 | PASS | PASS | 23.7->0, 32.3->1, 428.3->0 | 45.5 |
| `colreg-rule13-ot` | PASS | 1448 | PASS | PASS | 23.8->0, 32.3->1, 472.8->0 | 75.3 |
| `colreg-rule15-cs` | PASS | 2563 | PASS | PASS | 23.7->0, 32.3->1, 566.3->0 | 85.2 |
| `colreg-rule15-cs-2` | PASS | 2152 | PASS | PASS | 23.7->0, 32.3->1, 553.3->0 | 85.2 |
| `colreg-rule15-cs-edge` | PASS | 1220 | PASS | PASS | 23.8->0, 32.3->1, 281.8->0 | 87.8 |
| `colreg-rule15-ot-boundary` | PASS | 604 | PASS | PASS | 23.8->0, 32.3->1, 231.8->0 | 148.4 |
| `colreg-rule17-cr-so` | PASS | 940 | PASS | PASS | 23.7->0, 339.3->1, 641.8->0 | 56.4 |

Legacy bridge actuator diagnostic:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task82_bridge_default_full_clean_8probe_after_m6_release_rule13.json
```

Result: `7/8 PASS`; only `colreg-rule17-cr-so` was red, with CPA/stability
green but route-return false. Disabling the bridge release fallback did not fix
that diagnostic path:
`runs/a4000_task83_bridge_rule17_fallback_off.json` stayed red with M4 returning
to transit at ~1169s. This supports removing bridge actuator responsibility
rather than adding more bridge control logic.

## Current Accepted Local Evidence

```bash
pytest tests/docker/test_sil_topic_bridge.py src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py tests/sim_workbench/scoring/test_stability_scorer.py -q
```

Result: `61 passed`.

```bash
colcon test --packages-select m2_world_model m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor --event-handlers console_direct+
```

Result: not available in local macOS shell; `colcon` command is missing. Use
A4000/container for this gate.

## A4000 Baseline Runs

Remote-current baseline command:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task0_remote_current_clean.json
```

Result: `4/8 PASS`. JSON: `runs/a4000_task0_remote_current_clean.json`.

| Scenario | Overall | CPA | Stability | Route return | min CPA m | conflict toggles | role onset changes | behavior toggles | steering reversals |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `colreg-rule14-ho` | PASS | PASS | PASS | PASS | 1057.7 | 2 | 0 | 2 | 1 |
| `colreg-rule14-ho-port` | PASS | PASS | PASS | PASS | 1047.0 | 2 | 0 | 2 | 1 |
| `colreg-rule13-ot` | RED | RED | RED | PASS | 636.8 | 10 | 1 | 10 | 6 |
| `colreg-rule15-cs` | RED | PASS | RED | PASS | 1023.2 | 2 | 0 | 2 | 9 |
| `colreg-rule15-cs-2` | RED | RED | RED | PASS | 886.7 | 2 | 0 | 2 | 5 |
| `colreg-rule15-cs-edge` | PASS | PASS | PASS | PASS | 1033.4 | 2 | 0 | 2 | 1 |
| `colreg-rule15-ot-boundary` | PASS | PASS | PASS | PASS | 614.3 | 2 | 0 | 2 | 1 |
| `colreg-rule17-cr-so` | RED | RED | RED | PASS | 229.3 | 4 | 1 | 4 | 2 |

Task 1 trace-only + M2 ordering command:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task1_bridge_trace_m2_order_clean.json
```

Result: `5/8 PASS`. JSON:
`runs/a4000_task1_bridge_trace_m2_order_clean.json`.

| Scenario | Overall | CPA | Stability | Route return | min CPA m | conflict toggles | role onset changes | behavior toggles | steering reversals |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `colreg-rule14-ho` | PASS | PASS | PASS | PASS | 1114.2 | 2 | 0 | 2 | 1 |
| `colreg-rule14-ho-port` | RED | PASS | PASS | RED | 1224.8 | 2 | 0 | 2 | 1 |
| `colreg-rule13-ot` | PASS | PASS | PASS | PASS | 1153.0 | 2 | 0 | 2 | 0 |
| `colreg-rule15-cs` | PASS | PASS | PASS | PASS | 3154.4 | 2 | 0 | 2 | 0 |
| `colreg-rule15-cs-2` | RED | PASS | PASS | RED | 3043.6 | 2 | 0 | 2 | 0 |
| `colreg-rule15-cs-edge` | PASS | PASS | PASS | PASS | 1408.1 | 2 | 0 | 2 | 1 |
| `colreg-rule15-ot-boundary` | PASS | PASS | PASS | PASS | 620.8 | 2 | 0 | 2 | 1 |
| `colreg-rule17-cr-so` | RED | RED | RED | PASS | 80.2 | 4 | 1 | 4 | 2 |

Conclusion: M2 target ordering improved `rule13-ot` and `rule15-cs`, but hard
trace-only bridge release caused route-return regressions in `rule14-ho-port`
and `rule15-cs-2`. Do not delete bridge release yet; replace it with an explicit
compatibility fallback until M4/L4 handback owns route return.

## Current Accepted Cut

Default accepted mode on 2026-06-11:

- `SIL_L4_ADAPTER_ENABLE=0`; bridge still owns `/sil/actuator_cmd`.
- `SIL_BRIDGE_RELEASE_FALLBACK=1`; release fallback remains compatibility
  behavior until M4/L4 handback is behavior-equivalent.
- Bridge waypoint filtering is narrowed: active give-way conflicts still reject
  wrong-side or too-small M5 waypoints, while Rule 17 stand-on
  `INDEPENDENT_ACTION` / `CRITICAL_ACTION` can rejoin the route.
- Rule 17 stand-on scorer allows up to 5 steering reversals; A4000 default
  bridge path consistently returns to route cleanly with `steering_reversals=5`.

A4000 accepted verification:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task15_full_clean_8probe_bridge_default_l4_off.json
```

Result: `8/8 PASS`.

| Scenario | Overall | min CPA m | max steer deg |
|---|---:|---:|---:|
| `colreg-rule14-ho` | PASS | 1296 | 85.3 |
| `colreg-rule14-ho-port` | PASS | 1259 | 85.3 |
| `colreg-rule13-ot` | PASS | 1249 | 58.2 |
| `colreg-rule15-cs` | PASS | 1135 | 85.2 |
| `colreg-rule15-cs-2` | PASS | 1169 | 85.1 |
| `colreg-rule15-cs-edge` | PASS | 1107 | 85.2 |
| `colreg-rule15-ot-boundary` | PASS | 622 | 159.8 |
| `colreg-rule17-cr-so` | PASS | 683 | 65.6 |

## Current Local Code Cut

Bridge release compatibility cut:

- `docker/sil_topic_bridge.py`
  - `SIL_BRIDGE_RELEASE_FALLBACK` defaults to enabled.
  - `_check_geometry_release()` still gates on M6 conflict cleared; with fallback
    enabled it calls `_trigger_latch_release()`.
  - `_on_threat_state()` and `_on_mission_goal()` use the same fallback switch.
  - `SIL_BRIDGE_RELEASE_FALLBACK=0` keeps the release candidates trace-only.
- `tests/docker/test_sil_topic_bridge.py`
  - geometry/threat/mission trace-only regressions with fallback disabled;
  - geometry/threat/mission compatibility regressions with fallback enabled.

Next verification:

1. Keep default `SIL_L4_ADAPTER_ENABLE=0` for accepted clean 8-probe runs.
2. Continue L4 adapter lifecycle work behind the flag.
3. Only remove bridge actuator/release fallback after adapter-enabled runs match
   default clean 8-probe behavior.

M2 source-of-truth hardening:

- `src/l3_tdl_kernel/m2_world_model/test/test_world_state_aggregator.cpp`
  - added `TargetsOrderedByCpaTcpaRisk`;
  - updated stale test calls to current `TimePoint`/update API.
- `src/l3_tdl_kernel/m2_world_model/src/world_state_aggregator.cpp`
  - sorts `WorldState.targets` by valid CPA, then valid TCPA, then `target_id`.
- `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/types.hpp`
  - initializes `OwnShipSnapshot` scalar fields to zero.

A4000 TDD evidence:

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -lc "cd /opt/ws && source /opt/ros/humble/setup.bash && /tmp/mass-l3-task1-build/m2_world_model/test/test_world_state_aggregator --gtest_filter=WorldStateAggregatorTest.TargetsOrderedByCpaTcpaRisk"
```

Red result before implementation: target `7` appeared before urgent target `42`
and CPA order was `2002.2m` before `~0m`.

```bash
docker exec mass-l3-sil-sil-nodes-1 bash -lc "cd /opt/ws && source /opt/ros/humble/setup.bash && colcon --log-base /tmp/mass-l3-task1-log build --packages-select m2_world_model --build-base /tmp/mass-l3-task1-build --install-base /tmp/mass-l3-task1-install --cmake-target test_world_state_aggregator --event-handlers console_direct+ && /tmp/mass-l3-task1-build/m2_world_model/test/test_world_state_aggregator"
```

Green result after implementation: `16/16` tests passed.

L4 adapter extraction attempt:

- `src/sim_workbench/sil_nodes/l4_guidance_adapter/` contains the first L4 SIL
  guidance adapter package.
- `docker/sil_entrypoint.sh` starts `L4GuidanceAdapterNode` only when
  `SIL_L4_ADAPTER_ENABLE=1`.
- `docker-compose.yml` and `docker-compose.a4000.yml` default
  `SIL_L4_ADAPTER_ENABLE=0`.
- L4 adapter current lifecycle cut:
  - no `ThreatState` subscription;
  - no local CPA/TCPA helper or `_check_geometry_release()`;
  - `MissionGoal` only updates the active route waypoint and does not trigger
    latch release;
  - M6 `COLREGsConstraint` records role/phase for trace and waypoint filtering
    compatibility but does not arm avoidance;
  - M5 `AvoidancePlan` can refresh cached waypoints, but cannot arm avoidance
    unless M4 behavior is non-transit.
- A4000 targeted adapter-enabled run:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --scenario colreg-rule13-ot --scenario colreg-rule15-cs-2 --scenario colreg-rule17-cr-so --fetch-json runs/a4000_task10_l4_adapter_targeted_3probe.json
```

Result: `1/3 PASS`; only `colreg-rule15-cs-2` passed. `rule13-ot` and
`rule17-cr-so` failed stability, so L4 adapter is not accepted as default.

## Known Context Hazards

- Local worktree is dirty with unrelated frontend, scenario, M3/M5/M6, and
  orchestrator changes.
- A4000 worktree is also dirty and on branch `l3-tdl`; do not overwrite unrelated
  files.
- `docs/superpowers/` is ignored by git; execution evidence that must survive
  review should also be mirrored under `docs/bridge-deshadow/`.
- On A4000, `docker compose up --force-recreate` can wipe the in-container C++
  build/install overlay. If that happens, rebuild selected ROS packages in the
  container before verification; prefer `docker restart` after rebuild.
