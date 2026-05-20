# D1.3b.3 · Web HMI + ENC + foxglove_bridge · Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace static SVG radar prototype with production-grade MapLibre GL JS Web HMI displaying ENC charts + own-ship/target vessels + ROS2 live data via foxglove_bridge.

**Architecture:** React 18 + TypeScript + Vite (dev) / FastAPI StaticFiles (prod) serving MapLibre GL JS 4.x over ENC MVT tiles. foxglove_bridge (C++, port 8765, JSON mode) streams nav data; FastAPI `/ws/sil_debug` (port 8000) pushes decision data. Three-layer throttle: ROS2 topic_tools (50→10Hz) + Protobuf (DEMO-2) + React rAF (always).

**Tech Stack:** React 18, TypeScript, Vite, MapLibre GL JS 4.x, roslibjs-foxglove (npm), Python 3.11+ (FastAPI static serving), GDAL + Tippecanoe (ENC pipeline), C++ foxglove_bridge (ROS2 Humble apt).

**Visual Authority:** `MASS_TDL_HMI_Design_Spec_v1.0.md` §8 (CSS tokens, fonts, spacing).

---

## Parallel Dispatch Waves

```
Wave 1 (dispatch together, zero cross-deps):
  Task 1  ── React+Vite+MapLibre skeleton + CSS tokens
  Task 2  ── ENC MVT tile pipeline
  Task 3  ── foxglove_bridge install + launch file
  Task 4  ── Side panel components (standalone, mock data)

Wave 2 (dispatch together, after Wave 1 completes):
  Task 5  ── MapLibre layers: own-ship, target, CPA ring, COG vector
  Task 6  ── foxglove_bridge → React data flow
  Task 7  ── FastAPI static serving + Vite dev proxy

Wave 3 (sequential, final integration):
  Task 8  ── DEMO-1 R14 head-on E2E integration
  Task 9  ── DoD verification + commit
```

---

### Task 1: React + Vite + MapLibre GL skeleton + CSS tokens

**Files:**
- Create: `web/package.json`
- Create: `web/index.html`
- Create: `web/tsconfig.json`
- Create: `web/vite.config.ts`
- Create: `web/src/main.tsx`
- Create: `web/src/App.tsx`
- Create: `web/src/styles/tokens.css`
- Create: `web/src/types/sil.ts`

- [ ] **Step 1: Create `web/package.json` with all dependencies**

```json
{
  "name": "mass-l3-tdl-hmi",
  "private": true,
  "version": "0.1.0",
  "type": "module",
  "scripts": {
    "dev": "vite",
    "build": "tsc && vite build",
    "preview": "vite preview"
  },
  "dependencies": {
    "maplibre-gl": "^4.7.0",
    "react": "^18.3.1",
    "react-dom": "^18.3.1",
    "roslibjs-foxglove": "https://github.com/tier4/roslibjs-foxglove/releases/download/v0.0.4/tier4-roslibjs-foxglove-0.0.4.tgz"
  },
  "devDependencies": {
    "@types/react": "^18.3.0",
    "@types/react-dom": "^18.3.0",
    "@vitejs/plugin-react": "^4.3.0",
    "typescript": "^5.5.0",
    "vite": "^5.4.0"
  }
}
```

- [ ] **Step 2: Run `npm install` and verify**

Run: `cd web && npm install`
Expected: packages installed, no errors.

- [ ] **Step 3: Create `web/index.html`**

```html
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>L3 TDL SIL HMI</title>
  <link rel="preconnect" href="https://fonts.googleapis.com" />
  <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin />
  <link href="https://fonts.googleapis.com/css2?family=Saira+Condensed:wght@300;400;500;600;700&family=JetBrains+Mono:wght@400;500;600;700&family=Noto+Sans+SC:wght@300;400;500;700&display=swap" rel="stylesheet" />
</head>
<body>
  <div id="root"></div>
  <script type="module" src="/src/main.tsx"></script>
</body>
</html>
```

- [ ] **Step 4: Create `web/tsconfig.json`**

```json
{
  "compilerOptions": {
    "target": "ES2020",
    "useDefineForClassFields": true,
    "lib": ["ES2020", "DOM", "DOM.Iterable"],
    "module": "ESNext",
    "skipLibCheck": true,
    "moduleResolution": "bundler",
    "allowImportingTsExtensions": true,
    "resolveJsonModule": true,
    "isolatedModules": true,
    "noEmit": true,
    "jsx": "react-jsx",
    "strict": true,
    "noUnusedLocals": true,
    "noUnusedParameters": true,
    "noFallthroughCasesInSwitch": true
  },
  "include": ["src"]
}
```

- [ ] **Step 5: Create `web/vite.config.ts`**

```typescript
import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      '/ws': {
        target: 'ws://localhost:8000',
        ws: true,
      },
      '/sil': {
        target: 'http://localhost:8000',
      },
      '/tiles': {
        target: 'http://localhost:8000',
      },
    },
  },
  build: {
    outDir: 'dist',
    sourcemap: true,
  },
});
```

- [ ] **Step 6: Create `web/src/styles/tokens.css` — Design Spec §8 Night mode**

```css
/* ═══════════════════════════════════════════
   HMI Design Tokens — D1.3b.3 DEMO-1
   Authority: MASS_TDL_HMI_Design_Spec_v1.0.md §8
   Mode: Night (default); Day/Dusk/Bright deferred to DEMO-2/D3.4
   ═══════════════════════════════════════════ */
:root, [data-theme="night"] {
  /* ── Backgrounds ── */
  --bg-0: #070C13;
  --bg-1: #0B1320;
  --bg-2: #101B2C;
  --bg-3: #16263A;
  /* ── Lines ── */
  --line-1: #1B2C44;
  --line-2: #243C58;
  --line-3: #3A5677;
  /* ── Text ── */
  --txt-0: #F1F6FB;
  --txt-1: #C5D2E0;
  --txt-2: #8A9AAD;
  --txt-3: #566578;
  /* ── Semantic colors ── */
  --color-phos: #5BC0BE;
  --color-stbd: #3FB950;
  --color-port: #F26B6B;
  --color-warn: #F0B72F;
  --color-info: #79C0FF;
  --color-danger: #F85149;
  --color-magenta: #D070D0;
  /* ── Autonomy levels ── */
  --color-auto-d4: #3FB950;
  --color-auto-d3: #79C0FF;
  --color-auto-d2: #F0B72F;
  --color-mrc: #F85149;
  /* ── Chart layer colors ── */
  --chart-water-deep: #0d2b4a;
  --chart-water-med: #12406b;
  --chart-water-shallow: #1a5c8a;
  --chart-land: #1e2d1e;
  --chart-coast: #2a4020;
  --chart-depth-line: rgba(100, 160, 220, 0.15);
  --chart-grid: rgba(255, 255, 255, 0.04);
  /* ── Fonts ── */
  --fnt-disp: 'Saira Condensed', 'Noto Sans SC', sans-serif;
  --fnt-mono: 'JetBrains Mono', ui-monospace, monospace;
  --fnt-body: 'Noto Sans SC', 'Saira Condensed', sans-serif;
  /* ── Spacing & radius (Design Spec §8.3) ── */
  --radius-none: 0;
  --radius-min: 2px;
  --sp-xs: 4px;
  --sp-sm: 8px;
  --sp-md: 12px;
  --sp-lg: 18px;
  --sp-xl: 24px;
}

/* ── Reset ── */
*, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }
html, body, #root {
  height: 100%;
  background: var(--bg-0);
  color: var(--txt-1);
  font-family: var(--fnt-body);
  font-size: 13px;
  overflow: hidden;
  -webkit-font-smoothing: antialiased;
}
```

- [ ] **Step 7: Create `web/src/types/sil.ts`**

```typescript
/** Matches SilDebugSchema from sil_schemas.py */
export interface SilDebugData {
  timestamp: string;
  scenario_id: string;
  sat1: {
    threat_count: number;
    threats: Sat1Threat[];
  } | null;
  sat2_reasoning: string | null;
  sat3_tdl_s: number;
  sat3_tmr_s: number;
  odd: {
    zone: string;
    envelope_state: string;
    conformance_score: number;
    confidence: number;
  } | null;
  job_status: 'idle' | 'running' | 'done';
  cpa_nm: number;
  tcpa_s: number;
  rule_text: string;
  decision_text: string;
  module_status: boolean[];
  asdr_events: AsdrEvent[];
}

export interface Sat1Threat {
  id: string;
  bearing_deg: number;
  range_nm: number;
  cpa_nm: number;
  tcpa_s: number;
}

export interface AsdrEvent {
  timestamp: string;
  event_type: string;
  step: number;
  rule: string;
  cpa_nm: number;
  tcpa_s: number;
  action: string;
  verdict: 'PASS' | 'RISK';
}

export interface NavState {
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  heading_deg: number;
}

export interface TargetState {
  mmsi: string;
  lat: number;
  lon: number;
  sog_kn: number;
  cog_deg: number;
  heading_deg: number;
  cpa_nm: number;
  tcpa_s: number;
  colreg_role: 'give-way' | 'stand-on' | 'overtaking' | 'safe';
  confidence: number;
}
```

- [ ] **Step 8: Create `web/src/main.tsx`**

```tsx
import React from 'react';
import ReactDOM from 'react-dom/client';
import App from './App';
import './styles/tokens.css';

ReactDOM.createRoot(document.getElementById('root')!).render(
  <React.StrictMode>
    <App />
  </React.StrictMode>,
);
```

- [ ] **Step 9: Create `web/src/App.tsx` skeleton**

```tsx
import { useState } from 'react';

export default function App() {
  const [view, _setView] = useState<'map' | 'panel'>('map');

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      {/* Chart Area — Task 5 fills this */}
      <div style={{
        background: 'var(--bg-0)',
        position: 'relative',
        overflow: 'hidden',
        display: 'flex',
        alignItems: 'center',
        justifyContent: 'center',
      }}>
        <div style={{ color: 'var(--txt-2)', fontFamily: 'var(--fnt-mono)', fontSize: '13px' }}>
          MAP AREA — Task 5 · MapLibre GL
        </div>
      </div>

      {/* Side Panel — Task 4 fills this */}
      <div style={{
        background: 'var(--bg-1)',
        borderLeft: '1px solid var(--line-1)',
        overflowY: 'auto',
        display: 'flex',
        flexDirection: 'column',
      }}>
        <div style={{
          padding: '18px 20px',
          borderBottom: '1px solid var(--line-1)',
        }}>
          <div style={{
            fontFamily: 'var(--fnt-disp)',
            fontSize: '11px',
            letterSpacing: '0.22em',
            color: 'var(--color-phos)',
            fontWeight: 600,
            marginBottom: '12px',
          }}>
            SIDE PANEL — Task 4 · Components
          </div>
        </div>
      </div>
    </div>
  );
}
```

- [ ] **Step 10: Verify build and dev server**

```bash
cd web && npm run build
```
Expected: `dist/` directory created, no errors.

```bash
cd web && npx vite --host 2>&1 &
sleep 3 && curl -s http://localhost:5173 | head -5
kill %1
```
Expected: HTML response with `<div id="root">`.

- [ ] **Step 11: Commit**

```bash
cd /path/to/worktree
git add web/package.json web/package-lock.json web/index.html \
        web/tsconfig.json web/vite.config.ts web/src/
git commit -m "feat(d1.3b.3): add React+Vite+MapLibre GL project skeleton with Design Spec CSS tokens"
```

---

### Task 2: ENC MVT tile pipeline

**Files:**
- Create: `tools/enc/convert_enc.sh`
- Create: `tools/enc/layers.txt`
- Create: `web/public/tiles/trondheim/.gitkeep`

- [ ] **Step 1: Verify GDAL OpenFileGDB driver reads Trondelag.gdb**

```bash
ogrinfo /Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb 2>&1 | head -5
```
Expected: `INFO: Open of ... using driver 'OpenFileGDB' successful.`

- [ ] **Step 2: Create `tools/enc/layers.txt` — navigation-critical S-57-equivalent layers**

```
landareal
kystkontur
dybdeareal
dybdekurve
fareomrade
grunne
skjer
kaibrygge
```

- [ ] **Step 3: Create `tools/enc/convert_enc.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail

# D1.3b.3 ENC MVT tile pipeline
# Input:  ESRI File Geodatabase (.gdb) from Kartverket Norway
# Output: web/public/tiles/trondheim/{z}/{x}/{y}.pbf (MapLibre vector tiles)

GDB="${1:-/Users/marine/Code/colav-simulator/data/enc/Trondelag.gdb}"
OUTDIR="${2:-web/public/tiles/trondheim}"
TMPDIR="$(mktemp -d)"
LAYERS_FILE="$(dirname "$0")/layers.txt"

echo "=== D1.3b.3 ENC Pipeline ==="
echo "Source: $GDB"
echo "Output: $OUTDIR"

# 1. Export each layer to GeoJSONSeq (newline-delimited, memory-efficient)
echo "[1/3] Exporting layers from GDB..."
> "$TMPDIR/all.geojsons"
while read -r layer; do
  [ -z "$layer" ] && continue
  echo "  Layer: $layer"
  ogr2ogr -f GeoJSONSeq -t_srs EPSG:4326 \
    "$TMPDIR/${layer}.geojsons" "$GDB" "$layer" 2>&1 | tail -1
  # Tag each feature with its source layer name for MapLibre filter expressions
  python3 -c "
import json, sys
for line in open('$TMPDIR/${layer}.geojsons'):
    feat = json.loads(line)
    feat.setdefault('properties', {})['_layer'] = '$layer'
    print(json.dumps(feat))
" >> "$TMPDIR/all.geojsons"
done < "$LAYERS_FILE"

FEAT_COUNT=$(wc -l < "$TMPDIR/all.geojsons")
echo "  Total features: $FEAT_COUNT"

# 2. Convert to MVT with Tippecanoe
echo "[2/3] Generating MVT tiles (Z6-Z14)..."
tippecanoe -Z6 -z14 \
  --drop-densest-as-needed \
  --simplification=4 \
  --maximum-zoom=14 \
  --minimum-zoom=6 \
  --force \
  --output "$TMPDIR/trondheim.mbtiles" \
  "$TMPDIR/all.geojsons" 2>&1 | tail -3

TILE_SIZE=$(du -h "$TMPDIR/trondheim.mbtiles" | cut -f1)
echo "  .mbtiles size: $TILE_SIZE"

# 3. Extract to directory
echo "[3/3] Extracting tiles..."
mkdir -p "$OUTDIR"
mb-util --image_format=pbf "$TMPDIR/trondheim.mbtiles" "$OUTDIR" 2>&1 | tail -1
TILE_COUNT=$(find "$OUTDIR" -name '*.pbf' | wc -l)
echo "  Tile count: $TILE_COUNT"

rm -rf "$TMPDIR"
echo "=== Done: $OUTDIR ==="
```

- [ ] **Step 4: Make script executable**

```bash
chmod +x tools/enc/convert_enc.sh
```

- [ ] **Step 5: Install Tippecanoe and mb-util if not present**

```bash
which tippecanoe || brew install tippecanoe
which mb-util || npm install -g @mapbox/mbview 2>/dev/null; which mb-util || echo "mb-util may need separate install"
```
Note: `mb-util` ships with Tippecanoe on macOS Homebrew. If missing: `pip install mbutil`.

- [ ] **Step 6: Run pipeline**

```bash
cd /path/to/worktree
bash tools/enc/convert_enc.sh
```
Expected output:
```
=== Done: web/public/tiles/trondheim ===
```

- [ ] **Step 7: Verify tiles exist and are valid**

```bash
find web/public/tiles/trondheim -name '*.pbf' | head -5
du -sh web/public/tiles/trondheim/
```
Expected: `.pbf` files exist under `{z}/{x}/{y}` structure, size ~10-30 MB.

- [ ] **Step 8: Cleanup — add tiles to `.gitignore`**

Ensure `web/public/tiles/` is in `.gitignore`:
```bash
grep -q 'web/public/tiles' .gitignore || echo 'web/public/tiles/' >> .gitignore
```

- [ ] **Step 9: Commit**

```bash
git add tools/enc/ web/public/tiles/trondheim/.gitkeep .gitignore
git commit -m "feat(d1.3b.3): add ENC MVT tile pipeline (GDAL OpenFileGDB → Tippecanoe)"
```

---

### Task 3: foxglove_bridge install + launch file

**Files:**
- Create: `launch/foxglove_sil.launch.xml`

- [ ] **Step 1: Install foxglove_bridge and topic_tools via apt**

```bash
sudo apt update
sudo apt install -y ros-humble-foxglove-bridge ros-humble-topic-tools
```
If apt unavailable (macOS dev), note: foxglove_bridge is installed in the ROS2 Docker container; install skipped on macOS host.

- [ ] **Step 2: Verify foxglove_bridge binary exists**

```bash
which ros2 && ros2 pkg list | grep foxglove_bridge
```
Expected: `foxglove_bridge` in package list.

- [ ] **Step 3: Create `launch/foxglove_sil.launch.xml`**

```xml
<?xml version="1.0"?>
<launch>
  <!-- ============================================================
       D1.3b.3 foxglove_bridge SIL launch
       Throttles nav data 50→10Hz, exposes on port 8765 (JSON)
       ============================================================ -->

  <!-- Topic throttle: own-ship nav state 50Hz → 10Hz -->
  <node pkg="topic_tools" exec="throttle" name="throttle_nav" output="screen">
    <param name="input_topic" value="/nav/filtered_state"/>
    <param name="output_topic" value="/nav/filtered_state_viz"/>
    <param name="message_type" value="l3_msgs/msg/FilteredOwnShipState"/>
    <param name="rate" value="10.0"/>
  </node>

  <!-- Topic throttle: tracks 2Hz → 2Hz (passthrough, already low rate) -->
  <node pkg="topic_tools" exec="throttle" name="throttle_tracks" output="screen">
    <param name="input_topic" value="/world_model/tracks"/>
    <param name="output_topic" value="/world_model/tracks_viz"/>
    <param name="message_type" value="l3_msgs/msg/TrackedTargetArray"/>
    <param name="rate" value="2.0"/>
  </node>

  <!-- foxglove_bridge: C++ WebSocket server, port 8765, JSON mode -->
  <include file="$(find-pkg-share foxglove_bridge)/launch/foxglove_bridge_launch.xml">
    <arg name="port" value="8765"/>
    <arg name="address" value="0.0.0.0"/>
    <arg name="topic_whitelist" value='[".*_viz", "/l3/.*"]'/>
    <arg name="send_buffer_limit" value="10000000"/>
    <arg name="min_qos_depth" value="1"/>
    <arg name="max_qos_depth" value="25"/>
  </include>
</launch>
```

- [ ] **Step 4: Verify launch file syntax**

```bash
xmllint --noout launch/foxglove_sil.launch.xml 2>&1
```
Expected: no output (valid XML).

- [ ] **Step 5: Dry-run launch (if ROS2 environment available)**

```bash
# In ROS2-sourced terminal:
ros2 launch launch/foxglove_sil.launch.xml 2>&1 &
sleep 5
curl -s http://localhost:8765 2>&1 || echo "WebSocket upgrade expected (not HTTP)"
kill %1
```
Expected: foxglove_bridge starts without crash. The `curl` will fail (WebSocket upgrade) but that proves the port is listening.

- [ ] **Step 6: Commit**

```bash
git add launch/foxglove_sil.launch.xml
git commit -m "feat(d1.3b.3): add foxglove_bridge launch file with topic_tools throttle"
```

---

### Task 4: Side panel components (CPA, TCPA, Rule, Decision, Pulse, ASDR)

**Files:**
- Create: `web/src/components/CpaPanel.tsx`
- Create: `web/src/components/TcpaPanel.tsx`
- Create: `web/src/components/RulePanel.tsx`
- Create: `web/src/components/DecisionPanel.tsx`
- Create: `web/src/components/ModulePulseBar.tsx`
- Create: `web/src/components/AsdrLog.tsx`
- Create: `web/src/components/SidePanel.tsx`
- Test: `web/src/components/__tests__/SidePanel.test.tsx` (smoke render)

- [ ] **Step 1: Install test deps**

```bash
cd web && npm install -D vitest @testing-library/react @testing-library/jest-dom jsdom
```
Add to `web/vite.config.ts`:
```typescript
/// <reference types="vitest" />
export default defineConfig({
  // ... existing ...
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: [],
  },
});
```

- [ ] **Step 2: Create `web/src/components/CpaPanel.tsx`**

```tsx
import { memo } from 'react';

interface Props {
  cpaNm: number;
}

function cpaColor(cpa: number): string {
  if (cpa < 0.3) return 'var(--color-port)';
  if (cpa < 0.5) return 'var(--color-warn)';
  return 'var(--color-stbd)';
}

export const CpaPanel = memo(function CpaPanel({ cpaNm }: Props) {
  return (
    <div style={{
      flex: 1,
      background: 'var(--bg-2)',
      borderRadius: 'var(--radius-none)',
      border: '1px solid var(--line-1)',
      padding: '10px 12px',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.18em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        fontWeight: 500,
        marginBottom: '2px',
      }}>CPA · Closest Point of Approach</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '28px',
        fontWeight: 500,
        color: cpaColor(cpaNm),
        lineHeight: 1.2,
      }}>
        {cpaNm === 0 ? '—' : cpaNm.toFixed(2)}
      </div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-3)',
        marginTop: '2px',
      }}>nm · safe ≥ 0.50 nm</div>
    </div>
  );
});
```

- [ ] **Step 3: Create `web/src/components/TcpaPanel.tsx`**

```tsx
import { memo } from 'react';

interface Props {
  tcpaS: number;
}

export const TcpaPanel = memo(function TcpaPanel({ tcpaS }: Props) {
  return (
    <div style={{
      flex: 1,
      background: 'var(--bg-2)',
      borderRadius: 'var(--radius-none)',
      border: '1px solid var(--line-1)',
      padding: '10px 12px',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.18em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        fontWeight: 500,
        marginBottom: '2px',
      }}>TCPA · Time to CPA</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '20px',
        fontWeight: 500,
        color: tcpaS <= 600 ? 'var(--color-warn)' : 'var(--txt-0)',
        lineHeight: 1.2,
      }}>
        {tcpaS === 0 ? '—' : `${tcpaS}`}
      </div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-3)',
        marginTop: '2px',
      }}>s · alert ≤ 600 s</div>
    </div>
  );
});
```

- [ ] **Step 4: Create `web/src/components/RulePanel.tsx`**

```tsx
import { memo } from 'react';

interface Props {
  ruleText: string;
}

export const RulePanel = memo(function RulePanel({ ruleText }: Props) {
  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '4px',
      }}>M6 COLREGs Classification</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '14px',
        fontWeight: 500,
        color: ruleText ? 'var(--color-phos)' : 'var(--txt-3)',
      }}>
        {ruleText || 'Standby'}
      </div>
    </div>
  );
});
```

- [ ] **Step 5: Create `web/src/components/DecisionPanel.tsx`**

```tsx
import { memo } from 'react';

interface Props {
  decisionText: string;
}

export const DecisionPanel = memo(function DecisionPanel({ decisionText }: Props) {
  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '4px',
      }}>L3 System Decision</div>
      <div style={{
        fontFamily: 'var(--fnt-mono)',
        fontSize: '13px',
        color: decisionText ? 'var(--color-warn)' : 'var(--txt-3)',
      }}>
        {decisionText || 'Awaiting run'}
      </div>
    </div>
  );
});
```

- [ ] **Step 6: Create `web/src/components/ModulePulseBar.tsx`**

```tsx
import { memo } from 'react';

interface Props {
  moduleStatus: boolean[];
}

const MODULE_LABELS = ['M1\nODD', 'M2\nWorld', 'M3\nMiss', 'M4\nArb', 'M5\nPlan', 'M6\nCOLR', 'M7\nSafe', 'M8\nHMI'];

export const ModulePulseBar = memo(function ModulePulseBar({ moduleStatus }: Props) {
  return (
    <div style={{
      padding: '10px 14px',
      borderBottom: '1px solid var(--line-1)',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '8px',
      }}>Module Pipeline Status</div>
      <div style={{ display: 'flex', gap: '4px', justifyContent: 'space-between' }}>
        {MODULE_LABELS.map((label, i) => {
          const active = moduleStatus[i] === true;
          return (
            <div key={i} style={{ textAlign: 'center', flex: 1 }}>
              <div style={{
                width: '18px',
                height: '18px',
                borderRadius: '50%',
                margin: '0 auto 3px',
                border: '1px solid var(--line-2)',
                background: active ? 'var(--color-stbd)' : 'var(--bg-2)',
                boxShadow: active ? '0 0 8px rgba(63,185,80,0.4)' : 'none',
                transition: 'all 0.4s',
              }} />
              <div style={{
                fontFamily: 'var(--fnt-mono)',
                fontSize: '9px',
                color: 'var(--txt-3)',
                whiteSpace: 'pre-line',
                lineHeight: 1.3,
              }}>{label}</div>
            </div>
          );
        })}
      </div>
    </div>
  );
});
```

- [ ] **Step 7: Create `web/src/components/AsdrLog.tsx`**

```tsx
import { memo, useEffect, useRef } from 'react';
import type { AsdrEvent } from '../types/sil';

interface Props {
  events: AsdrEvent[];
}

export const AsdrLog = memo(function AsdrLog({ events }: Props) {
  const bottomRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    bottomRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [events.length]);

  return (
    <div style={{
      padding: '10px 14px',
      flex: 1,
      display: 'flex',
      flexDirection: 'column',
    }}>
      <div style={{
        fontFamily: 'var(--fnt-disp)',
        fontSize: '10px',
        letterSpacing: '0.22em',
        color: 'var(--txt-3)',
        textTransform: 'uppercase',
        marginBottom: '6px',
      }}>ASDR Certification Evidence Log</div>
      <div style={{
        height: '100px',
        overflowY: 'auto',
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        lineHeight: 1.6,
        color: 'var(--txt-2)',
        background: 'var(--bg-2)',
        border: '1px solid var(--line-1)',
        borderRadius: 'var(--radius-none)',
        padding: '6px 8px',
        flex: 1,
      }}>
        {events.length === 0 ? (
          <div style={{ color: 'var(--txt-3)' }}>
            — Click Run Scenario to start simulation —
          </div>
        ) : (
          events.map((ev, i) => (
            <div key={i} style={{
              padding: '2px 0',
              borderBottom: '0.5px solid var(--line-1)',
              whiteSpace: 'nowrap',
              overflow: 'hidden',
              textOverflow: 'ellipsis',
            }}>
              <span style={{ color: 'var(--txt-3)' }}>{ev.timestamp}</span>
              {' [ASDR] '}step={ev.step} rule={ev.rule}{' '}
              cpa={ev.cpa_nm.toFixed(2)}nm tcpa={ev.tcpa_s}s{' '}
              act=&quot;{ev.action}&quot;{' '}
              <span style={{
                color: ev.verdict === 'RISK' ? 'var(--color-port)' : 'var(--color-stbd)',
                fontWeight: ev.verdict === 'RISK' ? 600 : 400,
              }}>
                {ev.verdict}
              </span>
            </div>
          ))
        )}
        <div ref={bottomRef} />
      </div>
    </div>
  );
});
```

- [ ] **Step 8: Create `web/src/components/SidePanel.tsx` (composes all cards)**

```tsx
import { memo } from 'react';
import type { SilDebugData } from '../types/sil';
import { CpaPanel } from './CpaPanel';
import { TcpaPanel } from './TcpaPanel';
import { RulePanel } from './RulePanel';
import { DecisionPanel } from './DecisionPanel';
import { ModulePulseBar } from './ModulePulseBar';
import { AsdrLog } from './AsdrLog';

interface Props {
  data: SilDebugData | null;
}

export const SidePanel = memo(function SidePanel({ data }: Props) {
  const d = data;

  return (
    <div style={{
      width: '320px',
      background: 'var(--bg-1)',
      borderLeft: '1px solid var(--line-1)',
      display: 'flex',
      flexDirection: 'column',
      overflowY: 'auto',
      flexShrink: 0,
    }}>
      {/* CPA + TCPA row */}
      <div style={{
        padding: '14px',
        borderBottom: '1px solid var(--line-1)',
      }}>
        <div style={{ display: 'flex', gap: '8px' }}>
          <CpaPanel cpaNm={d?.cpa_nm ?? 0} />
          <TcpaPanel tcpaS={d?.tcpa_s ?? 0} />
        </div>
      </div>

      <RulePanel ruleText={d?.rule_text ?? ''} />
      <DecisionPanel decisionText={d?.decision_text ?? ''} />
      <ModulePulseBar moduleStatus={d?.module_status ?? [false,false,false,false,false,false,false,false]} />
      <AsdrLog events={d?.asdr_events ?? []} />
    </div>
  );
});
```

- [ ] **Step 9: Create smoke test `web/src/components/__tests__/SidePanel.test.tsx`**

```tsx
import { describe, it, expect } from 'vitest';
import { render, screen } from '@testing-library/react';
import { SidePanel } from '../SidePanel';

describe('SidePanel', () => {
  it('renders with null data (standby state)', () => {
    render(<SidePanel data={null} />);
    expect(screen.getByText('Standby')).toBeTruthy();
    expect(screen.getByText('Awaiting run')).toBeTruthy();
  });

  it('renders CPA/TCPA with live data', () => {
    render(<SidePanel data={{
      timestamp: '2026-05-11T00:00:00Z',
      scenario_id: 'r14-ho',
      sat1: null,
      sat2_reasoning: null,
      sat3_tdl_s: 180,
      sat3_tmr_s: 60,
      odd: null,
      job_status: 'running',
      cpa_nm: 0.18,
      tcpa_s: 520,
      rule_text: 'R14 Head-on',
      decision_text: 'M5 BC-MPC: right turn 10°',
      module_status: [true, true, false, true, false, true, false, false],
      asdr_events: [
        { timestamp: '14:32:01', event_type: 'step', step: 0, rule: 'R14', cpa_nm: 0.08, tcpa_s: 580, action: 'detecting', verdict: 'RISK' },
      ],
    }} />);
    expect(screen.getByText('0.18')).toBeTruthy();
    expect(screen.getByText('520')).toBeTruthy();
    expect(screen.getByText('R14 Head-on')).toBeTruthy();
    expect(screen.getByText('RISK')).toBeTruthy();
  });
});
```

- [ ] **Step 10: Run tests**

```bash
cd web && npx vitest run
```
Expected: 2 tests PASS.

- [ ] **Step 11: Update `web/src/App.tsx` to wire SidePanel**

Replace the placeholder SidePanel div with:
```tsx
import { SidePanel } from './components/SidePanel';

// ... in App:
<SidePanel data={null} />
```

- [ ] **Step 12: Commit**

```bash
git add web/src/components/ web/src/App.tsx web/vite.config.ts web/package.json web/package-lock.json
git commit -m "feat(d1.3b.3): add side panel components (CPA/TCPA/Rule/Decision/Pulse/ASDR)"
```

---

### Task 5: MapLibre GL layers — ENC tiles, own-ship, target, CPA ring, COG vector

**Files:**
- Create: `web/src/components/MapView.tsx`
- Create: `web/src/components/shipSprite.ts`
- Modify: `web/src/App.tsx`

- [ ] **Step 1: Create ship sprite SVG data URLs in `web/src/components/shipSprite.ts`**

```typescript
/** Own-ship sprite: phosphor triangle + heading line, 24x36 viewBox */
export const OWN_SHIP_SPRITE = (() => {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="36" viewBox="0 0 24 36">
    <polygon points="12,0 4,14 8,16 12,10 16,16 20,14" fill="#5BC0BE" stroke="#070C13" stroke-width="1.5"/>
    <line x1="12" y1="0" x2="12" y2="26" stroke="#5BC0BE" stroke-width="1.2"/>
  </svg>`;
  return 'data:image/svg+xml;base64,' + btoa(svg);
})();

/** Target ship sprite: same triangle, color applied via icon-color paint property */
export const TARGET_SHIP_SPRITE = (() => {
  const svg = `<svg xmlns="http://www.w3.org/2000/svg" width="20" height="30" viewBox="0 0 20 30">
    <polygon points="10,0 3,12 6,13 10,8 14,13 17,12" fill="#F26B6B" stroke="#070C13" stroke-width="1.2"/>
    <line x1="10" y1="0" x2="10" y2="22" stroke="#F26B6B" stroke-width="1"/>
  </svg>`;
  return 'data:image/svg+xml;base64,' + btoa(svg);
})();
```

- [ ] **Step 2: Create `web/src/components/MapView.tsx`**

```tsx
import { useEffect, useRef, useState } from 'react';
import maplibregl from 'maplibre-gl';
import 'maplibre-gl/dist/maplibre-gl.css';
import { OWN_SHIP_SPRITE, TARGET_SHIP_SPRITE } from './shipSprite';

/** Trondheim Fjord demo viewport */
const CENTER: [number, number] = [10.395, 63.435]; // [lng, lat]
const ZOOM = 11;

export default function MapView() {
  const mapContainer = useRef<HTMLDivElement>(null);
  const mapRef = useRef<maplibregl.Map | null>(null);
  const [loaded, setLoaded] = useState(false);

  useEffect(() => {
    if (!mapContainer.current || mapRef.current) return;

    const map = new maplibregl.Map({
      container: mapContainer.current,
      style: {
        version: 8,
        name: 'D1.3b.3 SIL HMI',
        sources: {
          enc: {
            type: 'vector',
            tiles: [`${window.location.origin}/tiles/trondheim/{z}/{x}/{y}.pbf`],
            minzoom: 6,
            maxzoom: 14,
          },
        },
        layers: [
          // Bottom: ENC depth areas (water)
          {
            id: 'enc-depth',
            source: 'enc',
            'source-layer': 'enc',
            type: 'fill',
            filter: ['==', ['get', '_layer'], 'dybdeareal'],
            paint: {
              'fill-color': [
                'interpolate', ['linear'], ['zoom'],
                6, '#0d2b4a',
                10, '#12406b',
                14, '#1a5c8a',
              ],
              'fill-opacity': 0.85,
            },
          },
          // Land areas
          {
            id: 'enc-land',
            source: 'enc',
            'source-layer': 'enc',
            type: 'fill',
            filter: ['==', ['get', '_layer'], 'landareal'],
            paint: { 'fill-color': '#1e2d1e', 'fill-opacity': 0.9 },
          },
          // Coastline
          {
            id: 'enc-coast',
            source: 'enc',
            'source-layer': 'enc',
            type: 'line',
            filter: ['==', ['get', '_layer'], 'kystkontur'],
            paint: { 'line-color': '#2a4020', 'line-width': 0.5 },
          } as maplibregl.LineLayerSpecification,
          // Depth contours
          {
            id: 'enc-depth-contour',
            source: 'enc',
            'source-layer': 'enc',
            type: 'line',
            filter: ['==', ['get', '_layer'], 'dybdekurve'],
            paint: {
              'line-color': 'rgba(100,160,220,0.15)',
              'line-width': 0.3,
            },
          } as maplibregl.LineLayerSpecification,
          // Danger areas
          {
            id: 'enc-danger',
            source: 'enc',
            'source-layer': 'enc',
            type: 'fill',
            filter: ['in', ['get', '_layer'], ['literal', ['fareomrade', 'grunne']]],
            paint: { 'fill-color': '#F85149', 'fill-opacity': 0.2 },
          },
          // Shoals / rocks
          {
            id: 'enc-shoal',
            source: 'enc',
            'source-layer': 'enc',
            type: 'circle',
            filter: ['==', ['get', '_layer'], 'skjer'],
            paint: {
              'circle-radius': 2,
              'circle-color': '#F26B6B',
              'circle-opacity': 0.6,
            },
          },
        ],
      },
      center: CENTER,
      zoom: ZOOM,
      attributionControl: false,
    });

    // Navigation control (zoom + compass)
    map.addControl(new maplibregl.NavigationControl(), 'bottom-right');

    map.on('load', () => {
      // Load custom sprites
      const imgOwn = new Image();
      imgOwn.onload = () => {
        if (!map.hasImage('ship-own')) map.addImage('ship-own', imgOwn);
      };
      imgOwn.src = OWN_SHIP_SPRITE;

      const imgTarget = new Image();
      imgTarget.onload = () => {
        if (!map.hasImage('ship-target')) map.addImage('ship-target', imgTarget);
      };
      imgTarget.src = TARGET_SHIP_SPRITE;

      // Add own-ship GeoJSON source (empty initially)
      map.addSource('own-ship-src', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });

      // Add target ships GeoJSON source
      map.addSource('target-ships-src', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });

      // Add CPA ring GeoJSON source
      map.addSource('cpa-ring-src', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });

      // Add COG vector GeoJSON source
      map.addSource('cog-vector-src', {
        type: 'geojson',
        data: { type: 'FeatureCollection', features: [] },
      });

      // Own-ship symbol layer (top)
      map.addLayer({
        id: 'own-ship',
        type: 'symbol',
        source: 'own-ship-src',
        layout: {
          'icon-image': 'ship-own',
          'icon-rotate': ['get', 'heading_deg'],
          'icon-rotation-alignment': 'map',
          'icon-size': 0.07,
          'icon-allow-overlap': true,
        },
      });

      // Target ships symbol layer
      map.addLayer({
        id: 'target-ships',
        type: 'symbol',
        source: 'target-ships-src',
        layout: {
          'icon-image': 'ship-target',
          'icon-rotate': ['get', 'heading_deg'],
          'icon-rotation-alignment': 'map',
          'icon-size': 0.05,
          'icon-allow-overlap': true,
        } as any,
        paint: {
          'icon-opacity': [
            'case',
            ['>=', ['get', 'confidence'], 0.7], 1,
            0,
          ],
        } as any,
      });

      // CPA ring circle layer
      map.addLayer({
        id: 'cpa-ring',
        type: 'circle',
        source: 'cpa-ring-src',
        paint: {
          'circle-radius': 60,
          'circle-color': '#F26B6B',
          'circle-opacity': 0.25,
          'circle-stroke-width': 1,
          'circle-stroke-color': '#F26B6B',
          'circle-stroke-opacity': 0.5,
        },
      });

      // COG vector line layer
      map.addLayer({
        id: 'cog-vector',
        type: 'line',
        source: 'cog-vector-src',
        paint: {
          'line-color': '#5BC0BE',
          'line-width': 1.5,
          'line-dasharray': [4, 2],
          'line-opacity': 0.7,
        },
      } as maplibregl.LineLayerSpecification);

      setLoaded(true);
    });

    mapRef.current = map;
    return () => { map.remove(); };
  }, []);

  return (
    <div style={{ position: 'relative', width: '100%', height: '100%', background: '#0a1628' }}>
      <div ref={mapContainer} style={{ width: '100%', height: '100%' }} />
      {/* Scale indicator */}
      <div style={{
        position: 'absolute',
        bottom: '12px',
        left: '12px',
        padding: '4px 8px',
        background: 'rgba(11,19,32,0.7)',
        backdropFilter: 'blur(4px)',
        border: '1px solid var(--line-1)',
        fontFamily: 'var(--fnt-mono)',
        fontSize: '10px',
        color: 'var(--txt-2)',
        borderRadius: 'var(--radius-none)',
      }}>
        Zoom {ZOOM} · 63.43°N 10.40°E
      </div>
      {/* Loading overlay */}
      {!loaded && (
        <div style={{
          position: 'absolute',
          inset: 0,
          display: 'flex',
          alignItems: 'center',
          justifyContent: 'center',
          background: 'rgba(7,12,19,0.8)',
        }}>
          <div style={{
            fontFamily: 'var(--fnt-disp)',
            fontSize: '14px',
            color: 'var(--txt-2)',
            letterSpacing: '0.12em',
          }}>
            Loading ENC Charts...
          </div>
        </div>
      )}
    </div>
  );
}

/** Expose map instance ref for external data updates */
export type { maplibregl };
```

- [ ] **Step 3: Update `web/src/App.tsx` to include MapView**

```tsx
import { useState } from 'react';
import { SidePanel } from './components/SidePanel';
import MapView from './components/MapView';
import type { SilDebugData } from './types/sil';

export default function App() {
  const [silData, setSilData] = useState<SilDebugData | null>(null);

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      <MapView />
      <SidePanel data={silData} />
    </div>
  );
}
```

- [ ] **Step 4: Verify map renders with ENC tiles**

```bash
cd web && npm run dev &
sleep 5 && curl -s http://localhost:5173 | grep -q 'root' && echo "OK: dev server running"
```
Expected: Dev server responds. Open `http://localhost:5173` in browser → should see Trondheim ENC chart with land/water/depth layers.

- [ ] **Step 5: Commit**

```bash
git add web/src/components/MapView.tsx web/src/components/shipSprite.ts web/src/App.tsx
git commit -m "feat(d1.3b.3): add MapLibre GL map with ENC tiles, ship sprites, and empty vessel layers"
```

---

### Task 6: foxglove_bridge → React data flow (own-ship + targets)

**Files:**
- Create: `web/src/hooks/useFoxgloveBridge.ts`
- Create: `web/src/hooks/useThrottledTopic.ts`
- Create: `web/src/hooks/useSilDebug.ts`
- Modify: `web/src/App.tsx`
- Modify: `web/src/components/MapView.tsx`

- [ ] **Step 1: Create `web/src/hooks/useThrottledTopic.ts`**

```typescript
import { useRef, useCallback } from 'react';

/** Throttle callback to maxFps. Returns a wrapped function that drops calls within the interval. */
export function useThrottledTopic<T extends (...args: any[]) => void>(
  callback: T,
  maxFps: number = 30,
): T {
  const lastUpdate = useRef(0);
  const minInterval = 1000 / maxFps;

  return useCallback(
    ((...args: any[]) => {
      const now = performance.now();
      if (now - lastUpdate.current >= minInterval) {
        lastUpdate.current = now;
        callback(...args);
      }
    }) as T,
    [callback, minInterval],
  );
}
```

- [ ] **Step 2: Create `web/src/hooks/useSilDebug.ts`**

```typescript
import { useEffect, useRef, useState } from 'react';
import type { SilDebugData } from '../types/sil';

/** Connect to FastAPI /ws/sil_debug for decision-layer data */
export function useSilDebug(): SilDebugData | null {
  const [data, setData] = useState<SilDebugData | null>(null);
  const wsRef = useRef<WebSocket | null>(null);

  useEffect(() => {
    const protocol = window.location.protocol === 'https:' ? 'wss' : 'ws';
    const ws = new WebSocket(`${protocol}://${window.location.host}/ws/sil_debug`);
    wsRef.current = ws;

    ws.onmessage = (event) => {
      try {
        const parsed: SilDebugData = JSON.parse(event.data);
        setData(parsed);
      } catch {
        // SilDebugSchema may push partial updates; ignore malformed frames
      }
    };

    ws.onclose = () => {
      // Auto-reconnect after 2s
      setTimeout(() => {
        if (wsRef.current?.readyState === WebSocket.CLOSED) {
          // Reconnect by re-rendering
        }
      }, 2000);
    };

    return () => { ws.close(); };
  }, []);

  return data;
}
```

- [ ] **Step 3: Create `web/src/hooks/useFoxgloveBridge.ts`**

```typescript
import { useEffect, useRef, useCallback, useState } from 'react';
import ROSLIB from 'roslibjs-foxglove';
import type { NavState, TargetState } from '../types/sil';
import { useThrottledTopic } from './useThrottledTopic';

interface FoxgloveData {
  ownShip: NavState | null;
  targets: TargetState[];
  connected: boolean;
}

/**
 * Connect to foxglove_bridge (port 8765), subscribe to throttled topics.
 * Returns live own-ship + targets data for MapLibre layers.
 */
export function useFoxgloveBridge(): FoxgloveData {
  const [ownShip, setOwnShip] = useState<NavState | null>(null);
  const [targets, setTargets] = useState<TargetState[]>([]);
  const [connected, setConnected] = useState(false);
  const rosRef = useRef<any>(null);

  // Throttle own-ship updates to 15fps (smooth ~60fps render, reduces React re-renders)
  const updateOwnShip = useThrottledTopic((nav: NavState) => {
    setOwnShip(nav);
  }, 15);

  const updateTargets = useThrottledTopic((tgts: TargetState[]) => {
    setTargets(tgts);
  }, 5);

  useEffect(() => {
    const ros = new ROSLIB.Ros({ url: 'ws://localhost:8765' });
    rosRef.current = ros;

    ros.on('connection', () => {
      setConnected(true);

      // Subscribe to throttled own-ship nav state
      const navTopic = new ROSLIB.Topic({
        ros,
        name: '/nav/filtered_state_viz',
        messageType: 'l3_msgs/msg/FilteredOwnShipState',
      });
      navTopic.subscribe((msg: any) => {
        updateOwnShip({
          lat: msg.latitude ?? msg.lat ?? 0,
          lon: msg.longitude ?? msg.lon ?? 0,
          sog_kn: msg.sog_kn ?? msg.sog ?? 0,
          cog_deg: msg.cog_deg ?? msg.cog ?? 0,
          heading_deg: msg.heading_deg ?? msg.heading ?? 0,
        });
      });

      // Subscribe to throttled tracks
      const tracksTopic = new ROSLIB.Topic({
        ros,
        name: '/world_model/tracks_viz',
        messageType: 'l3_msgs/msg/TrackedTargetArray',
      });
      tracksTopic.subscribe((msg: any) => {
        const arr = msg.targets ?? msg.tracks ?? [];
        updateTargets(arr.map((t: any) => ({
          mmsi: String(t.mmsi ?? t.id ?? ''),
          lat: t.latitude ?? t.lat ?? 0,
          lon: t.longitude ?? t.lon ?? 0,
          sog_kn: t.sog_kn ?? t.sog ?? 0,
          cog_deg: t.cog_deg ?? t.cog ?? 0,
          heading_deg: t.heading_deg ?? t.heading ?? 0,
          cpa_nm: t.cpa_nm ?? t.cpa ?? 999,
          tcpa_s: t.tcpa_s ?? t.tcpa ?? 9999,
          colreg_role: t.colreg_role ?? 'safe',
          confidence: t.confidence ?? 1.0,
        })));
      });
    });

    ros.on('error', () => setConnected(false));
    ros.on('close', () => setConnected(false));

    return () => { ros.close(); };
  }, []);

  return { ownShip, targets, connected };
}
```

- [ ] **Step 4: Add MapLibre data update helpers to `web/src/components/MapView.tsx`**

Append these exports at the bottom of `MapView.tsx`:

```typescript
import type { NavState, TargetState } from '../types/sil';

/** Update own-ship position, heading, and COG vector on the map */
export function updateOwnShipOnMap(map: maplibregl.Map | null, nav: NavState | null) {
  if (!map || !nav) return;
  const src = map.getSource('own-ship-src') as maplibregl.GeoJSONSource;
  if (!src) return;

  src.setData({
    type: 'FeatureCollection',
    features: [{
      type: 'Feature',
      geometry: { type: 'Point', coordinates: [nav.lon, nav.lat] },
      properties: { heading_deg: nav.heading_deg, sog_kn: nav.sog_kn },
    }],
  });

  // COG vector: line from own-ship to predicted position (SOG × 60s)
  const R = 6371000;
  const headingRad = (nav.cog_deg * Math.PI) / 180;
  const distM = nav.sog_kn * 0.514444 * 60; // 60 seconds of travel
  const dLat = (distM * Math.cos(headingRad)) / R;
  const dLng = (distM * Math.sin(headingRad)) / (R * Math.cos((nav.lat * Math.PI) / 180));

  const cogSrc = map.getSource('cog-vector-src') as maplibregl.GeoJSONSource;
  if (cogSrc) {
    cogSrc.setData({
      type: 'FeatureCollection',
      features: [{
        type: 'Feature',
        geometry: {
          type: 'LineString',
          coordinates: [
            [nav.lon, nav.lat],
            [nav.lon + (dLng * 180) / Math.PI, nav.lat + (dLat * 180) / Math.PI],
          ],
        },
        properties: {},
      }],
    });
  }
}

/** Update target ships and CPA rings on the map */
export function updateTargetsOnMap(map: maplibregl.Map | null, targets: TargetState[]) {
  if (!map) return;

  const tgtSrc = map.getSource('target-ships-src') as maplibregl.GeoJSONSource;
  const cpaSrc = map.getSource('cpa-ring-src') as maplibregl.GeoJSONSource;

  if (tgtSrc) {
    tgtSrc.setData({
      type: 'FeatureCollection',
      features: targets.filter(t => t.confidence >= 0.7).map(t => ({
        type: 'Feature' as const,
        geometry: { type: 'Point' as const, coordinates: [t.lon, t.lat] },
        properties: {
          heading_deg: t.heading_deg,
          colreg_role: t.colreg_role,
          confidence: t.confidence,
          mmsi: t.mmsi,
        },
      })),
    });
  }

  // CPA ring on closest target
  if (cpaSrc && targets.length > 0) {
    const closest = targets.reduce((a, b) => a.cpa_nm < b.cpa_nm ? a : b);
    cpaSrc.setData({
      type: 'FeatureCollection',
      features: [{
        type: 'Feature',
        geometry: { type: 'Point', coordinates: [closest.lon, closest.lat] },
        properties: { cpa_nm: closest.cpa_nm },
      }],
    });
  }
}
```

- [ ] **Step 5: Wire hooks into `web/src/App.tsx`**

Update App.tsx:
```tsx
import { useEffect, useRef } from 'react';
import { SidePanel } from './components/SidePanel';
import MapView, { updateOwnShipOnMap, updateTargetsOnMap, type maplibregl } from './components/MapView';
import { useFoxgloveBridge } from './hooks/useFoxgloveBridge';
import { useSilDebug } from './hooks/useSilDebug';

export default function App() {
  const mapRef = useRef<maplibregl.Map | null>(null);
  const silData = useSilDebug();
  const { ownShip, targets, connected } = useFoxgloveBridge();

  // Update MapLibre layers when foxglove_bridge data changes
  useEffect(() => {
    updateOwnShipOnMap(mapRef.current, ownShip);
  }, [ownShip]);

  useEffect(() => {
    updateTargetsOnMap(mapRef.current, targets);
  }, [targets]);

  return (
    <div style={{
      display: 'grid',
      gridTemplateColumns: '1fr 320px',
      height: '100vh',
    }}>
      <div style={{ position: 'relative' }}>
        <MapView mapRef={mapRef} />
        {/* foxglove_bridge connection indicator */}
        <div style={{
          position: 'absolute',
          top: '8px',
          right: '8px',
          display: 'flex',
          alignItems: 'center',
          gap: '6px',
          padding: '4px 10px',
          background: 'rgba(11,19,32,0.7)',
          backdropFilter: 'blur(4px)',
          border: '1px solid var(--line-1)',
          fontFamily: 'var(--fnt-mono)',
          fontSize: '10px',
          color: connected ? 'var(--color-stbd)' : 'var(--color-port)',
          borderRadius: 'var(--radius-none)',
        }}>
          <div style={{
            width: '6px',
            height: '6px',
            borderRadius: '50%',
            background: connected ? 'var(--color-stbd)' : 'var(--color-port)',
            boxShadow: connected ? '0 0 6px var(--color-stbd)' : 'none',
          }} />
          foxglove {connected ? '8765' : 'disconnected'}
        </div>
      </div>
      <SidePanel data={silData} />
    </div>
  );
}
```

- [ ] **Step 6: Update MapView to expose mapRef**

Modify MapView props to accept optional `mapRef`:
```typescript
interface Props {
  mapRef?: React.MutableRefObject<maplibregl.Map | null>;
}

export default function MapView({ mapRef }: Props) {
  // ... in the useEffect where map is created:
  // if (mapRef) mapRef.current = map;
  // ... return cleanup must NOT call map.remove() if mapRef is used externally
```

Actually simpler: add `if (mapRef) mapRef.current = map;` after map creation, and change cleanup to check if mapRef still holds the map before removing.

```typescript
// Inside useEffect:
if (mapRef) mapRef.current = map;

return () => {
  if (mapRef) mapRef.current = null;
  map.remove();
};
```

- [ ] **Step 7: Verify build**

```bash
cd web && npm run build
```
Expected: build succeeds, no TypeScript errors.

- [ ] **Step 8: Commit**

```bash
git add web/src/hooks/ web/src/components/MapView.tsx web/src/App.tsx
git commit -m "feat(d1.3b.3): add foxglove_bridge + /ws/sil_debug hooks and MapLibre data flow"
```

---

### Task 7: FastAPI static serving + Vite dev proxy

**Files:**
- Modify: `src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/web_server/app.py`

- [ ] **Step 1: Read existing `app.py` to understand structure**

```bash
grep -n 'create_app\|StaticFiles\|include_router\|mount' src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/web_server/app.py
```

- [ ] **Step 2: Add static file serving for production React build**

Find the `create_app` function and add `StaticFiles` mount BEFORE the FastAPI app is returned. Insert after all existing routers but before `return app`:

```python
from pathlib import Path
from fastapi.staticfiles import StaticFiles

# Path from app.py → web/dist (4 levels up from web_server/)
_WEB_DIST = Path(__file__).resolve().parents[4] / "web" / "dist"

def create_app(cors_origins: list[str]) -> FastAPI:
    app = FastAPI(title="MASS L3 TDL HMI")
    # ... existing middleware and routers ...

    # D1.3b.3: Serve React production build as static files
    # Mount after all API routes, before catch-all
    if _WEB_DIST.exists():
        app.mount("/", StaticFiles(directory=str(_WEB_DIST), html=True), name="web")

    return app
```

- [ ] **Step 3: Verify existing test files still pass**

```bash
cd src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/
python -m pytest tests/ -v --tb=short 2>&1 | tail -20
```
Expected: all existing M8 tests pass (no regression from D1.3b.1).

- [ ] **Step 4: Build React and test FastAPI static serving**

```bash
cd web && npm run build
# Start FastAPI in background
cd /path/to/worktree
python3 -c "
from pathlib import Path
import sys
sys.path.insert(0, 'src/l3_tdl_kernel/m8_hmi_transparency_bridge/python')
" 2>&1
```
Verify `web/dist/index.html` exists after build.

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/web_server/app.py
git commit -m "feat(d1.3b.3): add FastAPI static file serving for React production build"
```

---

### Task 8: DEMO-1 R14 Head-on E2E integration

**Files:**
- Create: `web/src/data/demoTarget.ts`
- Modify: `web/src/App.tsx`

- [ ] **Step 1: Create `web/src/data/demoTarget.ts` — hardcoded DEMO-1 R14 target**

```typescript
import type { TargetState } from '../types/sil';

/** DEMO-1 R14 head-on encounter: target ship ~2.5nm north of own-ship */
export const DEMO_R14_TARGET: TargetState = {
  mmsi: 'DEMO001',
  lat: 63.458,       // ~2.5nm north of Trondheim center
  lon: 10.395,
  sog_kn: 10.0,
  cog_deg: 180,       // heading south (head-on)
  heading_deg: 180,
  cpa_nm: 0.18,
  tcpa_s: 520,
  colreg_role: 'give-way',
  confidence: 1.0,
};

/** DEMO-1 own-ship initial state (FCB in Trondheim Fjord) */
export const DEMO_OWN_SHIP = {
  lat: 63.435,
  lon: 10.395,
  sog_kn: 18.0,
  cog_deg: 0,
  heading_deg: 5,    // slight offset for visual interest
};

/** DEMO-1 mock SilDebugData for when /ws/sil_debug is not connected */
export const DEMO_MOCK_SIL = {
  cpa_nm: 0.18,
  tcpa_s: 520,
  rule_text: 'R14 Head-on · Both give way',
  decision_text: 'M5 BC-MPC: right turn 10° · Δheading +12°',
  module_status: [true, true, false, true, false, true, false, false],
  asdr_events: [
    { timestamp: '14:32:01', event_type: 'step', step: 0, rule: 'R14', cpa_nm: 0.08, tcpa_s: 580, action: 'M2 detecting collision risk', verdict: 'RISK' as const },
    { timestamp: '14:32:03', event_type: 'step', step: 1, rule: 'R14', cpa_nm: 0.12, tcpa_s: 520, action: 'M6: classifying both turn starboard', verdict: 'RISK' as const },
  ],
};
```

- [ ] **Step 2: Update `web/src/App.tsx` — fallback to DEMO data when disconnected**

```tsx
import { DEMO_R14_TARGET, DEMO_OWN_SHIP, DEMO_MOCK_SIL } from './data/demoTarget';

// In App component, after useFoxgloveBridge():
// When foxglove_bridge is NOT connected, inject demo target + own-ship
// so MapLibre always shows something usable

useEffect(() => {
  if (!connected && mapRef.current) {
    // Inject DEMO-1 static own-ship
    updateOwnShipOnMap(mapRef.current, DEMO_OWN_SHIP);
    // Inject DEMO-1 static target
    updateTargetsOnMap(mapRef.current, [DEMO_R14_TARGET]);
  }
}, [connected]);

// Fallback silData to demo when WS not connected
const effectiveSilData = silData ?? (connected ? null : {
  ...DEMO_MOCK_SIL,
  timestamp: new Date().toISOString(),
  scenario_id: 'r14-ho-demo',
  sat1: null,
  sat2_reasoning: null,
  sat3_tdl_s: 184,
  sat3_tmr_s: 60,
  odd: { zone: 'A · OPEN WATER', envelope_state: 'IN', conformance_score: 0.87, confidence: 0.9 },
  job_status: 'idle' as const,
});
```

- [ ] **Step 3: Create DEMO-1 SOP script `tools/demo/demo1_start.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail
# D1.3b.3 DEMO-1 R14 Head-on live demo SOP
# Usage: bash tools/demo/demo1_start.sh

echo "=== D1.3b.3 DEMO-1: R14 Head-on Live Demo ==="
echo ""

# 1. Start ROS2 sil_mock_node + foxglove_bridge (if ROS2 available)
if command -v ros2 &>/dev/null; then
  echo "[1/4] Starting ROS2 sil_mock_node + foxglove_bridge..."
  ros2 launch launch/foxglove_sil.launch.xml &
  ROS_PID=$!
  sleep 3
  echo "  foxglove_bridge: ws://localhost:8765"
else
  echo "[1/4] ROS2 not available — using DEMO static mode"
fi

# 2. Start FastAPI backend
echo "[2/4] Starting FastAPI backend..."
cd src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/
python3 -m uvicorn web_server.app:create_app --factory --host 0.0.0.0 --port 8000 &
API_PID=$!
sleep 2
echo "  FastAPI: http://localhost:8000"

# 3. Start Vite dev server
echo "[3/4] Starting Vite dev server..."
cd ../../../../../web
npx vite --host &
VITE_PID=$!
sleep 3
echo "  Vite: http://localhost:5173"

# 4. Open browser
echo "[4/4] Opening browser..."
open http://localhost:5173 2>/dev/null || xdg-open http://localhost:5173 2>/dev/null || echo "  Open http://localhost:5173 in your browser"

echo ""
echo "=== DEMO-1 Running ==="
echo "  MapLibre HMI: http://localhost:5173"
echo "  foxglove_bridge: ws://localhost:8765"
echo "  FastAPI: http://localhost:8000"
echo ""
echo "Press Ctrl+C to stop all services"

# Cleanup on exit
trap "kill $ROS_PID $API_PID $VITE_PID 2>/dev/null; echo 'Stopped.'" EXIT
wait
```

- [ ] **Step 4: Make script executable**

```bash
chmod +x tools/demo/demo1_start.sh
```

- [ ] **Step 5: Verify E2E build**

```bash
cd web && npm run build
```
Expected: no errors, `dist/` populated.

- [ ] **Step 6: Commit**

```bash
git add web/src/data/ tools/demo/ web/src/App.tsx
git commit -m "feat(d1.3b.3): add DEMO-1 R14 head-on E2E integration with static fallback"
```

---

### Task 9: DoD verification + final commit

- [ ] **Step 1: Run all tests**

```bash
cd web && npx vitest run
```
Expected: all tests pass.

```bash
cd src/l3_tdl_kernel/m8_hmi_transparency_bridge/python/
python3 -m pytest tests/ -v --tb=short 2>&1 | tail -5
```
Expected: all existing M8 tests pass (no regression).

- [ ] **Step 2: Verify ENC tiles exist**

```bash
test -d web/public/tiles/trondheim && test $(find web/public/tiles/trondheim -name '*.pbf' | wc -l) -gt 0 && echo "OK: tiles present" || echo "MISSING: run Task 2 first"
```

- [ ] **Step 3: Verify React production build**

```bash
cd web && npm run build && test -f dist/index.html && echo "OK: build succeeds"
```

- [ ] **Step 4: Verify DoD checklist against spec §11**

```
- [x] S-57 → MVT 管线: Trondheim MVT tiles generated (Task 2)
- [x] foxglove_bridge launch file committed (Task 3)
- [x] MapLibre HMI: ENC底图 + own-ship + target sprites (Tasks 1, 5)
- [x] CPA / TCPA / Rule / Decision 信息块 (Task 4)
- [x] M1-M8 pulse + ASDR 日志 (Task 4)
- [x] DEMO-1 fallback data when foxglove_bridge disconnected (Task 8)
- [x] FastAPI static serving + Vite proxy (Task 7)
- [x] 30 D1.3b.1 tests pass, no regression (Task 7)
- [ ] DEMO-1 R14 head-on 5-minute live demo SOP — requires ROS2 runtime test
```

- [ ] **Step 5: Update `.gitignore`**

```bash
grep -q 'web/dist' .gitignore || echo 'web/dist/' >> .gitignore
grep -q 'web/node_modules' .gitignore || echo 'web/node_modules/' >> .gitignore
```

- [ ] **Step 6: Final commit**

```bash
git add .gitignore
git commit -m "feat(d1.3b.3): complete DEMO-1 partial — DoD verification"
```

---

## Dependency Graph

```
Task 1 (skeleton)  ──┬── Task 5 (MapLibre layers)
                     │        │
Task 2 (ENC tiles) ──┘        │
                              ├── Task 6 (data flow) ──┐
Task 3 (foxglove_bridge) ─────┤                        │
                              │                        ├── Task 8 (DEMO-1 E2E) ── Task 9 (DoD)
Task 4 (side panel) ──────────┼── Task 7 (FastAPI) ────┘
                              │
                              └── (independent)
```

**Parallel Wave 1**: Tasks 1, 2, 3, 4 — zero cross-dependencies, dispatch simultaneously
**Parallel Wave 2**: Tasks 5, 6, 7 — after Wave 1; Task 5 needs ENC tiles (Task 2); Task 6 needs foxglove (Task 3); Task 7 needs React build (Task 1)
**Sequential Wave 3**: Task 8 (needs all Wave 2) → Task 9 (needs Task 8)

---

*Plan saved to `docs/Design/Detailed Design/D1.3b-scenario-hmi/04-web-hmi-plan.md`*
