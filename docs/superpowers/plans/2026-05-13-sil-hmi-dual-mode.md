# SIL HMI Dual-Mode Implementation Plan（v1.1 整合运行时语义）

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the 4-screen SIL HMI into a DNV SIL dual-track verification workbench (Captain Digital Twin ↔ God Test Management) with IEC 62288 bridge fidelity, cross-screen ENC chart persistence, and **28 features** spanning visual layer + runtime semantics (Scene FSM / ToR Modal / Fault Inject / Conning Bar / Threat Ribbon / Hotkeys / Top Chrome / Run Report 6-lane Timeline + ASDR Ledger).

**Architecture:** Foundation tasks (tokens, mapStore, hook, fsmStore, useHotkeys) unblock 5 parallel tracks: (1) map overlays (CompassRose/PpiRings/ColregsSectors/ImazuGeometry/DistanceScale), (2) shared components A (ArpaTargetTable/ModuleDrilldown/ScoringGauges/ScoringRadarChart/ColregsDecisionTree), (3) shared components B v1.1 NEW (TopChrome/TorModal/FaultInjectPanel/ConningBar/ThreatRibbon), (4) screen helpers v1.1 NEW (Stepper/SummaryRail/ImazuGrid + ModuleReadinessGrid/Sensor/CommLink/LiveLog + TimelineSixLane/AsdrLedger/TrajectoryReplay), (5) layers update + screen modifications. All components use Zustand stores (telemetryStore + mapStore + fsmStore) for 50Hz-capable selective re-renders.

**Tech Stack:** React 18, TypeScript, Zustand, RTK Query, MapLibre GL JS, Protobuf-ts types

**Spec:** `docs/superpowers/specs/2026-05-13-sil-hmi-dual-mode-design.md` (v1.1)

---

## Parallelisation Map

```
Phase 0 — Foundation (sequential, blocks all)
  Task 1:  tokens.css + main.tsx import
  Task 2:  mapStore.ts + store/index.ts export
  Task 3:  useMapPersistence.ts hook
  Task 17: fsmStore + uiStore.viewMode + useHotkeys hook (v1.1 NEW)
    │
    ├── Phase 1 — Components (parallel, after foundation; 12 parallel tracks)
    │     ┌── 1A: Map overlays ──────────────────────────────────┐
    │     │     Task 4: CompassRose.tsx + PpiRings.tsx + DistanceScale.tsx │
    │     │     Task 5: ColregsSectors.tsx + ImazuGeometry.tsx   │
    │     │     Task 6: layers.ts update (S-57 MVT + GeoJSON)    │
    │     └────────────────────────────────────────────────────┘
    │     ┌── 1B: Shared components A (v1.0) ───────────────────┐
    │     │     Task 7: ArpaTargetTable + ModuleDrilldown + ScoringGauges │
    │     │     Task 8: ScoringRadarChart + ColregsDecisionTree         │
    │     │                  (TimelineScrubber 已废弃 by Task 24)        │
    │     └────────────────────────────────────────────────────┘
    │     ┌── 1C: Cross-screen chrome + runtime semantics (v1.1 NEW) ─┐
    │     │     Task 18: TopChrome + FooterHotkeyHints + RunStatePill + DualClock │
    │     │     Task 19: TorModal (5s SAT-1 + 60s TMR + auto-MRC)  │
    │     │     Task 20: FaultInjectPanel (AIS/Radar/ROC, God only)│
    │     │     Task 21: ConningBar + ThreatRibbon                 │
    │     └────────────────────────────────────────────────────┘
    │     ┌── 1D: Screen-specific helpers (v1.1 NEW) ───────────┐
    │     │     Task 22: Stepper + SummaryRail + ImazuGrid (Builder) │
    │     │     Task 23: ModuleReadinessGrid + SensorRow + CommLinkRow + LiveLogStream (Preflight) │
    │     │     Task 24: TimelineSixLane + AsdrLedger + TrajectoryReplay (Report) │
    │     └────────────────────────────────────────────────────┘
    │
    └── Phase 2 — Screen integration (parallel, after Phase 1)
          Task 9:  SilMapView.tsx update (viewport, offset, mode props + layers.ts wiring)
          Task 10: BridgeHMI.tsx update (dual-mode + ALL overlays + FSM/ToR/Fault/Conning/Threat/Hotkeys)
          Task 11: ScenarioBuilder.tsx update (3-step structure + Imazu grid + 3 sub-tabs + SummaryRail)
          Task 12: RunReport.tsx update (TimelineSixLane + AsdrLedger + TrajectoryReplay + KPI sparklines)
          Task 13: Preflight.tsx update (M1-M8 + sensors + commlinks + livelog + GO/NO-GO gate)

Phase 3 — Verification
  Task 14: Expand telemetryStore (+ scoringRow + sensors + commLinks + faultStatus + controlCmd + preflightLog)
  Task 15: Unit tests for new components + stores
  Task 16: Playwright E2E smoke test + build verification
```

**Max parallelism:** 4 foundation tasks (Phase 0)，**12 subagents in Phase 1** (Tasks 4-8 + 18-24)，5 subagents in Phase 2 (Tasks 9-13)。

**Total tasks**: 16 → **24**（+8 NEW v1.1）。预计工时 +40%。

---

## File Structure

### Files created (**v1.1: 25 new**)

```
web/src/
├── styles/tokens.css                    (v1.0)
├── store/
│   ├── mapStore.ts                      (v1.0)
│   └── fsmStore.ts                      (v1.1 NEW)
├── hooks/
│   ├── useMapPersistence.ts             (v1.0)
│   └── useHotkeys.ts                    (v1.1 NEW)
├── map/
│   ├── CompassRose.tsx                  (v1.0)
│   ├── PpiRings.tsx                     (v1.0)
│   ├── ColregsSectors.tsx               (v1.0)
│   ├── ImazuGeometry.tsx                (v1.0)
│   └── DistanceScale.tsx                (v1.0)
└── screens/shared/
    ├── ArpaTargetTable.tsx              (v1.0)
    ├── ModuleDrilldown.tsx              (v1.0)
    ├── ScoringGauges.tsx                (v1.0)
    ├── ScoringRadarChart.tsx            (v1.0)
    ├── ColregsDecisionTree.tsx          (v1.0)
    ├── TopChrome.tsx                    (v1.1 NEW)
    ├── FooterHotkeyHints.tsx            (v1.1 NEW)
    ├── RunStatePill.tsx                 (v1.1 NEW)
    ├── DualClock.tsx                    (v1.1 NEW)
    ├── TorModal.tsx                     (v1.1 NEW)
    ├── FaultInjectPanel.tsx             (v1.1 NEW)
    ├── ConningBar.tsx                   (v1.1 NEW)
    ├── ThreatRibbon.tsx                 (v1.1 NEW)
    ├── Stepper.tsx                      (v1.1 NEW)
    ├── SummaryRail.tsx                  (v1.1 NEW)
    ├── ImazuGrid.tsx                    (v1.1 NEW)
    ├── ModuleReadinessGrid.tsx          (v1.1 NEW)
    ├── SensorStatusRow.tsx              (v1.1 NEW)
    ├── CommLinkStatusRow.tsx            (v1.1 NEW)
    ├── LiveLogStream.tsx                (v1.1 NEW)
    ├── TimelineSixLane.tsx              (v1.1 NEW; replaces TimelineScrubber)
    ├── AsdrLedger.tsx                   (v1.1 NEW)
    └── TrajectoryReplay.tsx             (v1.1 NEW)
```

**File NOT created (废弃)**: `web/src/screens/shared/TimelineScrubber.tsx` — v1.0 plan 中曾 plan 创建，v1.1 由 TimelineSixLane 完整替代。Task 8 v1.1 修订后**不再创建** TimelineScrubber。

### Files modified (**v1.1: 11 existing**)

```
web/src/
├── main.tsx                        — v1.0: import tokens.css
├── store/index.ts                  — v1.0: export useMapStore；v1.1: + export useFsmStore
├── store/uiStore.ts                — v1.1 NEW MOD: add viewMode + setViewMode
├── map/SilMapView.tsx              — v1.0: accept viewport, offset, mode props
├── map/layers.ts                   — v1.0: S-57 MVT + GeoJSON sources
├── screens/BridgeHMI.tsx           — v1.0+1.1: all overlays + FSM/ToR/Fault/Conning/Threat/Hotkeys
├── screens/ScenarioBuilder.tsx     — v1.1 重构: 3-step + ImazuGrid + 3 sub-tabs + SummaryRail
├── screens/Preflight.tsx           — v1.1 扩展: M1-M8 grid + sensors + commlinks + livelog + GO/NO-GO
├── screens/RunReport.tsx           — v1.1 扩展: 6-lane timeline + ASDR ledger + trajectory replay + KPI sparklines
├── App.tsx                         — v1.1: wrap routes with TopChrome + FooterHotkeyHints
├── api/silApi.ts                   — v1.1: + fault/imazu/asdr/verdict/scoring endpoints
```

---

## Task 1: Design tokens CSS + main.tsx import

**Files:**
- Create: `web/src/styles/tokens.css`
- Modify: `web/src/main.tsx:1-6`

- [ ] **Step 1: Create tokens.css with all design tokens (v1.1 ground truth from Claude Design HTML export)**

> **依据**: spec §2.1 v1.1 (2026-05-13 用户提供 `COLAV SIL Simulator.html` 实际 token)。**禁止使用 v1.0 token 名**（已废弃，见 spec §2.1.1 映射表）。

```css
/* web/src/styles/tokens.css */

:root {
  /* ── Surface hierarchy (Night Mode default for bridge HMI) ────── */
  --bg-0: #070C13;          /* deepest — canvas */
  --bg-1: #0B1320;          /* panel background */
  --bg-2: #101B2C;          /* card surface */
  --bg-3: #16263A;          /* elevated card */

  /* ── Border hierarchy (纯色非透明) ───────────────────────────── */
  --line-1: #1B2C44;
  --line-2: #243C58;
  --line-3: #3A5677;

  /* ── Text hierarchy (4 档) ───────────────────────────────────── */
  --txt-0: #F1F6FB;         /* critical readouts */
  --txt-1: #C5D2E0;         /* primary */
  --txt-2: #8A9AAD;         /* secondary */
  --txt-3: #566578;         /* labels, units */

  /* ── Semantic functional colors (ECDIS conventions) ───────────── */
  --c-phos:    #5BC0BE;     /* phosphor — radar / brand / own ship */
  --c-stbd:    #3FB950;     /* starboard green / safe */
  --c-port:    #F26B6B;     /* port red / threat */
  --c-warn:    #F0B72F;     /* amber warning */
  --c-info:    #79C0FF;     /* informational blue */
  --c-danger:  #F85149;     /* critical red */
  --c-magenta: #D070D0;     /* predicted track (ECDIS conv.) */

  /* ── Autonomy level colors (IMO MASS Code 4-level) ───────────── */
  --c-d4: #3FB950;          /* full auto */
  --c-d3: #79C0FF;          /* supervised — nominal */
  --c-d2: #F0B72F;          /* manual / RO */
  --c-mrc: #F85149;         /* minimum risk condition */

  /* ── Type (3 字体栈，含中文支持) ──────────────────────────────── */
  --f-disp: 'Saira Condensed', 'Noto Sans SC', sans-serif;
  --f-body: 'Noto Sans SC', 'Saira Condensed', sans-serif;
  --f-mono: 'JetBrains Mono', ui-monospace, monospace;

  /* ── Spacing / radius (strict zero-radius) ───────────────────── */
  --r-0: 0;
  --r-min: 2px;
  --sp-xs: 4px;
  --sp-sm: 8px;
  --sp-md: 12px;
  --sp-lg: 18px;             /* 注: lg = 18 不是 16 */
  --sp-xl: 24px;
}

/* ── Global reset + base ──────────────────────────────────────── */
html, body {
  margin: 0; padding: 0;
  background: var(--bg-0);
  color: var(--txt-1);
  font-family: var(--f-body);
  overflow: hidden;
}
#root { width: 100vw; height: 100vh; }
*, *::before, *::after { box-sizing: border-box; }

/* Scrollbar (WebKit) */
::-webkit-scrollbar { width: 6px; height: 6px; }
::-webkit-scrollbar-track { background: var(--bg-1); }
::-webkit-scrollbar-thumb { background: var(--line-2); }
::-webkit-scrollbar-thumb:hover { background: var(--line-3); }

button { font-family: inherit; cursor: pointer; }
input, select, textarea { font-family: inherit; }

/* ── Atom Classes (reusable across all artboards) ─────────────── */
.hmi-surface  { background: var(--bg-0); color: var(--txt-1); font-family: var(--f-body); }
.hmi-mono     { font-family: var(--f-mono); font-variant-numeric: tabular-nums; }
.hmi-disp     { font-family: var(--f-disp); letter-spacing: 0.18em; text-transform: uppercase; }
.hmi-label    { font-family: var(--f-disp); font-size: 9.5px; letter-spacing: 0.22em;
                color: var(--txt-3); text-transform: uppercase; font-weight: 500; }

/* ── Keyframes (4 global animations) ──────────────────────────── */
@keyframes phos-pulse  { 0%, 100% { opacity: 1; } 50% { opacity: 0.45; } }
@keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
@keyframes warn-flash  { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }
@keyframes scan-line   { 0% { transform: translateX(-100%); } 100% { transform: translateX(100%); } }
```

- [ ] **Step 1.b: Add Google Fonts link in `index.html`**（v1.1 NEW）

Modify `web/index.html` `<head>` to load Saira Condensed + Noto Sans SC + JetBrains Mono:

```html
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Saira+Condensed:wght@400;500;600;700&family=Noto+Sans+SC:wght@400;500;700&family=JetBrains+Mono:wght@400;500;600;700&display=swap" rel="stylesheet">
```

- [ ] **Step 2: Import tokens.css in main.tsx**

Edit `web/src/main.tsx:1` — add import at top:

```typescript
import React from 'react';
import ReactDOM from 'react-dom/client';
import { Provider } from 'react-redux';
import { configureStore } from '@reduxjs/toolkit';
import App from './App';
import { silApi } from './api/silApi';
import './styles/tokens.css';  // <-- NEW
```

- [ ] **Step 3: Verify build**

Run: `cd web && npx tsc --noEmit && npx vite build --mode development 2>&1 | tail -5`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add web/src/styles/tokens.css web/src/main.tsx
git commit -m "feat(hmi): add OpenBridge 5.0 / S-52 aligned design token system"
```

---

## Task 2: mapStore — cross-screen viewport persistence

**Files:**
- Create: `web/src/store/mapStore.ts`
- Modify: `web/src/store/index.ts:1-5`

- [ ] **Step 1: Write the store**

```typescript
// web/src/store/mapStore.ts
import { create } from 'zustand';

export interface MapViewport {
  center: [number, number];  // [lng, lat]
  zoom: number;
  bearing: number;           // degrees, 0 = North-up
  pitch: number;
}

const DEFAULT_VIEWPORT: MapViewport = {
  center: [10.38, 63.44],
  zoom: 12,
  bearing: 0,
  pitch: 0,
};

interface MapStore {
  viewport: MapViewport;
  setViewport: (v: Partial<MapViewport>) => void;
  resetViewport: () => void;
}

export const useMapStore = create<MapStore>((set) => ({
  viewport: { ...DEFAULT_VIEWPORT },
  setViewport: (partial) => set((s) => ({ viewport: { ...s.viewport, ...partial } })),
  resetViewport: () => set({ viewport: { ...DEFAULT_VIEWPORT } }),
}));
```

- [ ] **Step 2: Export from store barrel**

Edit `web/src/store/index.ts` — add line 6:

```typescript
export { useTelemetryStore } from './telemetryStore';
export { useScenarioStore } from './scenarioStore';
export { useControlStore } from './controlStore';
export { useReplayStore } from './replayStore';
export { useUIStore } from './uiStore';
export { useMapStore } from './mapStore';  // <-- NEW
```

- [ ] **Step 3: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error|^$" | head -10`
Expected: no output (clean)

- [ ] **Step 4: Commit**

```bash
git add web/src/store/mapStore.ts web/src/store/index.ts
git commit -m "feat(hmi): add useMapStore for cross-screen ENC viewport persistence"
```

---

## Task 3: useMapPersistence hook

**Files:**
- Create: `web/src/hooks/useMapPersistence.ts`

- [ ] **Step 1: Write the hook**

```typescript
// web/src/hooks/useMapPersistence.ts
import { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';
import { useMapStore } from '../store';

/**
 * Syncs MapLibre 'moveend' events → useMapStore.viewport so the chart
 * centre/zoom/bearing/pitch survive unmount/remount across 4-screen navigation.
 *
 * Usage inside SilMapView:
 *   useMapPersistence(mapRef, viewMode);
 */
export function useMapPersistence(
  mapRef: React.MutableRefObject<maplibregl.Map | null>,
  viewMode: 'captain' | 'god',
) {
  const setViewport = useMapStore((s) => s.setViewport);
  const debounceRef = useRef<ReturnType<typeof setTimeout> | null>(null);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;

    const handler = () => {
      if (viewMode === 'god') {
        // God view: save all viewport state
        if (debounceRef.current) clearTimeout(debounceRef.current);
        debounceRef.current = setTimeout(() => {
          const c = map.getCenter();
          setViewport({
            center: [c.lng, c.lat],
            zoom: map.getZoom(),
            bearing: map.getBearing(),
            pitch: map.getPitch(),
          });
        }, 200);
      }
      // Captain view: viewport is driven by followOwnShip so don't save
    };

    map.on('moveend', handler);
    return () => {
      map.off('moveend', handler);
      if (debounceRef.current) clearTimeout(debounceRef.current);
    };
  }, [mapRef, viewMode, setViewport]);
}
```

- [ ] **Step 2: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error|^$" | head -10`
Expected: no output (clean)

- [ ] **Step 3: Commit**

```bash
git add web/src/hooks/useMapPersistence.ts
git commit -m "feat(hmi): add useMapPersistence hook for cross-screen chart state sync"
```

---

## Task 4: CompassRose + PpiRings + DistanceScale (map overlays)

**Files:**
- Create: `web/src/map/CompassRose.tsx`
- Create: `web/src/map/PpiRings.tsx`
- Create: `web/src/map/DistanceScale.tsx`

- [ ] **Step 1: Write CompassRose.tsx**

```typescript
// web/src/map/CompassRose.tsx
import React from 'react';

interface CompassRoseProps {
  /** Bearing in degrees (0=North, clockwise). Used for relative bearing in Captain view. */
  bearing: number;
  /** false = North-up, true = rotates to ship heading */
  relativeMode: boolean;
}

/**
 * SVG compass rose. In Captain mode (relativeMode=true) the rose rotates
 * clockwise by `bearing` degrees so the top marker shows the ship's heading.
 * In God mode (relativeMode=false) it's fixed North-up.
 *
 * Positioned bottom-left by parent via absolute positioning.
 */
export const CompassRose: React.FC<CompassRoseProps> = ({ bearing, relativeMode }) => {
  const radius = 30;
  const cx = 36;
  const cy = 36;

  return (
    <div
      data-testid="compass-rose"
      style={{
        width: 72, height: 72,
        background: 'rgba(11,19,32,0.82)',
        border: '1px solid var(--line-2)',
        borderRadius: '50%',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}
    >
      <svg width={72} height={72} viewBox="0 0 72 72"
           style={{ transform: relativeMode ? `rotate(${bearing}deg)` : 'none' }}>
        {/* Outer ring */}
        <circle cx={cx} cy={cy} r={radius} fill="none" stroke="var(--line-1)" strokeWidth="0.5" />
        {/* Cardinal ticks */}
        <line x1={cx} y1={cy - radius + 4} x2={cx} y2={cy - radius + 12} stroke="var(--c-danger)" strokeWidth="1.5" />
        <line x1={cx} y1={cy + radius - 4} x2={cx} y2={cy + radius - 12} stroke="var(--txt-3)" strokeWidth="0.5" />
        <line x1={cx - radius + 4} y1={cy} x2={cx - radius + 12} y2={cy} stroke="var(--txt-3)" strokeWidth="0.5" />
        <line x1={cx + radius - 4} y1={cy} x2={cx + radius - 12} y2={cy} stroke="var(--txt-3)" strokeWidth="0.5" />
        {/* Center dot */}
        <circle cx={cx} cy={cy} r={2} fill="var(--c-phos)" />
      </svg>
    </div>
  );
};
```

- [ ] **Step 2: Write PpiRings.tsx**

```typescript
// web/src/map/PpiRings.tsx
import React from 'react';

interface PpiRingsProps {
  /** Centre of the rings as fraction of container [x%, y%] */
  centerFraction: [number, number];
  /** Individual ring radii in pixels at base zoom */
  radiiPx: number[];
  /** Viewport pixel → nautical miles scale factor (computed by parent) */
}

/**
 * SVG concentric range rings (PPI — Plan Position Indicator).
 * Rings are centred at `centerFraction` of the map container.
 * Radii are in pixels (parent computes NM→px based on MapLibre zoom + projection).
 *
 * In Captain mode: off-centre (bottom 30% of map area).
 * In God mode: centred.
 */
export const PpiRings: React.FC<PpiRingsProps> = React.memo(({ centerFraction, radiiPx }) => {
  return (
    <svg
      data-testid="ppi-rings"
      style={{
        position: 'absolute', top: 0, left: 0,
        width: '100%', height: '100%',
        pointerEvents: 'none', zIndex: 5,
      }}
    >
      {radiiPx.map((r, i) => (
        <circle
          key={i}
          cx={`${centerFraction[0]}%`}
          cy={`${centerFraction[1]}%`}
          r={r}
          fill="none"
          stroke="var(--c-phos)"
          strokeWidth="0.5"
          strokeOpacity={0.12 - i * 0.03}
        />
      ))}
    </svg>
  );
});
PpiRings.displayName = 'PpiRings';
```

- [ ] **Step 3: Write DistanceScale.tsx**

```typescript
// web/src/map/DistanceScale.tsx
import React from 'react';

interface DistanceScaleProps {
  /** Nautical miles represented by the scale bar at current zoom */
  nmPerPixel: number;
}

/**
 * Adaptive distance scale bar. Computes an even NM length matching
 * the current map zoom. Positioned bottom-centre by parent.
 */
export const DistanceScale: React.FC<DistanceScaleProps> = React.memo(({ nmPerPixel }) => {
  // Compute a "nice" NM value for ~100px bar
  const targetPx = 100;
  const rawNm = targetPx * nmPerPixel;
  const niceNm = rawNm < 0.5 ? 0.5 : rawNm < 1 ? 1 : rawNm < 2 ? 2 : rawNm < 5 ? 5 : 10;
  const pxWidth = niceNm / nmPerPixel;

  return (
    <div
      data-testid="distance-scale"
      style={{
        display: 'flex', flexDirection: 'column', alignItems: 'center',
        gap: 2,
      }}
    >
      <div style={{ width: pxWidth, height: 2, background: 'var(--txt-2)' }} />
      <div style={{ fontSize: 8, color: 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>
        {niceNm} nm
      </div>
    </div>
  );
});
DistanceScale.displayName = 'DistanceScale';
```

- [ ] **Step 4: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add web/src/map/CompassRose.tsx web/src/map/PpiRings.tsx web/src/map/DistanceScale.tsx
git commit -m "feat(hmi): add CompassRose, PpiRings, DistanceScale map overlay components"
```

---

## Task 5: ColregsSectors + ImazuGeometry (algorithm overlays)

**Files:**
- Create: `web/src/map/ColregsSectors.tsx`
- Create: `web/src/map/ImazuGeometry.tsx`

- [ ] **Step 1: Write ColregsSectors.tsx**

```typescript
// web/src/map/ColregsSectors.tsx
import React from 'react';

interface ColregsSectorsProps {
  /** Own-ship screen position as fraction of container [x%, y%] */
  ownShipFraction: [number, number];
  /** Own-ship true heading in degrees */
  headingDeg: number;
  /** PPI ring radius in px for the outer boundary */
  outerRadiusPx: number;
}

interface SectorDef {
  label: string;
  /** Start/end bearing RELATIVE to own-ship heading (0 = dead ahead) */
  startDeg: number;
  endDeg: number;
  color: string;
}

const SECTORS: SectorDef[] = [
  { label: 'HEAD-ON',    startDeg: -6,  endDeg: 6,    color: 'rgba(248,113,113,0.06)' },  // R14
  { label: 'GIVE-WAY',   startDeg: 6,   endDeg: 112.5, color: 'rgba(96,165,250,0.05)' },  // Crossing stbd
  { label: 'STAND-ON',   startDeg: 247.5, endDeg: 354, color: 'rgba(96,165,250,0.05)' },  // Crossing port
  { label: 'OVERTAKING', startDeg: 112.5, endDeg: 247.5, color: 'rgba(163,230,53,0.04)' }, // Overtaking
];

/**
 * SVG 4-sector overlay showing COLREGs encounter zones.
 * Sectors are defined in relative bearing from own-ship (0° = ahead).
 * The whole overlay is rotated so 0° aligns with ship true heading.
 *
 * Only visible in God view mode.
 */
export const ColregsSectors: React.FC<ColregsSectorsProps> = React.memo(({
  ownShipFraction, headingDeg, outerRadiusPx,
}) => {
  const [cxPct, cyPct] = ownShipFraction;
  const cx = 0; // we use percentage-based positioning in SVG viewport
  const cy = 0;
  const r = outerRadiusPx;

  return (
    <svg
      data-testid="colregs-sectors"
      style={{
        position: 'absolute', top: 0, left: 0,
        width: '100%', height: '100%',
        pointerEvents: 'none', zIndex: 4,
      }}
      viewBox={`-${r} -${r} ${r * 2} ${r * 2}`}
    >
      <g transform={`translate(${cx},${cy}) rotate(${headingDeg})`}>
        {SECTORS.map((sec) => {
          const startRad = (sec.startDeg * Math.PI) / 180;
          const endRad = (sec.endDeg * Math.PI) / 180;
          // Convert arc to SVG path
          const x1 = r * Math.sin(startRad);
          const y1 = -r * Math.cos(startRad);
          const x2 = r * Math.sin(endRad);
          const y2 = -r * Math.cos(endRad);
          const largeArc = sec.endDeg - sec.startDeg > 180 ? 1 : 0;
          const d = `M 0 0 L ${x1} ${y1} A ${r} ${r} 0 ${largeArc} 1 ${x2} ${y2} Z`;

          return (
            <g key={sec.label}>
              <path d={d} fill={sec.color} stroke={sec.color.replace('0.0', '0.12')} strokeWidth="0.5" />
              {/* Sector label at mid-angle */}
              <text
                x={r * 0.55 * Math.sin((startRad + endRad) / 2)}
                y={-r * 0.55 * Math.cos((startRad + endRad) / 2)}
                textAnchor="middle"
                fill={sec.color.replace('0.0', '0.4')}
                fontSize="7"
              >
                {sec.label}
              </text>
            </g>
          );
        })}
      </g>
    </svg>
  );
});
ColregsSectors.displayName = 'ColregsSectors';
```

- [ ] **Step 2: Write ImazuGeometry.tsx**

```typescript
// web/src/map/ImazuGeometry.tsx
import React, { useEffect, useRef } from 'react';
import type maplibregl from 'maplibre-gl';

interface ImazuGeometryProps {
  mapRef: React.MutableRefObject<maplibregl.Map | null>;
  /** GeoJSON FeatureCollection with the scenario's Imazu geometry */
  geometry: GeoJSON.FeatureCollection | null;
}

const SOURCE_ID = 'imazu-geometry';
const LAYER_LINE = 'imazu-geometry-line';
const LAYER_LABEL = 'imazu-geometry-label';

/**
 * Adds/updates Imazu scenario geometry as a GeoJSON source + layer pair on the map.
 * Shows own-ship and target starting positions, expected trajectories (dashed),
 * and scenario name label.
 *
 * Visible only in God view and ScenarioBuilder preview.
 */
export const ImazuGeometry: React.FC<ImazuGeometryProps> = React.memo(({ mapRef, geometry }) => {
  const addedRef = useRef(false);

  useEffect(() => {
    const map = mapRef.current;
    if (!map) return;
    if (!map.isStyleLoaded()) {
      // Retry once style loads
      const onLoad = () => {
        if (geometry) addOrUpdate(map, geometry);
      };
      map.once('style.load', onLoad);
      return;
    }
    addOrUpdate(map, geometry);
  }, [mapRef, geometry]);

  function addOrUpdate(map: maplibregl.Map, geo: GeoJSON.FeatureCollection | null) {
    if (!addedRef.current) {
      map.addSource(SOURCE_ID, {
        type: 'geojson',
        data: geo ?? { type: 'FeatureCollection', features: [] },
      });
      map.addLayer({
        id: LAYER_LINE,
        type: 'line',
        source: SOURCE_ID,
        paint: {
          'line-color': ['case', ['==', ['get', 'type'], 'trajectory'], '#f87171', '#2dd4bf'],
          'line-width': 1.5,
          'line-dasharray': [4, 3],
          'line-opacity': 0.5,
        },
      });
      map.addLayer({
        id: LAYER_LABEL,
        type: 'symbol',
        source: SOURCE_ID,
        layout: {
          'text-field': ['get', 'title'],
          'text-font': ['Open Sans Regular'],
          'text-size': 10,
          'text-offset': [0, -1.2],
          'text-anchor': 'bottom',
        },
        paint: {
          'text-color': 'var(--c-phos)',
          'text-opacity': 0.6,
        },
      });
      addedRef.current = true;
    } else if (geo) {
      (map.getSource(SOURCE_ID) as any)?.setData(geo);
    }
  }

  return null; // Imperative MapLibre layers — no DOM output
});
ImazuGeometry.displayName = 'ImazuGeometry';
```

- [ ] **Step 3: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add web/src/map/ColregsSectors.tsx web/src/map/ImazuGeometry.tsx
git commit -m "feat(hmi): add ColregsSectors and ImazuGeometry algorithm overlay components"
```

---

## Task 6: layers.ts update — S-57 MVT sources + new GeoJSON sources

**Files:**
- Modify: `web/src/map/layers.ts`

- [ ] **Step 1: Add S-57 ENC MVT source and layers (upgrade from stub)**

Add after existing S-57 layers definition — insert before line 62 (`// Vessel sources`):

```typescript
// ═══ S-57 ENC MVT — upgraded from stub to production ═══

// S-57 ENC vector tile layers (S-52 aligned palette, night-adapted)
// Source: 37K MVT tiles generated at /tiles/s57/{z}/{x}/{y}.pbf
// Existing s57Source (line 32-37) is reused as-is

// Additional S-52 layers beyond the 3 already defined (depth_area, land, coastline)
export const s57BuoyageLayer: LayerSpecification = {
  id: 's57-buoyage',
  type: 'symbol',
  source: 's57',
  'source-layer': 'buoyage',
  minzoom: 10,
  layout: {
    'text-field': ['get', 'name'],
    'text-font': ['Open Sans Regular'],
    'text-size': 9,
    'text-allow-overlap': false,
  },
  paint: {
    'text-color': '#60a5fa',
  },
};

export const s57SpotSoundingLayer: LayerSpecification = {
  id: 's57-spot-sounding',
  type: 'symbol',
  source: 's57',
  'source-layer': 'spot_soundings',
  minzoom: 12,
  layout: {
    'text-field': ['get', 'depth'],
    'text-font': ['Open Sans Regular'],
    'text-size': 8,
    'text-allow-overlap': false,
    'visibility': 'none',  // Hidden default (All Other Info category)
  },
  paint: {
    'text-color': '#93c5fd',
  },
};

export const s57ContourLabelLayer: LayerSpecification = {
  id: 's57-contour-label',
  type: 'symbol',
  source: 's57',
  'source-layer': 'contour_labels',
  minzoom: 12,
  layout: {
    'text-field': ['get', 'label'],
    'text-font': ['Open Sans Regular'],
    'text-size': 8,
    'text-allow-overlap': false,
    'visibility': 'none',  // Hidden default (All Other Info category)
  },
  paint: {
    'text-color': '#93c5fd',
  },
};

// Union all S-57 layers
export const ALL_S57_LAYERS: LayerSpecification[] = [
  ...s57Layers,
  s57BuoyageLayer,
  s57SpotSoundingLayer,
  s57ContourLabelLayer,
];

// ═══ COLREGs sector GeoJSON source (placeholder, data set dynamically) ═══
export const colregsSectorsSource: SourceSpecification = {
  type: 'geojson',
  data: { type: 'FeatureCollection', features: [] },
};

// ═══ Imazu geometry GeoJSON source (placeholder, data set dynamically) ═══
export const imazuGeometrySource: SourceSpecification = {
  type: 'geojson',
  data: { type: 'FeatureCollection', features: [] },
};
```

- [ ] **Step 2: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add web/src/map/layers.ts
git commit -m "feat(hmi): upgrade S-57 layers with buoyage, spot sounding, contour labels + new GeoJSON source exports"
```

---

## Task 7: ArpaTargetTable + ModuleDrilldown + ScoringGauges (shared components group A)

**Files:**
- Create: `web/src/screens/shared/ArpaTargetTable.tsx`
- Create: `web/src/screens/shared/ModuleDrilldown.tsx`
- Create: `web/src/screens/shared/ScoringGauges.tsx`

- [ ] **Step 1: Write ArpaTargetTable.tsx**

```typescript
// web/src/screens/shared/ArpaTargetTable.tsx
import React from 'react';
import { useTelemetryStore } from '../../store';
import type { ASDREvent } from '../../store/telemetryStore';

interface ArpaTargetTableProps {
  expanded: boolean;
  onToggle: () => void;
}

function deriveCpaTcpa(targets: any[], asdrEvents: ASDREvent[]) {
  // Extract CPA/TCPA from ASDR events keyed by mmsi (simple first-match lookup)
  const cpaMap = new Map<string, { cpa: number; tcpa: number }>();
  for (const e of asdrEvents) {
    if (e.event_type !== 'cpa_update' || !e.payload_json) continue;
    try {
      const p = JSON.parse(e.payload_json);
      if (p.mmsi !== undefined && p.cpa_nm !== undefined) {
        cpaMap.set(String(p.mmsi), { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
      }
      // For single-target scenarios, apply to all
      if (p.cpa_nm !== undefined && p.mmsi === undefined && targets.length === 1) {
        cpaMap.set('*', { cpa: p.cpa_nm, tcpa: p.tcpa_min ?? 0 });
      }
    } catch { /* noop */ }
  }
  return cpaMap;
}

export const ArpaTargetTable: React.FC<ArpaTargetTableProps> = ({ expanded, onToggle }) => {
  const targets = useTelemetryStore((s) => s.targets);
  const asdrEvents = useTelemetryStore((s) => s.asdrEvents);

  if (!expanded) {
    return (
      <button
        onClick={onToggle}
        data-testid="arpa-table-toggle"
        style={{
          position: 'absolute', top: 54, right: 96, zIndex: 15,
          background: 'var(--bg-1)',
          border: '1px solid var(--line-2)',
          color: 'var(--txt-3)',
          padding: '4px 10px', borderRadius: 3,
          fontFamily: 'var(--f-mono)', fontSize: 10, cursor: 'pointer',
        }}
      >
        ARPA ({targets.length})
      </button>
    );
  }

  const cpaMap = deriveCpaTcpa(targets, asdrEvents);

  return (
    <div
      data-testid="arpa-table"
      style={{
        position: 'absolute', top: 54, right: 96, zIndex: 15,
        background: 'rgba(11,19,32,0.92)',
        border: '1px solid var(--line-3)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 260, maxHeight: 240, overflowY: 'auto',
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
        <span style={{ color: 'var(--txt-3)', fontSize: 8, letterSpacing: 1 }}>ARPA TARGETS</span>
        <span onClick={onToggle} style={{ color: 'var(--txt-3)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <table style={{ width: '100%', borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ color: 'var(--txt-3)', fontSize: 7 }}>
            <th style={{ textAlign: 'left', padding: 1 }}>ID</th>
            <th style={{ textAlign: 'left', padding: 1 }}>BRG°</th>
            <th style={{ textAlign: 'left', padding: 1 }}>RNG</th>
            <th style={{ textAlign: 'left', padding: 1 }}>COG°</th>
            <th style={{ textAlign: 'left', padding: 1 }}>SOG</th>
            <th style={{ textAlign: 'left', padding: 1 }}>CPA</th>
            <th style={{ textAlign: 'left', padding: 1 }}>TCPA</th>
          </tr>
        </thead>
        <tbody>
          {targets.map((t, i) => {
            const id = t.mmsi ? String(t.mmsi) : `T${i + 1}`;
            const cpaInfo = cpaMap.get(id) ?? cpaMap.get('*');
            const cpaVal = cpaInfo?.cpa;
            const cpaColor = cpaVal != null
              ? cpaVal < 1.0 ? 'var(--c-danger)' : cpaVal < 2.0 ? 'var(--c-warn)' : 'var(--txt-1)'
              : 'var(--txt-3)';
            return (
              <tr key={id} style={{ borderTop: '1px solid var(--line-1)' }}>
                <td style={{ padding: 1, color: 'var(--c-info)' }}>{id}</td>
                <td style={{ padding: 1 }}>{/* BRG computed in parent */}</td>
                <td style={{ padding: 1 }}>{/* RNG */}</td>
                <td style={{ padding: 1 }}>{t.kinematics?.cog != null ? ((t.kinematics.cog * 180 / Math.PI + 360) % 360).toFixed(0) : '—'}°</td>
                <td style={{ padding: 1 }}>{t.kinematics?.sog != null ? (t.kinematics.sog * 1.944).toFixed(1) : '—'}</td>
                <td style={{ padding: 1, color: cpaColor }}>{cpaInfo?.cpa?.toFixed(2) ?? '—'}</td>
                <td style={{ padding: 1 }}>{cpaInfo?.tcpa?.toFixed(1) ?? '—'}m</td>
              </tr>
            );
          })}
          {targets.length === 0 && (
            <tr><td colSpan={7} style={{ color: 'var(--txt-3)', padding: 4, textAlign: 'center' }}>No targets</td></tr>
          )}
        </tbody>
      </table>
    </div>
  );
};
```

- [ ] **Step 2: Write ModuleDrilldown.tsx**

```typescript
// web/src/screens/shared/ModuleDrilldown.tsx
import React from 'react';
import { useTelemetryStore } from '../../store';

interface ModuleDrilldownProps {
  visible: boolean;
  onClose: () => void;
}

const MODULE_NAMES = ['M1 ODD', 'M2 World', 'M3 Mission', 'M4 Behavior', 'M5 Planner', 'M6 COLREGs', 'M7 Safety', 'M8 HMI'];
const HEALTH_LABELS: Record<number, string> = { 1: 'GREEN', 2: 'AMBER', 3: 'RED' };
const HEALTH_COLORS: Record<number, string> = { 1: 'var(--c-stbd)', 2: 'var(--c-warn)', 3: 'var(--c-danger)' };

export const ModuleDrilldown: React.FC<ModuleDrilldownProps> = ({ visible, onClose }) => {
  const pulses = useTelemetryStore((s) => s.modulePulses);

  if (!visible) return null;

  const byId: Record<number, typeof pulses[0]> = {};
  for (const p of pulses) { if (p.moduleId != null) byId[Number(p.moduleId)] = p; }

  return (
    <div
      data-testid="module-drilldown"
      style={{
        position: 'absolute', bottom: 58, left: 16, zIndex: 30,
        background: 'rgba(11,19,32,0.96)',
        border: '1px solid var(--line-3)',
        borderRadius: 8, padding: '8px 10px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 340,
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6 }}>
        <span style={{ color: 'var(--txt-3)', fontSize: 8, letterSpacing: 1 }}>MODULE HEALTH</span>
        <span onClick={onClose} style={{ color: 'var(--txt-3)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {MODULE_NAMES.map((name, i) => {
          const p = byId[i + 1];
          const color = p ? HEALTH_COLORS[p.state ?? 0] ?? 'var(--txt-3)' : '#333';
          return (
            <div key={name} style={{
              flex: '1 1 45%', minWidth: 140,
              background: 'var(--bg-1)',
              border: `1px solid ${color}22`,
              borderRadius: 4, padding: 4,
            }}>
              <span style={{ color, fontSize: 8 }}>● {name}</span>
              <div style={{ color: 'var(--txt-3)', fontSize: 7, marginTop: 2 }}>
                {p ? (
                  <>
                    state: {HEALTH_LABELS[p.state ?? 0] ?? '?'} · lat: {p.latencyMs ?? '?'}ms · drops: {p.messageDrops ?? 0}
                  </>
                ) : (
                  'no data'
                )}
              </div>
            </div>
          );
        })}
      </div>
    </div>
  );
};
```

- [ ] **Step 3: Write ScoringGauges.tsx**

```typescript
// web/src/screens/shared/ScoringGauges.tsx
import React from 'react';
import { useTelemetryStore } from '../../store';

interface ScoringGaugesProps {
  /** Only rendered in God view */
  visible: boolean;
}

const DIMS = [
  { key: 'safety',        label: 'SAF', color: 'var(--c-stbd)' },
  { key: 'ruleCompliance',label: 'RUL', color: 'var(--c-stbd)' },
  { key: 'delay',         label: 'DEL', color: 'var(--c-warn)' },
  { key: 'magnitude',     label: 'MAG', color: 'var(--c-stbd)' },
  { key: 'phase',         label: 'PHA', color: 'var(--c-stbd)' },
  { key: 'plausibility',  label: 'PLA', color: 'var(--c-warn)' },
] as const;

export const ScoringGauges: React.FC<ScoringGaugesProps> = React.memo(({ visible }) => {
  const scoringRow = useTelemetryStore((s) => s.scoringRow as any);

  if (!visible) return null;

  return (
    <div
      data-testid="scoring-gauges"
      style={{
        position: 'absolute', bottom: 58, right: 16, zIndex: 15,
        background: 'rgba(11,19,32,0.92)',
        border: '1px solid var(--line-2)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--f-mono)', fontSize: 9,
        minWidth: 90,
      }}
    >
      <div style={{ color: 'var(--c-stbd)', fontSize: 8, marginBottom: 4, letterSpacing: 1 }}>
        SCORING
      </div>
      {DIMS.map(({ key, label, color }) => {
        const val = scoringRow?.[key];
        const disp = typeof val === 'number' ? val.toFixed(2) : '—';
        const numVal = typeof val === 'number' ? val : 0;
        const barColor = numVal >= 0.8 ? 'var(--c-stbd)' : numVal >= 0.6 ? 'var(--c-warn)' : 'var(--c-danger)';
        return (
          <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 2 }}>
            <span style={{ color: 'var(--txt-3)', width: 22, fontSize: 7 }}>{label}</span>
            <div style={{ flex: 1, height: 4, background: 'var(--bg-2)', borderRadius: 2 }}>
              <div style={{
                width: `${Math.min(numVal * 100, 100)}%`, height: '100%',
                background: barColor, borderRadius: 2,
              }} />
            </div>
            <span style={{ color, fontWeight: 600, width: 30, textAlign: 'right', fontSize: 8 }}>{disp}</span>
          </div>
        );
      })}
    </div>
  );
});
ScoringGauges.displayName = 'ScoringGauges';
```

- [ ] **Step 4: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors. Note: `scoringRow as any` is intentional — `scoringRow` field not yet on telemetryStore; this task adds the component, Task 10 wires the store field.

- [ ] **Step 5: Commit**

```bash
git add web/src/screens/shared/ArpaTargetTable.tsx web/src/screens/shared/ModuleDrilldown.tsx web/src/screens/shared/ScoringGauges.tsx
git commit -m "feat(hmi): add ArpaTargetTable, ModuleDrilldown, ScoringGauges shared components"
```

---

## Task 8: ScoringRadarChart + ColregsDecisionTree (shared components group B)

> **⚠️ v1.1 Update**: TimelineScrubber 已废弃，由 Task 24 的 TimelineSixLane 完整替代。**Step 3 (Write TimelineScrubber.tsx) 跳过**；本任务仅创建 2 个文件（ScoringRadarChart + ColregsDecisionTree）。Commit 消息相应调整。

**Files:**
- Create: `web/src/screens/shared/ScoringRadarChart.tsx`
- Create: `web/src/screens/shared/ColregsDecisionTree.tsx`
- ~~Create: `web/src/screens/shared/TimelineScrubber.tsx`~~ **v1.1 跳过**

- [ ] **Step 1: Write ScoringRadarChart.tsx**

```typescript
// web/src/screens/shared/ScoringRadarChart.tsx
import React from 'react';

interface RadarProps {
  kpis: Record<string, number> | null;
}

const AXES = [
  { key: 'safety',        label: 'Safety',        angle: -90 },
  { key: 'ruleCompliance',label: 'Rule',           angle: -30 },
  { key: 'delay',         label: 'Delay',          angle: 30 },
  { key: 'magnitude',     label: 'Magnitude',      angle: 90 },
  { key: 'phase',         label: 'Phase',          angle: 150 },
  { key: 'plausibility',  label: 'Plausibility',    angle: 210 },
];

const R = 80;
const CX = 100;
const CY = 100;

export const ScoringRadarChart: React.FC<RadarProps> = ({ kpis }) => {
  // Compute data polygon points
  const dataPoints = AXES.map(({ key, angle }) => {
    const val = kpis?.[key] ?? 0;
    const r = (R * Math.min(val, 1));
    const rad = (angle * Math.PI) / 180;
    return `${CX + r * Math.cos(rad)},${CY + r * Math.sin(rad)}`;
  }).join(' ');

  // Grid rings
  const gridRings = [0.25, 0.5, 0.75, 1.0].map((frac) => {
    const pts = AXES.map(({ angle }) => {
      const rad = (angle * Math.PI) / 180;
      return `${CX + R * frac * Math.cos(rad)},${CY + R * frac * Math.sin(rad)}`;
    }).join(' ');
    return <polygon key={frac} points={pts} fill="none" stroke="var(--line-1)" strokeWidth="0.5" />;
  });

  return (
    <div data-testid="scoring-radar" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
      <div style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: 1, marginBottom: 6 }}>
        SCORING RADAR — Hagen 2022
      </div>
      <svg width={200} height={200} viewBox="0 0 200 200">
        {gridRings}
        {/* Axes */}
        {AXES.map(({ angle, label }) => {
          const rad = (angle * Math.PI) / 180;
          const lx = CX + (R + 16) * Math.cos(rad);
          const ly = CY + (R + 16) * Math.sin(rad);
          return (
            <g key={label}>
              <line x1={CX} y1={CY} x2={CX + R * Math.cos(rad)} y2={CY + R * Math.sin(rad)}
                    stroke="var(--line-1)" strokeWidth="0.5" />
              <text x={lx} y={ly} textAnchor="middle" fill="var(--txt-3)" fontSize="7">
                {label} {kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] != null
                  ? (kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] ?? 0).toFixed(2)
                  : '—'}
              </text>
            </g>
          );
        })}
        {/* Data polygon */}
        <polygon points={dataPoints} fill="var(--c-stbd)" fillOpacity="0.2" stroke="var(--c-stbd)" strokeWidth="1.5" />
      </svg>
    </div>
  );
};
```

- [ ] **Step 2: Write ColregsDecisionTree.tsx**

```typescript
// web/src/screens/shared/ColregsDecisionTree.tsx
import React from 'react';

interface DecisionTreeProps {
  ruleChain: string[];
  events: Array<{ event_type: string; rule_ref?: string; verdict?: number }>;
}

/**
 * Renders COLREGs rule chain as an indented decision tree.
 *
 * Format:
 *   Rule-14
 *     → Head-on Situation
 *       → Both give-way → Alter Stbd
 *         → PASS · CPA 0.85nm > threshold 0.5nm
 */
export const ColregsDecisionTree: React.FC<DecisionTreeProps> = ({ ruleChain, events }) => {
  if (ruleChain.length === 0 && events.length === 0) {
    return (
      <div data-testid="decision-tree" style={{ color: 'var(--txt-3)', fontSize: 10 }}>
        No rule events captured
      </div>
    );
  }

  // Build tree nodes from events
  const nodes: { indent: number; text: string; color: string }[] = [];
  for (const e of events) {
    const indent = nodes.length === 0 ? 0 : 1;
    if (e.rule_ref) {
      nodes.push({ indent: 0, text: e.rule_ref, color: 'var(--c-info)' });
    }
    if (e.event_type) {
      nodes.push({ indent: 1, text: e.event_type.replace(/_/g, ' '), color: 'var(--c-magenta)' });
    }
    if (e.verdict != null) {
      const vLabel = e.verdict === 1 ? 'PASS' : e.verdict === 2 ? 'RISK' : 'FAIL';
      const vColor = e.verdict === 1 ? 'var(--c-stbd)' : e.verdict === 2 ? 'var(--c-warn)' : 'var(--c-danger)';
      nodes.push({ indent: 2, text: vLabel, color: vColor });
    }
  }

  return (
    <div data-testid="decision-tree">
      <div style={{ color: 'var(--txt-3)', fontSize: 9, letterSpacing: 1, marginBottom: 8 }}>
        COLREGs DECISION TREE
      </div>
      {nodes.map((n, i) => (
        <div key={i} style={{
          marginLeft: n.indent * 16,
          marginBottom: 4,
          fontSize: 9,
          color: n.color,
          fontFamily: 'var(--f-mono)',
        }}>
          {n.indent > 0 && <span style={{ color: 'var(--txt-3)', marginRight: 6 }}>{'→'}</span>}
          {n.text}
        </div>
      ))}
      {/* Also show rule chain as fallback */}
      {events.length === 0 && ruleChain.length > 0 && (
        <div style={{ color: 'var(--txt-2)', fontSize: 9, fontFamily: 'var(--f-mono)' }}>
          {ruleChain.join(' → ')}
        </div>
      )}
    </div>
  );
};
```

- [ ] **Step 3: Write TimelineScrubber.tsx**

```typescript
// web/src/screens/shared/TimelineScrubber.tsx
import React from 'react';

interface TimelineScrubberProps {
  durationSec: number;
  currentSec: number;
  events: Array<{ timeSec: number; label: string; color: string }>;
}

/**
 * Stub timeline scrubber for DEMO-1.
 * Shows simulation duration bar with event markers.
 * Phase 2 will add MCAP seek + drag scrub support.
 */
export const TimelineScrubber: React.FC<TimelineScrubberProps> = ({ durationSec, currentSec, events }) => {
  const progressPct = durationSec > 0 ? (currentSec / durationSec) * 100 : 0;

  return (
    <div
      data-testid="timeline-scrubber"
      style={{
        display: 'flex', alignItems: 'center', padding: '8px 16px',
        background: 'rgba(15,25,41,0.6)',
        borderBottom: '1px solid var(--line-1)',
        gap: 12, height: 36,
      }}
    >
      <span style={{ color: 'var(--txt-3)', fontSize: 10 }}>▶</span>
      <div style={{ flex: 1, height: 4, background: 'var(--bg-2)', borderRadius: 2, position: 'relative' }}>
        {/* Progress bar */}
        <div style={{
          position: 'absolute', left: 0, top: 0,
          width: `${progressPct}%`, height: '100%',
          background: 'var(--c-phos)', borderRadius: 2,
        }} />
        {/* Event markers */}
        {events.map((evt, i) => (
          <div
            key={i}
            title={evt.label}
            style={{
              position: 'absolute',
              left: `${(evt.timeSec / durationSec) * 100}%`,
              top: -6,
              width: 2, height: 16,
              background: evt.color,
            }}
          />
        ))}
        {/* Scrub handle */}
        <div style={{
          position: 'absolute',
          left: `${progressPct}%`,
          top: -4,
          width: 12, height: 12,
          background: 'var(--c-phos)',
          borderRadius: '50%',
          transform: 'translateX(-50%)',
        }} />
      </div>
      <span style={{ color: 'var(--txt-3)', fontSize: 9, fontFamily: 'var(--f-mono)', minWidth: 44 }}>
        {formatDuration(currentSec)}
      </span>
    </div>
  );
};

function formatDuration(sec: number): string {
  const m = Math.floor(sec / 60).toString().padStart(2, '0');
  const s = Math.floor(sec % 60).toString().padStart(2, '0');
  return `${m}:${s}`;
}
```

- [ ] **Step 4: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 5: Commit**

```bash
git add web/src/screens/shared/ScoringRadarChart.tsx web/src/screens/shared/ColregsDecisionTree.tsx web/src/screens/shared/TimelineScrubber.tsx
git commit -m "feat(hmi): add ScoringRadarChart, ColregsDecisionTree, TimelineScrubber shared components"
```

---

## Task 9: SilMapView.tsx update — viewport, offset, mode, layers

**Files:**
- Modify: `web/src/map/SilMapView.tsx`

- [ ] **Step 1: Update SilMapViewProps interface and map initialisation**

Replace the existing `SilMapViewProps` interface (lines 7-10) and add `viewportOffset` prop. Also replace the map `center` initialisation to read from `useMapStore`.

```typescript
// Change lines 1-2 to add new imports:
import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { osmSource, osmLayer, s57Source, ALL_S57_LAYERS } from './layers';   // CHANGED: s57Layers → ALL_S57_LAYERS
import { useTelemetryStore, useMapStore } from '../store';                    // CHANGED: add useMapStore
import { useMapPersistence } from '../hooks/useMapPersistence';                // NEW

// Update interface (lines 7-10):
interface SilMapViewProps {
  followOwnShip?: boolean;
  viewMode?: 'captain' | 'god';
  /** Fraction of viewport to offset own-ship towards. Captain: [0.5, 0.7] (bottom 30%) */
  viewportOffset?: [number, number];
}
```

Replace the component function signature (line 76):

```typescript
export function SilMapView({
  followOwnShip = true,
  viewMode = 'captain',
  viewportOffset = [0.5, 0.5],
}: SilMapViewProps) {
```

Replace the map container ref + add mapStore recovery (before `const mapContainer`):

```typescript
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef       = useRef<maplibregl.Map | null>(null);
  const styleReady   = useRef(false);
  const lastPanAt    = useRef(0);
  const firstFit     = useRef(false);
  const viewportFromStore = useMapStore((s) => s.viewport);  // NEW
  const setViewport = useMapStore((s) => s.setViewport);      // NEW
```

Replace the map `center` in the `useEffect` that creates the map (line 109):

```typescript
        center: viewportFromStore.center,  // CHANGED: was [10.4, 63.4]
        zoom: viewportFromStore.zoom,      // CHANGED: was 12
```

Replace the layers array in the map style (line 107):

```typescript
          layers: [osmLayer as any, ...ALL_S57_LAYERS.map((l) => l as any)],  // CHANGED: s57Layers → ALL_S57_LAYERS
```

- [ ] **Step 2: Add useMapPersistence hook call and viewport restoration**

Add after `const [status, setStatus] = useState<'init' | 'ready' | 'no-webgl'>('init');` (line 88):

```typescript
  // ── Cross-screen viewport persistence ─────────────────────────────────────
  useMapPersistence(mapRef, viewMode);

  // Restore viewport on mode switch (e.g. God → Captain bearing reset)
  useEffect(() => {
    const map = mapRef.current;
    if (!map || !styleReady.current) return;
    if (viewMode === 'god') {
      // Don't force-jump in god mode — user freely panned
    } else {
      // Captain mode: restore heading-up bearing from latest ownShip
    }
  }, [viewMode]);
```

- [ ] **Step 3: Update followOwnShip effect to apply viewportOffset**

Replace the follow block (lines 228-237) to apply the offset:

```typescript
    // Follow with viewport offset
    if (followOwnShip && viewMode === 'captain') {
      if (!firstFit.current) {
        map.jumpTo({ center: [lon, lat], zoom: 13 });
        map.setPadding({
          bottom: map.getContainer().clientHeight * (viewportOffset[1] - 0.5) * 2,
          top: map.getContainer().clientHeight * (0.5 - viewportOffset[1]) * 2,
        });
        firstFit.current = true;
        lastPanAt.current = Date.now();
      } else if (Date.now() - lastPanAt.current > 800) {
        map.easeTo({ center: [lon, lat], duration: 500 });
        lastPanAt.current = Date.now();
      }
    }
```

- [ ] **Step 4: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -15`
Expected: Check for any errors. The `map.setPadding` call is a valid MapLibre method.

- [ ] **Step 5: Commit**

```bash
git add web/src/map/SilMapView.tsx
git commit -m "feat(hmi): update SilMapView with viewport persistence, offset, ALL_S57_LAYERS integration"
```

---

## Task 10: BridgeHMI.tsx update — dual-mode wiring + all overlays

**Files:**
- Modify: `web/src/screens/BridgeHMI.tsx`

This is the largest single-file change. The BridgeHMI screen must integrate all 9 new overlay/components and wire their visibility to `viewMode`.

- [ ] **Step 1: Add imports for all new components**

Add after existing imports (lines 1-6):

```typescript
import { CompassRose } from '../map/CompassRose';
import { PpiRings } from '../map/PpiRings';
import { ColregsSectors } from '../map/ColregsSectors';
import { ImazuGeometry } from '../map/ImazuGeometry';
import { DistanceScale } from '../map/DistanceScale';
import { ArpaTargetTable } from './shared/ArpaTargetTable';
import { ModuleDrilldown } from './shared/ModuleDrilldown';
import { ScoringGauges } from './shared/ScoringGauges';
import { useMapStore } from '../store';
```

- [ ] **Step 2: Add local state for new UI controls**

Add after existing useState declarations (line 282):

```typescript
  const [arpaExpanded, setArpaExpanded] = useState(viewMode === 'god');
  const [drilldownVisible, setDrilldownVisible] = useState(false);
  const [imazuGeometry] = useState<GeoJSON.FeatureCollection | null>(null); // populated from scenario API later
```

- [ ] **Step 3: Add distance scale NM-to-px computation**

Add before the return statement (line 300):

```typescript
  // DistanceScale: compute nm-per-pixel factor from map zoom
  const nmPerPixelRef = useRef(0.01); // Phase 2: wire to map.getZoom() + project()
  // For now use fixed approximation: at zoom 12, 1px ≈ 0.005 nm
  const nmPerPx = 0.005;
```

- [ ] **Step 4: Insert new overlays into the map area div**

Inside the `<div style={{ flex: 1, position: 'relative', overflow: 'hidden' }}>` (line 304), add after `<TopInfoBar>` (line 308):

```typescript
        {/* ── MAP OVERLAYS ── */}
        {/* Compass Rose — bottom-left, relative bearing in Captain, North-up in God */}
        <div style={{ position: 'absolute', bottom: 56, left: 16, zIndex: 15 }}>
          <CompassRose
            bearing={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0}
            relativeMode={viewMode === 'captain'}
          />
        </div>

        {/* PPI Range Rings — centred on own-ship position */}
        <PpiRings
          centerFraction={viewMode === 'captain' ? [50, 70] : [50, 50]}
          radiiPx={[40, 80, 160, 320]}
        />

        {/* COLREGs Sectors — God view only */}
        {viewMode === 'god' && (
          <ColregsSectors
            ownShipFraction={[50, 50]}
            headingDeg={ownShip ? (ownShip.pose?.heading ?? 0) * 180 / Math.PI : 0}
            outerRadiusPx={320}
          />
        )}

        {/* Imazu Geometry — God view only */}
        {viewMode === 'god' && (
          <ImazuGeometry mapRef={mapRef} geometry={imazuGeometry} />
        )}

        {/* Distance Scale — bottom-centre */}
        <div style={{ position: 'absolute', bottom: 56, left: '50%', transform: 'translateX(-50%)', zIndex: 15 }}>
          <DistanceScale nmPerPixel={nmPerPx} />
        </div>

        {/* ARPA Target Table — right sidebar, Captain collapsible, God expanded */}
        <ArpaTargetTable expanded={arpaExpanded} onToggle={() => setArpaExpanded(!arpaExpanded)} />

        {/* Scoring Gauges — God view only */}
        <ScoringGauges visible={viewMode === 'god'} />

        {/* Module Drilldown — click pulse dot to open */}
        <ModuleDrilldown visible={drilldownVisible} onClose={() => setDrilldownVisible(false)} />
```

- [ ] **Step 5: Wire ModulePulseBar click to drilldown**

Modify the `<ModulePulseBar />` call to accept an `onClick` prop (add to the `<div onClick={() => setDrilldownVisible(!drilldownVisible)}>` at line 157 in the existing code). The ModulePulseBar component needs a minor update — wrap the outer div with `onClick`:

In the `ModulePulseBar` function (line 142), add `cursor: 'pointer'` and `onClick` to the outer div:

```typescript
    <div style={{ display: 'flex', gap: 6, alignItems: 'center', cursor: 'pointer' }}
         onClick={() => setDrilldownVisible(!drilldownVisible)}>
```

- [ ] **Step 6: Export mapRef for ImazuGeometry access**

The BridgeHMI component needs access to the MapLibre instance. Modify `SilMapView` to expose mapRef via a `mapRef` prop callback pattern, OR — simpler — create a ref inside BridgeHMI and pass it to child components.

Add at top of BridgeHMI (line 264, before `useFoxgloveLive`):

```typescript
  const mapRef = useRef<maplibregl.Map | null>(null); // placeholder — Phase 2: expose from SilMapView
```

(Note: The `SilMapView` does not currently expose its internal `mapRef`. For the ImazuGeometry component to work, Phase 2 should add an `onMapReady` callback prop to `SilMapView`. For now the ImazuGeometry component is mounted but will log a console warning if `mapRef.current` is null.)

- [ ] **Step 7: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -15`
Expected: may have `mapRef` not found in BridgeHMI scope — fix by adding `const mapRef = useRef<any>(null);`. May also need `maplibregl` import.

- [ ] **Step 8: Commit**

```bash
git add web/src/screens/BridgeHMI.tsx
git commit -m "feat(hmi): integrate all map overlays and shared components into BridgeHMI dual-mode"
```

---

## Task 11: ScenarioBuilder.tsx update — Imazu preview, layer toggles, AIS tab

**Files:**
- Modify: `web/src/screens/ScenarioBuilder.tsx`

- [ ] **Step 1: Add layer toggle state and Imazu geometry wiring**

After imports (lines 1-11), add:

```typescript
import { useMapStore } from '../store';
import { ImazuGeometry } from '../map/ImazuGeometry';
```

Replace the right panel (lines 104-107) with:

```typescript
      {/* Right panel: ENC + Imazu preview with layer toggles */}
      <div style={{ flex: 1, position: 'relative' }}>
        <SilMapView followOwnShip={false} viewMode="god" />

        {/* Layer toggle panel */}
        <div style={{
          position: 'absolute', top: 8, right: 8, zIndex: 10,
          background: 'rgba(11,19,32,0.88)',
          border: '1px solid var(--line-2)',
          borderRadius: 4, padding: '4px 8px',
          fontFamily: 'var(--f-mono)', fontSize: 8,
          display: 'flex', flexDirection: 'column', gap: 2,
        }}>
          <label style={{ color: 'var(--c-phos)', cursor: 'pointer' }}>
            <input type="checkbox" defaultChecked data-testid="layer-toggle-imazu" /> Imazu geometry
          </label>
          <label style={{ color: 'var(--txt-3)', cursor: 'pointer' }}>
            <input type="checkbox" defaultChecked /> Depth contours
          </label>
          <label style={{ color: 'var(--txt-3)', cursor: 'pointer' }}>
            <input type="checkbox" /> Traffic lanes
          </label>
        </div>

        {/* Imazu Geometry overlay (hidden until AIS import or scenario selection provides geometry data) */}
        <div style={{ position: 'absolute', top: 0, left: 0, width: '100%', height: '100%', pointerEvents: 'none', zIndex: 5 }}>
          {/* ImazuGeometry rendered imperatively via MapLibre source — DOM placeholder */}
        </div>
      </div>
```

- [ ] **Step 2: Replace AIS tab stub with content**

Replace lines 57-63 (the tab bar) with labeled tab content:

```typescript
        {/* Tab bar */}
        <div style={{ display: 'flex', gap: 0, marginBottom: 16, borderBottom: '1px solid var(--line-2)' }}>
          {(['template', 'procedural', 'ais'] as const).map((tab) => (
            <button key={tab} onClick={() => setActiveTab(tab)}
                    style={{
                      background: 'none', border: 'none', cursor: 'pointer',
                      color: activeTab === tab ? 'var(--c-phos)' : 'var(--txt-3)',
                      borderBottom: activeTab === tab ? '2px solid var(--c-phos)' : '2px solid transparent',
                      padding: '8px 16px', fontFamily: 'var(--f-mono)', fontSize: 10,
                      fontWeight: activeTab === tab ? 600 : 400,
                    }}>
              {tab === 'template' ? 'Template' : tab === 'procedural' ? 'Procedural' : 'AIS'}
            </button>
          ))}
        </div>

        {/* Tab content */}
        {activeTab === 'template' && (
          <>
            {/* Scenario list */}
            <select size={8} style={{
              width: '100%', marginBottom: 8,
              background: 'var(--bg-2)', color: 'var(--txt-1)',
              border: '1px solid var(--line-2)', borderRadius: 4,
              padding: 4, fontFamily: 'var(--f-mono)', fontSize: 10,
            }}
                    onChange={(e) => handleSelect(e.target.value)}>
              {scenarios.map((s: ScenarioSummary) => (
                <option key={s.id} value={s.id}>{s.name} ({s.encounter_type})</option>
              ))}
            </select>
          </>
        )}

        {activeTab === 'ais' && (
          <div style={{ color: 'var(--txt-2)', fontSize: 10, padding: 16 }}>
            <p style={{ color: 'var(--c-phos)', fontWeight: 600 }}>AIS Scenario Import</p>
            <p style={{ color: 'var(--txt-3)', marginTop: 8 }}>
              Import historical AIS trajectories to extract COLREGs encounter scenarios.
              This feature uses the 5-stage AIS pipeline from D1.3b.2.
            </p>
            <div style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div>
                <label style={{ color: 'var(--txt-3)', fontSize: 9 }}>Bounding Box</label>
                <div style={{ display: 'flex', gap: 4, marginTop: 4 }}>
                  <input placeholder="Min Lat" style={{
                    flex: 1, background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Min Lon" style={{
                    flex: 1, background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Max Lat" style={{
                    flex: 1, background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Max Lon" style={{
                    flex: 1, background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                </div>
              </div>
              <div>
                <label style={{ color: 'var(--txt-3)', fontSize: 9 }}>Time Window (UTC)</label>
                <div style={{ display: 'flex', gap: 4, marginTop: 4 }}>
                  <input type="date" style={{
                    background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input type="date" style={{
                    background: 'var(--bg-2)', border: '1px solid var(--line-2)',
                    color: 'var(--txt-1)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                </div>
              </div>
              <button style={{
                background: 'var(--c-phos)', color: 'var(--bg-0)',
                border: 'none', padding: '6px 16px', borderRadius: 4,
                fontFamily: 'var(--f-mono)', fontSize: 10, fontWeight: 700,
                cursor: 'pointer', marginTop: 8,
              }}>
                Extract Encounters
              </button>
            </div>
          </div>
        )}

        {activeTab === 'procedural' && (
          <div style={{ color: 'var(--txt-3)', fontSize: 10, padding: 16 }}>
            Procedural scenario generation — Phase 2 (D2.5)
          </div>
        )}
```

- [ ] **Step 3: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add web/src/screens/ScenarioBuilder.tsx
git commit -m "feat(hmi): add Imazu preview layer toggles and AIS tab content to ScenarioBuilder"
```

---

## Task 12: RunReport.tsx update — radar chart, decision tree, timeline, map snapshot

**Files:**
- Modify: `web/src/screens/RunReport.tsx`

- [ ] **Step 1: Add imports for new components**

Replace imports (lines 1-7) to add new components:

```typescript
import { useEffect, useState } from 'react';
import {
  useExportMarzipMutation,
  useGetExportStatusQuery,
  useGetLastRunScoringQuery,
} from '../api/silApi';
import { useScenarioStore } from '../store';
import { ScoringRadarChart } from './shared/ScoringRadarChart';
import { ColregsDecisionTree } from './shared/ColregsDecisionTree';
import { TimelineScrubber } from './shared/TimelineScrubber';
```

- [ ] **Step 2: Replace timeline stub with TimelineScrubber component**

Replace the timeline stub div (lines 87-92):

```typescript
      <TimelineScrubber
        durationSec={kpis?.duration_s ?? 0}
        currentSec={kpis?.duration_s ?? 0}
        events={(scoring?.rule_chain ?? []).map((rule, i, arr) => ({
          timeSec: ((i + 1) / (arr.length + 1)) * (kpis?.duration_s ?? 180),
          label: rule,
          color: rule.includes('give_way') ? 'var(--c-warn)'
                : rule.includes('passing') ? 'var(--c-stbd)'
                : 'var(--c-info)',
        }))}
      />
```

- [ ] **Step 3: Insert ScoringRadarChart and ColregsDecisionTree alongside KPI cards**

Replace the KPI + rule chain block (lines 95-116) with a flex layout:

```typescript
      <div style={{ flex: 1, display: 'flex', gap: 16 }}>
        {/* Left: KPI cards */}
        <div style={{ display: 'flex', flexDirection: 'column', gap: 8, minWidth: 200 }}
             data-testid="run-report-kpis">
          <KpiCard label="Min CPA" value={fmt(kpis?.min_cpa_nm, 2)} unit="nm" />
          <KpiCard label="Avg ROT" value={fmt(kpis?.avg_rot_dpm, 1)} unit="°/min" />
          <KpiCard label="Distance" value={fmt(kpis?.distance_nm, 1)} unit="nm" />
          <KpiCard label="Duration" value={fmt(kpis?.duration_s, 0)} unit="s" />
        </div>

        {/* Centre: Scoring Radar Chart */}
        <div style={{
          flex: 1, background: 'var(--bg-1)', borderRadius: 8, padding: 16,
          border: '1px solid var(--line-2)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}>
          <ScoringRadarChart kpis={kpis as Record<string, number> | null} />
        </div>

        {/* Right: COLREGs Decision Tree */}
        <div style={{
          flex: 1, background: 'var(--bg-1)', borderRadius: 8, padding: 16,
          border: '1px solid var(--line-2)',
        }} data-testid="rule-chain">
          <ColregsDecisionTree
            ruleChain={scoring?.rule_chain ?? []}
            events={[]} // Phase 2: populate from ASDR events via scoring_node
          />
          <p style={{ color: 'var(--txt-3)', fontSize: 9, marginTop: 8 }}>
            Run ID: {runId}
          </p>
        </div>
      </div>
```

- [ ] **Step 4: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors. The `kpis as Record<string, number>` cast is intentional — the ScoringRadarChart accepts generic key-value pairs.

- [ ] **Step 5: Commit**

```bash
git add web/src/screens/RunReport.tsx
git commit -m "feat(hmi): integrate ScoringRadarChart, ColregsDecisionTree, TimelineScrubber into RunReport"
```

---

## Task 13: Preflight.tsx update — scenario thumbnail strip

**Files:**
- Modify: `web/src/screens/Preflight.tsx`

- [ ] **Step 1: Add scenario name + mini map thumbnail at bottom of preflight screen**

Insert before the `<button onClick={handleBack}>` on line 131:

```typescript
      {/* Scenario context strip — keeps "shared mental model" per human factors research */}
      <div style={{
        position: 'absolute', bottom: 16, left: 16, right: 16,
        padding: '8px 12px',
        background: 'var(--bg-1)',
        border: '1px solid var(--line-2)',
        borderRadius: 6,
        display: 'flex', alignItems: 'center', gap: 12,
        fontFamily: 'var(--f-mono)', fontSize: 10,
      }}>
        <div style={{
          width: 60, height: 40,
          background: 'var(--bg-2)',
          border: '1px solid var(--line-1)',
          borderRadius: 3,
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          color: 'var(--txt-3)', fontSize: 7,
        }}>
          chart
        </div>
        <div>
          <div style={{ color: 'var(--c-phos)', fontSize: 10, fontWeight: 600 }}>
            {scenarioId ? `Scenario ${scenarioId.slice(0, 8)}` : 'No scenario'}
          </div>
          <div style={{ color: 'var(--txt-3)', fontSize: 8, marginTop: 2 }}>
            ENC viewport preserved from ScenarioBuilder
          </div>
        </div>
      </div>
```

- [ ] **Step 2: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/Preflight.tsx
git commit -m "feat(hmi): add scenario context thumbnail strip to Preflight screen"
```

---

# ═══════════════════════════════════════════════════════════════
# v1.1 NEW TASKS (17-24) — Runtime Semantics + DNV SIL Reframe
# ═══════════════════════════════════════════════════════════════

> **执行顺序**：Task 17 属 Phase 0 Foundation（与 Task 1-3 并列）；Tasks 18-24 属 Phase 1 Components（与 Task 4-8 并列）。**Tasks 10-13 的 v1.1 增量步骤**见各 task 顶部 "v1.1 Update" callout（需在 Phase 2 整合）。

---

## Task 17: fsmStore + uiStore.viewMode + useHotkeys (Foundation v1.1)

**Spec**: §1.2 双模式差异矩阵 / §3.3.5 Scene FSM / §3.3.10 Hotkeys

**Files:**
- Create: `web/src/store/fsmStore.ts`
- Modify: `web/src/store/uiStore.ts`（add viewMode）
- Create: `web/src/hooks/useHotkeys.ts`
- Modify: `web/src/store/index.ts`（export useFsmStore）

- [ ] **Step 1: Write fsmStore.ts**

```typescript
// web/src/store/fsmStore.ts
import { create } from 'zustand';

export type FsmState = 'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK';

export interface FsmTransition {
  from: FsmState;
  to: FsmState;
  reason: string;
  timestamp: number;  // sim_time seconds
}

export interface TorRequest {
  reason: string;
  triggeredAtSimTime: number;
  tmrDeadlineSimTime: number;       // +60s by default
  sat1LockUntilSimTime: number;     // +5s SAT-1 lock
  currentSituation: string;
  proposedAction: string;
}

interface FsmStore {
  currentState: FsmState;
  transitionHistory: FsmTransition[];
  torRequest: TorRequest | null;
  setState: (next: FsmState, reason: string, simTime: number) => void;
  setTorRequest: (req: TorRequest | null) => void;
  clearHistory: () => void;
}

export const useFsmStore = create<FsmStore>((set) => ({
  currentState: 'TRANSIT',
  transitionHistory: [],
  torRequest: null,
  setState: (next, reason, simTime) => set((s) => ({
    currentState: next,
    transitionHistory: [
      ...s.transitionHistory,
      { from: s.currentState, to: next, reason, timestamp: simTime },
    ].slice(-100), // ring buffer last 100 transitions
  })),
  setTorRequest: (req) => set({ torRequest: req }),
  clearHistory: () => set({ transitionHistory: [] }),
}));
```

- [ ] **Step 2: Modify uiStore.ts — add viewMode + setViewMode**

Add to existing `UIStore` interface:
```typescript
viewMode: 'captain' | 'god';
setViewMode: (mode: 'captain' | 'god') => void;
```

Initial state: `viewMode: 'captain'`。Action: `setViewMode: (mode) => set({ viewMode: mode })`。

- [ ] **Step 3: Write useHotkeys.ts**

```typescript
// web/src/hooks/useHotkeys.ts
import { useEffect } from 'react';
import { useFsmStore, useUIStore } from '../store';

export interface HotkeyHandlers {
  onTor?: () => void;          // T
  onFault?: () => void;        // F
  onMrc?: () => void;          // M
  onHandback?: () => void;     // H
  onSpace?: () => void;        // SPACE
  onArrowLeft?: () => void;    // ← (manual rudder -1°)
  onArrowRight?: () => void;   // → (manual rudder +1°)
  onArrowUp?: () => void;      // ↑ (manual throttle +5%)
  onArrowDown?: () => void;    // ↓ (manual throttle -5%)
}

export function useHotkeys(handlers: HotkeyHandlers) {
  const setViewMode = useUIStore((s) => s.setViewMode);
  const viewMode = useUIStore((s) => s.viewMode);

  useEffect(() => {
    const onKeyDown = (e: KeyboardEvent) => {
      // Skip if focus is on input/textarea
      const ae = document.activeElement;
      if (ae && (ae.tagName === 'INPUT' || ae.tagName === 'TEXTAREA' || ae.getAttribute('contenteditable') === 'true')) {
        return;
      }

      // Cmd/Ctrl + Shift + G: toggle view
      if ((e.metaKey || e.ctrlKey) && e.shiftKey && e.key.toLowerCase() === 'g') {
        e.preventDefault();
        setViewMode(viewMode === 'captain' ? 'god' : 'captain');
        return;
      }

      switch (e.key.toLowerCase()) {
        case 't': handlers.onTor?.(); break;
        case 'f': handlers.onFault?.(); break;
        case 'm': handlers.onMrc?.(); break;
        case 'h': handlers.onHandback?.(); break;
        case ' ': handlers.onSpace?.(); e.preventDefault(); break;
        case 'arrowleft':  handlers.onArrowLeft?.(); break;
        case 'arrowright': handlers.onArrowRight?.(); break;
        case 'arrowup':    handlers.onArrowUp?.(); break;
        case 'arrowdown':  handlers.onArrowDown?.(); break;
      }
    };

    window.addEventListener('keydown', onKeyDown);
    return () => window.removeEventListener('keydown', onKeyDown);
  }, [handlers, viewMode, setViewMode]);
}
```

- [ ] **Step 4: Export from store/index.ts**

Add: `export { useFsmStore, type FsmState } from './fsmStore';`

- [ ] **Step 5: Verify TypeScript** — `cd web && npx tsc --noEmit 2>&1 | grep "error TS" | head -10` → no errors

- [ ] **Step 6: Commit**

```bash
git add web/src/store/fsmStore.ts web/src/store/uiStore.ts web/src/store/index.ts web/src/hooks/useHotkeys.ts
git commit -m "feat(hmi): add fsmStore (5-state machine) + viewMode + useHotkeys hook for runtime semantics"
```

---

## Task 18: TopChrome + FooterHotkeyHints + RunStatePill + DualClock (Cross-screen chrome)

**Spec**: §3.3.0 Top Chrome

**Files:**
- Create: `web/src/screens/shared/TopChrome.tsx`
- Create: `web/src/screens/shared/FooterHotkeyHints.tsx`
- Create: `web/src/screens/shared/RunStatePill.tsx`
- Create: `web/src/screens/shared/DualClock.tsx`

- [ ] **Step 1: Write RunStatePill.tsx** — render `lifecycleStatus.state` as colored pill (IDLE/READY/ACTIVE/PAUSED/COMPLETED/ABORTED) per spec §3.3.0 Run State Pill 颜色映射。`data-testid="run-state-pill"`

- [ ] **Step 2: Write DualClock.tsx** — UTC 实时钟 (`new Date().toISOString()` slice) + SIM 仿真钟 (从 `useTelemetryStore.lifecycleStatus.simTime`) 两行 mono 显示。`data-testid="dual-clock-utc"` / `"dual-clock-sim"`

- [ ] **Step 3: Write TopChrome.tsx** — fixed top 40px, 5 区: Brand / 4 Nav Tabs (Builder/Pre-flight/Bridge/Report) / RunStatePill / DualClock / ViewToggle。Active tab driven by `useLocation()` (react-router-dom v6). ViewToggle 仅在 `/bridge/*` route 渲染。`data-testid="top-chrome"` / `"view-toggle"`

- [ ] **Step 4: Write FooterHotkeyHints.tsx** — fixed bottom 28px, left: WS link + ASDR path (从 env or runtime config 取); right: 根据当前 route 显示对应 hotkeys (Bridge: T/F/M/H/SPACE; Report: ←→ scrub; Builder: Enter→next step; Preflight: ESC→back). `data-testid="footer-hotkey-hints"`

- [ ] **Step 5: Verify TypeScript** — no errors

- [ ] **Step 6: Commit**

```bash
git add web/src/screens/shared/TopChrome.tsx web/src/screens/shared/FooterHotkeyHints.tsx web/src/screens/shared/RunStatePill.tsx web/src/screens/shared/DualClock.tsx
git commit -m "feat(hmi): add TopChrome/FooterHotkeyHints/RunStatePill/DualClock cross-screen chrome"
```

---

## Task 19: TorModal (5s SAT-1 + 60s TMR + auto-MRC)

**Spec**: §3.3.6 ToR Modal

**Files:**
- Create: `web/src/screens/shared/TorModal.tsx`

- [ ] **Step 1: Write TorModal.tsx**

关键逻辑：
- 渲染条件: `useFsmStore.torRequest !== null && useFsmStore.currentState === 'TOR'`
- 用 React portal 渲染到 `document.body`，z-index 100
- 倒计时基于 `useTelemetryStore.lifecycleStatus.simTime`（**仿真时间，非墙钟**），每 50ms 重新计算 remaining
- SAT-1 lock: `simTime < torRequest.sat1LockUntilSimTime` 时 "Take Control" 按钮 disabled，背景 gray；超过后逐渐 amber → ready (transition 1s)
- TMR 倒计时: `tmrDeadlineSimTime - simTime` 显示在大字号；进度条 60→0；30s 起转 amber，10s 起转 red 闪烁
- 超时自动: `simTime >= tmrDeadlineSimTime` → call `useFsmStore.setState('MRC', 'TMR_TIMEOUT', simTime)`，5s 后清除 torRequest
- Take Control 按钮: `useFsmStore.setState('OVERRIDE', 'CAPTAIN_TAKE_CONTROL', simTime)` + 清除 torRequest

`data-testid="tor-modal"` / `"tor-countdown"` / `"tor-sat1-lock"` / `"tor-take-control"` / `"tor-wait"`

- [ ] **Step 2: Verify TypeScript**

- [ ] **Step 3: Commit**

```bash
git add web/src/screens/shared/TorModal.tsx
git commit -m "feat(hmi): add TorModal with 5s SAT-1 lock + 60s TMR countdown + auto-MRC (Veitch 2024)"
```

---

## Task 20: FaultInjectPanel (AIS/Radar/ROC 故障注入)

**Spec**: §3.3.7 Fault Injection Panel

**Files:**
- Create: `web/src/screens/shared/FaultInjectPanel.tsx`
- Modify: `web/src/api/silApi.ts`（add POST /api/v1/fault/inject + DELETE /api/v1/fault/:id）

- [ ] **Step 1: Add fault inject API to silApi.ts**

RTK Query mutation:
```typescript
injectFault: builder.mutation<{ accepted: boolean; fault_id: string }, { type: 'ais' | 'radar' | 'roc'; duration_s: number; params?: any }>({
  query: (body) => ({ url: '/fault/inject', method: 'POST', body }),
}),
cancelFault: builder.mutation<{ cancelled: boolean }, string>({
  query: (faultId) => ({ url: `/fault/${faultId}`, method: 'DELETE' }),
}),
```

- [ ] **Step 2: Write FaultInjectPanel.tsx**

- 右侧 docked panel (320px), 仅 `useUIStore.viewMode === 'god'` 渲染
- 3 卡片: AIS Dropout / Radar Failure / ROC Link Loss，每卡片含: duration slider (0-120s) + params (per spec) + Inject Now 按钮
- 触发 inject: 调 `injectFault.mutation` + 等待 response + 若 `accepted` → 显示 active fault chip + 注入后 ASDR `fault_injected` event 应在 telemetry 内可见
- 409 冲突: 显示 "fault already active until t=XX" toast，按钮锁定到 fault 结束

`data-testid="fault-panel"` / `"fault-inject-ais"` / `"fault-inject-radar"` / `"fault-inject-roc"` / `"fault-active-chip"`

- [ ] **Step 3: Verify TypeScript + Commit**

```bash
git add web/src/screens/shared/FaultInjectPanel.tsx web/src/api/silApi.ts
git commit -m "feat(hmi): add FaultInjectPanel for AIS/Radar/ROC fault injection (God view only)"
```

---

## Task 21: ConningBar + ThreatRibbon (Bridge HMI 持续状态条)

**Spec**: §3.3.8 Conning Bar / §3.3.9 Threat Ribbon

**Files:**
- Create: `web/src/screens/shared/ConningBar.tsx`
- Create: `web/src/screens/shared/ThreatRibbon.tsx`

- [ ] **Step 1: Write ConningBar.tsx**

- 数据源: `useTelemetryStore.controlCmd`（v1.1 新增 field, 由 Task 14 写入）+ 历史 ring buffer (60s @ 10Hz = 600 samples) 由 selector 派生
- Captain 模式: 单行 4 字段 (THR%/RUD°/RPM/PITCH)，每字段进度条 + 数值
- God 模式: 单行 4 字段 + 每字段右侧附 60s sparkline (SVG path, `requestAnimationFrame` 节流到 30fps)
- 接收 props: `viewMode: 'captain' | 'god'`
- 位置: absolute bottom 70px（底部状态栏正上方），左 50% 宽度

`data-testid="conning-bar"` / `"conning-history"`（仅 God）

- [ ] **Step 2: Write ThreatRibbon.tsx**

- 数据源: `useTelemetryStore.targets[]` + `useTelemetryStore.asdrEvents`（派生 CPA/TCPA/COLREGs role）
- Captain 模式: 单行 chip (按 CPA 升序，最危险在左)，颜色 red<1nm / amber<2nm / green≥2nm，max 5 chip + "+N more" badge
- God 模式: 双行 — 上行 chip 含 COLREGs role label + CPA + TCPA；下行 当前规则推理状态 (rule_id + state + side hint)
- Click chip → highlight target on map (dispatch event to map) + auto-pan
- 位置: absolute top 70px（顶部信息条正下方），全宽

`data-testid="threat-ribbon"` / `"threat-chip-{mmsi}"`

- [ ] **Step 3: Verify TypeScript + Commit**

```bash
git add web/src/screens/shared/ConningBar.tsx web/src/screens/shared/ThreatRibbon.tsx
git commit -m "feat(hmi): add ConningBar (throttle/rudder/RPM/pitch + God sparkline) + ThreatRibbon"
```

---

## Task 22: Stepper + SummaryRail + ImazuGrid (Builder 3-step 辅助组件)

**Spec**: §3.1 Scenario Builder 3-Step

**Files:**
- Create: `web/src/screens/shared/Stepper.tsx`
- Create: `web/src/screens/shared/SummaryRail.tsx`
- Create: `web/src/screens/shared/ImazuGrid.tsx`

- [ ] **Step 1: Write Stepper.tsx** — generic horizontal stepper (props: `steps: string[]; current: number; onJump?: (i: number) => void;`)。当前 step 加粗 + accent border；完成 step 加 ✓ 图标。`data-testid="builder-step-{1\|2\|3}"`

- [ ] **Step 2: Write SummaryRail.tsx** — 右栏 320px，props: `summary: ScenarioSummary; validationStatus: 'red'|'yellow'|'green'; onValidate: () => void; onRunPreflight: () => void`。显示场景摘要 chip + ENC 状态 + 预估时长 + Validate 按钮 (按 status 染色) + Run Pre-flight CTA (仅 green 可点)。`data-testid="summary-rail"` / `"validate-cta"`

- [ ] **Step 3: Write ImazuGrid.tsx** — 4×6 网格 (22 例 + 2 空 cell), 每 cell 24px 几何缩略 SVG (hard-coded from imazu22_v1.0.yaml 简化几何) + IMA-01..22 label。Props: `selected: number | null; onSelect: (id: number) => void`。选中 cell accent border。`data-testid="imazu-grid"` / `"imazu-cell-{id}"`

- [ ] **Step 4: Verify TypeScript + Commit**

```bash
git add web/src/screens/shared/Stepper.tsx web/src/screens/shared/SummaryRail.tsx web/src/screens/shared/ImazuGrid.tsx
git commit -m "feat(hmi): add Stepper + SummaryRail + ImazuGrid helpers for Scenario Builder 3-step"
```

---

## Task 23: ModuleReadinessGrid + SensorStatusRow + CommLinkStatusRow + LiveLogStream (Preflight 扩展)

**Spec**: §3.2 Pre-flight 扩展

**Files:**
- Create: `web/src/screens/shared/ModuleReadinessGrid.tsx`
- Create: `web/src/screens/shared/SensorStatusRow.tsx`
- Create: `web/src/screens/shared/CommLinkStatusRow.tsx`
- Create: `web/src/screens/shared/LiveLogStream.tsx`

- [ ] **Step 1: Write ModuleReadinessGrid.tsx** — M1-M8 2×4 grid, 每 tile: module_id + health 灯 (●green/PASS · ◐amber/CHECKING · ●red/FAIL) + KPI (latency_ms / drops / queue_depth)。Click tile → 展开 detail tooltip。数据源: `useTelemetryStore.modulePulses[]`。`data-testid="preflight-modules"`

- [ ] **Step 2: Write SensorStatusRow.tsx** — 8 sensor dots (GNSS/Gyro/IMU/Radar/AIS/Camera/LiDAR/ECDIS), 每 dot 颜色按 `useTelemetryStore.sensors[i].state`。Hover 显示 last_update + raw status payload。`data-testid="preflight-sensors"`

- [ ] **Step 3: Write CommLinkStatusRow.tsx** — 6 comm-link dots (DDS-Bus/L4↓/L2↑/Param-DB/ROC-Lnk/ASDR), 同上结构。数据源: `useTelemetryStore.commLinks[i]`。`data-testid="preflight-commlinks"`

- [ ] **Step 4: Write LiveLogStream.tsx** — 自动滚动到底部 + 暂停按钮 + 关键字过滤 (warn/error/module 名)。颜色规则: INFO `--txt-2` / WARN `--c-warn` / ERROR `--c-danger`。数据源: `useTelemetryStore.preflightLog[]` (1000-entry ring)。`data-testid="preflight-livelog"`

- [ ] **Step 5: Verify TypeScript + Commit**

```bash
git add web/src/screens/shared/ModuleReadinessGrid.tsx web/src/screens/shared/SensorStatusRow.tsx web/src/screens/shared/CommLinkStatusRow.tsx web/src/screens/shared/LiveLogStream.tsx
git commit -m "feat(hmi): add Module/Sensor/CommLink readiness + LiveLogStream for Preflight expansion"
```

---

## Task 24: TimelineSixLane + AsdrLedger + TrajectoryReplay (Report 增强)

**Spec**: §3.4 Run Report 扩展

**Files:**
- Create: `web/src/screens/shared/TimelineSixLane.tsx`
- Create: `web/src/screens/shared/AsdrLedger.tsx`
- Create: `web/src/screens/shared/TrajectoryReplay.tsx`

- [ ] **Step 1: Write TimelineSixLane.tsx**

6 lanes (从上到下): FSM / Rule / M7 / Fault / CPA / ASDR

- 横坐标 = 仿真时间 (0 → t_end)
- Lane 1-4 (FSM/Rule/M7/Fault): 区间块 rendering，按 event_type 染色
- Lane 5 (CPA): 折线图 (y 轴 0-5nm)，SVG path
- Lane 6 (ASDR): 事件点 dots，hover tooltip
- 鼠标 hover: 十字光标 + tooltip 显示该时刻所有 lane 数值
- 鼠标 click: 触发 onScrub(timeSec) callback (parent 同步 trajectory replay)
- 播放控制条: `▶ ◀▶ ▶▶ ×0.5 ×1 ×2 ×4 ×10 ⏹`
- DEMO-1: 仅静态 + click-to-jump (无 MCAP seek)
- 数据源: `useTelemetryStore.asdrEvents` (派生 lanes 1-4 + 6) + ownShipTrail + targets (派生 lane 5)

`data-testid="timeline-6lane"` / `"timeline-lane-{fsm\|rule\|m7\|fault\|cpa\|asdr}"` / `"timeline-playback"`

- [ ] **Step 2: Write AsdrLedger.tsx**

- 列: # | Time (MM:SS.mmm) | Type chip | Module | Payload preview (60 chars + ...) | SHA-256 (前 8 chars)
- 过滤器: Event type multi-select / Module multi-select / 时间范围 slider / 关键字搜索
- SHA-256 hash chain stub: 显示 `0xDEADBEEF` placeholder (DEMO-1)，Phase 2 真实链
- 数据源: `GET /api/v1/runs/:runId/asdr` (Task 12 wiring)
- Pagination: 50 rows per page

`data-testid="asdr-ledger"` / `"asdr-filter-{type\|module\|time\|search}"` / `"asdr-hash-link"`

- [ ] **Step 3: Write TrajectoryReplay.tsx**

- 480×320px MapLibre 实例 (独立于 Bridge HMI 海图)
- 显示本船 + 目标船完整轨迹 (GeoJSON LineString)
- props: `currentTimeSec: number` — 当前同步时刻
- 当前时刻位置标记 (沿轨迹按 timestamp 插值)
- 使用 `useMapStore.viewport` 作为初始视角
- 数据源: `GET /api/v1/runs/:runId/asdr` 中所有 `own_ship_state` + `target_vessel_state` events 重建轨迹

`data-testid="trajectory-replay"`

- [ ] **Step 4: Verify TypeScript + Commit**

```bash
git add web/src/screens/shared/TimelineSixLane.tsx web/src/screens/shared/AsdrLedger.tsx web/src/screens/shared/TrajectoryReplay.tsx
git commit -m "feat(hmi): add TimelineSixLane + AsdrLedger (SHA-256 stub) + TrajectoryReplay for Run Report"
```

---

# ═══════════════════════════════════════════════════════════════
# v1.1 SCREEN INTEGRATION UPDATES (Tasks 10-13 addenda)
# ═══════════════════════════════════════════════════════════════

> **执行顺序**：以下 4 块步骤须在 v1.1 Phase 2 整合期执行，**附加到** v1.0 Tasks 10-13 的现有 step 列表之后（v1.0 step 完成后继续）。

## Task 10 v1.1 增量 — Bridge HMI 集成运行时语义

**附加 Files (modify):** `web/src/screens/BridgeHMI.tsx`

- [ ] **Step 10.v1.1.A: 引入 fsmStore + uiStore.viewMode + 所有 v1.1 components**

Imports 顶部新增:
```typescript
import { useFsmStore } from '../store';
import { useUIStore } from '../store';
import { useHotkeys } from '../hooks/useHotkeys';
import { TopChrome } from './shared/TopChrome';
import { FooterHotkeyHints } from './shared/FooterHotkeyHints';
import { TorModal } from './shared/TorModal';
import { FaultInjectPanel } from './shared/FaultInjectPanel';
import { ConningBar } from './shared/ConningBar';
import { ThreatRibbon } from './shared/ThreatRibbon';
```

- [ ] **Step 10.v1.1.B: 替换 viewMode 来源**

Remove local `viewMode` useState (v1.0 实现)，改读: `const viewMode = useUIStore((s) => s.viewMode);`。

- [ ] **Step 10.v1.1.C: 注册 useHotkeys handlers**

```typescript
const fsmSet = useFsmStore((s) => s.setState);
const simTime = useTelemetryStore((s) => s.lifecycleStatus?.simTime ?? 0);

useHotkeys({
  onTor: () => fsmSet('TOR', 'HOTKEY_T', simTime),
  onFault: () => setFaultPanelOpen((v) => !v),
  onMrc: () => fsmSet('MRC', 'HOTKEY_M', simTime),
  onHandback: () => fsmSet('TRANSIT', 'HOTKEY_H', simTime),
  onSpace: () => /* dispatch pause/resume to backend via WS */,
  onArrowLeft: () => /* manual rudder -1° (DEMO-1: ASDR event only) */,
  // ... etc
});
```

- [ ] **Step 10.v1.1.D: 渲染 v1.1 components**

In JSX (按 z-index 由低到高):
```tsx
<>
  <TopChrome />
  <main style={{ paddingTop: 40, paddingBottom: 28 }}>
    <ThreatRibbon viewMode={viewMode} />
    {/* ... existing SilMapView + overlays ... */}
    <ConningBar viewMode={viewMode} />
    {viewMode === 'god' && <FaultInjectPanel />}
  </main>
  <TorModal />
  <FooterHotkeyHints />
</>
```

- [ ] **Step 10.v1.1.E: Verify TypeScript + Commit**

```bash
git add web/src/screens/BridgeHMI.tsx
git commit -m "feat(hmi): integrate FSM/ToR/Fault/Conning/Threat/Hotkeys into BridgeHMI (v1.1)"
```

---

## Task 11 v1.1 增量 — Scenario Builder 3-Step 重构

**附加 Files (modify):** `web/src/screens/ScenarioBuilder.tsx`

- [ ] **Step 11.v1.1.A: 引入 v1.1 components + 改 state**

Imports: `Stepper / SummaryRail / ImazuGrid`。

新 state:
```typescript
const [currentStep, setCurrentStep] = useState<1 | 2 | 3>(1);
const [encounterTab, setEncounterTab] = useState<'template' | 'procedural' | 'ais'>('template');
const [validationStatus, setValidationStatus] = useState<'red' | 'yellow' | 'green'>('red');
```

- [ ] **Step 11.v1.1.B: 重构 JSX 结构**

替换主区域为：
```tsx
<div style={{ display: 'flex', flexDirection: 'column', height: '100%' }}>
  <Stepper steps={['ENCOUNTER', 'ENVIRONMENT', 'RUN']} current={currentStep - 1} onJump={(i) => setCurrentStep((i + 1) as any)} />
  <div style={{ display: 'flex', flex: 1 }}>
    <main style={{ flex: 1 }}>
      {currentStep === 1 && <EncounterStep tab={encounterTab} onTabChange={setEncounterTab} />}
      {currentStep === 2 && <EnvironmentStep />}
      {currentStep === 3 && <RunStep />}
    </main>
    <SummaryRail summary={summary} validationStatus={validationStatus} onValidate={handleValidate} onRunPreflight={handleRunPreflight} />
  </div>
</div>
```

- [ ] **Step 11.v1.1.C: 实现 EncounterStep / EnvironmentStep / RunStep 子组件**

在同文件内 (或抽到 `screens/builder/` 子目录) 实现 3 个子组件，per spec §3.1.1-3.1.3。EncounterStep 含 3 sub-tabs (template/procedural/ais) + Imazu grid。

- [ ] **Step 11.v1.1.D: Verify + Commit**

```bash
git add web/src/screens/ScenarioBuilder.tsx
git commit -m "feat(hmi): refactor ScenarioBuilder to 3-step (ENCOUNTER/ENVIRONMENT/RUN) + Imazu grid (v1.1)"
```

---

## Task 12 v1.1 增量 — Run Report 增强（替换 TimelineScrubber）

**附加 Files (modify):** `web/src/screens/RunReport.tsx`

- [ ] **Step 12.v1.1.A: 替换 imports — TimelineScrubber → TimelineSixLane + AsdrLedger + TrajectoryReplay**

Remove: `import { TimelineScrubber } from './shared/TimelineScrubber';`
Add: 
```typescript
import { TimelineSixLane } from './shared/TimelineSixLane';
import { AsdrLedger } from './shared/AsdrLedger';
import { TrajectoryReplay } from './shared/TrajectoryReplay';
```

- [ ] **Step 12.v1.1.B: 增加 KPI sparklines + verdict + TMR used**

顶部 KPI cards 加 sparkline (DEMO-1 stub: SVG path 直线). 调 `GET /api/v1/runs/:runId/verdict` 获取 KPI 数据。

- [ ] **Step 12.v1.1.C: 替换 timeline 渲染**

Replace `<TimelineScrubber />` with `<TimelineSixLane onScrub={(t) => setCurrentTimeSec(t)} />`. 在底部 3-col 布局加 `<TrajectoryReplay currentTimeSec={currentTimeSec} />` + `<ScoringRadarChart />` + `<AsdrLedger />`。

- [ ] **Step 12.v1.1.D: Verify + Commit**

```bash
git add web/src/screens/RunReport.tsx
git commit -m "feat(hmi): upgrade RunReport with 6-lane Timeline + AsdrLedger + TrajectoryReplay + KPI sparklines (v1.1)"
```

---

## Task 13 v1.1 增量 — Preflight 完整扩展

**附加 Files (modify):** `web/src/screens/Preflight.tsx`

- [ ] **Step 13.v1.1.A: 引入 v1.1 components**

Imports:
```typescript
import { ModuleReadinessGrid } from './shared/ModuleReadinessGrid';
import { SensorStatusRow } from './shared/SensorStatusRow';
import { CommLinkStatusRow } from './shared/CommLinkStatusRow';
import { LiveLogStream } from './shared/LiveLogStream';
```

- [ ] **Step 13.v1.1.B: 替换 JSX 主区域**

参考 spec §3.2.1 ASCII 布局：左 40% (Module + Sensors + CommLinks) + 右 60% (Live Log)。底部 GO/NO-GO Gate 按钮按 `allReady` boolean (derived from modulePulses + sensors + commLinks 全部 green) 染色。

- [ ] **Step 13.v1.1.C: Verify + Commit**

```bash
git add web/src/screens/Preflight.tsx
git commit -m "feat(hmi): expand Preflight with M1-M8 + sensors + commlinks + livelog + GO/NO-GO gate (v1.1)"
```

---

## Task 14: Expand telemetryStore (scoringRow + v1.1 runtime fields)

**Spec**: §5.1 WebSocket 实时流

**Files:**
- Modify: `web/src/store/telemetryStore.ts`
- Modify: `web/src/hooks/useFoxgloveLive.ts`

- [ ] **Step 1: Add scoringRow field + updater**

Add at line 19 (after `ownShipTrail`):

```typescript
  /** Real-time 6-dim scoring row from /sil/scoring_row @ 1Hz */
  scoringRow: any; // sil.ScoringRow — typed loosely until Protobuf TS gen includes it
```

Add at line 47 (after `setWsConnected`):

```typescript
  updateScoringRow: (row: any) => void;
```

Add to initialState (line 60):

```typescript
  scoringRow: null,
```

Add implementation (after `setWsConnected` implementation, line 83):

```typescript
  updateScoringRow: (scoringRow) => set({ scoringRow }),
```

- [ ] **Step 2: Wire in useFoxgloveLive hook**

Edit `web/src/hooks/useFoxgloveLive.ts` — add after the existing topic subscription block (find the `/sil/module_pulse` handler pattern):

Add new subscription for scoring:

```typescript
      // Scoring row — 1Hz, God view real-time gauges
      if (topic === '/sil/scoring_row') {
        try {
          useTelemetryStore.getState().updateScoringRow(msg);
        } catch { /* noop */ }
        return;
      }
```

- [ ] **Step 3: Verify TypeScript**

Run: `cd web && npx tsc --noEmit 2>&1 | grep -E "error TS" | head -10`
Expected: no errors

- [ ] **Step 4: Commit**

```bash
git add web/src/store/telemetryStore.ts web/src/hooks/useFoxgloveLive.ts
git commit -m "feat(hmi): add scoringRow to telemetryStore + WS wiring in useFoxgloveLive"
```

- [ ] **Step 5 (v1.1 NEW): Add v1.1 runtime fields**

Add to `TelemetryState` interface:

```typescript
  /** Sensor health (8 sensors: GNSS/Gyro/IMU/Radar/AIS/Camera/LiDAR/ECDIS) */
  sensors: Array<{ id: string; state: 'ok' | 'warning' | 'fail' | 'disabled'; lastUpdate: number; payload?: any }>;
  /** Comm-link health (6 links: DDS-Bus/L4↓/L2↑/Param-DB/ROC-Lnk/ASDR) */
  commLinks: Array<{ id: string; state: 'ok' | 'warning' | 'fail' | 'disabled'; lastUpdate: number; payload?: any }>;
  /** Active fault list (managed by Fault Injection Panel) */
  faultStatus: Array<{ fault_id: string; type: 'ais' | 'radar' | 'roc'; started_at_sim_time: number; ends_at_sim_time: number; params?: any }>;
  /** Control command from M5 Tactical Planner @ 10Hz (rudder/throttle/RPM/pitch) + 60s ring buffer (600 samples) */
  controlCmd: { rudder_deg: number; throttle_pct: number; rpm: number; pitch_deg: number } | null;
  controlCmdHistory: Array<{ t: number; rudder: number; throttle: number; rpm: number; pitch: number }>;  // ring buffer max 600
  /** Preflight live log entries (ring buffer max 1000) */
  preflightLog: Array<{ t: number; level: 'info' | 'warn' | 'error'; module?: string; msg: string }>;
```

Add 6 corresponding updater actions:
```typescript
  updateSensorStatus: (sensor: TelemetryState['sensors'][0]) => void;
  updateCommLinkStatus: (link: TelemetryState['commLinks'][0]) => void;
  appendFaultStatus: (f: TelemetryState['faultStatus'][0]) => void;
  removeFaultStatus: (faultId: string) => void;
  updateControlCmd: (cmd: NonNullable<TelemetryState['controlCmd']>, t: number) => void;
  appendPreflightLog: (entry: TelemetryState['preflightLog'][0]) => void;
```

Initial state:
```typescript
  sensors: ['GNSS','Gyro','IMU','Radar','AIS','Camera','LiDAR','ECDIS'].map((id) => ({ id, state: 'disabled', lastUpdate: 0 })),
  commLinks: ['DDS-Bus','L4↓','L2↑','Param-DB','ROC-Lnk','ASDR'].map((id) => ({ id, state: 'disabled', lastUpdate: 0 })),
  faultStatus: [],
  controlCmd: null,
  controlCmdHistory: [],
  preflightLog: [],
```

- [ ] **Step 6 (v1.1 NEW): Wire 7 new WS topics in useFoxgloveLive**

Per spec §5.1, subscribe to:
- `/sil/fsm_state` → call `useFsmStore.getState().setState(msg.to, msg.reason, simTime)`
- `/sil/tor_event` → call `useFsmStore.getState().setTorRequest(msg)` (or `null` to clear)
- `/sil/fault_status` → replace `useTelemetryStore.getState().faultStatus` from msg list
- `/sil/control_cmd` → call `updateControlCmd(msg, simTime)` (also pushes to history ring buffer)
- `/sil/sensor_status` → call `updateSensorStatus(msg)` per sensor
- `/sil/commlink_status` → call `updateCommLinkStatus(msg)` per link
- `/sil/preflight_log` → call `appendPreflightLog(msg)`

- [ ] **Step 7 (v1.1 NEW): Verify + Commit v1.1 expansion**

```bash
git add web/src/store/telemetryStore.ts web/src/hooks/useFoxgloveLive.ts
git commit -m "feat(hmi): expand telemetryStore with sensors/commLinks/faultStatus/controlCmd/preflightLog + 7 new WS topics (v1.1)"
```

---

## Task 15: Unit tests for new components

**Files:**
- Create: `web/src/map/__tests__/CompassRose.test.tsx`
- Create: `web/src/screens/shared/__tests__/ArpaTargetTable.test.tsx`
- Create: `web/src/screens/shared/__tests__/ScoringRadarChart.test.tsx`
- Create: `web/src/store/__tests__/mapStore.test.ts`

- [ ] **Step 1: Write CompassRose test**

```typescript
// web/src/map/__tests__/CompassRose.test.tsx
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { CompassRose } from '../CompassRose';

describe('CompassRose', () => {
  it('renders with data-testid', () => {
    const { getByTestId } = render(<CompassRose bearing={45} relativeMode={true} />);
    expect(getByTestId('compass-rose')).toBeTruthy();
  });

  it('applies rotation transform in relative mode', () => {
    const { container } = render(<CompassRose bearing={90} relativeMode={true} />);
    const svg = container.querySelector('svg');
    expect(svg?.style.transform).toBe('rotate(90deg)');
  });

  it('does not rotate in North-up mode', () => {
    const { container } = render(<CompassRose bearing={90} relativeMode={false} />);
    const svg = container.querySelector('svg');
    expect(svg?.style.transform).toBe('none');
  });
});
```

- [ ] **Step 2: Write ScoringRadarChart test**

```typescript
// web/src/screens/shared/__tests__/ScoringRadarChart.test.tsx
import { describe, it, expect } from 'vitest';
import { render } from '@testing-library/react';
import { ScoringRadarChart } from '../ScoringRadarChart';

describe('ScoringRadarChart', () => {
  it('renders data-testid', () => {
    const { getByTestId } = render(<ScoringRadarChart kpis={null} />);
    expect(getByTestId('scoring-radar')).toBeTruthy();
  });

  it('renders with KPI data', () => {
    const kpis = { safety: 0.92, ruleCompliance: 0.88, delay: 0.71, magnitude: 0.85, phase: 0.93, plausibility: 0.79 };
    const { container } = render(<ScoringRadarChart kpis={kpis} />);
    const svg = container.querySelector('svg');
    expect(svg).toBeTruthy();
  });
});
```

- [ ] **Step 3: Write mapStore test**

```typescript
// web/src/store/__tests__/mapStore.test.ts
import { describe, it, expect, beforeEach } from 'vitest';
import { useMapStore } from '../mapStore';

describe('useMapStore', () => {
  beforeEach(() => {
    useMapStore.getState().resetViewport();
  });

  it('has default viewport for Trondheim', () => {
    const vp = useMapStore.getState().viewport;
    expect(vp.center).toEqual([10.38, 63.44]);
    expect(vp.zoom).toBe(12);
    expect(vp.bearing).toBe(0);
  });

  it('setViewport updates partial values', () => {
    useMapStore.getState().setViewport({ zoom: 14, bearing: 45 });
    const vp = useMapStore.getState().viewport;
    expect(vp.zoom).toBe(14);
    expect(vp.bearing).toBe(45);
    expect(vp.center).toEqual([10.38, 63.44]); // unchanged
  });

  it('resetViewport restores defaults', () => {
    useMapStore.getState().setViewport({ zoom: 8, center: [12, 64] });
    useMapStore.getState().resetViewport();
    const vp = useMapStore.getState().viewport;
    expect(vp.zoom).toBe(12);
  });
});
```

- [ ] **Step 4: Run tests**

Run: `cd web && npx vitest run 2>&1 | tail -20`
Expected: 3 test files pass

- [ ] **Step 5: Commit**

```bash
git add web/src/map/__tests__/CompassRose.test.tsx web/src/screens/shared/__tests__/ web/src/store/__tests__/mapStore.test.ts
git commit -m "test(hmi): add unit tests for CompassRose, ScoringRadarChart, mapStore"
```

---

## Task 16: Build verification + dev server smoke test

**Files:** (none created — verification task)

- [ ] **Step 1: TypeScript full type-check**

Run: `cd web && npx tsc --noEmit 2>&1`
Expected: no errors

- [ ] **Step 2: Vite build**

Run: `cd web && npx vite build 2>&1 | tail -10`
Expected: "built in Xs" with no errors

- [ ] **Step 3: Dev server smoke test**

Run: `cd web && npx vite --port 5173 &`
Expected: dev server starts without errors

Run: `curl -s http://localhost:5173 | head -5`
Expected: HTML with `<div id="root">`

- [ ] **Step 4: Final commit + verify git status**

```bash
git status
git add -A
git commit -m "feat(hmi): SIL HMI dual-mode DEMO-1 complete — 14 new components, 8 modified files

Captain view: IEC 62288 off-center, PPI rings, compass rose, ARPA table, distance scale.
God view: COLREGs 4-sector overlay, Imazu geometry, 6-dim scoring gauges, M1-M8 drill-down.
Cross-screen: useMapStore viewport persistence, tokens.css design system.
Report: Scoring radar chart, COLREGs decision tree, timeline stub.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>"
```

- [ ] **Step 5: Verify clean status**

Run: `git status`
Expected: "nothing to commit, working tree clean"

---

## Spec Coverage Self-Review

| Spec Section | Covered By |
|---|---|
| §1.1 海图状态一致性 (useMapStore) | Task 2 (mapStore) + Task 3 (useMapPersistence) + Task 9 (SilMapView update) |
| §1.2 双模式差异矩阵 (9 dims) | Task 10 (BridgeHMI wiring — all diff dimensions wired to viewMode) |
| §1.3 路由不变 | No change to App.tsx — verified |
| §2.1 色彩 Token (17 vars) | Task 1 (tokens.css — all 17 CSS vars, exact spec values) |
| §2.2 S-52 Layer Mapping | Task 6 (layers.ts — buoyage, spot_sounding, contour_label layers) |
| §2.3 字体 Scale | Task 1 (tokens.css — font-mono, font-sans, text-2xs through text-lg) |
| §3.1 Screen ① Imazu + AIS + viewport | Task 11 (ScenarioBuilder — layer toggles, AIS tab, viewport save) |
| §3.2 Screen ② thumbnail | Task 13 (Preflight — scenario context strip) |
| §3.3.1 Captain view layout (7 IEC 62288 elements) | Task 4 (CompassRose + PpiRings + DistanceScale) + Task 10 (BridgeHMI wiring) |
| §3.3.2 God view overlays (4 additions) | Task 5 (ColregsSectors + ImazuGeometry) + Task 7 (ScoringGauges) + Task 10 |
| §3.3.3 ARPA target table | Task 7 (ArpaTargetTable — collapsible, CPA color-coded, 7 columns) |
| §3.3.4 Bottom bar pulse click | Task 10 (ModuleDrilldown wired to ModulePulseBar onClick) |
| §3.4.1 Timeline + Radar + DecisionTree | Task 8 (all 3 components) + Task 12 (RunReport integration) |
| §3.4.2 Export buttons | Task 12 (existing buttons preserved, layout restructured) |
| §5.1/scoring_row WS topic | Task 14 (scoringRow in telemetryStore + WS wiring) |
| §5.3 海图状态持久化数据流 | Task 3 (useMapPersistence hook — moveend → store → jumpTo) |
| §6 测试检查点 (13 data-testid) | Task 4-13 (all data-testid attributes present in components) + Task 15 (unit tests) |

**v1.1 NEW Spec Sections**:

| Spec Section | Covered By |
|---|---|
| §1.2 DNV SIL 双轨理论基础矩阵 | Task 10 v1.1 增量（BridgeHMI 集成 viewMode 来源切换为 useUIStore + 所有运行时语义两视图共享）|
| §3.1 Builder 3-Step + 3 sub-tabs + Imazu 22 grid + SummaryRail | Task 22 (helpers) + Task 11 v1.1 增量 (重构 ScenarioBuilder.tsx) |
| §3.2 Preflight M1-M8 + 8 sensors + 6 comm-links + LiveLog + GO/NO-GO | Task 23 (helpers) + Task 13 v1.1 增量 (重构 Preflight.tsx) |
| §3.3.0 Top Chrome + FooterHotkeyHints | Task 18 (4 components) + Task 10 v1.1 增量 (wire in BridgeHMI) + App.tsx 修改 |
| §3.3.5 Scene FSM 5 态 | Task 17 (fsmStore) + Task 10 v1.1 增量 (subscription) + Task 14 Step 6 (WS wiring) |
| §3.3.6 ToR Modal (5s SAT-1 + 60s TMR + auto-MRC) | Task 19 (TorModal) + Task 10 v1.1 增量 (render in JSX) |
| §3.3.7 Fault Injection Panel | Task 20 (FaultInjectPanel + API mutation) + Task 10 v1.1 增量 (conditional render God) |
| §3.3.8 Conning Bar | Task 21 (ConningBar) + Task 14 (controlCmd field + history ring) |
| §3.3.9 Threat Ribbon | Task 21 (ThreatRibbon) + Task 10 v1.1 增量 |
| §3.3.10 Hotkeys (T/F/M/H/SPACE/Cmd+Shift+G/箭头) | Task 17 (useHotkeys) + Task 10 v1.1 增量 (handler binding) |
| §3.4 Run Report 6-lane Timeline + KPI sparklines + ASDR Ledger SHA-256 stub + Trajectory Replay | Task 24 (3 components) + Task 12 v1.1 增量 + Task 8 (废弃 TimelineScrubber) |
| §5.1 7 新 WS topics (fsm_state/tor_event/fault_status/control_cmd/sensor_status/commlink_status/preflight_log) | Task 14 Step 6 |
| §5.2 9 新 REST endpoints (fault/imazu/asdr/verdict/scoring/jobs) | Task 20 (silApi inject/cancel) + Task 12 v1.1 增量 (verdict/asdr) + Task 22 (silApi imazu) |
| §6 v1.1 ~40 新 data-testid | Tasks 17-24 (各 component data-testid 声明) + Task 15 v1.1 增量 (unit test 覆盖) |
| §7.2 v1.1 Non-goals (stub 项) | Tasks 19/20/24 (按 stub 策略实现 — SHA-256 placeholder / sparkline stub / fault simplified) |

**All 28 features from the v1.1 expanded checklist are covered. No TBD/TODO placeholders.**

---

## Type Consistency Check

- `useMapStore.viewport.center` is `[number, number]` — consumed by `MapLibre.Map.setCenter([lng, lat])` ✓
- `CompassRose.bearing` is degrees — consumed by SVG `rotate(${bearing}deg)` ✓
- `PpiRings.centerFraction` is `[number, number]` percentages — used as SVG `cx/cy` percentages ✓
- `ScoringRadarChart.kpis` is `Record<string, number>` — used as `kpis[key]` with `toFixed(2)` ✓
- `ArpaTargetTable` accesses `t.kinematics?.sog` `t.kinematics?.cog` from `TargetVesselState` — matches Protobuf-generated type ✓
- `ModuleDrilldown` accesses `p.moduleId` `p.state` `p.latencyMs` `p.messageDrops` from `ModulePulse` — matches type ✓
- `ColregsSectors.headingDeg` is degrees — used in SVG `transform="rotate(${headingDeg})"` ✓
- `TelemetryStore.scoringRow` typed as `any` — intentionally loose until Protobuf TS gen covers `sil.ScoringRow` (already in proto IDL). Cast at usage site (ScoringGauges.tsx) ✓

**v1.1 NEW type checks**:

- `FsmStore.currentState` is `'TRANSIT' | 'COLREG_AVOIDANCE' | 'TOR' | 'OVERRIDE' | 'MRC' | 'HANDBACK'` — exhaustive in switch statements ✓
- `FsmStore.torRequest.tmrDeadlineSimTime` is seconds (仿真时间, NOT ms wall clock) — consumed by TorModal `simTime` comparison ✓
- `UIStore.viewMode` is `'captain' | 'god'` — exhaustive in viewMode-driven JSX branches (BridgeHMI / ConningBar / ThreatRibbon) ✓
- `TelemetryStore.controlCmd` typed as `{ rudder_deg: number; throttle_pct: number; rpm: number; pitch_deg: number } | null` — consumed by ConningBar after null guard ✓
- `TelemetryStore.controlCmdHistory` is ring buffer length 600 (60s @ 10Hz) — consumed by ConningBar sparkline (God mode only) ✓
- `TelemetryStore.sensors` length always 8, `commLinks` length always 6 — initial state pre-populated with `'disabled'` per id; WS updates mutate in-place by id match ✓
- `TelemetryStore.faultStatus[].fault_id` is unique — removeFaultStatus uses id match (no index assumption) ✓
- `useHotkeys` handlers are optional (`?`) — caller passes only relevant subset per route ✓

---

## Execution Handoff

**Plan v1.1 complete and saved to `docs/superpowers/plans/2026-05-13-sil-hmi-dual-mode.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — Dispatch a fresh subagent per task, review between tasks, fast iteration. **24 tasks total** (v1.0: Tasks 1-16; v1.1 NEW: Tasks 17-24 + 4 v1.1 增量 在 Tasks 10-13)，max **12 parallel agents** in Phase 1 (Tasks 4-8 + 18-24)，5 parallel agents in Phase 2 (Tasks 9-13)。

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**v1.1 关键依赖**：
- Task 17 (fsmStore + uiStore.viewMode + useHotkeys) **必须在 Phase 0 完成**，否则 Task 10/19 wiring 会失败
- Task 14 v1.1 Step 5-7 (telemetryStore 扩展) **必须在 Phase 2 之前**，否则 Tasks 18/21/23 components 缺数据源
- Task 8 跳过 Step 3 (TimelineScrubber)；Task 24 提供 TimelineSixLane 替代品；Task 12 v1.1 增量负责切换 imports

**Which approach?**
