# TDL Bridge De-shadowing Core Implementation Plan

> Required implementation mode: use `superpowers:subagent-driven-development`
> for parallel lanes after this plan is reviewed against current dirty
> worktree. Do not let workers edit the same source file concurrently.

Goal: progressively remove `docker/sil_topic_bridge.py` tactical/control
responsibilities and pass a complete clean COLREGs acceptance path with clear
M2/M6/M4/M5/L4/M7 ownership.

Scope boundary: this plan supersedes the narrower
`2026-06-10-bridge-deshadow-strict-8probe` plan as the long-running backend
core remediation. The 2026-06-10 plan remains Batch 1 evidence for M6/M4/M5
contract hardening.

## Done Means

- Local unit/integration tests for changed packages pass.
- Bridge has no net-new tactical code.
- Each removed bridge function has an equivalent owner module and test.
- A4000 clean restart-between-runs 8-probe passes with route-return green.
- Multi-target smoke probes prove per-target constraints and no bridge fallback.
- Memory/workspace docs record final L4 adapter location and bridge thin-mode
  status.

## Current State

- Current worktree is dirty; do not commit or reset unrelated files.
- Batch 1 already exists: M6 latch hardening, M4 `colregs_directive`, M5
  explicit fallback direction, bridge M6-authority regression guard.
- `docker/sil_topic_bridge.py` still publishes `/sil/actuator_cmd` and owns
  release, latch, route-return, and controller logic.
- A4000 latest known single run `runs/a4000_rule15_cs_1200_m5_contract.json`
  had CPA and route return OK but failed stability (`steering_reversals=9`).

## Parallel Lanes

- Lane A: M2/M6 authority and per-target contracts.
- Lane B: M4/M5 behavior/plan contract validity.
- Lane C: L4 SIL guidance adapter and M7 command gate.
- Lane D: bridge shrink, trace comparator, harness/scoring acceptance.
- Lane E: frontend/HMI source-of-truth display checks.

Run lanes in parallel only when write sets are disjoint. Merge order must be:
contracts -> L4 adapter -> M7 gate -> bridge thin mode -> multi-target probes.

## Task 0: Baseline And Safety Net

Files:

- Read: `docker/sil_topic_bridge.py`
- Read: `tests/docker/test_sil_topic_bridge.py`
- Read: `scripts/run_6_scenarios.py`
- Read: `runs/a4000_rule15_cs_1200_m5_contract.json`
- Add if missing: `docs/bridge-deshadow/`

Steps:

- [ ] Save a bridge shadow inventory table: function, current line, tactical
      responsibility, target owner, removal stage.
- [ ] Capture current local test output:

```bash
pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
colcon test --packages-select m2_world_model m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor --event-handlers console_direct+
colcon test-result --verbose
```

- [ ] Capture current clean A4000 result before removing anything:

```bash
python scripts/run_6_scenarios.py --clean-restart --scenarios-dir scenarios/COLREGs测试
```

If the local script lacks `--clean-restart`, use the current A4000
restart-between-runs harness pattern and store the exact command in the
inventory doc.

Acceptance:

- Baseline document lists every bridge shadow path.
- All future removals reference this inventory row.

## Task 1: M2 Geometry Authority

Files:

- Modify: `docker/sil_topic_bridge.py`
- Modify/add: `tests/docker/test_sil_topic_bridge.py`
- Modify/add: `src/l3_tdl_kernel/m2_world_model/test/*`

Steps:

- [ ] Add a failing bridge test proving bridge release cannot depend on
      bridge-local `_compute_dcpa_tcpa()` when M2/M6 authority is active.
- [ ] Add/confirm M2 tests for CPA/TCPA on head-on, crossing, overtaking, static
      target, stale target, and multi-target ordering.
- [ ] Route any release-relevant geometry through M2/M6 state, not bridge-local
      recomputation.
- [ ] Keep bridge geometry only as trace-only comparator during dual mode.
- [ ] Remove comparator after A4000 clean run proves no regression.

Acceptance:

- Bridge-local CPA/TCPA cannot arm/release avoidance.
- M2 test coverage proves CPA/TCPA source-of-truth for all COLREGs probe shapes.

## Task 2: M6 Per-Target Constraint Contract

Files:

- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/include/m6_colregs_reasoner/types.hpp`
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp`
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp`
- Modify/add: `src/l3_tdl_kernel/m6_colregs_reasoner/test/*`
- Modify only if contract extension is unavoidable:
  `src/l3_tdl_kernel/l3_msgs/msg/COLREGsConstraint.msg`

Steps:

- [ ] Add tests for two-target rule assessment where primary target changes only
      when urgency order changes, not from iteration noise.
- [ ] Keep `primary_*` fields for backward compatibility.
- [ ] Add an internal per-target constraint collection with target ID,
      encounter type, role, phase, preferred direction, min alteration,
      confidence, and release state.
- [ ] If `.msg` extension is needed, add a compatible array message rather than
      overloading `rationale`.
- [ ] Add latch tests for target disappearance and reappearance with same ID.
- [ ] Publish trace/rationale identifying all active target IDs, not only the
      dominant one.

Acceptance:

- M6 can explain all active targets.
- M6 does not flicker primary role/direction while an obligation remains active.
- Single-target 8-probe behavior stays unchanged or improves.

## Task 3: M4 Behavior Lifecycle Authority

Files:

- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/colregs_directive.hpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp`
- Modify/add: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/*`
- Modify/add: `tests/integration/test_int_009_m4_arbitration_chain.cpp`

Steps:

- [ ] Add tests where M6 holds conflict and M5 publishes empty/stale plan; M4
      must keep authoritative behavior until M6 releases or M7 overrides.
- [ ] Add tests for `STARBOARD`, `PORT`, `REDUCE_SPEED`, and `HOLD` with
      multi-target urgency.
- [ ] Make `BehaviorPlan` lifecycle sufficient for downstream L4 state:
      transit, colregs avoid, route return/handback, degraded/no-plan.
- [ ] Remove any hidden assumption that starboard is default when M6 gives a
      different direction.

Acceptance:

- Bridge no longer needs `_avoidance_active` to decide tactical lifecycle.
- M4 toggles remain within clean probe thresholds.

## Task 4: M5 Plan Contract And Degraded Modes

Files:

- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- Modify/add: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_geometric_fallback.cpp`
- Modify/add: `tests/integration/test_int_001_m2_m6_m5_chain.cpp`

Steps:

- [ ] Add tests for valid avoidance waypoints: finite lat/lon, forward
      lookahead, bounded turn magnitude, route-return compatibility.
- [ ] Add tests for degraded/no-plan status when constraints conflict or inputs
      are stale.
- [ ] Ensure NLP success and fallback path populate waypoint schema, stamp,
      confidence, and rationale.
- [ ] Publish `ReactiveOverrideCmd` path or explicitly mark it unavailable with
      a tested no-publisher/degraded signal.
- [ ] Remove any need for bridge to filter wrong-side/too-near M5 waypoints.

Acceptance:

- L4 can follow M5 output without tactical interpretation.
- Bridge waypoint selection helpers become dead code.

## Task 5: L4 SIL Guidance Adapter

Files:

- Add: `src/sim_workbench/sil_nodes/l4_guidance_adapter/package.xml`
- Add: `src/sim_workbench/sil_nodes/l4_guidance_adapter/setup.py`
- Add: `src/sim_workbench/sil_nodes/l4_guidance_adapter/l4_guidance_adapter/node.py`
- Add: `src/sim_workbench/sil_nodes/l4_guidance_adapter/test/test_guidance_adapter.py`
- Modify: `docker/sil_entrypoint.sh`
- Modify: `docker-compose.yml`
- Modify: `docker-compose.a4000.yml`

Steps:

- [ ] Implement pure helper functions first: heading wrap, cross-track error,
      LOS target selection, speed target selection, stale-plan timeout,
      override priority.
- [ ] Add tests for normal route following, avoidance plan following, route
      return, stale avoidance release, reactive override priority, and actuator
      saturation.
- [ ] Add lifecycle ROS node wrapper that consumes M3/M4/M5/own state and
      publishes `/sil/actuator_cmd`.
- [ ] Launch adapter in dual mode while bridge still publishes only if
      `SIL_L4_ADAPTER_ENABLE=0`.
- [ ] Add trace fields for L4 mode, selected waypoint, xte_m, heading_cmd_deg,
      speed_cmd_kn, rudder_angle_deg, throttle.

Acceptance:

- L4 adapter can reproduce current green route following and avoidance behavior
  without using bridge methods.
- Bridge actuator publication can be disabled by env flag.

## Task 6: M7 Hard Command Gate

Files:

- Modify/add: `src/sim_workbench/sil_nodes/l4_guidance_adapter/*`
- Modify/add: `src/l3_tdl_kernel/m7_safety_supervisor/test/*`
- Modify/add: `tests/integration/test_int_003_checker_veto.cpp`

Steps:

- [ ] Subscribe L4 adapter or a small gate node to `/l3/m7/safety_alert` and
      `/l3/checker/veto`.
- [ ] Add tests for warning/no block, MRC_REQUIRED safe command, CRITICAL safe
      command, checker veto command suppression.
- [ ] Ensure trace records active safety gate reason.
- [ ] Confirm bridge no longer treats checker veto as trace-only for command
      path.

Acceptance:

- Active hard veto changes actuator-facing output.
- M7 gate path has a test independent of frontend/HMI.

## Task 7: Bridge Thin Mode

Files:

- Modify: `docker/sil_topic_bridge.py`
- Modify: `tests/docker/test_sil_topic_bridge.py`
- Modify: `handoff/audit_slices/_bandaids.md` or create a new closure note
  under `docs/bridge-deshadow/`

Steps:

- [ ] Disable bridge `/sil/actuator_cmd` publication when L4 adapter is enabled.
- [ ] Delete bridge `_check_geometry_release` after M6/M4 release is proven.
- [ ] Delete bridge `_trigger_latch_release` and `_avoidance_active` lifecycle
      once M4 behavior lifecycle is source-of-truth.
- [ ] Delete `_compute_avoidance_autopilot` and `_compute_transit_autopilot`
      once L4 adapter is source-of-truth.
- [ ] Delete bridge waypoint filtering and heading-target derivation once M5
      contract tests pass.
- [ ] Keep only SIL-to-L3 relays, L3-to-HMI relays, trace writer, module pulse,
      and compatibility shims.

Acceptance:

- Tests fail if bridge computes/release/commands tactical behavior in thin mode.
- `sil_topic_bridge.py` can be read as transport code, not hidden planner.

## Task 8: Frontend/HMI Source-Of-Truth Check

Files:

- Modify as needed: `web/src/hooks/useFoxgloveLive.ts`
- Modify as needed: `web/src/store/telemetryStore.ts`
- Modify as needed: `web/src/map/AvoidanceRouteLayer.tsx`
- Modify/add tests under `web/src/**/__tests__`

Steps:

- [ ] Verify frontend avoidance state comes from M6/M4/M5/L4/M7 topics, not
      bridge-inferred state.
- [ ] Add a test showing M6/M4/M5 active state renders when bridge tactical
      fields are absent.
- [ ] Add a test showing route clears after M4/M5 source-of-truth clears.

Acceptance:

- HMI remains useful after bridge thin mode.
- No UI test depends on bridge shadow fields as tactical truth.

## Task 9: Multi-Target Probes

Files:

- Add/modify generator, not hand-edited YAML:
  `src/sil_orchestrator/encounter_geometry.py` or current COLREG scenario
  generator.
- Add tests: `src/sil_orchestrator/tests/*`
- Add scenarios under `scenarios/COLREGs测试/` only through generator.

Steps:

- [ ] Add two-target crossing + stand-on monitor scenario.
- [ ] Add two give-way targets with same safe maneuver side.
- [ ] Add conflicting target constraints requiring speed reduction/degraded
      output.
- [ ] Add disappearing target while another active target remains latched.
- [ ] Extend scorer to record per-target M6/M4/M5 chain.

Acceptance:

- Multi-target probes do not require bridge-specific logic.
- Per-target IDs are visible in trace and scoring output.

## Task 10: Verification Gate

Local:

```bash
pytest tests/docker/test_sil_topic_bridge.py tests/sim_workbench/scoring/test_stability_scorer.py -q
pytest src/sim_workbench/sil_nodes/l4_guidance_adapter/test -q
colcon test --packages-select m2_world_model m4_behavior_arbiter m5_tactical_planner m6_colregs_reasoner m7_safety_supervisor --event-handlers console_direct+
colcon test-result --verbose
npm --prefix web test -- --run
```

A4000:

```bash
python scripts/run_6_scenarios.py --clean-restart --scenarios-dir scenarios/COLREGs测试
```

Required result:

- clean 8-probe pass;
- route-return green;
- no U-turn/circling;
- stability pass;
- frontend route/avoidance display matches M6/M4/M5/L4 trace;
- bridge thin mode enabled;
- rollback env flag documented for one release window only.

## Worktree Rules

- Do not reset or clean the dirty worktree.
- Stage only files touched by the active lane.
- For A4000, deploy by patch/scp and service restart; do not pull/reset remote
  checkout.
- If a lane discovers a new bridge dependency, add it to the shadow inventory
  with target owner before patching.
