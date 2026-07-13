# Batch Delete Task 4 Report

## Status

Implementation complete on `codex/evidence-library-replay-impl`. Task 4 source, component tests, build, backend regression suite, and non-destructive browser verification completed. Task 3 review minor closed with a filter-scope regression test.

Task 4 commit: `950a2141f feat(evaluator): complete batch evidence deletion`.

## Scope

Changed owned paths only:

- `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
- `handoff/workspace_log.md`

No API, backend, ROS2, ODD, COLREGs, M5, or M7 source changed. `.agent/rules/superpowers.md` preserved unchanged.

## Implementation

- Added `删除所选（N）` action using snapshot of selected safe sessions.
- Added batch confirmation dialog with total, pass/fail/unknown, distinct non-empty `worktree_name` count, and permanent database/filesystem warning.
- Added batch mutation busy state across search, filters, sorting, table actions, selection, scan, page-size, and pagination controls.
- Added complete-success handling: clear selection, close dialog, restore stable search focus.
- Added partial-result handling: retain only failed IDs selected, show escaped per-item errors, keep failed snapshot, retry failed IDs only.
- Reused dialog focus trap, background `inert`, `aria-hidden`, Escape, overlay, and focus restoration behavior.
- Added whole-request failure message without dropping selected snapshot.
- Added Task 3 regression proving select-all-filtered stays at one selected safe failure while a safe passing row is filtered out.

## TDD Evidence

RED:

```text
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "batch confirmation|partial batch"
Test Files 1 failed
Tests 2 failed | 48 skipped
Expected cause: 删除所选 batch action/dialog absent.
```

GREEN:

```text
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "batch confirmation|partial batch"
Test Files 1 passed
Tests 2 passed | 48 skipped

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "excludes a safe filtered-out row"
Test Files 1 passed
Tests 1 passed | 49 skipped

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
Test Files 1 passed
Tests 50 passed
```

## Automated Verification

Frontend requested suite:

```text
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx \
  src/screens/__tests__/SimulationEvaluator.test.tsx
Test Files 1 failed | 2 passed
Tests 6 failed | 56 passed
```

Task-owned `EvidenceLibraryView` passed 50/50. `ReplayDetailView` passed 3/3. `SimulationEvaluator` retained exactly six pre-existing unrelated failures:

1. `renders replay detail successfully with indexed replay data`
2. `renders evidence library when no evidence id is bound`
3. `navigates from evidence library to selected replay`
4. `renders replay detail when evidence id is bound`
5. `falls back to evidence library when latest has no indexed session`
6. `ignores stale store evidence id when route evidence id is absent`

Failure output remains stale English text assertions (`session-123`, `Evidence Library`, `Open Replay`, `colreg-rule14-ho`) plus jsdom `HTMLCanvasElement.prototype.getContext`/MapLibre stderr. No Task 4 code path appears in these failures; scope not broadened.

Final green frontend rerun before commit:

```text
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
Test Files 2 passed
Tests 53 passed
```

Build:

```text
npm run build
tsc: pass
vite: 2396 modules transformed; build pass
Warnings only: two Foxglove eval warnings and one >500 kB chunk warning.
```

Backend Evidence Library regression:

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
59 passed in 22.64s
```

## Browser Verification

Target: `http://192.168.121.50:55763/#/evaluator`

Evidence: `runs/e2e/evidence_library_batch_delete_20260713_181544/`

Artifacts:

- `01_initial.png`
- `02_fail_filtered.png`
- `03_confirmation.png`
- `04_after_cancel.png`
- `checklist.json`

Observed at 1440x900:

- Automatic interval and countdown absent.
- `不通过` filter produced 132 records.
- Current-page selection, then all 132 filtered safe records selected.
- Selection remained `已选择 132 条` across next/back pagination.
- Confirmation showed `共 132 条`, `通过 0`, `不通过 132`, `未知 0`, `工作树 0`, and permanent warning.
- Dialog canceled. Playwright route blocked batch-delete POST defensively; recorded request list remained empty.
- Filter menu remained within viewport; toolbar clipping check empty; screenshots show no overlap or table-width regression.

No live deletion request issued.

## Review And Concerns

- CodeGraph blast-radius review found `EvidenceLibraryView` callers only in `SimulationEvaluator`; component test remains direct coverage.
- `git diff --check` passed before final handoff edit.
- Browser worktree count was zero because live records had empty `worktree_name`; derived display labels intentionally do not create synthetic summary worktrees, matching brief.
- Local OrbStack acceptance and A4000 acceptance were not run. Both remain required before promotion/push.
- Full requested frontend command remains red only from six explicitly pre-existing `SimulationEvaluator` harness failures listed above.

## Final Batch-Delete Review Fixes

Status: all Critical and Important findings in `final-review-950a2141f.md` closed by implementation commit `b439c342b` (`fix(evidence): harden batch deletion recovery`).

### Changed Files

- `src/sil_orchestrator/evidence_library/service.py`
- `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- `web/src/api/silApi.ts`
- `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

`routes.py` and `SimulationEvaluator.test.tsx` required no changes. `.agent/rules/superpowers.md` remained untouched.

### Behavior Closed

- Filesystem target now moves atomically into same-parent staging before database deletion. Database/delete/commit failure rolls back and restores original path.
- Batch processing continues in request order after per-item database, filesystem, or unexpected failures. Internal exception details are replaced with operator-safe messages.
- Successful database commit reports filesystem cleanup as `completed`, `not_needed`, or `pending`. Pending cleanup returns exact `cleanup_path` for manual recovery and is never offered as a destructive retry.
- Rejected/lost batch response invalidates authoritative query cache, marks outcome unknown, blocks further deletion, and requires a rescan. Rescan locks selection, clears snapshots, and re-enables selection only after successful reconciliation.
- Filter, row style, and confirmation use one canonical multi-scenario outcome: any failure is failed; all scenarios passed is passed; empty/partial data is unknown.
- Selected IDs, outcomes, and worktree metadata are immutable snapshots across query refresh. Toolbar supports `取消选择`; select-all disappears when every filtered safe row is selected.

### TDD Evidence

Initial RED:

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py \
  -k 'restores_staged_path_after_database_failure or post_commit_cleanup_failure or sanitizes_unexpected_failures or delete_unified_session_removes_run_dir'
4 failed, 44 deselected

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t 'canonical scenario-count|selected IDs|locks selection|cancel selection|rejected batch response|pending post-commit|batch confirmation|refreshes authoritative'
8 failed, 48 skipped
```

Malformed/missing body characterization before implementation:

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -k missing_or_malformed_body
5 passed
```

External-review follow-up RED:

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -k post_commit_cleanup_failure
1 failed: cleanup_path absent

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t 'pending post-commit'
1 failed, 55 skipped: cleanup_path absent from dialog
```

Focused GREEN:

```text
Backend review behaviors: 9 passed, 39 deselected
Frontend review behaviors: 8 passed, 48 skipped
Cleanup-path backend: 1 passed, 47 deselected
Cleanup-path frontend: 1 passed, 55 skipped
```

### Final Verification

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
66 passed in 1.86s

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
2 files passed; 59 tests passed

npm run build
tsc pass; Vite pass; 2396 modules transformed

git diff --check
pass
```

Requested three-file frontend run: 62 passed, six pre-existing `SimulationEvaluator.test.tsx` failures remained unchanged. Failures are stale English exact-text assertions plus jsdom MapLibre canvas stderr; no new hook mock was needed.

### Residual Risks

- `filesystem_cleanup=pending` requires manual removal of returned `cleanup_path`; no automatic ledger/sweeper added, per approved minimal recoverable protocol.
- Local OrbStack and A4000 acceptance not run. Both remain mandatory before promotion or push.
- No live destructive browser test issued.

## Re-review Blocker Closure After `5744dc703`

Status: all five blockers appended to `final-review-950a2141f.md` closed by
`a0016cd91` (`fix(evidence): close remaining deletion blockers`).

### Changed Files

- `src/sil_orchestrator/evidence_library/service.py`
- `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- `web/src/api/silApi.ts`
- `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

`routes.py` and `SimulationEvaluator.test.tsx` required no change.
`.agent/rules/superpowers.md` remained untouched and uncommitted.

### Behavior Closed

- Every fulfilled manual scan invalidates the Evidence Library list, including
  zero-change and partial-error responses. View explicitly awaits authoritative
  list refetch; unknown deletion state clears only after an error-free scan and
  successful list refresh.
- Unknown batch state disables row selection, single deletion, and batch
  deletion until reconciliation completes.
- Pending post-commit cleanup results merge by cleanup path across single delete
  and batch retry. Earlier paths and recovery metadata remain visible until the
  operator explicitly acknowledges each entry.
- Staging uses a deterministic evidence-ID/target hash plus an atomic sidecar
  containing exact evidence ID, original path, staged path, and metadata
  version. Rescan restores an interrupted pre-commit rename and protects failed
  recoveries from database pruning.
- Added focused zero-change/error refresh, unknown-state single delete,
  mixed-pending retry, single-pending acknowledgement, and repeated
  process-exit crash-recovery tests.

### TDD RED

```text
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t 'authoritative list refresh|unknown deletion state|rejected batch response|every fulfilled scan|every HTTP-200 scan error'
7 failed, 52 skipped: zero-change scan did not refetch; unknown state unlocked
before authoritative refresh; single deletion remained enabled.

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t 'single-delete cleanup path|preserves pending cleanup paths'
2 failed, 59 skipped: pending cleanup had no persistent operator-visible state.

PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py \
  -k 'post_commit_cleanup_failure or precommit_process_exit'
2 failed, 47 deselected: no cleanup metadata path and no deterministic recovery sidecar.
```

### Focused GREEN

```text
Frontend scan/unknown behaviors: 7 passed, 52 skipped
Frontend durable cleanup behaviors: 3 passed, 58 skipped
Backend cleanup metadata/crash recovery: 2 passed, 47 deselected
Rejected-response asynchronous cache assertion: 1 passed, 60 skipped
```

External uncommitted review initially exposed one non-reproducible cache-test
timing failure. The assertion now waits for both GET count and reconciled cache
contents; focused rerun passed. External review verdict: no actionable
correctness defects.

### Final Verification

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
67 passed in 1.99s

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
2 files passed; 64 tests passed

npm run build
tsc pass; Vite pass; 2396 modules transformed

git diff --check
pass
```

Requested three-file frontend run: 67 passed; six unchanged pre-existing
`SimulationEvaluator.test.tsx` failures. Failures remain stale English text
assertions and jsdom MapLibre canvas support; no new hook mock was required.

### Residual Risks

- Post-commit cleanup remains explicit/manual. Deterministic payload and sidecar
  stay on disk; no schema ledger, daemon, or automatic destructive retry added.
- Local OrbStack and A4000 acceptance not run. Both remain promotion gates.
- No live destructive browser test issued. Build retains existing Foxglove
  `eval` and large-chunk warnings.

## Final Adversarial Recovery Closure After `973e87540`

Status: all five merge blockers under `Re-review After 973e87540` closed by
implementation commit `7a17d192d` (`fix(evidence): make deletion recovery
restart durable`).

### Changed Files

- `src/sil_orchestrator/evidence_library/service.py`
- `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

No route/API/schema files changed. `.agent/rules/superpowers.md` remained
untouched and unstaged.

### Behavior Closed

- Deletion writes an atomic central recovery record under
  `config_home/.evidence-library-delete-recovery` before the same-parent staging
  rename. A retry before rescan detects and restores an interrupted pre-commit
  stage before evaluating deletion again.
- Recovery uses the central record to preserve the evidence-ID/original-path
  mapping when the configured root was removed or path validation now fails.
  The affected database row is protected from scan pruning and the scan returns
  a sanitized recovery error.
- Post-commit payloads remain discoverable after the database row and backend
  process are gone. Rescan reports them in durable `cleanup_pending` results;
  the view merges those results into versioned local storage and retains the
  notice across reload until explicit acknowledgement.
- Final and `.json.tmp` metadata are treated as recovery inputs. Valid temporary
  metadata can be promoted; invalid/interrupted metadata fails closed and keeps
  the evidence ID protected instead of silently pruning or deleting it.
- Per-evidence kernel locks reject concurrent duplicate/single deletion. A
  shared/exclusive scan coordination lock prevents rescan from restoring or
  pruning an active deletion. Locks are released by the kernel on process exit.

### Strict TDD Evidence

Initial blocker RED:

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py \
  -k 'retry_before_rescan or recovery_target_drifts or temporary_sidecar or postcommit_cleanup_after_process_exit'
5 failed, 49 deselected

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t 'rescan-discovered cleanup notice'
1 failed, 61 skipped
```

Initial blocker GREEN:

```text
Backend focused: 5 passed, 49 deselected
Frontend reload notice: 1 passed, 61 skipped
```

Adversarial-review follow-up RED/GREEN cycles:

```text
Pre-commit recovery exit before metadata rewrite:
RED 1 failed, 54 deselected
GREEN 1 passed, 54 deselected

Concurrent duplicate deletion during active stage:
RED 1 failed, 56 deselected
GREEN 1 passed, 56 deselected

Partial and valid recovery-created temporary metadata:
RED 2 failed, 55 deselected
GREEN 2 passed, 55 deselected

Rescan concurrent with active deletion:
RED 1 failed, 57 deselected
GREEN 1 passed, 57 deselected

Combined focused recovery command:
9 passed, 49 deselected in 0.54s
```

The final uncommitted adversarial review reported no actionable correctness
defects after inspecting the complete diff and rerunning backend, frontend, and
build coverage.

### Final Verification

```text
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
76 passed in 2.42s

npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
2 files passed; 65 tests passed

npm run build
TypeScript pass; Vite pass; 2396 modules transformed

git diff --check
pass
```

### Residual Risks

- Recovery and cleanup intentionally fail closed. Corrupt operator-modified
  recovery metadata requires manual inspection/cleanup; no automatic destructive
  retry, schema ledger, or daemon was added.
- File locking uses Linux `fcntl.flock`, matching deployed hosts. Persistent lock
  files are inert bookkeeping; lock ownership is kernel state.
- Local OrbStack and A4000 acceptance were not run. Both remain promotion gates.
- No live destructive browser test issued. Build retains existing Foxglove
  `eval` and large-chunk warnings.
