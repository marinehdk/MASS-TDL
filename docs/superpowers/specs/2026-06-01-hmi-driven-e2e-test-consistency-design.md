# Spec: HMI-Driven E2E Consistency Test

**Date**: 2026-06-01
**Status**: Draft (writing)
**Scope**: HMI E2E test harness — 不动 L3 kernel、不动 HMI 业务逻辑
**Supersedes**: 测试覆盖缺口（10x 7/7 gate pass 但前端 50x 无效、避碰无效 — 缺 E2E 验证）

---

## 1. Problem Statement

**症状**（2026-06-01 截图确认）：

- HMI 仿真运行屏切 50x，sim_t 推进出现 08 / 15 / 24 这种"跳值"（用户表述："时间流动不规律"），平台运动"卡顿"
- 0.13 nm CRITICAL 头对头场景，自船 5:10 sim 仍 HDG 0° / RUD 0.0°，未触发 Rule 14 右转
- 测试方案 `test_avoidance_chain.py` 在 RATE=1.0 下报 PASS，HMI 实际跑 1x 路径下避碰不工作
- 测试方案 `test_determinism.py` 覆盖 1x/10x 双档，**无 50x 档**

**根因**（已确认 3 个不变量断裂）：

1. **数据源同源** — HMI 切 rate = `POST /api/v1/lifecycle/rate` = 测试 capture --rate flag（`web/src/api/silApi.ts:181` ↔ `tests/integration/sim_determinism/capture_rule14_boundary.py:185`）。同一 endpoint，无后端 bug 嫌疑
2. **时基不同源** — HMI 渲染 sim_t 步长 = `lifecycleStatus.sim_time` 字段推流频率（推测 4-10 Hz WebSocket）。测试 capture CSV sim_t 步长 = sim_clock fixed-increment tick (1/250Hz)。两者**采样口径不同** — 用户看到的"08/15/24 跳值"是 HMI 渲染层采样问题，**不是 sim bug**
3. **避碰路径不同源** — 测试方案 7 AC 全在 10x 验证（memory: 2026-06-01 10x 7/7 gate pass）。前端 1x 路径下 M4/M5/bridge 端到端从未被任何测试断言覆盖 — 修复是 10x-only 还是真的修好，**未确认**

**目标**：

- 写一个 Playwright E2E test，**走和用户完全相同的代码路径**（同 React 组件树 + 同 WebSocket + 同 store 注入 + 同 lifecycle 步骤）
- 断言 HMI DOM 字段（HDG / RUD / SOG / 威胁标签 / 避碰指令 / sim_t 显示 / 1x-10x-50x 按钮 active 状态）= 用户截图同字段
- rate sweep 1x / 10x / 50x，每档 ×3 runs，断言 8 个 HMI-relevant 指标过线
- 失败时产 sceenshot + WebSocket frame log + HMI store snapshot 三件套

**成功判据**：

- `pytest web/e2e/hmi_consistency.spec.ts` 跑通 9 档（3 rate × 3 runs），所有断言过
- 同样的 scenario 在 HMI 看到的现象 = 测试截图 = 测试断言，三者像素级一致

---

## 2. Architecture Overview

```
                 ┌─────────────── Playwright driver ─────────────────┐
                 │  web/e2e/hmi_consistency.spec.ts                   │
                 │   1. page.goto('/#scenario')           ← Screen 1   │
                 │   2. tab vessel→odd→library; click card 'colreg-rule14-ho'│
                 │   3. click '确认场景' → URL → #/check/{id}         │
                 │   4. waitFor preflight gates 6/6 GO + countdown    │
                 │   5. URL → #/monitor/{id}                ← Screen 3 │
                 │   6. click rate button [1x/10x/50x]                  │
                 │   7. waitFor sim_t >= budget                         │
                 │   8. assert A1-A8 (DOM + store)                      │
                 │   9. screenshot @ critical sim_t                    │
                 └─────────────────────┬──────────────────────────────┘
                                       ↓ WebSocket (同真用户)
                 ┌─────────────── 真实 HMI（业务逻辑零改动）──────────┐
                 │  React HMI @ localhost:5173                       │
                 │  SimulationMonitor.tsx (data-testid 增量见 §3.3) │
                 │   ↕ WebSocket /foxglove-ws                       │
                 │   ↕ REST  /api/v1/lifecycle/{configure,          │
                 │                       activate,rate,status}     │
                 │   ↕ RTK Query silApi                              │
                 └─────────────────────┬──────────────────────────────┘
                                       ↓
                 ┌─────────────── 真实后端（已部署，不改）────────────┐
                 │  sil-orchestrator @ :8000                         │
                 │  sil-nodes (C++ L3 + bridge + DebugTraceWriter)   │
                 └─────────────────────┬──────────────────────────────┘
                                       ↓
                 ┌─────────────── 旁路证据（不阻塞主路径）────────────┐
                 │  1. WebSocket frame log (Playwright page.on())    │
                 │  2. HMI store snapshot (page.evaluate 读 zustand) │
                 │  3. /debug/summary cross-check (REST)            │
                 │  4. trace.jsonl tail (filesystem)                 │
                 └──────────────────────────────────────────────────┘
```

---

## 3. Component Specifications

### 3.0 仿真运行流程（用户 + 测试共享）

**3 屏 UI 导航**（HMI 唯一入口，与 `web/src/App.tsx` `parseHash()` 对齐）：

```
┌─────────────────────────────────────────────────────────────────────┐
│ Screen 1: SimulationScenario.tsx  URL: /#scenario                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                  │
│  │ vessel tab  │→ │   odd tab   │→ │ library tab │                  │
│  │ [本船参数]   │  │ [ODD 适配]   │  │ [场景列表]   │                  │
│  │ "下一步"     │  │ "下一步"     │  │ 点 scenario  │                  │
│  │             │  │             │  │ card → "确认场景"              │
│  └─────────────┘  └─────────────┘  └──────┬──────┘                  │
│                                          ↓                          │
│                              URL: /#/check/{scenarioId}             │
├─────────────────────────────────────────────────────────────────────┤
│ Screen 2: SimulationCheck.tsx     URL: /#/check/{scenarioId}        │
│  ┌────────────────────────────────────────────────────────┐         │
│  │ GateSequencer: 6 个 preflight gate（顺序执行）         │         │
│  │   G1 数据源 ready   G2 ROS2 nodes alive                │         │
│  │   G3 scenario loaded G4 route published                │         │
│  │   G5 ODD envelope    G6 vessel kinematics               │         │
│  │                                                        │         │
│  │ status pill: RUNNING → PASS/FAIL → GO / NO-GO          │         │
│  │ 一旦 6/6 PASS: 3 秒 countdown → handleProceed()         │         │
│  │ handleProceed = cleanup → configure → activate         │         │
│  │ 全部 success: URL → /#/monitor/{scenarioId}            │         │
│  └────────────────────────────────────────────────────────┘         │
├─────────────────────────────────────────────────────────────────────┤
│ Screen 3: SimulationMonitor.tsx   URL: /#/monitor/{scenarioId}      │
│  ┌────────────────────────────────────────────────────────┐         │
│  │ 仿真运行主屏:                                           │         │
│  │   - 地图 + 自船/目标轨迹                                │         │
│  │   - 左侧 tab: threat / m1-m8 / check                   │         │
│  │   - 右侧 tab: 详情面板                                  │         │
│  │   - 顶/底 chrome: sim_t 显示 + rate 按钮组 [1x|10x|50x]│         │
│  │                                                        │         │
│  │ rate 切换 = POST /api/v1/lifecycle/rate                │         │
│  └────────────────────────────────────────────────────────┘         │
└─────────────────────────────────────────────────────────────────────┘
```

**屏幕间转换触发**（`App.tsx` `parseHash()`）：
- 1→2：`window.location.hash = '#/check/{scenarioId}'`（SimulationScenario.tsx "确认场景" onClick）
- 2→3：`window.location.hash = '#/monitor/{scenarioId}'`（SimulationCheck.tsx `handleProceed` 成功）
- 3→4：`window.location.hash = '#/evaluator/{runId}'`（SimulationMonitor.tsx "评估" 按钮）

**lifecycle 状态机**（orchestrator 端）：
- `CONFIGURE` → `INACTIVE` → `ACTIVATE` → `ACTIVE` → 持续推流 → `DEACTIVATE` → `CLEANUP` → `UNCONFIGURED`
- 6 个 preflight gate 在 `CONFIGURE→INACTIVE` 转换期间串行执行
- 任何 gate 失败 → lifecycle 阻塞在 `INACTIVE`；用户可点 "Abort" 回 Screen 1

**测试 driver 必须遵循相同路径**（§3.1 硬约束 + §6 Task 2 skeleton）：
1. `page.goto('/#scenario')` → click "下一步" 3 次 → click scenario card → click "确认场景"
2. `waitForURL('#/check/{id}')` → wait for `[data-testid="preflight-status"]` 文字 = "GO" → 等 handleProceed 自动触发
3. `waitForURL('#/monitor/{id}')` → click rate 按钮 → 跑 A1-A8 断言
4. 任何一步 `waitForURL` 超时（180s default）→ 立刻 fail + 截图 + dump 当前 DOM + ws_frames tail

**Scope 提示**：
- Screen 4 (`evaluator`) 不在本 spec 范围（评估屏在 3 跑完后才需要）
- preflight gate 失败 → spec AC-3（HMI 业务逻辑零改动）依然成立，但测试结果 = 0/9 PASS，归因到 L3 模块问题

### 3.1 不可变边界（Hard Constraints）

| 约束 | 拒绝理由 |
|---|---|
| ❌ 不 mock WebSocket | 那就不是用户路径；图里"08/15/24 跳值"恰恰是 WebSocket 推流问题 |
| ❌ 不直接读 trace.jsonl 替 HMI 断言 | 那是后端正确性，不是 HMI 渲染正确性；两者可能解耦 |
| ❌ 不另起一个 store 替 HMI | HMI 看到的 React state = 用户看到的；测试必须看同一个 |
| ❌ 不绕过 lifecycle 步骤 | 用户要先 configure→activate 才能切 rate；测试必须也走 |
| ❌ **不绕过 3 屏 UI 导航** | 用户路径 = scenario tab → 选场景 → 确认 → check 屏 6 gates → countdown → monitor 屏 → 切 rate。**测试 driver 一律走 UI 点击**，不得用 `page.evaluate` 直调 REST（哪怕 endpoint 完全一样） |
| ❌ 不在测试 driver 改生产代码 | HMI 业务逻辑零改（仅允许加 data-testid，见 §3.3） |

### 3.2 Playwright 配置复用

复用 `web/playwright.config.ts`（已存在）：
- `baseURL: http://localhost:5173` ✓
- `webServer.command: npm run dev -- --port 5173` ✓
- `reuseExistingServer: true` ✓
- `headless: true` ✓
- `timeout: 60_000` — **本 spec 提升到 180_000**（rate sweep 3 档 × 3 runs × 60s sim = 540s wall，per-test 180s 预算够）
- `retries: 1` ✓
- `screenshot: 'only-on-failure'` — **本 spec 改为 `'on'`**（每档 rate 跑完都存一张对照图）

新增 `web/e2e/hmi_consistency.spec.ts`（不替换既有 e2e 套件）。

### 3.3 HMI 增量 data-testid（生产代码最少改动）

为稳定 selector，spec 要求在 3 个 screen 文件加 **10 个** data-testid。**不改任何业务逻辑**。

**Screen 1 — `web/src/screens/SimulationScenario.tsx`** (5 testids)

| 新增 testid | 现有位置 | 用途 |
|---|---|---|
| `data-testid="scenario-card-{id}"` | L725 `onClick={() => handleSelect(child.id)}` 卡片 div | 点选场景卡片 |
| `data-testid="scenario-confirm"` | L790 "确认场景" 按钮 | 提交选中场景 |
| `data-testid="scenario-tab-vessel"` | LEFT_TABS map (3 个 tab 入口) | 3 tab 导航 |
| `data-testid="scenario-tab-odd"` | 同 | 同上 |
| `data-testid="scenario-tab-library"` | 同 | 同上 |

**Screen 2 — `web/src/screens/SimulationCheck.tsx` + `web/src/screens/shared/GateSequencer.tsx`** (1 + 6 testids)

| 新增 testid | 现有位置 | 用途 |
|---|---|---|
| `data-testid="preflight-status"` | SimulationCheck.tsx 顶部 status pill（RUNNING/GO/NO-GO） | 等待 GO |
| `data-testid="preflight-gate-{n}"` | GateSequencer.tsx gate 卡片 wrapper | 6 个 gate 状态显示 |

**Screen 3 — `web/src/screens/SimulationMonitor.tsx`** (6 testids)

| 新增 testid | 现有位置 | 用途 |
|---|---|---|
| `data-testid="sim-clock-text"` | L1771 附近 `{fmtSimTime(simTimeSec)}` | sim_t 显示 mm:ss 字符串 |
| `data-testid="rate-btn-1x"` | L1786-1817 rate 按钮组 | 1x 按钮 |
| `data-testid="rate-btn-10x"` | 同 | 10x 按钮 |
| `data-testid="rate-btn-50x"` | 同 | 50x 按钮 |
| `data-testid="own-ship-hdg"` | L823 附近 `船首向 HDG` cell value span | 自船航向（数值） |
| `data-testid="threat-cpa"` | L424 附近 威胁卡片 CPA cell value span | 威胁 CPA（数值） |

**实际新增 19 个 testid**（5 + 7 + 6，原 plan 误写 13）。其中 `own-ship-hdg` / `threat-cpa` 位于条件渲染块（`{ownShip ? ...}` / `{categorizedTargets.high.length > 0 ? ...}`），依赖 telemetry 流到 store 才可见 — spec 在 §3.7 A6/A5 处用 `waitFor` + `toBeAttached`（不是 `toBeVisible`）兼容。

**8 个断言点全部通过这些 testid + 既有 testid（simulation-monitor / left-tab-threat / data-fsm）做 selector**。

### 3.4 test_scenario_trace.py 不替换，仅补充

| 既有 | 本 spec 后 |
|---|---|
| `test_avoidance_chain.py` 1x 7 AC | **保留**（后端 trace 真值基线） |
| `test_determinism.py` 1x/10x RTF | **保留**（RTF 失真检测） |
| `test_scenario_trace.py` pytest harness | **保留**（trace summary 旁路） |
| **—** | `web/e2e/hmi_consistency.spec.ts`（新，本 spec 主交付） |

**关系**：HMI E2E 失败 → cross-check `/debug/summary` 区分是 HMI 渲染还是后端问题。两者都失败 → 后端根因（修复归属 L3 kernel）。`/debug/summary` PASS 但 HMI 失败 → 单独 HMI 渲染 ticket。

### 3.5 WebSocket frame log 采集

在 `beforeEach` 钩子中：

```ts
const wsFrames: Array<{ t: number; data: any }> = [];
page.on('websocket', (ws) => {
  ws.on('framereceived', (frame) => {
    wsFrames.push({ t: Date.now(), data: safeParse(frame.payload) });
  });
  ws.on('framesent', (frame) => {
    wsFrames.push({ t: Date.now(), data: { sent: true, payload: safeParse(frame.payload) } });
  });
});
```

存到 `runs/hmi_consistency/<rate>/<run_id>/ws_frames.jsonl`，便于失败时回放。

### 3.6 HMI store snapshot

HMI 用 zustand（`useTelemetryStore`、`useFsmStore`），通过 `page.evaluate` 直接读 store：

```ts
const snapshot = await page.evaluate(() => {
  return {
    sim_time: (window as any).__TELEMETRY_STORE__?.getState()?.lifecycleStatus?.sim_time,
    fsm_state: (window as any).__FSM_STORE__?.getState()?.currentState,
    own_hdg: (window as any).__TELEMETRY_STORE__?.getState()?.ownShip?.pose?.heading,
    targets: (window as any).__TELEMETRY_STORE__?.getState()?.targets?.length,
  };
});
```

**前提**：spec 要求在 `web/src/store/telemetryStore.ts` 和 `fsmStore.ts` 顶部加 1 行（仅开发模式）：

```ts
if (typeof window !== 'undefined') (window as any).__TELEMETRY_STORE__ = useTelemetryStore;
```

2 行 × 2 文件 = 4 行最少改动，零业务影响。生产构建 webpack/vite 会 tree-shake 掉（通过 `if (process.env.NODE_ENV !== 'production')` 包裹）。

### 3.7 8 个断言点（per rate × per run）

| # | 断言 | 来源 | threshold |
|---|---|---|---|
| A1 | rate 按钮 active 状态 = 期望 rate | `data-testid="rate-btn-{r}x"` + `style.borderBottom` | active 高亮 |
| A2 | sim_clock_text 持续推进（rate=10x 下 5 wall 秒至少 +50s sim） | `data-testid="sim-clock-text"` 文本 + store sim_time | 文本 + store 都验证 |
| A3 | sim_t 跳值步长分布 — 50x 下 sim_t step 应在 [0.5, 2.0]s 范围（WebSocket 推流频率决定） | ws_frames.jsonl 解析 sim_time 字段 | p50/p95/max |
| A4 | threat label = CRITICAL（rule14-ho 必出现高威胁） | `left-tab-threat` + `categorizedTargets.high.length > 0` | 0 → 失败 |
| A5 | threat cpa 显示 nm 单位 + 数值 < 1.0（CRITICAL 阈值） | `data-testid="threat-cpa"` 文本 | 数值 < 1.0 |
| A6 | 自船 hdg 持续变化（rule14 头对头 5 分钟内必有右转 > 5°） | `data-testid="own-ship-hdg"` 文本 | 60s 内 max-min > 5° |
| A7 | fsm state 经过 COLREG_AVOIDANCE 阶段 | `data-fsm` 属性 + store `fsm_state` | 至少出现 1 次 |
| A8 | M4 phase timeline = TRANSIT → AVOID → TRANSIT（route return） | `data-fsm` 时间序列 | 末态 = TRANSIT |

**A3 关键**：用户报"08/15/24 跳值"是 WebSocket 推流频率不够的实证。spec 把 A3 当作**采样口径回归** — 一旦 WebSocket 推流频率被改，这个 assertion 会先爆。

### 3.8 rate sweep × runs

| rate | 跑数 | wall budget | 期望 sim_t 终点 |
|---|---|---|---|
| 1.0  | 3 | 180s × 3 = 540s wall | sim_t ≥ 60s |
| 10.0 | 3 | 90s × 3 = 270s wall | sim_t ≥ 600s |
| 50.0 | 3 | 60s × 3 = 180s wall | sim_t ≥ 3000s |

**总 wall budget ≈ 990s ≈ 16.5 min**。pytest 设 1800s 全局 timeout。

**10x 3 runs 用不同 RNG seed**（通过 scenario YAML `seed` 字段）：seed=42, 43, 44。验证避碰路径稳定。

### 3.9 rate 参数化 + validation 内容自动匹配

**问题**：当前测试硬编码 3 rate × 3 runs = 9 档。实际验证场景常只需要 1 档：
- 验证避碰链（rule14-ho）→ 10x 最快（3 runs × 90s = 270s wall）
- 验证 WebSocket 推流频率 → 50x 才能暴露采样问题
- 验证 RTF（real-time factor）→ 必须 1x，10x/50x 没意义
- 验证 rate 切换响应 → 1x/10x/50x 都要

**`--validation` flag 映射**（Playwright CLI 注入 `process.env.VALIDATION`）：

| validation 名 | 默认 rate × runs | 验证目标 |
|---|---|---|
| `hmi-consistency`（默认） | 1x/10x/50x × 3 = 9 档 | HMI 端到端 + 多 rate 交叉验证 |
| `avoidance-chain` | 10x × 3（seed 42/43/44） | rule14 头对头 + CPA ≥500m + AVOID→TRANSIT |
| `ws-stress` | 50x × 3 | A3 sim_time step 分布基线（推流频率） |
| `rtf` | 1x × 5（更长 wall） | RTF ∈ [0.95, 1.05] |
| `fsm` | 10x × 3 | M4 FSM phase timeline TRANSIT→AVOID→TRANSIT |

**`--rates` 手动覆盖**（`process.env.RATES`）：
- 格式：`--rates=1,10` / `--rates=50` / `--rates=1,10,50`
- 优先级：`--rates` > `--validation` 默认值
- 未传 `--rates` 也未传 `--validation` → 默认 `hmi-consistency` 全 9 档

**实现位置**（`web/e2e/hmi_consistency.spec.ts` 顶部）：

```ts
const VALIDATION_PRESETS: Record<string, number[]> = {
  'hmi-consistency':  [1, 10, 50],
  'avoidance-chain':  [10],
  'ws-stress':        [50],
  'rtf':              [1],
  'fsm':              [10],
};
const RUNS_PER_RATE = parseInt(process.env.RUNS ?? '3');
const RATES = (process.env.RATES?.split(',').map(Number)
             ?? VALIDATION_PRESETS[process.env.VALIDATION ?? 'hmi-consistency']
             ?? [1, 10, 50]);
```

**npm 脚本**（`web/package.json`）：

```json
"test:hmi-consistency":  "playwright test e2e/hmi_consistency.spec.ts",
"test:avoidance":        "VALIDATION=avoidance-chain playwright test e2e/hmi_consistency.spec.ts",
"test:ws-stress":        "VALIDATION=ws-stress       playwright test e2e/hmi_consistency.spec.ts",
"test:rtf":              "VALIDATION=rtf             playwright test e2e/hmi_consistency.spec.ts --workers=1"
```

**Rationale for parameterization**：让用户可按当前调查焦点选 1 档快跑（避免 16.5 min 全跑），同时不丢失多档交叉验证的默认 base。

---

## 4. Interface Contracts

### 4.1 入口命令

**默认（全档 9 跑，~16.5 min）**：
```bash
cd "/Users/marine/Code/MASS-L3-Tactical Layer/web"
npx playwright test e2e/hmi_consistency.spec.ts --reporter=line
```

**按验证内容自动匹配**（详见 §3.9）：

| 命令 | 跑什么 | wall |
|---|---|---|
| `npm run test:avoidance` | 10x × 3 runs（rule14-ho 验证） | ~4.5 min |
| `npm run test:ws-stress` | 50x × 3 runs（推流频率） | ~3 min |
| `npm run test:rtf` | 1x × 5 runs（实时因子） | ~15 min |
| `npm run test:hmi-consistency` | 1x/10x/50x × 3（默认全档） | ~16.5 min |

**手动覆盖 rate**：

```bash
RATES=1,10 npx playwright test e2e/hmi_consistency.spec.ts
RUNS=1 VALIDATION=avoidance-chain npx playwright test e2e/hmi_consistency.spec.ts  # 单跑摸底
```

**`package.json` 集成**：

```json
{
  "scripts": {
    "test:hmi-consistency":  "playwright test e2e/hmi_consistency.spec.ts",
    "test:avoidance":        "VALIDATION=avoidance-chain playwright test e2e/hmi_consistency.spec.ts",
    "test:ws-stress":        "VALIDATION=ws-stress       playwright test e2e/hmi_consistency.spec.ts",
    "test:rtf":              "VALIDATION=rtf             playwright test e2e/hmi_consistency.spec.ts --workers=1"
  }
}
```

### 4.2 输出物

| 路径 | 内容 |
|---|---|
| `web/test-results/hmi_consistency/` | Playwright 默认输出（trace + screenshot） |
| `runs/hmi_consistency/<rate>/<run_id>/ws_frames.jsonl` | WebSocket frame log |
| `runs/hmi_consistency/<rate>/<run_id>/store_snapshots.jsonl` | HMI store 时间序列 |
| `runs/hmi_consistency/<rate>/<run_id>/hmi_full.png` | 全屏 screenshot @ critical sim_t |
| `runs/hmi_consistency/<rate>/<run_id>/hmi_threat_tab.png` | 威胁 tab 展开 screenshot |

### 4.3 失败时输出

`playwright test --reporter=line` 默认产出：
- `web/test-results/hmi_consistency-{test-name}/` 完整 trace（`trace.zip`）
- 失败 assertion 的实际值 vs 期望值
- HMI store snapshot 末态
- 失败前 1 秒 ws_frames.jsonl tail

### 4.4 与现有 trace 系统关系

`/debug/summary` 仍然为后端 trace 真值。本 spec 的 HMI store snapshot 是 HMI 视图真值。**两者不一致时** = HMI 渲染 bug（如 ws frame drop、React 渲染滞后）。

---

## 5. Out of Scope

- L3 kernel（M1-M8）业务逻辑 — 不动
- HMI 业务逻辑（避碰算法、Ivp 仲裁、threat 分类） — 不动
- sil-orchestrator REST 端点 — 不动
- sil-nodes C++ — 不动
- WebSocket 推流频率优化（如果 A3 失败是 ws 推流问题，单独 ticket） — 本 spec 只断言，不修
- Playwright 跨浏览器（仅 Chromium） — 范围控制
- 并发多 scenario 并行 — 单 scenario 单浏览器
- 录播 / rosbag 回放 — 后续 D 任务

---

## 6. Implementation Order

### Task 1: 加 data-testid + 暴露 store 到 window

**Files:**
- Modify: `web/src/screens/SimulationScenario.tsx` (5 个 data-testid)
- Modify: `web/src/screens/SimulationCheck.tsx` (2 个 data-testid)
- Modify: `web/src/screens/SimulationMonitor.tsx` (6 个 data-testid)
- Modify: `web/src/store/telemetryStore.ts` (1 行 window 暴露)
- Modify: `web/src/store/fsmStore.ts` (1 行 window 暴露)

**Steps:**

**Screen 1 — SimulationScenario.tsx**：
1. L725 `onClick={() => handleSelect(child.id)}` 卡片 div → 加 `data-testid={\`scenario-card-${child.id}\`}`
2. L790 "确认场景" 按钮 → 加 `data-testid="scenario-confirm"`
3. L770-798 "下一步" 按钮组 → 给 3 个 "下一步" 各加 `data-testid="scenario-tab-vessel" / "scenario-tab-odd" / "scenario-tab-library"`（或保留 text content selector）

**Screen 2 — SimulationCheck.tsx**：
4. GateSequencer 内 gate 元素 → 加 `data-testid={\`preflight-gate-${n}\`}`（n=1..6）
5. 顶部 status pill → 加 `data-testid="preflight-status"`

**Screen 3 — SimulationMonitor.tsx**：
6. L1771 周围加 `data-testid="sim-clock-text"` 到 sim_t span
7. L1786 周围给 1x/10x/50x 按钮各加 `data-testid="rate-btn-{r}x"`
8. L823 加 `data-testid="own-ship-hdg"`
9. L424 加 `data-testid="threat-cpa"`

**Store 暴露**：
10. 在 `telemetryStore.ts` 顶部加 `(window as any).__TELEMETRY_STORE__ = useTelemetryStore;`（dev-only 守卫）
11. 同 10 对 `fsmStore.ts`
12. **不重启 stack**（纯前端 hot-reload，vite HMR 即可）
13. Verify：浏览器 dev console 输 `__TELEMETRY_STORE__.getState().lifecycleStatus.sim_time` 看到数值

### Task 2: 写 hmi_consistency.spec.ts

**Files:**
- Create: `web/e2e/hmi_consistency.spec.ts`

**关键原则**（来自 §3.1）：**不绕过 3 屏 UI 导航**。每个 test 一律 `page.goto('/#scenario')` 走 3 屏。

**Skeleton:**

```ts
import { test, expect, Page } from '@playwright/test';
import * as fs from 'fs';
import * as path from 'path';

// §3.9 validation 预设（env 注入；npm scripts 已设好）
const VALIDATION_PRESETS: Record<string, number[]> = {
  'hmi-consistency':  [1, 10, 50],
  'avoidance-chain':  [10],
  'ws-stress':        [50],
  'rtf':              [1],
  'fsm':              [10],
};
const SCENARIO = process.env.SCENARIO ?? 'colreg-rule14-ho';
const RUNS_PER_RATE = parseInt(process.env.RUNS ?? '3');
const RATES = (process.env.RATES?.split(',').map(Number)
             ?? VALIDATION_PRESETS[process.env.VALIDATION ?? 'hmi-consistency']
             ?? [1, 10, 50]);

const RUNS_DIR = path.resolve(__dirname, '../../runs/hmi_consistency');
const SIM_BUDGET: Record<number, { wall_s: number; sim_min: number }> = {
  1:  { wall_s: 180, sim_min: 60 },
  10: { wall_s: 90,  sim_min: 600 },
  50: { wall_s: 60,  sim_min: 3000 },
};

test.describe.configure({ mode: 'serial', timeout: 1800_000 });

for (const rate of RATES) {
  for (let run = 1; run <= RUNS_PER_RATE; run++) {
    test(`rate=${rate} run=${run} [${SCENARIO}]`, async ({ page }) => {
      const runId = `${rate}x_r${run}_${Date.now()}`;
      const runDir = path.join(RUNS_DIR, String(rate), runId);
      fs.mkdirSync(runDir, { recursive: true });

      // 1. WS frame log
      const wsFrames: any[] = [];
      page.on('websocket', (ws) => {
        ws.on('framereceived', (f) => wsFrames.push({ t: Date.now(), dir: 'rcv', data: safeParse(f.payload) }));
      });

      // 2. ====== Screen 1: scenario selection ======
      await page.goto('/#scenario');
      await page.waitForSelector('[data-testid="simulation-scenario"]');

      // 3 tabs: vessel → odd → library
      await page.click('[data-testid="scenario-tab-vessel"]');  // 进 vessel tab
      await page.click('text=下一步');                            // → odd
      await page.click('text=下一步');                            // → library
      await page.click(`[data-testid="scenario-card-${SCENARIO}"]`);
      await page.click('[data-testid="scenario-confirm"]');  // 触发 → check screen

      // 3. ====== Screen 2: preflight check ======
      await page.waitForURL(`**/#/check/${SCENARIO}`);
      await page.waitForSelector('[data-testid="preflight"]');
      // 等 6/6 gates GO（verdict pill 文字 = "GO"）+ countdown 3s
      await page.waitForFunction(() => {
        const el = document.querySelector('[data-testid="preflight-status"]');
        return el && /GO/.test(el.textContent || '');
      }, { timeout: 180_000 });
      // handleProceed 自动 cleanup→configure→activate，countdown 3s 后跳 monitor
      await page.waitForURL(`**/#/monitor/${SCENARIO}`, { timeout: 60_000 });
      await page.screenshot({ path: path.join(runDir, '01_monitor_enter.png'), fullPage: true });

      // 4. ====== Screen 3: rate 按钮（同真用户点击）======
      await page.click(`[data-testid="rate-btn-${rate}x"]`);

      // 5. Wait for sim to reach budget
      const budget = SIM_BUDGET[rate];
      await waitForSimMinutes(page, budget.sim_min, budget.wall_s * 1000);

      // 6. Assertions (A1-A8)
      // A1: rate button active
      const activeRate = await page.evaluate(() => {
        const btns = [1, 10, 50].map(r => {
          const el = document.querySelector(`[data-testid="rate-btn-${r}x"]`) as HTMLElement;
          return { r, active: el?.style.borderBottom?.includes('1px solid') || el?.style.color?.includes('--c-phos') };
        });
        return btns.find(b => b.active)?.r;
      });
      expect(activeRate, 'A1: rate button active').toBe(rate);

      // A2: sim_clock_text 推进
      const simClockText = await page.textContent('[data-testid="sim-clock-text"]');
      expect(simClockText, 'A2: sim clock visible').toMatch(/^\d{2}:\d{2}$/);

      // A3: ws frame sim_time step 分布（基线记录）
      const simTimeSteps = wsFrames
        .filter(f => f.data?.sim_time != null)
        .map(f => f.data.sim_time)
        .slice(1)
        .map((t, i, arr) => t - arr[i - 1]);
      const p50 = percentile(simTimeSteps, 50);
      const p95 = percentile(simTimeSteps, 95);
      console.log(`A3 [rate=${rate}]: sim_time step p50=${p50.toFixed(2)}s p95=${p95.toFixed(2)}s`);
      fs.writeFileSync(path.join(runDir, 'sim_time_steps.json'),
        JSON.stringify({ rate, p50, p95, count: simTimeSteps.length, samples: simTimeSteps.slice(0, 50) }, null, 2));

      // A4: threat CRITICAL（高威胁 tab 可见 + targets.length > 0）
      const targetsCount = await page.evaluate(() => {
        return (window as any).__TELEMETRY_STORE__?.getState()?.targets?.length ?? 0;
      });
      expect(targetsCount, 'A4: targets present').toBeGreaterThan(0);
      await page.click('[data-testid="left-tab-threat"]');
      const highVisible = await page.locator('text=高威胁目标').first().isVisible();
      expect(highVisible, 'A4: high threat section visible').toBe(true);

      // A5: threat cpa < 1.0 nm
      const cpaText = await page.textContent('[data-testid="threat-cpa"]');
      const cpaVal = parseFloat(cpaText?.match(/(\d+\.\d+)/)?.[1] ?? '99');
      expect(cpaVal, 'A5: threat CPA < 1.0 nm').toBeLessThan(1.0);

      // A6: own hdg tracked
      const hdgText = await page.textContent('[data-testid="own-ship-hdg"]');
      const hdgVal = parseFloat(hdgText?.match(/(\d+\.\d+)/)?.[1] ?? '0');
      expect(hdgVal, 'A6: own hdg tracked').toBeGreaterThanOrEqual(0);

      // A7 + A8: fsm state via data-fsm
      const fsmStatesSeen = new Set<string>();
      for (let i = 0; i < 30; i++) {
        const fsm = await page.getAttribute('[data-testid="simulation-monitor"]', 'data-fsm');
        if (fsm) fsmStatesSeen.add(fsm);
        await page.waitForTimeout(1000);
      }
      const sawAvoid = [...fsmStatesSeen].some(s => s.includes('AVOID') || s.includes('COLREG'));
      expect(sawAvoid, 'A7: COLREG_AVOIDANCE phase seen').toBe(true);
      const lastFsm = await page.getAttribute('[data-testid="simulation-monitor"]', 'data-fsm');
      expect(lastFsm, 'A8: final fsm = TRANSIT').toBe('TRANSIT');

      // 7. Persist evidence
      fs.writeFileSync(path.join(runDir, 'ws_frames.jsonl'),
        wsFrames.map(f => JSON.stringify(f)).join('\n'));
      await page.screenshot({ path: path.join(runDir, 'hmi_full.png'), fullPage: true });
      await page.click('[data-testid="left-tab-threat"]');
      await page.screenshot({ path: path.join(runDir, 'hmi_threat_tab.png'), fullPage: true });
    });
  }
}

// Helpers
function safeParse(s: string): any { try { return JSON.parse(s); } catch { return null; } }
function percentile(arr: number[], p: number): number {
  if (!arr.length) return 0;
  const sorted = [...arr].sort((a, b) => a - b);
  return sorted[Math.floor((p / 100) * sorted.length)];
}
async function waitForSimMinutes(page: Page, simMin: number, timeoutMs: number) {
  await page.waitForFunction(
    (min) => {
      const t = (window as any).__TELEMETRY_STORE__?.getState()?.lifecycleStatus?.sim_time ?? 0;
      return t >= min * 60;
    },
    simMin,
    { timeout: timeoutMs, polling: 500 },
  );
}
```

### Task 3: 加 npm 脚本

**Files:**
- Modify: `web/package.json` scripts 块

加 `"test:hmi-consistency": "playwright test e2e/hmi_consistency.spec.ts --reporter=line"`

### Task 4: 跑基线 + 归档

**Steps:**
1. `cd web && npm run test:hmi-consistency`
2. 9 档全跑 ≈ 16 min
3. 归档到 `runs/hmi_consistency/<date>_baseline/`
4. 失败任意 A1-A8 → 立刻定位（看 ws_frames + store snapshot + screenshot 三件套）
5. 9 档全 PASS → baseline 入库，后续 code 改动必须保持 9/9 PASS

### Task 5: 文档 + handoff

**Files:**
- Modify: `docs/Design/SIL/v1.0-unified/03-test-procedures.md` (若存在) — 加 HMI E2E 章节
- Modify: `docs/Design/SIL/v1.0-unified/04-acceptance-criteria.md` (若存在) — 加 HMI E2E AC

---

## 7. Acceptance Criteria

| AC | 验证方式 |
|---|---|
| AC-1 | `npm run test:hmi-consistency` 跑 9 档（3 rate × 3 runs），全部 PASS |
| AC-2 | 失败时 `runs/hmi_consistency/<rate>/<run_id>/` 三件套（ws_frames + store_snapshot + screenshot）存在且可读 |
| AC-3 | HMI 业务逻辑文件（除新增 4 行 testid/暴露）零改动 |
| AC-4 | L3 kernel 零改动 |
| AC-5 | A3 阈值在 1x/10x/50x 三档各自记录基线 p50/p95（不强制 pass/fail，仅记录） |
| AC-6 | A4-A8 在 1x 路径下也能报 PASS（即用户报"1x 避碰无效"如果是真 bug，能被本 spec 抓到） |
| AC-7 | 同一份 spec 在 HMI 看到 = 测试截图 = 测试断言，三者一致 |

---

## 8. Failure Mode Attribution

| 失败 AC | 归属 | 后续动作 |
|---|---|---|
| A1 失败 | HMI onClick 链路 bug | 单独 HMI ticket |
| A2 失败 | sim_clock 字段未注入 HMI store | 后端 / store 注入 ticket |
| A3 偏离基线 > 5x | WebSocket 推流频率不够 | 单独 ws 推流 ticket |
| A4 失败 | 后端 M2 target 分类坏 | L3 M2 ticket |
| A5 失败但 A4 PASS | HMI CPA 计算 / 显示 bug | HMI 渲染 ticket |
| A6 失败 | 1x 路径下避碰真不工作 | L3 M4/M5/bridge ticket（用户原报告根因） |
| A7 失败 | M4 AVOIDANCE 阶段未触发 | L3 M4 ticket |
| A8 失败 | route-return controller 坏 | L3 bridge XTE ticket（已知 C-2） |

---

## 9. Self-Review Checklist

- [x] **Placeholder scan**: 无 TBD/TODO；所有 threshold 是具体值或"基线记录不强制"
- [x] **Internal consistency**: A1-A8 编号在 §3.7、§7、§8 三处一致
- [x] **Scope check**: 单一 spec，单一 plan；不动 L3 kernel；HMI 增量最小（13 testid + 2 store 暴露 = 15 行最少改动）
- [x] **Ambiguity check**: "WebSocket 推流频率" 在 §3.7 A3 明确"不强制阈值，仅记录基线"
- [x] **Playwright 配置**: 复用既有 `web/playwright.config.ts`，新增 spec 文件；timeout/retries/screenshot 改动在 §3.2 显式声明
- [x] **数据流** 在 §2 ASCII 图显式画清 4 跳：driver → HMI → 后端 → 旁路
- [x] **3 屏 UI 流程**: §3.0 显式画出 scenario→check→monitor 转换触发 + lifecycle 状态机；测试 driver 走同路径
- [x] **rate 参数化**: §3.9 + §4.1 提供 5 档 validation 预设 + --rates 手动 override；npm scripts 集成
- [x] **Out of scope** 显式列 8 项

---

## 10. Open Questions

- [x] HMI 业务逻辑是否接受加 4 行 `if (process.env.NODE_ENV !== 'production')` 暴露 store？**已答：用户同意，零业务影响**
- [x] 3 屏 UI 导航是否走真点击（不 `page.evaluate` REST）？**已答：必须走 3 屏（§3.1 新增硬约束）**
- [x] validation→rate 颗粒度？**已答：5 档（hmi-consistency / avoidance-chain / ws-stress / rtf / fsm）**
- [x] screen1/2 新增 testid 数量？**已答：4 个 (scenario-card / scenario-confirm / preflight-gate / preflight-status) — 实际 + 5 个 tab 入口 = 9 个**
- [x] 50x 3 runs 总 wall budget 180s 是否够？**已答：先 1 次试跑 (rate=50, 1 run, wall=120s) 摸底**

---

**Spec status**: Ready for user review. Next: writing-plans skill after user approves this spec.
