# Evidence Library Batch Delete Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add filter-aware multi-selection and safe batch deletion to Screen 04 while removing automatic scanning.

**Architecture:** The browser snapshots explicit `evidence_id` values from the current filtered result set and submits them to a new batch endpoint. The backend validates request shape, invokes the existing safe single-session deletion service independently for each ID, and returns ordered per-item results; the frontend patches only confirmed deletions and retains failed selections for retry.

**Tech Stack:** React 18, TypeScript, Redux Toolkit Query, Vitest/Testing Library, FastAPI, Python 3.10, pytest/httpx, SQLite.

## Global Constraints

- Selection uses explicit evidence-ID snapshots; the server never re-evaluates UI filters.
- Only rows with `deletion_allowed` and `deletion_target` may be selected.
- Batch size is 1 to 500 unique non-empty evidence IDs; invalid requests return HTTP 422.
- Valid batch requests return HTTP 200 with ordered per-item success or failure results.
- Unified deletion continues to remove the validated parent `runs/<run_id>` directory and all evidence rows.
- Search, filter, sort, page-size, and manual scan changes clear selection; page navigation preserves it.
- Automatic scan interval, countdown, persistence, and timer effects are removed.
- No ROS2 topic, message, IDL, ODD, COLREGs, M5, or M7 change.
- Browser verification must not delete existing A4000 evidence.

## File Map

- `src/sil_orchestrator/evidence_library/service.py`: ordered, per-item batch orchestration over existing safe deletion.
- `src/sil_orchestrator/evidence_library/routes.py`: request validation and `POST /sessions/batch-delete` route.
- `src/sil_orchestrator/tests/test_evidence_library_routes.py`: backend contract, safety, and filesystem/database tests.
- `web/src/api/silApi.ts`: batch request/result types, mutation, and race-safe Evidence Library cache update.
- `web/src/screens/evaluator/EvidenceLibraryView.tsx`: manual scan only, selection model, toolbar, checkboxes, confirmation and result UI.
- `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`: component and RTK Query lifecycle coverage.
- `web/src/screens/__tests__/SimulationEvaluator.test.tsx`: mocked hook export required by the screen-level test harness.

---

### Task 1: Backend Batch Deletion Contract

**Files:**
- Modify: `src/sil_orchestrator/evidence_library/service.py:465-498`
- Modify: `src/sil_orchestrator/evidence_library/routes.py:5-52`
- Test: `src/sil_orchestrator/tests/test_evidence_library_routes.py:591-920`

**Interfaces:**
- Consumes: `delete_evidence_session(evidence_id: str, repo_root: Path | None = None) -> dict[str, Any]`.
- Produces: `delete_evidence_sessions(evidence_ids: list[str], repo_root: Path | None = None) -> dict[str, Any]` and `POST /api/v1/evidence-library/sessions/batch-delete`.

- [ ] **Step 1: Add failing route tests for ordered complete and partial results**

Add tests that create two unified runs, rescan, and post their IDs in reverse list order. Assert HTTP 200, preserved response order, both parent directories removed, and all indexed rows removed. Add a second test containing one valid ID, `missing-evidence`, and an unsafe-root ID; assert `requested == 3`, `deleted == 1`, `failed == 2`, and failed paths/rows remain.

```python
response = await client.post(
    "/api/v1/evidence-library/sessions/batch-delete",
    json={"evidence_ids": requested_ids},
)
assert response.status_code == 200
payload = response.json()
assert [item["evidence_id"] for item in payload["results"]] == requested_ids
assert payload["requested"] == len(requested_ids)
assert payload["deleted"] + payload["failed"] == payload["requested"]
```

- [ ] **Step 2: Add failing request-validation tests**

Parameterize empty, duplicate, blank, non-string, and 501-item lists. Assert HTTP 422 and no session directory or database row changes.

```python
@pytest.mark.parametrize("evidence_ids", [[], ["same", "same"], [""], [123], [f"id-{i}" for i in range(501)]])
async def test_batch_delete_rejects_invalid_evidence_ids(...):
    response = await client.post(
        "/api/v1/evidence-library/sessions/batch-delete",
        json={"evidence_ids": evidence_ids},
    )
    assert response.status_code == 422
```

- [ ] **Step 3: Run backend tests and verify RED**

Run:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -k batch_delete
```

Expected: failures because the batch route does not exist.

- [ ] **Step 4: Implement ordered per-item service orchestration**

Add beside `delete_evidence_session`:

```python
def delete_evidence_sessions(evidence_ids: list[str], repo_root: Path | None = None) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    for evidence_id in evidence_ids:
        try:
            deleted = delete_evidence_session(evidence_id, repo_root=repo_root)
        except (LookupError, PermissionError, OSError) as exc:
            results.append({"evidence_id": evidence_id, "status": "failed", "error": str(exc)})
        else:
            results.append({**deleted, "status": "deleted"})
    deleted_count = sum(item["status"] == "deleted" for item in results)
    return {
        "requested": len(evidence_ids),
        "deleted": deleted_count,
        "failed": len(results) - deleted_count,
        "results": results,
    }
```

- [ ] **Step 5: Implement route validation and endpoint**

Import `delete_evidence_sessions`. Validate exact list constraints before service invocation:

```python
def _batch_evidence_ids(request: dict) -> list[str]:
    evidence_ids = request.get("evidence_ids")
    valid = (
        isinstance(evidence_ids, list)
        and 1 <= len(evidence_ids) <= 500
        and all(isinstance(item, str) and item.strip() == item and item for item in evidence_ids)
        and len(set(evidence_ids)) == len(evidence_ids)
    )
    if not valid:
        raise HTTPException(status_code=422, detail="evidence_ids must contain 1 to 500 unique non-empty strings")
    return evidence_ids


@router.post("/sessions/batch-delete")
async def batch_delete_sessions(request: dict):
    return delete_evidence_sessions(_batch_evidence_ids(request), repo_root=REPO_ROOT)
```

Declare this fixed route before `@router.delete("/sessions/{evidence_id}")` for route readability.

- [ ] **Step 6: Run focused and full Evidence Library backend tests**

Run:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py -k batch_delete
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
```

Expected: all tests pass.

- [ ] **Step 7: Commit backend contract**

```bash
git add src/sil_orchestrator/evidence_library/service.py \
  src/sil_orchestrator/evidence_library/routes.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
git commit -m "feat(evidence): add safe batch deletion"
```

---

### Task 2: Frontend Batch Mutation And Cache Lifecycle

**Files:**
- Modify: `web/src/api/silApi.ts:309-313,493-533,725-728`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx:760-1100`
- Test: `web/src/screens/__tests__/SimulationEvaluator.test.tsx:7-20`

**Interfaces:**
- Consumes: `POST /evidence-library/sessions/batch-delete` from Task 1.
- Produces: `EvidenceLibraryBatchDeleteRequest`, `EvidenceLibraryBatchDeleteResult`, and `useBatchDeleteEvidenceLibrarySessionsMutation()`.

- [ ] **Step 1: Add failing RTK Query mutation tests**

Extend the existing store/MSW lifecycle tests. Assert the mutation posts the exact ID array, removes only `status: "deleted"` IDs from cached sessions, retains failed IDs, and preserves cache on rejected requests.

```typescript
const result = await store.dispatch(
  silApi.endpoints.batchDeleteEvidenceLibrarySessions.initiate({ evidence_ids: ['ok-id', 'failed-id'] }),
).unwrap();
expect(result.deleted).toBe(1);
expect(selectSessionIds(store.getState())).not.toContain('ok-id');
expect(selectSessionIds(store.getState())).toContain('failed-id');
```

Include the existing pending-GET race pattern so a stale list response cannot reintroduce successfully deleted IDs.

- [ ] **Step 2: Run mutation tests and verify RED**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "batch delete mutation"
```

Expected: failure because the endpoint and hook are undefined.

- [ ] **Step 3: Add exact wire types**

```typescript
export interface EvidenceLibraryBatchDeleteRequest {
  evidence_ids: string[];
}

export type EvidenceLibraryBatchDeleteItem =
  | (EvidenceLibraryDeleteResult & { status: 'deleted' })
  | { evidence_id: string; status: 'failed'; error: string };

export interface EvidenceLibraryBatchDeleteResult {
  requested: number;
  deleted: number;
  failed: number;
  results: EvidenceLibraryBatchDeleteItem[];
}
```

- [ ] **Step 4: Add mutation with successful-ID cache patching**

Add `batchDeleteEvidenceLibrarySessions` beside the single mutation. Post the request body. After `queryFulfilled`, derive `deletedIds` only from successful results, abort an in-flight list query using the established single-delete sequence, patch those IDs from cache, invalidate `EvidenceLibrary`, await refresh, and preserve the patched data if refresh fails. Rejected mutations return without cache changes.

```typescript
const { data: result } = await queryFulfilled;
const deletedIds = new Set(
  result.results.filter((item) => item.status === 'deleted').map((item) => item.evidence_id),
);
dispatch(silApi.util.updateQueryData('getEvidenceLibrarySessions', undefined, (draft) => {
  draft.sessions = draft.sessions.filter((session) => !deletedIds.has(session.evidence_id));
}));
```

Export `useBatchDeleteEvidenceLibrarySessionsMutation` and add a mock implementation to `SimulationEvaluator.test.tsx`.

- [ ] **Step 5: Run RTK Query lifecycle and screen harness tests**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/__tests__/SimulationEvaluator.test.tsx
```

Expected: all tests pass.

- [ ] **Step 6: Commit frontend API contract**

```bash
git add web/src/api/silApi.ts \
  web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  web/src/screens/__tests__/SimulationEvaluator.test.tsx
git commit -m "feat(evaluator): add batch delete client"
```

---

### Task 3: Manual Scan And Filter-Aware Selection

**Files:**
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx:26-28,230-282,349-434,459-468,630-741,799-930,943-959`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx:132-585`

**Interfaces:**
- Consumes: existing `sortedRows`, `visibleRows`, `safePage`, `pageSize`, and server-derived `deletion_allowed`/`deletion_target`.
- Produces: `selectedEvidenceIds: Set<string>`, current-page three-state checkbox, and select-all-filtered snapshot command.

- [ ] **Step 1: Replace automatic-scan tests with a failing manual-only test**

Delete fake-timer expectations for interval changes and countdown. Assert no controls named `自动刷新间隔` or `距离下次扫描`, advance fake timers, and verify `rescan` is not called. Preserve a manual scan assertion.

```typescript
expect(screen.queryByRole('combobox', { name: '自动刷新间隔' })).not.toBeInTheDocument();
expect(screen.queryByLabelText('距离下次扫描')).not.toBeInTheDocument();
act(() => vi.advanceTimersByTime(86_400_000));
expect(apiMocks.rescan).not.toHaveBeenCalled();
fireEvent.click(screen.getByRole('button', { name: '扫描' }));
await waitFor(() => expect(apiMocks.rescan).toHaveBeenCalledTimes(1));
```

- [ ] **Step 2: Add failing selection behavior tests**

Create at least 25 deletable sessions plus one unsafe session. Test:

- checkbox column precedes `序号`;
- header checkbox selects 20 current-page rows and enters checked state;
- clearing one row makes the header mixed;
- page 2 selection adds to the same selection;
- `选择全部 25 条筛选结果` snapshots all deletable filtered IDs;
- unsafe row checkbox is disabled;
- page navigation preserves selection;
- search, result filter, sort, page-size, and scan clear selection.

Use accessible queries:

```typescript
const selectPage = screen.getByRole('checkbox', { name: '选择当前页' });
fireEvent.click(selectPage);
expect(screen.getByText('已选择 20 条')).toBeInTheDocument();
fireEvent.click(screen.getByRole('button', { name: '选择全部 25 条筛选结果' }));
expect(screen.getByText('已选择 25 条')).toBeInTheDocument();
```

- [ ] **Step 3: Run component tests and verify RED**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "selection|manual scan"
```

Expected: missing checkboxes/action bar and existing automatic controls cause failures.

- [ ] **Step 4: Remove automatic refresh implementation**

Remove `RefreshIntervalSeconds`, `formatCountdown`, `autoRefreshSeconds`, `countdownSeconds`, `automaticScanAttemptedRef`, both automatic-scan effects, and selector/countdown JSX. Reduce `handleRescan` dependencies and clear selection before invoking `rescan`.

```typescript
const handleRescan = useCallback(async () => {
  setSelectedEvidenceIds(new Set());
  try {
    const result = await rescan({ force: false }).unwrap();
    setScanFailed(false);
    setScanResult(result);
  } catch {
    setScanFailed(true);
  }
}, [rescan]);
```

- [ ] **Step 5: Implement explicit selection state and reset points**

Add:

```typescript
const [selectedEvidenceIds, setSelectedEvidenceIds] = useState<Set<string>>(() => new Set());
const selectableRows = sortedRows.filter(
  (row) => row.raw.deletion_allowed && row.raw.deletion_target,
);
const visibleSelectableIds = visibleRows
  .filter((row) => row.raw.deletion_allowed && row.raw.deletion_target)
  .map((row) => row.raw.evidence_id);
const selectedSessions = sessions.filter((session) => selectedEvidenceIds.has(session.evidence_id));
const selectedOnPage = visibleSelectableIds.filter((id) => selectedEvidenceIds.has(id)).length;
```

Implement immutable toggle helpers. Set the native header checkbox `indeterminate` through a ref/effect. Clear selection inside search, filter, sort, page-size, and scan handlers; do not clear it in previous/next page handlers.

- [ ] **Step 6: Add checkbox column and compact action bar**

Place a 42 px checkbox header before `序号` and row checkbox cells before row numbers. Labels include continuous row number and scenario. Insert the selected action group into the existing top toolbar without creating another toolbar.

```tsx
<input ref={selectPageRef} type="checkbox" aria-label="选择当前页" ... />
<input
  type="checkbox"
  aria-label={`选择第 ${safePage * pageSize + index + 1} 条 ${row.scenario}`}
  disabled={!row.raw.deletion_allowed || !row.raw.deletion_target || batchDeleteState.isLoading}
  checked={selectedEvidenceIds.has(row.raw.evidence_id)}
  onChange={() => toggleSelected(row.raw.evidence_id)}
/>
```

The select-all-filtered command snapshots `selectableRows.map(row => row.raw.evidence_id)`.

- [ ] **Step 7: Run focused component tests**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "selection|manual scan"
```

Expected: all focused tests pass.

- [ ] **Step 8: Commit selection and manual scan UI**

```bash
git add web/src/screens/evaluator/EvidenceLibraryView.tsx \
  web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
git commit -m "feat(evaluator): add filter-aware row selection"
```

---

### Task 4: Batch Confirmation, Partial Failure, And Final Verification

**Files:**
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx:230-337,614-741,963-1068`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx:585-760`
- Modify: `handoff/workspace_log.md`

**Interfaces:**
- Consumes: `selectedSessions` from Task 3 and `useBatchDeleteEvidenceLibrarySessionsMutation()` from Task 2.
- Produces: batch confirmation summary, disabled busy state, partial-failure result view, and retry-preserving failed selection.

- [ ] **Step 1: Add failing confirmation and result-state tests**

Select a mix of pass, fail, and unknown sessions from two worktrees. Assert the dialog shows total, outcome counts, worktree count, and permanent database/filesystem warning. Mock complete success and partial failure separately.

```typescript
expect(dialog).toHaveTextContent('共 4 条');
expect(dialog).toHaveTextContent('通过 1');
expect(dialog).toHaveTextContent('不通过 2');
expect(dialog).toHaveTextContent('未知 1');
expect(dialog).toHaveTextContent('工作树 2');
```

For partial failure, assert successful rows disappear, failed row remains selected, result details are visible, and `重试失败项（1）` submits only the failed ID. Assert all table/search/filter/scan/pagination controls are disabled while the promise is pending.

- [ ] **Step 2: Run dialog tests and verify RED**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx -t "batch confirmation|partial batch"
```

Expected: batch dialog and mutation integration are absent.

- [ ] **Step 3: Implement batch dialog state and summaries**

Add separate state so single deletion remains unchanged:

```typescript
const [pendingBatchDelete, setPendingBatchDelete] = useState<EvidenceLibrarySession[] | null>(null);
const [batchDeleteResult, setBatchDeleteResult] = useState<EvidenceLibraryBatchDeleteResult | null>(null);
const [batchDeleteSessions, batchDeleteState] = useBatchDeleteEvidenceLibrarySessionsMutation();
```

Compute pass/fail/unknown from `displayResult(session)` and distinct worktrees
from non-empty `session.worktree_name` values; Front records with no worktree do
not create a synthetic worktree count. Open the dialog from `删除所选（N）` with a
snapshot of `selectedSessions`.

- [ ] **Step 4: Implement complete and partial result handling**

```typescript
const handleBatchDelete = async () => {
  if (!pendingBatchDelete) return;
  const result = await batchDeleteSessions({
    evidence_ids: pendingBatchDelete.map((session) => session.evidence_id),
  }).unwrap();
  const failedIds = new Set(
    result.results.filter((item) => item.status === 'failed').map((item) => item.evidence_id),
  );
  setSelectedEvidenceIds(failedIds);
  setBatchDeleteResult(result);
  if (result.failed === 0) {
    setPendingBatchDelete(null);
    setBatchDeleteResult(null);
  } else {
    setPendingBatchDelete((current) => current?.filter((session) => failedIds.has(session.evidence_id)) ?? null);
  }
};
```

Render one modal with confirmation and partial-result modes. Reuse the existing focus trap/inert approach, use red danger styling, and list sanitized per-item failure messages. Disable all mutating/navigation controls while either delete mutation is loading.

- [ ] **Step 5: Run complete frontend suite and build**

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx \
  src/screens/__tests__/SimulationEvaluator.test.tsx
npm run build
```

Expected: all tests pass; build completes with only previously documented Foxglove `eval` and large-chunk warnings.

- [ ] **Step 6: Run complete backend Evidence Library suite**

Run:

```bash
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_config_store.py \
  src/sil_orchestrator/tests/test_evidence_library_ingest.py \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
```

Expected: all tests pass.

- [ ] **Step 7: Perform non-destructive browser verification**

At `http://192.168.121.50:55763/#/evaluator` verify:

1. Automatic interval and countdown are absent.
2. Filter result to `不通过`.
3. Select current page, then select all filtered results.
4. Page forward/back and confirm selection count persists.
5. Open batch delete confirmation and verify summary counts.
6. Cancel without issuing deletion.
7. Confirm no clipped controls, overlapping menus, or table-width regression.

Save screenshots and a JSON checklist under:

```text
runs/e2e/evidence_library_batch_delete_<timestamp>/
```

- [ ] **Step 8: Update handoff and commit completed feature**

Append branch, commits, changed paths, test commands/results, browser evidence path, and remaining local/A4000 promotion gates to `handoff/workspace_log.md`.

```bash
git add web/src/screens/evaluator/EvidenceLibraryView.tsx \
  web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  handoff/workspace_log.md
git commit -m "feat(evaluator): complete batch evidence deletion"
git diff --check HEAD~4..HEAD
git status --short
```

Expected: `git diff --check` emits no output and the worktree is clean.
