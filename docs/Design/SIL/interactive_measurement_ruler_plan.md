# HMI Interactive Measurement Ruler Implementation Plan (交互式测距测角工具实施计划)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the Vessel Context Menu Ruler (Trigger A) and the Free-form Map Toolbar Ruler (Trigger C) with Snapping, Midpoint Labels, and Esc-key cancellation. Remove obsolete static circles and predicted yellow trajectory COG lines in Screen 1 Scenario Builder preview mode.

**Architecture:** Manage measurement state natively inside `SilMapView.tsx`. Listen to MapLibre mouse events (`mousedown`, `mousemove`, `mouseup`, `contextmenu`) and use native `line` and `symbol` layers on top of a dynamic GeoJSON source `'measurement-ruler'` to draw the lines and midpoint bearing/distance labels.

**Tech Stack:** React, TypeScript, MapLibre GL, Turf.js (or simple Great Circle trigonometric formulas for low performance overhead).

---

### Task 1: Clean Up Scenario Builder (Screen 1) Map Layers

**Files:**
*   Modify: `web/src/map/SilMapView.tsx:757-828`

- [ ] **Step 1: Implement conditional rendering for Target COG Trajectories**
    Update the `useEffect` responsible for drawing `'tgt-cog'` and `'tgt-cog-ticks'` to check if `previewData` is active. If `previewData` is provided, set both sources to empty features to completely hide the yellow dynamic trajectories in Screen 1.
    ```typescript
    // In SilMapView.tsx (target markers + COG leaders useEffect):
    if (previewData) {
      // Clean/hide expected trajectory layers in static Scenario Builder mode
      (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: [] });
      (map.getSource('tgt-cog-ticks') as any)?.setData({ type: 'FeatureCollection', features: [] });
    } else {
      // Normal dynamic simulation monitoring drawing logic (Screen 3)
      (map.getSource('tgt-cog') as any)?.setData({ type: 'FeatureCollection', features: cogFeatures });
      (map.getSource('tgt-cog-ticks') as any)?.setData({ type: 'FeatureCollection', features: tickFeatures });
    }
    ```
- [ ] **Step 2: Run build to verify clean compilation**
    Run: `npm run build` in `web/`
    Expected: Successful build.

---

### Task 2: Implement Coordinate Calculations (Distance & Bearing)

**Files:**
*   Modify: `web/src/map/SilMapView.tsx` (Add helper functions above component)

- [ ] **Step 1: Write helper functions for Haversine Distance and True Bearing**
    Add mathematical functions to compute Great Circle distance (in nm) and initial true bearing (in degrees):
    ```typescript
    function calculateDistanceNm(lon1: number, lat1: number, lon2: number, lat2: number): number {
      const R = 6371000; // Earth radius in meters
      const dLat = (lat2 - lat1) * Math.PI / 180;
      const dLon = (lon2 - lon1) * Math.PI / 180;
      const a =
        Math.sin(dLat / 2) * Math.sin(dLat / 2) +
        Math.cos(lat1 * Math.PI / 180) * Math.cos(lat2 * Math.PI / 180) *
        Math.sin(dLon / 2) * Math.sin(dLon / 2);
      const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
      return (R * c) / 1852; // convert meters to nautical miles
    }

    function calculateBearingDeg(lon1: number, lat1: number, lon2: number, lat2: number): number {
      const lat1Rad = lat1 * Math.PI / 180;
      const lat2Rad = lat2 * Math.PI / 180;
      const dLonRad = (lon2 - lon1) * Math.PI / 180;
      const y = Math.sin(dLonRad) * Math.cos(lat2Rad);
      const x = Math.cos(lat1Rad) * Math.sin(lat2Rad) - Math.sin(lat1Rad) * Math.cos(lat2Rad) * Math.cos(dLonRad);
      const theta = Math.atan2(y, x);
      return (theta * 180 / Math.PI + 360) % 360;
    }
    ```
- [ ] **Step 2: Add a basic unit test to verify calculations**
    Verify that `calculateDistanceNm(10.38, 63.44, 10.38, 63.54)` yields exactly `6.0` nm and `calculateBearingDeg` yields `0` degrees.
- [ ] **Step 3: Run the build to verify**
    Run: `npm run build` in `web/`
    Expected: SUCCESS

---

### Task 3: Implement Measurement Mode Logic & Map Event Binding

**Files:**
*   Modify: `web/src/map/SilMapView.tsx` (Component internals & Event binding)

- [ ] **Step 1: Define Measurement React States**
    ```typescript
    const [measurementMode, setMeasurementMode] = useState<'none' | 'vessel' | 'freeform'>('none');
    const [rulerLines, setRulerLines] = useState<RulerLine[]>([]);
    const [activeLine, setActiveLine] = useState<ActiveLine | null>(null);
    const [contextMenu, setContextMenu] = useState<{ x: number, y: number, lng: number, lat: number } | null>(null);
    ```
- [ ] **Step 2: Set up MapLibre GeoJSON Ruler Source and Layers in `load` event**
    Add the source `'measurement-ruler'` and layers `'measurement-ruler-line'`, `'measurement-ruler-label'` in `map.on('load')` in `SilMapView.tsx`.
- [ ] **Step 3: Implement dynamic GeoJSON feature generation**
    Add a `useEffect` that synchronizes `rulerLines` and `activeLine` into the `'measurement-ruler'` GeoJSON source:
    ```typescript
    useEffect(() => {
      const map = mapRef.current;
      if (!map || !styleReady.current) return;

      const features: any[] = [];
      const lines = [...rulerLines];
      if (activeLine) {
        lines.push({
          id: 'active',
          start: activeLine.start,
          end: activeLine.current,
          label: activeLine.label,
        });
      }

      for (const line of lines) {
        // LineString Feature
        features.push({
          type: 'Feature',
          geometry: { type: 'LineString', coordinates: [line.start, line.end] },
          properties: { type: 'line' },
        });

        // Midpoint Point Feature for label
        const midLng = (line.start[0] + line.end[0]) / 2;
        const midLat = (line.start[1] + line.end[1]) / 2;
        features.push({
          type: 'Feature',
          geometry: { type: 'Point', coordinates: [midLng, midLat] },
          properties: { type: 'label', label: line.label },
        });
      }

      (map.getSource('measurement-ruler') as any)?.setData({
        type: 'FeatureCollection',
        features,
      });
    }, [rulerLines, activeLine]);
    ```
- [ ] **Step 4: Implement snapping and mouse events on map**
    Implement snapping helper to snap coordinates to any target ship or own ship:
    ```typescript
    const getSnappedCoordinate = (lngLat: [number, number]): { coords: [number, number]; id?: string } => {
      const map = mapRef.current;
      if (!map) return { coords: lngLat };

      const px = map.project(lngLat);
      const vessels = [
        ...(ownShip ? [{ id: 'ownship', lat: ownShip.pose.lat, lon: ownShip.pose.lon }] : []),
        ...targets.map(t => ({ id: t.mmsi, lat: t.pose.lat, lon: t.pose.lon })),
      ];

      for (const v of vessels) {
        const vPx = map.project([v.lon, v.lat]);
        const dx = px.x - vPx.x;
        const dy = px.y - vPx.y;
        if (Math.sqrt(dx * dx + dy * dy) < 20) {
          return { coords: [v.lon, v.lat], id: v.id };
        }
      }

      return { coords: lngLat };
    };
    ```
    Add event listeners for map interaction:
    *   `mousedown` / `click`: Click handlers to set starting point in freeform mode or lock endpoint.
    *   `mousemove`: Updates current cursor coordinate, applies snapping, calculates distance and bearing, and updates `activeLine`.
    *   `contextmenu`: Handles Trigger A globally. Shows context menu if `selectedVesselId` is not null.
    *   `keydown` for `'Escape'`: Clears active measurement and resets state.
- [ ] **Step 5: Run npm run build**
    Expected: Successful build.

---

### Task 4: UI Toolbar and Context Menu Presentation

**Files:**
*   Modify: `web/src/map/SilMapView.tsx` (Render UI overlays)

- [ ] **Step 1: Render Map Floating Toolbar**
    Create the vertical毛玻璃 floating toolbar directly above the Zoom controls in the bottom-right corner.
    ```tsx
    {/* Floating Measurement Toolbar */}
    <div style={{
      position: 'absolute', bottom: 120, right: 20, zIndex: 30,
      display: 'flex', flexDirection: 'column', gap: 6,
      background: 'rgba(10, 15, 24, 0.85)', backdropFilter: 'blur(16px)',
      border: '1px solid var(--line-2)', borderRadius: 8, padding: 4,
      boxShadow: '0 4px 20px rgba(0,0,0,0.4)'
    }}>
      <button
        title="测距/测角 (Measure)"
        onClick={() => {
          setMeasurementMode(prev => prev === 'freeform' ? 'none' : 'freeform');
          setActiveLine(null);
        }}
        style={{
          width: 36, height: 36, borderRadius: 6, border: 'none', cursor: 'pointer',
          background: measurementMode === 'freeform' ? 'rgba(45, 212, 191, 0.2)' : 'transparent',
          color: measurementMode === 'freeform' ? 'var(--c-phos)' : 'var(--txt-3)',
          display: 'flex', alignItems: 'center', justifyContent: 'center', transition: 'all 0.2s'
        }}
      >
        📐
      </button>
      <button
        title="清除测量 (Clear)"
        onClick={() => {
          setRulerLines([]);
          setActiveLine(null);
          setMeasurementMode('none');
        }}
        style={{
          width: 36, height: 36, borderRadius: 6, border: 'none', cursor: 'pointer',
          background: 'transparent', color: 'var(--txt-3)',
          display: 'flex', alignItems: 'center', justifyContent: 'center', transition: 'all 0.2s'
        }}
        onMouseEnter={(e) => e.currentTarget.style.color = 'var(--c-danger)'}
        onMouseLeave={(e) => e.currentTarget.style.color = 'var(--txt-3)'}
      >
        🧹
      </button>
    </div>
    ```
- [ ] **Step 2: Render Right-Click Context Menu overlay**
    ```tsx
    {contextMenu && (
      <div style={{
        position: 'absolute', left: contextMenu.x, top: contextMenu.y, zIndex: 1000,
        background: 'rgba(7, 16, 27, 0.94)', backdropFilter: 'blur(12px)',
        border: '1px solid var(--line-2)', borderRadius: 6, padding: '4px 0',
        minWidth: 160, boxShadow: '0 8px 24px rgba(0,0,0,0.6)'
      }}>
        <div
          onClick={() => {
            const startVessel = selectedVesselId === 'ownship'
              ? (ownShip ? [ownShip.pose.lon, ownShip.pose.lat] as [number, number] : null)
              : (() => {
                  const t = targets.find(tgt => tgt.mmsi === selectedVesselId);
                  return t ? [t.pose.lon, t.pose.lat] as [number, number] : null;
                })();
            if (startVessel) {
              setMeasurementMode('vessel');
              setActiveLine({
                start: startVessel,
                current: [contextMenu.lng, contextMenu.lat],
                label: '0.00 nm / 000°',
                startSnapVesselId: selectedVesselId || undefined
              });
            }
            setContextMenu(null);
          }}
          style={{
            padding: '8px 12px', color: 'var(--txt-1)', fontSize: 11, fontFamily: 'monospace',
            cursor: 'pointer', transition: 'background 0.15s'
          }}
          onMouseEnter={(e) => e.currentTarget.style.background = 'rgba(45,212,191,0.15)'}
          onMouseLeave={(e) => e.currentTarget.style.background = 'transparent'}
        >
          📐 测量相对距离/方位
        </div>
      </div>
    )}
    ```
- [ ] **Step 3: Run full build and tests**
    Run: `npm run build`
    Expected: SUCCESS with zero warnings/errors.
