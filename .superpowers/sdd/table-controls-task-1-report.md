# Table Controls Task 1 Report

## Scope

- M8 Web HMI only.
- No ROS2 topics, messages, IDL, ODD, COLREGs, M5/M7 boundary, SIL scenario,
  backend API, or database changes.

## Delivered

- Replaced displayed evidence UUIDs with continuous `序号` values.
- Replaced `套件` with numeric `场景数量`, using existing scenario-count priority.
- Formatted timestamps to whole seconds while retaining existing timezone stripping.
- Replaced combined sortable-header toggles with fixed-size, accessible ascending
  and descending Lucide icon buttons. Selecting either direction resets pagination.
- Preserved evidence IDs for React keys, hover state, search, overview, replay,
  and deletion actions.

## TDD Evidence

RED command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx \
  -t "renders continuous row numbers|formats run time to whole seconds|uses separate sort directions"
```

Result: 3 failures. Expected missing `序号`, missing `场景数量`, and missing
direction-specific sort buttons.

GREEN command:

```bash
cd web
npm test -- src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx
```

Result: 38 passed.

## Verification

```bash
cd web
npm run build
git diff --check
```

Result: build passed; diff check passed. Build retained pre-existing Rollup
warnings for `eval` in `@foxglove/rosmsg-serialization` and a large output chunk.

## Self Review

- Numbering derives from `safePage * pageSize + rowIndex + 1`, so it remains
  continuous after search, filtering, sorting, and page-size changes.
- Scenario count remains numeric for comparison and display.
- Active sort direction is exposed with `aria-pressed`; inactive direction stays
  available and selecting an already active direction is idempotent.
- Lifecycle, overview, replay, deletion, search, and pagination behavior remain
  covered by the focused suite.
