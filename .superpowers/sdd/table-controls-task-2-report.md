# Table Controls Task 2 Report

## Task Header

- Affected modules: M8 Web HMI only.
- Affected files: `web/src/screens/evaluator/EvidenceLibraryView.tsx` and
  `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`.
- ROS2 topics/messages/IDL: none.
- ODD impact: none.
- COLREGs impact: none.
- M5/M7 boundary impact: none.
- Required tests: focused Evidence Library view suite and web production build.
- Required SIL scenarios: none.
- Required evidence output: browser screenshot and result JSON are controller-owned
  under `runs/e2e/evidence_library_table_controls_<timestamp>/`.

## Delivered

- Replaced the three native header selects with one shared Lucide
  `ListFilter` popover pattern for `场景数量`, `模式`, and `来源`.
- Added one open-filter state, shared menu reference, trigger references,
  outside-pointer and Escape lifecycle cleanup, and focus restoration after
  Escape or selection.
- Added compact active-value badges, accessible trigger/menu labels, and
  keyboard-reachable `menuitem` buttons.
- Kept only one menu open, reset pagination on selection, and preserved
  AND composition with text search and other filters.
- Kept Task 1 behavior: separate sort controls, continuous row numbering,
  whole-second timestamps, numeric scenario-count display and sorting, and
  overview/replay/delete lifecycle behavior.

## TDD Evidence

RED command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "opens compact filter menus|closes filter menus|composes popover filters"
```

Result: 3 failures, each caused by missing compact filter trigger buttons.

GREEN command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "opens compact filter menus|closes filter menus|composes popover filters"
```

Result: 3 passed.

Focused regression command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

Result: 44 passed.

## Verification

```bash
cd web
npm run build
git diff --check
```

Result: TypeScript and Vite build passed; diff check passed. Existing Vite
warnings remain for Foxglove `eval` usage and an output chunk above 500 kB.

Browser verification was not run in this task because evidence collection is
controller-owned. No browser screenshot or result JSON was created here.

## Self Review

- `scenarioCount` unique values use numeric comparison, producing `1`, `2`,
  `10` ordering independently of locale collation.
- The menu is absolutely positioned beneath its trigger with `zIndex: 30`, so
  it overlays table rows without changing table layout.
- `pointerdown` ignores events inside the open trigger/menu; Escape closes and
  returns focus to the corresponding trigger; effect cleanup removes both
  document listeners.
- Focused tests cover scenario-count ordering and selection, mode/source
  selection, one-menu state, outside click, Escape, page reset, search
  composition, and existing Task 1 sorting behavior.

## Concerns

- None in local focused tests, build, or diff check.
- Browser layout evidence remains pending controller execution.
