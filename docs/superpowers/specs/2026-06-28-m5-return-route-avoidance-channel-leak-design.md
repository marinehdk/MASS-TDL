# M5 Return-to-Route Avoidance-Channel Leak Design

Date: 2026-06-28

Status: design draft for COLREGs 12-probe Class B defect (avoidance persists too long)

## Problem

When M4 transitions from COLREGS_AVOID to RECOVERY, M5 keeps emitting non-empty
plans on `/l3/m5/avoidance_waypoints` (which the bridge forwards to GNC as
`/colav/avoidance_plan`). GNC's `ActiveRouteManager::avoidance_plan_callback`
(`third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp:196`)
calls `mark_avoidance_active()` (line 216) on **every accepted avoidance plan,
regardless of `behavior_mode`**. It does not distinguish `emergency_avoidance`
from `return_to_route`. As a result, GNC keeps `avoidance_is_active()` true for
the whole RECOVERY window and into TRANSIT, DEFERRed every incoming nominal route.

### Evidence (fresh Class-A-fixed trace, ho-port)

`runs/trace_eval/20260628_113127_rule14_cohort_after_rule5_fix/colreg-rule14-ho-port`:

GNC `/l3/gnc/execution_status` distribution:
- `ACCEPTED` reason `feasible`: 1194 samples (29 %)
- `DEFERRED` reason `avoidance_active`: 2984 samples (71 %)

M4 `/l3/m4/behavior_plan` timeline:
- t=4.6 TRANSIT
- t=309.7 → COLREG_AVOID
- t=1474.4 → RECOVERY
- t=2085.8 → TRANSIT

M5 `/l3/m5/avoidance_waypoints`: 1194 messages, t=311.5 → t=1504. The non-empty
return-to-route geometry continues past M4's RECOVERY onset.

Seamanship gate (`scripts/run_6_scenarios.py:521-652`):
- `integrated_abs_xte_m_s = 352,165` vs limit `300,000` (FAIL, +17 %)
- `route_crossing_overshoot_count = 0` (PASS)
- `path_length_ratio = 0.569` (PASS)
- The avoidance window is 309.7 → 2085.8 = 1776 s (~30 min). The 300,000 m·s
  cap implicitly allows ~500 m XTE held ~600 s; the actual hold is ~3× that.

ho-intelligent fails the same check far worse (1,427,965 m·s) because the
intelligent target amplifies the avoidance duration.

### Root cause chain

```
M5 emits VALID return_to_route plan via /l3/m5/avoidance_waypoints during RECOVERY
  → bridge forwards as /colav/avoidance_plan
  → GNC avoidance_plan_callback accepts + mark_avoidance_active (ignores behavior_mode)
  → GNC avoidance_is_active() stays true (60 s hold, re-armed each publish)
  → every nominal route DEFERRED (avoidance_active, 71 %)
  → avoidance window stretches to ~1776 s
  → integrated_abs_xte time-integral breaches 300,000 m·s → seamanship FAIL
```

### Why M5 over-publishes

`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`:

- The `avoidance_waypoints` publish function (line ~727) unconditionally sets
  `command_source = "collision_avoidance"` (line 735) for every branch.
- The return-to-route branch (line 834) emits VALID waypoints with
  `behavior_mode = "return_to_route"` (line 881), but they go through the same
  publisher/topic as emergency avoidance.
- GNC has no field it reads to tell return_to_route from emergency_avoidance on
  the avoidance channel; it treats any accepted plan as "avoidance active".

## Scope

In scope:

- `m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` publish_avoidance_waypoints_:
  stop emitting non-empty plans via the avoidance channel once M4 has left
  COLREGS_AVOID. The return-to-route intent must release GNC's avoidance hold,
  not perpetuate it.
- Affects `colreg-rule14-ho-port`, `colreg-rule14-ho-intelligent`,
  `colreg-rule15-cs`, `colreg-rule15-cs-2`, `colreg-rule15-cs-intelligent` (all
  Layer-2 GREEN but Layer-3 seamanship/phase RED from prolonged avoidance).

Out of scope (separate specs):

- Class A `colreg-rule15-ot-boundary` M6 WRONG_RULE/ROLE (overtake classification).
- Class C `colreg-rule15-cs-edge` safety_floor near-miss (GNC speed envelope).
- GNC source (`third_party/gnc_ws`) — per AGENTS.md and the TDL-GNC contract,
  GNC behavior logic stays untouched; the bridge is a field mapper. The fix is
  M5 publishing the right thing, not making GNC interpret return_to_route.

## Design

### Option A (recommended): M5 emits EMPTY avoidance plan during RECOVERY

When M4 is in RECOVERY, the return-to-route geometry should reach GNC through
the **nominal route channel**, not the avoidance channel. M5 should emit an
EMPTY avoidance plan (no waypoints) so GNC's `avoidance_plan_callback` receives
a plan that fails `basic_route_valid` (latitude.size() < 2), does NOT call
`mark_avoidance_active`, and lets the existing 60 s hold expire naturally. The
return-to-route geometry itself is delivered by the route planner / L2 nominal
route, which GNC follows once avoidance is released.

```cpp
// In publish_avoidance_waypoints_, the return-to-route branch:
} else if (last_emitted_conflict_active_ || return_republish_active) {
  // ... existing anchor setup for traceability ...
  // Emit an EMPTY avoidance plan: M4 is in RECOVERY, so the avoidance channel
  // must release GNC's avoidance hold (mark_avoidance_active must not fire).
  // A plan with <2 waypoints is rejected by GNC basic_route_valid and does not
  // re-arm the 60 s hold. Return-to-route geometry is owned by L2/L3 nominal
  // route, which GNC resumes once avoidance expires.
  wp.behavior_mode = "return_to_route";
  wp.parent_route_id = "nominal";
  wp.plan_id = return_route_anchor_->plan_id;
  wp.has_return_to_route_point = true;
  wp.return_latitude = ...;   // hint retained for trace
  wp.return_longitude = ...;
  // latitude/longitude/command_speed_mps/navigation_mode left EMPTY
  wp.allow_degraded_execution = true;
  wp.rationale = "M4 RECOVERY — release GNC avoidance hold; route owned by L2 nominal";
}
```

The `return_to_route_emit_until_` 30 s republish window stays, but now each
republish is EMPTY, so it reinforces the release rather than re-arming.

Rationale: GNC's contract (TDL-GNC avoidance interface contract §ActiveRouteManager)
already defines that an accepted plan with <2 waypoints is invalid and does not
arm avoidance. We use the existing GNC semantics, not a GNC change.

### Why not Option B (separate return_to_route topic)

Adding a new `/l3/m5/return_route` topic + GNC subscriber is a larger interface
change that touches the bridge and GNC. It violates "bridge is a field mapper"
and YAGNI. The EMPTY-plan approach uses existing channels and GNC semantics.

### Why not Option C (M5 stops publishing entirely in RECOVERY)

Simply not publishing leaves GNC with the last accepted avoidance plan active
until its 60 s hold expires, then it resumes nominal. That would work, but M5
must keep the ASDR/SAT/trace records flowing and signal the state transition.
An explicit EMPTY plan with `behavior_mode=return_to_route` is observable and
debuggable; silence is not.

### What is not changed

- GNC source (`third_party/gnc_ws`): untouched.
- Bridge (`gnc_bridge`): untouched (it maps fields, including empty arrays).
- M5 emergency_avoidance publish path (M4 == COLREGS_AVOID): unchanged.
- `return_to_route_emit_until_` window: unchanged.
- Seamanship gate thresholds: unchanged.
- Oracle: unchanged.

## Verification

### Unit tests (container)

- `test_avoidance_waypoint_gen`: extend to assert that the return-to-route
  branch emits a plan with `latitude.empty()` (EMPTY) while the conflict-active
  branch emits VALID waypoints.
- Existing 43 tests must stay green.

### Module oracle (Layer-2)

After rebuild, re-run module oracle on fresh traces for all 5 Class B scenarios:
- Layer-2 must stay 6/6 GREEN (no regression in M2/M4/M5/M6/M7/L4 decisions).

### Integration test (Layer-3)

Re-run Rule14 cohort + Rule15 cohort. For each Class B scenario:
- `gnc_execution_state_counts`: DEFERRED avoidance_active must drop sharply
  (target: near 0 after M4 leaves AVOID; ACCEPTED/EXECUTING dominates).
- `integrated_abs_xte_m_s` must drop below 300,000 (seamanship gate target).
- `returned_to_route` must stay True (regression guard — release must not break
  route recovery; L2 nominal route must still be followed).

### Regression guard

- `colreg-rule14-ho` (Class A fixed): must stay GREEN at Layer-2; integration
  result must not regress.
- GNC avoidance must still arm correctly during active COLREGS_AVOID — verify
  on rule14-ho that ACCEPTED avoidance plans still fire during the M4 AVOID
  window.

## Acceptance Criteria

Class B (avoidance persists too long) is resolved when:

- During M4 RECOVERY, `/l3/m5/avoidance_waypoints` emits plans with empty
  latitude/longitude arrays (release signal), not VALID return-to-route geometry.
- GNC `DEFERRED avoidance_active` samples drop to near 0 outside the M4 AVOID
  window on all Class B scenarios.
- `integrated_abs_xte_m_s` drops below 300,000 on ho-port (and materially on
  ho-intelligent, accepting it may still fail other gates from separate causes).
- Layer-2 module oracle stays 6/6 GREEN on all affected scenarios (no M5/M6
  decision regression).
- `returned_to_route` stays True where it was True (no route-recovery regression).
- No change to GNC source, bridge behavior logic, seamanship thresholds, oracle
  thresholds, or scenario geometry.
- No scenario-id conditionals, vessel-specific branches, mocks, skips, or forced
  PASS paths.
