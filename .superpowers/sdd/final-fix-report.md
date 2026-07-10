# Final Whole-Branch Review Fix Report

Date: 2026-07-10

Branch: `codex/evidence-library-replay-impl`

Starting HEAD: `39f7322cf220db655ca5c586fd47f1df46f85c7d`

Fix commit: `ad649d186 fix(evidence): close final lifecycle review findings`

## Task Header

- Affected module: M8 evidence library backend and Screen 04 frontend.
- Tracked files changed:
  - `src/sil_orchestrator/evidence_library/service.py`
  - `src/sil_orchestrator/tests/test_evidence_library_routes.py`
  - `web/src/api/silApi.ts`
  - `web/src/screens/evaluator/EvidenceLibraryView.tsx`
  - `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
- Reviewed without change: `src/sil_orchestrator/evidence_library/routes.py`.
  Existing routes return service list fields directly and preserve DELETE
  404/409 translation plus overview `FileResponse` behavior.
- ROS2 topics/messages/IDL: none.
- ODD, COLREGs, and M5/M7 boundary impact: none.
- SIL scenarios: none. Controller will rerun live browser proof after merge.
- Ignored live e2e script: untouched.

## Root Causes

1. The delete target resolver existed only inside DELETE handling. Session list
   responses exposed only `session_path`, causing the browser to display the
   indexed trace directory instead of the actual deletion target.
2. Prune health used `Path.is_dir()`/`is_file()`, which follow symlinks.
   `list_sessions()` did no physical-health filtering before returning rows.
3. Overview lookup resolved both the artifact and indexed session first. A
   post-index session-directory symlink swap could therefore make both paths
   resolve outside the configured root and still pass relative containment.
4. Escape called dialog cancellation without checking mutation loading.
   Delete retry also cleared the prior error before the retry completed.
5. Successful DELETE only invalidated the RTK tag. If the resulting GET failed,
   stale cached data retained the deleted row and the focus guard stayed active.
6. Successful scan results were cleared at the next attempt and rendered only
   when their `errors` array was non-empty.

## Fixes

### Backend

- Added one `_resolve_deletion_target()` path used by both DELETE and session
  list previews. Each healthy list row now returns:
  - `deletion_allowed`
  - `deletion_target`
  - `deletion_error`
- Unified previews resolve to the exact `runs/<run_id>` parent. Legacy previews
  resolve to the indexed session directory. Unsafe/untrusted/mismatched targets
  return no preview and remain rejected by DELETE with HTTP 409.
- Added `_session_path_is_healthy()` with absolute-path, directory, manifest,
  and every-path-component symlink checks.
- Rescan discovery skips symlink-invalid candidates. Pruning removes indexed
  session/manifest/ancestor-symlink records. Listing hides absent,
  non-directory, missing-manifest, and symlink-invalid rows before scan while
  leaving their SQLite rows for the next prune.
- Overview retrieval reloads current config, checks the indexed `root_id`,
  revalidates the current configured match, rejects all session/artifact
  symlink components, and checks resolved artifact containment before returning
  the path to `FileResponse`.
- Added a direct SQLite corruption regression for
  `sessions.session_id != runs/<run_id>`; list preview disables deletion and
  DELETE returns 409 without removing the run.

### Frontend

- Extended `EvidenceLibrarySession` with backend-owned deletion preview fields.
  The confirmation renders only `deletion_target`; it never derives a path from
  `session_path`. Unsafe/missing previews disable the row action.
- Centralized cancellation loading guard. Escape, Cancel, and overlay click
  cannot close an in-flight DELETE. Prior delete errors stay visible during a
  retry and remain after another rejection.
- DELETE `onQueryStarted` patches the subscribed session cache immediately on
  fulfillment, then preserves result-dependent tag invalidation. A deferred or
  failed invalidated GET cannot restore the deleted row or indefinitely retain
  the post-delete focus guard.
- Successful scan payloads always render ingested/pruned/error counts. The
  latest successful payload remains visible while a later request failure is
  shown separately.
- Existing search, enum composition, pagination, grid, overview controls,
  replay action, inert background, focus trap, and focus restoration behavior
  remain covered.

## TDD Evidence

Backend RED:

```text
8 failed, 23 deselected in 0.55s
```

Failures covered stale list health, manifest/session/ancestor symlinks, unified
and legacy target previews, unsafe preview state, indexed session-ID mismatch,
and overview symlink swap escape.

Backend focused GREEN:

```text
8 passed, 23 deselected in 0.49s
```

Frontend RED:

```text
6 failed, 25 passed (31)
```

Failures covered exact server target display, unsafe target disablement,
always-visible scan counts, in-flight Escape, retry error retention, and stale
refetch cache behavior.

Frontend GREEN:

```text
EvidenceLibraryView.test.tsx: 31 passed in 2.28s
```

The RTK regression holds the invalidated GET unresolved, verifies cache removal
before releasing the network response, then releases HTTP 500 and verifies the
patched cache remains.

## Verification

Backend evidence suites:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
```

Result: `51 passed in 1.61s`.

Focused frontend aggregate:

```text
EvidenceLibraryView: 31 passed
ReplayDetailView: 3 passed
SimulationEvaluator: 3 passed, 6 failed
Aggregate: 37 passed, 6 failed, 43 total
```

All six focused failures are unchanged base selector assertions. They expect
old standalone English or exact unprefixed text while the current UI renders
Chinese labels/prefixed values.

Full web suite, run once:

```text
Test Files: 45 passed, 2 failed, 47 total
Tests: 302 passed, 7 failed, 309 total
```

Base classification used `6378001d4` and reran the two failing files only:

```text
15 passed, 7 failed, 22 total
```

The branch and base have the same seven failure identities:

1. `SimulationEvaluator > renders replay detail successfully with indexed replay data`
2. `SimulationEvaluator > renders evidence library when no evidence id is bound`
3. `SimulationEvaluator > navigates from evidence library to selected replay`
4. `SimulationEvaluator > renders replay detail when evidence id is bound`
5. `SimulationEvaluator > falls back to evidence library when latest has no indexed session`
6. `SimulationEvaluator > ignores stale store evidence id when route evidence id is absent`
7. `telemetryStore > normalizes M5 avoidance plan waypoints`

No new full-suite failure identity was introduced. Six new passing tests account
for the total increase from the prior branch snapshot.

Production build:

```text
tsc: pass
vite: 2396 modules transformed; built in 5.35s
```

Retained warnings: Foxglove dependency `eval` use and a 1,804.12 kB output
chunk above Vite's 500 kB warning threshold.

Scope checks:

```text
git diff --check: pass
Tracked diff: exactly the five owned files listed in Task Header
```

## Remaining Concerns

- Seven web assertions already fail at base `6378001d4`; user scope explicitly
  excludes modifying those unrelated tests.
- No browser, container, SIL, or A4000 gate was run. Controller will rerun the
  ignored live e2e proof after merge.
- Filesystem validation and later `FileResponse`/`shutil.rmtree` access are not
  an fd-based atomic operation. The reviewed post-index symlink swap cases are
  rejected, but a privileged concurrent attacker can still race path checks.

---

## Final Important Race Fix

Date: 2026-07-10

### Task Header

- Affected module: M8 Web HMI, Screen 04.
- Changed paths:
  - `web/src/api/silApi.ts`
  - `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
  - `.superpowers/sdd/final-fix-report.md`
- ROS2 topics/messages/IDL: none.
- ODD, COLREGs, and M5/M7 boundary impact: none.
- SIL scenarios: none.

### Root Cause and Fix

DELETE fulfillment patched only currently cached sessions. A sessions GET that
started before DELETE could fulfill later and replace that cache with its stale
payload. If tag invalidation then issued a second GET which failed, RTK Query
retained the stale payload, resurrecting the deleted row and keeping the UI's
post-delete focus guard active.

Added a process-lifetime tombstone set for successful evidence IDs. Sessions
responses now filter tombstoned IDs in `transformResponse`, before RTK Query
writes any response to cache. The existing fulfillment patch remains for
immediate removal. Therefore both older in-flight and later successful GETs
cannot restore a deleted row; failed invalidation no longer determines cache
correctness.

### TDD Evidence

RED command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "does not resurrect a deleted row when an older sessions request wins before a failed refetch"
```

RED result: `1 failed | 31 skipped`. Exact reproduced cache mismatch:

```text
Expected: ["fedcba98-7654-3210-fedc-ba9876543210"]
Received: ["pending-before-delete", "fedcba98-7654-3210-fedc-ba9876543210"]
```

Regression controls exact order:

```text
GET-1 pending -> DELETE fulfilled -> GET-1 stale 200 -> GET-2 invalidation 500
```

GREEN command and result:

```bash
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

```text
Test Files  1 passed (1)
Tests       32 passed (32)
```

Production build:

```bash
npm run build
```

Result: TypeScript and Vite build passed; 2,396 modules transformed. Existing
Foxglove `eval` and chunk-size warnings remain.

Final scope/whitespace gate:

```bash
git diff --check
```

Result: pass. Diff contains only the three owned paths.

### Self-Review and Remaining Risk

- Tombstones are added only after successful DELETE and filtering occurs before
  cache insertion, covering the reviewed interleaving without depending on a
  successful invalidation request.
- Rejected DELETE behavior remains unchanged.
- Tombstones last until page reload. Reintroducing the exact same immutable
  evidence ID during one page lifetime remains hidden; the set also grows by
  one entry per successful interactive deletion. Both are accepted tradeoffs
  for durable client-side deletion consistency.
- No browser, SIL, container, or A4000 gate required for this cache-only fix.
