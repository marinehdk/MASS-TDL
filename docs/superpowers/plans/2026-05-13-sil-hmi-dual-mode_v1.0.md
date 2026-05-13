# SIL HMI Dual-Mode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Transform the 4-screen SIL HMI into a dual-mode (Captain ↔ God) algorithm verification hub with IEC 62288 bridge fidelity, cross-screen ENC chart persistence, and 17 new features across all screens.

**Architecture:** Foundation tasks (tokens, mapStore, hook) unblock 4 parallel tracks: map overlays (CompassRose/PpiRings/ColregsSectors/ImazuGeometry/DistanceScale), shared components (ArpaTargetTable/ModuleDrilldown/ScoringGauges/ScoringRadarChart/ColregsDecisionTree/TimelineScrubber), layers update, and screen modifications (SilMapView/BridgeHMI/ScenarioBuilder/Preflight/RunReport). All components use Zustand stores (telemetryStore + new mapStore) for 50Hz-capable selective re-renders.

**Tech Stack:** React 18, TypeScript, Zustand, RTK Query, MapLibre GL JS, Protobuf-ts types

**Spec:** `docs/superpowers/specs/2026-05-13-sil-hmi-dual-mode-design.md`

---

## Parallelisation Map

```
Phase 0 — Foundation (sequential, blocks all)
  Task 1: tokens.css + main.tsx import
  Task 2: mapStore.ts + store/index.ts export
  Task 3: useMapPersistence.ts hook
    │
    ├── Phase 1 — Map overlays + shared components (parallel, after 1-3)
    │     Task 4: CompassRose.tsx + PpiRings.tsx + DistanceScale.tsx
    │     Task 5: ColregsSectors.tsx + ImazuGeometry.tsx
    │     Task 6: layers.ts update (S-57 MVT + new GeoJSON sources)
    │     Task 7: ArpaTargetTable.tsx + ModuleDrilldown.tsx + ScoringGauges.tsx
    │     Task 8: ScoringRadarChart.tsx + ColregsDecisionTree.tsx + TimelineScrubber.tsx
    │
    └── Phase 2 — Screen integration (parallel, after Phase 1)
          Task 9:  SilMapView.tsx update (viewport, offset, mode props + layers.ts wiring)
          Task 10: BridgeHMI.tsx update (dual-mode wiring, all overlays, new components)
          Task 11: ScenarioBuilder.tsx update (Imazu preview, layer toggles, AIS tab)
          Task 12: RunReport.tsx update (radar chart, decision tree, timeline, map snapshot)
          Task 13: Preflight.tsx update (scenario thumbnail strip)

Phase 3 — Verification
  Task 14: Unit tests for new components + store
  Task 15: Playwright E2E smoke test
  Task 16: Commit + verify build
```

**Max parallelism:** 5 subagents in Phase 1 (Tasks 4-8), 5 subagents in Phase 2 (Tasks 9-13).

---

## File Structure

### Files created (14 new)

```
web/src/
├── styles/tokens.css
├── store/mapStore.ts
├── hooks/useMapPersistence.ts
├── map/
│   ├── CompassRose.tsx
│   ├── PpiRings.tsx
│   ├── ColregsSectors.tsx
│   ├── ImazuGeometry.tsx
│   └── DistanceScale.tsx
└── screens/shared/
    ├── ArpaTargetTable.tsx
    ├── ModuleDrilldown.tsx
    ├── ScoringGauges.tsx
    ├── ScoringRadarChart.tsx
    ├── ColregsDecisionTree.tsx
    └── TimelineScrubber.tsx
```

### Files modified (10 existing)

```
web/src/
├── main.tsx                        — import tokens.css
├── store/index.ts                  — export useMapStore
├── map/SilMapView.tsx              — accept viewport, offset, mode props; wire moveend→mapStore
├── map/layers.ts                   — add S-57 MVT sources, Imazu geometry, COLREGs sectors sources
├── screens/BridgeHMI.tsx           — integrate all new overlays + shared components
├── screens/ScenarioBuilder.tsx     — Imazu geometry on preview map, layer toggles, AIS tab
├── screens/Preflight.tsx           — scenario thumbnail at bottom
├── screens/RunReport.tsx           — radar chart, decision tree, timeline, map snapshot
```

---

## Task 1: Design tokens CSS + main.tsx import

**Files:**
- Create: `web/src/styles/tokens.css`
- Modify: `web/src/main.tsx:1-6`

- [ ] **Step 1: Create tokens.css with all design tokens**

```css
/* web/src/styles/tokens.css */

:root {
  /* ── Status colors (OpenBridge 5.0 alert-alarm/warning/caution, night-adapted) ── */
  --status-green:        #34d399;
  --status-amber:        #fbbf24;
  --status-red:          #f87171;

  /* ── Accent palette ── */
  --accent-primary:      #2dd4bf;
  --accent-info:         #60a5fa;
  --accent-decision:     #c084fc;
  --accent-score:        #a3e635;
  --accent-windsea:      #93c5fd;

  /* ── Surface hierarchy (S-52 Night black background) ── */
  --surface-root:        #0b1320;
  --surface-panel:       #0f1929;
  --surface-bar:         #060e1a;
  --surface-input:       #0d1f2d;

  /* ── Border hierarchy ── */
  --border-subtle:       rgba(45, 212, 191, 0.08);
  --border-default:      rgba(45, 212, 191, 0.12);
  --border-active:       rgba(45, 212, 191, 0.22);
  --border-alert:        #dc2626;

  /* ── Text hierarchy ── */
  --text-primary:        #e6edf3;
  --text-secondary:      #9ca3af;
  --text-dimmed:         #4b6888;
  --text-inverse:        #0b1320;

  /* ── Typography ── */
  --font-mono:           'JetBrains Mono', 'Courier New', monospace;
  --font-sans:           'Inter', system-ui, -apple-system, sans-serif;

  /* ── Spacing scale (4px base) ── */
  --space-1: 4px;
  --space-2: 8px;
  --space-3: 12px;
  --space-4: 16px;
  --space-5: 20px;
  --space-6: 24px;
}

/* Global reset helpers */
html, body, #root {
  margin: 0;
  padding: 0;
  height: 100%;
  background: var(--surface-root);
  color: var(--text-primary);
  font-family: var(--font-mono);
}
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
        border: '1px solid var(--border-default)',
        borderRadius: '50%',
        display: 'flex', alignItems: 'center', justifyContent: 'center',
      }}
    >
      <svg width={72} height={72} viewBox="0 0 72 72"
           style={{ transform: relativeMode ? `rotate(${bearing}deg)` : 'none' }}>
        {/* Outer ring */}
        <circle cx={cx} cy={cy} r={radius} fill="none" stroke="var(--border-subtle)" strokeWidth="0.5" />
        {/* Cardinal ticks */}
        <line x1={cx} y1={cy - radius + 4} x2={cx} y2={cy - radius + 12} stroke="var(--status-red)" strokeWidth="1.5" />
        <line x1={cx} y1={cy + radius - 4} x2={cx} y2={cy + radius - 12} stroke="var(--text-dimmed)" strokeWidth="0.5" />
        <line x1={cx - radius + 4} y1={cy} x2={cx - radius + 12} y2={cy} stroke="var(--text-dimmed)" strokeWidth="0.5" />
        <line x1={cx + radius - 4} y1={cy} x2={cx + radius - 12} y2={cy} stroke="var(--text-dimmed)" strokeWidth="0.5" />
        {/* Center dot */}
        <circle cx={cx} cy={cy} r={2} fill="var(--accent-primary)" />
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
          stroke="var(--accent-primary)"
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
      <div style={{ width: pxWidth, height: 2, background: 'var(--text-secondary)' }} />
      <div style={{ fontSize: 8, color: 'var(--text-dimmed)', fontFamily: 'var(--font-mono)' }}>
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
          'text-color': 'var(--accent-primary)',
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
          background: 'var(--surface-panel)',
          border: '1px solid var(--border-default)',
          color: 'var(--text-dimmed)',
          padding: '4px 10px', borderRadius: 3,
          fontFamily: 'var(--font-mono)', fontSize: 10, cursor: 'pointer',
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
        border: '1px solid var(--border-active)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--font-mono)', fontSize: 9,
        minWidth: 260, maxHeight: 240, overflowY: 'auto',
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 4 }}>
        <span style={{ color: 'var(--text-dimmed)', fontSize: 8, letterSpacing: 1 }}>ARPA TARGETS</span>
        <span onClick={onToggle} style={{ color: 'var(--text-dimmed)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <table style={{ width: '100%', borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ color: 'var(--text-dimmed)', fontSize: 7 }}>
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
              ? cpaVal < 1.0 ? 'var(--status-red)' : cpaVal < 2.0 ? 'var(--status-amber)' : 'var(--text-primary)'
              : 'var(--text-dimmed)';
            return (
              <tr key={id} style={{ borderTop: '1px solid var(--border-subtle)' }}>
                <td style={{ padding: 1, color: 'var(--accent-info)' }}>{id}</td>
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
            <tr><td colSpan={7} style={{ color: 'var(--text-dimmed)', padding: 4, textAlign: 'center' }}>No targets</td></tr>
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
const HEALTH_COLORS: Record<number, string> = { 1: 'var(--status-green)', 2: 'var(--status-amber)', 3: 'var(--status-red)' };

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
        border: '1px solid var(--border-active)',
        borderRadius: 8, padding: '8px 10px',
        fontFamily: 'var(--font-mono)', fontSize: 9,
        minWidth: 340,
      }}
    >
      <div style={{ display: 'flex', justifyContent: 'space-between', marginBottom: 6 }}>
        <span style={{ color: 'var(--text-dimmed)', fontSize: 8, letterSpacing: 1 }}>MODULE HEALTH</span>
        <span onClick={onClose} style={{ color: 'var(--text-dimmed)', cursor: 'pointer', fontSize: 10 }}>✕</span>
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', gap: 4 }}>
        {MODULE_NAMES.map((name, i) => {
          const p = byId[i + 1];
          const color = p ? HEALTH_COLORS[p.state ?? 0] ?? 'var(--text-dimmed)' : '#333';
          return (
            <div key={name} style={{
              flex: '1 1 45%', minWidth: 140,
              background: 'var(--surface-panel)',
              border: `1px solid ${color}22`,
              borderRadius: 4, padding: 4,
            }}>
              <span style={{ color, fontSize: 8 }}>● {name}</span>
              <div style={{ color: 'var(--text-dimmed)', fontSize: 7, marginTop: 2 }}>
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
  { key: 'safety',        label: 'SAF', color: 'var(--status-green)' },
  { key: 'ruleCompliance',label: 'RUL', color: 'var(--status-green)' },
  { key: 'delay',         label: 'DEL', color: 'var(--status-amber)' },
  { key: 'magnitude',     label: 'MAG', color: 'var(--status-green)' },
  { key: 'phase',         label: 'PHA', color: 'var(--status-green)' },
  { key: 'plausibility',  label: 'PLA', color: 'var(--status-amber)' },
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
        border: '1px solid var(--border-default)',
        borderRadius: 6, padding: '6px 8px',
        fontFamily: 'var(--font-mono)', fontSize: 9,
        minWidth: 90,
      }}
    >
      <div style={{ color: 'var(--accent-score)', fontSize: 8, marginBottom: 4, letterSpacing: 1 }}>
        SCORING
      </div>
      {DIMS.map(({ key, label, color }) => {
        const val = scoringRow?.[key];
        const disp = typeof val === 'number' ? val.toFixed(2) : '—';
        const numVal = typeof val === 'number' ? val : 0;
        const barColor = numVal >= 0.8 ? 'var(--status-green)' : numVal >= 0.6 ? 'var(--status-amber)' : 'var(--status-red)';
        return (
          <div key={key} style={{ display: 'flex', alignItems: 'center', gap: 6, marginBottom: 2 }}>
            <span style={{ color: 'var(--text-dimmed)', width: 22, fontSize: 7 }}>{label}</span>
            <div style={{ flex: 1, height: 4, background: 'var(--surface-input)', borderRadius: 2 }}>
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

## Task 8: ScoringRadarChart + ColregsDecisionTree + TimelineScrubber (shared components group B)

**Files:**
- Create: `web/src/screens/shared/ScoringRadarChart.tsx`
- Create: `web/src/screens/shared/ColregsDecisionTree.tsx`
- Create: `web/src/screens/shared/TimelineScrubber.tsx`

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
    return <polygon key={frac} points={pts} fill="none" stroke="var(--border-subtle)" strokeWidth="0.5" />;
  });

  return (
    <div data-testid="scoring-radar" style={{ display: 'flex', flexDirection: 'column', alignItems: 'center' }}>
      <div style={{ color: 'var(--text-dimmed)', fontSize: 9, letterSpacing: 1, marginBottom: 6 }}>
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
                    stroke="var(--border-subtle)" strokeWidth="0.5" />
              <text x={lx} y={ly} textAnchor="middle" fill="var(--text-dimmed)" fontSize="7">
                {label} {kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] != null
                  ? (kpis?.[AXES.find(a=>a.label===label)?.key ?? ''] ?? 0).toFixed(2)
                  : '—'}
              </text>
            </g>
          );
        })}
        {/* Data polygon */}
        <polygon points={dataPoints} fill="var(--accent-score)" fillOpacity="0.2" stroke="var(--accent-score)" strokeWidth="1.5" />
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
      <div data-testid="decision-tree" style={{ color: 'var(--text-dimmed)', fontSize: 10 }}>
        No rule events captured
      </div>
    );
  }

  // Build tree nodes from events
  const nodes: { indent: number; text: string; color: string }[] = [];
  for (const e of events) {
    const indent = nodes.length === 0 ? 0 : 1;
    if (e.rule_ref) {
      nodes.push({ indent: 0, text: e.rule_ref, color: 'var(--accent-info)' });
    }
    if (e.event_type) {
      nodes.push({ indent: 1, text: e.event_type.replace(/_/g, ' '), color: 'var(--accent-decision)' });
    }
    if (e.verdict != null) {
      const vLabel = e.verdict === 1 ? 'PASS' : e.verdict === 2 ? 'RISK' : 'FAIL';
      const vColor = e.verdict === 1 ? 'var(--status-green)' : e.verdict === 2 ? 'var(--status-amber)' : 'var(--status-red)';
      nodes.push({ indent: 2, text: vLabel, color: vColor });
    }
  }

  return (
    <div data-testid="decision-tree">
      <div style={{ color: 'var(--text-dimmed)', fontSize: 9, letterSpacing: 1, marginBottom: 8 }}>
        COLREGs DECISION TREE
      </div>
      {nodes.map((n, i) => (
        <div key={i} style={{
          marginLeft: n.indent * 16,
          marginBottom: 4,
          fontSize: 9,
          color: n.color,
          fontFamily: 'var(--font-mono)',
        }}>
          {n.indent > 0 && <span style={{ color: 'var(--text-dimmed)', marginRight: 6 }}>{'→'}</span>}
          {n.text}
        </div>
      ))}
      {/* Also show rule chain as fallback */}
      {events.length === 0 && ruleChain.length > 0 && (
        <div style={{ color: 'var(--text-secondary)', fontSize: 9, fontFamily: 'var(--font-mono)' }}>
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
        borderBottom: '1px solid var(--border-subtle)',
        gap: 12, height: 36,
      }}
    >
      <span style={{ color: 'var(--text-dimmed)', fontSize: 10 }}>▶</span>
      <div style={{ flex: 1, height: 4, background: 'var(--surface-input)', borderRadius: 2, position: 'relative' }}>
        {/* Progress bar */}
        <div style={{
          position: 'absolute', left: 0, top: 0,
          width: `${progressPct}%`, height: '100%',
          background: 'var(--accent-primary)', borderRadius: 2,
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
          background: 'var(--accent-primary)',
          borderRadius: '50%',
          transform: 'translateX(-50%)',
        }} />
      </div>
      <span style={{ color: 'var(--text-dimmed)', fontSize: 9, fontFamily: 'var(--font-mono)', minWidth: 44 }}>
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
          border: '1px solid var(--border-default)',
          borderRadius: 4, padding: '4px 8px',
          fontFamily: 'var(--font-mono)', fontSize: 8,
          display: 'flex', flexDirection: 'column', gap: 2,
        }}>
          <label style={{ color: 'var(--accent-primary)', cursor: 'pointer' }}>
            <input type="checkbox" defaultChecked data-testid="layer-toggle-imazu" /> Imazu geometry
          </label>
          <label style={{ color: 'var(--text-dimmed)', cursor: 'pointer' }}>
            <input type="checkbox" defaultChecked /> Depth contours
          </label>
          <label style={{ color: 'var(--text-dimmed)', cursor: 'pointer' }}>
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
        <div style={{ display: 'flex', gap: 0, marginBottom: 16, borderBottom: '1px solid var(--border-default)' }}>
          {(['template', 'procedural', 'ais'] as const).map((tab) => (
            <button key={tab} onClick={() => setActiveTab(tab)}
                    style={{
                      background: 'none', border: 'none', cursor: 'pointer',
                      color: activeTab === tab ? 'var(--accent-primary)' : 'var(--text-dimmed)',
                      borderBottom: activeTab === tab ? '2px solid var(--accent-primary)' : '2px solid transparent',
                      padding: '8px 16px', fontFamily: 'var(--font-mono)', fontSize: 10,
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
              background: 'var(--surface-input)', color: 'var(--text-primary)',
              border: '1px solid var(--border-default)', borderRadius: 4,
              padding: 4, fontFamily: 'var(--font-mono)', fontSize: 10,
            }}
                    onChange={(e) => handleSelect(e.target.value)}>
              {scenarios.map((s: ScenarioSummary) => (
                <option key={s.id} value={s.id}>{s.name} ({s.encounter_type})</option>
              ))}
            </select>
          </>
        )}

        {activeTab === 'ais' && (
          <div style={{ color: 'var(--text-secondary)', fontSize: 10, padding: 16 }}>
            <p style={{ color: 'var(--accent-primary)', fontWeight: 600 }}>AIS Scenario Import</p>
            <p style={{ color: 'var(--text-dimmed)', marginTop: 8 }}>
              Import historical AIS trajectories to extract COLREGs encounter scenarios.
              This feature uses the 5-stage AIS pipeline from D1.3b.2.
            </p>
            <div style={{ marginTop: 12, display: 'flex', flexDirection: 'column', gap: 8 }}>
              <div>
                <label style={{ color: 'var(--text-dimmed)', fontSize: 9 }}>Bounding Box</label>
                <div style={{ display: 'flex', gap: 4, marginTop: 4 }}>
                  <input placeholder="Min Lat" style={{
                    flex: 1, background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Min Lon" style={{
                    flex: 1, background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Max Lat" style={{
                    flex: 1, background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input placeholder="Max Lon" style={{
                    flex: 1, background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                </div>
              </div>
              <div>
                <label style={{ color: 'var(--text-dimmed)', fontSize: 9 }}>Time Window (UTC)</label>
                <div style={{ display: 'flex', gap: 4, marginTop: 4 }}>
                  <input type="date" style={{
                    background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                  <input type="date" style={{
                    background: 'var(--surface-input)', border: '1px solid var(--border-default)',
                    color: 'var(--text-primary)', padding: 4, borderRadius: 3, fontSize: 9,
                  }} />
                </div>
              </div>
              <button style={{
                background: 'var(--accent-primary)', color: 'var(--text-inverse)',
                border: 'none', padding: '6px 16px', borderRadius: 4,
                fontFamily: 'var(--font-mono)', fontSize: 10, fontWeight: 700,
                cursor: 'pointer', marginTop: 8,
              }}>
                Extract Encounters
              </button>
            </div>
          </div>
        )}

        {activeTab === 'procedural' && (
          <div style={{ color: 'var(--text-dimmed)', fontSize: 10, padding: 16 }}>
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
          color: rule.includes('give_way') ? 'var(--status-amber)'
                : rule.includes('passing') ? 'var(--status-green)'
                : 'var(--accent-info)',
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
          flex: 1, background: 'var(--surface-panel)', borderRadius: 8, padding: 16,
          border: '1px solid var(--border-default)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
        }}>
          <ScoringRadarChart kpis={kpis as Record<string, number> | null} />
        </div>

        {/* Right: COLREGs Decision Tree */}
        <div style={{
          flex: 1, background: 'var(--surface-panel)', borderRadius: 8, padding: 16,
          border: '1px solid var(--border-default)',
        }} data-testid="rule-chain">
          <ColregsDecisionTree
            ruleChain={scoring?.rule_chain ?? []}
            events={[]} // Phase 2: populate from ASDR events via scoring_node
          />
          <p style={{ color: 'var(--text-dimmed)', fontSize: 9, marginTop: 8 }}>
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
        background: 'var(--surface-panel)',
        border: '1px solid var(--border-default)',
        borderRadius: 6,
        display: 'flex', alignItems: 'center', gap: 12,
        fontFamily: 'var(--font-mono)', fontSize: 10,
      }}>
        <div style={{
          width: 60, height: 40,
          background: 'var(--surface-input)',
          border: '1px solid var(--border-subtle)',
          borderRadius: 3,
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          color: 'var(--text-dimmed)', fontSize: 7,
        }}>
          chart
        </div>
        <div>
          <div style={{ color: 'var(--accent-primary)', fontSize: 10, fontWeight: 600 }}>
            {scenarioId ? `Scenario ${scenarioId.slice(0, 8)}` : 'No scenario'}
          </div>
          <div style={{ color: 'var(--text-dimmed)', fontSize: 8, marginTop: 2 }}>
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

## Task 14: Add scoringRow to telemetryStore

**Files:**
- Modify: `web/src/store/telemetryStore.ts`

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

All 17 features from the Balanced checklist are covered. No TBD/TODO placeholders.

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

---

## Execution Handoff

**Plan complete and saved to `docs/superpowers/plans/2026-05-13-sil-hmi-dual-mode.md`. Two execution options:**

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. 16 tasks, max 5 parallel agents (Phase 1: Tasks 4-8, Phase 2: Tasks 9-13).

**2. Inline Execution** — Execute tasks in this session using executing-plans, batch execution with checkpoints.

**Which approach?**
