# Evidence Library Batch Delete Design

Date: 2026-07-13
Status: Approved design

## Goal

Add explicit multi-selection and safe batch deletion to the Screen 04 simulation
database. Selection must compose with existing search and filters so an operator
can select every currently filtered run, review the exact deletion set, and
delete the corresponding database records and run directories in one action.

Remove automatic scanning. The database changes only when the operator presses
the existing manual `扫描` command or when a delete operation updates the list.

## Scope And Safety Boundaries

Affected module: M8 Web HMI and the Evidence Library backend service.

Affected production surfaces:

- `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- `web/src/api/silApi.ts`
- `src/sil_orchestrator/evidence_library/routes.py`
- `src/sil_orchestrator/evidence_library/service.py`

No ROS2 topic, message, IDL, ODD, COLREGs decision logic, or M5/M7 boundary
changes are allowed.

Each requested evidence ID must pass the existing server-side deletion target
validation. Unified runs delete the matched parent `runs/<run_id>` directory and
their database rows. Unsafe or unresolved targets fail individually and remain
available for operator review.

## Selection Model

Add a fixed-width checkbox column before `序号`.

- Each row checkbox selects that row's `evidence_id`.
- The header checkbox selects or clears all rows on the current page.
- The header checkbox has unchecked, mixed, and checked states.
- Selection persists while paging through the same result set.
- After selecting the current page, show an action bar with the selected count
  and, when more filtered rows exist, `选择全部 N 条筛选结果`.
- Choosing that command snapshots every evidence ID in the current filtered and
  sorted result set. Deletion always uses this fixed snapshot; the server does
  not re-evaluate filters.
- Changing search text, any filter, sort order, page size, or initiating a scan
  clears selection. Ordinary page navigation preserves selection.
- Rows whose deletion target is already marked unsafe cannot be selected.

The action bar is shown only while at least one row is selected. It appears in
the existing top toolbar and contains `已选择 N 条`, optional select-all-filtered
command, `取消选择`, and a red `删除所选（N）` command.

## Batch Delete API

Add:

```text
POST /api/v1/evidence-library/sessions/batch-delete
```

Request body:

```json
{
  "evidence_ids": ["id-1", "id-2"]
}
```

Validation rules:

- `evidence_ids` must contain 1 to 500 unique, non-empty strings.
- Duplicate IDs are rejected with HTTP 422 rather than silently collapsed.
- The endpoint processes each ID independently through the same service used by
  single deletion.
- A missing, unsafe, or failed item does not roll back successful items.
- The route returns HTTP 200 after processing a valid request, including partial
  failure. Request-shape errors use HTTP 422.

Response body:

```json
{
  "requested": 2,
  "deleted": 1,
  "failed": 1,
  "results": [
    {"evidence_id": "id-1", "status": "deleted", "deleted_path": "/runs/id-1"},
    {"evidence_id": "id-2", "status": "failed", "error": "deletion target is unsafe"}
  ]
}
```

Result order matches request order. Error text is suitable for the operator but
must not expose stack traces.

## Confirmation And Results

Before deletion, show one confirmation dialog containing:

- total selected records;
- counts for pass, fail, and unknown outcomes;
- number of affected worktrees;
- explicit warning that database records and matching run directories are
  permanently deleted.

The operator does not type a confirmation phrase. During execution, disable
selection, search, filters, sorting, scanning, pagination, and delete commands.

After success, refresh the Evidence Library query and clear deleted IDs. On
partial failure, the dialog changes to a result view showing deleted and failed
counts plus per-item failure reasons. Failed IDs remain selected after closing
the result view so the operator can retry. A complete success closes the dialog
and clears selection.

Single-row deletion remains available and continues to use its existing dialog.

## Manual Scan Only

Remove the automatic scan interval selector, countdown display, timer state,
interval persistence, and timer-driven scan effects. Preserve the manual `扫描`
button and its current scan result feedback.

## Accessibility And Visual Rules

- Use native checkbox semantics and visible focus states.
- Give row checkboxes labels containing their continuous row number and scenario.
- Give the header checkbox the label `选择当前页`.
- Reuse existing toolbar, cyan accent, red danger, border, and typography tokens.
- Keep the checkbox column narrow and prevent it from resizing other columns.
- Do not add a second toolbar, decorative cards, gradients, or nested panels.

## Verification

Frontend component tests cover:

- header checkbox unchecked, mixed, and checked states;
- current-page select and clear;
- selection persistence across pages;
- selecting the complete filtered result snapshot;
- search, filter, sort, page-size, and scan changes clearing selection;
- unsafe rows remaining unselectable;
- confirmation summary counts;
- complete success and partial-failure retry behavior;
- removal of automatic scan controls and timer-driven requests;
- existing overview, replay, single delete, search, filtering, and pagination.

Backend tests cover:

- complete success;
- mixed success, missing ID, and unsafe target;
- stable request-order results;
- duplicate, empty, oversized, and malformed requests;
- unified parent directory and database row deletion;
- preservation of failed records and paths.

Required commands:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
npm run build

cd ..
PYTHONPATH=src /usr/bin/python3.10 -m pytest -q -o addopts='' \
  src/sil_orchestrator/tests/test_evidence_library_routes.py
```

Browser verification at `http://192.168.121.50:55763/#/evaluator` covers the
filter-fail, select-page, select-all-filtered, confirmation, cancel, and partial
failure presentation flows. Browser testing must not delete existing A4000 run
data; destructive verification uses temporary backend fixtures.
