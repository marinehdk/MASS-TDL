# SIL Simulator HMI Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Redesign the SIL simulation dashboard HMI layout by replacing the top redundant pulse bar and vertical right sidebar list with double-collapsed tab rails (symmetrical floating rails on Left/Right sides) and moving the M1-M8 monitor into a bottom-centered diagnostic bar (styled identically to the side rails) with local popovers above active cards.

**Architecture:**
1. **Symmetrical Sidebar Rails**: Symmetrical absolute layouts for left (`left: 20`) and right (`right: 20`) tab rails displaying geometric Lucide icons, vertically centered at `top: 50%`, `transform: translateY(-50%)` to reserve sea map viewing space. Content panels slide out (`left: 100` and `right: 100` respectively) only when clicked.
2. **Cohesive Bottom Diagnostic Bar**: Positioned horizontally centered at the bottom of the map view (`bottom: 20`, `left: 50%`, `transform: translateX(-50%)`), matching the height of bottom-left distance scale and bottom-right map switcher. Background is styled as `rgba(10, 15, 24, 0.9)` matching the side tab rails exactly to maintain aesthetic consistency and distinctiveness from other map operation controls.
3. **吸附式局部诊断气泡 (Floating Popovers)**: Popover elements are rendered directly above the active M1-M8 card, offset geometrically based on index. Anchored with an exact downward-pointing CSS triangle aligned to the center of the clicked button.

**Tech Stack:** React (TypeScript), CSS absolute positioning & flexbox layouts, Lucide React icons,zustand store hooks (`useTelemetryStore`, `useFsmStore`, `useUIStore`).

---

### Task 1: ModulePulseBar Cleanup & Right Sidebar Tab Consolidation

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx`

- [ ] **Step 1: Remove redundant top `<ModulePulseBar />` call**

Find the rendering of `<ModulePulseBar />` in the DOM tree of `SimulationMonitor` and delete it so that duplicate monitoring at the top of the viewport is removed.
```diff
- <ModulePulseBar />
```

- [ ] **Step 2: Restructure `MONITOR_TABS` array to exactly 3 tabs**

Update the global constant array `MONITOR_TABS` at the top of `SimulationMonitor.tsx` to keep only the engineering tabs (`asdr`, `score`, `fault`):
```typescript
const MONITOR_TABS = [
  { id: 'asdr',       label: 'ASDR 记录账本',     icon: <LucideTerminalSquare size={20} /> },
  { id: 'score',      label: '五维实时评分',      icon: <LucideAward size={20} /> },
  { id: 'fault',      label: '故障测试注入',      icon: <LucideZap size={20} /> },
] as const;
```

- [ ] **Step 3: Update `SimulationMonitor` component state and tab panel renderers**

Ensure the right-side content drawer is toggled when the corresponding icon is clicked in the right tab rail, and only processes `asdr`, `score`, and `fault` rendering blocks.
```typescript
{/* Tab Contents */}
<div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
  {/* Tab 1: ASDR Ledger */}
  {activeRightTab === 'asdr' && (
    <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%' }}>
...
```

- [ ] **Step 4: Verify TypeScript compilation of Task 1 changes**

Run command:
`npm run build --prefix web`
Expected: Passes type check without errors in `MONITOR_TABS` properties.

- [ ] **Step 5: Commit changes**
```bash
git add web/src/screens/SimulationMonitor.tsx
git commit -m "refactor(hmi): remove redundant top pulse bar and restrict right sidebar to 3 tabs"
```

---

### Task 2: Implement Symmetrical Collapsible Left Sidebar (Captain Cockpit)

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx`

- [ ] **Step 1: Add Lucide icon imports and define `CAPTAIN_TABS`**

At the top of the file, ensure Lucide icons are imported, and declare the `CAPTAIN_TABS` array containing the 3 captain tabs:
```typescript
import {
  LucideCompass, LucideAlertTriangle, LucideNavigation
} from 'lucide-react';

const CAPTAIN_TABS = [
  { id: 'ship',   label: '本船状态', icon: <LucideCompass size={20} /> },
  { id: 'threat', label: '威胁列表', icon: <LucideAlertTriangle size={20} /> },
  { id: 'avoid',  label: '避碰决策', icon: <LucideNavigation size={20} /> },
] as const;

type CaptainTabId = typeof CAPTAIN_TABS[number]['id'];
```

- [ ] **Step 2: Declare `activeLeftTab` state inside the `SimulationMonitor` component**

```typescript
const [activeLeftTab, setActiveLeftTab] = useState<CaptainTabId | null>(null);
```

- [ ] **Step 3: Render the Symmetrical Left tab rail at `left: 20`**

Create a vertical floating rail symmetrical to the right side, using `rgba(10, 15, 24, 0.9)` background, floating vertically centered on the left:
```typescript
{/* LEFT SIDEBAR (CAPTAIN COCKPIT) */}
{/* Vertical Tab Rail on Left side */}
<div style={{
  position: 'absolute',
  top: '50%',
  left: 20,
  transform: 'translateY(-50%)',
  width: 64,
  height: 'fit-content',
  display: 'flex',
  flexDirection: 'column',
  alignItems: 'center',
  paddingTop: 16,
  paddingBottom: 16,
  gap: 8,
  background: 'rgba(10, 15, 24, 0.9)',
  border: '1px solid var(--line-2)',
  borderRadius: 12,
  transition: 'all 0.2s',
  zIndex: 110
}}>
  {CAPTAIN_TABS.map((tab) => {
    const active = activeLeftTab === tab.id;
    return (
      <button
        key={tab.id}
        title={tab.label}
        onClick={() => setActiveLeftTab(active ? null : tab.id)}
        style={{
          width: 44, height: 44, borderRadius: 8, border: 'none', cursor: 'pointer',
          background: active ? 'rgba(91,192,190,0.15)' : 'transparent',
          color: active ? 'var(--c-phos)' : 'var(--txt-3)',
          display: 'flex', alignItems: 'center', justifyContent: 'center',
          transition: 'all 0.2s',
          borderLeft: active ? '3px solid var(--c-phos)' : '3px solid transparent',
          position: 'relative'
        }}
        className="rail-item-left"
      >
        {tab.icon}
      </button>
    );
  })}
</div>
```

- [ ] **Step 4: Render Symmetrical Left Sidebar content panel floating next to the rail**

Create the sliding panel at `left: 100`, which opens smooth transition slide-out when a Captain tab is active:
```typescript
{/* Collapsible Content Panel on Left side */}
<div style={{
  position: 'absolute',
  top: '50%',
  left: 100,
  width: '320px',
  maxHeight: 'calc(100% - 240px)',
  background: 'rgba(13, 19, 31, 0.95)',
  backdropFilter: 'blur(16px)',
  border: '1px solid var(--line-2)',
  borderRadius: 12,
  display: 'flex',
  flexDirection: 'column',
  transition: 'all 0.3s cubic-bezier(0.4, 0, 0.2, 1)',
  opacity: activeLeftTab ? 1 : 0,
  transform: `translateY(-50%) translateX(${activeLeftTab ? '0' : '-20px'})`,
  pointerEvents: activeLeftTab ? 'auto' : 'none',
  zIndex: 105,
  boxShadow: activeLeftTab ? '20px 0 50px rgba(0,0,0,0.5)' : 'none',
  overflow: 'hidden'
}}>
  {activeLeftTab && (
    <div style={{ display: 'flex', flexDirection: 'column', maxHeight: '100%', overflow: 'hidden', minHeight: 0 }}>
      {/* Header */}
      <div style={{
        padding: '16px 20px', borderBottom: '1px solid var(--line-1)',
        display: 'flex', justifyContent: 'space-between', alignItems: 'center',
        flexShrink: 0
      }}>
        <span style={{
          fontFamily: 'var(--f-disp)', fontSize: 13, fontWeight: 700,
          color: 'var(--txt-1)', letterSpacing: '0.15em'
        }}>
          {CAPTAIN_TABS.find(t => t.id === activeLeftTab)?.label.toUpperCase()}
        </span>
        <button
          onClick={() => setActiveLeftTab(null)}
          style={{
            background: 'transparent', border: 'none', color: 'var(--txt-3)',
            cursor: 'pointer', display: 'flex', alignItems: 'center', justifyContent: 'center',
            padding: 4, borderRadius: '50%'
          }}
        >
          <LucideChevronRight size={16} style={{ transform: 'rotate(180deg)' }} />
        </button>
      </div>
      {/* Contents */}
      <div style={{ padding: 20, overflowY: 'auto', flex: 1, minHeight: 0 }}>
        {activeLeftTab === 'ship' && renderShipStatus()}
        {activeLeftTab === 'threat' && renderThreatList()}
        {activeLeftTab === 'avoid' && renderAvoidanceDecision()}
      </div>
    </div>
  )}
</div>
```

- [ ] **Step 5: Verify build works**

Run:
`npm run build --prefix web`
Expected: Successful compile.

- [ ] **Step 6: Commit Task 2**
```bash
git add web/src/screens/SimulationMonitor.tsx
git commit -m "feat(hmi): implement symmetrical Captain Cockpit left tab rail"
```

---

### Task 3: Cohesive Bottom Bar & Contextual Floating Popovers

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx`

- [ ] **Step 1: Declare state `activeBottomModule` inside `SimulationMonitor`**

```typescript
const [activeBottomModule, setActiveBottomModule] = useState<string | null>(null);
```

- [ ] **Step 2: Render Bottom Card Bar styled identically to side rails**

Style the bottom M1-M8 card bar as a floating segmented rail at `bottom: 20`, centered horizontally (`left: 50%`, `transform: translateX(-50%)`), using the exact same background `rgba(10, 15, 24, 0.9)` and border styles to preserve visual consistency:
```typescript
{/* Center Bottom M1-M8 card bar (Width 580px, centered horizontally) */}
<div style={{
  position: 'absolute',
  bottom: 20,
  left: '50%',
  transform: 'translateX(-50%)',
  display: 'flex',
  background: 'rgba(10, 15, 24, 0.9)',
  border: '1px solid var(--line-2)',
  borderRadius: 12,
  padding: 4,
  gap: 4,
  zIndex: 110,
  backdropFilter: 'blur(12px)',
  width: 580
}}>
  {MODULE_NAMES.map((name, i) => {
    const active = activeBottomModule === name;
    const p = modulePulses.find(x => Number(x.moduleId) === i + 1);
    const color = p ? (HEALTH_COLOR[p.state ?? 0] ?? '#444') : '#333';
    const lat = p?.latencyMs;
    return (
      <div
        key={name}
        onClick={() => setActiveBottomModule(active ? null : name)}
        style={{
          flex: 1,
          height: 38,
          background: active ? 'rgba(45, 212, 191, 0.12)' : 'transparent',
          border: active ? '1px solid var(--c-phos)' : '1px solid transparent',
          borderRadius: 8,
          display: 'flex',
          flexDirection: 'column',
          alignItems: 'center',
          justifyContent: 'center',
          cursor: 'pointer',
          transition: 'all 0.15s ease-out',
          position: 'relative'
        }}
      >
        <span style={{ fontSize: 9, fontWeight: 800, color: active ? 'var(--c-phos)' : 'var(--txt-3)', letterSpacing: '0.05em', marginBottom: 2, fontFamily: 'var(--f-mono)' }}>{name}</span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <span style={{ width: 5, height: 5, borderRadius: '50%', background: color }} />
          <span style={{ fontSize: 8, color: lat != null ? 'var(--txt-2)' : 'var(--txt-3)', fontFamily: 'var(--f-mono)' }}>{lat != null ? `${lat}ms` : '—'}</span>
        </div>
      </div>
    );
  })}
</div>
```

- [ ] **Step 3: Render Floating Contextual Popover Anchored Above clicked card**

When `activeBottomModule` is selected, render a popover floating at `bottom: 68`, with `left` offset dynamically calculated based on module index:
```typescript
{activeBottomModule && (
  <div style={{
    position: 'absolute',
    bottom: 68,
    left: `calc(50% - 290px + ${['M1','M2','M3','M4','M5','M6','M7','M8'].indexOf(activeBottomModule) * 72.5}px + 36px)`,
    transform: 'translateX(-50%)',
    width: '280px',
    background: 'rgba(18, 25, 39, 0.96)',
    border: '1px solid var(--c-phos)',
    borderRadius: 8,
    boxShadow: '0 12px 40px rgba(0,0,0,0.8), 0 0 15px rgba(45,212,191,0.15)',
    zIndex: 150,
    display: 'flex',
    flexDirection: 'column',
    padding: 12,
    gap: 8,
    backdropFilter: 'blur(16px)',
  }}>
    <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', borderBottom: '1px solid rgba(255,255,255,0.06)', paddingBottom: 6 }}>
      <span style={{ fontSize: 11, fontFamily: 'var(--f-disp)', fontWeight: 700, color: 'var(--c-phos)', textTransform: 'uppercase', letterSpacing: '0.05em' }}>
        {/* Render module-specific title */}
      </span>
      <button onClick={() => setActiveBottomModule(null)} style={{ background: 'transparent', border: 'none', color: 'var(--txt-3)', cursor: 'pointer', fontSize: 12 }}>×</button>
    </div>
    {/* Popover detailed metrics (M1 to M8) */}
    {/* Triangular pointer pointing down */}
    <div style={{
      position: 'absolute',
      bottom: -6,
      left: '50%',
      transform: 'translateX(-50%)',
      width: 0, height: 0,
      borderLeft: '6px solid transparent',
      borderRight: '6px solid transparent',
      borderTop: '6px solid var(--c-phos)'
    }} />
  </div>
)}
```

- [ ] **Step 4: Compile-check and verify**

Run:
`npm run build --prefix web`
Expected: Code compiles cleanly.

- [ ] **Step 5: Commit changes**
```bash
git add web/src/screens/SimulationMonitor.tsx
git commit -m "feat(hmi): implement bottom segmented bar and popup popover inspector"
```

---

### Task 4: Complete E2E Verification & Review

- [ ] **Step 1: Run production-grade TypeScript & Vite Build**

Compile the whole React suite locally:
`npm run build --prefix web`
Expected: Exit code 0, bundles created in `web/dist` successfully.

- [ ] **Step 2: Startup development local tile and dev server**

Run the live preview dev server:
`npm run dev:frontend`
Expected: Dev server runs at local viewport.

- [ ] **Step 3: Manually Verify UI Layout in Browser**

Launch and test HMI layout:
1. Verify Symmetrical Sidebars: Left and right vertical tab rails are displayed centered slightly inside the edges (`left: 20` and `right: 20`) showing Lucide icons.
2. Verify Collapsed state: Content panels are completely hidden when no tab is selected, maximizing sea map visibility.
3. Verify Slide out: Clicking any tab slide-opens the floating panel (`left: 100` / `right: 100`) smoothly with no flexbox map shifting.
4. Verify Bottom diagnostic bar: The horizontal segmented bar is aligned in height with distance scale and map switcher, and is styled identically to side rails (`rgba(10, 15, 24, 0.9)` background).
5. Verify Contextual Popovers: Clicking any M1-M8 module card displays the popover card vertically aligned above it with accurate telemetry/FSM values.

- [ ] **Step 4: Commit all modifications and document Walkthrough**

Save walkthrough changes to `walkthrough.md`.
```bash
git status
```
Expected: Clean directory status.
