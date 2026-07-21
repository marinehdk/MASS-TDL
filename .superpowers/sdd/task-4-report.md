# L5-T1 -- BC to L4 Chain Closure: Status Report

**Status: DONE**

## Research Findings

### 1. Publication/Subscription Chain Analysis

| Component | Publishes | Topic | GNC/L4 Reach |
|-----------|-----------|-------|--------------|
| Mid-MPC | `AvoidancePlan` | `/l3/m5/avoidance_plan` | Yes, via GNC bridge |
| BC-MPC | `ReactiveOverrideCmd` | `/l3/m5/reactive_override_cmd` | No -- only M7 + FCB Sim |
| BC-MPC | `BcMpcHealth` | `/l3/m5/bc_mpc/health` | No |

**Root cause**: GNC bridge (`L3SideNode`) subscribes ONLY to `AvoidancePlan` on `/l3/m5/avoidance_plan`. BC-MPC's `ReactiveOverrideCmd` on `/l3/m5/reactive_override_cmd` is consumed by M7 (safety supervisor) and FCB simulator, but never reaches the GNC bridge or L4.

### 2. M5 Output Switching Logic

When `bc_mpc_should_take_over == true` (line 1054 in `mid_mpc_node.cpp`):
- `committed_route_manager_.mark_bc_mpc_takeover()` is called (line 1064)
- On 3rd consecutive NLP failure, `try_revise()` transitions to `LifecycleState::BcMpcFollow` (committed_route.cpp line 160)
- **But `publish_committed_route_()` had NO `BcMpcFollow` branch.** It fell through to the corridor fallback (building a DEGRADED corridor plan with `COMMIT_BRANCH_CORRIDOR`) or the no-conflict return path.

### 3. HandoverManager

**Does not exist** in the codebase. The handover logic is distributed across the `CommittedAvoidanceRoute` class:
- `notify_handover_inputs()` -- hysteresis evaluation
- `mark_bc_mpc_takeover()` / `bc_mpc_takeover_requested_` -- takeover flag
- P6 state machine: `BcMpcFollow` -> `HandoverNeutral` -> `Committed`

### 4. AvoidancePlan Schema

The `COMMIT_BRANCH_BCMPC_FOLLOW=5` enum value existed in `AvoidancePlan.msg` but was **never assigned** in any code path. The `status="BCMPC_FOLLOW"` string only appeared in `publish_keep_last_()` (which creates empty plans), but `publish_committed_route_()` never reached that code path for `BcMpcFollow` state.

### 5. The Exact Chain Break

```
bc_mpc_should_take_over=true
  -> committed_route -> BcMpcFollow
  -> publish_committed_route_() builds a CORRIDOR plan (or returns silently)
  -> GNC bridge receives CORRIDOR plan (no signal that BC-MPC is in control)
  -> BC-MPC's ReactiveOverrideCmd goes to M7 only
```

The `avoidance_plan` topic never carried `commit_branch=COMMIT_BRANCH_BCMPC_FOLLOW` or `status="BCMPC_FOLLOW"`.

## Implemented Fix

### Files Changed

1. **`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp`**
   - Added `#include "l3_msgs/msg/reactive_override_cmd.hpp"`
   - Added `sub_override_cmd_` subscription member
   - Added `last_override_cmd_` cache member
   - Added `has_override_cmd_` boolean flag
   - Added `override_cmd_mutex_` for thread safety

2. **`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`** (three changes):
   - **Constructor**: Added subscription to `/l3/m5/reactive_override_cmd` (best-effort QoS). Caches the BC-MPC heading/speed/validity for use in BcMpcFollow plan construction.
   - **`publish_committed_route_()`**: Added a dedicated `BcMpcFollow`/`HandoverNeutral` branch (inserted after the early-return guard, before the optimized/corridor/return branches). When the lifecycle state is either of these states:
     - Builds an `AvoidancePlan` with `commit_branch = COMMIT_BRANCH_BCMPC_FOLLOW` (5)
     - Sets `status = "BCMPC_FOLLOW"` or `"HANDOVER_NEUTRAL"` depending on state
     - Populates waypoints from the committed route's `active_geometry` (for GNC route continuity)
     - Falls back to a minimal 2-waypoint plan from ownship toward BC heading if no active geometry exists
     - Incorporates BC-MPC heading/speed from the cached `ReactiveOverrideCmd` into rationale and waypoint construction
     - Publishes via `publish_avoidance_plan_()`
   - **`reset_cross_run_state()`**: Added `has_override_cmd_ = false` reset on scenario change.

3. **`src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_committed_route.cpp`**
   - Added 3 `BcToL4Contract` tests:
     - `TakeoverSignalYieldsBcMpcFollowState` -- verifies that BC-MPC takeover + 3 NLP failures transitions to BcMpcFollow state
     - `BcMpcFollowResistsStaleGateAndReEscalation` -- verifies BcMpcFollow survives stale gate and rejects further commits (BC-MPC owns the maneuver)
     - `BcMpcFollowHandoverToCommitted` -- verifies the full handback path: BcMpcFollow -> HandoverNeutral -> Committed

### How the Chain Now Works

```
BC-MPC takeover (is_bc_active_=true)
  -> publishes ReactiveOverrideCmd on /l3/m5/reactive_override_cmd
       -> M7 receives it (existing)
       -> Mid-MPC caches it (NEW)
  -> Mid-MPC: bc_mpc_should_take_over=true
       -> committed_route -> BcMpcFollow
       -> publish_committed_route_ detects BcMpcFollow state
       -> publishes AvoidancePlan with:
            commit_branch = COMMIT_BRANCH_BCMPC_FOLLOW (5)
            status = "BCMPC_FOLLOW"
            waypoints from active_geometry
            BC heading in rationale
       -> GNC bridge receives on /l3/m5/avoidance_plan
       -> forwards to /colav/avoidance_plan
       -> L4 executes
```

## Test Evidence

### New Tests (3 tests, all PASSED)

```
[----------] 3 tests from BcToL4Contract
[ RUN      ] BcToL4Contract.TakeoverSignalYieldsBcMpcFollowState
[       OK ] BcToL4Contract.TakeoverSignalYieldsBcMpcFollowState (0 ms)
[ RUN      ] BcToL4Contract.BcMpcFollowResistsStaleGateAndReEscalation
[       OK ] BcToL4Contract.BcMpcFollowResistsStaleGateAndReEscalation (0 ms)
[ RUN      ] BcToL4Contract.BcMpcFollowHandoverToCommitted
[       OK ] BcToL4Contract.BcMpcFollowHandoverToCommitted (0 ms)
[----------] 3 tests from BcToL4Contract (0 ms total)
```

### Existing Tests (all 46 tests still PASSED)

Total: 46 tests from 7 test suites all passed, including:
- 4 `BcMpcNodeHandover` tests
- 10 `CommittedRouteP6` tests (handover hysteresis, FinalDegrade)
- 7 `CommittedRouteV22` tests (BcMpcFollow lifecycle)
- 3 `BcToL4Contract` tests (new)

```
[==========] 46 tests from 7 test suites ran. (1 ms total)
[  PASSED  ] 46 tests.
```

## ROS2 Contract Compliance

- `stamp`: Set to `now` in each plan publish
- `schema_version`: Set to 116 (consistent with existing contract)
- `confidence`: 0.3F for BCMPC_FOLLOW, 0.5F for HANDOVER_NEUTRAL (lower than normal plans since BC-MPC owns the maneuver, Mid-MPC is in follow mode)
- `rationale`: Populated with BC-MPC state, heading, speed, and validity when available
- `commit_branch`: Set to `COMMIT_BRANCH_BCMPC_FOLLOW` (5)

## Remaining Risks

1. **Pre-existing build failure**: `test_l4_contracts` has linker errors (from L4 task -- `map_acados_status_to_solver_status` and `solver_status_to_status` are declared `[[nodiscard]]` in `.cpp` file but not marked `inline`/exported). This is orthogonal to L5-T1 and does not affect the committed_route or BC-MPC tests.
2. **Integration testing**: The BcMpcFollow branch in `publish_committed_route_` has not been tested in a running SIL scenario. The unit tests verify the lifecycle state machine and contract, but end-to-end verification of the `avoidance_plan` content on the ROS2 bus during BC-MPC takeover requires a SIL scenario run.
3. **`HandoverManager` class**: The task brief mentions `HandoverManager` (section 5.3) as a potential new class. The current implementation keeps the handover logic distributed in `CommittedAvoidanceRoute` (consistent with the existing P6 architecture). A dedicated `HandoverManager` could be a future refactoring but is not required to close the BC->L4 chain.

## Commands Used

```bash
# Build test target
docker exec codex-m5-p3-sil-nodes-1 bash -c \
  "cd /opt/ws/build/m5_tactical_planner && make test_committed_route"

# Run tests
docker exec codex-m5-p3-sil-nodes-1 bash -c \
  "cd /opt/ws/build/m5_tactical_planner && ./test_committed_route"

docker exec codex-m5-p3-sil-nodes-1 bash -c \
  "cd /opt/ws/build/m5_tactical_planner && ./test_bc_mpc_node_handover"
```
