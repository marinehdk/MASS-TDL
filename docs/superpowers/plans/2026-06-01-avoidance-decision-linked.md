# Avoidance Decision Linked Interaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement premium, visual-consistent 4-row grid cards inside Left Sidebar Tab 3 (避碰决策) that represent dynamic Captain-friendly telemetry, fully linked with M1-M8 bottom modules and dynamically shifting popover borders.

**Architecture:** Add state-linked styles and dynamic information card widgets inside SimulationMonitor, allowing cross-panel glow, click-to-open, and color-shifting popover borders.

**Tech Stack:** React, TypeScript, inline styles (for glassmorphism dark-blue aesthetic consistency).

---

### Task 1: Unify Bottom Popover Border Color & Layout

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx:1300-1350`

- [x] **Step 1: Write helper map for active module colors**
  Add the color mapping constant right above the Popover rendering block inside the main `SimulationMonitor` render body:
  ```typescript
  const activeColorMap: Record<string, string> = {
    M1: 'var(--c-phos)',
    M2: 'var(--c-phos)',
    M3: 'var(--c-phos)',
    M4: '#38bdf8',
    M5: '#38bdf8',
    M6: 'var(--c-warn)',
    M7: 'var(--c-danger)',
    M8: 'var(--c-danger)',
  };
  const popoverBorderColor = activeBottomModule ? (activeColorMap[activeBottomModule] || 'var(--c-phos)') : 'var(--c-phos)';
  ```

- [x] **Step 2: Update the popover overlay styling**
  Find the popover element in `SimulationMonitor.tsx` and change its style:
  ```typescript
  border: `1px solid ${popoverBorderColor}`,
  boxShadow: `0 12px 40px rgba(0,0,0,0.8), 0 0 15px ${popoverBorderColor}26`,
  ```

- [x] **Step 3: Format popover rows for high legibility**
  Update the child rows of popover content (e.g. details of M1-M8 details around line 1340-1460) to follow unified label/value styling:
  * Description label: `.grid-label` layout and styling.
  * Description value: bold white/colored status layouts.

- [x] **Step 4: Type-safety compile check**
  Run: `npx tsc --noEmit` in `web/` to verify syntactical correctness.
  Expected: PASS

---

### Task 2: Implement Left Tab 3 Dynamic Decision Cards & Dual-linkage Glow

**Files:**
- Modify: `web/src/screens/SimulationMonitor.tsx:870-930`

- [x] **Step 1: Write card glow style helper**
  Define a style getter inside the `SimulationMonitor` component to handle focus glows when developers click a bottom module:
  ```typescript
  const getCardStyle = (modules: string[], activeColor: string) => {
    const isActive = activeBottomModule && modules.includes(activeBottomModule);
    return {
      background: 'rgba(0,0,0,0.2)',
      border: `1px solid ${isActive ? activeColor : 'var(--line-1)'}`,
      padding: '12px 14px',
      borderRadius: 8,
      display: 'flex',
      flexDirection: 'column',
      cursor: 'pointer',
      boxShadow: isActive ? `0 0 12px ${activeColor}40` : 'none',
      transition: 'all 0.25s ease-out',
      marginBottom: 12,
    };
  };
  ```

- [x] **Step 2: Implement Card 1 (ODD Envelope & FSM State)**
  Modify the `avoid` tab content to render Card 1 styled exactly like Tab 1/2:
  * Steer Indicator Bar: `var(--c-phos)`
  * Badge: `[M1 ODD]` (clickable: switches bottom M1 state)
  * Fields: ODD envelope (`IN ODD` / `OUT`) and FSM state (`TRANSIT` / `COLREG_AVOIDANCE`).

- [x] **Step 3: Implement Card 2 (Encounter Rule & Responsibility)**
  Render Card 2:
  * Steer Indicator Bar: `var(--c-warn)`
  * Badge: `[M6 COLREGs]` (clickable: switches bottom M6 state)
  * Fields: Active rule extracted dynamically from `sat2?.colregs_chain` layer 2; Encounter role (`Give-way` in red / `Stand-on` in blue) extracted from layer 3 conclusion.

- [x] **Step 4: Implement Card 3 (Maneuver Steer Directive)**
  Render Card 3:
  * Steer Indicator Bar: `#38bdf8`
  * Badge: `[M4/M5 战术]` (clickable: switches bottom M4 state)
  * Fields: Steer directive (`右舵转向 15°` when avoiding, else `常规保向`); Speed control throttle and SOG in kn.

- [x] **Step 5: Implement Card 4 (SOTIF Checkers & Alert countdowns)**
  Render Card 4:
  * Steer Indicator Bar: `var(--c-danger)`
  * Badge: `[M7/M8 安全]` (clickable: switches bottom M7 state)
  * Fields: SOTIF veto check rate (`无否决 (0.0%)` or `重新规划`); Alarm/Take-over requests status.

- [x] **Step 6: Run verification check**
  Run: `npx tsc --noEmit` in `web/` to make sure React components compile successfully.
  Expected: PASS

- [x] **Step 7: Commit changes**
  Run:
  ```bash
  git add web/src/screens/SimulationMonitor.tsx web/src/styles/tokens.css
  git commit -m "feat: implement Tab 3 avoidance decision cards and bidirectional linkage with M1-M8"
  ```
