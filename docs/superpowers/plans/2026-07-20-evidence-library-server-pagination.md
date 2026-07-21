# Evidence Library Server Pagination Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace truncated client-side Evidence Library listing with exact server-side totals, filtering, sorting, search, and page retrieval.

**Architecture:** Backend builds one eligible-session view from indexed metadata plus filesystem safety checks, derives facets and exact counts, then filters, sorts, and slices one page. Frontend treats API metadata as authoritative and sends every list-state change as query arguments. Existing scan, replay, delete, selection, and cleanup flows remain unchanged.

**Tech Stack:** Python 3.10+, FastAPI, SQLite, React 18, TypeScript, RTK Query, Vitest.

## Global Constraints

- M8 Evidence Library list contract and UI only.
- No overall result cap; `page_size` bounds one response only.
- Supported page sizes: exactly `20` and `50`.
- Preserve filesystem eligibility, deletion-target safety, scan, replay, overview, delete, batch-delete, and cleanup recovery.
- No ROS2/IDL, ODD, COLREGs, M5, M7, planner, guidance, or safety behavior change.
- TDD mandatory: every production change follows witnessed RED.
- Do not restart/reconfigure `codex-gnc-validation` or `m5-design-grounding` containers.

---

### Task 1: Exact backend query contract

**Files:**

- Modify: `src/sil_orchestrator/evidence_library/service.py:1286-1338`
- Modify: `src/sil_orchestrator/evidence_library/routes.py:1-52`
- Create: `src/sil_orchestrator/tests/test_evidence_library_pagination.py`
- Modify: `src/sil_orchestrator/tests/test_evidence_library_ingest.py:221`
- Modify: `src/sil_orchestrator/tests/test_evidence_library_routes.py:220-280`
- Modify: `src/sil_orchestrator/tests/test_evidence_routes.py:203`

**Interfaces:**

- Produces `EvidenceSessionListQuery`.
- Produces `list_sessions(query, repo_root) -> {sessions,total,filtered_total,page,page_size,total_pages,facets}`.
- Preserves existing returned session, overview, deletion, replay fields.

- [ ] **Step 1: Write failing exact-count/page tests**

Create 313 healthy temporary indexed sessions with deterministic timestamps, outcomes, suites, sources, scenarios, and worktrees. Use real `list_sessions`.

```python
def test_lists_exact_total_without_global_cap(paged_library):
    result = list_sessions(EvidenceSessionListQuery(page=1, page_size=20), repo_root=paged_library.repo)
    assert result["total"] == 313
    assert result["filtered_total"] == 313
    assert result["total_pages"] == 16
    assert len(result["sessions"]) == 20

def test_lists_last_page_and_fifty_row_pages(paged_library):
    assert len(list_sessions(EvidenceSessionListQuery(page=16), repo_root=paged_library.repo)["sessions"]) == 13
    fifty = list_sessions(EvidenceSessionListQuery(page=7, page_size=50), repo_root=paged_library.repo)
    assert fifty["total_pages"] == 7
    assert len(fifty["sessions"]) == 13

def test_excludes_unhealthy_paths_and_normalizes_page(paged_library):
    paged_library.remove_session_path(0)
    result = list_sessions(EvidenceSessionListQuery(page=99, page_size=50), repo_root=paged_library.repo)
    assert result["total"] == 312
    assert result["page"] == 7
```

- [ ] **Step 2: Verify backend RED**

Run: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 /tmp/mass-l3-evidence-venv/bin/python -m pytest -o addopts= -p pytest_asyncio.plugin src/sil_orchestrator/tests/test_evidence_library_pagination.py -q`

Expected: import failure for missing `EvidenceSessionListQuery` or missing paged metadata.

- [ ] **Step 3: Implement query and bulk eligible-session view**

Add:

```python
@dataclass(frozen=True)
class EvidenceSessionListQuery:
    page: int = 1
    page_size: int = 20
    search: str = ""
    sort_key: str = "time"
    sort_direction: str = "desc"
    result: str | None = None
    scenario_count: int | None = None
    mode: str | None = None
    scenario: str | None = None
    source: str | None = None
    worktree: str | None = None
```

Load sessions, scenarios, and available overview artifacts in three bulk queries. Group child rows by `evidence_id`. Exclude paths failing `_session_path_is_healthy`. Derive `_outcome`, `_time`, `_mode`, `_scenario`, `_source`, `_worktree`, and a lower-case search blob. Port existing visible labels exactly: debug/cohort/full/avoidance, CLI/Front, passed/failed/unknown. Recheck page paths before enrichment; if one disappeared, rebuild eligibility/count/page once so returned rows and counts remain coherent.

Build facets from the complete eligible set with `Counter`, returning sorted `{value,label,count}` options. Apply search OR across searchable fields; combine filters with AND. Sort only through allow-listed key functions with `evidence_id` tie-break. Normalize page to `1..total_pages`; slice; resolve deletion metadata for page rows; remove private fields.

Return exactly:

```python
return {
    "sessions": page_records,
    "total": len(eligible),
    "filtered_total": len(filtered_records),
    "page": normalized_page,
    "page_size": query.page_size,
    "total_pages": max(1, ceil(len(filtered_records) / query.page_size)),
    "facets": facets,
}
```

- [ ] **Step 4: Verify service GREEN and add filter/sort/facet tests**

Add tests for each filter, localized-label search, deterministic asc/desc sorting across pages, facets containing values absent from page 1, and empty results. Run Step 2 command; expected all pass.

- [ ] **Step 5: Write failing typed-route tests**

```python
@pytest.mark.asyncio
async def test_sessions_route_forwards_complete_query(monkeypatch):
    captured = {}
    def fake_list_sessions(query, **_):
        captured["query"] = query
        return empty_page()
    monkeypatch.setattr(routes, "list_sessions", fake_list_sessions)
    response = await client.get("/api/v1/evidence-library/sessions?page=2&page_size=50&search=rule15&sort_key=scenarioCount&sort_direction=asc&result=failed&scenario_count=1&mode=avoidance&source=cli&worktree=tree-b")
    assert response.status_code == 200
    assert captured["query"].page == 2
    assert captured["query"].page_size == 50

@pytest.mark.asyncio
@pytest.mark.parametrize("query", ["page=0", "page_size=200", "sort_key=bad", "sort_direction=bad", "result=bad", "scenario_count=-1", "limit=500"])
async def test_sessions_route_rejects_invalid_and_legacy_query(query):
    response = await client.get(f"/api/v1/evidence-library/sessions?{query}")
    assert response.status_code == 422
```

- [ ] **Step 6: Verify route RED**

Run: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 /tmp/mass-l3-evidence-venv/bin/python -m pytest -o addopts= -p pytest_asyncio.plugin src/sil_orchestrator/tests/test_evidence_library_routes.py src/sil_orchestrator/tests/test_evidence_routes.py -q`

Expected: forwarding/validation failures because route still exposes `limit`.

- [ ] **Step 7: Implement typed FastAPI route**

Use `Annotated[..., Query(...)]` plus `Literal` for page, page size, sort, result, and non-negative scenario count. Keep a hidden `limit` argument only to reject it with HTTP 422. Construct `EvidenceSessionListQuery`, call `list_sessions`, return its complete dictionary. Update direct test/service consumers from list iteration to `["sessions"]`; update monkeypatches to paged response.

- [ ] **Step 8: Run backend regression gate**

Run: `PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 /tmp/mass-l3-evidence-venv/bin/python -m pytest -o addopts= -p pytest_asyncio.plugin src/sil_orchestrator/tests/test_evidence_routes.py src/sil_orchestrator/tests/test_evidence_library_config_store.py src/sil_orchestrator/tests/test_evidence_library_ingest.py src/sil_orchestrator/tests/test_evidence_library_pagination.py src/sil_orchestrator/tests/test_evidence_library_routes.py -q`

Expected: all pass.

- [ ] **Step 9: Commit backend**

Run: `git add src/sil_orchestrator/evidence_library/service.py src/sil_orchestrator/evidence_library/routes.py src/sil_orchestrator/tests/test_evidence_library_pagination.py src/sil_orchestrator/tests/test_evidence_library_ingest.py src/sil_orchestrator/tests/test_evidence_library_routes.py src/sil_orchestrator/tests/test_evidence_routes.py && git commit -m "feat(evidence): paginate complete session index"`

---

### Task 2: Server-driven Evidence Library UI

**Files:**

- Modify: `web/src/api/silApi.ts:285-316,506-509`
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx:30-46,336-463,681-724,900-1010`
- Modify: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

**Interfaces:**

- Consumes Task 1 paged response.
- Produces `EvidenceLibrarySessionsQuery` RTK Query argument.
- Preserves cache tag `EvidenceLibrary` and scan/delete mutations.

- [ ] **Step 1: Add paged TypeScript types**

```typescript
export type EvidenceLibrarySortKey = 'time' | 'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree';
export type EvidenceLibraryOutcome = 'passed' | 'failed' | 'unknown';
export interface EvidenceLibraryFacetOption { value: string; label: string; count: number; }
export interface EvidenceLibrarySessionsQuery {
  page: number; page_size: 20 | 50; search?: string;
  sort_key: EvidenceLibrarySortKey; sort_direction: 'asc' | 'desc';
  result?: EvidenceLibraryOutcome; scenario_count?: number;
  mode?: string; scenario?: string; source?: string; worktree?: string;
}
export interface EvidenceLibrarySessionsResponse {
  sessions: EvidenceLibrarySession[]; total: number; filtered_total: number;
  page: number; page_size: 20 | 50; total_pages: number;
  facets: Record<'result' | 'scenarioCount' | 'mode' | 'scenario' | 'source' | 'worktree', EvidenceLibraryFacetOption[]>;
}
```

Change endpoint to `builder.query<EvidenceLibrarySessionsResponse, EvidenceLibrarySessionsQuery>({ query: (params) => ({ url: '/evidence-library/sessions', params }), providesTags: ['EvidenceLibrary'] })`.

- [ ] **Step 2: Write failing UI tests**

Add tests asserting: metadata `313/313/16` renders while only 20 session rows exist; next page changes hook argument to page 2; page size/search/sort/filter reset page 1 and send correct arguments; search waits 250 ms; facets absent from current page still render; backend-normalized page replaces stale page; current-page checkbox preserves prior-page selections.

Core assertions:

```typescript
expect(screen.getByText('记录数: 313')).toBeInTheDocument();
expect(screen.getByText('显示: 313')).toBeInTheDocument();
expect(screen.getByText('1 / 16')).toBeInTheDocument();
expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 1, page_size: 20 });
fireEvent.click(screen.getByRole('button', { name: '下一页' }));
expect(apiMocks.lastSessionsQuery).toMatchObject({ page: 2 });
```

- [ ] **Step 3: Verify frontend RED**

Run: `npm --prefix web test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

Expected: metadata and query-argument assertions fail because current UI slices local rows.

- [ ] **Step 4: Implement server-driven state**

Use one-based page, page size, search input, 250 ms debounced search, sort, and canonical filters. Build memoized `EvidenceLibrarySessionsQuery`; call `useGetEvidenceLibrarySessionsQuery(query)`. Remove client global filtering/sorting/slicing and `uniqueValues(rows, key)`. Render current response sessions, API totals/pages, and API facets. Reset page to 1 after page-size/search/sort/filter changes. Synchronize `data.page` after count shrink. Preserve current rows when `isFetching`; show `Loading evidence` only for `isLoading && !data`. Page checkbox operates on current rows; selected snapshots persist across page navigation.

Required state skeleton:

```typescript
const [page, setPage] = useState(1);
const [pageSize, setPageSize] = useState<20 | 50>(20);
const [debouncedSearch, setDebouncedSearch] = useState('');
const sessionsQuery = useMemo<EvidenceLibrarySessionsQuery>(() => ({
  page, page_size: pageSize, search: debouncedSearch || undefined,
  sort_key: sort.key, sort_direction: sort.direction,
  result: filters.result as EvidenceLibraryOutcome | undefined,
  scenario_count: filters.scenarioCount ? Number(filters.scenarioCount) : undefined,
  mode: filters.mode, scenario: filters.scenario, source: filters.source, worktree: filters.worktree,
}), [debouncedSearch, filters, page, pageSize, sort]);
const { data, isLoading, isFetching, refetch: refetchSessions } = useGetEvidenceLibrarySessionsQuery(sessionsQuery);
```

- [ ] **Step 5: Verify frontend GREEN**

Run: `npm --prefix web test -- --run src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx src/screens/evaluator/__tests__/ReplayDetailView.test.tsx`

Expected: all pass.

- [ ] **Step 6: Run production build**

Run: `npm --prefix web run build`

Expected: exit 0; existing Foxglove eval/chunk warnings allowed.

- [ ] **Step 7: Commit frontend**

Run: `git add web/src/api/silApi.ts web/src/screens/evaluator/EvidenceLibraryView.tsx web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx && git commit -m "feat(evidence): drive library pages from server"`

---

### Task 3: Review, runtime evidence, handoff

**Files:**

- Modify: `handoff/workspace_log.md`

- [ ] **Step 1: Run complete regression gate**

Run backend command from Task 1 Step 8, frontend command from Task 2 Step 5, `npm --prefix web run build`, and `git diff --check`. Expected: zero failures/errors.

- [ ] **Step 2: Independent read-only review**

Review exact totals, missing-path eligibility, typed validation, facets, deterministic cross-page sort, stale RTK requests, normalized pages, selection, scan/delete preservation. Resolve blockers through unique write owner; rerun; re-review.

- [ ] **Step 3: Rebuild isolated display only**

Recreate only `l3-tdl-sil-orchestrator-1` on 18001 from this worktree and PM2 `l3-tdl-frontend` on 5174. Do not touch 18000/18765/3000 or any `codex-gnc-validation-*` container.

- [ ] **Step 4: Verify live API/UI**

Assert API page 1 reports `total=313`, `filtered_total=313`, `page_size=20`, `total_pages=16`, 20 sessions; page 16 has 13. Verify one search/filter count independently. Headless browser must show `记录数: 313`, `显示: 313`, `1 / 16`, navigate page 2, and reset page after filter.

- [ ] **Step 5: Verify M5 isolation**

Compare `RestartCount`, `StartedAt`, and listeners before/after. Expected: unchanged, restart 0, ports 18000/18765/3000 intact.

- [ ] **Step 6: Append handoff and commit**

Append date, goal, commits, exact test counts, runtime counts/pages, source worktree, M5 isolation evidence, remaining risks. Run: `git add handoff/workspace_log.md && git commit -m "docs: record evidence pagination handoff"`
