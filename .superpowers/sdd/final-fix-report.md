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

---

## Complete Final Review Finding Fix

Date: 2026-07-10

Starting HEAD: `fb2b7586f5aa625facc24bb9bf91d0139a7ff34b`

### Task Header

- Affected modules: M8 Screen 04 and evidence-library backend.
- Changed paths:
  - `web/src/api/silApi.ts`
  - `web/src/screens/evaluator/EvidenceLibraryView.tsx`
  - `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
  - `src/sil_orchestrator/evidence_library/service.py`
  - `src/sil_orchestrator/tests/test_evidence_library_routes.py`
  - `.superpowers/sdd/final-fix-report.md`
- ROS2 topics/messages/IDL: none.
- ODD, COLREGs, and M5/M7 boundary impact: none.
- SIL scenarios: none.
- Evidence: focused frontend mounted-hook tests, backend evidence suites,
  TypeScript/Vite build, and Git whitespace/scope checks.

### Verified Root Causes

1. The module-level tombstone set outlived cache stores and scan generations.
   A deterministic evidence ID rebuilt at the same legal path was therefore
   filtered from every later successful list response.
2. The scan-triggered mounted list refetch remained active when DELETE began.
   Its stale response could win after DELETE. Aborting that exact running RTK
   query prevents the pre-delete response from writing cache state.
3. RTK Query invalidation middleware runs on mutation fulfillment before the
   `queryFulfilled` continuation patches cache. A failed invalidation refetch
   also makes a mounted hook expose its previous fulfilled value even when the
   raw selector contains patched data. The delete lifecycle now patches first,
   explicitly invalidates, awaits the refresh, and re-fulfills retained patched
   data only when that refresh fails.
4. The view tied search focus and every delete action to disappearance of the
   deleted row. Focus now runs after dialog inert cleanup and does not wait for
   list reconciliation; other delete actions are disabled only during DELETE.
5. Legacy deletion trust resolution depended on a live configured-root glob
   match. After the complete direct root disappeared, no match remained. A
   missing indexed path may now use only an exact configured glob-matching
   ancestor, after enabled/trusted and symlink checks; normal live-path,
   unconfigured-root, path-escape, and untrusted-root rejection stays intact.

### Production Fix

- Removed all process-lifetime evidence tombstones and response filtering.
- At DELETE start, abort and await the currently running
  `getEvidenceLibrarySessions` query for this API store.
- After DELETE success, patch the cached list, explicitly invalidate the
  EvidenceLibrary tag, and await the mounted refresh. If that refresh fails,
  use RTK `upsertQueryData` to publish the retained patched list as the latest
  fulfilled value. A later successful scan/list response remains authoritative,
  including legal reconstruction of the same evidence ID.
- Decoupled stable-search focus and remaining-row delete availability from
  deleted-row reconciliation.
- Added missing-root lexical validation only for absent indexed paths. The
  deletion target must still remain below the exact configured root match.

### TDD RED Output

Frontend command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "restores stable focus|aborts a mounted|shows a legally rebuilt"
```

Output before production changes:

```text
RUN  v2.1.9
EvidenceLibraryView.test.tsx (33 tests | 3 failed | 30 skipped)

FAIL EvidenceLibraryView > restores stable focus and keeps remaining delete
actions enabled without waiting for row reconciliation
expect(element).toHaveFocus()
Expected: search input
Received: BODY

FAIL evidence library RTK invalidation > aborts a mounted scan refetch before
delete so stale success and failed invalidation cannot resurrect a row
AssertionError: expected false to be true
Expected stale GET signal aborted: true
Received: false

FAIL evidence library RTK invalidation > shows a legally rebuilt session with
the same evidence ID after scan and list refresh
Expected: ["rebuilt-same-evidence-id", "fedcba98-7654-3210-fedc-ba9876543210"]
Received: ["fedcba98-7654-3210-fedc-ba9876543210"]

Test Files  1 failed (1)
Tests       3 failed | 30 skipped (33)
Duration    3.11s
```

The first cache-selector implementation then passed, but replacing the manual
subscription with a real mounted RTK hook exposed one additional RED:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "aborts a mounted scan refetch"
```

```text
RUN  v2.1.9
EvidenceLibraryView.test.tsx (33 tests | 1 failed | 32 skipped)

FAIL evidence library RTK invalidation > aborts a mounted scan refetch before
delete so stale success and failed invalidation cannot resurrect a row
expect(element).not.toBeInTheDocument()
Found: <button aria-label="mounted delete mounted-race-delete">

The raw RTK selector contained only B, but the mounted hook still rendered A+B
after GET-3 returned HTTP 500. This isolated the rejected-query/last-fulfilled
hook behavior addressed by the final upsert-on-refresh-failure step.

Test Files  1 failed (1)
Tests       1 failed | 32 skipped (33)
Duration    2.09s
```

Backend command:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py::test_delete_missing_legacy_root_removes_index_rows_only
```

Output before production changes:

```text
F                                                                        [100%]
FAILED test_delete_missing_legacy_root_removes_index_rows_only
assert response.status_code == 200
E assert 409 == 200
1 failed in 0.42s
```

### TDD GREEN Output

New frontend regressions:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "restores stable focus|aborts a mounted|shows a legally rebuilt"
```

```text
RUN  v2.1.9
PASS EvidenceLibraryView.test.tsx (33 tests | 30 skipped)
Test Files  1 passed (1)
Tests       3 passed | 30 skipped (33)
Duration    1.49s
```

New backend regression:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py::test_delete_missing_legacy_root_removes_index_rows_only
```

```text
.                                                                        [100%]
1 passed in 0.39s
```

### Final Verification Output

Focused frontend suite:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

```text
RUN  v2.1.9
PASS EvidenceLibraryView.test.tsx (33 tests)
Test Files  1 passed (1)
Tests       33 passed (33)
Duration    4.77s
```

Backend evidence suites:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
```

```text
....................................................                     [100%]
52 passed in 1.47s
```

Production build:

```bash
cd web
npm run build
```

```text
> tsc && vite build
vite v5.4.21 building for production...
2396 modules transformed.
dist/index.html                     0.73 kB | gzip:   0.42 kB
dist/assets/index-Bc4R5AAw.css     67.57 kB | gzip:  10.00 kB
dist/assets/index-ljVCTGe7.js   1,804.44 kB | gzip: 497.57 kB
built in 8.61s
```

Retained build warnings: Foxglove serialization uses `eval`; the main output
chunk exceeds Vite's 500 kB warning threshold.

Scope and whitespace:

```bash
git diff --check
git diff --name-only
```

```text
git diff --check: pass
Changed paths: exactly the six owned files in this task header.
```

### Self-Review

- Exact mounted order is asserted as `GET-1 200 -> SCAN 200 -> GET-2 pending
  -> DELETE 200 -> GET-2 200 -> GET-3 500`; GET-2's AbortSignal must be set.
- Final mounted hook contains no A and keeps B's delete button enabled.
- View test independently confirms stable search focus and no focus-guard lock.
- Same-store scan/list reconstruction restores the same deterministic evidence
  ID, with no module state, growth, reset hook, or cross-store pollution.
- Missing legacy root removes all related SQLite rows and reports
  `filesystem_deleted: false`; existing untrusted, disabled, escaped, symlink,
  and filesystem-failure tests stay green.
- No Playwright run: the mounted React/RTK regression directly executes the
  reviewed concurrency order and exposes the hook-specific stale-value path.

---

## Final Rejected-Delete Scan Recovery Fix

Date: 2026-07-13

### Task Header

- Affected module: M8 Web HMI evidence-library API lifecycle.
- Changed paths:
  - `web/src/api/silApi.ts`
  - `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
  - `.superpowers/sdd/final-fix-report.md`
- ROS2 topics/messages/IDL: none.
- ODD, COLREGs, and M5/M7 boundary impact: none.
- Required tests: rejected-DELETE/scan overlap regression, successful mounted
  DELETE race regression, replay wire-type compile check, full focused view
  suite, production build, and diff check.
- SIL scenarios and evidence output: none; test/build output recorded below.
- `EvidenceLibraryView.tsx`: intentionally unchanged.

### Root Causes and Minimal Fixes

1. DELETE aborted the running sessions query before `queryFulfilled` established
   success. A rejected DELETE therefore canceled the scan-triggered authoritative
   GET and returned without recovery, leaving the query rejected with stale A+B.
   DELETE now waits for fulfillment first. Rejections leave the independent scan
   GET untouched; successful deletes retain the existing abort, cache patch,
   invalidation, and failed-refresh recovery sequence.
2. `EvidenceReplaySession` omitted only `scenario_ids` from the list response
   type, inheriting list-only deletion preview fields absent from replay wire
   payloads. Its wire type now also omits `deletion_allowed`, `deletion_target`,
   and `deletion_error` while preserving optional replay `scenario_ids`.

### TDD RED Evidence

Rejected DELETE overlap regression before production changes:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "preserves a scan refetch when overlapping delete is rejected"
```

```text
FAIL evidence library RTK invalidation > preserves a scan refetch when overlapping delete is rejected
Expected scan GET AbortSignal aborted: false
Received: true
Tests: 1 failed | 34 skipped (35)
```

Replay wire-type regression before production changes:

```bash
cd web
npx tsc --noEmit
```

```text
TS2322: replay payload without deletion preview fields is missing
deletion_allowed and deletion_target from EvidenceReplaySession.
```

### TDD GREEN Evidence

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "preserves a scan refetch when overlapping delete is rejected|aborts a mounted scan refetch before delete|accepts replay session wire payloads"
npx tsc --noEmit
```

```text
Test Files  1 passed (1)
Tests       3 passed | 32 skipped (35)
TypeScript  passed
```

The rejected path asserts exact order `GET-1 A+B -> SCAN 200 -> GET-2 pending
-> DELETE A 404 -> GET-2 B 200`, an un-aborted GET-2, no GET-3, fulfilled query
status, and final cache containing only B. The retained successful path confirms
pending scan GET cancellation still prevents stale resurrection after DELETE 200.

### Final Verification

Focused suite:

```text
EvidenceLibraryView.test.tsx: 35 passed in 6.01s
```

Production build:

```text
tsc: passed
vite: 2396 modules transformed; built in 8.03s
```

Retained warnings: Foxglove serialization uses `eval`; the main output chunk is
1,804.44 kB and exceeds Vite's 500 kB warning threshold.

Scope and whitespace:

```text
git diff --check: passed
Changed paths: exactly the three owned files in this task header.
```

### Remaining Concerns

- No browser, container, SIL, or A4000 gate was required or run for this scoped
  RTK Query lifecycle and TypeScript wire-contract correction.
