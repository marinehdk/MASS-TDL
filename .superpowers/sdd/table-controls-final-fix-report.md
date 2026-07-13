# Table Controls Final Review Fix Report

Date: 2026-07-13

Branch: `codex/evidence-library-replay-impl`

Starting HEAD: `7df412920`

## Scope

- Affected module: M8 Screen 04 evidence library.
- Changed paths:
  - `web/src/screens/evaluator/EvidenceLibraryView.tsx`
  - `web/src/screens/evaluator/__tests__/EvidenceLibraryView.test.tsx`
  - `.superpowers/sdd/table-controls-final-fix-report.md`
- ROS2 topics/messages/IDL: none.
- ODD, COLREGs, and M5/M7 boundary impact: none.
- SIL scenarios: none.

## Root Causes and Fixes

1. `scenarioCount()` used truthiness fallback. A present empty `scenario_ids`
   array therefore fell through to `scenario_count`, overstating indexed rows
   and enabling replay without an indexed scenario. It now treats every
   non-null `scenario_ids` value as authoritative and falls back to
   `scenario_count` only when IDs are absent or null.
2. `displayRunTime()` removed fractional seconds only before a timezone suffix.
   Its fraction matcher now also accepts the end of a timezone-less value,
   while retaining the existing timezone suffix removal.

## TDD Evidence

RED command:

```bash
cd web
npm test -- EvidenceLibraryView.test.tsx
```

RED result: `2 failed, 43 passed`.

- Empty `scenario_ids: []` with `scenario_count: 12` rendered `12` rather than
  `0`, exposing the existing source-priority defect.
- `2026-07-07T13:20:00.123` rendered with `.123` rather than whole seconds.

GREEN command:

```bash
cd web
npm test -- EvidenceLibraryView.test.tsx
```

GREEN result: `45 passed`.

The added regression verifies the empty-ID row displays, sorts, and filters as
`0`, and disables replay. The timestamp regression verifies the timezone-less
ISO form renders exactly `2026-07-07 13:20:00`.

## Verification

```bash
cd web
npm test -- EvidenceLibraryView.test.tsx
npm run build
git diff --check
```

- Focused suite: `45 passed`.
- Production build: TypeScript passed; Vite built `2,396` modules.
- Diff check: passed.

## Concerns

- Existing Vite warnings remain: Foxglove dependency `eval` usage and a
  1,808.49 kB output chunk above the 500 kB warning threshold.
- No browser, container, SIL, or A4000 validation ran; this UI-only fix does
  not require them under the requested scope.
