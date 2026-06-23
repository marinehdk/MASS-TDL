# COLREGs Generalized Repair Strict-12 Diagnosis

Date: 2026-06-22
Branch: `codex/colregs-generalization-debug`
Worktree: `.worktrees/colregs-generalization-debug`

## Scope

This report records the trace-first diagnosis checkpoint for the generalized COLREGs repair branch. It does not change scenario geometry, gate thresholds, or behavior tuning. The run used strict restart-between-runs 12-probe validation at 5x simulation rate.

Evidence:

- Summary: `runs/batch_20260622_222636_clean12_l4_trace_5x.json`
- Log: `runs/batch_20260622_222636_clean12_l4_trace_5x.log`
- Trace reports and dashboards: `runs/trace_eval/20260622_222636_clean12/`

Runner:

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 -u scripts/run_colregs_clean_8probe.py \
  --include-intelligent \
  --restart-between-runs \
  --restart-container colregs-generalization-debug-sil-nodes-1 \
  --restart-settle 120 \
  --sim-rate 5.0 \
  --summary-out runs/batch_20260622_222636_clean12_l4_trace_5x.json \
  --trace-report-dir runs/trace_eval/20260622_222636_clean12
```

## Result

Overall: 5/12 PASS.

| Scenario | Result | Primary failing gate | Notes |
|---|---:|---|---|
| `colreg-rule14-ho` | PASS | none | CPA 275.3 m >= 180 m, route return true. |
| `colreg-rule14-ho-port` | PASS | none | CPA 216.4 m >= 180 m, route return true. |
| `colreg-rule13-ot` | RED | CPA and overtake completion | CPA 94.6 m < 180 m; overtake completed false. |
| `colreg-rule15-cs` | RED | CPA margin | CPA 875.7 m < 900 m; route/risk/stability pass. |
| `colreg-rule15-cs-2` | RED | CPA margin | CPA 869.2 m < 900 m; route/risk/stability pass. |
| `colreg-rule15-cs-edge` | RED | phase semantics | CPA 455.9 m >= 270 m; no-cross-ahead gate fails. |
| `colreg-rule15-ot-boundary` | RED | release/route return | CPA 330.9 m >= 270 m; M4 remains in avoidance; route return false. |
| `colreg-rule17-cr-so` | PASS | none | CPA 227.0 m >= 180 m, route return true. |
| `colreg-rule14-ho-intelligent` | PASS | none | CPA 432.7 m >= 180 m, route return true. |
| `colreg-rule15-cs-intelligent` | RED | CPA margin | CPA 877.9 m < 900 m; route/risk/stability pass. |
| `colreg-rule13-ot-target-giveway` | RED | risk gate | CPA 526.9 m >= 270 m; route/stability pass; risk gate false. |
| `colreg-rule17-cr-so-target-giveway` | PASS | none | CPA 496.8 m >= 180 m, route return true. |

## Chain Evidence

New trace coverage is partially effective:

- M5 ASDR records now expose planner health classes from runtime traces.
- L4 ASDR records now expose execution source classes from runtime traces.
- M2/M6/M7/M8 snapshots are present in trace files, but the current `chain_summary.diagnosis` still reports `OK` for gate-level RED cases. It is not yet a sufficient first-broken-stage classifier.

M5 planner health counts from strict 12-probe:

| Scenario | M5 planner health |
|---|---|
| `colreg-rule13-ot` | `EMPTY_TRANSIT=1010`, `GEOMETRIC_FALLBACK=1046`, `RECOVERY=152` |
| `colreg-rule15-cs` | `EMPTY_TRANSIT=113`, `GEOMETRIC_FALLBACK=791`, `SOLVER_CONVERGED=65`, `RECOVERY=149` |
| `colreg-rule15-cs-2` | `EMPTY_TRANSIT=109`, `GEOMETRIC_FALLBACK=793`, `SOLVER_CONVERGED=36`, `RECOVERY=149` |
| `colreg-rule15-cs-edge` | `EMPTY_TRANSIT=109`, `SOLVER_CONVERGED=18`, `GEOMETRIC_FALLBACK=270`, `RECOVERY=144` |
| `colreg-rule15-ot-boundary` | `EMPTY_TRANSIT=90`, `GEOMETRIC_FALLBACK=2121`, `SOLVER_CONVERGED=1` |
| `colreg-rule15-cs-intelligent` | `EMPTY_TRANSIT=113`, `GEOMETRIC_FALLBACK=793`, `SOLVER_CONVERGED=65`, `RECOVERY=150` |
| `colreg-rule13-ot-target-giveway` | `EMPTY_TRANSIT=112`, `GEOMETRIC_FALLBACK=127`, `RECOVERY=152` |

L4 execution source evidence:

- Passing scenarios still include `safety_gate` samples, so `safety_gate` alone is not a failure signal.
- `colreg-rule15-ot-boundary` is abnormal: L4 stays mostly in avoidance (`avoidance=2597`, `transit=33`) and M4 never transitions to recovery/transit. This points to M6/M4 release semantics plus M5 fallback persistence, not an isolated L4 controller issue.
- CPA-margin failures (`rule15-cs`, `rule15-cs-2`, `rule15-cs-intelligent`) show long avoidance windows and successful route return, but final minimum CPA remains 22-31 m below the 900 m floor. This points to a missing generalized safety-margin contract between M6 constraint intent and M5 executable trajectory, not a scenario-specific coordinate issue.

## Failure Taxonomy

1. CPA under-margin family:
   `rule15-cs`, `rule15-cs-2`, `rule15-cs-intelligent`.
   These are consistent, small shortfalls against the 900 m floor with otherwise stable behavior. Candidate system-level repair: enforce CPA floor as a hard executable M5/M6 contract, then verify route return and stability. Do not lower the 900 m floor.

2. Overtaking physical clearance family:
   `rule13-ot`.
   The vessel avoids and returns, but CPA is below 180 m and overtake completion remains false. Candidate system-level repair: inspect rule13 geometry, target-relative along-track progress, and M6 release condition before changing M5.

3. Phase semantics family:
   `rule15-cs-edge`.
   CPA and route return pass, but no-cross-ahead semantics fail. Candidate system-level repair: inspect the no-cross-ahead measurement and rule15 boundary classification. If this turns out to be scenario geometry or acceptance threshold coupling, user approval is required before editing.

4. Release and route-return family:
   `rule15-ot-boundary`.
   M4 remains in avoidance for the full run, M5 is almost entirely `GEOMETRIC_FALLBACK`, and route return fails. Candidate system-level repair: diagnose M6 encounter FSM release, M4 recovery eligibility, and M5 fallback lifecycle as one chain.

5. Risk-gate family:
   `rule13-ot-target-giveway`.
   CPA, route return, stability, and seamanship pass, but risk gate fails due warning/danger exposure. Candidate system-level repair: inspect M7 risk-domain scoring and target give-way interpretation. Do not suppress the risk gate.

## Next Implementation Order

1. Improve `chain_summary` so gate-level RED cases map to `L2`, `M2`, `M6`, `M4`, `M5`, `L4`, `M7`, or `M8` first-broken-stage categories.
2. Add focused tests for planner health extraction and L4 execution-source aggregation in the strict report path.
3. Diagnose `rule15-ot-boundary` first, because it contains the strongest full-chain symptom: no M4 release plus M5 persistent fallback.
4. Diagnose CPA under-margin family second, using one generalized M6/M5 constraint contract across all crossing-starboard cases.
5. Diagnose `rule13-ot-target-giveway` risk gate through M7 risk scoring before any behavior changes.

Any scenario geometry, timing threshold, or gate-threshold change remains blocked until user approval.
