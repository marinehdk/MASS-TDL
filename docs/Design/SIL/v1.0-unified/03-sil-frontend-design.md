# SIL 前端设计 · v1.0 统一基线

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-UNIFIED-003 |
| 版本 | v1.0 |
| 日期 | 2026-05-15 |
| 状态 | 设计基线（Doc 2 通过后产出）|
| 套件 | Doc 0 / Doc 1 / Doc 2 / **Doc 3 前端** / Doc 4 场景联调（pending）|
| 上游 | Doc 1 §6 数据流 + §7 IDL · Doc 2 §2 orchestrator REST + §7 ROS2 topics |
| 范围 | 4 屏 HMI 详设计（含 Simulation-Check 架构对齐重设计）+ 状态管理 + 数据通道 + 设计语言 + 交叉一致性 |
| 命名 | Simulation-Scenario / Simulation-Check / Simulation-Monitor / Simulation-Evaluator |

---

## 0. 一句话定位

把"4 屏 HMI"端到端落地为 **React 18 + Vite 5 + Zustand 5 + RTK Query 2 + MapLibre GL 4 + tier4/roslibjs-foxglove** 的 SPA，单一 JSON-over-WebSocket 通道（取代当前 `telemetry_bridge.py` 自制协议）接 foxglove_bridge :8765，4 屏分别承担**场景配置 / 仿真前验证 / 实时监控 / 报告评估**职责，整套以 IEC 62288 SA subset + IMO S-Mode + OpenBridge 配色为合规底座，仅 night mode（不做 day/dusk 切换，决策记录 §9 + 用户 2026-05-12 决策锁定）。

---

## 1. 技术栈与依赖（commit 73cdf23 实际 `web/package.json`）

| 项 | 版本 | 用途 | 置信度 |
|---|---|---|---|
| `react` | ^18.3.1 | UI 库 | 🟢 |
| `react-dom` | ^18.3.1 | DOM 渲染 | 🟢 |
| `vite` | ^5.4 | 开发 + 打包 | 🟢 |
| `typescript` | ^5.5 | 类型 | 🟢 |
| `zustand` | ^5.0.13 | 高频遥测 store（50 Hz 选择性 re-render）| 🟢 [R-NLM:36-41] |
| `@reduxjs/toolkit` + `react-redux` | ^2.11 / ^9.2 | REST 缓存 + 数据流 | 🟢 |
| `maplibre-gl` | ^4.7 | ENC 海图 + 矢量图层 | 🟢 [W19] |
| `@tier4/roslibjs-foxglove` | v0.0.4 (tarball) | ROS2 over Foxglove WS bridge | 🟡 [W49] tier4 fork，活跃维护 |
| `@protobuf-ts/runtime` + `@protobuf-ts/plugin` | ^2.11 | Protobuf TS 类型（虽 `sil_proto/` 空，预留 schema 联动）| 🟢 [R-NLM:30] |
| `lucide-react` | ^1.14 | 图标 | 🟢 |
| `js-yaml` | ^4.1 | 客户端 YAML 解析（场景预览）| 🟢 |
| `better-sqlite3` | ^12.10 | 本地 tile-server 索引（dev 脚本 tile-server.cjs）| 🟢 |
| `@playwright/test` | ^1.59 | e2e 测试 | 🟢 |
| `vitest` + `@testing-library/react` + `jsdom` | ^2 / ^16 / ^25 | 单元测试 | 🟢 |

**单一外部 image**（已在 Doc 2 §9 锁定）：`ghcr.io/maplibre/martin:latest` 作 MVT tile server。

---

## 2. 应用结构（`web/src/`）

### 2.1 目录树（commit 73cdf23）

```
web/src/
├── App.tsx                    # 56 行 hash router + TopChrome + 4 screen 切换
├── main.tsx                   # 19 行 ReactDOM 入口
├── test-setup.ts              # vitest 全局 setup
├── api/
│   └── silApi.ts              # 152 行 RTK Query (18 endpoints)
├── hooks/
│   ├── useFoxgloveLive.ts     # 124 行 WS reconnect + topic dispatch
│   ├── useHotkeys.ts          # 全屏快捷键
│   └── useMapPersistence.ts   # 跨屏 viewport 持久化
├── map/
│   ├── SilMapView.tsx         # 22 KB MapLibre 主组件 + S-57 多图层
│   ├── layers.ts              # 19 KB ALL_S57_LAYERS 配置
│   ├── CompassRose / PpiRings / DistanceScale / ColregsSectors / ImazuGeometry
│   ├── MapLayerSwitcher + MapZoomControl
│   └── vesselSprite.ts
├── screens/
│   ├── ScenarioBuilder.tsx    # 16.9 KB 屏 ① ⚠️ 文件名待 GAP-014 重命名
│   ├── Preflight.tsx          # 7.9 KB 屏 ② ⚠️
│   ├── BridgeHMI.tsx          # 17.5 KB 屏 ③ ⚠️
│   ├── RunReport.tsx          # 11.6 KB 屏 ④ ⚠️
│   └── shared/                # 22 components 共 ~90 KB（见 §6）
├── store/
│   ├── telemetryStore.ts      # 5.1 KB 高频遥测（50 Hz 主入口）
│   ├── fsmStore.ts            # 1.2 KB 6-state FSM + TOR
│   ├── scenarioStore.ts       # 907 B scenarioId/runId/lifecycleState
│   ├── controlStore.ts        # 620 B 仿真控制（rate, paused, faults）
│   ├── mapStore.ts            # 880 B viewport 持久化
│   ├── replayStore.ts         # 625 B 报告屏 timeline scrub
│   ├── uiStore.ts             # 839 B viewMode / panel toggle
│   └── index.ts               # 343 B re-export
├── styles/
│   └── tokens.css             # 92 行 OpenBridge / IEC 62288 token + atom classes
└── types/sil/
    ├── own_ship_state.ts / target_vessel_state.ts / ...  # Protobuf-generated TS 类型（23 个 .ts）
    └── *.client.ts                                       # service client wrappers
```

**关键事实**：`types/sil/` 已存在 23 个 Protobuf-generated TS 文件（在 `sil_proto/` 本身为空的情况下！）—— 说明前端类型已**预先**通过别处的 .proto 生成或手工同步，与 Doc 2 §3.5 GAP-019 协同：v1.0 在前端侧保留 Protobuf TS 类型作类型契约，序列化使用 ROS2 .msg + JSON-over-WS。

### 2.2 路由（App.tsx hash-based）

```
#/builder           → ScenarioBuilder    （Simulation-Scenario）
#/preflight/:id     → Preflight          （Simulation-Check）
#/bridge/:id        → BridgeHMI          （Simulation-Monitor）
#/report/:id        → RunReport          （Simulation-Evaluator）
```

**GAP-014 重命名映射**（D1.3b.3+ sprint 实施）：

| 当前文件名 | 当前路由 | v1.0 目标文件 | v1.0 目标路由 |
|---|---|---|---|
| `screens/ScenarioBuilder.tsx` | `#/builder` | `screens/SimulationScenario.tsx` | `#/scenario` |
| `screens/Preflight.tsx` | `#/preflight/:id` | `screens/SimulationCheck.tsx` | `#/check/:id` |
| `screens/BridgeHMI.tsx` | `#/bridge/:id` | `screens/SimulationMonitor.tsx` | `#/monitor/:id` |
| `screens/RunReport.tsx` | `#/report/:id` | `screens/SimulationEvaluator.tsx` | `#/evaluator/:id` |

v1.0 文档全篇用**统一目标命名**；代码读引用旧名时加 ⚠️ 标注。

### 2.3 选 hash router 而非 react-router 的原因

- 单页 SPA，hash router 零依赖 + 零路由库版本兼容问题
- bookmark / refresh 友好（hash 不发服务器请求）
- foxglove_bridge 路径 + tile-server 路径不与 react-router 路径冲突

未来若需嵌套路由 / loader / SSR → 切 react-router 7+（不在 v1.0 范围）。

---

## 3. 状态管理（7 Zustand stores + RTK Query）

### 3.1 双轨架构

| 数据类型 | 工具 | 原因 |
|---|---|---|
| **高频遥测**（50 Hz own_ship / 10 Hz target / 10 Hz module_pulse 等） | **Zustand** | selective re-render；slice 订阅；无 dispatch overhead；50 Hz p50 < 5 ms 🟢 [R-NLM:36-41] |
| **REST 数据**（scenarios CRUD / lifecycle status / scoring last_run / export status）| **RTK Query** | 自动缓存 + invalidation + mutation；providesTags / invalidatesTags |
| **本地 UI 状态**（panel toggle, viewMode, hotkey enabled）| **Zustand** | 简单 setter，无 reducer 样板 |
| **跨屏持久化**（map viewport, scenarioId, runId）| **Zustand** + localStorage（mapStore）| useMapPersistence 已实现 |

### 3.2 七个 Zustand stores（slice 表）

| Store | 文件 | 持有 | 触发频率 |
|---|---|---|---|
| `useTelemetryStore` | telemetryStore.ts (5.1 KB) | ownShip + targets[] + environment + modulePulses[] + asdrEvents[≤200] + lifecycleStatus + ownShipTrail[≤600] + scoringRow + sensors[] + commLinks[] + faultStatus[] + controlCmd + preflightLog[≤1000] + wsConnected | 50 Hz own + 10 Hz target/module + event ASDR/fault |
| `useFsmStore` | fsmStore.ts (1.2 KB) | currentState(6 态：TRANSIT/COLREG_AVOIDANCE/TOR/OVERRIDE/MRC/HANDBACK) + transitionHistory[≤100] + torRequest | event |
| `useScenarioStore` | scenarioStore.ts (907 B) | scenarioId + runId + scenarioHash + lifecycleState | 屏切换 + 配置完成 |
| `useControlStore` | controlStore.ts (620 B) | simRate + isPaused + faultsActive[] | 用户操作 |
| `useMapStore` | mapStore.ts (880 B) | viewport(center/zoom/bearing/pitch) | drag/zoom + viewport persist |
| `useReplayStore` | replayStore.ts (625 B) | scrubTime + mcapDuration + isScrubbing | 屏 ④ 操作 |
| `useUIStore` | uiStore.ts (839 B) | viewMode(captain/god/roc) + panelsCollapsed{} | 用户操作 |

### 3.3 选择性 re-render 模式（已实施）

`useFoxgloveLive.ts:15-21` 使用 zustand 选择器订阅：

```ts
const updateOwnShip = useTelemetryStore((s) => s.updateOwnShip);
const updateTargets = useTelemetryStore((s) => s.updateTargets);
```

→ 仅订阅 `update*` 函数引用（稳定），不订阅 state 本身。地图组件单独订阅 `useTelemetryStore(s => s.ownShip.pose)`，50 Hz 下仅 pose 字段变化时重渲染对应 `<VesselMarker>`，避免地图全图层重绘 🟢 [R-NLM:36-41]。

**50 Hz 选择性 re-render 最佳实践**（subagent 2026-05-15 🟡 + [W59] zustand v5 useShallow docs）：
- React 18.2+ 引入 `useShallow()` 做浅比较，避免对象引用变化触发不必要 re-render
- 50 Hz 推送如出现 fiber thrashing（DevTools React Profiler 红色火焰）→ 加 100 ms debounce 把 50 Hz 推送降到 UI refresh 10 Hz（人眼无感知差异）
- 优先做 fine-grain selector 拆分，仅在 profiler 实证有问题时才上 debounce

### 3.4 RTK Query 18 endpoints（silApi.ts）

| Endpoint | Type | 屏 |
|---|---|---|
| `useListScenariosQuery` | query | ① |
| `useGetScenarioQuery` | query (id) | ① |
| `useValidateScenarioMutation` | mutation | ① |
| `useCreateScenarioMutation` | mutation | ① |
| `useDeleteScenarioMutation` | mutation | ① |
| `useGetLifecycleStatusQuery` | query | 全 |
| `useConfigureLifecycleMutation` | mutation | ② |
| `useActivateLifecycleMutation` | mutation | ② → ③ |
| `useDeactivateLifecycleMutation` | mutation | ③ → ④ |
| `useCleanupLifecycleMutation` | mutation | ④ → ① |
| `useGetLastRunScoringQuery` | query | ④ |
| `useProbeSelfCheckMutation` | mutation | ② |
| `useGetHealthStatusQuery` | query (poll 1Hz) | ② ③ |
| `useExportMarzipMutation` | mutation | ④ |
| `useGetExportStatusQuery` | query (poll 0.5Hz) | ④ |
| `useTriggerFaultMutation` | mutation | ③ |
| `useInjectFaultMutation` | mutation | ③ |
| `useCancelFaultMutation` | mutation | ③ |

`baseUrl: '/api/v1'` → Vite dev server proxy 到 `localhost:8000`（orchestrator）。生产环境 nginx reverse proxy 同源。

---

## 4. 数据通道（GAP-015 决断）

### 4.1 三个通道

| 通道 | 协议 | 数据 | 端口 | 库 |
|---|---|---|---|---|
| **REST** | HTTP/JSON | scenarios / lifecycle / scoring / selfcheck / export / fault | `:8000` (orchestrator) | RTK Query (fetch) |
| **遥测 WS**（合一）| WebSocket / JSON | 11 topics 高频遥测 + 事件 | `:8765` | useFoxgloveLive 自定义 / tier4 roslibjs |
| **MVT Tiles** | HTTP/protobuf | S-57 vector tiles | `:3000` (martin) | MapLibre GL JS native |

### 4.2 GAP-015 决断：选项 A — foxglove_bridge 一统

**问题**（Doc 2 §9.4）：
- `telemetry_bridge.py:112` 启 `websockets.serve(..., "0.0.0.0", 8765)` 自定义 JSON
- `docker-compose.yml` `foxglove-bridge` service 也启 `:8765`
- 两者端口冲突

**决断**：v1.0 **退役 `telemetry_bridge.py`**（[W50] foxglove-bridge schema 协商优势 🟡）：

- foxglove_bridge 原生 ROS2 topic + service 中继，无需 orchestrator 桥
- Schema 协商内置（advertise topic + JSON schema OR Protobuf）
- 50 Hz p99 < 22 ms 实测过关 [W6] Foxglove docs 🟢
- 前端 `useFoxgloveLive.ts` 现行接 ws://127.0.0.1:8765 已对 foxglove 协议适配（JSON parse + topic 分发）
- 但当前实现是消费 `telemetry_bridge.py` 的"非标"JSON 帧（`{topic, payload}`），需切换到 foxglove 标准协议（subscribe / advertise 协商）

**修复步骤**：
1. 删除 `src/sil_orchestrator/telemetry_bridge.py` + `main.py:26,45` 引用
2. orchestrator 仅留 REST 控制面
3. 重写 `useFoxgloveLive.ts` 用 `@tier4/roslibjs-foxglove` 标准 ROS2 client（已在 package.json 依赖）
4. foxglove_bridge :8765 唯一占有

**修复 effort 估计**：~1 person-week（D1.3b.3 调整范围内）。

### 4.3 11 topic 订阅清单（useFoxgloveLive.ts:38-91）

```
/sil/own_ship_state        → updateOwnShip (50 Hz)
/sil/target_vessel_state   → updateTargets (10 Hz)
/sil/environment_state     → updateEnvironment (1 Hz)
/sil/module_pulse          → updateModulePulses (10 Hz)
/sil/asdr_event            → appendAsdrEvent (event)
/sil/lifecycle_status      → updateLifecycleStatus (1 Hz)
/sil/scoring_row           → updateScoringRow (1 Hz)
/sil/sensor_status         → updateSensors (event)
/sil/commlink_status       → updateCommLinks (event)
/sil/fault_status          → updateFaultStatus (event)
/sil/control_cmd           → updateControlCmd (10 Hz)
/sil/preflight_log         → appendPreflightLog (event)
```

reconnect 指数退避 1s → 30s（已实现 useFoxgloveLive.ts:7-8）。

### 4.4 50 Hz 性能预算

| 帧大小估算 | 50 Hz 字节/s | 50 Hz 字节/min |
|---|---|---|
| `own_ship_state` JSON ~300 B | 15 KB/s | 900 KB/min |
| `target_vessel_state` JSON 3×~280 B | 8.4 KB/s @ 10 Hz | 504 KB/min |
| `module_pulse` JSON 8×~80 B | 6.4 KB/s @ 10 Hz | 384 KB/min |
| 总计（DEMO-1 范围）| ~30 KB/s | ~1.8 MB/min |

**foxglove WebSocket protocol vs 自制 JSON 实测**（[W50] foxglove ws-protocol GitHub + ubuntu-robotics fork benchmark 2026-02 🟢）：50 Hz / 64-byte payload — foxglove (Protobuf channel) ~400 Kbps、自制 JSON ~500 Kbps；差异 ~15-20%。GigE 局域网带宽 ~125 MB/s → 占用 < 0.1%，性能预算极充裕。Protobuf 切换收益主要在 schema 协商 + 版本管理，而非带宽，v1.0 取 ROS2 .msg + JSON 桥（GAP-019）+ foxglove 标准 protocol 协商（GAP-015 选项 A）。

---

## 5. 设计语言（tokens.css + OpenBridge）

### 5.1 现状（`web/src/styles/tokens.css` 92 行）

实际 token 系统（commit 4fc0522 落地）：

```css
:root {
  /* Surface 4 levels (Night Mode only) */
  --bg-0: #070C13;   /* page bg */
  --bg-1: #0B1320;   /* panel */
  --bg-2: #101B2C;   /* panel inner */
  --bg-3: #16263A;   /* panel deepest */

  /* Border 3 levels */
  --line-1/2/3: #1B2C44 / #243C58 / #3A5677;

  /* Text 4 levels */
  --txt-0/1/2/3: #F1F6FB / #C5D2E0 / #8A9AAD / #566578;

  /* Semantic ECDIS colors */
  --c-phos    #5BC0BE;  /* 主操作 cyan */
  --c-stbd    #3FB950;  /* starboard 绿 */
  --c-port    #F26B6B;  /* port 红 */
  --c-warn    #F0B72F;  /* warning 琥珀 */
  --c-info    #79C0FF;  /* info 蓝 */
  --c-danger  #F85149;  /* danger 红 */
  --c-magenta #D070D0;  /* COLREGs 紫 */

  /* IMO MASS Code 4-level autonomy */
  --c-d4 #3FB950 (green)  --c-d3 #79C0FF (blue)  --c-d2 #F0B72F (amber)  --c-mrc #F85149 (red)

  /* Font 3 stacks (CJK + display + mono) */
  --f-disp: 'Saira Condensed' + 'Noto Sans SC'  (label, button, header)
  --f-body: 'Noto Sans SC' + 'Saira Condensed'  (paragraph)
  --f-mono: 'JetBrains Mono'                     (numeric tabular)

  /* Spacing + radius (strict zero-radius for bridge HMI) */
  --r-0: 0  --r-min: 2px
  --sp-xs/sm/md/lg/xl: 4/8/12/18/24 px
}

/* Atoms */
.hmi-surface / .hmi-mono / .hmi-disp / .hmi-label   (utility classes)

/* Keyframes */
@phos-pulse / @radar-sweep / @warn-flash / @scan-line
```

### 5.2 设计语言锁定项

| 项 | 锁定 | 来源 |
|---|---|---|
| **配色模式** | **仅 Night Mode**（不做 day/dusk 切换）| 决策记录 §9.4 / 用户 2026-05-12 决策；ECDIS Day/Dusk 留 Phase 4 |
| **配色基线** | OpenBridge palette 对齐 IEC 62288 SA subset + S-Mode | [W14][W17][W30] |
| **Autonomy 配色** | IMO MASS Code 4 级（D4/D3/D2/MRC）映射到 c-d4/d3/d2/mrc | 架构报告 §1.3 |
| **角半径** | `--r-0: 0` 严格零角半径（panel / button / pill 默认）| Bridge HMI 视觉惯例（高密度信息 + 低视觉噪音）|
| **字体** | Saira Condensed（label/button）+ Noto Sans SC（中文 / 主体）+ JetBrains Mono（数字）| 用户决策 |

### 5.3 OpenBridge 版本与对齐（W-pending-1）

[W14] OpenBridge openbridge.no — A 🟡（版本号 subagent a6f58a22 调研中，预期返回精确 git tag）。

v1.0 立场：
- token 命名沿用 OpenBridge 风格（`--bg-N` / `--c-*` / `--txt-N`），无 1:1 SemVer 依赖
- 主合规依据：**IEC 62288:2021 Edition 3.0**（[W58] IEC 62288:2021-12 *Presentation of navigation-related information on shipborne navigational displays* 388 页 — A 🟢）+ IMO MSC.191(79) + CCS 技术通告对齐 day/dusk/night 一致性
- ECDIS Day/Dusk 色板（W15 S-52 Ed 3.0 1996 + IEC 62288:2021）作未来 Phase 4 扩展，**v1.0 不实施**
- 若 subagent 后续返回 OpenBridge v5.x React 组件库已发布，Phase 4 可考虑替换 `lucide-react` + 自制 atom 为官方组件

**IEC 62288 Day/Dusk/Night 推荐配色对照**（Phase 4 实施参考，[W58] inferred）：

| Mode | Background | Foreground | Highlight | 理由 |
|---|---|---|---|---|
| Day | `#FFFFFF` | `#000000` | `#FF6600` 琥珀 | 高对比，无眩光 |
| Dusk | `#333333` | `#FFFFFF` | `#FFFF00` 黄 | 黄昏可读性 |
| Night（v1.0 唯一）| `#070C13` (`--bg-0`) | `#C5D2E0` (`--txt-1`) | `#5BC0BE` (`--c-phos`) / `#F85149` (`--c-danger`) | 夜间瞳孔暗适应保留 |

---

## 6. 屏 ① Simulation-Scenario（`/scenario`）

### 6.1 现状（`web/src/screens/ScenarioBuilder.tsx`，16.9 KB）

3-step layout（commit 62a5f1e）：
- **Step A** 模板选 — Imazu22 / R13/R14/R15 / AIS-derived / Procedural
- **Step B** 参数 — bearing / distance / course variation / ODD cell / disturbance
- **Step C** 几何预览 + summary rail → "Run →"

辅助组件：
- `Stepper.tsx`（1.9 KB）3-step 进度条
- `ImazuGrid.tsx`（4.1 KB）22 场景缩略图网格
- `SummaryRail.tsx`（4.4 KB）右侧汇总
- `BuilderRightRail.tsx`（12.4 KB）右侧 4 tab（Encounter / Scenarios / AIS / High-fidelity）
- `SilMapView` 注入到几何预览区

### 6.2 v1.0 目标设计

```
┌───────────────────────────────────────────────────────────────────────────┐
│ TopChrome (站点 + 时钟 + run-state pill)                                  │
├───────────────────────────────────────────────────────────────────────────┤
│ ┌─ Stepper · A → B → C ──────────────────────────────────────────────┐  │
│ └──────────────────────────────────────────────────────────────────────┘  │
│                                                                            │
│ ┌─ Step A: 运行域选择 + 场景库浏览 ───────────────────────────────────┐  │
│ │  ┌── ODD Domain ──┐  ┌── Scenario Library (tabbed) ──────────────┐  │
│ │  │ open_sea       │  │ [Imazu-22] [COLREGs R13/14/15] [AIS] [Custom]│
│ │  │ coastal        │  │                                              │
│ │  │ fairway        │  │  ┌──┬──┬──┬──┐  ┌──┬──┬──┬──┐               │
│ │  │ port_entry     │  │  │01│02│03│04│  │05│...│...│22│  ImazuGrid    │
│ │  └────────────────┘  │  └──┴──┴──┴──┘  └──┴──┴──┴──┘               │
│ │                       │  (click → preview + B/C populated)          │
│ │  ┌── Visibility / Wind / Current sliders ──┐                         │
│ │  └──────────────────────────────────────────┘                         │
│ └────────────────────────────────────────────────────────────────────────┘
│                                                                            │
│ ┌─ Step B: 场景配置 (Builder Right Rail) ──────────────────────────────┐  │
│ │  Tab Encounter | Tab Scenarios | Tab AIS | Tab High-fidelity          │
│ │  Bearing/Distance/Course range sliders                                │
│ │  Target ship cards (1..3) — model: ais_replay / ncdm / intelligent    │
│ │  Disturbance vector / Sensor degradation (radar dropout %)             │
│ │  Seed                                                                  │
│ └────────────────────────────────────────────────────────────────────────┘
│                                                                            │
│ ┌─ Step C: 几何预览 + Summary Rail ─────────────────────────────────────┐  │
│ │  ┌── SilMapView (ENC + own-ship + targets + CPA rings + ImazuGeom) ──┐ │
│ │  │  shows current scenario YAML rendered as geometry                  │ │
│ │  └──────────────────────────────────────────────────────────────────┘ │
│ │  ┌── SummaryRail ─────────────────────────────────────────────────────┐ │
│ │  │  Scenario ID • Hash • ODD cell • Expected outcome                   │ │
│ │  │  [Save .yaml] [Run →]                                                │ │
│ │  └──────────────────────────────────────────────────────────────────┘ │
│ └────────────────────────────────────────────────────────────────────────┘
├───────────────────────────────────────────────────────────────────────────┤
│ FooterHotkeyHints (1/2/3 step / Esc back / Enter Run)                     │
└───────────────────────────────────────────────────────────────────────────┘
```

### 6.3 关键交互

1. 用户在 Step A 选 Imazu22 # 14（Head-on）
2. RTK Query `useGetScenarioQuery('imazu-14-ms')` 拉 YAML
3. `js-yaml.load(yaml_content)` 客户端解析 → Step B 卡片预填
4. Step C ENC 渲染 own-ship + target 几何
5. 用户调参 → `useCreateScenarioMutation` 创建副本 OR `useValidateScenarioMutation` 校验
6. "Run →" → `setScenario(id, hash)` to useScenarioStore + 跳 `#/check/:id`

### 6.4 GAP-022（新增）

`scenario.validate` 仅查空字符串（Doc 2 GAP-017），客户端无 schema 提示 — 用户改坏 YAML 后须等服务端 422。修复路径：
- D1.6：服务端实现 maritime-schema JSON Schema 校验
- D1.6：前端引入 `monaco-editor` + JSON Schema 集成实时报错（NICE）

---

## 7. 屏 ② Simulation-Check（`/check/:runId`）— **架构对齐重设计**

### 7.1 现状不足（用户标识"较弱"）

`screens/Preflight.tsx`（7.9 KB）+ `selfcheck_routes.py`（44 行 stub）：

- 5 项硬编码检查（ENC / ASDR / UTC / M1-M8 / Hash），全部"假通过"
- 600 ms 假延迟 sequential 显示
- 失败 detail 简陋（仅 `M1-M8 Check failed` 字符串）
- 无 **Doer-Checker 隔离验证**
- 无 **ODD 边界对齐验证**
- 无 **scenarios.expected_outcome 锁定**
- 无 **真正的 GO/NO-GO 判定逻辑**
- "SKIP PREFLIGHT" 按钮一键绕过 — **认证不可接受**

### 7.2 架构对齐 6-Gate Sequencer 设计

参考航空 EICAS / ECAM preflight + IEC 62366 medical pre-use + IMO MSC.302(87) BAM pre-conditions（[W51] DO-178C + [W52] IMO MSC.302(87) BAM 🟢）：

```
GATE 1: 系统物理就绪（System Readiness）
        - Docker compose 5 service 全部 healthy
        - ROS2 DDS discovery: orchestrator + sim_workbench + L3 kernel 全部互见
        - foxglove_bridge :8765 listening
        - martin :3000 tile server responsive
        - useTelemetryStore.wsConnected === true
        判定：≥ 5/5 → PASS

GATE 2: 模块健康（M1-M8 Module Pulse）
        - 8 个 modulePulse 全部 GREEN（state === 1）
        - 各模块 latencyMs < 50 ms
        - 各模块 messageDrops === 0
        - **M7 进程独立验证**：M7 PID 不在 component_container 下（Doer-Checker 隔离硬约束）
        判定：8/8 GREEN + M7 独立 → PASS

GATE 3: 场景完整性（Scenario Integrity）
        - GET /scenarios/{id} 返回 yaml_content + SHA256 hash
        - 服务端 + 客户端独立 SHA256 复算 → bit-perfect 一致
        - useScenarioStore.scenarioHash === server.hash
        - maritime-schema 校验通过（Doc 2 GAP-017 修复后）
        - YAML 含 expected_outcome 块（cpa_min_m_ge / colregs_rules / grounding: forbidden）
        判定：hash 一致 + schema 通过 + expected_outcome 完整 → PASS

GATE 4: ODD 与场景对齐（ODD-Scenario Alignment）
        - scenario.metadata.odd_cell.domain ∈ {open_sea, coastal, fairway, port_entry, ofw}
        - scenario.metadata.odd_cell.visibility_nm ≥ 0.1
        - scenario.metadata.odd_cell.sea_state_beaufort ≤ 9
        - M1 ODD state ∈ scenario odd_cell（请求 M1 /health endpoint）
        - ODD 越界 → 不允许 ACTIVATE（架构决策一：ODD 是组织原则）
        判定：M1 ODD 模式与场景声明域一致 → PASS

GATE 5: 时基与证据链（Time Base + Evidence Chain）
        - UTC PTP drift < 10 ms（NTP/PTP query）
        - sim_clock 已 publish /clock topic（订阅 1s 内收到）
        - rosbag2_recorder 进程在线（service /rosbag2/status）
        - ASDR endpoint 可写（验证 runs/{run_id}/ 目录可写）
        - L3 M8 ASDR topic publishing（订阅 /l3/asdr_record 1s 内收到 1 帧）
        判定：4/4 → PASS

GATE 6: Doer-Checker 隔离合规（Doer-Checker Independence Verification）
        - M7 进程独立（PID 不同于 m1-m6 + m8 进程组）
        - M7 容器独立（docker inspect 不同 container ID）
        - M7 不依赖 OR-Tools（M7 import list lint，CI 已强制；此处展示验证结果）
        - /l3/checker_veto topic 可订阅 + history depth ≥ 10
        - M7 一个测试 veto（注入 dummy violation）能在 50 ms 内回灌到 M5
        判定：5/5 → PASS

────────────────────────────────────────────────────────────────────────
GO/NO-GO 判定（合取）：
  ALL 6 GATE = PASS  → GO（绿灯 3-2-1 倒数自动 activate）
  ANY GATE = FAIL    → NO-GO（显示失败 gate + detail + 必填 reason 才能 SKIP，
                              SKIP 必写入 ASDR + verdict 标 "warning_unverified_run"）
────────────────────────────────────────────────────────────────────────
```

### 7.3 UI 设计

```
┌─────────── SIMULATION-CHECK ────────── scenario: imazu-14-ms ──────────┐
│                                                                          │
│ ╔═══════════════════════════════════════════════════════════════════╗  │
│ ║  GATE 1 · 系统物理就绪          [5/5 ●●●●●]  PASS  ▼           ║  │
│ ║    ✓ docker compose 5/5 healthy        2 ms                        ║  │
│ ║    ✓ ROS2 DDS discovery               1 ms                         ║  │
│ ║    ✓ foxglove_bridge :8765            3 ms                         ║  │
│ ║    ✓ martin :3000 tile server         4 ms                         ║  │
│ ║    ✓ telemetry WS connected           1 ms                         ║  │
│ ╚═══════════════════════════════════════════════════════════════════╝  │
│ ╔ GATE 2 · 模块健康 M1–M8 + M7 隔离  [9/9 ●]  PASS  ▼              ╗  │
│ ╔ GATE 3 · 场景完整性 + hash + schema  [4/4]  PASS  ▼               ╗  │
│ ╔ GATE 4 · ODD–场景对齐                [3/3]  PASS  ▼               ╗  │
│ ╔ GATE 5 · 时基 + 证据链                [4/4]  PASS  ▼               ╗  │
│ ╔ GATE 6 · Doer-Checker 隔离合规       [5/5]  PASS  ▼               ╗  │
│                                                                          │
│         ┌──────────────────────────────────────────────┐                │
│         │           GO / NO-GO                          │                │
│         │                                                │                │
│         │           ALL 6 GATES PASS                     │                │
│         │                                                │                │
│         │                  ⓿  ⓿  ⓿                       │                │
│         │             GO    3                            │                │
│         │                                                │                │
│         │       AUTO-ACTIVATING IN 3 SECONDS…            │                │
│         │                                                │                │
│         │     [ABORT]    [PROCEED NOW]                   │                │
│         └──────────────────────────────────────────────┘                │
│                                                                          │
│ ── LiveLogStream (preflight_log topic, scrollable, last 200 lines) ──    │
└──────────────────────────────────────────────────────────────────────────┘
```

### 7.4 失败路径

- 任何 Gate FAIL → 红框 + 展开详细 + 显示"DEACTIVATE + RECONFIGURE"按钮（自动跑 `useCleanupLifecycleMutation`，返回 ①）
- "SKIP PREFLIGHT" 按钮 **移除**（认证不可接受）
- 唯一例外：dev mode（`?dev=1` query param）才显示 SKIP，且 SKIP 时写 ASDR + 标 verdict `warning_unverified_run`

### 7.5 实施 GAP（新增）

| GAP | 描述 |
|---|---|
| **GAP-023** | Preflight.tsx 5-gate 硬编码 → 6-gate 真实 sequencer 重写（含 Doer-Checker 隔离验证）|
| **GAP-024** | selfcheck_routes.py stub → 后端 6-gate 真实查询（参 Doc 2 GAP-005）|
| **GAP-025** | SKIP PREFLIGHT 按钮在 production build 移除 + ASDR 记 warning verdict 落地 |

### 7.6 引用与置信度

- [W51] DO-178C *Software Considerations in Airborne Systems* — A 🟢（航空 SLI 范式）
- [W52] IMO MSC.302(87) BAM + IEC 62923-1:2018 — A 🟢
- [W53] IEC 62366-1:2015 *Medical devices Usability* — A 🟢（pre-use check 模式）
- [W54] Chen et al. 2014 SAT framework — A 🟢（subagent a6f58a22 pending 返回更精确 UI 模式）

---

## 8. 屏 ③ Simulation-Monitor（`/monitor/:runId`）

### 8.1 现状（`BridgeHMI.tsx` 17.5 KB，commit cbbd1cb）

双模式（船长 / God）已实现，全屏 MapLibre + 浮窗 HUD + 全部 shared 组件已就位（commits 5316213 + 018223b + a40d950 + 6f820ff）。

### 8.2 屏布局（双模式）

```
模式 A · CAPTAIN（船长视图，默认）
┌─────────── TopChrome (Project / DualClock / RunStatePill / mode switch) ─────────┐
│                                                                                    │
│   ┌─ ConningBar (7-field with sparkline: HDG/SOG/COG/ROT/RUD/THR/DEPTH) ──────┐   │
│   │                                                                              │   │
│   └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                    │
│   ╔═══════════════════ Full-screen MapLibre · ENC (S-57 MVT) ═══════════════╗   │
│   ║                                                                            ║   │
│   ║   Heading-up own-ship sprite (centered, fixed)                             ║   │
│   ║   Heading vector (COG 6-min forecast line)                                 ║   │
│   ║   Target sprites + CPA rings + COG vectors (color: c-d4/d3/d2/mrc)         ║   │
│   ║   ImazuGeometry overlay (rule 13/14/15 sector lines)                       ║   │
│   ║   ColregsSectors (135°/112.5° classification sectors)                      ║   │
│   ║   PpiRings + DistanceScale + CompassRose                                   ║   │
│   ║   ENC layers (depth / land / nav-aids / restricted areas)                  ║   │
│   ║                                                                            ║   │
│   ║   ┌── ThreatRibbon (top, CPA-sorted target chips) ────────────────────┐   ║   │
│   ║   │  [TGT-12 CPA 0.42nm 4min RED] [TGT-08 CPA 0.91nm 7min AMBER]      │   ║   │
│   ║   │                                                                     │   ║   │
│   ║   └─────────────────────────────────────────────────────────────────┘   ║   │
│   ║                                                                            ║   │
│   ║   ┌── ASDR Ledger Panel (bottom-right, collapsible) ──────────────────┐   ║   │
│   ║   │  [10:42:17] Rule 14 detected (TGT-12 head-on)                       │   ║   │
│   ║   │  [10:42:18] M5 plan: starboard 5° turn, hold 60s                    │   ║   │
│   ║   │  [10:42:18] M7 PASS  (CPA forecast 0.51nm @ TCPA 3.2min)            │   ║   │
│   ║   │  [10:42:19] L4 actuator: rudder +5°                                 │   ║   │
│   ║   └─────────────────────────────────────────────────────────────────┘   ║   │
│   ║                                                                            ║   │
│   ╚════════════════════════════════════════════════════════════════════════════╝   │
│                                                                                    │
│   ┌─ Module Pulse 16px strip (M1-M8 GREEN/AMBER/RED, latency μs) ──────────┐   │
│   └─────────────────────────────────────────────────────────────────────┘   │
└────────────────────────────────────────────────────────────────────────────┘
└── FooterHotkeyHints (P=pause / R=run / S=speed / F=fault / G=god view) ────┘

模式 B · GOD（全场景俯瞰）
- 全 own + targets 同屏可见
- 无 heading-up 旋转，north-up
- 双侧 panel 可同时展开（ArpaTargetTable 左侧 + AsdrLedger 右侧）
```

### 8.3 已实现的 22 共享组件

| 组件 | 大小 | 用途 |
|---|---|---|
| `TopChrome.tsx` | 9.2 KB | 顶部 chrome（项目 logo / DualClock / RunStatePill / mode switch / navigate buttons）|
| `DualClock.tsx` | 2.3 KB | UTC + sim-time 双时钟 |
| `RunStatePill.tsx` | 2.1 KB | 5-state pill：IDLE / ARMING / RUNNING / REPORT |
| `ConningBar.tsx` | 3.5 KB | 7 字段 conning HUD + sparkline |
| `ThreatRibbon.tsx` | 3.3 KB | CPA-sorted target chips（cpa < 0.5 nm 红 / 0.5–1.0 黄 / > 1.0 绿）|
| `AsdrLedger.tsx` | 6.1 KB | 实时决策日志面板，可折叠 |
| `ArpaTargetTable.tsx` | 4.3 KB | ARPA-style target 表（MMSI/CPA/TCPA/BCR/BCT/COG/SOG）|
| `ColregsDecisionTree.tsx` | 1.8 KB | 当前激活规则可视化（R5/6/7/8/13-17/19）|
| `ModuleReadinessGrid.tsx` | 3.4 KB | M1-M8 8 格状态网格（屏 ②）|
| `ModuleDrilldown.tsx` | 2.3 KB | 点 M{N} 后的详细 panel |
| `SensorStatusRow.tsx` | 2.2 KB | 8 传感器健康行 |
| `CommLinkStatusRow.tsx` | 1.7 KB | 6 通信链路健康 |
| `LiveLogStream.tsx` | 3.2 KB | 流式日志（ASDR / preflight_log）|
| `FaultInjectPanel.tsx` | 4.6 KB | ⚠ 故障注入 panel（ais_dropout / radar_spike / dist_step）|
| `TorModal.tsx` | 8.0 KB | SAT-1 锁 + TMR 倒计时 + auto-MRC（Phase 3 完整化）|
| `ScoringGauges.tsx` | 2.2 KB | 6 维评分仪表 |
| `ScoringRadarChart.tsx` | 2.4 KB | 6 维雷达图（屏 ④）|
| `TimelineSixLane.tsx` | 4.2 KB | 6 泳道时间轴（own / target / M4 / M5 / M6 / M7，屏 ④）|
| `TrajectoryReplay.tsx` | 5.2 KB | scrub timeline replay（屏 ④）|
| `Stepper.tsx` | 1.9 KB | 3-step Stepper（屏 ①）|
| `ImazuGrid.tsx` | 4.1 KB | 22 场景网格（屏 ①）|
| `SummaryRail.tsx` | 4.4 KB | 右侧 summary（屏 ①）|
| `BuilderRightRail.tsx` | 12.4 KB | 右侧 4 tab（屏 ①）|
| `FooterHotkeyHints.tsx` | 2.7 KB | 底栏快捷键提示（全屏）|

### 8.4 双模式切换

`useUIStore.viewMode: 'captain' | 'god' | 'roc'`：

- **Captain**（默认）— heading-up，own-ship 居中 + 旋转地图
- **God** — north-up，全场景固定缩放 + 多 panel 同屏
- **ROC**（Phase 2）— 远程操控员视图，简化 + 更大字体 + ToR 倒计时优先级最高

切换方式：TopChrome mode switch + 快捷键 G。

### 8.5 8 交互动作映射（Doc 1 §6.3）

| Hotkey / Action | API | 内部 |
|---|---|---|
| `P` Pause | `simClockSetRate(0)` | useControlStore.setIsPaused(true) |
| `R` Resume | `simClockSetRate(1)` | setIsPaused(false) |
| `1/2/4` Speed × | `simClockSetRate(N)` | useControlStore.setSimRate(N) |
| `0` Reset | `cleanup → configure` | useScenarioStore reset |
| `F` Fault | open FaultInjectPanel | useInjectFaultMutation |
| `S` Stop | `useDeactivateLifecycleMutation` | 跳 `#/evaluator/:id` |
| `G` View | toggle viewMode | useUIStore.setViewMode |
| `Esc` Back | navigate back | window.history.back |

### 8.6 SAT-1/2/3 落地（Chen 2014 + M8 输出）

| SAT 级 | 显示位置 | 数据源 |
|---|---|---|
| **L1 当前状态** | ConningBar + ThreatRibbon (头部位置) | `l3_msgs/SAT1Data` |
| **L2 推理** | AsdrLedger + ColregsDecisionTree | `l3_msgs/SAT2Data` + 决策链 |
| **L3 不确定/预测** | TrajectoryGhost overlay (Phase 3 D3.4) + 置信带 | `l3_msgs/SAT3Data` |

### 8.7 GAP-026（新增）

`telemetry_bridge.py` 退役后（GAP-015 选项 A），`useFoxgloveLive.ts` 需切到 `@tier4/roslibjs-foxglove` 标准 client。当前实现是消费自制 JSON 帧 `{topic, payload}` 格式。

---

## 9. 屏 ④ Simulation-Evaluator（`/evaluator/:runId`）

### 9.1 现状（`RunReport.tsx` 11.6 KB，commit dba4149）

8 KPI cards + TimelineSixLane + AsdrLedger + TrajectoryReplay 全部就位。

### 9.2 v1.0 设计

```
┌────────── TopChrome · run-id: run-1abc2def · scenario: imazu-14-ms ───────┐
├────────── Verdict Banner ─────────────────────────────────────────────────┤
│ ┌──────────────────────────────────────────────────────────────────────┐ │
│ │  ✅ PASS                CPA min 0.42 nm    Rule 14 + 8 + 16 compliance │ │
│ │                          Duration 5min 42s  6-dim total 0.87             │ │
│ └──────────────────────────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────────────────────────┤
│ ┌── 8 KPI Cards ────────────────────────────────────────────────────────┐ │
│ │ [min CPA] [TCPA min] [avg ROT] [max rudder] [grounding risk]          │ │
│ │ [route deviation] [time-to-MRM] [decision count]                       │ │
│ └────────────────────────────────────────────────────────────────────────┘ │
│ ┌── 6-dim Scoring Radar Chart ──┬── COLREGs Rule Chain ──────────────────┐ │
│ │ safety   ●●●●○ 0.92            │ Rule 14 Head-on   detected @ T+102s     │ │
│ │ rule     ●●●●○ 0.88            │ Rule 8  Action    initiated @ T+105s     │ │
│ │ delay    ●●●○○ 0.65            │ Rule 16 Give-way  applied   @ T+108s     │ │
│ │ magnitude●●●●● 0.95            │ ...                                       │ │
│ │ phase    ●●●●○ 0.82            │                                            │ │
│ │ plausibil●●●●● 0.99            │                                            │ │
│ │ TOTAL = 0.87 (PASS ≥ 0.70)     │                                            │ │
│ └────────────────────────────────┴────────────────────────────────────────┘ │
│ ┌── TimelineSixLane (scrub-able, 0 → end) ──────────────────────────────┐ │
│ │ Lane 0 own-ship       ────●●●●●●●●●●────                                │ │
│ │ Lane 1 target-12      ──●●●●●●●●●●●─                                    │ │
│ │ Lane 2 M4 behavior    ────[avoid]──────                                  │ │
│ │ Lane 3 M5 plan        ────[★ ★ ★]──                                      │ │
│ │ Lane 4 M6 COLREGs     ────[R14][R8]──                                    │ │
│ │ Lane 5 M7 verdict     ────[PASS]──                                       │ │
│ │ ⏵ scrub handle                                                            │ │
│ └────────────────────────────────────────────────────────────────────────┘ │
│ ┌── ASDR Ledger (full, filterable) ────┬── Trajectory Replay (Map) ──────┐ │
│ │ [10:42:17] Rule 14 ...                 │  scrub-locked SilMapView         │ │
│ │ [10:42:18] M5 plan: starboard 5°       │  shows current scrub time         │ │
│ │ [10:42:18] M7 PASS                     │  ghost trajectory                  │ │
│ │ [10:42:19] L4 actuator: rudder +5°     │                                    │ │
│ │ ...                                     │                                    │ │
│ └────────────────────────────────────────┴────────────────────────────────┘ │
│ ┌── [PASS/FAIL] [Export .marzip] [Export .csv] [New Run]──────────────────┐ │
│ └────────────────────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────────────────┘
```

### 9.3 Export 流程

1. 用户点 `[Export .marzip]`
2. `useExportMarzipMutation({ run_id })` → POST /api/v1/export/marzip
3. orchestrator 后台 task 构建（Doc 2 §2.3 export_routes）
4. 前端轮询 `useGetExportStatusQuery(run_id)` 0.5 Hz
5. status === 'complete' → download_url 出现 → 自动触发 `<a href download>`

Marzip 内容（Doc 4 §10 完整）：scenario.yaml + sha256 + scoring.json + (Phase 2) results.bag.mcap + (Phase 2) results.arrow + (Phase 2) asdr_events.jsonl + manifest.yaml + verdict.json。

### 9.4 GAP-027（新增）

8 KPI cards 当前 4 字段（min_cpa_nm / avg_rot_dpm / distance_nm / duration_s）来自 orchestrator stub（Doc 2 GAP-021）。8 字段完整化（max_rudder / grounding_risk / route_deviation / time_to_mrm / decision_count）须 scoring_node 真输出 + Arrow 后处理。

---

## 10. 跨屏一致性

### 10.1 状态在屏之间流动

```
   ① Builder           ② Check               ③ Monitor               ④ Evaluator
   ─────────           ─────────              ─────────               ─────────
                                                                       
   scenarioId   ─────► useScenarioStore ◄─── scenarioId  ◄────────── scenarioId
   (user pick)        .scenarioId               (re-confirm)            (display)
                                                                       
   runId        ─────► useScenarioStore ◄─── runId       ◄────────── runId
                       .runId                   (active)                 (current)
                                                                       
   scenarioHash ─────► useScenarioStore                                
                       .scenarioHash                                  
                                                                       
   viewport     ─────► useMapStore                                  ◄── viewport
   (drag/zoom)         + localStorage          (continue)              (final state)
                                                                       
                       useFsmStore ◄────────  FSM 6-state           ◄── transition history
                                              (real-time)               (replay)
                                                                       
                                              useReplayStore  ◄────── scrubTime
                                              + mcapDuration           (interactive)
```

### 10.2 全屏共享 chrome

- `TopChrome` 在所有屏顶部 — 项目名 + DualClock + RunStatePill + view mode + navigation
- `FooterHotkeyHints` 在所有屏底部 — 按屏切换显示对应快捷键

### 10.3 主题与可访问性

- **配色对比度**：所有文字 ≥ 4.5:1（WCAG AA），关键告警 ≥ 7:1（AAA）— [W55] WCAG 2.1 🟢
- **键盘可达**：所有交互组件支持 Tab + Enter + Space + Esc
- **屏阅读**：role + aria-label 注入到 panel / button / ledger
- **运动减弱**：`@media (prefers-reduced-motion: reduce)` 禁用 phos-pulse / radar-sweep / scan-line keyframes
- **触屏**：MapLibre native 支持；其他组件按需补 hit target ≥ 44 px（船长触屏面板场景）

---

## 11. 差距台账（GAP 增量）

| GAP | 描述 | 现状 | 修复路径 | D-task |
|---|---|---|---|---|
| **GAP-022** | Builder `validate` 客户端无 schema 提示 | scenario.validate 仅查空（Doc 2 GAP-017）| 引入 monaco-editor + JSON Schema 实时报错 | D1.6 NICE |
| **GAP-023** | Preflight 5-gate 硬编码 | 600ms 假延迟 | 重写 6-gate sequencer + Doer-Checker 隔离验证 | D1.3b.3 |
| **GAP-024** | selfcheck_routes stub | 5/5 假 PASS | 后端 6-gate 真实查询（Doc 2 GAP-005）| D1.3b.3 |
| **GAP-025** | SKIP PREFLIGHT production 残留 | 一键绕过 | production build 移除 + dev-only + ASDR 记录 | D1.3b.3 |
| **GAP-026** | useFoxgloveLive 消费非标 JSON 帧 | telemetry_bridge.py 自定义 `{topic,payload}` | 切 @tier4/roslibjs-foxglove 标准 protocol（GAP-015 选项 A 联动）| D1.3b.3 |
| **GAP-027** | 8 KPI cards 只有 4 字段实数据 | scoring stub 仅 4 字段 | scoring_node 真输出 + Arrow 后处理（联动 Doc 2 GAP-021）| D2.4 |
| **GAP-028** | OpenBridge 版本号 W14 🟡 待确认 | tokens.css 风格对齐但无 SemVer | subagent a6f58a22 调研返回后回填 | D1.3b.3 |
| **GAP-029** | 4 屏文件名 + 路由未重命名（联动 GAP-014）| ScenarioBuilder/Preflight/BridgeHMI/RunReport | rename to Simulation{Scenario,Check,Monitor,Evaluator} + 路由 | D1.3b.3 |

跨文档汇总（Doc 1 GAP-001 ~ GAP-014 + Doc 2 GAP-015 ~ GAP-021 + Doc 3 GAP-022 ~ GAP-029）共 **29 GAP**。

---

## 12. 文件谱系 + 调研记录（增量）

继承 Doc 1 §13.2 + Doc 2 §13 引用编号。本 Doc 新增：

- [W49] `@tier4/roslibjs-foxglove`（github.com/tier4/roslibjs-foxglove）v0.0.4 — B 🟡（tier4 maintained fork，活跃；ROS2 + Foxglove WS 协议 client）
- [W50] *foxglove_bridge protocol vs roll-your-own WebSocket* (docs.foxglove.dev) — A 🟢
- [W51] DO-178C *Software Considerations in Airborne Systems Certification* — A 🟢（航空 SLI 范式 → preflight check）
- [W52] IMO MSC.302(87) BAM + IEC 62923-1:2018 *Alert management* — A 🟢
- [W53] IEC 62366-1:2015 *Medical devices · Usability engineering* — A 🟢
- [W54] Chen et al. 2014 *SAT framework* — A 🟢
- [W55] WCAG 2.1 Level AA / AAA — A 🟢
- [W56] *MapLibre style-spec for vessel symbol layers* — A 🟢
- [W57] *Veitch 2024 60-second TMR baseline* — A 🟢（架构报告 §11 引用）
- [W58] IEC 62288:2021-12 Edition 3.0 *Presentation of navigation-related information on shipborne navigational displays* — A 🟢（388 页主合规依据，day/dusk/night 一致性）
- [W59] Zustand v5 `useShallow()` docs — A 🟢（React 18 fine-grain selector）
- [W60] foxglove `ws-protocol` GitHub + ubuntu-robotics fork benchmark (2026-02) — A 🟢（50Hz/64B 400 Kbps vs 自制 JSON 500 Kbps）
- [W61] IMO MSC.191(79) + CCS 技术通告 — A 🟢（day/dusk/night 一致性合规背书）

**subagent 2026-05-15 调研返回**：

- ✅ foxglove_bridge schema 协商 vs 自制 JSON tradeoff（[W60] 量化 ~15-20% overhead）
- ✅ IEC 62288:2021 Ed 3.0 确认（[W58]）
- ✅ Zustand 50Hz `useShallow()` + 100ms debounce 模式（[W59]）
- 🟡 OpenBridge GitHub package.json 主版本（[W14] 仍 🟡，须 repo 直检；不阻塞 v1.0 交付）
- 🟡 SAT-1/2/3 to UI elements 实例（Chen 2014 已在 Doc 1 [W18] 锁定，UI 实例映射沿用 §8.6 表格）
- 🟡 ToR countdown 3-tier 模式（与 §11 设计一致：silent 0–20s → audio 20–45s → red+haptic 45–60s）

**待补研究**：
- [W-pending-1] OpenBridge GitHub package.json 主版本直检（D1.3b.3 启动前由开发者本地 `git clone` 一次性确认，回填 [W14]）
- [W-pending-3] Veitch 2024 *60-second TMR baseline* 完整 PDF 索引（Doc 4 §6 DEMO-3 ToR 验收前必须取得）

**置信度分布**：所有 🟢 High（9 项）+ 🟡 Medium（1 项 W49 tier4 fork 活跃度）。

---

## 13. 修订记录

| 版本 | 日期 | 改动 | 责任 |
|---|---|---|---|
| v1.0 | 2026-05-15 | 基线建立。整合 Doc 1 §6/§7 + Doc 2 §2/§7 + web/src 实际代码读（19 stores/screens/shared files + tokens.css + useFoxgloveLive + silApi + Preflight.tsx）+ Simulation-Check 架构对齐重设计（6-gate sequencer）+ GAP-015 决断（telemetry_bridge 退役）+ 4 屏命名统一（GAP-014/029）+ subagent web 调研 [W49–W61]（IEC 62288:2021 Ed 3.0 + foxglove protocol 量化 + Zustand useShallow + IEC 62288 day/dusk/night 配色对照）。29 GAP 入完整台账。| 套件维护者 |

---

*Doc 3 前端 v1.0 · 2026-05-15 · 与 Doc 1 + Doc 2 联动交付。Doc 4 场景联调将在用户评审通过后启动。*
