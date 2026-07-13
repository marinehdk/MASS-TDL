# Evidence Library Table Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace Screen 04's UUID-first table and native header selects with continuous row numbering, second-precision time, numeric scenario counts, compact popover filters, and separate sort-direction controls.

**Architecture:** Keep all behavior inside the existing `EvidenceLibraryView` because the controls are local to one table and share its filter/sort/page state. Add small presentation helpers and one reusable header-filter component pattern; preserve RTK Query, evidence actions, search, and pagination contracts.

**Tech Stack:** React 18, TypeScript, Lucide React, Testing Library, Vitest, Vite.

## Global Constraints

- Modify only M8 Screen 04 frontend presentation and focused tests.
- No ROS2 topic, message, IDL, backend API, database, ODD, COLREGs, M5/M7, or SIL scenario changes.
- Row numbers follow filtered and sorted results and continue across pages.
- Run time format is exactly `YYYY-MM-DD HH:mm:ss` with fractional seconds removed.
- Scenario count source priority is `scenario_ids.length`, then `scenario_count`, then `0`.
- Scenario-count values sort numerically ascending in the filter menu.
- Only one filter popover may be open; Escape and outside click close it without changing selection.
- Active filters use the existing cyan accent and an upper-right compact value badge.
- Sort uses separate ascending and descending buttons with fixed dimensions and an active-direction state.
- Keep cells and headers centered; keep 20/50 pagination and full-width table.
- Reuse existing color, border, typography, and spacing variables; maximum popover radius is 6 px.

---

### Task 1: Row Presentation And Directional Sorting

**Files:**
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`

**Interfaces:**
- Consumes: `scenarioCount(session: EvidenceLibrarySession): number`, `sortedRows`, `safePage`, and `pageSize`.
- Produces: `SessionRow.scenarioCount: number`, `setSortDirection(key: SortKey, direction: SortDirection): void`, and table rows numbered with `safePage * pageSize + rowIndex + 1`.

- [ ] **Step 1: Add failing presentation tests**

Add focused tests with these assertions:

```tsx
expect(screen.getByRole('columnheader', { name: /序号/ })).toBeInTheDocument();
expect(screen.queryByText('会话ID')).not.toBeInTheDocument();
expect(screen.getByText('2026-07-07 13:20:00')).toBeInTheDocument();
expect(screen.queryByText(/13:20:00\.\d+/)).not.toBeInTheDocument();
expect(screen.getByRole('columnheader', { name: /场景数量/ })).toBeInTheDocument();
expect(within(firstDataRow).getByText('1')).toBeInTheDocument();
```

Use a 21-row fixture, navigate to page 2, and assert the first row number is
`21`. Click the explicit ascending and descending buttons and assert the time
order and `aria-pressed` states.

- [ ] **Step 2: Run tests and verify RED**

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "renders continuous row numbers|formats run time to whole seconds|uses separate sort directions"
```

Expected: failures mention missing `序号`, missing `场景数量`, and missing
direction-specific accessible buttons.

- [ ] **Step 3: Implement presentation helpers and row model**

Change time formatting and row shape:

```tsx
const displayRunTime = (value?: string | null) => {
  if (!value) return '-';
  return value
    .replace('T', ' ')
    .replace(/\.\d+(?=Z|[+-]\d\d:\d\d$)/, '')
    .replace(/([+-]\d\d:\d\d|Z)$/, '');
};

interface SessionRow {
  raw: EvidenceLibrarySession;
  time: string;
  timeValue: number;
  result: string;
  scenarioCount: number;
  mode: string;
  scenario: string;
  source: string;
  worktree: string;
}
```

Remove UUID display from the table but keep `raw.evidence_id` for React keys and
actions. Render the number cell as:

```tsx
{safePage * pageSize + rowIndex + 1}
```

- [ ] **Step 4: Implement separate direction controls**

Replace `toggleSort` with:

```tsx
const setSortDirection = (key: SortKey, direction: SortDirection) => {
  setSort({ key, direction });
  setPage(0);
};
```

For sortable headers render fixed-size up/down icon buttons. Each button uses
`aria-label={`按${label}升序`}` or `aria-label={`按${label}降序`}` and
`aria-pressed={sort.key === key && sort.direction === direction}`.

- [ ] **Step 5: Run focused GREEN tests**

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

Expected: all `EvidenceLibraryView` tests pass.

- [ ] **Step 6: Commit Task 1**

```bash
git add web/src/screens/evaluator/EvidenceLibraryView.tsx \
  web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
git commit -m "feat(evaluator): clarify evidence table columns"
```

### Task 2: Shared Popover Filters And Browser Verification

**Files:**
- Modify: `web/src/screens/evaluator/EvidenceLibraryView.tsx`
- Test: `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
- Evidence: `runs/e2e/evidence_library_table_controls_<timestamp>/`

**Interfaces:**
- Consumes: `filters`, `setFilter`, `rows`, and `uniqueValues` from the existing table.
- Produces: `openFilterKey: SortKey | null`, compact filter trigger buttons, and one anchored menu for `scenarioCount`, `mode`, or `source`.

- [ ] **Step 1: Add failing popover behavior tests**

Test the following sequence with Testing Library roles:

```tsx
const trigger = screen.getByRole('button', { name: '筛选场景数量' });
fireEvent.click(trigger);
expect(trigger).toHaveAttribute('aria-expanded', 'true');
expect(screen.getByRole('menu', { name: '场景数量筛选选项' })).toBeInTheDocument();
expect(screen.getAllByRole('menuitem').map((item) => item.textContent)).toEqual(['全部', '1', '2']);
fireEvent.click(screen.getByRole('menuitem', { name: '2' }));
expect(screen.getByLabelText('场景数量筛选值')).toHaveTextContent('2');
```

Add separate tests for mode and source selection, only-one-menu behavior,
outside click, Escape close, page reset, and filter composition with search.

- [ ] **Step 2: Run tests and verify RED**

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "opens compact filter menus|closes filter menus|composes popover filters"
```

Expected: failures mention missing filter buttons, menus, badges, and Escape
behavior.

- [ ] **Step 3: Implement shared filter state and lifecycle**

Add:

```tsx
const [openFilterKey, setOpenFilterKey] = useState<SortKey | null>(null);
const filterMenuRef = useRef<HTMLDivElement | null>(null);
```

Register one document `pointerdown` listener while a menu is open to close it
when neither trigger nor menu contains the target. Register one document
`keydown` listener that closes on Escape and restores focus to the matching
trigger. Remove both listeners in effect cleanup.

- [ ] **Step 4: Render compact trigger and anchored menu**

Use `LucideListFilter` in a 24 by 24 px button. Header wrapper is
`position: relative`; menu is `position: absolute`, `top: calc(100% + 6px)`,
`right: 0`, and `zIndex: 30`. Active badge is a small absolutely positioned
element with `aria-label={`${label}筛选值`}`. Selecting an item calls
`setFilter(key, value)`, closes the menu, and returns focus to the trigger.

- [ ] **Step 5: Run frontend verification**

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
npm run build
```

Expected: all focused tests pass; TypeScript and Vite build exit 0. Existing
Foxglove `eval` and output chunk-size warnings may remain unchanged.

- [ ] **Step 6: Verify the live browser**

Open `http://192.168.121.50:55763/#/evaluator` at 1920x1080 and verify:

```text
序号 is continuous across pages.
Times contain no fractional seconds.
场景数量 cells are numeric.
Each popover overlays rows and aligns to the trigger's right edge.
Only one menu opens; Escape and outside click close it.
Up/down sort controls do not resize the header.
No header, badge, popover, action, or pagination overlap.
```

Save a screenshot and a short result JSON under the evidence directory.

- [ ] **Step 7: Commit Task 2**

```bash
git add web/src/screens/evaluator/EvidenceLibraryView.tsx \
  web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
git commit -m "feat(evaluator): add compact evidence filters"
```

### Task 3: Final Review And Verification

**Files:**
- Review: all changes from the branch merge base through Task 2.
- Evidence: `.superpowers/sdd/` review package and reviewer report.

**Interfaces:**
- Consumes: Task 1 and Task 2 commits and their verification output.
- Produces: clean whole-branch review and final branch status.

- [ ] **Step 1: Run fresh combined verification**

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  src/screens/evaluator/__tests__/ReplayDetailView.test.tsx
npm run build
cd ..
git diff --check
git status --short
```

Expected: focused tests and build pass, diff check is clean, and only ignored
runtime/review evidence may remain untracked.

- [ ] **Step 2: Generate and review the whole-branch package**

```bash
/home/marine.huang/.codex/plugins/cache/openai-curated-remote/superpowers/6.1.1/skills/subagent-driven-development/scripts/review-package \
  6378001d4 HEAD
```

Dispatch a fresh high-capability reviewer. Fix every Critical or Important
finding and repeat review until verdict is `Ready to merge`.
