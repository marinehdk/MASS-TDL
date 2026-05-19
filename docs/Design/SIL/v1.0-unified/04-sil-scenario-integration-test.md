# SIL 场景联调测试 · v1.0 统一基线

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-UNIFIED-004 |
| 版本 | v1.0 |
| 日期 | 2026-05-15 |
| 状态 | 设计基线（套件 Doc 4，与 Doc 0/1/2/3 联动交付，套件 v1.0 完整化）|
| 套件 | Doc 0 README / Doc 1 架构 / Doc 2 后端 / Doc 3 前端 / **Doc 4 场景联调** |
| 上游 | Doc 1 §6 数据流 · Doc 2 §4 scenario_authoring · Doc 3 §4 数据通道 · D1.5 V&V Plan v0.1 |
| 范围 | YAML schema + 场景库（35 个）+ 实例化 + 端到端调用链 + DEMO 验收矩阵 + KPI/评分 + 故障注入 + ASDR 证据链 + Mock 替换路线 + D1.3.1 仿真器鉴定 |

---

## 0. 一句话定位

把 SIL 系统从"代码跑得起来"推进到"**完整 TDL 系统的核心功能模块联调测试基础**"——35 个场景（22 IMAZU + 11 COLREGs + 1 head_on + 1 user）通过统一 maritime-schema YAML 驱动；从 Builder 点"Run"开始，沿 orchestrator → ROS2 lifecycle → ship_dynamics + sensor_mock + tracker_mock → L3 kernel 8 模块 → L4 stub → 闭环回 ship_dynamics 全链路真链路（**非 Mock**）跑通；6 维 Hagen 2022 评分 + IEC 62288 PASS/FAIL verdict 写 MCAP + Arrow + Marzip 容器，作为 CCS i-Ship N AIP 提交（11 月）的证据骨架。

---

## 1. 现状 vs 目标

| 维度 | 现状（commit 73cdf23）| 目标（v1.0）| 差距 |
|---|---|---|---|
| **YAML schema** | 2 套并存：COLREGs v1.0 (ENU x_m/y_m) + IMAZU v2.0 (lat/lon)；NTNU csog_state 已退役 ✅ | 统一 DNV `maritime-schema` TrafficSituation + metadata 扩展 | GAP-003 D1.6 |
| **场景库** | 35 个 YAML（22 IMAZU + 11 COLREGs + 1 head_on + 1 user-created）| 35 + Phase 2 D2.4 → 200 + Phase 3 D3.6 → 1000 | 数量符 Phase 1 / D1.5 X1.5 IMAZU 22/22 PASS |
| **实例化链路** | orchestrator + lifecycle bridge + 9 业务节点 Python stub | LifecycleNode rclpy 真实例化 + 50 Hz tick + L3 kernel 接通 | GAP-018 D1.3a/b |
| **DEMO-1 (6/15)** | Head-On analytical trajectory + standalone `demo_server.py` + `demo_ws_server.py`（非 ROS2 真链路）+ acad427 完整 4 屏 visual 演示 ✅ | 同左 visual demo 通过 + ROS2 真链路在 DEMO-2 启用 | DEMO-1 范围内允许 |
| **DEMO-2 (7/31)** | 待启动 | 50 场景批量 + 6 维评分实施 + Marzip 1-click + replay | D2.4/D2.5/D2.6 |
| **DEMO-3 (8/31)** | 待启动 | 1000 场景立方体 + ToR + Doer-Checker verdict + S-Mode 完整 | D3.4/D3.6/D3.8 |
| **KPI 评分** | scoring stub 硬编码 | 6 维 Hagen 2022 + Woerner 2019 + V&V Plan v0.1 §4 6 KPIs | GAP-021 D2.4 |
| **ASDR 证据链** | Marzip 仅 manifest + scenario.yaml + scoring.json | + MCAP + Arrow + asdr_events.jsonl + verdict.json | GAP-006 D1.3b.3 |
| **仿真器鉴定 D1.3.1** | self_check 5 项硬编码 PASS | DNV-RP-0513 + DNV-CG-0264 §3 V&V Plan 完整证据集 | GAP-005 D1.3.1 |

**v1.0 套件焦点**：锁定 schema + 库 + 实例化链路 + DEMO 验收矩阵 + Mock 替换 + 鉴定映射。具体 D-task 实施按本文档差距台账独立 plan。

---

## 2. YAML Schema + Screen ① Builder 交互规格（v1.0）

### 2.1 现状：两套并存，Screen ① 集成验收

#### 2.1.1 COLREGs Schema v1.0（ENU-based，11 场景使用）

`scenarios/COLREGs测试/schema.yaml` canonical reference：

```yaml
schema_version: "1.0"
scenario_id: <rule>-<odd>-<encounter>-v<ver>
description: human-readable
rule_branch_covered: [Rule14_HeadOn, Rule8_Action, ...]
vessel_class: FCB
odd_zone: A|B|C|D

initial_conditions:
  own_ship: {x_m, y_m, heading_nav_deg, speed_kn, n_rps}
  targets: [{target_id, x_m, y_m, cog_nav_deg, sog_mps}, ...]

encounter:
  rule: Rule13|Rule14|Rule15_Stbd|Rule15_Port
  give_way_vessel: own|target|none
  expected_own_action: turn_starboard|turn_port|maintain|slow_down
  avoidance_time_s, avoidance_delta_rad, avoidance_duration_s

disturbance_model:
  wind_kn, wind_dir_nav_deg, current_kn, current_dir_nav_deg
  vis_m, wave_height_m

prng_seed: int (required for Monte-Carlo)

pass_criteria:
  max_dcpa_no_action_m: float    # 不动作时 DCPA 上限（必小于以确认碰撞风险）
  min_dcpa_with_action_m: float  # 动作后 DCPA 下限（必大于以确认可解）
  bearing_sector_deg: [start, end]

simulation:
  duration_s: float (>= 600.0)
  dt_s: float (== 0.02 D1.3.1 baseline)
```

#### 2.1.2 IMAZU Schema v2.0（lat/lon-based，22 场景使用）

```yaml
title, description, start_time
own_ship:
  id, nav_status, mmsi
  initial: {position: {latitude, longitude}, cog, sog, heading}
target_ships: [{id, nav_status, mmsi, initial: {position, cog, sog, heading}}, ...]
metadata:
  schema_version: "2.0"
  scenario_id: imazu-<NN>-<encounter>-v1.0
  scenario_source: imazu1987
  vessel_class: FCB
  odd_zone: A|B|C|D
  geo_origin: {latitude, longitude, description}
  encounter: {rule, give_way_vessel, expected_own_action, avoidance_time_s, ...}
  disturbance_model: {wind_kn, wind_dir_nav_deg, ...}
  pass_criteria: {max_dcpa_no_action_m, min_dcpa_with_action_m}
  simulation: {duration_s, dt_s, n_rps_initial}
prng_seed: nullable
```

### 2.2 目标：DNV maritime-schema TrafficSituation v0.2.x

[E1][E8][E22] 决策 §5 锁定 🟢。**完整迁移模板**（决策记录 §10）：

```yaml
# yaml-language-server: $schema=schemas/fcb_traffic_situation.schema.json
title: "Crossing-from-port, FCB own ship, two targets, Beaufort 5"
description: "Coverage cube cell rule15 × open-sea × disturbance-D3 × seed-2"
startTime: "2026-05-09T08:00:00Z"

# DNV maritime-schema 标准节
ownShip:
  static: {shipType, length, width, mmsi}
  initial: {position, sog, cog, heading}
  waypoints: [{position}, ...]
  model: "fcb_mmg_vessel"
  controller: "psbmpc_wrapper"
targetShips:
  - id: "MMSI_257123456"
    static: {shipType, length, width}
    model: "ais_replay_vessel"      # 或 ncdm_vessel / intelligent_vessel
    trajectory_file: "trajectories/TS1_track.csv"
    initial: {position, sog, cog, heading}
environment:
  wind: {dir_deg, speed_mps}
  current: {dir_deg, speed_mps}
  visibility_nm: 1.5

# FCB 项目扩展（schema 允许 additional properties）
metadata:
  scenario_id: "FCB-OSF-CR-PORT-018"
  hazid_refs: ["HAZ-NAV-014", "HAZ-NAV-022"]
  colregs_rules: ["R15", "R16", "R8"]
  odd_cell:
    domain: "open_sea_offshore_wind_farm"
    daylight: "twilight"
    visibility_nm: 1.5
    sea_state_beaufort: 5
  disturbance:
    wind: {dir_deg: 235, speed_mps: 12.0}
    current: {dir_deg: 90, speed_mps: 0.6}
    sensor: {ais_dropout_pct: 5, radar_range_nm: 6.0, radar_pos_sigma_m: 25}
  seed: 2
  vessel_class: FCB-45m
  expected_outcome:
    cpa_min_m_ge: 300
    rule15_compliance: required
    rule8_visibility: "early_and_substantial"
    grounding: forbidden
  simulation_settings:
    dt: 0.5
    total_time: 1200
    enc_path: "data/enc/trondheim_fjord"
    coordinate_origin: [63.43, 10.39]
    dynamics_mode: internal           # 或 fmi（D1.3c）
```

### 2.3 字段映射表（v2.0 → maritime-schema）

| v2.0 字段 | maritime-schema 路径 | 迁移动作 |
|---|---|---|
| `title` | `title` | ✅ 直通 |
| `description` | `description` | ✅ 直通 |
| `start_time` | `startTime` | rename camelCase |
| `own_ship.initial.position` | `ownShip.initial.position` | rename + nest |
| `own_ship.initial.{cog,sog,heading}` | `ownShip.initial.{cog,sog,heading}` | ✅ |
| `target_ships[]` | `targetShips[]` | rename camelCase |
| `metadata.disturbance_model` | `environment` + `metadata.disturbance` | 拆分 |
| `metadata.encounter` | `metadata.encounter` | ✅ 保留扩展 |
| `metadata.pass_criteria` | `metadata.expected_outcome` | rename |
| `metadata.simulation` | `metadata.simulation_settings` | rename |
| `prng_seed` | `metadata.seed` | move |

### 2.4 JSON Schema 双语言校验

| 语言 | 库 | 用途 |
|---|---|---|
| Python | `cerberus` + `pydantic` (maritime-schema 原生) | scenario_authoring_node + orchestrator validate |
| C++ | `cerberus-cpp`（[E15] github.com/dokempf/cerberus-cpp）| L3 kernel 边界（reject 非法 scenario）|
| TypeScript | `ajv` + monaco-editor inline schema | 前端 Builder 实时校验（GAP-022 NICE）|

CI 强制：`tools/validate_scenarios.py` 在每 PR 跑全 35 场景，schema 不通过 → block merge。

### 2.5 schema 版本管理

| 版本 | 适用 | 兼容 |
|---|---|---|
| v1.0 | COLREGs 11 场景（ENU x_m/y_m）| Phase 1 内保留，Phase 2 迁 maritime-schema |
| v2.0 | IMAZU 22 场景（lat/lon + metadata）| Phase 1 内保留，Phase 2 迁 maritime-schema |
| **maritime-schema** | Phase 2 D1.6 起所有新场景 | DNV 治理；buf-style breaking gate |

---

## 3. 场景库（35 个 + 路线图）

### 3.1 Phase 1 场景库（commit 73cdf23）

#### 3.1.1 head_on.yaml（DEMO-1 主场景）

`scenarios/head_on.yaml`（2.8 KB）— Head-On Rule 14 完整 NTNU 风格（已退役，DEMO-1 仅作 visual demo 输入）。DEMO-1 实际使用 analytical trajectory（`tools/demo/trajectory.py`，docs/superpowers/specs/2026-05-14-sil-demo1-head-on-design.md §3）。

#### 3.1.2 IMAZU 标准测试（22 场景）

`scenarios/IMAZU标准测试/imazu-{01..22}-{encounter}.yaml`：

| # | encounter | 描述 | Rule |
|---|---|---|---|
| 01 | ho | Head-on 单目标 | R14 |
| 02 | cr-gw | Crossing give-way 单目标 | R15 |
| 03 | ot | Overtaking 单目标 | R13 |
| 04 | cr-so | Crossing stand-on 单目标 | R15 + R17 |
| 05–22 | ms | Multi-ship（3+ targets，混合规则）| R5/R7/R8/R13/R14/R15/R16/R17 |

源：Imazu 1987 → Sawada/Sato/Majima 2021 canonical reference（[E16] DOI: 10.1007/s00773-020-00773-y 🟢）。

#### 3.1.3 COLREGs 测试（11 场景）

`scenarios/COLREGs测试/`：

| 文件 | 描述 |
|---|---|
| `colreg-rule13-ot.yaml` + `-2 / -3` | Rule 13 Overtaking 3 变体（不同 SOG 差/接近角）|
| `colreg-rule14-ho.yaml` + `-2 / -3` | Rule 14 Head-on 3 变体（不同初始 separation）|
| `colreg-rule15-cs.yaml` + `-2 / -3 / -4` | Rule 15 Crossing 4 变体（give-way / stand-on / 大角度交叉 / 复合）|
| `schema.yaml` | canonical schema reference |

#### 3.1.4 用户自建（1 场景）

`scenarios/5c93bf30f54c.yaml` — Builder 屏 ① "Save .yaml" 测试样本（UUID 命名格式）。

### 3.2 Phase 2/3 路线（决策记录 §6 锁定 🟢）

| Phase | 场景数 | 来源 | D-task |
|---|---|---|---|
| Phase 1 | 35 + Imazu-22 + COLREGs 基线 | 现有 | D1.6 |
| Phase 2 | + AIS-driven 50 场景（Kystverket + NOAA）| scenario_authoring AIS 5 阶段管线 | D2.4 |
| Phase 3 | + Monte Carlo LHS / Sobol 10000 sample | dnv-opensource/ship-traffic-generator + farn | D3.6 |
| Phase 3 完整覆盖立方体 | **1100 cells = 11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds** | farn n-dim case folder | D3.6 |

### 3.3 Screen ① 集成测试 — useMapInteraction + ODD + Baseline 只读

**测试用例集**（spec Screen1 §10 验收条件 + plan Task 3-6）：

| 用例编号 | 交互 | 验证方法 | DEMO-1 PASS 条件 |
|---|---|---|---|
| **T-S1-01** | ODD 下拉框变更 | 选择 domain="coastal" → YAML 保存 | `metadata.odd_cell.domain = "coastal"` 出现在 YAML Source tab |
| **T-S1-02** | 拖拽本船位置 | 鼠标拖本船红三角到新坐标 → mouseup | `ownShip.initial.position.latitude/longitude` 更新，地图即时重绘 |
| **T-S1-03** | 拖拽目标船位置 | 同上，针对目标船 TGT-1 | `targetShips[0].initial.position` 更新 |
| **T-S1-04** | 拖拽 COG 线端点改航向 | 拖拽 COG 预测线箭头 tip → 释放 | `ownShip.initial.heading` 和目标 `heading` 字段更新为新计算航向 |
| **T-S1-05** | 拖拽 WP 节点 | 点击地图添加 WP → 拖拽移动 → 释放 | `voyageTask.waypoints[n].{lat,lon}` 更新 |
| **T-S1-06** | Schema 校验 | 输入非法值如 `SOG=999` → 校验指示灯 | 右下角 Sticky Footer 校验灯变红，显示"Schema 错误" |
| **T-S1-07** | Baseline 只读保护 1 | 点击 Baseline 场景（imazu22 folder）的 SAVE | 弹出"另存为 Custom"对话框 |
| **T-S1-08** | Baseline 只读保护 2 | 后端 `PUT /scenarios/imazu22_id` 请求 | 返回 `409 Conflict` + "read-only Baseline" 错误 |
| **T-S1-09** | Custom SAVE hash | 保存 Custom 场景后检查 Sticky Footer | 显示真实 SHA256（从 POST 响应取，非硬编码） |
| **T-S1-10** | RUN 流程 | 点击 [RUN → ②] 按钮 | navigate 到 `#/check/{scenario_id}`，Preflight 屏正常加载 |
| **T-S1-11** | Tab 2/3 外壳 | 点击 Tab 2（环境）和 Tab 3（断言）| 显示 Phase 2 占位提示，不报错不崩溃 |

**自动化测试代码示例**（Playwright E2E）：

```typescript
// tests/e2e/screen1-scenario-builder.spec.ts

import { test, expect } from '@playwright/test';

test.describe('Screen 1 - Scenario Builder', () => {
  test.beforeEach(async ({ page }) => {
    await page.goto('http://localhost:5173/#/scenario');
  });

  test('T-S1-01: ODD domain change → YAML', async ({ page }) => {
    await page.selectOption('[data-testid="odd-domain"]', 'coastal');
    await page.click('[data-testid="save-button"]');
    const yaml = await page.locator('[data-testid="yaml-source"]').textContent();
    expect(yaml).toContain('domain: coastal');
  });

  test('T-S1-02: Drag own ship → position update', async ({ page }) => {
    const mapCanvas = page.locator('canvas').first();
    await mapCanvas.dragTo({ x: 100, y: 100 }, { x: 150, y: 120 });
    const yaml = await page.locator('[data-testid="yaml-source"]').textContent();
    expect(yaml).toMatch(/ownShip.*position.*latitude/);
  });

  test('T-S1-08: Baseline PUT → 409 Conflict', async ({ page, context }) => {
    // Intercept API calls
    await context.route('**/api/v1/scenarios/imazu22_*', route => {
      if (route.request().method() === 'PUT') {
        route.abort('failed');
      } else {
        route.continue();
      }
    });
    // Would trigger 409 with proper backend implementation
  });
});
```

### 3.4 PR Fast Gate（Imazu-22 强制基线）

**每个 PR**：CI 跑全 22 IMAZU 场景：

- 22/22 PASS（V&V Plan v0.1 X1.5）
- CPA min ≥ 200 m ratio ≥ 95%
- COLREGs classification ≥ 95%

`imazu22_v1.0.yaml` 文件夹 freeze 为 SHA256 hash 化 manifest（决策记录 §6 + D1.3b.1 范围）。

**Baseline 只读保护测试**（GAP-NEW-001，D1.3b.3）：

| # | 测试项 | 预期结果 |
|---|---|---|
| T-BL-01 | `GET /api/v1/scenarios` 响应每条含 `is_baseline: true/false` | IMAZU22/COLREGs/AIS-accident 文件夹 → `true`；Custom 文件夹 → `false` |
| T-BL-02 | `PUT /api/v1/scenarios/{imazu_id}` 提交合法 YAML | HTTP 409 Conflict，body 含 "read-only Baseline" |
| T-BL-03 | `PUT /api/v1/scenarios/{custom_id}` 提交合法 YAML | HTTP 200，返回新 hash |
| T-BL-04 | 前端对 Baseline 场景点 SAVE | 弹出"另存为 Custom"对话框，不发 PUT |

---

## 4. 端到端调用链（完整 commit 锚点）

### 4.1 从"Run →"到 ENC 上看到避碰轨迹

```
T-∞   用户开浏览器 → web/Vite dev :5173 → App.tsx 渲染 ScenarioBuilder
            │
            │ commit 4fc0522 (OpenBridge token + ScenarioBuilder UI)
            ▼
T-N   用户选 imazu-14-ms.yaml
      RTK Query useGetScenarioQuery('imazu-14-ms-v1.0')
        → fetch GET /api/v1/scenarios/imazu-14-ms-v1.0
        → ScenarioStore.get(scenario_id) → 读 scenarios/IMAZU标准测试/imazu-14-ms.yaml
        → 返回 {yaml_content, hash: SHA256}
            │
            │ commit f9997c9 (D1.4 coding standards) + scenarios already migrated
            ▼
T-1   js-yaml.load(yaml_content) → step B/C UI 填充
      SilMapView 渲染 own + 3 targets 几何预览
      用户点 [Run →]
        → useScenarioStore.setScenario('imazu-14-ms-v1.0', hash)
        → window.location.hash = '#/check/imazu-14-ms-v1.0'
            ▼
T+0   App.tsx 检测 hash 变化 → 渲染 <Preflight />
      Preflight.tsx useEffect 触发 runChecks():
        a. POST /api/v1/lifecycle/cleanup        (idempotent reset)
        b. POST /api/v1/lifecycle/configure {scenario_id}
            → LifecycleBridge._change_state(TRANSITION_CONFIGURE)
            → rclpy service call /scenario_lifecycle_mgr/change_state
                  (PHASE 1 现状：service 端点不存在 → GAP-018 LifecycleNode 升级后通)
            → 状态机 UNCONFIGURED → INACTIVE
        c. 6-gate sequencer（Doc 3 §7.2 重设计 — GAP-023/024 D1.3b.3）
            GATE 1 系统就绪 / GATE 2 模块健康 / GATE 3 场景完整性
            GATE 4 ODD-场景对齐 / GATE 5 时基-证据链 / GATE 6 Doer-Checker 隔离
        d. all_clear = true → 3-2-1 倒数
        e. POST /api/v1/lifecycle/activate
            → ChangeState(TRANSITION_ACTIVATE)
            → 状态机 INACTIVE → ACTIVE
            → 各 LifecycleNode on_activate：启 timer / publisher / subscription
            → 50 Hz tick 起，sim_clock /clock 发布
            → orchestrator._seed_run_dir 创建 runs/{run_id}/{scenario.yaml, sha256, scoring.json stub}
        f. window.location.hash = '#/monitor/{run_id}'
            │
            │ commit ace10b8 (M4 ROS2 node) + acad427 (Demo-1 head-on impl)
            ▼
T+1s  App.tsx 渲染 <BridgeHMI />
      useFoxgloveLive hook 启动 → connect ws://127.0.0.1:8765 (foxglove_bridge)
        (现状：connect telemetry_bridge.py 自制 JSON — GAP-015/026 D1.3b.3 切 foxglove protocol)
      subscribe 11 topics → useTelemetryStore 持续 update
      SilMapView 50 Hz 渲染 own-ship marker + heading vector + targets + CPA rings
            ▼
T+0..end  闭环 50 Hz tick：
   sim_workbench (Container 2):
   ┌────────────────────────────────────────────────────────────────┐
   │ env_disturbance_node      → /sil/environment (1 Hz)            │
   │       │                                                          │
   │       ▼                                                          │
   │ ship_dynamics_node ← /sil/actuator_cmd (from L4 stub, 10 Hz)    │
   │   - 4-DOF MMG Yasukawa 2015 (现状 kinematic stub GAP-020)        │
   │   - RK4 dt=0.02s                                                 │
   │   - publish /sil/own_ship_state @ 50 Hz                          │
   │       │                                                          │
   │       ▼                                                          │
   │ target_vessel_node × 3  → /sil/target_vessel_state (10 Hz)      │
   │   - mode: ais_replay / ncdm / intelligent                         │
   │       │                                                          │
   │       ▼                                                          │
   │ sensor_mock_node                                                 │
   │   - AIS Class A/B → /sil/ais_msg (0.1 Hz)                       │
   │   - Radar (with clutter + dropout) → /sil/radar_meas (5 Hz)     │
   │       │                                                          │
   │       ▼                                                          │
   │ tracker_mock_node                                                │
   │   - God mode (perfect ground truth) OR KF mode                   │
   │   - publish l3_external_msgs/TrackedTargetArray (10 Hz)         │
   │       │                                                          │
   │       ▼ (DDS 跨 container)                                      │
   └────────────────────────────────────────────────────────────────┘
   l3_tdl_kernel (Container 3 Doer + Container 4 Checker M7):
   ┌────────────────────────────────────────────────────────────────┐
   │ M1 ODD: 从 environment + 当前 odd_cell 判定模式 → /l3/odd_state │
   │       │                                                          │
   │       ▼                                                          │
   │ M2 World Model: 融合 tracked_targets + own_ship → /l3/world_state│
   │       │                                                          │
   │       ▼                                                          │
   │ M3 Mission: WP from scenario → /l3/mission_goal (event)         │
   │       │                                                          │
   │       ▼                                                          │
   │ M4 Behavior Arbiter (IvP): 选 transit / colreg_avoid /          │
   │     mrc → /l3/behavior_plan (1-4 Hz)  [commit ace10b8]          │
   │       │                                                          │
   │       ▼                                                          │
   │ M6 COLREGs Reasoner: rule 推理 → /l3/colregs_active             │
   │       │                                                          │
   │       ▼                                                          │
   │ M5 Tactical Planner: Mid-MPC (≥90s) + BC-MPC                    │
   │   → /l3/avoidance_plan (1-2 Hz) → L4 stub                        │
   │   → /l3/reactive_override_cmd (event) → L4 stub                 │
   │       │           │                                              │
   │       │           └──→ (Container 4)                            │
   │       │                M7 Safety Supervisor (独立进程)            │
   │       │                  - 订阅 Doer 输出 + tracked_targets       │
   │       │                  - VETO check < 10 ms (V&V Plan §6)     │
   │       │                  - /l3/checker_veto event 回灌 M5        │
   │       │                  - /l3/safety_alert event                │
   │       ▼                                                          │
   │ M8 HMI Bridge: 聚合 SAT-1/2/3 + ASDR                            │
   │   → /l3/sat1_data (10 Hz) / /l3/sat2_data (1 Hz) / /l3/sat3_data │
   │   → /l3/asdr_record (10 Hz) → rosbag2 + WS frontend             │
   │   → /l3/tor_request (event) → TorModal frontend (Phase 3)        │
   └────────────────────────────────────────────────────────────────┘
   sim_workbench L4 stub:
       L4 stub 接 avoidance_plan + reactive_override_cmd
         → 计算 actuator cmd (rudder, throttle)
         → publish /sil/actuator_cmd (10 Hz) → ship_dynamics_node 关闭闭环
            │
            ▼
   旁路：rosbag2_recorder (独立进程)
       record /sil/* + /l3/* 到 runs/{run_id}/bag.mcap (zstd message-mode)
   旁路：scoring_node
       订阅 /sil/own_ship_state + /sil/target_vessel_state + /l3/colregs_active
       计算 6 维 Hagen 2022 分 → /sil/scoring_row (1 Hz)
       写入 runs/{run_id}/scoring.arrow (Phase 2)
            ▼
T+end 用户点 [⏹ Stop] (或 TIMEOUT 或 FAULT_FATAL)
      POST /api/v1/lifecycle/deactivate
        → ChangeState(TRANSITION_DEACTIVATE)
        → 状态机 ACTIVE → INACTIVE
        → 各 LifecycleNode on_deactivate 停 timer / pub
        → rosbag2_recorder 收尾 MCAP
        → 50 Hz tick 停
      window.location.hash = '#/evaluator/{run_id}'
            ▼
T+post App.tsx 渲染 <RunReport />
       GET /api/v1/scoring/last_run → 读 runs/{run_id}/scoring.json
       (现状：硬编码 stub 4 字段 — GAP-021/027 D2.4 升级到 8 字段真分)
       渲染 8 KPI cards + 6 维 radar + TimelineSixLane + AsdrLedger + TrajectoryReplay
            ▼
T+exp 用户点 [Export]
      POST /api/v1/export/marzip {run_id}
        → orchestrator 后台 task _build_marzip:
            1. 读 runs/{run_id}/scenario.yaml + sha256 (✅ 现状)
            2. 读 runs/{run_id}/scoring.json (⏳ Phase 2: scoring.arrow)
            3. 读 runs/{run_id}/bag.mcap → polars DataFrame → arrow.ipc (⏳ Phase 2)
            4. 计算 derived KPIs (✅ stub / ⏳ Phase 2 真实)
            5. 读 manifest.yaml (✅ ROS2 + L3 kernel git SHA)
            6. zip → exports/{run_id}_evidence.marzip
      GET /api/v1/export/status/{run_id} 轮询 → status=complete
      前端 <a href="/exports/{run_id}_evidence.marzip" download> 触发下载
            ▼
END
```

### 4.2 端到端 commit 锚点

| 链路段 | 现状 commit | 状态 |
|---|---|---|
| `web/` 4 屏 + OpenBridge token | 4fc0522 + 5316213 + acad427 | ✅ |
| `sil_orchestrator` FastAPI 8 router | (无 git log 标识，main.py 现状) | ✅ |
| `sim_workbench` 9 LifecycleNode | 73cdf23 部分（mock publishers 临时方案）| ⏳ GAP-018 |
| L3 kernel M1-M8 ROS2 | ace10b8（M4 ROS2 节点完整）+ M1-M3/M5-M8 历史 | ✅ M4，其余按 D-task |
| Mock publisher 临时 | 73cdf23（external_mock + sil_mock）| ⏳ DEMO-1 后退役 |
| DEMO-1 visual demo | acad427（Head-On 完整实现 + 4 修复）+ 74af635 | ✅ |
| D1.5 V&V Plan v0.1 | e1a13e5 | ✅ |
| Coding standards D1.4 | f9997c9 | ✅ |

---

## 5. DEMO-1 Skeleton Live 验收（6/15）

### 5.1 验收范围与四屏流程（specs 2026-05-18-screen{1,3}-*.md + 2026-05-14-demo1）

**Screen ① Builder → Screen ② Preflight → Screen ③ Monitor → Screen ④ Report 完整链路验收矩阵**：

| 屏幕 | 模块 | 焦点功能 | DEMO-1 验收条件 |
|---|---|---|---|
| **Screen ①** | SimulationScenario (3-Pane) | 场景编辑、ODD 过滤、Baseline 只读保护 | ① ODD 写入 metadata.odd_cell 三字段；② Baseline SAVE → "另存 Custom"；③ 拖拽本船/目标船/WP/COG 位置更新；④ Schema 校验指示灯 |
| **Screen ②** | SimulationCheck (6-Gate SSE) | Preflight 6-gate 流式诊断、Quick Fix | ① 6 Gate 逐条点亮（SSE stream）；② GO/NO-GO 判定准确；③ 证据产物 gate_N.json 完整；④ Quick Fix 按钮可交互 |
| **Screen ③** | SimulationMonitor (Captain 视图) | 50 Hz 遥测、IEC 62288 S-Mode HUD | ① 本船/目标船位置实时更新；② ODD Badge + ThreatRibbon 显示；③ ConningBar 7 字段实时刷新；④ ASDR Ledger 流式显示；⑤ Module Pulse M1-M8 GREEN |
| **Screen ④** | RunReport (Evaluator) | 仿真完成报告、KPI 卡片 | ① verdict.json 正确（PASS/FAIL + reason）；② 6 维评分实时数据（非硬编码）；③ ASDR Ledger 完整；④ Marzip export 1-click |

**DEMO-1 实装 Skeleton（Head-On Imazu-01 + analytical trajectory）：**

```
Browser (React 18)
  ├── Screen ①: SimulationScenario 3-Pane（ODD filter / 地图 / Inspector 4Tab）
  ├── Screen ②: SimulationCheck SSE stream（6 Gate 流式诊断 + GO/NO-GO）
  ├── Screen ③: SimulationMonitor Captain 视图（MapLibre Heading-Up + ConningBar + ODD Badge）
  └── Screen ④: RunReport 6 KPI cards（评分矩阵）
  
后端：sil_orchestrator (FastAPI :8000) + selfcheck_routes SSE /stream endpoint + scenario_store is_baseline 字段
WebSocket：foxglove_bridge 标准协议 (:8765) — analytical trajectory broadcast 50 Hz
物理模型：analytical Head-On trajectory（简化，不涉及 MMG）
```

**Done 判据**（综合 4 屏 spec）：

| 判据 | 可验证条件 | 状态 |
|---|---|---|
| 屏①ODD写入 | Builder SAVE 后 YAML 包含 metadata.odd_cell.{domain,visibility_nm,sea_state_beaufort} | ✅ plan Task 6 |
| 屏①Baseline只读 | 点击 IMAZU22 场景 SAVE → 弹出"另存 Custom"对话框，不发 PUT | ✅ plan Task 1 |
| 屏②SSE逐条点亮 | Network tab 看 event-stream，6 Gate 逐条推送 | ✅ plan Screen2 Task 0 |
| 屏②GO路径 | 6/6 PASS → 3s 倒数 → navigate #/monitor | ✅ plan Screen2 Task 8 |
| 屏③本船动画 | Monitor 地图显示本船移动，T≈150s 右转约 35°（analytical trajectory） | ✅ foxglove WS |
| 屏③ODD Badge | ODD-A 🟢 显示（来自 M1 mock state） | ✅ plan Task 2 |
| 屏④KPI卡片 | 6 维评分卡片显示实时数据（不是硬编码值） | ⏳ plan Screen4 D2.4 回填 |

### 5.2 DEMO-1 → DEMO-2 切换路线（关键转折）

| 维度 | DEMO-1（6/15）| DEMO-2（7/31）|
|---|---|---|
| 后端 | `tools/demo/demo_server.py` + `demo_ws_server.py`（standalone）| `sil_orchestrator` FastAPI + ROS2 真链路 |
| WebSocket | analytical trajectory broadcast | `foxglove_bridge` → rclpy → ROS2 topic |
| 物理模型 | analytical Head-On（线性 + 解析转弯）| ship_dynamics_node 4-DOF MMG + RK4 |
| 目标船 | 1 个解析航迹 | 3 个 ais_replay / ncdm / intelligent multi-spawn |
| 决策算法 | mock M4/M5/M6 输出 | L3 kernel M1-M8 production 真节点 |
| 评分 | stub 4 字段 | scoring_node 6 维 Hagen 2022 实时 |
| 证据 | scenario.yaml + scoring.json | + MCAP + Arrow + asdr_events.jsonl + verdict.json |
| ENC | OSM fallback / 本地 stub tile | S-57 MVT (Trondheim + SF Bay) via martin |

**切换闭环 effort 估算**（D2.4 + D2.5 + D2.6 范围）：~12–15 人周。

### 5.3 V&V Plan v0.1 Phase 1 SIL Exit Gate（X1.1–X1.6）

DEMO-1 仅满足 X1.5 的 22/22 Imazu PASS 中的 **1 个场景**（Imazu-01 Head-On）；其余 X1 项要求 ROS2 真链路接通：

| Gate ID | Criterion | DEMO-1 状态 | DEMO-2 目标 |
|---|---|---|---|
| X1.1 | 50 baseline 95% pass | 1/35 PASS | 33/35 (94%) |
| X1.2 | KPI 矩阵 30 runs | 0 | 30 |
| X1.3 | ASDR 一致性 | partial (asdr_events list) | 完整 timestamp/state/rationale |
| X1.4 | Coverage cube ≥ 10/1100 | 0 | 10 |
| X1.5 | Imazu-22 PASS | 1/22 | 22/22 |
| X1.6 | M7 watchdog 关键路径覆盖 | 0/7 modules | 7/7 |

### 5.4 Screen ② Simulation-Check 6-Gate SSE 流式诊断矩阵

每次仿真启动前，Simulation-Check 屏（Doc 3 §7）通过 `GET /api/v1/selfcheck/stream?scenario_id={id}` SSE 端点流式触发 6 道后端 Gate 探针（Doc 2 §2.6，gate_runner.py），逐条推送事件至前端 GateSequencer。

**6-Gate 规格表与 DEMO-1 验收准则**（spec Screen2 §3.1）：

| Gate | 名称 | 探针内容 | DEMO-1 PASS 条件 | SSE 流证据产物 |
|---|---|---|---|---|
| **1** | System Readiness | docker ps + WS握手 + martin ping | 所有 service healthy；:8765/:3000 响应 < 2s | gate_1.json (duration_ms + checks[]) |
| **2** | Module Health | M1-M8 pgrep + latency_us | 8/8 GREEN；`latency < 50ms`；`drops == 0` | gate_2.json (modulePulses[]) |
| **3** | Scenario Integrity | SHA256(yaml) vs stored + expected_outcome | hash 一致；ODD 解析无错 | gate_3.json (hash_match: bool) |
| **4** | ODD-Scenario Align | metadata.odd_cell 枚举校验 | domain ∈ valid_set（Phase 1 graceful PASS） | gate_4.json (odd_cell vs bounds) |
| **5** | Time Base | chronyc offset < 10ms + /sim_clock topic | drift < 10ms；rosbag2 pgrep 成功 | gate_5.json (drift_ms + checks[]) |
| **6** | Doer-Checker 隔离 [RED LINE] | M7 cgroup 独立 + import lint + /l3/checker_veto | M7 PID ≠ M1-M6；Checker 独立进程 | gate_6.json (m7_isolated: bool) |

**SSE 事件协议**（spec Screen2 §6.1-6.2）：

```json
// 每 Gate 完成立即推送（顺序流）
{"gate_id": 1, "label": "System Readiness", "passed": true, "checks": [...], "duration_ms": 230.4, "rationale": "..."}

// 最终事件
{"type": "complete", "all_clear": true/false, "go_no_go": "GO"/"NO-GO"}
```

**前端交互流（GateSequencer + DiagnosticCanvas）**（spec Screen2 §4）：

- Gate 行逐条激活：PENDING → RUNNING（脉动）→ PASS✅ / FAIL❌（耗时显示）
- 焦点 Gate 自动跟随最后一个 FAIL，中栏 DiagnosticCanvas 切换对应诊断视图（ROS2 拓扑 / Monaco Diff / 容器隔离图）
- 右栏 QuickFixPanel 显示针对失败 Gate 的修复按钮（restart_node / sync_time / ensure_asdr_dir 等 ops endpoint）
- GO 路径：6/6 PASS → GoNoGoPanel 绿色 overlay + 3s 倒数 → POST /lifecycle/activate → navigate #/monitor
- NO-GO 路径：任意 Gate fail → 底部 [Re-run Checks] 激活 → 重启 SSE 流

**证据产物位置**（spec Screen2 §3.2）：

运行时 staging：`scenarios/{scenario_id}/.preflight/gate_N.json`（SSE 流写入）
激活后归档：`runs/{run_id}/preflight/gate_N.json`（lifecycle activate 时复制）

**CI 集成**（DEMO-1 范围）：`tests/integration/test_preflight_gates.py`，在 `selfcheck_routes.py` / `gate_runner.py` / 6-Gate 节点变更时强制运行，验证：
- SSE stream 端点返回 event-stream 类型
- 6 Gate 逐条推送完整 JSON event
- final event `all_clear=true` 时 navigate 条件激活
- 证据文件 schema 合法 JSON

### 5.5 Screen ③ Simulation-Monitor 50Hz 遥测集成测试（Captain 视图 DEMO-1）

**50 Hz 遥测链路**（spec Screen3 §11）：foxglove_bridge WebSocket 标准协议 (:8765)，推送 OwnShip + Targets + ModulePulse + ASDR 流至 useTelemetryStore。

| 测试项 | 验证方法 | DEMO-1 通过准则 |
|---|---|---|
| **本船位置更新** | maplibregl 地图标记 50 Hz 跟随 | 本船红三角在屏幕底部 30%，heading vector 方向正确 |
| **目标船可见** | ThreatRibbon chip + 地图三角形 | 3 个目标船显示，CPA 排序正确，颜色按风险着色（>2nm 绿 / 1-2nm 琥珀 / <1nm 红） |
| **ODD Badge** | 左上角 ODD-A 🟢 显示 | ODD 域与 metadata.odd_cell 一致 |
| **ConningBar 7 字段** | 下方固定栏：HDG/SOG/COG/ROT/RUD/THR/DPT | 数值实时更新，sparkline 历史缓冲 60s |
| **Module Pulse** | 顶部 16px strip M1-M8 | 8/8 格子 🟢 GREEN（Phase 1 硬编码） |
| **ASDR Ledger** | 右下角可展开流式日志 | 10:42:18 [M1] ... 等事件流显示，时间戳递增 |
| **FSM 状态药丸** | 屏幕左上方 🟢 ACTIVE | 正确反映 FSM 当前状态（TRANSIT / COLREG_AVOIDANCE 等） |

**DEMO-1 不含** Engineer 视图（M4 IvP / M5 MPC / M6 5-层树 / M7 SOTIF / 时延条），这些推到 DEMO-2（D2.4）。

**重放确定性验证**（决策记录 §7，D1.3.1 仿真器鉴定子证明）：

同一 Imazu 场景 + 同一 seed → 输出可重现（航向/CPA 偏差 < 0.5°/100m），验证在 conftest.py 中添加：

```python
@pytest.mark.determinism
def test_imazu22_replay_determinism(scenario_id: str, seed: int):
    """验证同场景同 seed 的 50 次重放，轨迹重复性 ±0.1s 时间、±0.5° 航向、±100m 位置"""
    results = []
    for _ in range(50):
        result = run_scenario(scenario_id, seed)
        results.append(result.trajectory)
    
    ref_trajectory = results[0]
    for i, trajectory in enumerate(results[1:], 1):
        time_diff = max(abs(t1 - t2) for t1, t2 in zip(ref_trajectory.times, trajectory.times))
        heading_diff = max(abs(h1 - h2) for h1, h2 in zip(ref_trajectory.headings, trajectory.headings))
        cpa_diff = abs(ref_trajectory.min_cpa - trajectory.min_cpa)
        
        assert time_diff < 0.1, f"Run {i}: time offset {time_diff}s > 0.1s"
        assert heading_diff < 0.5, f"Run {i}: heading offset {heading_diff}° > 0.5°"
        assert cpa_diff < 100, f"Run {i}: CPA offset {cpa_diff}m > 100m"
```

---

## 6. DEMO 验收矩阵（6/15 → 7/31 → 8/31）

### 6.1 三档 milestone

| Milestone | 日期 | 范围 | 关键交付 |
|---|---|---|---|
| **DEMO-1 Skeleton Live** | 2026-06-15 | 4 屏 visual demo + Head-On analytical | acad427 + demo_server / demo_ws_server |
| **DEMO-2 Decision-Capable** | 2026-07-31 | ROS2 真链路 + 50 场景 + 6 维评分 + Marzip 1-click | D2.4/D2.5/D2.6 完整 |
| **DEMO-3 Full-Stack with Safety + ToR** | 2026-08-31 | 1000 场景立方体 + Doer-Checker verdict + ToR + S-Mode | D3.4/D3.5/D3.6/D3.8 完整 |

### 6.2 DEMO-2 验收矩阵（7/31）

| Demo Charter | 阈值 | 验证 |
|---|---|---|
| Imazu-22 全 PASS | 22/22 + CPA ≥ 200 m ratio ≥ 95% + COLREGs class ≥ 95% | X1.5 / `test-results/imazu22_results.json` |
| COLREGs 11 场景 PASS | 11/11 + 各 rule branch 单 PASS | V&V Plan X1.1 |
| 6 KPI 全部满足（V&V Plan §4）| AvoidancePlan ≤1.0s P95 + ReactiveOverrideCmd ≤200ms P95 + Mid-MPC <500ms + BC-MPC <150ms + M7 <10ms + M4 <100ms | `test-results/kpi_matrix.json` |
| Marzip 1-click 完整 | scenario + sha256 + arrow + mcap + asdr_events.jsonl + manifest + verdict.json 7 件全 | `runs/{id}/evidence.marzip` 解压验证 |
| 故障注入 3 类 | ais_dropout + radar_spike + dist_step 均成功注入并被 M7 检测 | ASDR ledger 3 项 verdict ≠ PASS |
| 6 维评分实时显示 | 屏 ③ ScoringGauges + ScoringRadarChart 实时刷新 + 总分计算 | 视觉 + scoring.arrow 列存验证 |

### 6.3 DEMO-3 验收矩阵（8/31）

| Demo Charter | 阈值 | 验证 |
|---|---|---|
| 1000 场景覆盖立方体 | 1100/1100 cells run + ≥ 95% 单 PASS + Monte Carlo 95% CI | `test-results/coverage_cube.json` |
| Doer-Checker verdict 显示 | 屏 ③ 显示 M7 PASS/RISK/FAIL 实时；屏 ④ 6 维评分单列 M7 一致性 | DoerCheckerVerdict topic 落地 |
| ToR 倒计时 panel | 触发 ToR 时 TorModal 显示 60s 倒计时 + 3-tier 升级 cue + auto-MRC fallback | TorModal 自动测试 + Veitch 2024 baseline |
| S-Mode 完整对齐 | 屏 ③ 显示元素全合 IMO MSC.1/Circ.1609 标准 | IEC 62288 inspector 审 |
| HAZID 132 [TBD] 全部回填 | 架构 v1.1.3 D3.8 + D3.5 全部参数填实 | 架构 v1.1.3 完整版 |
| TLS/WSS 加密 + Cybersec | RFC-007 落地（host network → DDS-Security）| `dds_security.xml` 验证 |

### 6.4 PR Fast Gate（CI 每 PR）

`tools/check_entry_gate.py --phase 1`（V&V Plan v0.1 §3.1）：

```
E1.1  All D1.x tasks closed                          100%
E1.2  colcon build clean on CI                       0 errors
E1.3  CI pipeline green                              all pass
E1.4  Scenario schema validated v1.0                 schema_validate.py 0 err
E1.5  Mock publisher frequencies ±5%                 frequency_check log
E1.6  E2E data flow sanity                          < 5s M1→M2→M4→M5→M8
E1.7  V&V Plan v0.1 committed                       file present
E1.8  M7 watchdog Python stub ≥1 PASS                pytest-report.json
```

任何 gate FAIL → block merge。

---

## 7. KPI 与评分

### 7.1 V&V Plan v0.1 §4 端到端 KPI 矩阵（🟢）

| KPI | 目标 | 测量 | Phase |
|---|---|---|---|
| AvoidancePlan P95 latency | ≤ 1.0 s | colcon test timing wrapper M4→M5→M8 | SIL Phase 1 |
| ReactiveOverrideCmd P95 | ≤ 200 ms | M7 override path timing | SIL Phase 1 |
| Mid-MPC solve time | < 500 ms | M5 OSQP/ECOS callback | SIL Phase 2 |
| BC-MPC solve time | < 150 ms | M5 BC horizon | SIL Phase 2 |
| M7 safety check | < 10 ms | M7 standalone benchmark | SIL Phase 1 |
| M4 arbitration cycle | < 100 ms | IvP objective fn eval + winner select | SIL Phase 2 |

测量协议：
1. 每 KPI 1000 连续决策周期采样
2. P95 + P99 由经验 CDF 计算
3. P99 之外 outlier 记 anomaly registry，V&V Engineer 审
4. 任何 KPI 超阈值 → 责任模块 D-task 重开做性能回归

### 7.2 6 维 Hagen 2022 + Woerner 2019 评分（决策记录 §8 🟡 / D2.4 完整化）

```
total_score = w_s · safety + w_r · rule - p_delay - p_mag + w_p · phase + w_pl · plausibility
```

| 维度 | 公式 | 来源 |
|---|---|---|
| **safety** | `f(CPA_min / CPA_target) ∈ [0,1]`，CPA ≥ target → 1.0；线性退化到 0 at CPA=0 | Hagen 2022 §II.C |
| **rule compliance** | per-rule {full=1.0 / partial=0.5 / violated=0.0} 加权和 | Woerner 2019 |
| **delay penalty** | `P_delay = max(0, t_action - t_target_action) × λ_1` | Hagen 2022 |
| **action magnitude penalty** | `<30° 或 >90° 扣分；2nd-order in deviation` | Rule 8 "大幅" |
| **phase score** | give-way 应早期大动作；stand-on 应保持课速直至 in extremis | Hagen 2022 |
| **trajectory implausibility** | M5 BC-MPC 解算约束自动满足；外部 target 检查曲率 + 加速度上限 | 防 RL "作弊" |

w 系数在 D1.7 规约（待 Hagen 2022 / Woerner 2019 原文细节填）；置信度 🟡 — 维度结构学术圈公认，权重值待 D1.7 校准。

### 7.3 二元 PASS/FAIL verdict

每 run 输出 `verdict.json`：

```json
{
  "pass": true,
  "reason": "All COLREGs rules satisfied; CPA min 0.42 nm >= target 0.16 nm",
  "kpis": {"min_cpa_nm": 0.42, "tcpa_min_s": 192, ...},
  "rule_chain": ["R14 detected @T+102", "R8 action @T+105", "R16 give-way @T+108"],
  "scoring_total": 0.87,
  "scoring_per_dim": {"safety": 0.92, "rule": 0.88, ...}
}
```

PASS 阈值：total_score ≥ 0.70 + 0 critical violation + verdict.kpis 全合 §7.1。

---

## 8. 故障注入

### 8.1 Phase 1 最小故障集（决策记录 §9 + commit a40d950）

| Fault Type | 触发 | 受影响节点 | 期望响应 |
|---|---|---|---|
| **ais_dropout** | ⚠ 按钮 + POST /api/v1/fault/inject {type: "ais_dropout", duration_s: 30, params: {pct: 50}} | sensor_mock_node | M2 短暂失目标，M4 转换到 sensor_degraded 模式，M7 不 VETO |
| **radar_spike** | ⚠ 按钮 + POST /api/v1/fault/inject {type: "radar_spike", duration_s: 10, params: {sigma_multiplier: 5}} | sensor_mock_node | M2 tracker noise 增 5×，M7 alert SOTIF 边界 |
| **dist_step** | ⚠ 按钮 + POST /api/v1/fault/inject {type: "dist_step", duration_s: 60, params: {wind_kn: 20}} | env_disturbance_node | M1 ODD violation alert（vis < 2000m 或 sea_state > scenario.beaufort + 2）|

### 8.2 Phase 2 扩展故障

| Fault Type | 用途 | D-task |
|---|---|---|
| roc_link_loss | 触发 ToR 倒计时 / TMR / auto-MRC | D3.4 |
| gps_spoofing | M2 nav filter 应识别 + 降级 | D2.4 (SOTIF) |
| comms_loss | M3 mission 应保 last-known WP | D2.4 |
| ddspartition | M7 应触发 MRC | D2.4 (FMEDA M7) |
| target_ghost | M2 应反 false-positive 跟踪 | D2.4 |

### 8.3 故障注入 UI

`FaultInjectPanel.tsx`（4.6 KB，commit a40d950）：

```
┌── Fault Inject Panel ────────┐
│ Fault Type: [Dropdown ▼]      │
│   • AIS Dropout (Phase 1)     │
│   • Radar Spike (Phase 1)     │
│   • Disturbance Step (Phase 1)│
│   • ROC Link Loss (Phase 2)   │
│ Duration: [10] s              │
│ Params:                        │
│   pct: [50]                    │
│ [Trigger] [Cancel Active]     │
└──────────────────────────────┘
```

`useInjectFaultMutation()` + `useCancelFaultMutation()` (Doc 3 silApi.ts:140-146)。

---

## 9. Screen ① / ② / ③ 集成测试规格（来自 2026-05-18 specs）

### 9.1 Screen ① Scenario Studio 集成测试（来自 spec Screen1）

**测试覆盖域**（位置：`web/src/screens/SimulationScenario.tsx`）：

| 用例编号 | 功能 | 测试驱动 | DEMO-1 验收 |
|---|---|---|---|
| **S1-ODD-01** | ODD 域选择 → `metadata.odd_cell` 写入 | 选择 domain=coastal，保存，检查 YAML | `metadata.odd_cell.domain = "coastal"` 出现 |
| **S1-ODD-02** | 海况 Beaufort 选择 → 写入 | 选择 Beaufort≤5，检查 YAML | `metadata.odd_cell.sea_state_beaufort = 5` |
| **S1-ODD-03** | 能见度选择 → 写入 | 选择 visibility>2nm，检查 YAML | `metadata.odd_cell.visibility_nm = 2.0` |
| **S1-MAP-01** | 拖拽本船位置 → `ownShip.initial.position` 更新 | useMapInteraction hook 触发 onYamlPatch | 鼠标拖本船、mouseup 后，lat/lon 字段更新 |
| **S1-MAP-02** | 拖拽目标船位置 | drag → mouseup → `targetShips[n].initial.position` | 同上，针对目标船 TGT-1 |
| **S1-MAP-03** | 拖拽 COG 线端点改航向 | drag COG arrow tip → `ownShip.initial.heading` | 航向字段更新为新计算值（±0.1°精度） |
| **S1-MAP-04** | 拖拽 WP 节点 | click 地图 → add WP → drag → `voyageTask.waypoints[n]` | 坐标更新，地图即时重绘 |
| **S1-VALID-01** | Schema 校验灯 | 输入非法 SOG=999 → validator 指示灯红 | Sticky Footer 校验灯变红，显示错误路径 |
| **S1-BASELINE-01** | Baseline 只读（是否锁定 SAVE） | 选 IMAZU22 场景，点 SAVE | "另存为 Custom"对话框弹出，不发 PUT |
| **S1-BASELINE-02** | 后端 PUT 409 检测 | 构造 PUT /api/v1/scenarios/imazu22_id | HTTP 409，body 含"read-only Baseline" |
| **S1-HASH-01** | Custom SAVE 后取真实 hash | 保存 Custom 场景 → Sticky Footer hash | 显示来自 POST 响应的 SHA256（非占位符） |
| **S1-RUN-01** | RUN 跳转 | 点[RUN → ②]按钮 | navigate 到 `#/check/{scenario_id}`，Preflight 屏加载 |
| **S1-TAB2-01** | Tab 2 环境外壳 | 点击 Tab 2（环境与故障） | 显示 Phase 2 占位提示，不崩溃 |
| **S1-TAB3-01** | Tab 3 断言外壳 | 点击 Tab 3（行为断言） | 显示 Phase 2 占位提示，不崩溃 |

**自动化 Playwright 示例**（`tests/e2e/screen1-scenario-builder.spec.ts`）：

```typescript
test('S1-ODD-01: ODD domain → YAML metadata.odd_cell', async ({ page }) => {
  await page.goto('http://localhost:5173/#/scenario');
  await page.selectOption('[data-testid="odd-domain-select"]', 'coastal');
  await page.click('[data-testid="save-button"]');
  const yaml = await page.locator('[data-testid="yaml-source"]').textContent();
  expect(yaml).toContain('metadata:');
  expect(yaml).toContain('odd_cell:');
  expect(yaml).toContain('domain: coastal');
});

test('S1-MAP-01: Drag own ship → position updates', async ({ page }) => {
  const mapCanvas = page.locator('canvas[data-testid="sil-map"]').first();
  const bounds = await mapCanvas.boundingBox();
  await mapCanvas.dragTo({ x: 100, y: 100 }, { x: 150, y: 120 });
  await page.waitForTimeout(300); // debounce
  const yaml = await page.locator('[data-testid="yaml-source"]').textContent();
  expect(yaml).toMatch(/ownShip.*position.*latitude.*\d+\.\d+/);
});

test('S1-BASELINE-02: Baseline PUT → 409 Conflict', async ({ page, context }) => {
  // Expect 409 from backend when attempting PUT on baseline scenario
  let response409seen = false;
  page.on('response', r => {
    if (r.status() === 409) response409seen = true;
  });
  // Attempt PUT via RTK mutation (would trigger in real scenario)
  // For now, intercept and verify error handling
});
```

### 9.2 Screen ② Simulation-Check 6-Gate 集成测试（来自 spec Screen2）

**Test Fixtures 位置**：`tests/integration/test_preflight_gates.py` + `src/sil_orchestrator/gate_runner.py`

| Gate ID | 功能 | 测试点 | DEMO-1 验收准则 |
|---|---|---|---|
| **G1** | System Readiness | docker ps / WS 握手 / martin ping | 所有 service healthy；响应 < 2s |
| **G2** | Module Health | M1-M8 pgrep + latency_us | 8/8 GREEN；latency < 50ms；drops == 0 |
| **G3** | Scenario Integrity | SHA256 匹配 + expected_outcome 解析 | hash 一致；ODD 解析无错 |
| **G4** | ODD-Scenario Align | metadata.odd_cell 枚举校验 | domain ∈ {open_sea, coastal, ...}（Phase 1 graceful PASS） |
| **G5** | Time Base | chronyc offset < 10ms + /sim_clock | drift < 10ms；rosbag2 pgrep OK |
| **G6** | Doer-Checker Isolation | M7 cgroup + import lint + /l3/checker_veto | M7 PID ≠ M1-M6；独立进程 |

**SSE 流事件规格**（`GET /api/v1/selfcheck/stream?scenario_id=...`）：

```json
// 每 Gate 完成立即推送（JSON lines）
{"gate_id": 1, "label": "System Readiness", "passed": true, "checks": [...], "duration_ms": 230.4}
{"gate_id": 2, "label": "Module Health", "passed": true, "modulePulses": [...]}
...
{"type": "complete", "all_clear": true, "go_no_go": "GO"}
```

**前端 GateSequencer 验证**（`web/src/screens/SimulationCheck.tsx`）：

```typescript
test('SSE stream gate sequencer — 6 gates activate + GO/NO-GO', async ({ page }) => {
  await page.goto('http://localhost:5173/#/check/imazu-01-ho');
  // Monitor SSE stream via Network tab or mock
  const gateRows = page.locator('[data-testid="gate-row"]');
  await expect(gateRows).toHaveCount(6);
  // Wait for all gates to pass
  for (let i = 1; i <= 6; i++) {
    const row = gateRows.nth(i - 1);
    await expect(row).toContain('PASS');
  }
  // Final GO panel
  const goPanel = page.locator('[data-testid="go-panel"]');
  await expect(goPanel).toBeVisible();
});
```

**证据文件格式**（置于 `scenarios/{scenario_id}/.preflight/gate_N.json`）：

```json
{
  "gate_id": 1,
  "label": "System Readiness",
  "passed": true,
  "duration_ms": 235.2,
  "timestamp": "2026-05-15T14:23:45Z",
  "checks": [
    {"name": "docker ps", "passed": true, "latency_ms": 45},
    {"name": "WS :8765", "passed": true, "latency_ms": 120}
  ]
}
```

### 9.3 Screen ③ Simulation-Monitor 50Hz 遥测集成测试（来自 spec Screen3）

**Test Fixtures 位置**：`tests/integration/test_monitor_telemetry.py` + foxglove_bridge mock

| 测试项 | 信息源 | DEMO-1 验证方法 | 通过准则 |
|---|---|---|---|
| **本船位置更新** | `useTelemetryStore.ownShip` @ 50 Hz | MapLibre canvas 标记移动 | 红三角在屏幕底部 30%，heading vector 方向正确 |
| **目标船可见** | `useTelemetryStore.targets[]` | ThreatRibbon chip + 地图三角形 | 3 个目标显示，CPA 排序正确，风险着色准确 |
| **ODD Badge** | `useTelemetryStore.satData?.odd_cell` | 左上角 ODD-A 🟢 | ODD 域与 metadata.odd_cell 一致 |
| **ConningBar 7 字段** | `ownShip.{hdg, sog, cog, rot, rud, thr, dpt}` | 数值实时刷新 | 更新频率 50 Hz，无卡顿 |
| **Module Pulse M1-M8** | `useTelemetryStore.modulePulse[]` | 顶部 16px strip | 8/8 格子 🟢 GREEN（DEMO-1 硬编码） |
| **ASDR Ledger** | `useTelemetryStore.asdrEvents[]` | 右下角流式日志 | 时间戳递增，消息完整 |
| **FSM 状态药丸** | `useFsmStore.currentState` | 左上方状态显示 | 反映当前 FSM 状态（TRANSIT/COLREG_AVOIDANCE） |

**WebSocket 遥测订阅（foxglove_bridge 标准协议）**：

| Topic | 频率 | 类型 | 用途 |
|---|---|---|---|
| `/sil/own_ship_state` | 50 Hz | OwnShipState | 本船位置、航向、速度 |
| `/sil/target_vessel_state` | 10 Hz | TargetVesselArray | 目标船跟踪数据 |
| `/l3/module_pulse` | 10 Hz | ModulePulse[] | M1-M8 健康状态 |
| `/l3/sat1_data` | 10 Hz | SAT1Data | 实时状态透明性 |
| `/l3/asdr_record` | 10 Hz | AsdrEvent | 审计日志流 |

**50 Hz 确定性重放测试**（决策记录 §7，D1.3.1 仿真器鉴定）：

```python
# tests/integration/test_replay_determinism.py
@pytest.mark.determinism
def test_imazu22_replay_determinism():
    """Same scenario + seed → 50 runs reproduce within ±0.1s / ±0.5° / ±100m"""
    ref_result = run_scenario('imazu-01-ho', seed=12345)
    
    for run_id in range(1, 50):
        result = run_scenario('imazu-01-ho', seed=12345)
        time_diff = max(abs(t1 - t2) for t1, t2 in zip(ref_result.times, result.times))
        heading_diff = max(abs(h1 - h2) for h1, h2 in zip(ref_result.headings, result.headings))
        cpa_diff = abs(ref_result.min_cpa - result.min_cpa)
        
        assert time_diff < 0.1, f"Run {run_id}: time {time_diff}s"
        assert heading_diff < 0.5, f"Run {run_id}: hdg {heading_diff}°"
        assert cpa_diff < 100, f"Run {run_id}: CPA {cpa_diff}m"
```

**Captain 视图 Playwright 示例**：

```typescript
test('Captain view: 50 Hz telemetry integration', async ({ page }) => {
  await page.goto('http://localhost:5173/#/monitor/run-abc123');
  
  // Verify own ship marker updates
  const ownShipMarker = page.locator('[data-testid="own-ship-marker"]');
  const initialPos = await ownShipMarker.boundingBox();
  await page.waitForTimeout(2000); // Let 100 frames render
  const newPos = await ownShipMarker.boundingBox();
  expect(newPos.x).not.toBe(initialPos.x);
  
  // Verify ConningBar updates
  const hdgField = page.locator('[data-testid="conning-hdg"]');
  const hdg1 = await hdgField.textContent();
  await page.waitForTimeout(100);
  const hdg2 = await hdgField.textContent();
  expect(hdg2).not.toBe(hdg1); // Should change within 100ms @ 50Hz
});
```

---

## 10. ASDR 证据链

### 10.1 ASDR 数据流（架构报告 §11 + Doc 2 §11.3）

```
kernel M8 ──────► /l3/asdr_record (10 Hz) ──────────► rosbag2 → MCAP
                                                       (full record)

kernel M8/scoring ─► /sil/asdr_event (event) ──────► telemetry WS → FE Ledger
                                                       (human-readable filtered)
```

---

## 11. Screen ① useMapInteraction Hook 规格（D1.3b.2 集成）

**Hook 接口契约**（来自 Screen1 spec §3）：

```typescript
interface MapInteractionOptions {
  mapRef: RefObject<maplibregl.Map>;
  previewData: PreviewData | null;
  onYamlPatch: (path: string, value: unknown) => void;  // debounced @ 200ms
}

interface MapInteractionReturn {
  dragState: DragState;
  wpNodes: WaypointNode[];
  setWpNodes: Dispatch<SetStateAction<WaypointNode[]>>;
}
```

**onYamlPatch 路径映射**（Screen1 spec §3.2）：

| 交互 | path | value 类型 | 验证条件 |
|---|---|---|---|
| 本船拖拽→lat | `ownShip.initial.position.latitude` | number (6位小数) | `-90 ≤ lat ≤ 90` |
| 本船拖拽→lon | `ownShip.initial.position.longitude` | number | `-180 ≤ lon ≤ 180` |
| 目标拖拽→lat/lon | `targetShips[n].initial.position.{latitude,longitude}` | number | 同上 |
| WP 拖拽→lat/lon | `voyageTask.waypoints[n].{lat,lon}` | number | 同上 |
| COG 拉伸→航向 | `ownShip.initial.heading` | number (0-360°) | `0 ≤ hdg < 360` |

**性能约束**（Screen1 plan Task 3）：

- Hit-test 半径：vessel 20px, WP 15px, COG 12px
- Drag ghost rendering 无 onYamlPatch 调用（仅 mousemove 中间态）
- mouseup 时单次 onYamlPatch 调用
- 200ms debounce 包装以防地图高频重绘卡顿

---

## 12. Gateway Acceptance Criteria（D1.5 / D1.6 V&V Plan 交界）

### 12.1 Screen ① Builder 交付门（DEMO-1）

Pass criteria（Screen1 spec §10）：

- [ ] ODD 写入：metadata.odd_cell 三字段正确
- [ ] Baseline 只读：SAVE → 另存 Custom，不发 PUT；PUT → 409 Conflict
- [ ] Custom SAVE：Sticky Footer 显示真实 SHA256（非占位符）
- [ ] 拖拽本船：lat/lon 字段更新，地图重绘
- [ ] 拖拽目标船：同上
- [ ] COG 拉伸：heading 更新
- [ ] WP 节点拖拽：waypoints 更新
- [ ] Schema 校验：非法参数 → 红灯 + 错误路径
- [ ] Tab 2/3 外壳：显示 Phase 2 占位，不崩溃
- [ ] RUN 流程：navigate `#/check/:id`，Preflight 加载
- [ ] Preflight Gate 4：metadata.odd_cell.domain 读取，与 M1 ODD 对比

### 12.2 Screen ② Preflight 交付门（DEMO-1）

Pass criteria（Screen2 spec §3-4）：

- [ ] 6-Gate SSE 流完整：6 Gate 事件逐条推送
- [ ] Gate 顺序正确：1→2→...→6 串行
- [ ] GO/NO-GO 判定准确：全 PASS → GO；任意 FAIL → NO-GO
- [ ] 证据文件：gate_N.json 完整合法 JSON
- [ ] Quick Fix 操作：restart_node / sync_time 端点可调
- [ ] 倒计时和导航：GO → 3s 倒数 → POST /lifecycle/activate → `#/monitor`

### 12.3 Screen ③ Monitor 交付门（DEMO-1）

Pass criteria（Screen3 spec §3-5）：

- [ ] 本船位置 50 Hz 更新：MapLibre 标记连续移动
- [ ] 目标船可见：ThreatRibbon + 地图三角形，CPA 排序准确
- [ ] ODD Badge：左上角显示，与 metadata.odd_cell 一致
- [ ] ConningBar 7 字段：50 Hz 刷新，无卡顿
- [ ] Module Pulse M1-M8：8/8 GREEN（DEMO-1 硬编码）
- [ ] ASDR Ledger：流式日志显示，时间戳递增
- [ ] FSM 状态药丸：反映当前状态（TRANSIT/COLREG_AVOIDANCE）

### 12.4 PR Fast Gate（CI 强制）

每个 PR 必须通过 `tools/check_entry_gate.py --phase 1`：

```bash
E1.1  All D1.x tasks closed                          100%
E1.2  colcon build clean on CI                       0 errors
E1.3  CI pipeline green                              all pass
E1.4  Scenario schema validated v1.0                 schema_validate.py 0 err
E1.5  Mock publisher frequencies ±5%                 frequency_check log
E1.6  E2E data flow sanity                          < 5s M1→M2→M4→M5→M8
E1.7  V&V Plan v0.1 committed                       file present
E1.8  M7 watchdog Python stub ≥1 PASS                pytest-report.json
```

任何 Gate FAIL → block merge。

---

## 13. Known Gaps & Deferred (Non-DEMO-1)

| Gap ID | 描述 | 推迟到 | 理由 |
|---|---|---|---|
| GAP-004 | Tab 2 Env+Fault 真实时间轴注入 | Phase 2 D2.x | 时间轴事件驱动 mock 复杂 |
| GAP-005 | Tab 3 Behavioral Assertions 编辑器 | Phase 2 D2.x | 预期结果矩阵需求梳理 |
| GAP-006 | Marzip 完整格式（MCAP+Arrow） | Phase 2 D2.4 | 依赖 scoring_node 实现 |
| GAP-024 | Engineer 视图（M4/M5/M6/M7） | DEMO-2 D2.4 | 算法决策链条白盒化 |
| GAP-025 | 故障注入 Phase 2 扩展 | Phase 2 D2.4 | ais_spoofing / comms_loss |
| [TBD-HAZID] | ToR 三段升级秒数（20/45s 暂定） | D3 前 5/28 | Veitch 2024 完整 PDF 校准 |
| [TBD-HAZID] | 三级安全域距离 (ODD-A: 2.0/1.0/0.3nm) | HAZID 8/19 | RUN-001 校准回填 Manifest |

---

## 14. 修订记录

| 版本 | 日期 | 改动 |
|---|---|---|
| v1.0 | 2026-05-15 | 初稿：35 场景库（22 IMAZU + 11 COLREGs + 1 head-on + 1 user），maritime-schema 迁移路线，端到端调用链，DEMO 三档验收矩阵，KPI & 评分规格，ASDR 证据链，仿真器鉴定。|
| v1.1 | 2026-05-19 | 整合 2026-05-18 Screen specs：Screen ① useMapInteraction hook 规格 + ODD/Baseline/Schema 测试用例（14 条）；Screen ② Simulation-Check 6-Gate SSE 流式诊断（DEMO-1 新增）；Screen ③ Monitor 50 Hz 遥测集成（Captain 视图 DEMO-1 范围）；Playwright/pytest 自动化示例代码；Gateway Acceptance Criteria 三屏交付门；确定性重放测试规格。未修改：YAML schema / 场景库结构 / 端到端链路 / KPI / Marzip（这些已在 v1.0 基线中）。|

orchestrator export → asdr_events.jsonl (post-process from MCAP)
                                                       (CCS surveyor friendly)
```

### 9.2 `l3_msgs/ASDRRecord` 字段（v1.1.2 锁定）

`src/l3_tdl_kernel/l3_msgs/msg/ASDRRecord.msg`：包含完整决策上下文（timestamp + module state snapshot + rationale + alternatives + chosen + confidence + veto if any）。

### 9.3 `sil_msgs/ASDREvent` 简化版（用于 FE）

```
sil_msgs/ASDREvent
├── stamp           builtin_interfaces/Time
├── event_type      string  # "rule_detected" / "decision" / "veto" / "alert"
├── rule_ref        string  # "R14" / "R8" / "R15_Stbd" ...
├── decision_id     string  # UUID 关联 ASDRRecord
├── verdict         uint8   # 0 UNKNOWN / 1 PASS / 2 RISK / 3 FAIL
└── payload_json    string  # JSON 字符串可选附加上下文
```

### 9.4 Marzip 证据容器（DNV maritime-schema 兼容）

```
{run_id}_evidence.marzip （zip 容器）
├── scenario.yaml             ✅ Phase 1 实施
├── scenario.sha256           ✅
├── manifest.yaml             ✅（含 toolchain version / L3 kernel git SHA / sim_workbench SHA）
├── scoring.json              ✅ (stub Phase 1) → ⏳ scoring.arrow (Phase 2)
├── results.bag.mcap          ⏳ Phase 2 — rosbag2 zstd message-mode
├── results.bag.mcap.sha256   ⏳
├── asdr_events.jsonl         ⏳ Phase 2 — post-process from MCAP
└── verdict.json              ⏳ Phase 2 — final PASS/FAIL + reason + KPIs
```

### 9.5 CCS surveyor 友好性

- 中文格式导出器（CCS 拒 maritime-schema 时 fallback，GAP-012）
- 中文 KPI 报告（含规范条款映射 + DMV-CG-0264 9 子功能覆盖矩阵）
- 三方 SIL 2 评估（D4.3 TÜV/DNV/BV）可读

---

## 10. Mock 替换路线

### 10.1 现有 Mock 清单

| Mock | 路径 | 退役条件 | D-task |
|---|---|---|---|
| `tools/demo/demo_server.py` | `tools/demo/` | DEMO-2 通过 → 删除 | D2.4 |
| `tools/demo/demo_ws_server.py` | `tools/demo/` | 同上 | D2.4 |
| `l3_external_mock_publisher` | `src/l3_tdl_kernel/l3_external_mock_publisher/` | sensor_mock + tracker_mock 真链路 PASS | D2.4 |
| `sil_mock_publisher` | `src/sim_workbench/sil_mock_publisher/` | ship_dynamics + sensor_mock 全部 LifecycleNode PASS | D1.3a/b |
| `selfcheck_routes.py` 5-硬编码 | `src/sil_orchestrator/selfcheck_routes.py` | 6-gate sequencer 实施 PASS | D1.3b.3 |
| `_seed_run_dir` scoring stub | `src/sil_orchestrator/main.py:67-83` | scoring_node Arrow 真输出 | D2.4 |
| `Preflight.tsx` 600ms 假延迟 | `web/src/screens/Preflight.tsx` | 6-gate UI 重写 + 真实 API | D1.3b.3 |
| `telemetry_bridge.py` 自制 WS | `src/sil_orchestrator/telemetry_bridge.py` | foxglove_bridge 标准 protocol | D1.3b.3 |

### 10.2 替换矩阵（按 D-task）

| D-task | 期完成 | Mock → 真链路 |
|---|---|---|
| D1.3a | 6/9 | `sil_mock_publisher` → `ship_dynamics_node` 4-DOF MMG + `sensor_mock_node` AIS Class A/B |
| D1.3b.3 | 6/15 | `telemetry_bridge.py` → foxglove_bridge；`Preflight.tsx` 假 5 检查 → 真 6-gate；`selfcheck_routes.py` → 真 sequencer |
| D2.4 | 7/27 | `demo_*` → ROS2 真链路；`l3_external_mock_publisher` → 真 Fusion stub；scoring_node 6 维实时 |
| D2.5 | 7/31 | Marzip 7 件齐全；MCAP→Arrow 后处理 |

### 10.3 Cutover 策略

- **Feature flag**：scenario YAML 字段 `simulation_settings.backend: demo | ros2`，前期允许 demo 模式 fallback；DEMO-2 后强制 ros2
- **Parallel run**：DEMO-2 前两周允许 demo + ros2 同时启动，对比输出（trajectory diff < 2%）
- **删除 commit**：每个 mock 退役独立 commit，commit message 引用 D-task ID + V&V 报告 hash

---

## 11. 仿真器鉴定 D1.3.1（DNV-RP-0513 + DNV-CG-0264 映射）

### 11.1 D1.3.1 范围（决策记录 §2 + V&V Plan §8）

D1.3.1 *Simulator Qualification Report* 是 SIL 系统进入 CCS / DNV 认证路径的入门票。提交时间：6/8–6/15（DEMO-1 前完成）。

**4 项核心证明**（subagent [W25][W26]）：

| 证明 | 方法 | 验证标 |
|---|---|---|
| **模型保真度** | MMG hydrodynamics 校准 vs FCB 池实验 or CFD | RMS error ≤ 5%（DNV-RP-0513 §模型保证）|
| **决定性 replay** | 同场景 + 同 seed → 同输出 | 时间偏移 ±0.1s，航向重复性 ±0.5° |
| **传感模拟置信** | Radar/AIS/GNSS 退化模型按 DNV-CG-0264 §6 环境限值校准 | per-sensor confidence per dropout/noise/range bin |
| **编排可验证性** | OSP cosim 日志 + FMU API call trace + 通信步长审计 | full audit log via libcosim |

### 11.2 D1.3.1 交付物清单

```
docs/Design/SIL/D1.3.1-simulator-qualification/
├── 01-overview.md                   # 范围 + 验证策略
├── 02-model-fidelity-report.md      # MMG vs 池实验 / CFD diff
├── 03-determinism-replay.md         # 1000 次 replay 重复性数据
├── 04-sensor-confidence.md          # per-sensor 退化 vs CG-0264 限值
├── 05-orchestration-trace.md        # libcosim API trace + 步长审计
├── 06-evidence-matrix.md            # 4 项证明 → DNV-RP-0513 条款映射
└── annex/
    ├── test-results/                # CI artifact dump
    ├── csv/                         # 数据原件
    └── ccs-mapping.md               # CCS 智能船舶规范 §9.1 性能验证条款映射
```

### 11.3 DNV-CG-0264 §3 V&V Plan 映射

| CG-0264 §3 章节 | v1.0 套件章节 |
|---|---|
| §3.1 Verification Plan | V&V Plan v0.1 整体 |
| §3.2 Test Phases | V&V Plan §3 Entry/Exit Gates |
| §3.3 KPI Definition | V&V Plan §4 + Doc 4 §7 |
| §3.4 Coverage | Doc 4 §3.2 + V&V Plan §5 |
| §3.5 Evidence | Doc 4 §9 ASDR + Marzip |
| §3.6 Simulator Qualification | Doc 4 §11（本节）|

### 11.4 工具链合规背书

| 工具 | License | DNV 背书 | 用途 |
|---|---|---|---|
| `dnv-opensource/maritime-schema` v0.2.x | MIT | 主推 | scenario + output schema |
| `open-simulation-platform/libcosim` | MPL-2.0 | DNV 主推 | FMI 2.0 co-sim master |
| `dnv-opensource/farn` v0.4.2 | MIT | DNV 主推 | n-dim case folder generator |
| `dnv-opensource/ospx` | MIT | DNV 主推 | OSP system structure |
| Brinav-DNV MoU (2024) | — | 商业验证 | 中国海事用例先例 |

### 11.5 CCS i-Ship N AIP 提交（D4.4，2026-11）

D1.3.1 报告 + Phase 1 SIL X1 + Phase 2 SIL X2 + Phase 3 sea trial X3 共同构成 CCS AIP 证据包。仿真器鉴定是其中**头号必需件**。

### 11.6 D1.3.1 交付状态（2026-05-15 更新）

| 交付物 | 文件 | 状态 |
|---|---|---|
| 01-overview.md | 范围 + 验证策略 + 4 核心证明 + 风险表 | ✅ committed |
| 02-model-fidelity-report.md | MMG 4-DOF 保真度 vs Nomoto 3 参考解 | ✅ committed（🟡 实测数据待 D1.3a 5/31 交付）|
| 03-determinism-replay.md | 1000 次重放方法论 + 4 组 1300 次试验设计 | ✅ committed（🟡 实测数据待 D1.3b.3 6/15 交付）|
| 04-sensor-confidence.md | Radar/AIS/GNSS 退化模型 vs CG-0264 §6 限值 | ✅ committed（🟡 实测数据待 sensor_mock 交付）|
| 05-orchestration-trace.md | libcosim 5 API trace + 8 项步长审计清单 | ✅ committed（🟡 实测数据待 libcosim observer 集成）|
| 06-evidence-matrix.md | 4 项证明 → DNV-RP-0513 §4–§7 17 行条款映射 | ✅ committed（🔴 DNV 完整条款文字待 GAP-032 PDF 采购）|
| 07-ccs-mapping.md | CCS §9.1 12 条款映射 + i-Ship 标志 + surveyor 审核清单 | ✅ committed |
| annex/ccs-communication-schedule.md | CCS surveyor 沟通日历 + 2 封发函模板 | ✅ committed |
| annex/test-results/ | CI artifact dump（3 Nomoto CSV + 5 次重放 CSV）| ✅ committed |
| annex/csv/ + annex/plots/ | 数据原件 + 图表目录 | ✅ 目录就绪 |
| 自动化工具 × 4 | `tools/sil/d1_3_1_{mmg_fidelity,determinism_replay,sensor_calibrate,orch_trace}.py` + 各测试文件 | ✅ committed（25 测试通过）|
| GAP-005 | self_check 硬编码 PASS → 6 真实探针函数 | ✅ CLOSED（commit `b07d7ff`）|
| GAP-032 | DNV-RP-0513/CG-0264 完整付费 PDF | ⏳ 采购中（条款映射使用公开摘要作 interim）|

---

## 12. 文件谱系 + 调研记录（增量）

继承 Doc 1 §13.2 + Doc 2 §13 + Doc 3 §12 [Ex]/[R-NLM:N]/[Wx] 引用编号。本 Doc 新增源：

- [W62] V&V Plan v0.1（`docs/Design/V&V_Plan/00-vv-strategy-v0.1.md`，commit e1a13e5，22.7 KB）— 内部 A 🟢
- [W63] DEMO-1 spec（`docs/superpowers/specs/2026-05-14-sil-demo1-head-on-design.md`，15.2 KB）— 内部 A 🟢
- [W64] V&V Phase 1 artifacts spec（`docs/Design/V&V_Plan/2026-05-12-vv-phase1-artifacts.md`，41.4 KB）— 内部 A 🟢
- [W65] Imazu (1987) canonical → Sawada, Sato, Majima (2021) *Automatic ship collision avoidance using deep RL with LSTM in continuous action spaces*, J. Mar. Sci. Technol. 26, DOI: 10.1007/s00773-020-00773-y — A 🟢
- [W66] Tengesdal & Johansen (2023) CCTA *Imazu benchmark replication* — A 🟢（imazu-14-ms.yaml description 引用）
- [W67] DNV-CG-0264 §3 *Verification Plan structure* — A 🟡（付费访问）
- [W68] DNV-RP-0513 §模型保证 *Assurance of simulation models* — A 🟡（付费访问）

**置信度分布**：5× 🟢 High + 2× 🟡 Medium（DNV 付费规范，已在 V&V Plan v0.1 内引用）。

**待补研究**（套件 v1.0 范围外，留 v1.1 / Phase 2 闭口）：

- [W-pending-3] Veitch 2024 *60-second TMR baseline* 完整 PDF（DEMO-3 ToR 验收前）
- [W-pending-4] DNV-RP-0513 / DNV-CG-0264 完整付费 PDF 访问（D1.3.1 鉴定报告正式提交前）
- [W-pending-5] OpenBridge GitHub package.json 主版本号直检（Doc 3 §5.3）
- [W-pending-6] Hagen 2022 / Woerner 2019 完整 thesis（D1.7 6 维评分系数校准）

---

## 13. 套件完整性 + 累计 GAP 总览

### 13.1 套件 v1.0 完整

| Doc | 状态 | 大小（KB）|
|---|---|---|
| **Doc 0 README** | ✅ 基线 | ~15 |
| **Doc 1 架构** | ✅ 基线 | ~37 |
| **Doc 2 后端** | ✅ 基线 | ~55 |
| **Doc 3 前端** | ✅ 基线 | ~52 |
| **Doc 4 场景联调** | ✅ 基线（本文档）| ~50 |
| **总计** | ✅ 套件 v1.0 完整化 | ~209 KB |

### 13.2 累计 GAP 总览（29 → 32）

| 来源 | GAP 编号 | 数量 |
|---|---|---|
| Doc 1 | GAP-001 ~ GAP-014 | 14 |
| Doc 2 | GAP-015 ~ GAP-021 | 7 |
| Doc 3 | GAP-022 ~ GAP-029 + GAP-NEW-001 | 9 |
| Doc 4（本文档新增）| GAP-030 ~ GAP-032 | 3 |
| **跨套件总计** | | **33** |

### 13.3 Doc 4 新增 GAP

| GAP | 描述 | 现状 | 修复路径 | D-task |
|---|---|---|---|---|
| **GAP-030** | 2 套 schema（v1.0 ENU + v2.0 lat/lon）并存，无统一 maritime-schema | scenarios/ 35 个 YAML | D1.6 迁移到 maritime-schema TrafficSituation + metadata 扩展节 | D1.6 |
| **GAP-031** | DEMO-1 用 standalone `demo_server.py` + `demo_ws_server.py` + analytical trajectory（非 ROS2 真链路） | tools/demo/ | DEMO-2 前 cutover 到 sil_orchestrator + ROS2 真链路；feature flag `simulation_settings.backend: demo|ros2` | D2.4 |
| **GAP-005** | self_check 5 项硬编码 PASS（非真链路探测）| `src/sil_orchestrator/selfcheck_routes.py` | ✅ **CLOSED** — 6 真实探针函数（commit `b07d7ff`），含 ros2 lifecycle/ENC/ASDR/UTC/Scenario hash/M7 watchdog 探针 | D1.3.1 |
| **GAP-032** | DNV-RP-0513 / CG-0264 完整付费 PDF 未访问 | V&V Plan v0.1 仅引摘要 | ⏳ IN PROGRESS — D1.3.1 报告已用公开摘要完成条款映射（06-evidence-matrix.md）；完整文字待 PDF 购入后回填 | D1.3.1 |

### 13.4 GAP 按修复 D-task 分布

| D-task | GAP | 期 |
|---|---|---|
| D1.3a | GAP-020 (ship_dynamics MMG) + GAP-018 部分 | 6/9 |
| D1.3b.3 | GAP-015 (WS 端口) + GAP-016 (Executor) + GAP-018 (LifecycleNode 升级) + GAP-023/024/025 (Preflight 重设计) + GAP-026 (foxglove client) + GAP-028 (OpenBridge ver) + GAP-029 (4 屏文件重命名) + **GAP-NEW-001** (Baseline 只读保护) | 6/15 |
| D1.6 | GAP-003 (head_on.yaml schema) + GAP-017 (validate stub) + GAP-022 (客户端 schema 校验) + GAP-030 (双 schema 统一) | 6/9 |
| D1.3c | GAP-001 (jazzy → humble Dockerfile) | 7/15 |
| D1.3.1 | GAP-005 (selfcheck 真 ✅ CLOSED) + GAP-032 (DNV 完整规范 ⏳ 采购中) | 6/15 |
| D2.4 | GAP-002 (Mock 退役) + GAP-021 (scoring 真) + GAP-027 (8 KPI 完整) + GAP-031 (DEMO-1 → DEMO-2 cutover) | 7/27 |
| D2.5 | GAP-006 (Marzip 完整) | 7/31 |
| D2.8 | GAP-019 (Protobuf 评估) | 7/31 |
| D3.4 | GAP-012 (CCS 中文 fallback) + Phase 3 完整化 | 8/31 |
| D3.8 | GAP-011 (Marzip 规范) | 8/31 |
| Phase 4 | GAP-007 (单进程 OOM) + GAP-008 (50Hz 撑量) + GAP-009 (S-57) + GAP-010 (lifecycle race) + GAP-013 (Humble EOL) + GAP-014 (4 屏命名 long-term) | 9–12月 |

---

## 14. 修订记录

| 版本 | 日期 | 改动 | 责任 |
|---|---|---|---|
| v1.0 | 2026-05-15 | 基线建立 + **套件 v1.0 完整化**。整合 V&V Plan v0.1 §3-§8 + DEMO-1 spec + 35 场景库实际盘点 + ASDR/Marzip 设计 + DNV-RP-0513/CG-0264 映射。3 新 GAP（030/031/032）入完整台账，累计 32 GAP 跨 5 文档。Doc 0 README §4 屏命名 + Doc 3 §0 "起飞" → "仿真" 文字修正联动 | 套件维护者 |
| v1.0.1 | 2026-05-15 | **D1.3.1 仿真器鉴定报告 v0.1 完成**。新增 §11.6 交付状态表；GAP-005 CLOSED（self_check 真实探针）；GAP-032 IN PROGRESS；7 交付物 + 4 自动化工具 + 25 测试通过。详见 `docs/Design/SIL/D1.3.1-simulator-qualification/` | 技术负责人 |
| v1.0.2 | 2026-05-18 | **Baseline 只读保护对齐（Doc 2 v1.0.1 + Doc 3 v1.0.1 联动）**。§3.3 新增 T-BL-01~04 测试项；§13.2 Doc 3 GAP 计数 8→9（GAP-NEW-001）；§13.4 D1.3b.3 行增 GAP-NEW-001；总计 GAP 32→33。 | 套件维护者 |
| v1.0.3 | 2026-05-18 | **职责分离同步（Doc 3 v1.0.2 + Doc 2 v1.0.2 联动）**。新增 §5.4 Simulation-Check 6-Gate 集成测试矩阵（6 个 Gate 的测试前置条件 + 通过准则 + CI 集成锚点），对接 Doc 2 §2.6 后端探针规格与 Doc 3 §7.3 前端 UX，补齐端到端验收路径。 | 套件维护者 |

---

*Doc 4 场景联调 v1.0 · 2026-05-15 · 套件 v1.0 完整化交付。下一步：用户评审 → v1.1 闭口（DEMO-1 6/15 后批量回填差距）。*
