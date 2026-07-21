# Evidence Library Background Rescan Implementation Plan

> **Execution:** One task-scoped M8 write owner. Backend and frontend stay one coupled task because the API state machine is one contract. Independent read-only review follows implementation.

**Goal:** Make Evidence Library scanning non-blocking, incremental, force-aware, and observable while preserving current library availability.

**Architecture:** Process-local single-flight scan manager runs existing scan service in one worker thread. Per-session mtime comparison prevents unchanged database rewrites. Frontend starts the job, polls status, preserves loaded sessions, then refreshes once.

**Tech stack:** Python 3.10+, FastAPI, SQLite, React, TypeScript, Vitest.

---

### Task 1: Background scan contract, incremental ingest, and progress UI

**Files:**

- Modify: `src/sil_orchestrator/evidence_library/ingest.py`
- Modify: `src/sil_orchestrator/evidence_library/service.py`
- Modify: `src/sil_orchestrator/evidence_library/routes.py`
- Modify: `src/sil_orchestrator/tests/test_evidence_library_ingest.py`
- Modify: `src/sil_orchestrator/tests/test_evidence_library_routes.py`
- Modify: `web/src/api/silApi.ts`
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- Modify: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

**Step 1: Write failing backend tests**

Add tests proving unchanged sessions skip destructive writes, force bypasses the skip, start returns before a blocked worker finishes, two starts share one job, status exposes progress/terminal failure, and unrelated routes remain responsive.

Run:

```bash
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 /tmp/mass-l3-evidence-venv/bin/python -m pytest -o addopts= -p pytest_asyncio.plugin \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -q
```

Expected: new tests fail for missing skip semantics and status API.

**Step 2: Implement minimal backend**

Add `IngestResult.skipped`, mtime/ingest-status early return, progress callback/counting in service, single-worker scan manager, immediate HTTP 202 start, and status route. Keep deletion coordination and existing result fields compatible.

Run the Step 1 command. Expected: pass.

**Step 3: Write failing frontend tests**

Add tests proving the start response is polled, loaded rows stay rendered during active scan, progress appears, terminal completion refetches sessions once, and failure stops polling with a visible error.

Run:

```bash
npm --prefix web test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

Expected: new tests fail because current UI awaits the original synchronous response.

**Step 4: Implement minimal frontend**

Add typed job snapshot API contract, scan polling lifecycle, preserved list rendering, progress button label, terminal refetch, error handling, and cleanup on unmount.

Run the Step 3 command. Expected: pass.

**Step 5: Full targeted verification**

Run:

```bash
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 /tmp/mass-l3-evidence-venv/bin/python -m pytest -o addopts= -p pytest_asyncio.plugin \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -q
npm --prefix web test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
npm --prefix web run build
```

Expected: all pass.

**Step 6: Independent review and isolated runtime verification**

Provide spec, plan, diff, RED/GREEN evidence, and impact statement to read-only reviewer. Resolve blocking findings. Rebuild only the task-owned/display Evidence Library backend and 5174 frontend from this worktree. Verify start latency, status progress, health/list latency during scan, terminal state, and unchanged-session skips. Record before/after container port and restart evidence for `m5-design-grounding`.

**Step 7: Commit and handoff**

Append `handoff/workspace_log.md`, verify clean scope, then commit on `codex/evidence-rescan-background`. Do not push or merge without a separate request.
