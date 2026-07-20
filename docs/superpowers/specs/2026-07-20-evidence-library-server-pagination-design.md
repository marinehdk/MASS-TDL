# Evidence Library Server Pagination Design

**Date:** 2026-07-20

## Problem

Evidence scanning and evidence listing currently use different cardinalities. A scan reports every discovered session, while `GET /api/v1/evidence-library/sessions` defaults to 200 records and caps requests at 500. The frontend requests no explicit limit and performs filtering, sorting, and pagination only on that truncated response. Consequently, 313 indexed sessions appear as 200 records and 10 pages of 20.

The system must report the real eligible evidence count without an overall result threshold. Page size may bound one response; it must never bound the dataset.

## Scope

- M8 Evidence Library list contract and UI only.
- Modify Evidence Library backend query/route, typed frontend API, list state, and tests.
- Preserve scan, replay, overview, delete, batch-delete, and cleanup-recovery behavior.
- No ROS2 topic, message, or IDL change.
- No ODD, COLREGs, M5, M7, planner, guidance, or safety-behavior change.

## Chosen Approach

Use server-owned page, search, filter, and sort semantics. The backend evaluates the complete eligible dataset, returns only the requested page, and reports exact total and filtered counts. Offset/page-number pagination is chosen because the UI requires deterministic page numbers and direct previous/next navigation. Fetch-all and cursor pagination are rejected: fetch-all transfers unbounded payloads; cursors complicate page counts and arbitrary sort/filter navigation.

## API Contract

`GET /api/v1/evidence-library/sessions` accepts:

- `page`: one-based positive integer, default `1`.
- `page_size`: `20` or `50`, default `20`.
- `search`: optional trimmed text.
- `sort_key`: `time`, `result`, `scenarioCount`, `mode`, `scenario`, `source`, or `worktree`; default `time`.
- `sort_direction`: `asc` or `desc`; default `desc`.
- `result`: optional canonical outcome `passed`, `failed`, or `unknown`.
- `scenario_count`: optional non-negative integer.
- `mode`, `scenario`, `source`, `worktree`: optional canonical facet values.

Invalid enumerations, negative values, and unsupported page sizes return HTTP 422. No `limit` parameter and no global 200/500 cap remain.

Response:

```json
{
  "sessions": [],
  "total": 313,
  "filtered_total": 313,
  "page": 1,
  "page_size": 20,
  "total_pages": 16,
  "facets": {
    "result": [{"value": "failed", "label": "不通过", "count": 120}],
    "scenarioCount": [{"value": "1", "label": "1", "count": 280}],
    "mode": [],
    "scenario": [],
    "source": [],
    "worktree": []
  }
}
```

Definitions:

- `total`: all sessions eligible for display before search and filters.
- `filtered_total`: sessions matching search and every active filter.
- `total_pages`: `max(1, ceil(filtered_total / page_size))`; empty results still use page 1.
- `page`: normalized to the last valid page when deletion, scan, or filters make the requested page too large.
- `facets`: stable options computed from the complete eligible dataset, not only the current page. Counts are global eligible counts for each option.

## Eligibility and View Semantics

Eligibility retains the existing safety boundary: a session counts only when its configured session path passes `_session_path_is_healthy`. Deletion target resolution still runs for returned list items. Raw session fields remain available for replay and deletion.

The backend becomes the authority for list semantics used by filtering and sorting:

- outcome: failed when any scenario failed; passed when every known scenario passed and at least one exists; otherwise unknown.
- scenario count: count of indexed scenario rows.
- time: `created_at`, then `ended_at`, then `session_id` fallback.
- mode, source, worktree, and scenario: existing visible list meanings.

Facet options carry a canonical `value` and visible `label`. Frontend sends `value` back to the API. Search matches the same fields currently searchable in the UI, including visible labels, evidence/session IDs, scenario IDs, branch, source, suite/mode, outcome, and worktree.

All active filters combine with AND. Search is an OR match across searchable fields. Filtering occurs before sorting; sorting occurs before pagination. Equal sort values use `evidence_id` as deterministic tie-breaker.

## Frontend Behavior

The frontend query key includes page, page size, debounced search, sort, and filters. It no longer derives global rows from the current response.

- `记录数` renders `total`.
- `显示` renders `filtered_total`.
- Page count renders `total_pages`.
- Page-size change, search change, sort change, or filter change resets requested page to 1.
- Backend-normalized `page` replaces local page after deletion, scan, or count shrink.
- Filter menus use response `facets`; page contents do not restrict filter choices.
- Header selection toggles only selectable rows on the current page.
- Selected snapshots persist while navigating pages. Existing batch-delete and failed-selection retry semantics remain.
- Scan completion, single delete, and batch delete invalidate/refetch the active query and current counts.

Search uses a 250 ms debounce. Stale requests remain isolated by RTK Query argument keys; only the active argument response renders.

## Backend Structure

Replace `list_sessions(limit)` with a query object and paged result. The service:

1. Loads indexed session metadata and scenario/artifact aggregates in bulk.
2. Applies existing filesystem eligibility checks.
3. Derives canonical list fields and complete facets.
4. Applies search and filters.
5. Sorts deterministically.
6. Normalizes page and slices only the requested page.
7. Resolves deletion and overview metadata for returned sessions.

The first implementation may evaluate eligible metadata in memory because filesystem eligibility cannot be expressed safely in SQLite. It must avoid the current per-session scenario/artifact query loop by loading aggregates in bulk. The API contract permits later SQL/caching optimization without frontend change.

## Error Handling

- Invalid query input: HTTP 422 from typed FastAPI parameters.
- Database/config failure: existing API failure path; frontend retains the last rendered page and shows query failure state.
- Requested page beyond range: return normalized final page, not an empty phantom page.
- Session disappears between eligibility check and enrichment: omit it, recompute counts/page once, and return a coherent response.
- Empty dataset/filter result: `sessions=[]`, counts zero, `page=1`, `total_pages=1`.

## Compatibility

This is an intentional internal API contract replacement. The repository frontend and route tests change in the same commit series. No legacy `limit` alias is retained because it would preserve ambiguous truncation semantics. Replay, deletion, scanning, and per-session endpoints are unchanged.

## Testing

Backend RED/GREEN coverage:

- 313 eligible sessions produce `total=313`, `filtered_total=313`, 20 items, and 16 pages.
- Final page contains 13 items.
- Page size 50 produces 7 pages.
- Search and every filter report exact filtered totals.
- Sort direction/key and deterministic tie-break work across page boundaries.
- Facets include values absent from the current page.
- Missing/unsafe paths do not enter totals.
- Out-of-range page normalizes after deletion/count shrink.
- Invalid parameters return 422.

Frontend RED/GREEN coverage:

- Header shows real total and filtered count from API metadata.
- Page navigation changes API arguments rather than slicing local rows.
- Page size, search, sort, and filters reset page and issue correct arguments.
- Facets remain complete on a partial page.
- Scan/delete invalidation refreshes page and counts.
- Cross-page selection remains stable; page checkbox affects current page only.

Regression and runtime verification:

- Complete Evidence Library backend suite.
- Evaluator/replay frontend suite and production build.
- Runtime API returns 313 total with 20 page rows and 16 pages.
- Headless browser renders `记录数: 313`, `显示: 313`, and `1 / 16`.
- Existing M5 containers and ports remain untouched.

## Acceptance

1. No overall list threshold exists in route, service, or frontend.
2. Counts reflect every eligible indexed session.
3. Search, filters, and sorting operate across the entire eligible dataset.
4. Only the requested page is transferred and rendered.
5. Existing scan/replay/delete/cleanup behavior remains green.
6. Backend, frontend, build, runtime, and independent review gates pass.
