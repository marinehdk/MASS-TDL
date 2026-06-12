# Bridge Shadow Inventory

Date: 2026-06-11
Scope: `docker/sil_topic_bridge.py`
Purpose: every bridge shadow behavior must move to its owner module before the
bridge copy is disabled or deleted.

## Summary

`sil_topic_bridge.py` currently mixes four roles:

- valid SIL transport: namespace/type relay, trace writer, module pulse;
- M2 shadow: CPA/TCPA and local geometry;
- M4/M6 shadow: avoidance arm/release/latch;
- M5/L4/M7 shadow: waypoint filtering, route return, control, safety gate.

The staged target is not "delete bridge first". The staged target is "move
equivalent behavior to correct owner, verify, then delete bridge copy".

## Inventory

| Bridge code | Current responsibility | Target owner | Removal stage | Verification gate |
|---|---|---|---|---|
| `HeadingController` at `docker/sil_topic_bridge.py:205` | PD heading control and rate limit | L4 SIL guidance adapter | Stage 2/4 | L4 route-following + avoidance unit tests; clean 8-probe no command regression |
| `SpeedController` at `docker/sil_topic_bridge.py:223` | PI speed/throttle control | L4 SIL guidance adapter | Stage 2/4 | L4 speed target tests; route-return green |
| constants `SHIP_LENGTH_M`, waypoint thresholds, `CPA_SAFE_M` at `docker/sil_topic_bridge.py:101-119` | vessel-specific control and tactical threshold constants | M1/M2/M5/L4 by domain | Stage 1-4 | constants no longer affect bridge command/release path |
| `_on_colregs_constraint` at `docker/sil_topic_bridge.py:670` | bridge-local M6 conflict latch mirror | M6/M4 source-of-truth; bridge trace only | Stage 1/4 | bridge tests prove M6 state is trace/read-only for bridge |
| `_on_behavior_plan` at `docker/sil_topic_bridge.py:698` | target heading refresh, transit release timing, M4 window interpretation | M4 behavior lifecycle; M5/L4 plan following | Stage 1/4 | M4 behavior tests; bridge no longer derives target heading |
| `_compute_dcpa_tcpa` at `docker/sil_topic_bridge.py:884` | bridge-local CPA/TCPA solver | M2 `CpaTcpaCalculator` and M6 release logic | Stage 1/4 | M2 CPA/TCPA tests; bridge release cannot call local solver |
| `_check_geometry_release` at `docker/sil_topic_bridge.py:942` | release when TCPA passed and DCPA safe | M6 finally-past-and-clear + M4 lifecycle | Stage 1/4 | `ot` and route-return stay green with bridge release disabled |
| `_on_threat_state` at `docker/sil_topic_bridge.py:983` | release on M2 cleared/astern state | M6/M4 lifecycle; M2 only supplies evidence | Stage 1/4 | M4 holds/release tests; bridge callback trace-only |
| `_on_mission_goal` at `docker/sil_topic_bridge.py:1003` | release on mission task valid + transit | M4 transit/handback lifecycle | Stage 1/4 | route-return green; no circling/U-turn |
| `_trigger_latch_release` at `docker/sil_topic_bridge.py:1050` | latch offset decay and teardown | M4 route-return/handback + L4 smooth route capture | Stage 2/4 | L4 stale-plan and handback tests |
| `_on_avoidance_plan` at `docker/sil_topic_bridge.py:1142` | arm avoidance from M5 plan, cache waypoints, infer validity | M5 plan validity + M4 lifecycle + L4 following | Stage 1/4 | bridge empty-plan regression remains green; L4 follows plan |
| `_make_actuator_msg` at `docker/sil_topic_bridge.py:1128` | actuator message construction | L4 adapter or explicit command gate | Stage 2/4 | bridge publication can be disabled by `SIL_L4_ADAPTER_ENABLE=1`; default remains bridge-owned after adapter 1/3 PASS |
| `_autopilot_step` at `docker/sil_topic_bridge.py:1287` | command publishing scheduler | L4 adapter / M7 gate | Stage 2/4 | L4 adapter can publish `/sil/actuator_cmd` in flagged mode; flagged mode not accepted as default |
| `_compute_avoidance_autopilot` at `docker/sil_topic_bridge.py:1359` | avoidance heading, rudder, throttle, route latch decay | L4 adapter | Stage 2/4 | L4 avoidance command tests; strict probes green |
| `_avoidance_waypoint_heading_deg` at `docker/sil_topic_bridge.py:1448` | M5 waypoint filtering and selected target heading | M5 contract validity; L4 LOS target selection | Stage 2/4 | bridge guard narrowed: give-way keeps safety rejection, Rule 17 stand-on rejoin is allowed |
| `_signed_xte_m` at `docker/sil_topic_bridge.py:1513` and `_segment_signed_xte_m` at `docker/sil_topic_bridge.py:1549` | route cross-track error | L4 adapter | Stage 2/4 | L4 XTE tests; route-return green |
| `_compute_transit_autopilot` at `docker/sil_topic_bridge.py:1575` | route following, XTE correction, speed boost, rudder/throttle | L4 adapter | Stage 2/4 | L4 normal LOS and route-return tests |
| `/l3/checker/veto` subscription at `docker/sil_topic_bridge.py:553` and `_on_checker_veto` at `docker/sil_topic_bridge.py:644` | trace-only veto handling | M7 hard gate in L4 adapter or gate node | Stage 3/4 | active veto changes actuator-facing output |

## Thin Bridge Allowlist

Allowed to remain after completion:

- `/sil/own_ship_state` to `/fusion/own_ship_state` relay.
- `/sil/target_vessel_state` or tracker output to `/fusion/tracked_targets`
  relay, if M2 remains the downstream world owner.
- `/sil/environment` to `/fusion/environment_state` relay.
- ASDR/UI trace relays when they do not infer tactical truth.
- Trace writer and module pulse.
- Temporary dual-run comparator, removed after cutover.

Not allowed to remain:

- any method that publishes actuator commands from bridge-owned tactical state;
- any release or arm condition in bridge;
- any bridge-only CPA/TCPA decision path;
- any bridge-only waypoint target selection;
- any bridge-only XTE/route-return controller;
- any checker/M7 signal that is trace-only on the command path.

## Batch 1 Status

Already partially addressed by the current dirty worktree / 2026-06-10 batch:

- M6 latch hardening and release/onset stability.
- M4 direction-aware `colregs_directive`.
- M5 explicit COLREGs fallback direction/magnitude.
- Bridge guard against empty M5 plan clearing active M6 conflict.

These are prerequisites, not final bridge removal.

## Current Cut Status

Accepted default path on 2026-06-11:

- `SIL_L4_ADAPTER_ENABLE=0`; bridge remains `/sil/actuator_cmd` publisher.
- `SIL_BRIDGE_RELEASE_FALLBACK=1`; bridge release remains compatibility
  behavior, not target architecture.
- A4000 clean 8-probe passes 8/8 with default bridge path:
  `runs/a4000_task15_full_clean_8probe_bridge_default_l4_off.json`.

Stage 2 scaffold:

- L4 adapter package exists at
  `src/sim_workbench/sil_nodes/l4_guidance_adapter/`.
- `SIL_L4_ADAPTER_ENABLE=1` disables bridge actuator publisher and starts
  `L4GuidanceAdapterNode`.
- L4 adapter no longer duplicates bridge/M2 local CPA/TCPA release: it has no
  `ThreatState` subscription, no `_check_geometry_release()`, and M6
  `COLREGsConstraint` does not arm avoidance by itself.
- M5 `AvoidancePlan` refreshes cached waypoints, but L4 only arms avoidance when
  M4 behavior is non-transit, keeping lifecycle authority in M4.
- A4000 adapter-enabled targeted run is only 1/3 PASS
  (`runs/a4000_task10_l4_adapter_targeted_3probe.json`), so the flag must stay
  off until lifecycle behavior matches default clean 8-probe results.

## Baseline Evidence

Local fast tests on 2026-06-11:

```bash
pytest tests/docker/test_sil_topic_bridge.py src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py tests/sim_workbench/scoring/test_stability_scorer.py -q
```

Result: 61 passed.

Local C++ package tests on 2026-06-11:

```bash
colcon test --packages-select m2_world_model m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor --event-handlers console_direct+
```

Result: not run locally; `colcon` is not installed in this macOS shell. Use
container or A4000 for this gate.

Task 0 A4000 remote-current clean baseline on 2026-06-11:

```bash
python3 /Users/marine/.codex/skills/colregs-clean-8probe/scripts/run_a4000_clean_8probe.py --settle 24 --fetch-json runs/a4000_task0_remote_current_clean.json
```

Result: 4/8 PASS. JSON: `runs/a4000_task0_remote_current_clean.json`.

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

Earlier local A4000 artifact checked:

- `runs/a4000_rule15_cs_1200_m5_contract.json`
- `runs/a4000_rule15_cs_1200_m5_contract.trace.jsonl` has 10053 lines.
- `colreg-rule15-cs`: `overall_pass=false`, `cpa_ok=true`,
  `returned_to_route=true`, `stability_pass=false`,
  `min_cpa_m=1019.0669990283066`, `steering_reversals=9`,
  `behavior_toggles=2`, `plan_valid_segments=1`, `conflict_toggles=2`,
  `role_onset_changes=0`.

## Next Cut

First code cut should avoid L4 extraction until release authority is stable:

1. Keep legacy bridge release behind explicit `SIL_BRIDGE_RELEASE_FALLBACK`
   while M4/L4 handback is still incomplete.
2. Prove M6/M4 own release/hold through tests with fallback disabled.
3. Then introduce L4 adapter in dual mode.
4. Then disable bridge actuator publication and release fallback.
