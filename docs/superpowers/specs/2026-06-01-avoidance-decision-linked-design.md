# Design Specification - Tab 3 Avoidance Decision-making & Bottom M1-M8 Linked Interaction

Implement the complete real-time decision transparency dashboard for Tab 3 (避碰决策) of the Simulation Monitor left sidebar. Align the visual style of Tab 3 cards to be 100% consistent with Tab 1 and Tab 2, and implement a double-way interactive linkage between the Captain's intuitive cards and the M1-M8 developer's detailed telemetry popovers.

## 1. Visual Consistency & Card Layout (Left Sidebar Tab 3)

The card container, indicator bars, fonts, and grid sizes will match Tab 1 ("本船状态") and Tab 2 ("威胁列表") exactly.

### CSS Theme Variables & Classes
We will reuse the following design tokens:
* Background: `rgba(0, 0, 0, 0.2)`
* Border: `1px solid var(--line-1)`
* Margin Bottom: `12px` or `16px`
* Padding: `12px 14px`
* Font Sizes: Labels `9px` (`var(--txt-3)`, spacing `0.05em`), Values `20px` (`#fff`, bold `700`, line-height `1.1`, font-family `var(--f-mono)`).

### The 4 Integrated Decision Cards

#### Card 1: 航行运行包络 [M1 ODD]
* **Indicator Bar Color**: `var(--c-phos)` (#2dd4bf)
* **Title**: `安全运行包络域`
* **Linked Badge**: `[M1 ODD]` (phosphor green, clickable)
* **Grid Fields**:
  1. `包络状态 ENVELOPE`: Displays `IN ODD` (phosphor green) or `OUT ODD` / `TOR` / `MRC` dynamically based on `lifecycleStatus` and FSM state.
  2. `决策状态 FSM`: Displays `TRANSIT` / `COLREG_AVOIDANCE` / `TOR` / `MRC` dynamically from `useFsmStore`.

#### Card 2: 避碰态势与规则 [M6 COLREGs]
* **Indicator Bar Color**: `var(--c-warn)` (#fbbf24)
* **Title**: `会遇规则与规避责任`
* **Linked Badge**: `[M6 COLREGs]` (warning yellow, clickable)
* **Grid Fields**:
  1. `适用规则 RULE`: Dynamic extraction from `sat2?.colregs_chain` layer 2 conclusion (e.g. `Rule 14 (对遇)` / `Rule 15 (交叉)`). Fallback: `Nominal Autopilot`.
  2. `会遇责任 ROLE`: Dynamic extraction from `sat2?.colregs_chain` layer 3 conclusion (e.g. `Give-way (让路船)` in red / `Stand-on (直航船)` in blue). Fallback: `Nominal`.

#### Card 3: 规避操纵建议 [M4/M5 Action]
* **Indicator Bar Color**: `var(--c-info)` (#38bdf8)
* **Title**: `船长规避指令动作`
* **Linked Badge**: `[M4/M5 战术]` (info blue, clickable)
* **Grid Fields**:
  1. `避规舵角 STEER`: Displays steer advice (e.g., `右舵转向 15°` dynamically when `fsmState === 'COLREG_AVOIDANCE'`, otherwise `常规保向`).
  2. `当前速度车钟 THR`: Displays `AH 3` / `STOP` dynamically from `ownShip?.controlState` with SOG in kn.

#### Card 4: 安全防御与预警 [M7/M8 Guard]
* **Indicator Bar Color**: `var(--c-danger)` (#f87171)
* **Title**: `安全防御与预警卫士`
* **Linked Badge**: `[M7/M8 安全]` (danger red, clickable)
* **Grid Fields**:
  1. `安全审计 SOTIF`: Displays SOTIF veto status (e.g. `无安全否决` or `否决重新规划` in red).
  2. `交互警报 ALERT`: Displays active alarm levels from M8 or TOR request countdowns when active.

---

## 2. Linked Interaction Design

We will achieve a deep linked interaction between the intuitive Captain's cards and the detailed M1-M8 bottom panels.

### Left-to-Bottom Linkage (Captain Click)
Clicking any card in Tab 3 or its header `[M1-M8]` badge triggers `setActiveBottomModule(name)`. This immediately:
1. Highlights the clicked module in the bottom bar with `border: 1px solid var(--c-phos)`.
2. Activates the technical telemetry popover above the bottom bar.
3. Automatically closes any open card highlight states before switching.

### Bottom-to-Left Linkage (Developer Click)
When the developer clicks any module in the bottom bar (e.g. `M6`), the active card in Tab 3 corresponding to that module (e.g. the COLREGs Encounter Card) dynamically receives a premium breathing phosphorescent border glow (`box-shadow: 0 0 12px rgba(45, 212, 191, 0.3)` and `border-color: var(--c-phos)`):
```typescript
const isCardGlowActive = (modules: string[]) => {
  return activeBottomModule && modules.includes(activeBottomModule);
};
```
* **M1 Card Glow**: Active when `activeBottomModule === 'M1'` (or `M2`/`M3` routing/guidance).
* **M6 Card Glow**: Active when `activeBottomModule === 'M6'`.
* **M4/M5 Card Glow**: Active when `activeBottomModule === 'M4' || activeBottomModule === 'M5'`.
* **M7/M8 Card Glow**: Active when `activeBottomModule === 'M7' || activeBottomModule === 'M8'`.

---

## 3. Popover Aesthetic Unification

To align the developer's expanded popup layout with the high legibility of the left sidebar, the bottom popover container will:
1. Dynamically shift its border color matching the active module's tier color (`var(--c-phos)`, `var(--c-warn)`, `var(--c-info)`, or `var(--c-danger)`).
2. Format technical rows with `.grid-label` (upper-case small, color `var(--txt-3)`) and `.grid-val` (white bold, size `12px` or `14px`) styling.

---

## 4. Verification & Testing

### Automated Type Safety Check
Run compiler validation in the `web` workspace:
```bash
npx tsc --noEmit
```

### Visual Verification
Deploy the Vite web server, navigate to Tab 3, and confirm:
1. Card components match Tab 1 & Tab 2 layout, spacing, and borders.
2. Clicking left cards toggles bottom modules; clicking bottom modules applies glowing borders to left cards.
3. Popover borders color-shift in harmony with active categories.
