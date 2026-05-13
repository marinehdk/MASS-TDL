# SIL HMI Reference Prototype（Claude Design 输出 ground truth）

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-REFPROTO-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-13 |
| 状态 | 视觉/结构权威参考（不可改） |
| 来源 | 用户通过 Claude Design (claude.ai/design) 设计并 export `COLAV SIL Simulator.html` (8MB)；本目录解包出 7 个 babel JSX 源 |

---

## 0. 这是什么 / 为什么存在

本目录保存 **SIL HMI v1.1 spec/plan 实施期的"视觉与结构真相"**。

- **spec** (`docs/superpowers/specs/2026-05-13-sil-hmi-dual-mode-design.md`) 描述：**意图 / 架构 / 集成 / 测试点 / 数据流 / 风险**
- **plan** (`docs/superpowers/plans/2026-05-13-sil-hmi-dual-mode.md`) 描述：**任务分解 / 并行图 / 依赖**
- **本目录 (reference-prototype)** 描述：**精确的 React 组件实现 / CSS 微距 / 字段命名 / 状态机内部细节 / 文案 / 配色应用**

实施时必须 **两者结合**：
- spec 给出"应该有什么"
- 本目录给出"长什么样、字段叫什么、组件怎么组合"

> **重要：本目录的 .jsx 文件是 ground truth，不是参考意见。spec 与 .jsx 冲突时以 .jsx 为准（除非 .jsx 明显是 design-time mock，例如 hardcoded fixture data）。**

---

## 1. 文件清单

| # | 文件 | 行数 | 角色 | 实施 Task |
|---|---|---|---|---|
| 01 | `01-hmi-atoms.jsx` | 220 | 共享视觉原子（COL palette / StatusPill / Chip / Card / 等可复用组件）| Task 1 (tokens) + Task 18 (chrome) |
| 02 | `02-sil-imazu.jsx` | 80 | Imazu 22 经典会遇几何 SVG 数据（IM01-IM22 with rules R13/R14/R15/R17 + ship positions）| Task 22 (ImazuGrid) |
| 03 | `03-sil-app.jsx` | 194 | 顶级路由 + chrome (StatusPill / 4 nav tabs / dual clock / state pill / REC indicator) | Task 18 (TopChrome) + Task 17 (uiStore.viewMode 调度) |
| 04 | `04-sil-builder.jsx` | 453 | Screen ① Scenario Builder（A/B/C steps + Imazu grid + Environment 4 cards + Run settings + Summary rail）| Tasks 11 + 22 |
| 05 | `05-sil-preflight.jsx` | 309 | Screen ② Pre-flight（M1-M8 with topic/Hz/sub-checks + 8 sensors with make-model + 6 comm-links + live log + GO/NO-GO gate）| Tasks 13 + 23 |
| 06 | `06-sil-bridge.jsx` | 553 | Screen ③ Bridge HMI（PPI map + scene FSM panel + 3 target chips + 7-field Conning bar + 3 action buttons + ToR modal + Fault catalog + Event log）| Tasks 10 + 19 + 20 + 21 |
| 07 | `07-sil-report.jsx` | 372 | Screen ④ Run Report（8 KPI cards + trajectory replay + event timeline + ASDR ledger with 4-level filter + SHA chain verify）| Tasks 12 + 24 |

---

## 2. 如何使用本目录（实施者工作流）

### 2.1 启动新 Task 前

1. **读 spec**（对应 §3.x 子节）— 确定要做什么 + 为什么
2. **读对应 JSX 文件**（上表"实施 Task"列）— 确定具体长什么样 + 怎么实现
3. **如有冲突**：spec 描述与 JSX 实际行为不同 → 以 JSX 为准 + 在 PR 描述注记差异（spec 待 v1.x 修订）

### 2.2 实施期

- **直接移植**：JSX 是 Babel-standalone 的 React（用 `window.X` 而非 ESM import）— 本项目用 ESM + TypeScript + Zustand + RTK Query，**结构/JSX/逻辑直接移植**，仅改：
  - `window.COL` → `import COL from './hmi-atoms'`（或转 token CSS var）
  - `useState_X` (避免 closure 冲突的临时命名) → 标准 `useState`
  - 局部 `useState` (UI state) → 保留；跨组件 / 跨屏 state → Zustand store
  - `setTimeout` 模拟实时数据 → 替换为 WS subscription
  - Fixture data (`MODULE_LIST` / `FAULT_CATALOG` / `REPORT_EVENTS` 等) → 保留 *fallback default*，运行时由 backend 覆盖
- **CSS**：完全保留 design 的 CSS-in-JS 内联样式（用 `var(--bg-0)` etc.）— spec §2 v1.1 已对齐 token

### 2.3 验证

- **Pixel 对照**：实施完成后用 `gstack` 或 Claude Preview 渲染我方实现 → 与本目录可运行 HTML 并排截图 → 比对
- **行为对照**：spec §3.x.x test-id 全部要在我方实现可访问；同时验证 JSX 中可观察行为（如 FSM state transition / TMR 倒计时 / Fault 注入 / SHA chain verify）

---

## 3. 关键发现（spec v1.1 撰写时未覆盖的细节）

### 3.1 Top Chrome (`03-sil-app.jsx`)

- Brand: **"COLAV · SIL"** + 副标 "MASS L3 · TDL v1.1.2 · Session SIL-0427"（含 session ID）
- 4 tab 用 **双语**: "01 SCENARIO / 场景编辑"、"02 PRE-FLIGHT / 飞行前检查"、"03 BRIDGE / 驾驶台运行"、"04 REPORT / 回放与报告"
- Chrome 右侧: scenario name + dual clock (SIM T+00:00 / UTC HH:MM:SSZ) + state pill (IDLE/READY/ACTIVE/PAUSED/COMPLETED/ABORTED) + REC indicator (red dot pulse during ACTIVE)
- 底部 footer: WS link + ASDR path + 当前页对应 hotkeys

### 3.2 Builder Steps (`04-sil-builder.jsx`)

- Steps 标签用 **A / B / C** 而非 1/2/3：
  - **A · ENCOUNTER** 会遇几何 · Imazu 22
  - **B · ENVIRONMENT** 海况 · 传感器 · 本船
  - **C · RUN** 时序 · 故障 · 评估
- 左侧 sidebar 含独立 **SCENARIO LIBRARY** 区，4 个预设：`IM03_Crossing_GiveWay_v2` / `IM07_Overtaken_FogA` / `IM14_Triple_Conflict` / `IM22_Restricted_Narrow`（与 Imazu 22 网格分开）

### 3.3 Imazu 22 (`02-sil-imazu.jsx`)

- 每例数据格式：`{ id, name, rule, ships: [{x, y, h, role}] }`
- Rule 标注: IM01=R14 / IM02=R13 / IM03=R15 / IM04=R17 / IM05=R15 / IM06=R15 / IM07=R13 / IM08=R14+R13 / ... / IM22=R9 复合
- Ship role: `give-way` / `stand-on` / `safe`
- Own ship 固定在 viewport (50, 75) 朝上，target ships 相对位置 + heading

### 3.4 Environment 4 Cards (`04-sil-builder.jsx` Step B)

| Card | 字段 |
|---|---|
| **风浪 · WIND / SEA / CURRENT** | 风向 (BEAUFORT scale 0-12) · 浪向 · 风速 · 浪高 · 浪周期 · 海流向 |
| **能见度与气象** | 时段 (DAY/DUSK/NIGHT) · 气象现象 · 雾·海况浓度 (LOW/FOG/RAIN) · SWELL 强度 · AIS 容噪度 · 障碍距离 |
| **SCO 区域配置** | 区域 (PRIMARY: A-OPEN / B-TSS / C-RESTRICTED) · 沿岸 SOCG 系数 · 拥挤系数 · 越线复杂度 |
| **本船 · 动力学** | 船型 (FCB-NSM / CONT-100M / TUG-20M) · 排水量 · 初始 HDG · 初始 SOG · 最大舵 · ROT 上限 |

### 3.5 Pre-flight Detail (`05-sil-preflight.jsx`)

- 每个 module 显示：`{id, name, src(topic path), hz, checks[]}` 数据
  - 例: `M1 ODD State / TMR / TDL · sango_adas/m1 · 1Hz · 3 checks`
  - 例: `M5 Mid-MPC Planner · sango_adas/m5 · 2Hz · 4 checks · sub: [BC-MPC init, warm-start, solver health]`
- 传感器有具体 make/model：
  - `GNSS-A: TrimMV BD992` (RTK FLOAT · 24 sats)
  - `IMU: Xsens MTi-680G` (200Hz · bias compensated)
  - `AIS: Furuno FA-170` (often shows WARN)
  - `GNSS-B: Septentrio mosaic`
  - `RADAR: JRC JMA-9230` (27 RPM · 24 nm)
  - `LiDAR: Velodyne VLS-128`
- Comm-link 显示具体类型: 含 "Iridium backup 高延迟" 等真实标签
- Live log 格式: `[HH:MM:SS.ms] preflight.runner: <message>`

### 3.6 Bridge HMI (`06-sil-bridge.jsx`)

- Ownship 顶部标识: **FCB-A5M** + OWNSHIP + IMO 9876543 + scenario name + 右上 **D3 - SUPERVISED** autonomy pill
- Scene panel 顶右：current scene (TRANSIT/COLREG_AVOIDANCE/TOR/OVERRIDE/MRC) + verdict label (NOMINAL/CAUTION/CRITICAL)
- **TMR + TOL 双倒计时**（不是仅 TMR）
  - TMR (Time of Maneuver Reservation): 60s (用户操作 ToR 后的截止)
  - TOL (Time Of Last...): 240s（具体含义查 JSX 源）
- 3 target chips（每个含: name + COLREGs role + Rule + CPA + TCPA）
- Fault catalog (内置 enum)：
  - AIS_DROP (M2, warn, 30s)
  - 其他类型见 JSX 中 `FAULT_CATALOG`
- Conning bar 7 字段（**非 4 字段**）：HDG · COG · SOG · OOG · ROT · RUDDER · ROL
- 底部 3 立式 action button：REQUEST TAKEOVER (T) / MRC-DRIFT (M) / FAULT INJECT (F)
- Event log 区右下: "no events recorded" 空态

### 3.7 Run Report (`07-sil-report.jsx`)

- **8 KPI cards**（非 4-5）：
  1. **VERDICT**: PASS/FAIL (大字) + criteria 计数 (e.g., "8/8 criteria · 0 fail · 0 warn") + sealed indicator
  2. 最小 CPA + sparkline
  3. COLREGs 合规率 + sparkline
  4. 最大舵角 / RUD + sparkline
  5. TOR 累计时长 + sparkline
  6. 决策延迟 P99 + sparkline (target threshold 标注)
  7. Fault 注入计数
  8. SHA-256 hash chain status + failure count
- Trajectory replay (大型) + 播放控制（×0 ×1 ×2 ×4 ×10）
- Event timeline (compact horizontal strip, click to scrub) — **不是 6 lanes**
- ASDR Ledger 右侧（**4-level filter**: ALL · INFO · WARN · CRIT）
- ASDR event types enum (~18 types from `REPORT_EVENTS` fixture):
  - INIT / TGT_DET / CPA_PROJ / SCENE_CHG / COLREG_R14 / MPC_BRANCH / AIS_DROP / AIS_REC / CPA_MIN / RUL_LOSS / TOR_REQ / TOR_ACK / OVERRIDE / HANDBACK / TRANSIT / BNSS_BIAS / WP_REACH / END
- SHA chain section: 显示完整 hash + **VERIFY CHAIN** + **EXPORT JSON** buttons
- 顶部 3 action buttons: ← BACK TO BRIDGE / ↓ EXPORT ASDR / + NEW RUN →

---

## 4. 与 spec v1.1 的差异（v1.2 待修订项）

| spec v1.1 | design 实际 | 处理 |
|---|---|---|
| Steps 编号 1/2/3 | **A/B/C** | spec §3.1 修订 |
| Builder Step ② 含 "ODD Level dropdown" | **不在 ② Environment，改用 SCO 区域 (A-OPEN/B-TSS/C-RESTRICTED) + Bridge 顶 D2/D3/D4 pill 分担** | spec §3.1.2 修订 |
| 6-lane timeline | **1 紧凑 timeline strip** (含多色 segments 但非显式 6 lanes) | spec §3.4.2 修订 |
| TMR 仅 60s | **TMR + TOL 双倒计时** | spec §3.3.6 修订 |
| Conning 4 字段 (THR/RUD/RPM/PITCH) | **7 字段 (HDG/COG/SOG/OOG/ROT/RUDDER/ROL)** | spec §3.3.8 修订 |
| 4-5 KPI cards | **8 KPI cards** | spec §3.4.1 修订 |
| ASDR ledger filter: type/module multi-select | **4-level filter: ALL/INFO/WARN/CRIT** | spec §3.4.5 修订 |
| 标签英文 | **双语 EN + 中文** | spec 全文修订（标签层）|
| Library presets 无概念 | **存在 SCENARIO LIBRARY** (4 预设，与 Imazu 22 分开) | spec §3.1 新增 §3.1.7 |
| Sensors 抽象 (GNSS/Gyro/...) | **具体 make/model** | spec §3.2.4 改为命名传感器表 |
| Modules 抽象 (M1/M2/...) | **+ topic path + Hz + check 数 + sub-checks** | spec §3.2.3 增字段 |

---

## 5. 本目录不包含什么

- ❌ Font woff2 文件（6 MB，从 Google Fonts CDN 加载）
- ❌ Vendor scripts (React / Babel standalone / 等，从 vendor CDN 加载)
- ❌ Runnable `index.html`（用户原 8MB 文件在 ~/Downloads/浏览器下载/COLAV SIL Simulator.html；实施期如需要可重新构建 preview，见下方）

## 6. 重建 runnable preview（可选）

如果实施过程中需要**反复对照 design 行为**，重建本地可运行 preview：

```bash
# 一次性脚本（从用户 export HTML 解包，~1 分钟）
HTML=~/Downloads/浏览器下载/COLAV\ SIL\ Simulator.html
OUT=/tmp/sil-preview
mkdir -p "$OUT"
# 1) 抽 line 197 (manifest) + line 205 (template)
node -e "
  const fs=require('fs'),zlib=require('zlib');
  const f=fs.readFileSync('$HTML','utf8').split('\n');
  const m=JSON.parse(f[196]);
  for(const [id,e] of Object.entries(m)){
    let b=Buffer.from(e.data,'base64');
    if(e.compressed)b=zlib.gunzipSync(b);
    fs.writeFileSync('$OUT/'+id, b);
  }
  fs.writeFileSync('$OUT/index.html', JSON.parse(f[204]));
"
# 2) 启 HTTP server
python3 -m http.server 8765 --directory "$OUT"
# 3) 浏览器打开 http://localhost:8765
```

或调用 Claude Preview 工具 (preview_start) — 参见 `.claude/launch.json` 中 `sil-design-preview` 配置。

---

## 7. License / Provenance

- **来源**: 用户 (marinehdk@gmail.com) 通过 claude.ai/design 设计 + handoff export
- **MASS-L3 项目内部使用**，按本项目 LICENSE 处理
- **不可对外公开**（含项目内部命名 / 模块结构 / 算法语义）
