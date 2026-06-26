# Target Ship Display Consistency Design Specification

This design document outlines the implementation plan for enhancing target ship (Target Vessel) display on the SIL HMI Map to be functionally and visually consistent with the own ship.

## Background & Goal

Currently, the own ship has historical track (trail) functionality represented on the map as a teal dashed line (`#2dd4bf`). Target ships do not have tracks/trails, making it harder for marine operators to quickly assess target ships' historical paths.

Additionally, the target ship's label/plaque needs to maintain high visual and functional consistency with the own ship's plaque (such as structural styling, display metrics, and anti-rotation behaviors) while maintaining color distinction (yellow theme `#fbbf24` for targets vs teal theme `#2dd4bf` for own ship) to ensure the operator can easily distinguish them.

This specification implements **Scheme A (Color-Differentiated Consistency)**:
* **Target Ship Track (Trail)**: Implemented as dashed line trails of target ship yellow (`#fbbf24`), identical in width, opacity, and dash structure to the own ship's trail.
* **Target Ship Label (Plaque)**: Visually consistent in terms of layout, header elements, leader lines, and anti-rotation, using target yellow colors.

---

## Proposed Changes

### 1. Telemetry Store (`web/src/store/telemetryStore.ts`)

To support trails for multiple target ships, the telemetry store needs to track the history of coordinates (`[longitude, latitude]`) per target ship.

* **State Variables**:
  * `targetTrails`: `Record<string, [number, number][]>` mapping target ship ID/MMSI to an array of coordinate pairs.
  * `targetLastTrailTimes`: `Record<string, number>` mapping target ship ID/MMSI to the timestamp of the last trail point added, limiting rate to at most once per second.
* **Store Mutation & Lifecycle**:
  * `updateTargets`: When target telemetry is updated, parse each target's current position and append it to `targetTrails[id]` if coordinates are valid and at least `1000ms` have passed since `targetLastTrailTimes[id]`. Like `ownShipTrail`, limit the trail length to `MAX_TRAIL`.
  * `reset`: Clear `targetTrails` and `targetLastTrailTimes` by resetting them to `{}`.

### 2. Map View Component (`web/src/map/SilMapView.tsx`)

* **Maplibre Source and Layer Initialization**:
  * Add a new vector source `'tgt-trail'` of type `geojson` to render target ship trails.
  * Add a new line layer `'tgt-trail-line'` styled to render dashed lines in yellow:
    * `'line-color'`: `'#fbbf24'`
    * `'line-width'`: `1.5`
    * `'line-opacity'`: `0.55`
    * `'line-dasharray'`: `[3, 2]` (identical style to `'trail-line'`)
* **Target Trails Synchronizer Effect**:
  * Select `targetTrails` from the telemetry store.
  * Add a new `useEffect` that listens to `targetTrails` and updates `'tgt-trail'` source with a GeoJSON `FeatureCollection` composed of `LineString` features representing active target ship trails.
* **Plaque Compatibility Check**:
  * Verify that target plaques use the yellow color scheme (`#fbbf24`) and display all relevant telemetry fields (`HDG`, `COG`, `SOG`, `ROT`) aligned with the own ship plaque.

---

## Spec Self-Review

* **Placeholder Scan**: No TODOs or TBDs. All details are concrete and fully defined.
* **Internal Consistency**: Color scheme A is uniformly applied across all store and view components.
* **Scope Check**: Scoped tightly to `telemetryStore.ts` and `SilMapView.tsx`. Completely manageable.
* **Ambiguity Check**: Target ship trails are keyed by target MMSI (or fallback ID) ensuring no coordinate mixing.

---

## Verification Plan

### Automated Tests
* We will verify that state updates in `telemetryStore.ts` work perfectly by running the unit tests using:
  `npm run test` or `npm run test:unit` in `web/` directory.

### Manual / Visual Verification
* Start the SIL application (`npm run sys:start`).
* Observe simulated targets on the HMI map (http://localhost:5173 or the native UI).
* Verify that as target ships move, they draw a yellow dashed track line behind them.
* Click on a target ship and verify its plaque draws correctly, stays upright when rotating, and displays correct live telemetry (`HDG`, `COG`, `SOG`, `ROT`).
