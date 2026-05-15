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

## 2. YAML Schema v1.0

### 2.1 现状：两套并存

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

### 3.3 PR Fast Gate（Imazu-22 强制基线）

**每个 PR**：CI 跑全 22 IMAZU 场景：

- 22/22 PASS（V&V Plan v0.1 X1.5）
- CPA min ≥ 200 m ratio ≥ 95%
- COLREGs classification ≥ 95%

`imazu22_v1.0.yaml` 文件夹 freeze 为 SHA256 hash 化 manifest（决策记录 §6 + D1.3b.1 范围）。

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

### 5.1 验收范围（spec 2026-05-14-sil-demo1-head-on-design.md）

**已实现**：在本地单机环境跑 4 屏 visual demo，Imazu-01 Rule 14 Head-On 场景。

```
Browser (React 18)
  ├── RTK Query → REST  → :8000  demo_server.py  (FastAPI)
  └── useFoxgloveLive → WS    → :8765  demo_ws_server.py (websockets)
                                  └── analytical Head-On trajectory
                                        10 Hz broadcast, 700s × 2 vessels
```

**Done 判据**（spec §1.1）：

| 判据 | 可验证条件 | 状态 |
|---|---|---|
| 两船可见 | Builder 地图显示 OS（红三角，63.000°N/5.000°E）+ TS（蓝三角，63.117°N/5.000°E）| ✅ acad427 |
| 动画运行 | Bridge 地图显示两船移动，OS 在 T≈150s 右转约 35° | ✅ |
| 4-screen 流 | Builder → [Run→] → Preflight（5 checks PASS + 3-2-1）→ Bridge（live HUD）→ [Stop] → Report（KPI 数据，非硬编码）| ✅ partial — Report KPI 仍 stub GAP-021/027 |
| 右侧栏可用 | Builder 右侧栏默认折叠（48px），展开后 4 Tab 高保真字段 | ✅ commit bdde4ce |
| 无外部依赖 | `pip install fastapi uvicorn websockets` + 现有 npm 包 | ✅ |

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

## 9. ASDR 证据链

### 9.1 ASDR 数据流（架构报告 §11 + Doc 2 §11.3）

```
kernel M8 ──────► /l3/asdr_record (10 Hz) ──────────► rosbag2 → MCAP
                                                       (full record)

kernel M8/scoring ─► /sil/asdr_event (event) ──────► telemetry WS → FE Ledger
                                                       (human-readable filtered)

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
| Doc 3 | GAP-022 ~ GAP-029 | 8 |
| Doc 4（本文档新增）| GAP-030 ~ GAP-032 | 3 |
| **跨套件总计** | | **32** |

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
| D1.3b.3 | GAP-015 (WS 端口) + GAP-016 (Executor) + GAP-018 (LifecycleNode 升级) + GAP-023/024/025 (Preflight 重设计) + GAP-026 (foxglove client) + GAP-028 (OpenBridge ver) + GAP-029 (4 屏文件重命名) | 6/15 |
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

---

*Doc 4 场景联调 v1.0 · 2026-05-15 · 套件 v1.0 完整化交付。下一步：用户评审 → v1.1 闭口（DEMO-1 6/15 后批量回填差距）。*
