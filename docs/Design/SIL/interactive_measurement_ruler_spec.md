# HMI Interactive Measurement Ruler Specification (交互式测距测角工具设计规格书)

This specification outlines the technical design, user interactions, and rendering mechanics for the HMI Interactive Measurement Ruler in the MASS SIL HMI. It enables operators to measure relative distances and bearings between vessels or arbitrary geographic points dynamically.

---

## 1. Requirement & Goals

### 1.1 Remove Obsolete Elements in Screen 1 (Scenario Builder)
*   **Static Circles removal**: Completely remove the simple `0.5 nm` static DCPA warning circles around both the Own Ship and Target Ships in Screen 1 (Scenario Builder) to avoid visual clutter.
*   **Predicted Trajectory removal**: Hide/disable the target ship's yellow predicted trajectory line (`tgt-cog` and `tgt-cog-ticks` layers) when `previewData` is active (Scenario Builder). This keeps Screen 1 purely static as requested.

### 1.2 Interactive Measurement Tool (Ruler)
*   **Trigger A (Vessel Context Menu)**: After selecting a vessel (own ship or target ship), right-clicking anywhere on the map pops up a context menu with a "测量相对方位与距离" option. Choosing it starts a measurement line from that vessel to the cursor.
*   **Trigger C (Floating Map Toolbar)**: Add a vertical map toolbar directly above the Zoom controls (bottom-right of the map). The toolbar hosts a "Ruler/Measure" button. Clicking it starts a freeform measurement: first click on the map sets the start point, mouse move dynamically draws the line, second click locks the line.
*   **Snap (磁力吸附) Behavior**: When drawing a line, if the cursor gets within `20` screen pixels of any vessel's center, the measurement line automatically "snaps" to that vessel's center.
*   **Ruler Rendering (Native Map图层)**: The measurement lines and text labels are rendered natively in MapLibre using GeoJSON sources, ensuring fluid panning, zooming, and rotation with zero lag.
*   **Text Label (中点标注)**: Each measurement line displays its relative distance in nautical miles (`nm`) and true bearing in degrees (`°`) at its geometric midpoint: e.g., `1.45 nm / 042°`.
*   **Dismissal (退出与清除)**: Pressing the `Esc` key or clicking a "Clear" button in the toolbar clears all measurement lines and exits measurement mode.

---

## 2. Dynamic State & Data Model

Inside `SilMapView.tsx` (or an auxiliary React hook `useMapMeasurement`), we introduce:

```typescript
export type MeasurementMode = 'none' | 'vessel' | 'freeform';

export interface RulerLine {
  id: string;
  start: [number, number]; // [lng, lat]
  end: [number, number];   // [lng, lat]
  startSnapVesselId?: string;
  endSnapVesselId?: string;
  label: string;
}

export interface ActiveLine {
  start: [number, number];   // [lng, lat]
  current: [number, number]; // [lng, lat]
  startSnapVesselId?: string;
  currentSnapVesselId?: string;
}
```

---

## 3. Snapping & Maritime Calculations

### 3.1 Snapping Algorithm
On `mousemove` during measurement mode:
1.  Gather positions of all active vessels: own ship and targets.
2.  Project each vessel's `[lng, lat]` coordinates to screen pixels `(vx, vy)` using `map.project()`.
3.  Calculate the distance between the mouse position `(mx, my)` and `(vx, vy)`.
4.  If the distance is less than `20px`, snap `current` to `[v.lng, v.lat]` and save `currentSnapVesselId = v.id`.

### 3.2 Distance & Bearing Math
To calculate nautical miles and true bearing between `Start [lng1, lat1]` and `End [lng2, lat2]`:

*   **Distance (Haversine Formula)**:
    $$\Delta \text{lat} = \text{lat}_2 - \text{lat}_1, \quad \Delta \text{lng} = \text{lng}_2 - \text{lng}_1$$
    $$a = \sin^2\left(\frac{\Delta \text{lat}}{2}\right) + \cos(\text{lat}_1) \cos(\text{lat}_2) \sin^2\left(\frac{\Delta \text{lng}}{2}\right)$$
    $$c = 2 \cdot \text{atan2}(\sqrt{a}, \sqrt{1 - a})$$
    $$D_{\text{meters}} = R \cdot c \quad (\text{where } R = 6,371,000 \text{ m})$$
    $$D_{\text{nm}} = \frac{D_{\text{meters}}}{1852}$$

*   **Bearing (True Bearing)**:
    $$y = \sin(\Delta \text{lng}) \cos(\text{lat}_2)$$
    $$x = \cos(\text{lat}_1) \sin(\text{lat}_2) - \sin(\text{lat}_1) \cos(\text{lat}_2) \cos(\Delta \text{lng})$$
    $$\theta_{\text{rad}} = \text{atan2}(y, x)$$
    $$\text{Bearing}_{\text{deg}} = (\theta_{\text{rad}} \cdot \frac{180}{\pi} + 360) \pmod{360}$$

---

## 4. UI/UX Elements & Layout

### 4.1 Right-Click Context Menu (Trigger A)
*   **Behavior**: When a ship is selected in Screen 1 (via click) and the user right-clicks anywhere on the map:
    1.  Prevent the browser's default context menu.
    2.  Render a floating HTML div (`class="hmi-context-menu"`) at `e.point` (mouse position).
    3.  Menu option: `📐 测量从该船出发的距离与方位`
*   **Styling**: Premium dark aesthetic, glassmorphism (`backdrop-filter: blur(12px)`), phosphor teal border (`border: 1px solid var(--line-2)`), matching the unified MASS HMI theme.

### 4.2 Map Floating Toolbar (Trigger C)
*   **Position**: Fixed absolute positioning at the bottom-right corner, directly above the map zoom control group.
*   **Items**:
    *   `📐` **Ruler Button**: Toggles freeform measurement mode.
    *   `🧹` **Clear Button**: Wipes out all locked measurement lines.
*   **Styling**: Vertical segmented button group, glassmorphism background, standard Lucide icons (`LucideRuler` & `LucideTrash2`).

---

## 5. MapLibre GL Layers Configuration

We register a dynamic GeoJSON source `'measurement-ruler'` and two layers:

### 5.1 Lines Layer (`measurement-ruler-line`)
*   **Type**: `line`
*   **Source**: `'measurement-ruler'`
*   **Filter**: `['==', ['geometry-type'], 'LineString']`
*   **Paint Properties**:
    ```json
    {
      "line-color": "#2dd4bf",
      "line-width": 2,
      "line-dasharray": [4, 3],
      "line-opacity": 0.85
    }
    ```

### 5.2 Labels Layer (`measurement-ruler-label`)
*   **Type**: `symbol`
*   **Source**: `'measurement-ruler'`
*   **Filter**: `['==', ['geometry-type'], 'Point']`
*   **Layout Properties**:
    ```json
    {
      "text-field": ["get", "label"],
      "text-font": ["Open Sans Regular"],
      "text-size": 10,
      "text-anchor": "center",
      "text-offset": [0, -1.0]
    }
    ```
*   **Paint Properties**:
    ```json
    {
      "text-color": "#2dd4bf",
      "text-halo-color": "#070c13",
      "text-halo-width": 1.5,
      "text-halo-blur": 0.5
    }
    ```

---

## 6. Verification & E2E Testing Plan

### 6.1 Automated Unit Tests
*   Verify snapping coordinates projection using mock `map.project` values.
*   Validate Haversine distance and True Bearing calculations for standard values:
    *   `[10.38, 63.44] -> [10.38, 63.54]` must return exactly `6.0 nm / 000°` bearing.

### 6.2 Manual UI Verification
1.  **Static Preview**: Load Screen 1, verify that all simple circular warning lines are completely removed, and target yellow COG lines are not drawn.
2.  **Ruler (Trigger A)**: Click Own Ship, right-click on the map, select "Measure", move mouse. Verify that a dashed teal line draws from own ship to mouse. Drag cursor near target ship and verify the line snaps to the target's center and displays correct distance/bearing.
3.  **Ruler (Trigger C)**: Click the Ruler icon in the right toolbar. Click any empty spot, move mouse, click another spot. Verify a persistent line is locked. Verify multiple lines can be created.
4.  **Exiting**: Click "Clear" or press `Esc` to verify all lines disappear and cursor returns to normal.
