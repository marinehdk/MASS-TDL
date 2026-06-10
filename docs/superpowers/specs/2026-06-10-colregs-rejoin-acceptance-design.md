# COLREGs Rejoin Acceptance Design

## Goal

Make the strict COLREGs 8-probe acceptance depend on A4000 SIL evidence and a
real frontend browser screenshot, not on generated runner plots.

Acceptance artifacts:

- strict 8-probe data from `scenarios/COLREGs测试/`;
- browser screenshot from the A4000 frontend monitor showing the own-ship route;
- local regression output for Bridge/M4/M6 changes.

## Finding

The projected-past M6 release fallback was tested and rejected. It can make
`conflict_detected` flap in boundary probes because M2's clamped `tcpa_s` can
briefly look post-CPA before the encounter is truly past and clear.

The remaining strict-probe instability was a one-to-two-cycle false gap between
M6 release/re-detect and M4 commitment release. M4 released its COLREG anchor
immediately, then re-accepted a new COLREG window on the next M6 active cycle.
That produced a tail-end AVOID re-entry and extra ROT variance in
`colreg-rule15-ot-boundary`.

## Final Design

M6 remains conservative:

- release only when target is past the encounter-reference beam, range is
  opening, and CPA/current separation is safe;
- do not use projected-past CPA as a release authority.

M4 provides the missing commitment state:

- latch a COLREG turn anchor at maneuver onset;
- compute COLREG windows from the committed anchor, not current own heading;
- scale turn magnitude from CPA risk, with a quartering critical-CPA gate for
  the crossing/overtaking boundary probe;
- cache the last active COLREG directive and hold it across short M6 false gaps;
- release the committed directive only after a short consecutive-inactive dwell.

Bridge remains a temporary SIL transition layer:

- allow M4 target-heading updates only when they shrink same-side deviation, so
  Bridge can accept rejoin windows after a large turn without rolling the target
  farther away.

Frontend evidence:

- browser screenshot must come from `#/monitor` while live telemetry is flowing;
- runner PNGs are diagnostic only and are not acceptance screenshots.

## Acceptance Evidence

Current A4000 evidence after the M4 release-dwell fix:

```text
OVERALL: 8/8 PASS

scenario                   PASS  conf_tog role_chg beh_tog    cpa_m  steer
colreg-rule14-ho           True  2        0        1           1336   48.8
colreg-rule14-ho-port      True  2        0        2           1381   49.7
colreg-rule13-ot           True  2        0        1           1498   34.4
colreg-rule15-cs           True  2        0        2           1770   36.9
colreg-rule15-cs-2         True  2        0        2           1664   41.8
colreg-rule15-cs-edge      True  2        0        2           1028   71.6
colreg-rule15-ot-boundary  True  2        0        1            592  148.7
colreg-rule17-cr-so        True  2        0        1           1424   44.2
```

Browser screenshot candidate:

```text
artifacts/colregs_8probe_browser_20260610/colreg-rule15-ot-boundary_monitor_browser_pass_candidate.png
```

Batch data:

```text
artifacts/colregs_8probe_browser_20260610/batch_colregs_clean_after_m4_dwell.json
```

## Non-Goals

- No M6 projected-past release fallback.
- No full L4/Bridge extraction in this pass.
- No frontend authoritative/candidate layer refactor in this pass.
- No broad M5 rejoin-controller implementation before strict 8-probe stability is locked.
