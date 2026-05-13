# SIL HMI Dual-Mode Design Spec — DEMO-1

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-HMI-SPEC-001 |
| 版本 | v1.1 |
| 日期 | 2026-05-13 |
| 状态 | 设计锁定（v1.1 整合 Claude Design bundle 运行时语义） |
| 上游依据 | (a) SIL Architecture Design `docs/Design/SIL/2026-05-12-sil-architecture-design.md` §5/§7; (b) SIL Foundation Plan `docs/superpowers/plans/2026-05-12-sil-foundation.md`; (c) HMI Functional Spec `hmi_functional_spec.md` (commit 044e278); (d) NLM Research: silhil_platform (43 new) + maritime_human_factors (54 new) + maritime_regulations (nlm-ask); (e) Claude Design handoff bundle `colav-simulator` — chat1 (2026-05-12) + chat2 (2026-05-13) + 5 SIL screens JSX prototype（`sil-app.jsx` / `sil-builder.jsx` / `sil-preflight.jsx` / `sil-bridge.jsx` / `sil-report.jsx`） |
| 用户决策 | 2026-05-12: Dual-Mode (Captain ↔ God) + Balanced scope (17 features) + ENC chart persistence；**2026-05-13: Dual-Mode 重新定位为 DNV SIL 双轨观测点**（Captain = Digital Twin Black Box operator-facing；God = Test Management White Box engineer-facing，依据 DNVGL-CG-0264）+ scope 扩至 **28 features** 含完整运行时语义（FSM / ToR Modal / Fault Inject Panel / Conning Bar / Threat Ribbon / Hotkeys / Top Chrome / Run Report 6-lane Timeline + ASDR Ledger） |
| 对应 Branch | `feat/d1.3b.3-sil-hmi-dual-mode` |

---

## 📂 Reference Prototype（**v1.2 顶部 callout — 视觉/结构 ground truth**）

> **本 spec 描述 "意图 / 架构 / 集成"，不再用 ASCII 描述像素级 layout。所有视觉与结构细节以下列文件为权威。**

📁 [`docs/Design/SIL/reference-prototype/`](../../Design/SIL/reference-prototype/) — 用户通过 Claude Design 设计并 export 的 **7 个 babel JSX 源（~2181 行真组件代码）**，覆盖 4 屏 + 顶部 chrome + 共享原子 + Imazu 22 几何数据。

| 文件 | 角色 | 实施 Task 引用 |
|---|---|---|
| `01-hmi-atoms.jsx` (220 行) | 共享视觉原子（StatusPill / Chip / Card / COL palette）| Task 1 (tokens.css 已覆盖) + Task 18 (chrome) |
| `02-sil-imazu.jsx` (80 行) | Imazu 22 例 SVG 几何数据 + COLREGs Rule 标注 | Task 22 (ImazuGrid) |
| `03-sil-app.jsx` (194 行) | 顶级路由 + 4 nav tab + dual clock + state pill + REC indicator | Task 18 (TopChrome) |
| `04-sil-builder.jsx` (453 行) | Screen ① 完整实现：A/B/C steps + Imazu grid + Environment 4 cards + Run settings + SummaryRail | Tasks 11 + 22 |
| `05-sil-preflight.jsx` (309 行) | Screen ② 完整实现：M1-M8 (含 topic/Hz/sub-checks) + 8 命名传感器 + 6 命名 comm-links + LiveLog + GO/NO-GO | Tasks 13 + 23 |
| `06-sil-bridge.jsx` (553 行) | Screen ③ 完整实现：FCB-A5M ownship + autonomy pill + TMR+TOL + Scene panel + 3 target chips + 7-field Conning + Fault catalog + ToR modal | Tasks 10 + 19 + 20 + 21 |
| `07-sil-report.jsx` (372 行) | Screen ④ 完整实现：8 KPI cards + Trajectory replay + Event timeline strip + ASDR ledger (4-level filter + SHA chain verify) | Tasks 12 + 24 |

**冲突解决规则**：spec 文本 vs JSX 实际行为冲突 → **以 JSX 为权威**（除非 JSX 明显是 design-time mock，如 hardcoded fixture data）。

详见 [`reference-prototype/README.md`](../../Design/SIL/reference-prototype/README.md) — 含实施者工作流 + 与 spec v1.1 的 11 项已知差异（v1.2 修订表）。

**本 spec 在 §3.1 / §3.3.6 / §3.3.8 / §3.4.1 / §3.4.2 / §3.4.5 等关键节加入"v1.2 design 校准"小框，覆盖与 reference prototype 不一致的旧描述。**

---

## 0. 设计目标

SIL HMI 作为 MASS L3 TDL 的**算法验证枢纽**，通过 4 个串联屏幕完成"场景定义 → 系统预检 → 实时仿真 → 结果归档"完整闭环。本次 DEMO-1 重塑目标：

1. **DNV SIL 双轨视角** — Captain 视图 = DNVGL-CG-0264 "Virtual World / Digital Twin Black Box" operator-facing（对齐 IEC 62288 §SA + IMO S-Mode + BAM 一致性约束）；God 视图 = "Test Management White Box" engineer-facing（场景管理 / 故障注入 / I/O 信号 trace / 算法状态机透视）
2. **海图连续性** — ENC 海图在所有 4 屏保持一致 viewport 状态，屏间切换无跳变
3. **设计 token 化** — OpenBridge 5.0 + IHO S-52 PresLib 4.0 对齐的完整色彩/字体/间距系统
4. **算法可解释性** — COLREGs 规则扇区叠加、M1-M8 健康下钻、6 维评分实时仪表盘
5. **运行时语义剧情层**（v1.1 NEW）— 5 态场景 FSM（TRANSIT / COLREG_AVOIDANCE / TOR / OVERRIDE / MRC / HANDBACK）+ ToR 模态（≥5s SAT-1 lock + 60s TMR + auto-MRC 超时回退，对齐 Veitch 2024 / IMO MASS Code）+ 故障注入面板（AIS 丢包 / Radar 失效 / ROC 链路中断）+ Conning bar（油门/舵角实时）+ Threat ribbon（目标威胁条）+ 快捷键（T/F/M/H/SPACE）— **两视图共享，不属任一视图独占**

---

## 1. 全局架构决策

### 1.1 海图状态一致性（跨屏 ENC 共享）

**问题**：当前 4 屏各自独立 mount/unmount MapLibre 实例，切换屏幕时海图 viewport 重置。

**方案**：新增 `useMapStore`  Zustand slice，跨屏持久化海图状态。

```typescript
// web/src/store/mapStore.ts (NEW)
interface MapViewport {
  center: [number, number];  // [lng, lat]
  zoom: number;
  bearing: number;           // degrees, 0=North-up
  pitch: number;
}

interface MapStore {
  viewport: MapViewport;
  setViewport: (v: Partial<MapViewport>) => void;
  resetViewport: () => void;
}
```

**规则**：
- 每次 MapLibre `moveend` 事件 → 写入 `useMapStore.viewport`
- 每个屏幕的 `SilMapView` 组件 `mount` 时 → 从 `useMapStore.viewport` 恢复状态
- Screen ② Preflight 无地图，但保留 viewport 不重置
- Screen ④ RunReport 可选显示最终态势快照（使用相同 viewport）

**默认 viewport**（Trondheim SF Bay 初始）：

```typescript
const DEFAULT_VIEWPORT: MapViewport = {
  center: [10.38, 63.44],
  zoom: 12,
  bearing: 0,
  pitch: 0,
};
```

### 1.2 双模式差异矩阵（DNV SIL 双轨观测点框架）

**理论基础**（v1.1 新增）：

| | Captain（Digital Twin / Black Box）| God（Test Management / White Box）|
|---|---|---|
| **DNV 角色** [R-DNV-1] | "Virtual World" 中的 operator/RO，按真实船长视角交互 | "Test Management System" 中的 engineer，监控 I/O / 注入故障 / 评估算法 |
| **BAM 约束** [R-NLM:hf-2] | 严格遵守 MSC.302(87) 一致性 + 单一标准化展示 | 不在 BAM 约束范围（属测试基础设施层）|
| **信息覆盖** | "What captain needs to drive" — SA-1/2 态势 + 决策理由 + 接管告警 | "What engineer needs to debug" — 上 + COLREGs sectors + Imazu 几何 + 6-dim scoring + M1-M8 内部状态 |
| **核心问题** | "captain 能否理解系统在做什么" | "算法是否按预期工作" |
| **场景 FSM / ToR / Fault Inject / Conning / Threat Ribbon** | **均存在**（剧情层不分视图） | **均存在**（剧情层不分视图） |

**视觉差异矩阵**（决定哪些 widget 显隐 / 哪些 layer 渲染）：

| 维度 | Captain | God |
|------|---------|-----|
| 本船位置 | Off-center (bottom 30%) | Center |
| 地图跟随 | Heading-up, follow ownShip | Free pan (North-up default) |
| PPI 距离圈 | Off-center ring center | Centered ring center |
| 罗经花 | Relative bearing (bottom-left) | North-up (bottom-left) |
| COLREGs 扇区叠加 | Hidden | 4-sector (head-on/cross-stbd/cross-port/overtake) |
| Imazu 几何 | Hidden (captain only sees targets) | Scenario-specific overlay |
| 6-dim 实时评分 | Hidden | Bottom-right HUD panel |
| ARPA 目标表 | Collapsible right sidebar（默认收起）| Expanded default |
| M1-M8 下钻 | Pulse dots + tooltip only | Click-to-expand detail panel |
| Fault Inject 面板 | Hidden（captain 不会主动注入故障）| 右侧 docked panel（永显）|
| Conning Bar | 顶部精简（rudder/throttle 数值） | 底部完整（rudder/throttle/RPM/pitch + 历史曲线 60s）|
| Threat Ribbon | 顶部 1 行简化（按 CPA 染色目标 chip） | 顶部 2 行（按 CPA + COLREGs 角色染色 + ETA 倒计时）|
| Top Chrome | 双时钟 + tab + state pill（与 God 一致）| 同上（chrome 跨视图共享）|

**View Mode 切换**：`viewMode: 'captain' \| 'god'` 状态由 `useUIStore` 持有；右上角 toggle 按钮（保留 v1.0 设计）+ 快捷键 `Cmd/Ctrl + Shift + G` 切换。切换不重置 FSM 状态。

### 1.3 路由不变

```
/                   →  redirect → /builder
/builder            →  ① Scenario Builder
/preflight/:runId   →  ② Pre-flight
/bridge/:runId      →  ③ Bridge HMI
/report/:runId      →  ④ Run Report
```

---

## 2. Design Token 系统（**v1.1 修订对齐 Claude Design 输出 HTML ground truth — 2026-05-13**）

> **依据**：用户 export `COLAV SIL Simulator.html`（540KB unbundled HTML）抽取的 `:root` token 块 + 4 个 `.hmi-*` atom 类 + 4 个 keyframes。v1.1 前期 §2.1 基于 OpenBridge 5.0 独立调研选值，与 design 实际不一致；本节为 ground truth。

### 2.1 色彩 Token（ECDIS Night Mode — Bridge HMI 严格语义）

```css
/* web/src/styles/tokens.css (NEW) */

:root {
  /* ── Surface hierarchy (Night Mode default for bridge HMI) ────── */
  --bg-0: #070C13;          /* deepest — canvas */
  --bg-1: #0B1320;          /* panel background */
  --bg-2: #101B2C;          /* card surface */
  --bg-3: #16263A;          /* elevated card */

  /* ── Border hierarchy (纯色非透明，逐级抬升) ─────────────────── */
  --line-1: #1B2C44;        /* subtle */
  --line-2: #243C58;        /* default */
  --line-3: #3A5677;        /* active / focused */

  /* ── Text hierarchy (4 档，注意 txt-0 比我 v1.0 spec 多 1 档) ── */
  --txt-0: #F1F6FB;         /* critical readouts — 最亮，仅 verdict/min CPA 等关键数值 */
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

  /* ── Autonomy level colors (IMO MASS Code 4-level mapping) ──── */
  --c-d4: #3FB950;          /* full auto */
  --c-d3: #79C0FF;          /* supervised — nominal */
  --c-d2: #F0B72F;          /* manual / RO */
  --c-mrc: #F85149;         /* minimum risk condition */

  /* ── Type (3 字体栈，含中文支持) ──────────────────────────────── */
  --f-disp: 'Saira Condensed', 'Noto Sans SC', sans-serif;  /* display: 大字号 / chrome / KPI */
  --f-body: 'Noto Sans SC', 'Saira Condensed', sans-serif;  /* body: 段落 / 中文优先 */
  --f-mono: 'JetBrains Mono', ui-monospace, monospace;       /* mono: 数字 / 代码 / 时钟 */

  /* ── Spacing / radius (strict zero-radius for bridge HMI) ─────── */
  --r-0: 0;                  /* 严格零圆角（桥楼 HMI IEC 标准）*/
  --r-min: 2px;              /* 极小圆角（仅 hover state / chip）*/
  --sp-xs: 4px;
  --sp-sm: 8px;
  --sp-md: 12px;
  --sp-lg: 18px;             /* 注: lg = 18 不是 16（design 实际值）*/
  --sp-xl: 24px;
}

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
```

### 2.1.1 v1.0 → v1.1 token 名称映射（开发者迁移指南）

| v1.0 名（spec 文中残留引用）| v1.1 真名 | 备注 |
|---|---|---|
| `--bg-0` | `--bg-0` | 值变更 `#0b1320` → `#070C13` |
| `--bg-1` | `--bg-1` | 值变更 `#0f1929` → `#0B1320` |
| `--bg-0` | `--bg-0`（同 canvas）| 不再独立 token |
| `--bg-2` | `--bg-2` | 值变更 `#0d1f2d` → `#101B2C` |
| `--line-1` | `--line-1` | 改纯色 `#1B2C44`（不再 rgba 透明） |
| `--line-2` | `--line-2` | 改纯色 `#243C58` |
| `--line-3` | `--line-3` | 改纯色 `#3A5677` |
| `--txt-1` | `--txt-1` | 值变更 `#e6edf3` → `#C5D2E0` |
| `--txt-2` | `--txt-2` | 值变更 `#9ca3af` → `#8A9AAD` |
| `--txt-3` | `--txt-3` | 值变更 `#4b6888` → `#566578` |
| （无）| `--txt-0` | **v1.1 新增最亮档**，用于关键 readout |
| `--c-phos` | `--c-phos` | **语义重定位**: 雷达磷光 + 本船 + 品牌色，`#2dd4bf` → `#5BC0BE` |
| `--c-info` | `--c-info` | 值变更 `#60a5fa` → `#79C0FF` |
| `--c-magenta` | `--c-magenta` | 语义升级为"ECDIS 预测轨迹"，值变更 `#c084fc` → `#D070D0` |
| `--c-stbd` | `--c-d4`（autonomy full-auto）或 `--c-stbd` | **拆解**: scoring 各维不再单一色 |
| `--c-info` | `--c-info` 派生 | 不再独立 token |
| `--c-stbd` | `--c-stbd` 或 `--c-d4` | **海事语义化**: 舷绿 / autonomy |
| `--c-warn` | `--c-warn` 或 `--c-d2` | 值变更 `#fbbf24` → `#F0B72F` |
| `--c-danger` | `--c-port` / `--c-danger` / `--c-mrc` | **拆 3 用途**: 舷红 / 危险 / MRC |
| `--f-body` | `--f-disp` 或 `--f-body` | **拆 2 字体**: Saira Condensed / Noto Sans SC |
| `--f-mono` | `--f-mono` | 同名 |
| `--space-1..6` | `--sp-xs/sm/md/lg/xl` | **5 档非 6 档**，lg = 18 不是 16 |

**全局规则**：spec §3-§8 内残留的 v1.0 token 名称在 Task 1 实施时按上表迁移；token 名 v1.1 起统一用 design 真名。

### 2.2 Atom Classes（4 个 reusable 类）

```css
/* ── Reusable atoms used across all artboards ──────────────────── */
.hmi-surface  { background: var(--bg-0); color: var(--txt-1); font-family: var(--f-body); }
.hmi-mono     { font-family: var(--f-mono); font-variant-numeric: tabular-nums; }
.hmi-disp     { font-family: var(--f-disp); letter-spacing: 0.18em; text-transform: uppercase; }
.hmi-label    { font-family: var(--f-disp); font-size: 9.5px; letter-spacing: 0.22em;
                color: var(--txt-3); text-transform: uppercase; font-weight: 500; }
```

**使用规约**：
- `.hmi-disp` — 标题 / chrome 顶部品牌 / KPI 大数 / chip label（大写 + 0.18em 间距）
- `.hmi-label` — 区块小标签（"ARPA TARGETS" / "SCORING" 等，9.5px + 0.22em）
- `.hmi-mono` — 所有数字读数（时钟 / CPA / TCPA / ROT / RPM / 坐标）
- `.hmi-surface` — 顶层 root container

### 2.3 Keyframes（4 个全局动画）

```css
@keyframes phos-pulse  { 0%, 100% { opacity: 1; } 50% { opacity: 0.45; } }
@keyframes radar-sweep { from { transform: rotate(0deg); } to { transform: rotate(360deg); } }
@keyframes warn-flash  { 0%, 100% { opacity: 1; } 50% { opacity: 0.35; } }
@keyframes scan-line   { 0% { transform: translateX(-100%); } 100% { transform: translateX(100%); } }
```

**用法**：
- `phos-pulse` — 本船标记 / `--c-phos` 元素的"活跃"脉动（如 ACTIVE state pill）
- `radar-sweep` — PPI 雷达扫描线 / preflight loading spinner
- `warn-flash` — TOR modal 10s 内 / MRC 状态 / fault active chip
- `scan-line` — preflight self-check 进度 / loading state

### 2.4 S-52 Display Category → MapLibre Layer Mapping（v1.1 用真 token 名）

| S-52 Category | MapLibre Layer ID | Always Visible? | Paint Color |
|---|---|---|---|
| Display Base | `coastline`, `safety_contour`, `own_ship` | Yes (locked) | `var(--c-phos)` |
| Standard Display | `depth_area`, `land_area`, `buoyage` | Togglable (default on) | S-52 mapped |
| All Other Info | `spot_soundings`, `contour_labels` | Hidden default | S-52 mapped |
| Mariner's Info | `route_line`, `waypoints`, `imazu_geom` | On by default | `var(--c-warn)` / `var(--c-port)` |

### 2.5 字体 Scale（v1.1 校准为 design 实际值）

| Token / 用法 | Size | Family | Usage 举例 |
|---|---|---|---|
| label 微 | 9.5px | `--f-disp` (Saira Condensed) letter-spacing 0.22em uppercase | `.hmi-label`: "ARPA TARGETS" / "SCORING" / "CONNING" 区块小标签 |
| text-2xs | 8px | `--f-mono` | M1-M8 tile 内 KPI / chart annotations |
| text-xs | 10px | `--f-mono` | HUD 数值 / tooltips / ASDR ledger payload preview |
| text-sm | 11-12px | `--f-body` (Noto Sans SC) | 正文 / panel 内容 |
| text-base | 13-14px | `--f-body` 或 `--f-disp` | 顶部信息条 / KPI 值 |
| text-lg | 16-18px | `--f-disp` letter-spacing 0.18em uppercase | Sim time / verdict badge / TOR modal 标题 |
| text-xl | 22-26px | `--f-disp` | TOR TMR 倒计时大字 / Top Chrome state pill |

**关键差异 vs v1.0**：
- 现 spec 增加 9.5px 专用 label 档（matches `.hmi-label`）
- 字号同档可能用不同字体族（数字 mono / 文字 body / 标题 disp）— 不再是"全 mono 字体栈"
- 显示字体 Saira Condensed 强制 uppercase + letter-spacing 0.18em（区别于 body）

---

## 3. 屏幕详细设计

### 3.1 Screen ① — Scenario Builder（**v1.1 重构为 3-Step + 3 Sub-tabs**）

> **🔧 v1.2 design 校准**（依据 `04-sil-builder.jsx` + screenshot）：
> - **Steps 标签 A/B/C 而非 1/2/3**：A · ENCOUNTER / B · ENVIRONMENT / C · RUN（每个含中文副标 "会遇几何 · Imazu 22" 等）
> - **不存在 "3 sub-tabs in ENCOUNTER"**（Template/Procedural/AIS Replay）— design 中 ENCOUNTER 直接是 22-Imazu grid + 详情面板。Procedural / AIS 入口属未来扩展，DEMO-1 仅 Template + Imazu
> - **左侧 sidebar 含独立 SCENARIO LIBRARY 区**（与 Steps stepper 同列下方）：4 个预设 `IM03_Crossing_GiveWay_v2` / `IM07_Overtaken_FogA` / `IM14_Triple_Conflict` / `IM22_Restricted_Narrow`
> - **Imazu 22 名+规则**: 每例标 `IM{NN}` + `R{Rule}` chip + 名称 + ship 配置。COLREGs Rule 范围: R9 / R13 / R14 / R15 / R17 + 复合（如 R14+R15 / R15×2 / R9 复合）
> - **Environment Step B 不含 ODD Level dropdown** — ODD/autonomy 在 Step B 仅作为 **"SCO 区域配置" card** (A-OPEN / B-TSS / C-RESTRICTED)；运行时 autonomy level (D2/D3/D4/MRC) 由 Bridge 顶部 pill 显示
> - **Environment 实际 4 cards**: WIND/SEA/CURRENT · 能见度与气象 · SCO 区域配置 · 本船·动力学（含 FCB-NSM / CONT-100M / TUG-20M 船型选项）
> - 所有视觉细节以 `04-sil-builder.jsx` 为准

**路由**: `/#/builder`

**布局**: 顶部 3-Step Stepper（① ENCOUNTER · ② ENVIRONMENT · ③ RUN）+ 主区域当前 step 内容 + 右栏 320px 摘要侧栏（Summary Rail）+ 底部 Validate → Pre-flight CTA。ENC 地图预览在所有 step 共享右上区域（4:3 浮动卡片）。

#### 3.1.1 Step ① ENCOUNTER — 会遇场景定义

**3 Sub-tabs**（顶部 Tab 切换，对齐 colav-simulator 业务能力 [R-Design-1]）：

| Sub-tab | 内容 | 数据来源 |
|---|---|---|
| **Template** | 11 个标准场景 chip（head_on / crossing_give_way / overtaking / 8 个 R-Sxx）+ Imazu 22 例缩略图 4×6 grid（每个 cell 24px 几何缩略 SVG + IMA-01..22 label）| `scenarios/*.yaml` + `imazu22_v1.0.yaml` |
| **Procedural** | 程序化随机生成。控件: `type: HO/CR/OT` radio + bearing range slider + course range slider + dist range slider + n_ships dropdown (1–3) + n_episodes input | `behavior_generator` |
| **AIS Replay** | （DEMO-1 静态展示, Phase 2 接实数据）AIS 数据集 dropdown (Møre 2019-02-09 / Vestland 2021-08-01) + 时间窗 picker + MMSI multi-select + bbox 选择器 + encounter 抽取预览 thumbnail | `data/ais/*.csv` placeholder |

**选中后**: 右上 ENC 预览叠加 Imazu 几何（本船 + 1-3 目标起始位置标记 + 预期轨迹虚线 + 场景标签）。`data-testid="encounter-tab-{template\|procedural\|ais}"`

#### 3.1.2 Step ② ENVIRONMENT — 环境 / 传感 / 船体 / ODD

**4 卡片网格**（2×2）：

| 卡片 | 字段 |
|---|---|
| **Sea State** | 风向 (°) + 风速 (kn) + Beaufort 等级 + 海况 (Douglas 0–9) + 能见度 (nm) + 流向/流速 |
| **Sensor Config** | Radar: max_range (nm) + clutter level + Pd 滑块；AIS: class A/B + range；GNSS: 噪声 σ；Camera: FOV / FPS |
| **Hull Model** | 船体类型 (viknes / csog / FCB) + Loa / Beam / Draft + 推进 (single/twin/azimuth) + 操舵 (rudder/azipod) |
| **ODD Level** | ODD 等级 dropdown (E-1..E-3 / T-1..T-3 / H-1..H-3) + 9 轴 envelope 可视化 + 触发 OUT-of-ODD 预演按钮 |

#### 3.1.3 Step ③ RUN — 仿真参数

| 卡片 | 字段 |
|---|---|
| **Timing** | t_start / t_end / dt_sim (s) + UTM zone + map_origin + map_size |
| **IvP Weights** | 6 维 weight slider (Safety / Rule / Delay / Magnitude / Phase / Plausibility) 默认 1/6 等权 + Reset 按钮 |
| **Fault Script** | 故障注入脚本编辑器（YAML 片段）+ 预设按钮: AIS dropout @ t=30s / Radar fail @ t=60s / ROC link loss @ t=90s |
| **Pass Criteria** | CPA threshold (nm) + max ROT (°/min) + max heel + rule_violation count threshold + verdict 模式 (auto / manual) |

#### 3.1.4 Summary Rail（右栏 320px，所有 step 可见）

- 当前选择摘要 chip（场景名 + 目标数 + 海况 + ODD 等级）
- ENC 加载状态 indicator
- 预估仿真时长（基于 `t_end - t_start` / 速率）
- Validate 按钮（红 / 黄 / 绿状态：red=未填必填 / yellow=warnings / green=可启动）
- `Run Pre-flight →` CTA（仅绿时可点）

#### 3.1.5 数据流

- `GET /api/v1/scenarios` → scenario list
- `GET /api/v1/scenarios/:id` → YAML + geometry
- `GET /api/v1/imazu/{01..22}` → Imazu 几何（DEMO-1 hard-coded from `imazu22_v1.0.yaml`）
- `POST /api/v1/scenarios/validate` → validation errors（强类型 schema 检查）
- `POST /api/v1/scenarios` → save + hash（SHA-256）
- 海图 viewport 保存：map `moveend` → `useMapStore.setViewport()`

#### 3.1.6 测试点

- `data-testid="builder-step-{1\|2\|3}"` — 3 步切换可点
- `data-testid="imazu-grid"` — Imazu 22 例网格存在
- `data-testid="summary-rail"` — 右栏存在
- `data-testid="validate-cta"` — Validate 按钮按状态变色

### 3.2 Screen ② — Pre-flight（**v1.1 扩展为 M1-M8 + 8 Sensors + 6 Comm-links + Live Log**）

> **🔧 v1.2 design 校准**（依据 `05-sil-preflight.jsx`）：
> - **Module cards 字段**: 除 `{id, name}` 外含 `src` (DDS topic path，例 `sango_adas/m1`) · `hz` (publish 频率) · `checks[]` (sub-check 名称数组)
> - **M5 含展开子检查**: BC-MPC init · warm-start · solver health
> - **Sensors 用具体 make/model**（不是抽象 "GNSS/Gyro/IMU/Radar/AIS/Camera/LiDAR/ECDIS" 8 dots）：
>   - GNSS-A: `TrimMV BD992` (RTK FLOAT · 24 sats · backup)
>   - GNSS-B: `Septentrio mosaic`
>   - IMU: `Xsens MTi-680G` (200Hz · bias compensated)
>   - RADAR: `JRC JMA-9230` (27 RPM · 24 nm)
>   - AIS: `Furuno FA-170` (常出现 WARN)
>   - LiDAR: `Velodyne VLS-128`
>   - (其余 sensor 见 `MODULE_LIST` + `SENSOR_LIST` in JSX)
> - **Live log 格式**: `[HH:MM:SS.ms] preflight.runner: <message>` (例 `[06:42:01.015] preflight.runner: session SIL-0427 attached to scenario`)
> - **Comm-link 命名**: 含 "Iridium backup 高延迟" 等真实标签
> - **顶部 actions**: RUN CHECKS / AUTHORIZE → BRIDGE 两按钮
> - **底部 actions**: ← SCENARIO / START BRIDGE RUN
> - **底部 hotkey hints**: SPACE 运行/暂停 · R 重启检查
> - 所有视觉细节以 `05-sil-preflight.jsx` 为准

**路由**: `/#/preflight/:scenarioId`

#### 3.2.1 布局

```
┌─ Top: 场景元信息 (场景名 + hash + 海区缩略图 thumbnail) ──────────────┐
├─ Left col (40%): Module Readiness Tiles ──┬─ Right col (60%) ──────┤
│   ┌─ M1-M8 8-tile grid ─────────────┐    │ Live Log Stream         │
│   │ M1 ODD          ● PASS          │    │ [2026-05-13 09:23:14] M1 self-check OK│
│   │ M2 World Model  ● PASS          │    │ [09:23:15] Loading scenario... │
│   │ ...                              │    │ [09:23:16] ENC tiles cached  │
│   │ M8 HMI          ◐ CHECKING      │    │ [09:23:17] M2 fusion init... │
│   └─────────────────────────────────┘    │ ...                          │
│   ┌─ 8 Sensors row ─────────────────┐    │                              │
│   │ GNSS Gyro IMU Radar AIS Cam LiDAR ECDIS │                          │
│   │  ●    ●   ●    ●    ●   ●   ●    ●     │                          │
│   └─────────────────────────────────┘    │                              │
│   ┌─ 6 Comm-links row ──────────────┐    │                              │
│   │ DDS-Bus L4↓ L2↑ Param-DB ROC-Lnk ASDR │                          │
│   │   ●     ●   ●    ●        ●      ●   │                          │
│   └─────────────────────────────────┘    │                              │
└─ Bottom: GO / NO-GO Gate + Countdown ─────────────────────────────────┘
```

#### 3.2.2 自检序列

| Phase | 内容 | 持续 |
|---|---|---|
| 1. **Cleanup** | 清空 telemetry store / 释放上次仿真资源 | <0.5s |
| 2. **Configure** | 注入 scenario YAML → orchestrator + parameter database 同步 | 0.5-1s |
| 3. **Module Self-Check (M1-M8)** | 8 模块并发自检（每模块 publish `module_ready` event）| 1-2s |
| 4. **Sensor Init** | 8 传感器初始化（GNSS lock / Gyro warm-up / Radar PRF / AIS heartbeat ...）| 0.5-1.5s |
| 5. **Comm-link Verify** | DDS-Bus topic 探测 / L4↓ L2↑ heartbeat / Param-DB schema check / ROC-Lnk ping / ASDR writable | 0.5s |
| 6. **GO / NO-GO Gate** | 若任一项 fail → **NO-GO**（红色 + "Return to Builder" 按钮）；全 pass → 绿色 Activate 按钮 | — |
| 7. **Countdown 3s** | 按 Activate 后 3-2-1 倒数 | 3s |
| 8. **Activate** | 切换到 `/bridge/:runId`，写入 ASDR `simulation_started` event | — |

#### 3.2.3 Module Readiness Tile

每 tile 显示: module_id + 健康灯 (●绿/PASS · ◐黄/CHECKING · ●红/FAIL) + 关键 KPI (latency_ms / drops / queue_depth) + 点击 → 弹出 detail tooltip。

#### 3.2.4 Sensor & Comm-link Status

每 dot 状态：●green=ok / ●amber=warning / ●red=fail / ○gray=未启用。Hover 显示 last_update timestamp + raw status payload。

#### 3.2.5 Live Log Stream

- 自动滚动到底部 + 暂停按钮 + 关键字过滤（warn / error / module-specific）
- 颜色规则: INFO `--txt-2` / WARN `--c-warn` / ERROR `--c-danger`
- 数据源: 临时 WS topic `/sil/preflight_log` @ event 频率（最多 50/s 限流）

#### 3.2.6 测试点

- `data-testid="preflight-modules"` — M1-M8 grid 存在
- `data-testid="preflight-sensors"` — 8 sensor row 存在
- `data-testid="preflight-commlinks"` — 6 comm-link row 存在
- `data-testid="preflight-livelog"` — log 流容器存在
- `data-testid="go-nogo-gate"` — Gate 按钮根据状态变色

### 3.3 Screen ③ — Bridge HMI（核心）

**路由**: `/#/bridge/:scenarioId`

#### 3.3.0 Top Chrome（v1.1 NEW — 跨视图共享，从 `sil-app.jsx` 抽象）

**布局** (高度 40px, fixed top, z=20)：

```
┌─ Brand ─┬─ 4 Nav Tabs ───────┬─ Run State Pill ─┬─ Dual Clock ─┬─ View Toggle ─┐
│ MASS-L3 │ ① ② ③●④           │ ▶ ACTIVE         │ UTC 09:23:14 │ CAPTAIN GOD    │
│ SIL     │                    │                  │ SIM 00:01:23 │                │
└─────────┴────────────────────┴──────────────────┴──────────────┴────────────────┘
```

| 元素 | 实现 |
|---|---|
| **Brand** | "MASS-L3 SIL" mono 字体 + 版本号 chip（hover 显示 build hash）|
| **4 Nav Tabs** | ① Builder / ② Pre-flight / ③ Bridge / ④ Report，当前活跃 tab 加粗 + accent border |
| **Run State Pill** | 状态文字 + 颜色：`IDLE`(灰) / `READY`(amber) / `ACTIVE`(green pulse) / `PAUSED`(amber) / `COMPLETED`(blue) / `ABORTED`(red) |
| **Dual Clock** | UTC 实时钟 (`HH:MM:SS`) + SIM 仿真钟 (`MM:SS` or `HH:MM:SS`)，mono 字体小号 |
| **View Toggle** | Captain / God 切换按钮组（仅 Bridge HMI 页签显示，其他页签隐藏）|

**Footer Hotkey Hints** (高度 28px, fixed bottom, z=20)：

```
WS: ws://localhost:8080/sil  │  ASDR: /var/sil/run-{id}.mcap  │  T·ToR  F·Fault  M·MRC  H·Handback  SPACE·Pause
```

- 左：WS link 状态 + 当前 ASDR 写入路径
- 右：当前屏幕可用快捷键提示（Bridge HMI 显示 T/F/M/H/SPACE；其他屏幕显示对应快捷键）

`data-testid="top-chrome"` / `data-testid="run-state-pill"` / `data-testid="dual-clock-utc"` / `data-testid="dual-clock-sim"` / `data-testid="view-toggle"` / `data-testid="footer-hotkey-hints"`

#### 3.3.1 Captain View 布局

```
┌─ Top Info Bar (38px) ─────────────────────────────────────────────┐
│ CPA 0.85nm │ TCPA 6.2min │ RULE Rule-14 │ DECISION give way  │ PASS │
├─ Map Area (flex-1) ───────────────────────────────────────────────┤
│                                                                    │
│  ┌─ Captain HUD ─┐                    ┌─ View Toggle ─┐           │
│  │ SOG  18.3 kn  │                    │  CAPTAIN      │           │
│  │ COG  045.0°   │   ENC Fullscreen   │  GOD          │           │
│  │ HDG  046.2°   │   + S-57 tiles     │               │           │
│  │ ROT  2.1°/min │   own-ship @ bot   │               │           │
│  │ WIND 225° 8kn │   30% off-center   │               │           │
│  │ SEA  Bft 3    │   PPI rings        │               │           │
│  │ VIS  5.0 nm   │   COG vector       │               │           │
│  └───────────────┘   CPA rings        │               │           │
│                      target markers    ├─ ARPA Table ──┤           │
│   ┌─ Compass ─┐     distance scale    │ T1 BRG RNG    │           │
│   │  Relative  │                       │     COG SOG   │           │
│   │  Bearing   │                       │     CPA TCPA  │           │
│   └────────────┘                       └───────────────┘           │
│                                                       ● LIVE       │
├─ Bottom Bar (42px) ───────────────────────────────────────────────┤
│ 00:23 ▶ACTIVE │ ⏸ ×0.5×1×2×4×10 │ ●M1-M8 pulse │ ASDR(12) ⚠ ⏹    │
└────────────────────────────────────────────────────────────────────┘
```

**IEC 62288 对齐**:

| 元素 | IEC 62288 引用 | 实现 |
|------|---------------|------|
| 本船 off-center (bottom 30%) | §SA-1 | MapLibre `easeTo` with offset when viewMode=Captain |
| COG 6min heading vector | §SA-1 | GeoJSON line from ownShip → projected pos |
| 目标 CPA ring | §SA-2 | Circle layer centered on each target, radius=CPA threshold |
| PPI 距离圈 (0.5/1/2/4 nm) | §SA-3 | SVG circle overlays, center at ownShip offset |
| 距离比例尺 | §SA-3 | Bottom-center scale bar (adaptive to zoom) |
| 相对方位罗经花 | §SA-1 | SVG element bottom-left, rotates with bearing |
| 顶部信息条 (CPA/TCPA/Rule/Decision) | §SA-4 | TopInfoBar component (already implemented, enhanced) |

#### 3.3.2 God View 额外叠加

| 元素 | 实现 |
|------|------|
| **COLREGs 4 扇区** | SVG polygon sectors from ownShip position. 扇区角度 (relative bearing): Head-on 354°-6° (red), Crossing-Stbd 6°-112.5° (blue, Give-way), Crossing-Port 247.5°-354° (blue, Stand-on), Overtaking 112.5°-247.5° (green). 透明度 0.05-0.08，扇区边界虚线 stroke |
| **Imazu 场景几何** | 与 Screen ① 相同的 overlay，在 God 模式下显示。包含本船/目标起始位置、预期轨迹、场景标签 |
| **6-dim 实时评分** | Bottom-right HUD panel。6 行 color-coded 数值（SAF/RUL/DEL/MAG/PHA/PLA），每行格式: `label value`。数据源: WS topic `/sil/scoring_row` @ 1Hz |
| **M1-M8 下钻面板** | 点击任一 pulse dot → 弹出 2×4 grid 面板。每格显示 module_id, health color, latency_ms, drops, 关键内部状态字段 |

#### 3.3.3 ARPA 目标表组件

**位置**: Captain view 右侧可折叠 sidebar，God view 默认展开。

**列**: `ID | BRG(°) | RNG(nm) | COG(°) | SOG(kn) | CPA(nm) | TCPA(min)`

**数据源**: `useTelemetryStore.targets[]` @ 2Hz，CPA/TCPA 从 ASDR events 派生。

**颜色规则**: CPA < 1.0nm → 行文字红色。CPA 1.0-2.0nm → 黄色。CPA > 2.0nm → 默认。

#### 3.3.4 底部状态栏（与当前一致，minor 增强）

- 仿真时间 `MM:SS` + lifecycle 状态标签
- 速度控制 `⏸/▶ ×0.5 ×1 ×2 ×4 ×10`
- Module Pulse 条 + health summary badges `●5 ●2 ●1`
- ASDR toggle button（事件计数）+ Fault 注入 + Stop

**增强**: Module Pulse dots 在 God 模式下可点击，点击后展开 M1-M8 下钻面板。

#### 3.3.5 Scene FSM（v1.1 NEW — 剧情层，两视图共享）

**5 态状态机**，对齐 IMO MASS Code 操作模式分类 + Veitch 2024 TMR 框架：

```
            ┌──── target detected, CPA < threshold ────┐
            ↓                                          │
   ┌─────────────┐  user/auto trigger    ┌──────────────────┐
   │   TRANSIT   │ ───────────────────→ │ COLREG_AVOIDANCE │
   └─────────────┘                       └──────────────────┘
        ▲                                      │
        │ HANDBACK (captain reclaims)          │ TMR depleted / safety violation
        │                                      ↓
   ┌─────────────┐                       ┌─────────────┐
   │  HANDBACK   │                       │     TOR     │ (5s SAT-1 lock)
   └─────────────┘                       └─────────────┘
        ▲                                ↙       │      ↘
        │      captain accepts          /        │       \ 60s deadline expires
        │      (within 60s)           /          │        \
        │                          ↙             ↓         ↘
   ┌─────────────┐         ┌─────────────┐  ┌──────────────────┐
   │   ACTIVE    │←────────│   OVERRIDE  │  │       MRC        │
   │  (auto)     │         │  (captain)  │  │ (auto-fallback)  │
   └─────────────┘         └─────────────┘  └──────────────────┘
```

| 状态 | 含义 | UI 表现 |
|---|---|---|
| **TRANSIT** | 自动正常巡航，无威胁 | Run state pill `ACTIVE` 绿 + 顶部 RULE/DECISION 字段 dim |
| **COLREG_AVOIDANCE** | 检测到目标 CPA < threshold，系统主动按规则避让 | Pill `ACTIVE` 绿但脉动加快 + 顶部 RULE chip 高亮 + COLREGs sector (God) 标红当前规则扇区 |
| **TOR (Transfer of Responsibility)** | 系统检测无法独立解决（OOD / Veto / 关键传感失效）→ 请求 captain 接管 | **ToR 模态弹出**（见 §3.3.6）+ Pill 转 `AMBER PULSING` + Threat ribbon 全红闪 |
| **OVERRIDE** | Captain 已接管，手动控制中 | Pill `MANUAL` amber + 显示"Manual Control" overlay + Conning bar 进入 manual 输入模式 |
| **MRC (Minimum Risk Condition)** | Captain 60s 内未响应 → 系统自动回退到最小风险条件（减速/停车/掉头到安全位置）| Pill `MRC` red + 全屏 amber border + 显示 MRC 行为说明 + 自动 broadcast `LIFE_AT_RISK` 报警 |
| **HANDBACK** | Captain 主动归还控制权 → 系统恢复自动巡航 | Pill 从 `MANUAL` → `ACTIVE`，3s 缓冲后取消 manual overlay |

**触发条件**（按优先级）：
1. **Auto: TRANSIT → COLREG_AVOIDANCE** — Track CPA < `cpa_threshold`（默认 1.5nm）且 TCPA > 0
2. **Auto: COLREG_AVOIDANCE → TOR** — M7 Safety Supervisor Veto 触发 / M2 OOD 退出 envelope / 关键传感降级（Radar+AIS 同时失效）
3. **Manual: ANY → OVERRIDE** — Hotkey `T` 或点击 ToR 模态 "Take Control" 按钮
4. **Auto: TOR → MRC** — 60s 倒计时归零，captain 未响应
5. **Manual: OVERRIDE → HANDBACK → TRANSIT** — Hotkey `H` 或点击 "Hand Back" 按钮

**实现**:
- `web/src/store/fsmStore.ts` — Zustand state machine（current state + history + transition log）
- 每次状态变更 publish ASDR event `fsm_transition` (from_state + to_state + reason + timestamp)
- `data-testid="fsm-state-{transit|colreg|tor|override|mrc|handback}"`

#### 3.3.6 ToR Modal（v1.1 NEW — 对齐 Veitch 2024 TMR 框架）

> **🔧 v1.2 design 校准**（依据 `06-sil-bridge.jsx`）：
> - **TMR + TOL 双倒计时**（不是单 TMR）：
>   - **TMR** (Time of Maneuver Reservation): 60s 默认 — 用户接管 deadline
>   - **TOL** (Time of LOITER / Operating Limit): 240s 默认 — 系统持续运行 envelope 限制
> - Scene panel 顶部同时显示 `TMR 60` 和 `TOL 240`（带进度条）
> - **3 立式 action buttons**（非仅 modal 内"Take Control"按钮）：
>   - `REQUEST TAKEOVER` (T) — 主动触发 ToR
>   - `MRC - DRIFT` (M) — 紧急 MRC 触发（含 "紧急触发 (M)" 副标）
>   - `FAULT INJECT` (F) — 故障注入（与 §3.3.7 联动）
> - **Scene + Verdict 双显示**: `SCENE: TRANSIT/COLREG_AVOIDANCE/TOR/OVERRIDE/MRC` + `VERDICT: NOMINAL/CAUTION/CRITICAL`
> - **Resume / End Report 按钮**: 系统在 ACTIVE 时显示 `▶ RESUME (SPACE)` + `END · REPORT →`
> - 所有视觉细节以 `06-sil-bridge.jsx` 为准

**触发**: FSM 进入 TOR 状态时全屏 modal overlay（不可关闭，仅 Take Control / Wait 选择）。

**布局** (居中 480×360px modal, backdrop 60% opacity)：

```
┌─────────────────────────────────────────────────┐
│ ⚠ TRANSFER OF RESPONSIBILITY                    │
├─────────────────────────────────────────────────┤
│ Reason: M7 Veto — TCPA below 30s & CPA < 0.5nm  │
│ Triggered: 09:24:18 UTC                          │
│                                                  │
│ ┌─ TMR Countdown ──────────────────────────┐    │
│ │            ⏱  00:54                       │    │
│ │  ████████████░░░░░░░░░░ 54s remaining     │    │
│ │  SAT-1 lock countdown: 3s                  │    │
│ └──────────────────────────────────────────┘    │
│                                                  │
│ Current Situation:                               │
│   • Target T2 @ BRG 015° RNG 0.42nm             │
│   • Own COG 045° SOG 18.3kn                     │
│   • Proposed avoidance: starboard 20° turn      │
│                                                  │
│   [Wait for system MRC]      [Take Control] ◀── │
│                                  (disabled 3s)   │
└─────────────────────────────────────────────────┘
```

**机制**:
- **5s SAT-1 lock**: "Take Control" 按钮前 3 秒灰显（防误触），3 秒过后逐渐 amber → ready，5s 后才完全可点
- **60s TMR (Time of Maneuver Reservation)**: 整体倒计时，倒数 60→0；可视化进度条；30s 起进度条转 amber，10s 起转 red 闪烁
- **超时自动 MRC**: 倒计时归零 → FSM 自动转 MRC，modal 转为 "MRC Activated — see bridge" 提示，5s 后自动关闭
- **Take Control 按钮**: 触发 FSM TOR → OVERRIDE，关闭 modal，进入 manual control 模式
- **音频提示**: TOR 触发时播放 1 次 BAM 标准 alarm（`maritime-alarm-l2.wav`），captain 接管后停止

**实现**:
- `web/src/screens/shared/TorModal.tsx` — 模态组件，使用 React portal 渲染到 document.body
- 倒计时使用 `requestAnimationFrame` + `useTelemetryStore.lifecycleStatus.simTime` 同步（仿真时钟，不是墙钟）
- `data-testid="tor-modal"` / `data-testid="tor-countdown"` / `data-testid="tor-take-control"` / `data-testid="tor-sat1-lock"`

#### 3.3.7 Fault Injection Panel（v1.1 NEW — 仅 God 视图）

**位置**: God 视图右侧 docked panel (320px 宽), z=15, 在 ARPA 表上方。Captain 视图隐藏。

**布局**:

```
┌─ FAULT INJECTION ─────────────────┐
│ Active Faults: 0                  │
├───────────────────────────────────┤
│ ┌─ AIS Dropout ───────────────┐   │
│ │ Duration: [▬▬▬▬░░░░░] 30s   │   │
│ │ Affected: [☑ T1 ☑ T2 ☐ T3] │   │
│ │ [Inject Now]                 │   │
│ └─────────────────────────────┘   │
│ ┌─ Radar Failure ─────────────┐   │
│ │ Mode: ⦿ Full ○ PRF only    │   │
│ │ Duration: [▬▬░░░░░░░] 20s   │   │
│ │ [Inject Now]                 │   │
│ └─────────────────────────────┘   │
│ ┌─ ROC Link Loss ─────────────┐   │
│ │ Recovery: ⦿ Auto ○ Manual  │   │
│ │ Duration: [▬░░░░░░░░] 10s   │   │
│ │ [Inject Now]                 │   │
│ └─────────────────────────────┘   │
└───────────────────────────────────┘
```

| 故障类型 | 触发效果 |
|---|---|
| **AIS Dropout** | 选中目标的 AIS state stream 暂停 N 秒，期间 telemetry store `targets[i].lastAisAt` 不更新；M2 fallback 到 Radar/Camera-only 跟踪 |
| **Radar Failure** | Full = 全部 radar tracks 消失；PRF only = 速度量测劣化（增噪 σ=2kn）；触发 M7 SOTIF 假设监控降级 |
| **ROC Link Loss** | Shore Link WS 心跳中断；Auto recovery = N 秒后自动重连；Manual = 需要 captain 手动 reconnect；触发 FSM TOR（如果在 critical phase） |

**实现**:
- `web/src/screens/shared/FaultInjectPanel.tsx`
- POST 请求至 backend `/api/v1/fault/inject` (type + duration + params)
- 注入后 ASDR 写入 `fault_injected` event
- 快捷键 `F` 切换面板可见性（仅 God 视图）
- DEMO-1 implementation 简化版（3 按钮 + 1 duration slider 共用）；Phase 2 扩展为分故障类型独立配置 + 场景化 noise/loss profile

`data-testid="fault-panel"` / `data-testid="fault-inject-ais"` / `data-testid="fault-inject-radar"` / `data-testid="fault-inject-roc"`

#### 3.3.8 Conning Bar（v1.1 NEW — 两视图共享，密度按视图差异）

> **🔧 v1.2 design 校准**（依据 `06-sil-bridge.jsx` Bridge HMI 底部 HUD）：
> - **7 字段而非 4 字段**：HDG · COG · SOG · OOG · ROT · RUDDER · ROL (RPM/Pitch)
>   - HDG: 005 (船首向)
>   - COG: 005° (Course Over Ground)
>   - SOG: 18.4 kn / 1?.2 nm/cmd (Speed Over Ground + 命令值)
>   - OOG: 008 (Over Other Ground？查 JSX 源)
>   - ROT: +0.2 °/min (Rate of Turn)
>   - RUDDER: -02° (port，舵角实际值)
>   - ROL: 127 (RPM or Pitch — 详查 JSX `06-sil-bridge.jsx`)
> - **位置**: 底部 horizontal HUD，跨整个屏幕宽度（不是仅左 50%）
> - **God 模式额外**: 每字段右侧附 60s sparkline (`requestAnimationFrame` 节流到 30fps)
> - 所有视觉细节以 `06-sil-bridge.jsx` 为准

**位置**: 底部状态栏正上方 36px 条状区 (左 50%, 与 ARPA 表错位)。

**Captain 模式（精简）**:
```
┌─ CONNING ──────────────────────────────────────────────────────────┐
│ THR ░▒▓████▒░ 67% │ RUD ◀░░▌████░░░░░▶ +12°STBD │ RPM 1840 │ PITCH +4° │
└────────────────────────────────────────────────────────────────────┘
```

**God 模式（完整 + 历史曲线）**:
```
┌─ CONNING + 60s HISTORY ────────────────────────────────────────────┐
│ THR ░▒▓████▒░ 67%  ┌── 60s sparkline ──┐                          │
│ RUD ◀░░▌████░░░░▶ +12°STBD ┌── sparkline ──┐                      │
│ RPM 1840           ┌── sparkline ──┐                              │
│ PITCH +4°          ┌── sparkline ──┐                              │
└────────────────────────────────────────────────────────────────────┘
```

**数据源**: WS topic `/sil/control_cmd` @ 10Hz (由 M5 Tactical Planner 发布, sub by L4)。Manual override 时 captain 通过键盘 ←/→ (rudder) 和 ↑/↓ (throttle) 调整。

`data-testid="conning-bar"` / `data-testid="conning-history"` (God only)

#### 3.3.9 Threat Ribbon（v1.1 NEW — 两视图共享，密度按视图差异）

**位置**: 顶部信息条正下方 28px 条状区，z=14。

**Captain 模式（1 行 chip）**:
```
[T1 ●0.85nm] [T2 ●1.20nm] [T3 ●2.30nm] ...
```
- Chip 颜色按 CPA 染色（red < 1.0 / amber < 2.0 / green ≥ 2.0）
- 按 CPA 升序排列（最危险在左）

**God 模式（2 行扩展）**:
```
[T1 ●HEAD-ON 0.85nm 4.2min] [T2 ●CROSS-STBD 1.20nm 6.8min] ...
[Rule-14 give_way pending] [Rule-15 stand_on active] ...
```
- 上行：chip + COLREGs role label + CPA + TCPA
- 下行：当前规则推理状态（rule_id + state + side hint）

**实现**:
- 数据从 `useTelemetryStore.targets[]` + `asdrEvents` 派生
- 点击 chip → 高亮地图上的目标 + 自动 pan 到目标位置
- `data-testid="threat-ribbon"` / `data-testid="threat-chip-{mmsi}"`

#### 3.3.10 Hotkeys（v1.1 NEW — 全局键盘快捷键）

| 键 | 行为 | 适用 FSM 状态 |
|---|---|---|
| `T` | 弹出 ToR 模态 / 触发 OVERRIDE | TRANSIT / COLREG_AVOIDANCE / TOR |
| `F` | 切换 Fault Inject Panel（仅 God）| 任意 |
| `M` | 立即触发 MRC（强制最小风险条件） | 任意 |
| `H` | Hand back control 给系统（仅 OVERRIDE）| OVERRIDE |
| `SPACE` | 仿真暂停 / 恢复 | 任意 |
| `Cmd/Ctrl + Shift + G` | 切换 Captain ↔ God 视图 | 任意 |
| `←` / `→` | OVERRIDE 模式下舵角调整（每按 +1°） | OVERRIDE |
| `↑` / `↓` | OVERRIDE 模式下油门调整（每按 +5%） | OVERRIDE |

**实现**:
- `web/src/hooks/useHotkeys.ts` — useEffect 注册 keydown listener, dispatch to fsmStore / uiStore
- 当焦点在 input/textarea 时禁用（防止 Builder YAML 编辑器冲突）
- 快捷键提示在 footer 持续显示（参见 §3.3.0）

`data-testid="hotkey-tor"` / `data-testid="hotkey-fault"` etc.（虚拟元素，仅用于 e2e 模拟 keydown）

### 3.4 Screen ④ — Run Report（**v1.1 扩展为 6-lane Timeline + ASDR Ledger**）

**路由**: `/#/report/:runId`

**布局**:

```
┌─ Verdict + Top KPI Tiles ──────────────────────────────────────────┐
│ ● PASS │ Min CPA 0.84nm │ Max ROT 8.2°/min │ Rule Violations 0     │
│         (sparkline)      (sparkline)        (sparkline)             │
├─ 6-Lane Event Timeline (scrubbable) ──────────────────────────────┤
│ FSM   │ TRANSIT─────[COLREG_AVOIDANCE]─────[TOR]─[OVERRIDE]─────  │
│ Rule  │ ─────────[R-14 give_way]─[R-15 stand_on]────────────────  │
│ M7    │ ────────────────────[Veto]────────────────────────────── │
│ Fault │ ──────────────[AIS dropout 30s]──────────────────────── │
│ CPA   │ ────╲──╲──╲──╲──╲──╲──╲──╲ (line chart)                │
│ ASDR  │ ●──────●──●──●────────●──────● (event dots)             │
│       │ 0:00            0:30            1:00            1:30       │
│       │       [▶ ◀▶ ▶▶ ×0.5 ×1 ×2 ×4 ×10  ⏹]                   │
├─ Bottom Area (3-col) ──────────────────────────────────────────────┤
│ Trajectory Replay  │ Scoring Radar      │ ASDR Ledger Table        │
│ (map snapshot      │ (6-dim SVG polygon)│ (filterable + SHA-256    │
│  + animated path)  │                    │  hash chain column)       │
├─ Operation Bar ────────────────────────────────────────────────────┤
│ [PASS][FAIL] [Export .marzip] [Export ASDR.csv] [New Run]         │
└────────────────────────────────────────────────────────────────────┘
```

#### 3.4.1 Verdict + KPI 顶部条

> **🔧 v1.2 design 校准**（依据 `07-sil-report.jsx`）：
> - **8 KPI cards 而非 4-5**：每个含数值 + sparkline + 单位 / 含义副标
>   1. **VERDICT**: PASS/FAIL (大字) + criteria 计数 (例 "8/8 criteria · 0 fail · 0 warn") + sealed indicator + 签名行 (例 "signed SHA-256 / VPSB · sealed")
>   2. **最小 CPA**: 0.52 nm + sparkline (绿色路径)
>   3. **COLREGs 合规率**: 1.00 + sparkline
>   4. **最大舵角 RUD**: 0.48 m? 或 rad? + sparkline
>   5. **TOR 累计时长**: 5.5 s + sparkline
>   6. **决策延迟 P99**: 24 ms + sparkline + threshold 标注（"<50ms"）
>   7. **Fault 注入计数**: "2 件 · 注入"
>   8. **SHA-256 状态**: hash 缩写 + "0 次" 失败计数
> - 所有视觉细节以 `07-sil-report.jsx` 为准

| KPI | 数据源 | 显示 |
|---|---|---|
| **Verdict** | `GET /api/v1/runs/:runId/verdict` | 大号 ● PASS（绿）/ ● FAIL（红）/ ● INCONCLUSIVE（黄）|
| **Min CPA** | `GET /api/v1/scoring/last_run` `.kpi.min_cpa_nm` | 数值 + 60s sparkline（红区线下=危险）|
| **Max ROT** | 同上 `.kpi.max_rot_deg_per_min` | 数值 + sparkline |
| **Rule Violations** | 同上 `.kpi.rule_violation_count` | 数值 + sparkline (红色 chip if > 0) |
| **TMR Reservation Used** | 同上 `.kpi.tmr_used_s` | "X / 60s" + 进度条 |

**Sparkline**: DEMO-1 stub（SVG path with zero data）；Phase 2 接 telemetry buffer。`data-testid="kpi-sparkline-{cpa\|rot\|violations}"`

#### 3.4.2 Event Timeline（v1.1 NEW — 可点击 scrub）

> **🔧 v1.2 design 校准**（依据 `07-sil-report.jsx`）：
> - **不是显式 6-lane 而是 1 紧凑 timeline strip**（多色 segment 堆叠在窄水平条内）
> - "19 events · click track to scrub" 单行 hint + 时间刻度
> - 上方独立大型 **Trajectory Replay** map 显示完整航迹 + 播放控制（×0 ×1 ×2 ×4 ×10 + ⏸/▶ + `T+02:20` 时间指示）
> - 点击 timeline 任意位置 → 跳转 trajectory 到该时刻 + 同步 ASDR ledger 高亮当时事件
> - 所有视觉细节以 `07-sil-report.jsx` 为准

**6 lanes** (从上到下)：

| Lane | 内容 | 数据源 |
|---|---|---|
| **FSM** | FSM 状态条 + 转换点（颜色按状态）| ASDR `fsm_transition` events |
| **Rule** | COLREGs 规则激活区间（带 rule_id 标签）| ASDR `colreg_rule_active` events |
| **M7** | Safety Supervisor Veto / Warning 标记 | ASDR `m7_veto` events |
| **Fault** | 注入故障区间（带类型 + 时长）| ASDR `fault_injected` events |
| **CPA** | min CPA 折线图（y 轴: 0-5nm）| 派生自 `target_vessel_state` + `own_ship_state` |
| **ASDR** | 所有 ASDR event 点（hover 显示详情）| ASDR full event stream |

**Scrub 交互**:
- 横坐标 = 仿真时间（0:00 → t_end）
- 鼠标 hover → 显示十字光标 + 对应时刻所有 lane 的数值 tooltip
- 鼠标点击 → 在地图回放区跳转到该时刻态势快照
- 播放控制按钮: `▶ ◀▶ ▶▶ ×0.5 ×1 ×2 ×4 ×10 ⏹`
- DEMO-1: 仅静态时间轴 + click-to-jump（不带 MCAP seek）；Phase 2 加完整 scrubbing + MCAP seek

`data-testid="timeline-6lane"` / `data-testid="timeline-lane-{fsm\|rule\|m7\|fault\|cpa\|asdr}"` / `data-testid="timeline-playback"`

#### 3.4.3 Trajectory Replay（地图快照 + 动画）

- 左下区域 ~480×320px MapLibre 实例（独立于 Bridge HMI 海图）
- 显示本船 + 目标船轨迹（GeoJSON LineString）
- 沿时间轴 scrub 时同步显示当前时刻位置标记
- 使用 `useMapStore.viewport` 作为初始视角
- `data-testid="trajectory-replay"`

#### 3.4.4 Scoring Radar Chart（保持 v1.0 设计）

- 6 维评分雷达图（SVG polygon）
- 轴: Safety / Rule / Delay / Magnitude / Phase / Plausibility
- 数据源: `GET /api/v1/scoring/last_run` 返回的 KPI 对象
- 颜色: 数据多边形 `--c-stbd` 半透明填充
- `data-testid="scoring-radar"`

#### 3.4.5 ASDR Ledger Table（v1.1 NEW — SHA-256 hash chain）

> **🔧 v1.2 design 校准**（依据 `07-sil-report.jsx`）：
> - **过滤器: 4-level (ALL / INFO / WARN / CRIT)**，非 type+module multi-select
> - **列结构**: T+TIME (sim time MM:SS) · Module ID (M2/M4/M5/M7/M8) · Event Type (大写下划线 enum) · Description (中文混 English)
> - **Event Type enum（~18 类型，from `REPORT_EVENTS` fixture）**：
>   - 生命周期: `INIT` / `END`
>   - 检测/分类: `TGT_DET` / `CPA_PROJ` / `CPA_MIN`
>   - 场景转换: `SCENE_CHG` (例 "TRANSIT → COLREG_AVOIDANCE")
>   - COLREGs: `COLREG_R14` / `COLREG_R13` / `COLREG_R15` / `COLREG_R17`
>   - 规划: `MPC_BRANCH`
>   - 故障: `AIS_DROP` / `AIS_REC` / `RUL_LOSS` / `BNSS_BIAS`
>   - ToR/接管: `TOR_REQ` / `TOR_ACK` / `OVERRIDE` / `HANDBACK` / `TRANSIT`
>   - WP: `WP_REACH`
> - **SHA chain section** 在 ledger 底部，含完整 hash 显示 + `VERIFY CHAIN` 按钮 + `EXPORT JSON` 按钮（与单行 SHA 列分离）
> - **顶部 actions**: ← BACK TO BRIDGE / ↓ EXPORT ASDR / + NEW RUN →
> - 所有视觉细节以 `07-sil-report.jsx` 为准

**列**: `# | Time | Type | Module | Payload preview | SHA-256 (8 chars)`

| 字段 | 实现 |
|---|---|
| **#** | 行号（1-N）|
| **Time** | 仿真时间 `MM:SS.mmm` |
| **Type** | event_type chip（带颜色按类别）|
| **Module** | source_module_id |
| **Payload preview** | JSON 前 60 字符 + "..." + hover 显示完整 |
| **SHA-256** | event 的 hash 前 8 字符（点击展开完整 64 字符 + 上一 event 的 prev_hash 链接）|

**过滤器**:
- Event type multi-select dropdown
- Module multi-select
- 时间范围 slider
- 关键字搜索框

**SHA-256 hash chain**:
- DEMO-1: hash gen stub（显示固定 `0xDEADBEEF` placeholder + 列存在但不验证）
- Phase 2: 真实 SHA-256 链（每 event hash = SHA256(prev_hash || event_payload)），导出时附带 ledger verification report

**数据源**: `GET /api/v1/runs/:runId/asdr` 返回完整 event list

`data-testid="asdr-ledger"` / `data-testid="asdr-filter-{type\|module\|time\|search}"` / `data-testid="asdr-hash-link"`

#### 3.4.6 COLREGs Decision Tree（保持 v1.0 设计）

- 从 ASDR events 重建规则链
- 格式: `Rule-XX → Situation → Action → Verdict` 缩进树
- 空态: `No rule events captured`
- 数据源: 与现有 `rule-chain` 相同
- 位置: ASDR ledger 上方折叠区（默认折叠）
- `data-testid="decision-tree"`

#### 3.4.7 操作按钮

- PASS / FAIL verdict 按钮（已有）
- **Export .marzip**（异步，轮询状态，已有）
- **Export ASDR.csv**（v1.1 NEW — 不带 hash 链 verification 的纯数据导出）
- Download link（导出完成后显示）
- New Run → 返回 ScenarioBuilder

---

## 4. 组件树与文件清单（**v1.1 大幅扩展**）

### 4.1 新增文件（v1.1 累计 25 个 NEW）

```
web/src/
├── styles/
│   └── tokens.css                    # NEW v1.0 — CSS custom properties
├── store/
│   ├── mapStore.ts                   # NEW v1.0 — cross-screen viewport persistence
│   └── fsmStore.ts                   # NEW v1.1 — Scene FSM state machine (5 states)
├── map/
│   ├── SilMapView.tsx                # MODIFIED — accept viewport, offset, mode props
│   ├── layers.ts                     # MODIFIED — add S-57 MVT, Imazu geometry, COLREGs sectors, PPI rings
│   ├── vesselSprite.ts               # (unchanged)
│   ├── CompassRose.tsx              # NEW v1.0
│   ├── PpiRings.tsx                  # NEW v1.0
│   ├── ColregsSectors.tsx            # NEW v1.0
│   ├── ImazuGeometry.tsx             # NEW v1.0
│   └── DistanceScale.tsx             # NEW v1.0
├── screens/
│   ├── BridgeHMI.tsx                 # MODIFIED — dual-mode + all overlays + FSM/ToR/Fault/Conning/Threat/Hotkeys
│   ├── ScenarioBuilder.tsx           # MODIFIED — 3-step structure + Imazu grid + 3 sub-tabs + SummaryRail
│   ├── Preflight.tsx                 # MODIFIED — M1-M8 grid + sensors + commlinks + live log + GO/NO-GO
│   ├── RunReport.tsx                 # MODIFIED — 6-lane timeline + ASDR ledger + trajectory replay
│   └── shared/
│       ├── ArpaTargetTable.tsx       # NEW v1.0
│       ├── ModuleDrilldown.tsx        # NEW v1.0
│       ├── ScoringGauges.tsx          # NEW v1.0
│       ├── ScoringRadarChart.tsx      # NEW v1.0
│       ├── ColregsDecisionTree.tsx    # NEW v1.0
│       ├── TopChrome.tsx              # NEW v1.1 — brand + 4 tabs + state pill + dual clock + view toggle
│       ├── FooterHotkeyHints.tsx      # NEW v1.1 — WS link + ASDR path + hotkey hints
│       ├── RunStatePill.tsx           # NEW v1.1 — IDLE/READY/ACTIVE/PAUSED/COMPLETED/ABORTED pill
│       ├── DualClock.tsx              # NEW v1.1 — UTC + SIM dual clock
│       ├── TorModal.tsx               # NEW v1.1 — 5s SAT-1 lock + 60s TMR countdown + auto-MRC
│       ├── FaultInjectPanel.tsx       # NEW v1.1 — AIS/Radar/ROC fault injection (God only)
│       ├── ConningBar.tsx             # NEW v1.1 — throttle/rudder/RPM/pitch + history sparklines (God)
│       ├── ThreatRibbon.tsx           # NEW v1.1 — target threat chips + COLREGs role labels
│       ├── Stepper.tsx                # NEW v1.1 — generic stepper for ScenarioBuilder 3-step
│       ├── SummaryRail.tsx            # NEW v1.1 — Builder right-rail summary + Validate CTA
│       ├── ImazuGrid.tsx              # NEW v1.1 — 4×6 Imazu 22 thumbnail grid
│       ├── ModuleReadinessGrid.tsx    # NEW v1.1 — Preflight M1-M8 8-tile grid
│       ├── SensorStatusRow.tsx        # NEW v1.1 — Preflight 8 sensor status dots
│       ├── CommLinkStatusRow.tsx      # NEW v1.1 — Preflight 6 comm-link status dots
│       ├── LiveLogStream.tsx          # NEW v1.1 — Preflight live log with auto-scroll + filter
│       ├── TimelineSixLane.tsx        # NEW v1.1 — Report 6-lane scrubbable timeline (replaces TimelineScrubber)
│       ├── AsdrLedger.tsx             # NEW v1.1 — Report ASDR event ledger with SHA-256 chain + filters
│       └── TrajectoryReplay.tsx       # NEW v1.1 — Report animated trajectory map replay
├── hooks/
│   ├── useMapPersistence.ts          # NEW v1.0
│   └── useHotkeys.ts                 # NEW v1.1 — global keyboard shortcut registration
├── api/
│   └── silApi.ts                     # MODIFIED — add fault/imazu/asdr/verdict/scoring endpoints
├── types/
│   └── (protobuf-ts generated)       # (unchanged)
└── App.tsx                           # MODIFIED v1.1 — wrap routes with TopChrome + FooterHotkeyHints
```

### 4.2 修改文件（v1.1 累计 11 个 MODIFIED）

| 文件 | v1.0 变更 | v1.1 增量 |
|------|---------|---------|
| `web/src/store/index.ts` | Export `useMapStore` | + Export `useFsmStore` |
| `web/src/store/uiStore.ts` | (unchanged) | + Add `viewMode: 'captain' \| 'god'` state + `setViewMode()` action |
| `web/src/map/SilMapView.tsx` | Add `viewMode`, `followOwnShip`, `viewportOffset`, `bearing` props | — |
| `web/src/map/layers.ts` | Add S-57 MVT + Imazu/COLREGs sources | — |
| `web/src/screens/BridgeHMI.tsx` | Integrate CompassRose/PpiRings/etc., view mode toggle | + FSM state subscription / + TorModal / + FaultInjectPanel / + ConningBar / + ThreatRibbon / + useHotkeys binding |
| `web/src/screens/ScenarioBuilder.tsx` | Imazu overlay + AIS tab + mapStore | + 3-step Stepper / + 3 sub-tabs (Template/Procedural/AIS) / + ImazuGrid / + SummaryRail |
| `web/src/screens/Preflight.tsx` | scenario thumbnail strip | + ModuleReadinessGrid / + SensorStatusRow / + CommLinkStatusRow / + LiveLogStream / + GO/NO-GO gate |
| `web/src/screens/RunReport.tsx` | TimelineScrubber + ScoringRadarChart + DecisionTree | + TimelineSixLane (replaces TimelineScrubber) / + AsdrLedger / + TrajectoryReplay / + KPI sparklines |
| `web/src/App.tsx` | — | + Wrap `<Routes>` with `<TopChrome />` + `<FooterHotkeyHints />`; current page tab driven by location |
| `web/src/api/silApi.ts` | AIS endpoints | + `POST /fault/inject` / + `GET /imazu/:id` / + `GET /runs/:id/asdr` / + `GET /runs/:id/verdict` / + `GET /scoring/last_run` |
| `web/src/main.tsx` | Import `tokens.css` | — |

---

## 5. 数据流

### 5.1 WebSocket 实时流（Screen ② / ③ / ④ 共享）

| Topic | 频率 | Proto Type | Store Update | v1.1 新增 |
|------|------|-----------|-------------|--------|
| `/sil/own_ship_state` | 50Hz | `sil.OwnShipState` | `useTelemetryStore.ownShip` + `ownShipTrail` (600-point ring buffer) | |
| `/sil/target_vessel_state` | 2Hz | `sil.TargetVesselState` | `useTelemetryStore.targets[]` (indexed by mmsi) | |
| `/sil/environment_state` | 0.2Hz | `sil.EnvironmentState` | `useTelemetryStore.environment` | |
| `/sil/module_pulse` | 1Hz | `sil.ModulePulse` | `useTelemetryStore.modulePulses[]` (latest per moduleId) | |
| `/sil/asdr_event` | event | `sil.ASDREvent` | `useTelemetryStore.asdrEvents[]` (200-event ring buffer) | |
| `/sil/lifecycle_status` | 1Hz | `sil.LifecycleStatus` | `useTelemetryStore.lifecycleStatus` | |
| `/sil/scoring_row` | 1Hz | `sil.ScoringRow` | `useTelemetryStore.scoringRow` (latest) | |
| `/sil/fsm_state` | event | `sil.FsmState` | `useFsmStore.currentState` + `useFsmStore.transitionHistory[]` | ✅ v1.1 |
| `/sil/tor_event` | event | `sil.TorEvent` | `useFsmStore.torRequest` (含 reason / TMR remaining / SAT-1 lock countdown) | ✅ v1.1 |
| `/sil/fault_status` | 1Hz | `sil.FaultStatus` | `useTelemetryStore.faultStatus[]` (active fault list) | ✅ v1.1 |
| `/sil/control_cmd` | 10Hz | `sil.ControlCmd` | `useTelemetryStore.controlCmd` (rudder/throttle/RPM/pitch + 60s ring buffer for sparkline) | ✅ v1.1 |
| `/sil/sensor_status` | 1Hz | `sil.SensorStatus` | `useTelemetryStore.sensors[8]` (per-sensor health) | ✅ v1.1 |
| `/sil/commlink_status` | 1Hz | `sil.CommLinkStatus` | `useTelemetryStore.commLinks[6]` | ✅ v1.1 |
| `/sil/preflight_log` | event (≤50/s) | `sil.LogEntry` | `useTelemetryStore.preflightLog[]` (1000-entry ring) | ✅ v1.1 |

### 5.2 REST API（Screen ① ② ④）

v1.0 端点保留。**v1.1 新增端点**：

| 方法 | 路径 | 用途 | 响应 |
|---|---|---|---|
| `POST` | `/api/v1/fault/inject` | 故障注入请求 | `{ accepted, fault_id, will_inject_at }` |
| `DELETE` | `/api/v1/fault/:fault_id` | 取消未执行故障 | `{ cancelled }` |
| `GET` | `/api/v1/imazu/:id` | Imazu 几何（01..22）| `{ id, geometry: GeoJSON FC, narrative }` |
| `GET` | `/api/v1/imazu` | Imazu 22 例 manifest | `{ items: [{ id, label, thumbnail_svg }] }` |
| `GET` | `/api/v1/runs/:runId/asdr` | ASDR event 完整列表（支持 ?type=&module=&from=&to=）| `{ events: [...], total, hash_chain_verified }` |
| `GET` | `/api/v1/runs/:runId/verdict` | Verdict + KPI 顶部条数据 | `{ verdict, kpi: { min_cpa_nm, max_rot, violations, tmr_used_s }, sparklines }` |
| `GET` | `/api/v1/scoring/last_run` | 6 维评分雷达数据 | `{ safety, rule, delay, magnitude, phase, plausibility }` |
| `POST` | `/api/v1/runs/:runId/export` | 异步导出 .marzip / .csv | `{ job_id }` |
| `GET` | `/api/v1/jobs/:jobId` | 导出任务状态轮询 | `{ status, progress, download_url? }` |

### 5.3 海图状态持久化

```
MapLibre 'moveend' event
  → useMapPersistence hook
    → useMapStore.setViewport({ center, zoom, bearing, pitch })
      → next screen mount: SilMapView reads useMapStore.viewport
        → MapLibre.jumpTo(viewport) before first render
```

---

## 6. 测试检查点（补充）

在原有 `data-testid` 基础上新增。

### 6.1 全局（跨屏）
- `[data-testid="top-chrome"]` — 顶部 chrome 存在
- `[data-testid="run-state-pill"]` — Run state pill 显示当前 lifecycle 状态
- `[data-testid="dual-clock-utc"]` — UTC 时钟
- `[data-testid="dual-clock-sim"]` — SIM 仿真时钟
- `[data-testid="view-toggle"]` — Captain/God 视图切换按钮（仅 Bridge HMI 显示）
- `[data-testid="footer-hotkey-hints"]` — 底部快捷键提示

### 6.2 Screen ① Scenario Builder
- `[data-testid="builder-step-{1\|2\|3}"]` — 3-step Stepper
- `[data-testid="encounter-tab-{template\|procedural\|ais}"]` — 3 sub-tab
- `[data-testid="imazu-grid"]` — Imazu 22 例 4×6 网格
- `[data-testid="imazu-geometry"]` — 选中场景后右侧地图显示几何
- `[data-testid="summary-rail"]` — 右栏 summary 存在
- `[data-testid="validate-cta"]` — Validate 按钮按状态变色

### 6.3 Screen ② Pre-flight
- `[data-testid="preflight-modules"]` — M1-M8 grid 存在
- `[data-testid="preflight-sensors"]` — 8 sensor row 存在
- `[data-testid="preflight-commlinks"]` — 6 comm-link row 存在
- `[data-testid="preflight-livelog"]` — Live log 流容器存在
- `[data-testid="go-nogo-gate"]` — Gate 按钮根据状态变色

### 6.4 Screen ③ Bridge HMI
- `[data-testid="compass-rose"]` — 罗经花
- `[data-testid="ppi-rings"]` — PPI 距离圈
- `[data-testid="distance-scale"]` — 距离比例尺
- `[data-testid="colregs-sectors"]` — God 模式下 COLREGs 扇区
- `[data-testid="arpa-table"]` — ARPA 目标表
- `[data-testid="scoring-gauges"]` — God 模式下 6 维评分
- `[data-testid="module-drilldown"]` — 点击 pulse dot 后下钻
- **v1.1 NEW**:
- `[data-testid="fsm-state-{transit\|colreg\|tor\|override\|mrc\|handback}"]` — FSM 当前状态
- `[data-testid="tor-modal"]` — ToR 模态弹出
- `[data-testid="tor-countdown"]` — TMR 60s 倒计时
- `[data-testid="tor-sat1-lock"]` — SAT-1 5s lock 进度
- `[data-testid="tor-take-control"]` — Take Control 按钮（3s 后激活）
- `[data-testid="fault-panel"]` — Fault inject 面板（仅 God）
- `[data-testid="fault-inject-{ais\|radar\|roc}"]` — 故障注入按钮
- `[data-testid="conning-bar"]` — Conning bar
- `[data-testid="conning-history"]` — 60s sparkline（God only）
- `[data-testid="threat-ribbon"]` — 威胁条
- `[data-testid="threat-chip-{mmsi}"]` — 每个目标 chip

### 6.5 Screen ④ Run Report
- `[data-testid="scoring-radar"]` — 评分雷达图
- `[data-testid="decision-tree"]` — COLREGs 决策树
- `[data-testid="map-snapshot"]` — 态势快照图（可选）
- **v1.1 NEW**:
- `[data-testid="kpi-sparkline-{cpa\|rot\|violations}"]` — KPI sparklines
- `[data-testid="timeline-6lane"]` — 6-lane timeline 容器
- `[data-testid="timeline-lane-{fsm\|rule\|m7\|fault\|cpa\|asdr}"]` — 各 lane
- `[data-testid="timeline-playback"]` — 播放控制按钮组
- `[data-testid="trajectory-replay"]` — 轨迹回放地图
- `[data-testid="asdr-ledger"]` — ASDR ledger 表
- `[data-testid="asdr-filter-{type\|module\|time\|search}"]` — 过滤器
- `[data-testid="asdr-hash-link"]` — SHA-256 hash 展开链接

---

## 7. Non-goals（明确不做）

### 7.1 v1.0 既定（保留）

- 不实现 Day/Dusk/Night 三模式切换（DEMO-1 仅 Night dark theme）
- 不实现 BNWAS 集成（Phase 3）
- 不实现 ROS2 topic inspector（Phase 2）
- 不实现 S-52 Display Base / Standard / All Other Info 的完整 Category 切换（仅 layer toggle）
- 不实现多 monitor 支持
- 不在 Phase 1 加 TLS/WSS

### 7.2 v1.1 新增（DEMO-1 stub / 简化 / 推后）

- **MCAP seek（完整 scrubbing）** — DEMO-1 仅 click-to-jump，6-lane timeline 不真正 seek 到任意时刻；Phase 2 接入 MCAP foxglove player
- **ASDR SHA-256 hash chain 真实生成 + verification report** — DEMO-1 显示固定 placeholder `0xDEADBEEF`；Phase 2 实现链式 hash + 导出 verification PDF
- **KPI sparklines 真实数据** — DEMO-1 SVG path with zero/placeholder；Phase 2 接 telemetry ring buffer
- **Fault inject 场景化 noise/loss profile** — DEMO-1 仅 3 故障类型 × duration slider；Phase 2 加按场景类型的 noise/loss curve（如 AIS dropout 模拟基站遮蔽、Radar PRF jitter 模拟雨衰）
- **AIS Replay 真实数据回放** — DEMO-1 仅静态文件名 + MMSI 选择反馈；Phase 2 真实 CSV 解析 + 时间窗回放
- **Procedural scenario generation UI 真实生成** — DEMO-1 UI 完整但 backend stub；Phase 2 接 `behavior_generator`
- **音频提示（BAM alarm sound）** — DEMO-1 不播放音频（避免在 SIL workbench 中误触发本机环境 audio）；Phase 2 可选启用
- **Captain 视图键盘 manual control（←/→/↑/↓）真实下达指令** — DEMO-1 仅 UI 反馈 + ASDR `manual_control_attempt` event，不真实 publish 到 `/sil/control_cmd`；Phase 2 接 backend manual override interface

---

## 8. 风险与降级

| 风险 | 触发信号 | 降级 |
|------|---------|------|
| S-57 MVT 37K tiles 加载 > 2s | Week 2 tile benchmark | 降级到 OSM raster + S-57 vector 仅水深/陆地层 |
| COLREGs 4 扇区 SVG 性能 | 50Hz 下 SVG re-render 掉帧 | Canvas 替代 SVG，requestAnimationFrame 批量更新 |
| `useMapStore` viewport 恢复不同步 | 跨屏切换后地图跳动 | 在 `SilMapView` mount 时加 100ms debounce |
| MapLibre `easeTo` offset 与 PPI rings 圆心偏离 | visual QA | 用 MapLibre `project`/`unproject` 同步计算 |
| 6-dim scoring 数据源未就绪 | scoring_node 未实现 | 用 `asdrEvents` 中的 verdict + CPA 推导 stub 评分 |
| **FSM transition 与 backend 状态不一致**（v1.1）| Frontend FSM 与 ASDR 中的 `fsm_transition` event 出现顺序冲突 | Frontend FSM 仅 receive，不主动 transition；所有 transition 由 backend orchestrator 权威发布；frontend 在 100ms 内未收到则保持上一个状态并标 `desync` flag |
| **ToR Modal 倒计时 vs 仿真时钟漂移**（v1.1）| Sim time 加速 (10×) 时倒计时与剧情节奏脱节 | TMR 60s 锁定为**仿真时间**（不是墙钟），仿真暂停时倒计时停止；UI 上明确显示"SIM TIME"标签 |
| **Conning bar history sparkline 50Hz 数据丢帧**（v1.1）| WS `/sil/control_cmd` @ 10Hz 但 sparkline 渲染 60fps | 使用 `requestAnimationFrame` + ring buffer interpolation；丢帧时显示最后一个有效值 |
| **Fault inject 触发 backend race condition**（v1.1）| 用户在 fault active 期间再次 inject 相同类型 | API 返回 409 + 显示 "fault already active until t=XX"；frontend 锁定按钮直到 fault 结束 |
| **Threat ribbon chips 数量过多**（v1.1）| >10 个目标时 ribbon 溢出 | Captain 模式仅显示 CPA top-5；God 模式横向滚动 + 总数 badge |
| **Hotkeys 与 IME / form input 冲突**（v1.1）| 输入框内键入 T/F/M 触发动作 | `useHotkeys` 检测 `document.activeElement` 类型，input/textarea 时禁用全局快捷键 |

---

## 9. 参考文献

- [R-NLM:silhil-1] ADMIRALTY. *S-100 timelines explained*. https://www.admiralty.co.uk/news/s-100-timelines-explained
- [R-NLM:silhil-2] OpenBridge. *About OpenBridge*. https://www.openbridge.no/about-openbridge
- [R-NLM:silhil-3] CDP Studio. *Maritime User Interfaces*. https://cdpstudio.com/cdp-studio/design/maritime-user-interfaces/
- [R-NLM:silhil-4] Foxglove. *Autonomous Marine Robotics*. https://foxglove.dev/solutions/marine.html
- [R-NLM:hf-1] IMO. *MSC.252(83) INS Performance Standards*.
- [R-NLM:hf-2] IMO. *MSC.302(87) Bridge Alert Management*.
- [R-NLM:hf-3] IHO. *S-52 ECDIS Colours & Symbols*. https://journals.lib.unb.ca/index.php/ihr/article/download/20562/23724/
- [R-NLM:hf-4] IHO. *S-98 Annex C: Harmonised UX for ECDIS and INS*.
- [R-NLM:hf-5] IEC. *Bridge Alert Management for Mariners V4*.
- [R-NLM:hf-6] Seaways. *Making the Most of the Predictor*.
- **[R-DNV-1]** (v1.1 NEW) DNV. *DNVGL-CG-0264 — Autonomous and remotely operated ships*. §SIL/HIL Test Management System / Virtual World separation. 🟢 High (silhil_platform nlm-ask 2026-05-13)
- **[R-IMO-MASS]** (v1.1 NEW) IMO. *MASS Code (MSC 110/111) — Operator Control Modes*: Monitoring / Strategic / Tactical / Direct. Used for FSM mapping rationale.
- **[R-Veitch-2024]** (v1.1 NEW) Veitch, E. *Time of Maneuver Reservation (TMR) ≥ 60s for Remote Operator Takeover in MASS*. CCS-AIP TMR baseline. 🟢 High (CLAUDE.md §5 强制约束)
- **[R-Design-1]** (v1.1 NEW) Claude Design handoff bundle `colav-simulator` (2026-05-12 → 2026-05-13). Captures user 设计意图 + 5 SIL screens JSX prototype（`sil-app.jsx` / `sil-builder.jsx` / `sil-preflight.jsx` / `sil-bridge.jsx` / `sil-report.jsx`）+ 3 sub-tabs (Template/Procedural/AIS Replay) inspired by colav-simulator open-source repo. 🟢 High (用户直接 design 输出)
- [Spec-Ref] SIL Architecture Design `docs/Design/SIL/2026-05-12-sil-architecture-design.md` §5, §7
- [Plan-Ref] SIL Foundation Plan `docs/superpowers/plans/2026-05-12-sil-foundation.md`
- [E1-E33] Architecture Decision Record `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`

---

## 修订记录

- 2026-05-13 v1.0：基线建立。用户决策 Dual-Mode Balanced scope (17 features) + ENC chart persistence + OpenBridge 5.0/S-52 alignment。
- 2026-05-13 v1.2：**整合用户 export `COLAV SIL Simulator.html` ground truth**（含 7 babel JSX 源 ~2181 行 + 完整 :root token + 132 资源）。
  - **顶部 callout**: 新增"Reference Prototype"块，明确 `docs/Design/SIL/reference-prototype/` 为视觉/结构权威，spec 文本 vs JSX 行为冲突时**以 JSX 为准**
  - **§3.1 校准**: Steps A/B/C 而非 1/2/3 / 无 3-sub-tabs / 独立 SCENARIO LIBRARY / Environment 4 真实 cards 字段
  - **§3.2 校准**: Module 含 src+hz+checks / Sensors 具体 make-model (TrimMV BD992 / Xsens MTi-680G / JRC JMA-9230 等)
  - **§3.3.6 校准**: TMR + TOL **双倒计时** / 3 立式 action button / Scene + Verdict 双标
  - **§3.3.8 校准**: Conning **7 字段** (HDG/COG/SOG/OOG/ROT/RUDDER/ROL) 而非 4
  - **§3.4.1 校准**: **8 KPI cards** 而非 4-5（含 VERDICT sealed / COLREGs 合规率 / 决策延迟 P99 等）
  - **§3.4.2 校准**: **单 timeline strip** 而非显式 6-lane（多色 segment 堆叠紧凑显示）
  - **§3.4.5 校准**: ASDR ledger **4-level filter (ALL/INFO/WARN/CRIT)** + **~18 event types enum** (INIT/TGT_DET/CPA_PROJ/SCENE_CHG/COLREG_R14/MPC_BRANCH/AIS_DROP/CPA_MIN/RUL_LOSS/TOR_REQ/TOR_ACK/OVERRIDE/HANDBACK/TRANSIT/BNSS_BIAS/WP_REACH/END)
  - 视觉 fidelity 预期: 90% → **~98%**（pixel-port via JSX reference）
- 2026-05-13 v1.1：**整合 Claude Design handoff bundle（colav-simulator）运行时语义**。
  - **DNV 框架重定位**：Dual-Mode 从"信息密度切换"重新框定为 DNVGL-CG-0264 SIL 双轨观测点（Captain = Digital Twin Black Box / God = Test Management White Box）。证据 🟢 silhil_platform notebook nlm-ask + maritime_human_factors notebook nlm-ask。
  - **新增 §3.3.0 Top Chrome** — 跨屏共享（brand + 4 tab + run state pill + dual clock + view toggle + footer hotkey hints）
  - **§3.1 重构为 3-step structure** — ENCOUNTER (Template/Procedural/AIS Replay 3 sub-tabs + Imazu 22 grid) / ENVIRONMENT (Sea/Sensors/Hull/ODD) / RUN (Timing/IvP Weights/Fault Script/Pass Criteria) + Summary Rail
  - **§3.2 扩展** — M1-M8 grid + 8 sensors row + 6 comm-links row + Live Log Stream + GO/NO-GO gate
  - **新增 §3.3.5 Scene FSM** — 5 态（TRANSIT / COLREG_AVOIDANCE / TOR / OVERRIDE / MRC / HANDBACK）+ 转换条件 + ASDR 事件流
  - **新增 §3.3.6 ToR Modal** — 5s SAT-1 lock + 60s TMR 倒计时 + 超时 auto-MRC，对齐 Veitch 2024 + CLAUDE.md §5
  - **新增 §3.3.7 Fault Inject Panel** — AIS dropout / Radar failure / ROC link loss（仅 God），DEMO-1 简化版
  - **新增 §3.3.8 Conning Bar** — 油门 / 舵角 / RPM / Pitch 实时（God 含 60s history sparkline）
  - **新增 §3.3.9 Threat Ribbon** — 目标威胁条 chip + COLREGs role label（God 2 行扩展）
  - **新增 §3.3.10 Hotkeys** — T(ToR) / F(Fault) / M(MRC) / H(Handback) / SPACE(pause) / Cmd+Shift+G(view) / 箭头(manual control)
  - **§3.4 扩展** — Verdict + KPI sparklines / 6-lane scrubbable timeline (FSM/Rule/M7/Fault/CPA/ASDR) / Trajectory Replay / ASDR Ledger Table (SHA-256 hash chain stub)
  - **§4 大幅扩展** — 累计 25 个 NEW 文件 + 11 个 MODIFIED 文件
  - **§5.1 新增 6 WS topics** — `/sil/fsm_state` / `/sil/tor_event` / `/sil/fault_status` / `/sil/control_cmd` / `/sil/sensor_status` / `/sil/commlink_status` / `/sil/preflight_log`
  - **§5.2 新增 9 REST endpoints** — fault / imazu / runs/:id/asdr / runs/:id/verdict / scoring/last_run / jobs/:id
  - **§6 新增 ~40 test-ids** — FSM / ToR / Fault / Conning / Threat / 6-lane timeline / ASDR ledger 等
  - **§7 Non-goals 分 v1.0 / v1.1 两组**，明确 v1.1 stub / 简化 / 推后项
  - **§8 风险扩展 7 条** — FSM 同步 / ToR 时钟漂移 / Conning 丢帧 / Fault race / Threat 溢出 / Hotkeys 冲突
  - **§9 新增 4 references** — [R-DNV-1] / [R-IMO-MASS] / [R-Veitch-2024] / [R-Design-1]
  - **scope**: 17 features → **28 features**；预计工时增量 ~+40%
