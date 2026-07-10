# Evidence Library Session Lifecycle Design

## Goal

Make Screen 04 a reliable manager for unified simulation runs: scanning reconciles the SQLite index with disk, the table supports fast investigation filters, and an operator can permanently remove one run with an explicit confirmation.

## Scope

- Rename the evaluator action from `刷新` to `扫描`; loading text becomes `扫描中`.
- A scan ingests discoverable sessions and removes database-only records whose indexed session directory or `manifest.json` no longer exists.
- Add client-side free-text filtering across session ID, scenario, source, worktree, suite, mode, and result. Keep existing column enum filters, ordering, pagination, overview, and replay behavior.
- Replace the flat table treatment with a compact data-grid treatment: persistent toolbar, restrained header contrast, result chips, denser rows, clearly grouped actions, and responsive horizontal overflow rather than content overlap.
- Add a `删除` action with a confirmation dialog. It deletes both run files and all related SQLite rows.

## Run Ownership And Deletion Boundary

The active producer uses unified storage. For a stored `session_path` ending in `runs/<run_id>/trace`, deletion target is exactly its parent `runs/<run_id>` directory. This includes `trace/`, `run_meta.json`, summary artifacts, and per-scenario images.

Legacy `runs/trace_eval/<session>` entries keep their current session directory as the deletion target. The service derives the target from the persisted session path; it never accepts an arbitrary filesystem path from the browser.

Deletion is allowed only when all conditions hold:

1. `evidence_id` resolves to exactly one indexed session.
2. The corresponding configured root is enabled and trusted.
3. The indexed session path and derived deletion target resolve under that configured root's matched path.
4. Neither the session path nor deletion target is a symlink.

If files were already removed, the API still deletes the orphaned database record. If filesystem deletion fails, it returns an error and leaves database rows intact.

## Backend Contract

### `POST /api/v1/evidence-library/rescan`

Existing request remains `{ "force": boolean }`. Response gains `pruned`, the count of stale indexed sessions removed after discovery. A session is stale when its indexed directory is absent, is not a directory, or has no `manifest.json`. Scan errors remain per-path and do not abort other roots.

### `DELETE /api/v1/evidence-library/sessions/{evidence_id}`

No request body. Successful response:

```json
{
  "evidence_id": "...",
  "deleted_path": "/repo/runs/20260710_120000",
  "filesystem_deleted": true
}
```

`404` means no indexed evidence ID. `409` means an unsafe or untrusted configured root. `500` means a validated filesystem deletion failed and database rows were retained.

Database cleanup deletes matching rows from `trajectory_downsample`, `trajectory_samples`, `state_segments`, `events`, `gate_results`, `artifacts`, `scenarios`, then `sessions`, within one transaction.

## Frontend Behavior

Toolbar layout: session counts on the left; title centered; free-text search, automatic scan interval, countdown, and `扫描` on the right. Default interval remains 24 hours. Manual scan invalidates the session query and displays the returned ingest/prune result without altering active filters.

The table uses a fixed header, 20/50 page size, existing sort controls, and existing enum selects. Results are rendered as small green/red/neutral chips. The action cell contains `概述`, `回放`, and danger-style `删除`; disabled overview/replay actions remain visually muted. Search resets pagination to page one.

Pressing `删除` opens a modal naming the session ID and the exact run directory to be removed. Confirming calls the DELETE endpoint, closes any overview modal for that session, and refreshes the session list. Cancel makes no request.

## Error Handling

- A failed scan keeps existing visible data and shows its error state in the toolbar.
- A failed delete keeps the row visible and shows the server error in the confirmation modal.
- Stale records are hidden by the list service even before a user runs scan; the next scan removes their persisted rows.
- The API never deletes a path supplied by the browser and never follows symlinks.

## Verification

- Python route/service tests cover stale-record pruning, unified `runs/<run_id>` recursive deletion, legacy session deletion, missing-file database cleanup, and untrusted-root rejection.
- React tests cover `扫描` wording, text filtering, deletion confirmation/cancel, DELETE success, and DELETE failure behavior.
- Browser verification on `http://192.168.121.50:55763/#/evaluator` confirms a scan hides a manually removed record and a delete removes one unified run directory and its table row.
