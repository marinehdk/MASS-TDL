# Evidence Library And Replay Design

## Metadata

| Field | Value |
|---|---|
| Date | 2026-07-07 |
| Branch | `codex/evidence-replay-spec` |
| Scope | Screen 04 evidence management, replay, SQL ingestion, and COLREGs decision-chain drilldown |
| Primary Users | COLREGs debug operators, frontend simulation users, local/A4000 probe runners |
| Primary Source Inputs | Existing `runs/trace_eval/<session>/` probe artifacts and frontend evidence sessions |

## Problem

Screen 04 currently has a replay/report layout, but COLREGs debug evidence is still managed as scattered files under each checkout or worktree. A typical debug workflow produces evidence folders such as:

```text
runs/trace_eval/<timestamp>_single_<scenario_id>/
```

and many useful runs live under task worktrees:

```text
/home/marine.huang/Code/mass-l3/.worktrees/*/runs/trace_eval/
```

The current artifact folders are valuable but not a good human interaction surface. Operators need to answer concrete debugging questions:

- Is a RED caused by M6 rule/role/direction/release logic?
- Is it M5 route/solver/recovery behavior?
- Did L4 reject or defer a valid M5 plan?
- Did the evaluator gate fail because of real behavior or artifact inconsistency?
- At a specific replay time, what did M2, M6, M4, M5, L4, M7, and the gates say together?

The design must avoid duplicating the existing COLREGs probe evaluation stack. Screen 04 must consume the same evidence and verdicts as the strict probe system, not reimplement gate logic in the frontend.

## Goals

1. Make Screen 04 the unified run evidence management and replay surface.
2. Support both frontend-launched simulations and background CLI probes.
3. Discover evidence from the primary checkout, worktrees, and configured external evidence roots.
4. Store ingested evidence in a machine-local SQLite database for fast filtering, replay, and drilldown.
5. Preserve the existing probe artifact contract while making SQL the UI query source of truth.
6. Provide time-aligned decision-chain frames for COLREGs debugging.
7. Reuse current probe outputs: `manifest.json`, `TraceEvaluationReport`, runner gates, `chain_summary`, dashboard PNG, and G-ART artifact consistency.

## Non-Goals

- Do not change COLREGs pass/fail criteria.
- Do not reimplement G-SCN/G-SEM/G-SEP/G-ACT/G-REL/G-ART logic in the frontend.
- Do not change scenario geometry, probe workflows, or M2/M4/M5/M6/M7 behavior logic.
- Do not require all evidence roots to be under one repo checkout.
- Do not build A4000 sync or deployment logic in Screen 04.
- Do not remove JSONL support in the first version; current probe tooling still emits and expects raw trace artifacts.

## Existing Sources To Reuse

This design is grounded in these current repo and user-provided contracts:

- User-provided `colregs-probe` skill: strict probe workflow, evidence folder contents, layered gate, and G-ART semantics.
- `docs/superpowers/specs/2026-06-22-colregs-evidence-session-design.md`: evidence session naming, manifest schema, valid-data rule, and JSONL-first legacy artifact contract.
- `tools/sil/evidence_session.py`: current `EvidenceSessionManager`, `manifest.json` writer, archive/finalize behavior, and valid trace rule.
- `scripts/run_6_scenarios.py`: current strict gate computation, runner-only gate propagation, `chain_summary`, and artifact consistency integration.
- `tools/sil/colregs_trace_evaluator.py`: `TraceEvaluationReport` shape and layer verdict representation.
- `tools/sil/trajectory_dashboard.py`: current dashboard signal extraction and chain summary rendering.
- `web/src/screens/SimulationEvaluator.tsx`: current Screen 04 layout and playback shell.
- `src/sil_orchestrator/evidence_routes.py`: current frontend evidence session start/archive/finalize API surface.
- `docs/superpowers/plans/2026-06-03-simulation-replay-system-plan.md`: prior Screen 04 replay layout and synchronized scrubber direction.

## Screen 04 Routing

Screen 04 has two states, not two top-level product screens.

### Evidence Library

Route:

```text
#/evaluator
```

Behavior:

- No session is bound.
- The page opens the evidence library.
- User filters and selects evidence sessions.
- Session sources include frontend runs, primary-checkout probes, worktree probes, and configured external roots.

### Replay Detail

Route:

```text
#/evaluator/{evidence_id}
```

Behavior:

- A session is bound.
- The page opens the replay detail for that session.
- If navigation comes from a completed frontend simulation, it should pass the explicit `evidence_id` returned by the evidence backend.
- Avoid treating `latest` as authoritative. `latest` is allowed only when the frontend store already holds an explicit current evidence session id; otherwise route to Evidence Library.

## Evidence Roots Configuration

The repo ships a portable default config:

```text
config/evidence_library.default.json
```

Machine-local override:

```text
~/.config/mass-l3/evidence_library.json
```

Machine-local SQLite database:

```text
~/.config/mass-l3/evidence_index.sqlite
```

Config home resolution:

1. Use `MASS_L3_CONFIG_HOME` when set.
2. Otherwise use `~/.config/mass-l3`.
3. In Docker/compose deployments, this directory must be mounted from the host if settings and index must survive container recreation.
4. If config home is not writable, Screen 04 enters read-only evidence mode: browsing indexed rows is allowed when the database exists, but config saves, ingest, retention, and rescan mutations are disabled with an explicit warning.

Optional environment override:

```text
SIL_EVIDENCE_LIBRARY_CONFIG=/path/to/evidence_library.json
```

Default config:

```json
{
  "schema_version": 1,
  "roots": [
    {
      "id": "primary",
      "label": "Primary checkout",
      "kind": "frontend_and_probe",
      "trace_eval_glob": "{repo_root}/runs/trace_eval",
      "enabled": true,
      "trusted": true,
      "allow_retention_mutation": true,
      "follow_symlinks": false
    },
    {
      "id": "worktrees",
      "label": "Task worktrees",
      "kind": "probe_debug",
      "trace_eval_glob": "{repo_root}/.worktrees/*/runs/trace_eval",
      "enabled": true,
      "trusted": true,
      "allow_retention_mutation": true,
      "follow_symlinks": false
    }
  ],
  "scan": {
    "max_sessions_per_root": 500,
    "include_empty": false,
    "startup_stale_check": true
  },
  "retention": {
    "raw_trace_policy": "compress_after_ingest"
  }
}
```

On the current host, the A4000 repo root example resolves to:

```text
/home/marine.huang/Code/mass-l3
```

so the default roots expand to:

```text
/home/marine.huang/Code/mass-l3/runs/trace_eval
/home/marine.huang/Code/mass-l3/.worktrees/*/runs/trace_eval
```

This path is an example for the current host, not a permanent A4000 invariant. Deployment must still resolve `{repo_root}` from the running checkout.

### Config UI

Screen 04 Evidence Library exposes a settings drawer/tab.

Required controls:

- View effective roots.
- Add root.
- Edit root label/path/kind/enabled flag.
- Disable root without deleting it.
- Save machine-local config.
- Rescan roots.
- Show root health.

Worktree roots must display as a list after scanning, not as only a glob string.

Worktree list fields:

- worktree name
- branch
- trace_eval path
- session count
- latest session time
- scan status

## Persistent Evidence Store

SQLite database:

```text
~/.config/mass-l3/evidence_index.sqlite
```

Screen 04 queries SQLite by default. It must not scan all `trace_eval` folders on every page load.

Startup behavior:

- Read config.
- Open SQLite index.
- Run a lightweight stale check over configured roots.
- If roots or known session mtimes changed, show a `Needs Rescan` badge.
- Do not deep-scan folders unless user clicks `Rescan` or a future explicit background policy enables it.

Lightweight stale check:

- For each root, store the latest observed root directory mtime, discovered session count, and newest `manifest.json` mtime from the last rescan.
- On page load, check only root existence, root directory mtime, and a bounded sample of newest `manifest.json` mtimes.
- If the bounded sample differs from the stored sentinel, mark root stale.
- Do not open every report or JSONL during startup stale check.

Rescan behavior:

- Resolve enabled roots.
- Discover `manifest.json` files.
- Ingest changed sessions incrementally.
- Skip unchanged sessions by manifest/report/trace mtime plus stored sha256 where available.
- Mark missing sessions as unavailable, not deleted, unless user explicitly prunes index.

## SQLite Data Model

### Evidence Identity

`session_id` from `manifest.json` is a human-visible session name, not a globally unique key.

The database and API use `evidence_id` as the stable identity:

```text
evidence_id = sha256(root_id + canonical_resolved_session_path)
```

Rules:

- `evidence_id` is primary key for opening, serving, and mutating indexed evidence.
- `session_id` remains stored for display and compatibility.
- Two worktrees may contain the same `session_id`; they must produce different `evidence_id` values.
- Artifact links and API routes must use `evidence_id`, not bare `session_id`.
- If a folder is moved to a new root/path, it becomes a new evidence item unless an explicit future import/merge tool reconciles it.

### `sessions`

One row per evidence session.

Required fields:

- `evidence_id`
- `session_id`
- `source`
- `suite`
- `root_id`
- `worktree_name`
- `branch`
- `session_path`
- `created_at`
- `ended_at`
- `status`
- `valid_data`
- `scenario_count`
- `ingest_status`
- `ingest_error`
- `raw_trace_policy`
- `latest_mtime`

### `scenarios`

One row per scenario inside a session.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `run_id`
- `verdict`
- `overall_pass`
- `first_failure`
- `first_failed_gate`
- `first_failed_module`
- `role`
- `cpa_floor_m`
- `min_cpa_m`
- `min_cpa_nm`
- `returned_to_route`
- `route_return_required`
- `route_corridor_ok`
- `stability_pass`
- `compliance_verdict`

### `artifacts`

Files linked to a session or scenario.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `kind`
- `path`
- `relative_path`
- `sha256`
- `mtime`
- `compressed`
- `available`

Artifact kinds:

- `manifest`
- `batch_summary`
- `trace_jsonl`
- `trace_jsonl_gz`
- `trace_report`
- `trajectory_dashboard_png`
- `artifact_consistency`
- `postprocess_log`
- `other`

### `trajectory_samples`

Full-resolution dense trajectory store.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `vessel_id`
- `vessel_role`
- `sim_t`
- `wall_t`
- `lat`
- `lon`
- `heading_deg`
- `sog_kn`
- `rot_deg_s`
- `source_topic`
- `sample_seq`

Minimum initial sources:

- `/sil/own_ship_state`
- target state data extracted from `/l3/m2/world_state` when present in trace records

Required indexes:

- primary lookup: `(evidence_id, scenario_id, vessel_id, sim_t, sample_seq)`
- replay window: `(evidence_id, scenario_id, sim_t)`
- vessel replay: `(evidence_id, scenario_id, vessel_id, sim_t)`

### `trajectory_downsample`

Derived replay performance table.

Required fields:

- same identity fields as `trajectory_samples`
- `level`
- same numeric state fields

The frontend chart replay reads downsample data by default, then requests higher fidelity where needed.

### `state_segments`

Time-ranged latest-known module state.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `module`
- `field`
- `start_t`
- `end_t`
- `value_json`
- `source_topic`

Initial modules and fields:

- M2: primary target identity, geometry class, CPA/TCPA facts when available
- M6: rule, role, phase, preferred direction, conflict flag, encounter state, release prediction
- M4: behavior, avoidance_active
- M5: solver_status, plan_status, behavior_mode, command_source, plan_id, route/hash facts, recovery facts
- L4: execution_state, accepted, rejected, degraded, reason, suggested_action
- M7: alert_type, severity, recommended_mrm
- Lifecycle: state, autopilot_enabled, avoidance_active

Segment extraction rules:

- Sort by `(sim_t, wall_t, original_line_seq)` before segmenting.
- Repeated identical values extend the current segment instead of creating new rows.
- A changed value closes the previous segment at the new record's `sim_t`.
- Gaps larger than a configurable threshold close the segment and open a new segment with `gap_before=true` in `value_json`.
- Out-of-order records are retained in `events` with an ingest warning and are excluded from segment closure unless their order can be recovered by `original_line_seq`.
- Target identity churn is represented explicitly: when M2 primary target changes, close old target segment and open new target segment; do not silently merge targets.
- Final segment closes at scenario `sim_t_end`.

### `events`

Discrete evidence and decision events.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `sim_t`
- `wall_t`
- `module`
- `event_type`
- `severity`
- `payload_json`
- `source_topic`

Initial event sources:

- `/l3/asdr/record`
- gate failures
- M6 release/rearm changes
- M5 route/plan id changes
- L4 rejection/defer events
- M7 alert changes
- lifecycle transitions
- G-ART findings

### `gate_results`

Source-of-truth evaluator and runner gate results.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `gate_id`
- `status`
- `temporal_scope`
- `precedence_rank`
- `conflict_group`
- `payload_json`
- `source`

Gate ids:

- `G-SCN`
- `G-SEM`
- `G-SEP`
- `G-ACT`
- `G-REL`
- `G-ART`
- legacy report layers such as `L1_scenario_validity`, `L4_colregs_compliance`, `L5_route_recovery`

Sources:

- `TraceEvaluationReport`
- `batch_summary`
- `artifact_consistency`
- `chain_summary`

### Gate Import Mapping And Precedence

Gate values are imported from existing artifacts. They are not recomputed.

Import mapping:

| Existing source | Imported gate |
|---|---|
| `TraceEvaluationReport.layers.L1_scenario_validity` | `G-SCN` |
| `TraceEvaluationReport.layers.L2_safety_floor` | `G-SEP` |
| `TraceEvaluationReport.layers.L3_dynamic_risk` | `G-SEP` |
| `TraceEvaluationReport.layers.L4_colregs_compliance` | `G-SEM` |
| `TraceEvaluationReport.layers.L5_route_recovery` | `G-REL` |
| `TraceEvaluationReport.layers.L6_seamanship` | `G-REL` |
| `TraceEvaluationReport.layers.L7_stability` | `G-ACT` |
| `batch_summary.cpa_ok` / `domain_gates.risk_gate_ok` | `G-SEP` |
| `batch_summary.phase_semantics.phase_semantics_ok` / `compliance_verdict` | `G-SEM` |
| `batch_summary.stability_pass` | `G-ACT` |
| `batch_summary.returned_to_route` / `route_corridor_ok` / `domain_gates.seamanship_gate_ok` | `G-REL` |
| `<scenario_id>.artifact_consistency.json.g_art_ok` | `G-ART` |

Precedence:

1. Scenario `overall_pass` source of truth is `batch_summary` when present.
2. If `batch_summary` is absent, use the per-scenario `TraceEvaluationReport.verdict.overall_pass`.
3. If both are absent, use `manifest.scenarios[].status` only as a display fallback.
4. `G-ART` is always imported independently from artifact consistency. If G-ART is RED, the UI must label the issue as artifact/evaluator consistency before attributing behavior to SUT modules.
5. If sources disagree, store every source row and add a `gate_conflict` event. The UI must show a conflict badge instead of silently choosing a single gate status, except for the documented `overall_pass` precedence above.
6. If a mapped source is absent, store `UNKNOWN`; do not synthesize a PASS.

### `raw_records`

Optional raw payload store.

Required fields:

- `evidence_id`
- `session_id`
- `scenario_id`
- `sim_t`
- `wall_t`
- `topic`
- `payload_json`
- `payload_encoding`

Policy:

- First version may store raw records only for key topics used by drilldown.
- Full raw import is allowed by config for deep debugging.
- JSONL/gzip artifact remains the audit fallback.

## Ingestion Pipeline

### Sources

Ingest must accept existing evidence folders:

```text
runs/trace_eval/<session_name>/
  manifest.json
  batch_summary.json
  <scenario_id>.json
  <scenario_id>.trace_current.jsonl
  <scenario_id>.trace_current.jsonl.gz
  <scenario_id>.artifact_consistency.json
  <scenario_id>_trajectory_dashboard.png
  logs/postprocess.jsonl
```

Artifact discovery rules:

- Use `manifest.json` scenario entries for declared trace/report/png paths.
- Also scan the session directory for known sibling artifacts that are not listed in the manifest, including `<scenario_id>.artifact_consistency.json` and `batch_summary.json`.
- Only ingest whitelisted artifact names/patterns listed above.
- Ignore unrelated files by default and record them only as `other` when a future explicit config enables that behavior.
- Never infer gate status from an unrecognized filename.

### Steps

1. Read `manifest.json`.
2. Resolve scenario entries.
3. Discover whitelisted sibling artifacts not listed in the manifest.
4. Read `batch_summary.json` if present.
5. Read per-scenario `TraceEvaluationReport`.
6. Read `artifact_consistency.json` if present.
7. Parse raw trace JSONL or JSONL.gz.
8. Insert/update `sessions`, `scenarios`, and `artifacts`.
9. Insert full-resolution `trajectory_samples`.
10. Derive `trajectory_downsample`.
11. Derive `state_segments`.
12. Insert `events`.
13. Insert `gate_results` from evaluator outputs using the mapping and precedence rules above.
14. Verify transaction by row counts, artifact checksums, and scenario validity.
15. Apply retention policy only if the root permits mutation.

### Idempotency

Each ingest run must be repeatable.

Key identity:

- `evidence_id`
- `root_id`
- `session_path`
- `session_id`
- `scenario_id`
- artifact relative path
- artifact mtime and sha256 where available

If an artifact changes, reingest that scenario and replace derived rows for the same session/scenario in a transaction.

### Retention

Default:

```text
compress_after_ingest
```

Activation gate:

- The configured default is `compress_after_ingest`.
- Effective runtime policy downgrades to `keep` until the implementation includes gzip-compatible readers for the Evidence Library, dashboard regeneration, and any repo tool invoked by Screen 04.
- External roots default to `allow_retention_mutation=false`; compression cannot mutate them unless explicitly enabled by the operator.

Rules:

- Do not modify raw JSONL until SQLite transaction succeeds and verification passes.
- Do not apply retention until all existing postprocessors for that session have finished.
- Do not apply retention unless the source root has `trusted=true` and `allow_retention_mutation=true`.
- After success, compress `<scenario_id>.trace_current.jsonl` to `<scenario_id>.trace_current.jsonl.gz`.
- Preserve `manifest.json`, report JSON, artifact consistency JSON, dashboard PNG, and postprocess logs.
- Update `artifacts.compressed=true`.
- Keep `manifest.json` as the original run manifest. Do not rewrite source-of-truth probe artifacts just to reflect retention.
- Any repo tool that reads retained evidence through the new Evidence Library must support both `.trace_current.jsonl` and `.trace_current.jsonl.gz`.
- A restore/export action must be available to recreate raw JSONL from SQLite or gzip for legacy tools.
- If ingest fails, keep the original JSONL and mark `sessions.ingest_status='failed'`.

Other supported policies:

- `keep`
- `delete_after_ingest`

`delete_after_ingest` is not default because current probe tooling still treats raw trace JSONL as part of evidence.

## Decision Frame

The key debug API is the time-aligned decision chain:

```text
decision_frame(evidence_id, scenario_id, sim_t)
```

It returns the chain facts at or around `sim_t`.

### Selection Rules

- Dense trajectory: nearest sample to `sim_t`, with tolerance metadata.
- Module state: segment where `start_t <= sim_t < end_t`.
- Event context: nearby events in a configurable window, default +/- 5 s.
- Gates: imported evaluator artifacts. They are labelled as `final_run_verdict`, `phase_window_verdict`, or `artifact_consistency`; the UI must not imply a final scenario gate is a time-local fact.
- Missing field: return `UNKNOWN`, not a frontend-inferred value.
- Chain row `status` values are diagnostic display labels. They are not pass/fail verdicts unless backed by a gate or module oracle source field in `status_source`.

### Decision Frame Shape

```json
{
  "evidence_id": "...",
  "session_id": "...",
  "scenario_id": "colreg-rule14-ho",
  "sim_t": 316.3,
  "own_ship": {
    "lat": 0.0,
    "lon": 0.0,
    "heading_deg": 0.0,
    "sog_kn": 0.0
  },
  "targets": [
    {
      "target_id": "T01",
      "lat": 0.0,
      "lon": 0.0,
      "bearing": "starboard",
      "cpa_m": 0.0,
      "tcpa_s": 0.0,
      "confidence": 0.0
    }
  ],
  "chain": {
    "M2": {
      "status": "OK",
      "status_source": "diagnostic_availability",
      "facts": {}
    },
    "M6": {
      "status": "CHECK",
      "status_source": "diagnostic_availability",
      "facts": {
        "rule": "Rule14",
        "role": "give_way",
        "preferred_direction": "starboard",
        "phase": "active",
        "release_predicted": false
      }
    },
    "M4": {
      "status": "OK",
      "status_source": "diagnostic_availability",
      "facts": {}
    },
    "M5": {
      "status": "WARN",
      "status_source": "diagnostic_availability",
      "facts": {
        "solver_status": "VALID",
        "plan_status": "NORMAL",
        "route_hash": "...",
        "waypoint_count": 0
      }
    },
    "L4": {
      "status": "WARN",
      "status_source": "diagnostic_availability",
      "facts": {
        "execution_state": "DEFERRED",
        "reason": "avoidance_active"
      }
    },
    "M7": {
      "status": "OK",
      "status_source": "diagnostic_availability",
      "facts": {}
    }
  },
  "gates": [
    {
      "gate_id": "G-SEM",
      "status": "FAIL",
      "temporal_scope": "final_run_verdict",
      "source": "TraceEvaluationReport",
      "payload": {}
    }
  ],
  "nearby_events": []
}
```

## Backend API

New Screen 04 library/query endpoints live under:

```text
/api/v1/evidence-library
```

Existing evidence session lifecycle endpoints under `/api/v1/evidence/session/*` remain compatible and are not replaced in this spec. Frontend simulation finalization may call the new ingest service after the existing finalize path succeeds.

### Config

```text
GET /api/v1/evidence-library/config
PUT /api/v1/evidence-library/config
```

Requirements:

- Return repo default, machine override, and effective merged config.
- Save only machine override.
- Validate root paths/globs before saving.
- Refuse path traversal in roots that are intended to be repo-relative.
- Default new external roots to `trusted=false`, `allow_retention_mutation=false`, and `follow_symlinks=false`.

### Roots And Worktrees

```text
GET /api/v1/evidence-library/roots
```

Returns:

- effective roots
- resolved paths
- root health
- discovered worktree rows
- stale indicator

### Rescan

```text
POST /api/v1/evidence-library/rescan
```

Body:

```json
{
  "root_ids": ["primary", "worktrees"],
  "force": false
}
```

Behavior:

- Launch rescan synchronously for small updates or as background task for large roots.
- Return job id if background.

### Sessions

```text
GET /api/v1/evidence-library/sessions
GET /api/v1/evidence-library/sessions/{evidence_id}
```

Filters:

- source
- suite
- root_id
- worktree_name
- branch
- scenario_id
- verdict
- first_failed_gate
- first_failed_module
- created_at range

### Replay

```text
GET /api/v1/evidence-library/sessions/{evidence_id}/scenarios/{scenario_id}/replay
```

Returns:

- session metadata
- scenario metadata
- downsample trajectory
- event timeline
- module status summary
- gate summary
- artifact links

### Decision Frame

```text
GET /api/v1/evidence-library/sessions/{evidence_id}/scenarios/{scenario_id}/decision-frame?sim_t=316.3
```

Returns the time-aligned chain snapshot described above.

### Raw Artifact Access

```text
GET /api/v1/evidence-library/artifacts/{artifact_id}
```

Requirements:

- Serve only paths registered in `artifacts`.
- Support `.gz` trace download.
- Do not serve arbitrary filesystem paths.

## Frontend Design

### Evidence Library

Primary layout:

- Header: source tabs and root status.
- Filter rail: source, root, branch, worktree, suite, scenario, verdict, date.
- Session table: session id, source, suite, scenario count, verdict summary, first failure, worktree/branch, created time, ingest status.
- Preview panel: selected session summary, artifacts, latest dashboard preview, key KPIs.
- Settings drawer: roots config and worktree list.

Source tabs:

- All
- Frontend simulations
- Background probes
- Worktrees
- External roots

### Replay Detail

Default view:

- Main area: sea chart trajectory replay.
- Bottom: scrubber with event marks and playback controls.
- Right rail: KPI summary, gate summary, COLREGs chain status cards.
- Artifact actions: open dashboard PNG, download report, export package.
- Back action: Evidence Library.

Important rule:

- The default replay view must prioritize chart playback.
- The full chain inspector opens when user clicks a FAILED/WARN module, a gate, or an event mark.

### Chain Inspector

Inspector presentation:

- Time-bound decision frame header.
- M2/M6/M4/M5/L4/M7/Gates rows.
- Each row shows source topic, current facts, status, and evidence link.
- Use source-of-truth gate values imported from evaluator artifacts.
- No frontend recomputation of pass/fail.

Use cases:

- Compare M6 rule/role/direction at time T with M5 route/solver state.
- Show whether L4 rejected/deferred a plan.
- Show whether M7 veto/MRM fired.
- Show whether G-ART says artifacts are inconsistent.

## Compatibility With Current Probe System

The implementation must keep current CLI probe workflows valid.

Compatibility requirements:

- Existing `runs/trace_eval/<session>/manifest.json` remains accepted.
- Existing per-scenario report JSON remains accepted.
- Existing raw trace JSONL remains accepted.
- Existing dashboard PNG remains accepted.
- Existing `artifact_consistency.json` remains accepted.
- Existing `batch_summary.json` remains accepted.
- Existing empty-session cleanup behavior remains valid.

No changes to current strict gate expression are part of this spec.

The UI may surface gate explanations, but it may not change verdict semantics.

## Error Handling

### Missing Root

- Mark root as unavailable.
- Keep sessions already indexed.
- Show stale/unavailable badge.

### Missing Artifact

- Mark artifact unavailable.
- Keep session row.
- Replay may degrade if SQL has sufficient data.

### Ingest Failure

- Keep raw JSONL.
- Record `ingest_status='failed'`.
- Store error message.
- Do not compress/delete source trace.

### Partial Session

- If `manifest.valid_data=false`, hide by default unless `include_empty=true`.
- Allow settings toggle to show invalid/empty sessions.

### Schema Drift

- Unknown fields preserved in JSON payload columns.
- Ingest must tolerate older reports with missing `chain_summary` or missing G-ART.
- Missing chain fields become `UNKNOWN`.

## Security And Safety

- Machine config may contain absolute paths; do not commit machine config.
- Docker deployments must mount `MASS_L3_CONFIG_HOME` when persistent settings/index are required.
- Artifact serving must be path-safe and only serve indexed artifacts.
- Root scanning resolves every candidate path and enforces containment under the configured root.
- Symlinks are not followed unless `follow_symlinks=true` is explicitly set for a trusted root.
- Retention mutation is allowed only for roots with `trusted=true` and `allow_retention_mutation=true`.
- Artifact ingest is limited to whitelisted filenames/patterns. Unknown files are not served by default.
- External roots are read-only by default.
- `PUT /config` validates config JSON before writing.
- Rescan never deletes evidence folders.
- Compression only happens after successful verified ingest.
- `delete_after_ingest` requires explicit config and should not be default.
- Retention must not break the ability to export or restore original trace data for audit/reproduction.

## Storage Maintenance

Full-resolution trajectory storage is intentional for the first version, but the database still needs explicit maintenance controls.

Required controls:

- Show database size in Evidence Settings.
- Show session row counts and largest sessions.
- Support prune of unavailable indexed sessions without deleting source artifacts.
- Support archive/export of selected sessions.
- Run `VACUUM` only through an explicit maintenance action, not during normal page load.
- Keep downsample tables rebuildable from full-resolution SQL or retained raw traces.

## Testing And Verification

### Unit Tests

Backend:

- Config load/merge/default behavior.
- Config save to `~/.config/mass-l3/evidence_library.json`.
- Atomic config writes and permissions errors.
- Root glob expansion with `{repo_root}`.
- SQLite schema creation and migration.
- Idempotent ingest of the same session.
- Two roots with the same `session_id` produce different `evidence_id` values.
- Ingest from JSONL and JSONL.gz.
- Retention policy `compress_after_ingest`.
- Retention restore/export of raw JSONL.
- Missing artifact handling.
- Decision frame selection rules.
- Read-only mode when `MASS_L3_CONFIG_HOME` is not writable.
- Path traversal and symlink escape rejection.
- Gate source precedence and conflict recording.
- No-recompute assertion: missing source gate imports as `UNKNOWN`, not `PASS`.
- Concurrent rescan transaction locking and rollback.
- Large JSONL ingest performance budget.

Frontend:

- `#/evaluator` opens Evidence Library.
- `#/evaluator/{evidence_id}` opens Replay Detail.
- Evidence Library filters call session API.
- Settings drawer loads/saves config.
- Replay Detail renders chart replay from API data.
- Clicking FAILED/WARN module opens Chain Inspector.

### Integration Tests

- Seed a synthetic `runs/trace_eval/<session>` fixture.
- Run rescan.
- Assert session/scenario/artifact rows exist.
- Assert trajectory rows exist.
- Assert state segments exist for M6/M5/L4.
- Assert decision-frame API returns M2/M6/M5/L4/Gate facts.
- Assert original JSONL becomes `.gz` after verified ingest under default retention.
- Assert gzip-compatible replay still works after retention.
- Assert mismatched batch/report verdicts show a conflict badge and preserve authoritative precedence.
- Assert artifact consistency RED is surfaced as G-ART and not attributed to SUT behavior.

### Probe Compatibility Test

Use a real or fixture `colregs-probe` evidence folder containing:

- `manifest.json`
- per-scenario JSON report
- raw trace JSONL
- dashboard PNG
- `batch_summary.json` or `artifact_consistency.json` where available

Expected:

- Ingest succeeds.
- UI verdict matches report/batch summary.
- G-ART result is shown as source-of-truth artifact consistency.
- No frontend gate recomputation.
- Existing lifecycle endpoints under `/api/v1/evidence/session/*` remain compatible.

## Definition Of Done

Design is complete when:

- A written spec is committed on a task branch.
- A reviewer can map every UI verdict back to imported evaluator artifacts.
- The spec defines machine config, SQLite schema, ingest, retention, APIs, frontend states, and verification.
- The spec explicitly preserves current `colregs-probe` evidence contract.
- An adversarial review has checked for duplicate gate logic, unsafe file access, excessive scope, and mismatch with current probe artifacts.
