# COLREGs Evidence Session Design Spec

## Metadata

| Field | Value |
|---|---|
| Date | 2026-06-22 |
| Branch | `codex/colregs-behavior-fix` |
| Worktree | `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix` |
| Scope | Unified evidence folders for single probe, clean 8-probe, clean 12-probe, and frontend-launched SIL simulations |
| Primary output | JSONL-first trace evidence plus per-scenario trajectory PNG dashboards |

## Problem

COLREG probe evidence is currently split across multiple surfaces:

- `docker/sil_topic_bridge.py` writes the current debug trace to `runs/trace_current.jsonl`.
- `scripts/run_colregs_clean_8probe.py` delegates to `scripts/run_6_scenarios.py`; per-scenario JSONL/report archival only happens when `--trace-report-dir` is explicitly supplied.
- Frontend-launched simulation uses lifecycle APIs in `src/sil_orchestrator/main.py` and `web/src/screens/SimulationCheck.tsx`; it does not currently guarantee a matching `runs/trace_eval/<session>/` folder with trace JSONL and a dashboard PNG.
- `runs/run-*/scoring.arrow` exists for scoring, and `/api/v1/export/arrow` can attempt MCAP-to-Arrow replay export, but Arrow is not a complete or mandatory COLREG replay evidence source for the current probe workflow.

This makes quick visual inspection difficult. The user wants every meaningful simulation launch to produce a durable folder containing the JSONL trace and a PNG dashboard, while discarding empty folders created by accidental clicks or failed configuration.

## Goals

1. Every user-initiated simulation task creates one evidence session folder under `runs/trace_eval/`.
2. The same evidence session model covers:
   - single probe,
   - clean 8-probe,
   - clean 12-probe,
   - frontend click-to-start simulation.
3. Each scenario with valid data archives:
   - trace JSONL,
   - trace evaluation report JSON when available,
   - trajectory dashboard PNG generated asynchronously.
4. Empty or non-started sessions are discarded automatically.
5. JSONL is the initial source of truth for trajectory/debug evidence. Arrow replay remains optional and is only recorded as detected metadata in this scope.

## Non-Goals

- Do not implement full frontend replay/scrubbing.
- Do not replace `DebugTraceWriter` with a new ROS2 recorder.
- Do not require MCAP or Arrow to pass evidence generation.
- Do not change COLREG gate thresholds, scenario geometry, or behavior logic.
- Do not make `docker/sil_topic_bridge.py` depend on frontend session names.

## Evidence Session Granularity

One session folder is created per user-initiated task:

| Launch type | Folder contents |
|---|---|
| Single probe | One scenario trace/report/PNG |
| Clean 8-probe | Eight scenario traces/reports/PNGs plus batch summary |
| Clean 12-probe | Twelve scenario traces/reports/PNGs plus batch summary |
| Frontend click | One scenario trace/PNG and scenario summary if available |

Frontend behavior is explicit: every click that attempts to start a simulation creates a new pending session. If the simulation produces no valid trace data, that folder is removed during finalization.

## Session Naming

Session names are timestamp-prefixed for chronological browsing:

```text
<YYYYMMDD_HHMMSS>_single_<scenario_id>
<YYYYMMDD_HHMMSS>_clean8
<YYYYMMDD_HHMMSS>_clean12
<YYYYMMDD_HHMMSS>_frontend_<scenario_id>
```

Examples:

```text
runs/trace_eval/20260622_153012_single_colreg-rule14-ho/
runs/trace_eval/20260622_153500_clean8/
runs/trace_eval/20260622_160100_clean12/
runs/trace_eval/20260622_161230_frontend_colreg-rule15-cs/
```

## Directory Contract

```text
runs/trace_eval/<session_name>/
  manifest.json
  batch_summary.json
  scenario_summary.json
  <scenario_id>.json
  <scenario_id>.trace_current.jsonl
  <scenario_id>_trajectory_dashboard.png
  logs/
    postprocess.jsonl
```

Rules:

- `batch_summary.json` is written for 8-probe and 12-probe runs.
- `scenario_summary.json` is written for frontend and single-probe runs when no batch summary is present.
- `<scenario_id>.json` is the existing `TraceEvaluationReport` shape when evaluator data exists.
- `<scenario_id>.trace_current.jsonl` is copied from `runs/trace_current.jsonl` after the scenario stops or completes.
- `<scenario_id>_trajectory_dashboard.png` is generated asynchronously from JSONL plus optional report JSON.
- `logs/postprocess.jsonl` records post-processing outcomes without blocking the simulation.

## Manifest Schema

`manifest.json` is the stable index consumed by humans, frontend code, and future tools.

```json
{
  "schema_version": 1,
  "session_name": "20260622_160100_clean12",
  "source": "cli",
  "suite": "clean12",
  "created_at": "2026-06-22T16:01:00+08:00",
  "ended_at": "2026-06-22T16:23:00+08:00",
  "status": "completed",
  "valid_data": true,
  "validity": {
    "own_ship_samples": 6258,
    "sim_t_duration_s": 625.8,
    "threshold_samples": 20,
    "threshold_duration_s": 5.0
  },
  "scenarios": [
    {
      "scenario_id": "colreg-rule14-ho",
      "status": "pass",
      "valid_data": true,
      "trace_path": "colreg-rule14-ho.trace_current.jsonl",
      "report_path": "colreg-rule14-ho.json",
      "png_path": "colreg-rule14-ho_trajectory_dashboard.png",
      "run_id": "run-19eee13b85f"
    }
  ],
  "arrow": {
    "scoring_arrow_detected": true,
    "replay_arrow_detected": false,
    "scoring_arrow_path": "runs/run-19eee13b85f/scoring.arrow",
    "replay_arrow_path": null,
    "note": "Initial evidence is JSONL-first; Arrow replay is optional."
  }
}
```

Allowed `source` values:

- `cli`
- `frontend`

Allowed `suite` values:

- `single`
- `clean8`
- `clean12`
- `frontend`

Allowed `status` values:

- `pending`
- `completed`
- `stopped`
- `error`
- `discarded`

Scenario `status` values:

- `pass`
- `fail`
- `stopped`
- `error`
- `unknown`

## Valid Data Rule

A scenario trace is valid when all conditions are true:

```text
trace JSONL exists
count(topic == "/sil/own_ship_state") >= 20
max(sim_t) - min(sim_t) over /sil/own_ship_state >= 5.0 seconds
```

Session finalization rule:

```text
if valid scenario count == 0:
  remove the session folder
else:
  keep the session folder and set manifest.valid_data = true
```

This keeps real early-stop simulations but removes failed launches, accidental clicks, and configure-only attempts.

## Data Capture Scope

The initial JSONL must preserve the current bridge debug topics and continue to support existing trace evaluation:

- `/sil/own_ship_state`
- `/sil/actuator_cmd`
- `/sil/scoring`
- `/l3/m4/behavior_plan`
- `/l3/m5/avoidance_plan`
- `/l3/m6/colregs_constraint`
- `/l3/m3/mission_goal`
- `/l3/checker/veto`
- `/l3/fsm_state`

If future topics become available, they may be recorded without changing the folder contract as long as each JSONL record preserves `sim_t`, `wall_t`, and `topic`.

## Architecture

Use an orchestrator-level Evidence Session Manager. Keep `DebugTraceWriter` low-coupled and unchanged as the live writer to `runs/trace_current.jsonl`.

```text
CLI runner or frontend lifecycle
  -> EvidenceSessionManager.start(...)
  -> lifecycle configure/activate/run
  -> bridge writes runs/trace_current.jsonl
  -> EvidenceSessionManager.archive_scenario(...)
       copies JSONL
       validates data
       writes/updates manifest
       records Arrow metadata if available
       enqueues PNG generation
  -> EvidenceSessionManager.finalize(...)
       writes summary
       discards empty sessions
```

The manager lives outside ROS2 nodes. It can be used by both CLI and FastAPI routes.

## CLI Integration

`scripts/run_colregs_clean_8probe.py` remains the canonical command and continues delegating to `scripts/run_6_scenarios.py`.

Behavior:

- If `--trace-report-dir` is supplied, use that directory as the evidence session root.
- If `--trace-report-dir` is omitted, create one automatically:
  - `--scenario X` without `--include-intelligent`: `<ts>_single_X`
  - default clean 8-probe: `<ts>_clean8`
  - `--include-intelligent`: `<ts>_clean12`
- After each scenario, archive `runs/trace_current.jsonl` to `<scenario_id>.trace_current.jsonl`.
- Write the existing trace evaluation JSON to `<scenario_id>.json`.
- Enqueue PNG generation for each valid scenario.
- Write `batch_summary.json` into the session folder for batch runs.

## Frontend Integration

Frontend launch flow in `web/src/screens/SimulationCheck.tsx` creates a session before lifecycle cleanup/configure/activate:

```text
POST /api/v1/evidence/session/start
  body: { "source": "frontend", "suite": "frontend", "scenario_id": "<scenario>" }

POST /api/v1/lifecycle/cleanup
POST /api/v1/lifecycle/configure
POST /api/v1/lifecycle/activate
```

On stop, deactivate, cleanup, or monitor exit:

```text
POST /api/v1/evidence/session/{session_id}/finalize
  body: { "scenario_id": "<scenario>", "status": "stopped|completed|error" }
```

Read endpoints:

```text
GET /api/v1/evidence/session/{session_id}
GET /api/v1/evidence/sessions?limit=50
```

The frontend does not write files directly. It displays manifest status and PNG paths returned by the orchestrator.

## PNG Dashboard Contract

The dashboard generator reads JSONL plus optional report JSON and writes PNG.

Title:

```text
本船航线：<scenario_id>_<session_name>
```

Layout:

- Left half of the figure contains:
  - full own-ship trace,
  - behavior-colored stages,
  - vertical legend/notes on the far left,
  - no waypoint markers by default.
- Right half contains:
  - overall verdict,
  - core run data,
  - GATE checks,
  - L1-L7 layer checks,
  - trace signals covering M4/M5/M6/scoring/rudder.

If report JSON is missing, the PNG still renders the trace and marks report-derived fields as `UNKNOWN`.

## Arrow Strategy

Arrow remains optional in this scope.

Current repository behavior:

- `src/sim_workbench/sil_nodes/scoring/scoring/node.py` writes `runs/run-*/scoring.arrow`.
- `src/sil_orchestrator/arrow_routes.py` can generate `runs/run-*/replay.arrow` from MCAP when MCAP is present.

Evidence Session Manager records detected Arrow metadata but does not require Arrow for JSONL evidence or PNG generation.

## Error Handling

- JSONL copy failure: mark scenario `error`, write `logs/postprocess.jsonl`, keep session if another scenario is valid.
- PNG generation failure: write `logs/postprocess.jsonl`, keep JSONL/report.
- Missing report JSON: generate PNG with `UNKNOWN` report fields.
- Missing `trace_current.jsonl`: mark scenario `error`; discard session if no other valid scenario exists.
- Path traversal in session IDs: reject any session name containing path separators or resolving outside `runs/trace_eval`.
- Concurrent frontend starts: each click gets a unique timestamp-prefixed session. If two starts happen in the same second, append a short suffix such as `_01`.

## Testing Requirements

Unit tests:

- Session naming creates timestamp-prefixed names for single, clean8, clean12, frontend.
- Trace validation accepts `>=20` own-ship samples and `>=5.0s` duration.
- Trace validation rejects short or empty traces.
- Finalization deletes sessions with no valid scenario.
- Finalization keeps sessions with valid scenario and writes manifest.
- Dashboard generator produces a PNG from sample JSONL and optional report JSON.

Integration tests:

- CLI single probe with mocked `run_scenario` creates a single evidence folder.
- CLI clean12 with mocked scenarios writes one session folder with multiple scenario artifacts.
- FastAPI evidence routes start/finalize/list sessions and reject path traversal.
- Frontend lifecycle test verifies a frontend launch creates a session before configure/activate and finalizes on abort/deactivate path.

Manual verification:

```bash
python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule14-ho
python3 scripts/run_colregs_clean_8probe.py
python3 scripts/run_colregs_clean_8probe.py --include-intelligent --restart-between-runs
```

Expected evidence:

```text
runs/trace_eval/<timestamp>_single_colreg-rule14-ho/
runs/trace_eval/<timestamp>_clean8/
runs/trace_eval/<timestamp>_clean12/
```

Each retained folder contains a manifest, JSONL traces, reports when available, and PNG dashboards.

## Acceptance Criteria

1. Single probe creates one timestamp-prefixed evidence folder with one valid JSONL and one PNG.
2. Clean 8-probe creates one timestamp-prefixed evidence folder with up to eight scenario JSONL files and PNGs.
3. Clean 12-probe creates one timestamp-prefixed evidence folder with up to twelve scenario JSONL files and PNGs.
4. Frontend click creates a new pending session and finalizes to a retained folder only if the trace has at least 20 own-ship samples and at least 5 seconds of own-ship sim time.
5. Configure failures, accidental clicks, and zero-data runs do not leave empty session folders.
6. Existing `runs/trace_current.jsonl` debug behavior remains available.
7. Existing `scoring.arrow` behavior remains unchanged.
8. JSONL and PNG generation do not change COLREG behavior or gate thresholds.

