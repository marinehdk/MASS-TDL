# Evidence Library Table Controls Design

Date: 2026-07-13
Status: Approved design

## Goal

Improve the Screen 04 simulation database table without changing evidence
ingestion, replay, overview, deletion, search, or pagination behavior. The table
must remain compact, scan-friendly, and visually consistent with the existing
MASS control surface.

## Scope

Affected module: M8 Web HMI, Screen 04 Evidence Library.

Affected production surface:

- `web/src/screens/evaluator/EvidenceLibraryView.tsx`

No ROS2 topic, message, IDL, ODD, COLREGs, M5/M7, SIL scenario, backend API, or
database change is required.

## Table Columns

1. Replace `会话ID` with `序号`.
   - Number rows after search, filters, and sorting.
   - Continue numbering across pages: page 2 starts at 21 when page size is 20.
   - Do not display the evidence UUID in the table.
2. Keep `仿真时间` and display whole seconds only.
   - Format: `YYYY-MM-DD HH:mm:ss`.
   - Remove fractional seconds while preserving existing timezone handling.
3. Replace `套件` with `场景数量`.
   - Display the numeric scenario count for each run.
   - Count source priority remains `scenario_ids.length`, then
     `scenario_count`, then `0`.
4. Preserve `仿真结果`, `模式`, `仿真场景`, `来源`, `工作树`, and `操作`.

## Filter Controls

Replace native inline selects for `场景数量`, `模式`, and `来源` with one
shared compact popover-filter pattern.

- The closed control is a small square icon button aligned to the right side of
  the column header.
- Clicking opens a dark popover anchored below the button and aligned to its
  right edge.
- The popover lists `全部` followed by the available values for that column.
- Scenario-count values are numeric and sorted ascending (`1`, `2`, `3`, ...).
- Mode and source preserve the existing display labels.
- Selecting an item applies the filter, resets pagination to page 1, and closes
  the popover.
- An active filter uses the existing cyan accent and shows a compact value badge
  at the button's upper-right corner.
- Only one filter popover may be open at a time.
- Escape and outside click close the popover without changing the selection.
- Buttons expose `aria-expanded`, `aria-haspopup="menu"`, and descriptive
  labels. Menu items are keyboard reachable.

Existing filter composition remains AND-based and continues to work with search,
sorting, and pagination.

## Sort Controls

Replace the combined `↕` control with two vertically separated icon buttons:

- Up arrow selects ascending order.
- Down arrow selects descending order.
- The active direction uses the cyan accent; inactive direction remains muted.
- Re-selecting the active direction is idempotent.
- Each button has a descriptive accessible label naming its column and direction.
- Controls use fixed dimensions so header layout does not shift.

Sortable data semantics remain unchanged except `序号`, which is not sortable.
The scenario-count column sorts numerically.

## Visual Rules

- Reuse current border, background, cyan accent, red danger, typography, and
  spacing tokens from the Evidence Library.
- Popovers use a maximum 6 px corner radius, subtle border, and restrained shadow.
- Do not introduce cards, gradients, decorative elements, or a second toolbar.
- Keep headers and cells horizontally and vertically centered.
- Preserve the current full-width table and 20/50 row pagination.
- Popovers must overlay rows rather than resize the table.

## Testing

Add focused component tests for:

- Cross-page continuous row numbering after sorting and filtering.
- Fractional-second removal.
- Numeric scenario-count display and numeric filter ordering.
- Popover open, selection, active badge, outside-click close, and Escape close.
- Mode and source popover filtering.
- Separate ascending and descending sort controls and active state.
- Existing search, pagination, overview, replay, and delete behavior remains green.

Run:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
npm run build
```

Browser verification uses `http://192.168.121.50:55763/#/evaluator` at desktop
width and confirms no clipped headers, overlapping menus, or horizontal layout
shift.
