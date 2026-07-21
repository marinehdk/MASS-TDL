# Evidence Library Background Rescan Design

**Date:** 2026-07-20

## Problem

`POST /api/v1/evidence-library/rescan` currently performs the complete SQLite rebuild on the Uvicorn event-loop thread. A large evidence store therefore blocks health, session-list, and frontend requests until scanning finishes. `force` reaches `ingest_session` but has no effect, so every scan deletes and reinserts every session even when source files are unchanged. The frontend replaces the library with `Loading evidence` while awaiting the long request and exposes no progress.

## Scope

- M8 Evidence Library UI and SIL orchestrator Evidence Library service only.
- No ROS2 topic, message, or IDL change.
- No ODD, COLREGs, M5, M7, planner, guidance, or safety-behavior change.

## Design

### Background single-flight scan

`POST /api/v1/evidence-library/rescan` starts one process-local background scan and immediately returns HTTP 202 with its job snapshot. If a scan is already queued or running, the endpoint returns that existing job instead of starting a second database writer.

The scan manager owns a single-worker `ThreadPoolExecutor`, a lock-protected immutable snapshot, and terminal exception capture. The worker invokes the existing service scan path outside the ASGI event loop. Existing deletion/scan filesystem coordination remains authoritative.

`GET /api/v1/evidence-library/rescan/status` returns the latest snapshot. Stable fields:

- `job_id`
- `state`: `idle`, `queued`, `running`, `completed`, or `failed`
- `force`
- `total`, `processed`, `ingested`, `skipped`, `pruned`
- `errors`, `cleanup_pending`
- `started_at`, `finished_at`

Progress updates occur after each discovered session and on terminal transition. Status reads do not open the evidence database.

### Incremental ingest and force

Before destructive per-session ingest, `ingest_session` computes current `latest_mtime` and checks the existing session row. With `force=false`, matching `latest_mtime` plus `ingest_status='ok'` returns a skipped result and performs no delete or insert. With `force=true`, the session is always reingested.

`IngestResult` gains a `skipped` marker. Aggregate scan results report both `ingested` and `skipped`; `processed` is their sum plus failed session attempts.

### Frontend behavior

The scan button posts once, stores the returned snapshot, and polls the status endpoint until `completed` or `failed`. Existing session rows remain visible during scanning. Button text reports processed/total progress and remains disabled while the job is active. On completion, the library refetches once. Initial `Loading evidence` remains limited to the first session-list load when no data is available.

Polling stops on unmount and on terminal/error paths. Backend errors remain visible to the operator.

## Failure handling

- Worker exceptions become a `failed` snapshot; they cannot leave the job permanently `running`.
- Per-session ingest errors remain accumulated while later sessions continue.
- A conflicting deletion lock returns the existing service error as a completed scan result.
- A second start request is idempotent while the current job is active.
- Process restart resets process-local status to `idle`; persisted evidence remains unchanged.

## Acceptance

1. Start endpoint returns before a deliberately blocked scan finishes.
2. Concurrent start requests share one job.
3. Health and session-list routes respond while the worker is blocked.
4. Unchanged sessions skip writes; `force=true` reingests them.
5. Progress reaches a terminal snapshot on success and failure.
6. Frontend retains rows, displays progress, polls, and refetches after completion.
7. Evidence backend tests, evaluator tests, and frontend build pass.
8. Isolated A4000 display deployment verifies responsiveness without changing `m5-design-grounding` containers or ports.
