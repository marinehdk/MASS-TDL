# COLREGs Rejoin Acceptance Design

## Goal

Deepen the strict COLREGs 8-probe fix so the implementation is judged by A4000
simulation evidence, not by local inference. The acceptance artifact is:

- strict 8-probe data from `scenarios/COLREGs测试/`;
- A4000 trajectory screenshots showing the own ship full route for each probe.

## Current Evidence

The current branch can pass the strict 8 probes for CPA/stability, but the latest
A4000 clean run still shows weak route recovery:

| Probe | pass | returned_to_route | final_xte_m | final_heading_dev_deg |
|---|---:|---:|---:|---:|
| colreg-rule14-ho | true | false | 760.6 | 150.3 |
| colreg-rule14-ho-port | true | false | 761.5 | 150.1 |
| colreg-rule13-ot | true | false | 1065.6 | 150.4 |
| colreg-rule15-cs | true | false | 764.6 | 150.2 |
| colreg-rule15-cs-2 | true | false | 652.9 | 150.2 |
| colreg-rule15-cs-edge | true | false | 846.6 | 150.4 |
| colreg-rule15-ot-boundary | true | false | 1055.6 | 150.1 |
| colreg-rule17-cr-so | true | false | 39.4 | 42.7 |

This means the collision-avoidance decision is now stable enough to satisfy the
strict scorer, but the maneuver lifecycle still deviates from the architecture:
COLREGs obligation, tactical release, route rejoin, and operator-visible full
route are not yet a single closed loop.

## Design Deviation

The architecture requires:

```text
M2 world state
  -> M6 COLREGs conflict/role/phase/release authority
  -> M4 behavior envelope
  -> M5 avoidance/rejoin trajectory
  -> L4/Bridge guidance during this SIL transition
  -> M8/frontend transparent display
```

Current deviation for this acceptance pass:

1. M6 release is too conservative for the 8-probe synthetic encounters.
   It releases only when the target is abaft the encounter-reference beam and
   opening. When the closest point has already passed, M2 clamps `tcpa_s` near
   zero and reports safe CPA/current separation, but M6 can keep the duty latched
   until very late or beyond the scenario horizon.

2. Bridge route-return logic exists, but it receives `TRANSIT` too late.
   Bridge can decay the avoidance heading and route-follow afterward, but if M6
   holds `conflict_detected=true` until the end of the run, that rejoin controller
   has no useful simulation time.

3. Screenshots are not a first-class verification artifact.
   Existing A4000 runs write trajectory PNGs, but the final acceptance needs an
   explicit artifact set tied to the same JSON data run.

## Required Change

Add a documented M6 release fallback for the case where M2 has already projected
the encounter past CPA and safe:

```text
release if:
  range is opening
  AND (
    target is abaft encounter-reference beam and CPA/current separation is safe
    OR M2 reports tcpa_s <= clamp epsilon and cpa_m >= cpa_safe_m
  )
```

This is narrower than the removed "CPA is opening because own ship maneuvered"
heuristic:

- it does not release while range is still closing;
- it requires M2's projected closest point to be at or beyond the present;
- it requires `cpa_m >= cpa_safe_m`;
- it preserves the existing onset suppression for projected-past targets.

## Non-Goals

- No full message schema refactor in this pass.
- No frontend authoritative/candidate layer split unless A4000 evidence shows the
  backend route is correct but the UI artifact is misleading.
- No full L4 extraction from Bridge in this pass.
- No broad M4/M5 refactor before the M6 release/rejoin evidence is tested.

## Acceptance

The implementation is not complete until A4000 produces:

1. a clean strict 8-probe run with `overall_pass=true` for all eight probes;
2. route-recovery data that is no worse than the current baseline and materially
   improves final route alignment where the scenario horizon allows rejoin;
3. trajectory PNGs for all eight probes, copied back into this worktree and
   viewable as the acceptance screenshots.

If condition 1 fails, revert or repair the release logic before any next-layer
work. If condition 1 passes but route recovery/screenshot evidence is still poor,
continue into M4/M5/Bridge rejoin control as the next task lane.
