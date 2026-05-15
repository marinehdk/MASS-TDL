# SIL 后端设计 · v1.0 统一基线

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-UNIFIED-002 |
| 版本 | v1.0 |
| 日期 | 2026-05-15 |
| 状态 | 设计基线（Doc 1 架构通过后产出）|
| 套件 | Doc 0 README / Doc 1 架构 / **Doc 2 后端** / Doc 3 前端（pending）/ Doc 4 场景联调（pending）|
| 上游 | Doc 1 §3 系统拓扑 + §7 消息契约 + §8 生命周期 |
| 范围 | sil_orchestrator FastAPI + sim_workbench 10 ROS2 节点 + FMI 2.0 桥 + l3_tdl_kernel 集成 + 容器构建 + 启动序列 + 错误处理 |
| 命名 | 4 屏统一英文：Simulation-Scenario / Simulation-Check / Simulation-Monitor / Simulation-Evaluator |

---

## 0. 一句话定位

把"YAML 场景文件 → 真实算法链路 → 50 Hz 闭环遥测 → MCAP+Arrow+Marzip 证据"这条链路在后端落实成 **1 个 FastAPI 控制面 + 1 个 LifecycleNode 编排器 + 9 个仿真业务节点 + 8 个 L3 TDL 内核节点 + 1 个 FMI 桥**，全部跑在 ROS2 Humble + Ubuntu 22.04 + Docker/OrbStack 之上，对前端只暴露 REST + WebSocket 两个面，对 L3 内核只暴露 `l3_external_msgs/` DDS 边界。

---

## 1. 后端三层架构

```
┌──── Layer 1 · Control Plane (Host OS / Container 1 · python:3.11) ─────────┐
│  sil_orchestrator (FastAPI :8000 + rclpy spin)                              │
│  - REST /api/v1/{health, lifecycle, scoring, scenarios, selfcheck, export}  │
│  - WebSocket /ws (telemetry_bridge :8765 in current code, 见 GAP-015)       │
│  - 持久化：scenarios/ + runs/ + exports/ 卷                                  │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ▲   ▲
                            REST    │   │ rclpy service / sub
                                    │   │
                                    │   ▼
┌──── Layer 2 · Simulation Component Container (ros:humble + colcon, 2) ─────┐
│  scenario_lifecycle_mgr (LifecycleNode mgr)                                 │
│   ├── scenario_authoring_node       (YAML + AIS 5-stage 管线)               │
│   ├── self_check_node               (6-gate 自检 sequencer)                  │
│   ├── ship_dynamics_node            (FCB 4-DOF MMG, RK4 dt=0.02s)           │
│   ├── env_disturbance_node          (Gauss-Markov 风/流)                     │
│   ├── target_vessel_node × N        (ais_replay / ncdm / intelligent)        │
│   ├── sensor_mock_node              (AIS Class A/B + radar + clutter)        │
│   ├── tracker_mock_node             (God / KF)                               │
│   ├── fault_injection_node          (注入故障，③ ⚠ 按钮入口)                  │
│   ├── scoring_node                  (6 维 Hagen 2022 评分)                   │
│   └── rosbag2_recorder              (MCAP 旁路进程)                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    ▲   ▲
                                    │   │ DDS (l3_external_msgs/)
                                    │   ▼
┌──── Layer 3 · L3 TDL Kernel (ros:humble + colcon, 容器 3 ~ N) ──────────────┐
│  M1 ODD / M2 World Model / M3 Mission / M4 Behavior Arbiter /               │
│  M5 Tactical Planner / M6 COLREGs Reasoner / M8 HMI Bridge                  │
│                                                                              │
│  ── 独立进程 ──                                                              │
│  M7 Safety Supervisor (path-isolated process, 不进 component_container)     │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼ DDS l3_external_msgs/AvoidancePlan
              ┌─────── L4 stub (内置在 sim_workbench 闭环侧) ───────┐
              │  接收 AvoidancePlan → 转 actuator cmd → ship_dynamics │
              └────────────────────────────────────────────────────┘

┌──── Optional Layer 4 · FMI Bridge (D1.3c, container 4) ────────────────────┐
│  fmi_bridge (dds-fmu + libcosim::async_slave)                               │
│   └── ship_dynamics.fmu (Phase 2+) / target_policy.fmu (Phase 4 RL)         │
└─────────────────────────────────────────────────────────────────────────────┘
```

ROS2 Humble + Ubuntu 22.04 + PREEMPT_RT（决策记录 §3 锁定 🟢 [E9][E10]）。

---

## 2. sil_orchestrator FastAPI 控制面

### 2.1 实际代码结构（commit 73cdf23）

`src/sil_orchestrator/` Python 包，9 个模块共 693 行：

| 文件 | 行数 | 现状 | 职责 |
|---|---|---|---|
| `main.py` | 160 | ✅ | FastAPI app + CORS + lifecycle endpoints + rclpy spin thread + static `/exports` 挂载 |
| `config.py` | 31 | ✅ | 3-step 路径回退（env var → `/var/sil/` → 项目根）|
| `lifecycle_bridge.py` | 95 | ✅ | `LifecycleBridge(Node)` rclpy service client → `/scenario_lifecycle_mgr/change_state` |
| `telemetry_bridge.py` | 128 | ✅ | `TelemetryBridge(Node)` + WebSocket server :8765 + 4 topic subscription |
| `scenario_routes.py` | 45 | ✅ | `/api/v1/scenarios` CRUD |
| `scenario_store.py` | 121 | ✅ | File-backed YAML store + 递归扫描 + encounter type 推断（HO/CR/OT/MS）|
| `selfcheck_routes.py` | 44 | ⏳ stub | 硬编码 5 项"all PASS"；GAP-005 |
| `export_routes.py` | 69 | ⏳ partial | 后台 task 构建 Marzip 容器；scenario.yaml + sha256 + scoring.json 已就位，MCAP/Arrow 未集成 |
| `__init__.py` | 0 | ✅ | 包标记 |

### 2.2 启动流程（main.py）

```python
# 关键序列：
rclpy.init(args=None)                              # ROS2 域初始化
app = FastAPI(title="SIL Orchestrator", version="0.1.0")
app.add_middleware(CORSMiddleware, allow_origins=["*"], ...)
bridge = LifecycleBridge()                         # rclpy Node 实例
threading.Thread(target=_spin_bridge, daemon=True).start()
threading.Thread(target=start_telemetry_bridge, daemon=True).start()
_store = ScenarioStore()
# routers
app.include_router(selfcheck_router)               # /api/v1/selfcheck/*
app.include_router(export_router)                  # /api/v1/export/*
app.include_router(scenario_router)                # /api/v1/scenarios/*
# static
app.mount("/exports", StaticFiles(...))
```

**FastAPI + rclpy bridging 模式**（[W37] FastAPI threading docs + [W40] CSDN rclpy callback timing benchmark 🟡）：

现状：
- 主进程：uvicorn worker（asyncio event loop）跑 FastAPI
- 后台线程 1：`SingleThreadedExecutor` spin `LifecycleBridge` 节点
- 后台线程 2：`start_telemetry_bridge` 内启用另一 SingleThreadedExecutor 跑 `TelemetryBridge`，同时启动 asyncio WebSocket server
- service call：FastAPI handler 用 `await asyncio.sleep(0.1)` 轮询 ROS2 future（见 lifecycle_bridge.py:60-61）

**50 Hz latency 实测预算**（subagent 2026-05-15 调研 🟡）：

| 模型 | p50 latency | p99 latency | 适用 |
|---|---|---|---|
| SingleThreadedExecutor | ~15–25 ms | **> 50 ms** | ❌ 50 Hz 不可接受 |
| MultiThreadedExecutor (N=4) + ReentrantCallbackGroup | ~8–12 ms | ~18–22 ms | ✅ 50 Hz 主推 |
| 独立进程 + IPC | ~5–8 ms | ~12 ms | ✅ 最隔离 |

**GAP-016**（新增）：两个独立 SingleThreadedExecutor → 50 Hz 下 p99 > 50 ms 不可接受。修复路径 — 单个 `MultiThreadedExecutor(num_threads=4)` + `ReentrantCallbackGroup` 共享给 LifecycleBridge + TelemetryBridge，降低 GIL 竞争（[W40] CSDN 实测 + [W41] rclpy concurrent.futures ThreadPoolExecutor 源 🟡）。须项目级 prototype 在目标硬件 profiling（Intel/ARM 间 ~20% latency 方差）。

### 2.3 REST API 完整清单

完整 endpoints（按 `_routes.py` + `main.py` 现状）：

| Method | Path | Handler | 响应 schema | 屏幕 | 状态 |
|---|---|---|---|---|---|
| GET | `/api/v1/health` | `main.health()` | `{status: "ok"}` | 全 | ✅ |
| GET | `/api/v1/lifecycle/status` | `main.lifecycle_status()` | `{current_state, scenario_id, run_id}` | 全 | ✅ |
| POST | `/api/v1/lifecycle/configure` | `bridge.configure(scenario_id)` | `{success, error}` | ② | ✅ rclpy service call |
| POST | `/api/v1/lifecycle/activate` | `bridge.activate()` + `_seed_run_dir()` | `{success, error, run_id}` | ② → ③ | ✅ |
| POST | `/api/v1/lifecycle/deactivate` | `bridge.deactivate()` | `{success, error, run_id}` | ③ → ④ | ✅ |
| POST | `/api/v1/lifecycle/cleanup` | `bridge.cleanup()` (含 ACTIVE 自动 deactivate) | `{success, error}` | ④ → ① | ✅ |
| GET | `/api/v1/scoring/last_run` | reads `runs/{id}/scoring.json` | `{run_id, kpis, rule_chain, verdict}` | ④ | ⏳ stub（_seed_run_dir 写假值）|
| GET | `/api/v1/scenarios` | `store.list()` | `[{id, name, encounter_type, folder}]` | ① | ✅ 递归扫描 + encounter 推断 |
| GET | `/api/v1/scenarios/{id}` | `store.get(id)` | `{yaml_content, hash}` | ① | ✅ SHA256 |
| POST | `/api/v1/scenarios` | `store.create(yaml_content)` | `{scenario_id, hash}` | ① | ✅ |
| PUT | `/api/v1/scenarios/{id}` | `store.update(id, yaml_content)` | `{hash}` | ① | ✅ |
| DELETE | `/api/v1/scenarios/{id}` | `store.delete(id)` | `{deleted: true}` | ① | ✅ |
| POST | `/api/v1/scenarios/validate` | `store.validate(yaml_content)` | `{valid, errors}` | ① | ⏳ stub（仅查空）|
| POST | `/api/v1/selfcheck/probe` | hardcoded 5 checks | `{all_clear, items: [...]}` | ② | ⏳ GAP-005 |
| GET | `/api/v1/selfcheck/status` | hardcoded ModulePulse | `{modulePulses: [M1..M8]}` | ②③ | ⏳ |
| POST | `/api/v1/export/marzip` | `_build_marzip(run_id)` 后台 | `{status: "processing"}` | ④ | ⏳ partial |
| GET | `/api/v1/export/status/{id}` | `_export_status[id]` | `{status, download_url?}` | ④ | ⏳ |
| Static | `/exports/{run_id}_evidence.marzip` | StaticFiles | binary | ④ | ✅ |

### 2.4 设计缺口（按现状）

- **GAP-005** selfcheck_routes 仅 stub — 须实施 6-gate sequencer（详 Doc 3 § 仿真检查屏重设计）
- **GAP-006** export_routes Marzip 仅 manifest + yaml + scoring.json；缺 MCAP + Arrow + asdr_events.jsonl + verdict.json
- **GAP-016** 双 SingleThreadedExecutor → 应改 MultiThreadedExecutor + ReentrantCallbackGroup
- **GAP-017** scenario.validate 仅查空字符串；须接入 maritime-schema JSON Schema 校验

### 2.5 路径解析

`config.py` 三步回退（生产 + 开发同代码兼容）：

```
1. 显式 env：SIL_SCENARIO_DIR / SIL_RUN_DIR / SIL_EXPORT_DIR
2. Docker 路径：/var/sil/{scenarios,runs,exports}（容器内挂载）
3. 项目根路径：./scenarios + ./runs + ./exports（macOS 本地开发）
```

容器内 `docker-compose.yml` 卷映射：

```yaml
volumes:
  - ./scenarios:/var/sil/scenarios
  - ./runs:/var/sil/runs
  - ./exports:/var/sil/exports
```

ROS_DOMAIN_ID 由 `config.py:31` 读取（默认 0），容器 env 注入。

---

## 3. sim_workbench 10 ROS2 节点

### 3.1 colcon 包族（commit 73cdf23）

`src/sim_workbench/` 当前 11 个 colcon 包 + 4 个保留/降级包：

| Package | 类型 | 状态 | 节点入口 |
|---|---|---|---|
| `sil_lifecycle` | ament_python | ✅ Python FSM stub，**非 rclpy Node** | `sil_lifecycle/lifecycle_mgr.py:main()` 仅打印 |
| `sil_nodes/ship_dynamics` | ament_python | ⏳ kinematic stub | `ship_dynamics/node.py:main()` 仅打印 |
| `sil_nodes/env_disturbance` | ament_python | ⏳ stub | `env_disturbance/node.py` |
| `sil_nodes/target_vessel` | ament_python | ⏳ stub | `target_vessel/node.py` |
| `sil_nodes/sensor_mock` | ament_python | ⏳ stub | `sensor_mock/node.py` |
| `sil_nodes/tracker_mock` | ament_python | ⏳ stub | `tracker_mock/node.py` |
| `sil_nodes/fault_injection` | ament_python | ⏳ stub | `fault_injection/node.py` |
| `sil_nodes/scoring` | ament_python | ⏳ stub | `scoring/node.py` |
| `sil_nodes/scenario_authoring` | ament_python | ⏳ ROS2 wrapper stub | `scenario_authoring/node.py` |
| `scenario_authoring` (Python lib) | pure-python | ✅ AIS 5-stage 管线 + 测试套件 8 文件 | `pipeline/stage{1-5}*.py` |
| `sil_msgs` | ament_cmake | ✅ 10 .msg 已定义 | `msg/*.msg` |
| `ship_sim_interfaces` | ament_cmake | ✅ ship dynamics ↔ kernel IDL | 保留 |
| `fcb_simulator` | ament_cmake | ⏳ 4-DOF MMG 实现中 | `python/` + `src/` |
| `fmi_bridge` | ament_cmake | ⏳ D1.3c stub | `python/` + `src/` |
| `ais_bridge` | ament_python | ⏳ 已迁入 scenario_authoring | 退役 |
| `l3_external_mock_publisher` | ament_python | ⏳ Mock，DEMO-1 后退役 | GAP-002 |
| `sil_mock_publisher` | ament_python | ⏳ Mock，DEMO-1 后退役 | GAP-002 |

### 3.2 当前节点 stub vs 目标行为

**统一 stub 模式**（如 `ship_dynamics/node.py`）：

```python
class ShipDynamicsNode:
    def __init__(self, hull_class="SEMI_PLANING", dt=0.02): ...
    def step(self, rudder_angle=0.0, throttle=0.5) -> dict: ...
def main(): print("ShipDynamicsNode ready")
```

⏳ 现状：纯 Python 数据类，**未继承 `rclpy.Node`，无 publisher/subscription/timer，main() 仅打印**。

🎯 目标：每个节点须是 `rclpy.lifecycle.LifecycleNode`（[W12] ROS2 design 🟢），实现 `on_configure / on_activate / on_deactivate / on_cleanup` 回调，注册 publisher (到 `sil_msgs/*` topic) + 50 Hz timer（ship_dynamics）/ 10 Hz timer（sensor_mock 含 radar）/ 0.1 Hz timer（sensor_mock AIS）等。

**GAP-018**（新增，覆盖原 GAP-002）：sim_workbench 全部 9 业务节点尚未升级为 LifecycleNode；DEMO-1 6/15 前必须完成。`l3_external_mock_publisher` + `sil_mock_publisher` 作为 DEMO-1 临时方案存在直至替换完成。

### 3.3 10 节点 + 1 mgr 完整责任表

| # | 节点 | colcon 包 | Lifecycle 类型 | tick | publishes | subscribes | services |
|---|---|---|---|---|---|---|---|
| 0 | `scenario_lifecycle_mgr` | sil_lifecycle | LifecycleNode + 业务 mgr | — | `/sim_clock` (1ms) `/sil/lifecycle_status` (1Hz) | — | `/scenario_lifecycle_mgr/{change_state, get_state}` (Humble 标准) + `/sim_clock/set_rate` |
| 1 | `scenario_authoring_node` | scenario_authoring | std Node | event | `/sil/scenario_loaded` | — | `/scenario_authoring/{list,get,validate,create}` |
| 2 | `self_check_node` | sil_lifecycle | LifecycleNode | 1Hz | `/sil/selfcheck_status` `/sil/module_pulse_aggregate` | `/health/*` (8 个 L3 模块) | `/self_check/probe` |
| 3 | `ship_dynamics_node` | sil_nodes/ship_dynamics | LifecycleNode | 50Hz | `sil_msgs/OwnShipState` → `/sil/own_ship_state` | `ship_sim_interfaces/ActuatorCmd` → `/sil/actuator_cmd` (来自 L4 stub) | — |
| 4 | `env_disturbance_node` | sil_nodes/env_disturbance | LifecycleNode | 1Hz | `sil_msgs/EnvironmentState` → `/sil/environment` | — | — |
| 5 | `target_vessel_node` | sil_nodes/target_vessel | LifecycleNode (multi-spawn) | 10Hz | `sil_msgs/TargetVesselState` → `/sil/target_vessel_state` | — | — |
| 6 | `sensor_mock_node` | sil_nodes/sensor_mock | LifecycleNode | 5Hz radar / 0.1Hz AIS | `sil_msgs/RadarMeasurement` `sil_msgs/AISMessage` | own + target state | — |
| 7 | `tracker_mock_node` | sil_nodes/tracker_mock | LifecycleNode | 10Hz | `l3_external_msgs/TrackedTargetArray` → `/sil/tracked_targets` | radar + AIS | — |
| 8 | `fault_injection_node` | sil_nodes/fault_injection | std Node | event | `/sil/fault/*` 多个 fault topic | — | `/fault_inject/trigger` |
| 9 | `scoring_node` | sil_nodes/scoring | LifecycleNode | 1Hz | `sil_msgs/ScoringRow` → `/sil/scoring` | own + target + ASDR | — |
| 10 | `rosbag2_recorder` | rosbag2 官方 | 独立进程 | event | — | 全 `/sil/*` + `/l3/*` | — |

**11 个 active topic**（DEMO-1 阶段）：`/sim_clock` `/sil/{own_ship_state, target_vessel_state, radar_meas, ais_msg, environment, lifecycle_status, module_pulse, scoring, asdr_event}` `/sil/tracked_targets` (`l3_external_msgs/TrackedTargetArray`)。

### 3.4 ROS2 .msg IDL（sil_msgs，✅ 已定义）

10 个 .msg 文件（CMakeLists.txt:18-29 列出）：

```
sil_msgs/msg/
├── OwnShipState.msg
├── TargetVesselState.msg
├── RadarMeasurement.msg
├── AISMessage.msg
├── EnvironmentState.msg
├── FaultEvent.msg
├── ModulePulse.msg
├── ScoringRow.msg
├── ASDREvent.msg
└── LifecycleStatus.msg
```

实际字段（telemetry_bridge.py:14-99 表明实际使用的字段）：

| Msg | 字段（已落地）|
|---|---|
| `OwnShipState` | stamp, lat, lon, heading, sog, cog, rot, u, v, r, rudder_angle, throttle |
| `TargetVesselState` | mmsi, stamp, lat, lon, heading, sog, cog, rot, ship_type, mode |
| `ASDREvent` | stamp, event_type, rule_ref, decision_id, verdict, payload_json |
| `ModulePulse` | module_id, state(1/2/3), latency_ms, message_drops |

### 3.5 Protobuf 主 IDL（设计 vs 实现冲突）

**设计**（Doc 1 §7.1 + 2026-05-12 架构 §4）：sim_workbench 内部 + foxglove_bridge + 前端用 **Protobuf**，proto2ros 反向生成 ROS2 stubs。

**现状**：
- `src/l3_tdl_kernel/sil_proto/` 目录存在但**无 .proto 文件**（grep 返回 0）
- 全栈实际使用 ROS2 .msg（`sil_msgs/`）+ JSON over WebSocket（`telemetry_bridge` 手工序列化）
- buf CI gate 未配置

**GAP-019**（新增，覆盖原 GAP-004）：Protobuf IDL 主轨道未启动。决策选项：
- **A**（推荐）：v1.0 范围内**接受 ROS2 .msg + JSON 桥接** — DEMO-1 通过后 Phase 2 评估是否切 Protobuf。理由：sim_msgs 已有 10 个 .msg 落地，telemetry_bridge 序列化稳定，强切 Protobuf 当前阶段成本高 ROI 低。
- **B**（保留）：按 2026-05-12 设计推 Protobuf，付出 ~4 周工时换"未来多语言绑定 + buf breaking gate"收益。

v1.0 暂取 **选项 A**，但保留 `sil_proto/` 包占位 + 在 Doc 4 §10 evidence 章节复评。

---

## 4. scenario_authoring 管线（已有完整实现）

### 4.1 5 阶段管线

`src/sim_workbench/scenario_authoring/scenario_authoring/pipeline/` 已有完整 Python 实现 + 8 个测试文件：

| Stage | 文件 | 职责 |
|---|---|---|
| 1 | `stage1_group.py` + `test_stage1_group.py` | AIS DB 接入（PostGIS/CSV）+ MMSI 分组 + 间隙拆段（>5min）|
| 2 | `stage2_resample.py` | Δt=0.5s 重采样 + NE 线性插值 + COG 圆形插值 |
| 3 | `stage3_smooth.py` | Savitzky-Golay / Kalman 平滑 |
| 4 | `stage4_filter.py` | bbox + 时间窗 → encounter 提取（DCPA < 500m 阈值，TCPA 时间窗）|
| 5 | `stage5_export.py` | COLREG 几何分类（Head-on/Crossing/Overtaking）+ maritime-schema YAML 导出（目标） / 当前 NTNU schema 输出（现状 GAP-003）|

测试套件（`scenario_authoring/test/`）：
- `test_stage{1-5}_*.py`（5 个 stage 独立）
- `test_ais_source.py`（数据源）
- `test_encounter_extractor.py`（encounter 提取）
- `test_target_modes.py`（ais_replay / ncdm / intelligent 三模式）
- `test_ais_replay_node.py`（ROS2 节点集成）

### 4.2 数据源（决策记录 §7.3 锁定 🟢）

| Phase | 数据源 | 用途 |
|---|---|---|
| 1–3 | **Kystverket**（挪威）+ **NOAA MarineCadastre**（美国）开放数据 | 核心验证链路 + COLAV 逻辑 |
| 4+ | 商业 / 合作中国海域 AIS | 若 CCS 要求 |

下载脚本就位：`scripts/download_kystverket.py` + `scripts/download_noaa.py`（commit 73cdf23）。

### 4.3 maritime-schema 适配（GAP-003）

**现状**：`scenarios/head_on.yaml` 用 NTNU colav-simulator schema（csog_state / waypoints / los / kf / flsc，决策记录 §10 模板）。

**目标**：迁移到 `dnv-opensource/maritime-schema` TrafficSituation + `metadata.*` 扩展节（决策 §5 + §10 完整 YAML 模板 🟢 [E1][E8][E13]）。

**修复路径**：Doc 4 §2 schema 章节锁定字段映射 + `stage5_export.py` 输出格式切换。

---

## 5. ship_dynamics_node FCB 4-DOF MMG（D1.3a 范围）

### 5.1 现状（GAP-018）

`sil_nodes/ship_dynamics/ship_dynamics/node.py`（23 行）— **kinematic-only stub**：

```python
class ShipDynamicsNode:
    def step(self, rudder_angle=0.0, throttle=0.5) -> dict:
        # Phase 1: kinematic-only model (no hydrodynamics yet)
        u = self._state["u"] * throttle
        self._state["x"] += u * math.cos(self._state["psi"]) * self.dt
        # ...
        self._state["r"] += rudder_angle * 0.01  # simplified turning
```

仅做 SOG-COG 运动学积分 + rudder 一阶简化转艏。

### 5.2 目标：FCB 4-DOF MMG（Yasukawa 2015）

[W38] Yasukawa & Yoshimura *Introduction of MMG standard method for ship maneuvering predictions*, J. Marine Sci. Tech. 20(1):37–52 (2015), **DOI: 10.1007/s00773-015-0299-0** — A 🟢（subagent 调研 2026-05-15 确认）。100+ 引用，canonical 4-DOF X/Y/N + roll φ 形式。参数集为通用框架，**45m FCB 专属系数须项目级别校准**（CFD + 水池试验 + ITTC/SIMMAN 类船型基线）。

状态向量 $\eta = [x, y, \psi, \phi]^T$ + $\nu = [u, v, r, p]^T$（4-DOF：surge / sway / yaw / roll）：

```
M_RB · ν̇ + M_A · ν̇ + C(ν)·ν + D(ν)·ν + g(η) = τ_hull + τ_prop + τ_rud + τ_env
```

- $M_{RB}$：刚体质量矩阵（含 FCB 45m PVA 系数）
- $M_A$：附加水动力质量矩阵（Yasukawa 2015 §3 + 浅水修正）
- $C(\nu)$：科氏 + 离心
- $D(\nu)$：阻尼（线性 + 二次 + 三次组合 [E5] NTNU ViknesParams）
- $g(\eta)$：恢复力（roll）
- $\tau_{prop}$：螺旋桨推力 = $f(n, J)$，FCB 双螺旋桨
- $\tau_{rud}$：舵力 = $f(\delta, U)$
- $\tau_{env}$：env_disturbance_node 注入

积分：RK4，$dt = 0.02s$（50 Hz）。

参考 colav-simulator [E5] `models.py:ViknesParams` 作为初始系数源，按 FCB 45m PVA 校准（Capability Manifest + 水动力插件，架构报告决策四锁定）。

### 5.3 内部 sim vs FMI co-sim 切换

scenario YAML 字段 `simulation_settings.dynamics_mode: internal | fmi`（Doc 4 §2）：

- `internal`：调用 `ShipDynamicsNode.step()` 内置 RK4 4-DOF MMG
- `fmi`：通过 `fmi_bridge` 调用 `ship_dynamics.fmu`（D1.3c，OSP `libcosim`）

---

## 6. fmi_bridge FMI 2.0 桥（D1.3c）

### 6.1 现状

`src/sim_workbench/fmi_bridge/` — colcon C++ + Python 包，stub 阶段。

### 6.2 目标架构

[W34] OSP `libcosim` MPL-2.0 + [W29] PyFMI + [W31] FMI4cpp 🟢：

```
ROS2 ship_dynamics_node (LifecycleNode)
    │ on each tick (50 Hz)：
    │   actuator_cmd → write_real(input_refs)
    │   doStep(t, Δt=0.02)
    │   read_real(output_refs) → publish OwnShipState
    ▼
fmi_bridge (rclcpp::Node + libcosim::async_slave)
    │ dds-fmu mediator
    │   - 单次 doStep 实测 latency 2-10 ms [R-NLM:44]
    │   - jitter buffer 缓冲网络抖动
    ▼
ship_dynamics.fmu (OSP-IS extended)
    - FMI 2.0 Co-Simulation interface [W20]
    - modelDescription.xml 含 OSP-IS 海事元数据 [W34]
    - 4-DOF MMG embedded (待 wrap nikpau/mmgdynamics [W33] 或 OSP 参考模型)
```

### 6.3 边界规则（决策 §2.4 🟢）

- **MUST 走 FMI**：ship dynamics（Phase 2+ 可选）+ RL target_policy.fmu（Phase 4 D4.6 必须）
- **绝不走 FMI**：M7 Safety Supervisor（端到端 KPI < 10 ms，dds-fmu 单次 exchange 即吃完）
- **绝不走 FMI**：M1–M6 决策路径（同样 KPI 敏感）

### 6.4 D1.3c 工时（决策 §2.4）

12–16 person-week（dds-fmu 集成 + libcosim::async_slave 自定义 + jitter buffer + Phase 1 仅 own_ship FMU 接通）。

### 6.5 选用库（决策 + W29–W31 🟢）

| 库 | Phase | 用途 |
|---|---|---|
| **PyFMI** + `fmi_adapter_ros2` | Phase 1–2 prototype | sim_workbench 内 FMU 调度，与 rclpy 集成快 |
| **fmpy** | D1.7 | 场景 replay 独立验证（pure-Python，无 C 依赖）|
| **FMI4cpp** | D2.5+ | 高性能（< 10 ms FMU latency），M5 集成 |
| **libcosim** | D1.3c → 全周期 | co-sim master，MPL-2.0 商业可用 |

---

## 7. ROS2 话题表 + QoS

### 7.1 完整话题清单（DEMO-1 范围）

| Topic | IDL | 频率 | Publisher | Subscriber(s) | QoS 配置 |
|---|---|---|---|---|---|
| `/sim_clock` | builtin_interfaces/Time | 1 kHz | scenario_lifecycle_mgr | 全部节点（`use_sim_time:=true`） | RELIABLE / TRANSIENT_LOCAL / KEEP_LAST(10) |
| `/sil/own_ship_state` | sil_msgs/OwnShipState | 50 Hz | ship_dynamics_node | sensor_mock + telemetry_bridge + kernel M2 | BEST_EFFORT / VOLATILE / KEEP_LAST(1) |
| `/sil/target_vessel_state` | sil_msgs/TargetVesselState | 10 Hz | target_vessel_node × N | sensor_mock + telemetry_bridge + kernel M2 | BEST_EFFORT / VOLATILE / KEEP_LAST(1) |
| `/sil/radar_meas` | sil_msgs/RadarMeasurement | 5 Hz | sensor_mock_node | tracker_mock + kernel M2 | BEST_EFFORT / VOLATILE / KEEP_LAST(2) |
| `/sil/ais_msg` | sil_msgs/AISMessage | 0.1 Hz | sensor_mock_node | tracker_mock + kernel M2 | RELIABLE / VOLATILE / KEEP_LAST(10) |
| `/sil/environment` | sil_msgs/EnvironmentState | 1 Hz | env_disturbance_node | ship_dynamics + kernel M1 + telemetry_bridge | RELIABLE / VOLATILE / KEEP_LAST(2) |
| `/sil/tracked_targets` | l3_external_msgs/TrackedTargetArray | 10 Hz | tracker_mock_node | kernel M2 | RELIABLE / VOLATILE / KEEP_LAST(2) |
| `/sil/actuator_cmd` | ship_sim_interfaces/ActuatorCmd | 10 Hz | L4 stub | ship_dynamics_node | RELIABLE / VOLATILE / KEEP_LAST(2) |
| `/sil/lifecycle_status` | sil_msgs/LifecycleStatus | 1 Hz | scenario_lifecycle_mgr | orchestrator + telemetry_bridge | RELIABLE / TRANSIENT_LOCAL / KEEP_LAST(5) |
| `/sil/module_pulse` | sil_msgs/ModulePulse | 10 Hz | kernel M1–M8 + self_check | telemetry_bridge | BEST_EFFORT / VOLATILE / KEEP_LAST(2) |
| `/sil/scoring` | sil_msgs/ScoringRow | 1 Hz | scoring_node | rosbag2 + scoring 后处理 | RELIABLE / TRANSIENT_LOCAL / KEEP_LAST(100) |
| `/sil/asdr_event` | sil_msgs/ASDREvent | event | kernel M8 + scoring | rosbag2 + telemetry_bridge | RELIABLE / TRANSIENT_LOCAL / KEEP_LAST(50) |
| `/sil/fault/ais_dropout` | sil_msgs/FaultEvent | event | fault_injection_node | sensor_mock | RELIABLE / VOLATILE / KEEP_LAST(10) |
| `/sil/fault/radar_spike` | sil_msgs/FaultEvent | event | fault_injection_node | sensor_mock | RELIABLE / VOLATILE / KEEP_LAST(10) |
| `/sil/fault/dist_step` | sil_msgs/FaultEvent | event | fault_injection_node | env_disturbance | RELIABLE / VOLATILE / KEEP_LAST(10) |

### 7.2 L3 内核出口话题（kernel → SIL）

| Topic | IDL | 频率 | Publisher | Subscriber | 说明 |
|---|---|---|---|---|---|
| `/l3/odd_state` | l3_msgs/ODDState | 0.1–1 Hz | kernel M1 | self_check + telemetry | ODD 模式 |
| `/l3/world_state` | l3_msgs/WorldState | 10 Hz | kernel M2 | telemetry（debug） | 内部世界视图 |
| `/l3/behavior_plan` | l3_msgs/BehaviorPlan | 1–4 Hz | kernel M4 | M5 | M4 arbiter 输出 |
| `/l3/avoidance_plan` | l3_msgs/AvoidancePlan | 1–2 Hz | kernel M5 | L4 stub + scoring + telemetry | 主决策接口（RFC-002）|
| `/l3/reactive_override_cmd` | l3_msgs/ReactiveOverrideCmd | event | kernel M5 | L4 stub | 紧急接口（RFC-002）|
| `/l3/safety_alert` | l3_msgs/SafetyAlert | event | kernel M7 | telemetry + scoring | M7 VETO 通知 |
| `/l3/checker_veto` | l3_external_msgs/CheckerVetoNotification | event | kernel M7 | M5 (回灌) + telemetry | Doer-Checker veto |
| `/l3/asdr_record` | l3_msgs/ASDRRecord | 10 Hz | kernel M8 | rosbag2 + telemetry | ASDR 完整决策日志 |
| `/l3/tor_request` | l3_msgs/ToRRequest | event | kernel M8 | telemetry（ToR Modal）| Phase 3 |
| `/l3/sat1_data` `/l3/sat2_data` `/l3/sat3_data` | l3_msgs/SATxData | 1–10 Hz | kernel M8 | telemetry | SAT 三级 |

### 7.3 QoS 配置文件（按 [W10][W11] 🟢）

`config/qos_overrides.yaml`（按 ROS2 Humble override schema）：

```yaml
/sil/own_ship_state:
  reliability: best_effort
  durability: volatile
  history: keep_last
  depth: 1

/sil/scoring:
  reliability: reliable
  durability: transient_local
  history: keep_last
  depth: 100

/sil/asdr_event:
  reliability: reliable
  durability: transient_local
  history: keep_last
  depth: 50

/l3/checker_veto:
  reliability: reliable
  durability: transient_local
  history: keep_last
  depth: 10
  deadline:
    sec: 0
    nanosec: 50000000  # 50 ms deadline for M7
```

rosbag2 自动适配订阅 QoS 与 publisher 一致 🟢 [W8]。

---

## 8. l3_tdl_kernel 集成

### 8.1 8 模块 + 1 mock publisher

`src/l3_tdl_kernel/` 当前结构（commit ace10b8 起 M4 已为完整 ROS2 节点）：

| 模块 | 包路径 | ROS2 状态 |
|---|---|---|
| M1 ODD | `m1_odd_envelope_manager/` | ✅ ROS2 节点 |
| M2 World Model | `m2_world_model/` | ✅ ROS2 节点 |
| M3 Mission Manager | `m3_mission_manager/` | ✅ ROS2 节点 |
| M4 Behavior Arbiter | `m4_behavior_arbiter/` | ✅ ROS2 节点（ace10b8 新增）|
| M5 Tactical Planner | `m5_tactical_planner/` | ✅ ROS2 节点 |
| M6 COLREGs Reasoner | `m6_colregs_reasoner/` | ✅ ROS2 节点 |
| M7 Safety Supervisor | `m7_safety_supervisor/` | ✅ ROS2 节点（**独立进程**）|
| M8 HMI Bridge | `m8_hmi_transparency_bridge/` | ✅ ROS2 节点 |
| `l3_external_mock_publisher` | `l3_external_mock_publisher/` | ⏳ DEMO-1 临时；GAP-002 |

### 8.2 进程拓扑（Doer-Checker 隔离）

[W39] IEC 61508-1/2 + [W42] Nav2 lifecycle_manager + [W43] ROS2 LifecycleNode state machine 🟡：

| 进程 | 节点 | 容器 | 通信 |
|---|---|---|---|
| **Doer process** | M1, M2, M3, M4, M5, M6, M8 | `mass-l3/ci:humble-ubuntu22.04` Container 3 | `component_container_mt` + intra-process zero-copy + DDS for sim_workbench 边界 |
| **Checker process** | M7 only | 同镜像但**独立 container** Container 4 | **独立 OS process** + 同 ROS_DOMAIN_ID；仅 DDS 通信 |
| **Mock process**（DEMO-1）| l3_external_mock_publisher | 同 Doer container（临时）| DEMO-1 后退役 |

**SIL 2 隔离硬约束**（subagent 2026-05-15 调研 🟡）：

| 隔离方式 | Diversity | SIL 2 合规 |
|---|---|---|
| 独立进程（分独立 container）| 最高 — 独立地址空间 + crash isolation + 独立 thread pool | ✅ MUST |
| 独立 DDS Domain ID（Doer=0, Checker=1）| 中 — 逻辑隔离，multicast 域分段，防全局故障传播 | ⚠️ 可选增强（牺牲 Doer-Checker 通信便利）|
| ReentrantCallbackGroup 同进程 | 低 — 共享内存 + GIL，仅"false diversity" | ❌ **不满足 IEC 61508 SIL 2 独立性要求**，仅适合性能调优 |

**当前 v1.0 决定**：选独立进程（Container 3 + 4），ROS_DOMAIN_ID 共享 0（Doer/Checker 需互通 VETO 消息）。如未来 cybersecurity（RFC-007）要求强分段，可拆 Domain ID。

Doer-Checker 隔离硬约束（架构报告 §11 + Doc 1 §11，subagent 确认 🟡）：
- 进程独立（OS-level fault domain）— 独立 container
- 代码路径独立（`m7_*` **不 import** `m1_*` ~ `m6_*` Python/C++ 模块；CI lint 强制）
- 依赖库独立子集（M7 不依赖 OR-Tools / 复杂规划库；M7 仅 Eigen + 内置 FSM）
- 数据结构共享仅通过 `l3_msgs` IDL（只读 subscribe）
- VETO 通过专用 topic `/l3/checker_veto` 单向通知（不调 service，避免阻塞）
- lifecycle_manager 协调：M7 与 M1-M6 lifecycle 转换**异步**，避免 M1-M6 启动 race 阻塞 M7

### 8.3 集成边界（l3_external_msgs/ IDL）

5 个边界 IDL（RFC-001/002/003 锁定）：

| Msg | 方向 | 频率 | 用途 |
|---|---|---|---|
| `FilteredOwnShipState` | Fusion → L3 (M1+M2) | 50 Hz | Nav Filter 输出 |
| `TimeWindow` | L3 → Fusion | event | 同步时间窗 |
| `OverrideActiveSignal` | L3 (M7) → L4 | event | Reactive override 标志 |
| `VoyageTask` | L1/L2 → L3 (M3) | event | 航次任务 |
| `CheckerVetoNotification` | L3 (M7) → L3 (M5) | event | Doer-Checker veto 回灌 |

SIL 内由 `sim_workbench` 节点扮演 Fusion + L1/L2 + L4 角色，通过这 5 个 IDL 与 kernel 通信。

---

## 9. 容器构建链

### 9.1 docker-compose.yml 现状

5 个 service，**host 网络 MANDATORY**（subagent 2026-05-15 🟢）— Linux + macOS OrbStack 上 DDS multicast discovery 仅在 `network_mode: host` 下可达；bridge 模式 multicast 限于 container subnet，跨服务节点不可见 [W44] DDS multicast RFC + [W45] Foxglove DDS discovery troubleshooting：

```yaml
services:
  sil-orchestrator:                       # 控制面
    build: docker/sil_orchestrator.Dockerfile   # ❌ jazzy → GAP-001
    ports: [8000]
    volumes: [scenarios, runs, exports]
    network_mode: host

  sil-component-container:                # 仿真节点
    build: docker/sil_nodes.Dockerfile          # ✅ humble
    volumes: [scenarios, runs]
    network_mode: host

  foxglove-bridge:                        # WS 桥
    image: mass-l3/ci:jazzy-ubuntu22.04          # ❌ → GAP-001
    command: ros2 launch foxglove_bridge foxglove_bridge_launch.xml port:=8765
    network_mode: host

  web:                                    # 前端
    build: web/Dockerfile
    ports: [5173]
    depends_on: [sil-orchestrator]

  martin-tile-server:                     # MVT tile
    image: ghcr.io/maplibre/martin:latest
    ports: [3000]
    volumes: [data/tiles]
    network_mode: host
```

### 9.2 Dockerfile 修复路径（GAP-001）

**目标**：统一 `ros:humble-ros-base` + `mass-l3/ci:humble-ubuntu22.04` 自建镜像。

`docker/sil_orchestrator.Dockerfile` 应改：

```dockerfile
FROM mass-l3/ci:humble-ubuntu22.04        # ← was jazzy
WORKDIR /opt/sil
RUN apt-get update && apt-get install -y python3-pip && \
    pip3 install --no-cache-dir \
    fastapi==0.115.4 uvicorn[standard]==0.32.0 \
    websockets==12.0 pydantic==2.9.2 \
    protobuf==5.28.2 pyyaml==6.0.2 \
    --break-system-packages

# Copy orchestrator + IDL
COPY src/sil_orchestrator /opt/sil/sil_orchestrator
COPY src/sim_workbench/sil_msgs /opt/sil/src/sil_msgs
RUN /bin/bash -c "source /opt/ros/humble/setup.bash && colcon build --base-paths /opt/sil/src"

RUN mkdir -p /var/sil/scenarios /var/sil/runs /var/sil/exports
ENV PYTHONPATH=/opt/sil
EXPOSE 8000

CMD ["/bin/bash", "-c", "source /opt/ros/humble/setup.bash && \
     source /opt/sil/install/setup.bash && \
     python3 -m uvicorn sil_orchestrator.main:app --host 0.0.0.0 --port 8000"]
```

`docker/sil_nodes.Dockerfile` 已用 `ros:humble-ros-base`，仅检查 packages-select 列表是否含全部 9 业务节点 + sil_lifecycle + sil_msgs。

`foxglove-bridge` service 改：

```yaml
foxglove-bridge:
  image: mass-l3/ci:humble-ubuntu22.04         # ← was jazzy
  command: bash -c "source /opt/ros/humble/setup.bash && \
                    ros2 launch foxglove_bridge foxglove_bridge_launch.xml port:=8765"
```

CI 镜像构建（`docker/ci.Dockerfile` 假设存在）：基于 `ros:humble-ros-base` + 预装 colcon + dev tools + ros-humble-{rclcpp-components, foxglove-bridge, rosbag2-storage-mcap}（[W6][W8] 🟢）。

### 9.3 OrbStack 启动序列

```bash
# 1. 创建 OrbStack VM（首次）
orb create sil

# 2. 启动
orb start

# 3. 构建 + 启动全栈
docker compose -f docker-compose.yml up -d --build

# 4. 验证
docker compose ps
curl http://localhost:8000/api/v1/health
curl http://localhost:5173                          # 前端
ros2 topic list                                      # 主机直接看 DDS（host network）
```

ROS_DOMAIN_ID 默认 0（config.py:31 + docker-compose env），跨 service 共享同一 DDS 域。

### 9.4 GAP-015 WebSocket 端口冲突

**问题**：`telemetry_bridge.py:112` 启动 `websockets.serve(..., "0.0.0.0", 8765)` 同时 `docker-compose.yml` `foxglove-bridge` service 也用 `port:=8765`。两者都用 host 网络，端口冲突。

**修复路径**：
- **选项 A**（推荐）：`telemetry_bridge` 退役，前端改用 foxglove_bridge 标准 WS 协议（含 Protobuf schema 协商）。orchestrator 仅留 REST 面。
- **选项 B**：换端口（如 telemetry :8766）。但 foxglove 标准 protocol 已支持 ROS2 subscription + service relay，B 路径会引入两套协议维护。

Doc 3 §8 数据通道章节锁定选 A 或 B。

---

## 10. 启动序列（完整）

```
T-10s   docker compose up
        Container 1: sil-orchestrator         (FastAPI ready, rclpy.init)
        Container 2: sil-component-container  (component_container_mt 启动)
        Container 3: foxglove-bridge          (WS :8765 listening)
        Container 4: web                      (Vite dev server :5173)
        Container 5: martin-tile-server       (MVT :3000 listening)

T-9s    sil-component-container 内:
        ros2 run sil_lifecycle lifecycle_mgr     # PHASE 1: pure-Python stub (GAP)
                                                 # PHASE 2: rclpy LifecycleNode
        ros2 component load sil_nodes/* (LifecycleNode × 8)
        all nodes UNCONFIGURED

T-8s    rosbag2 recorder 独立进程（按需）
        ros2 bag record -s mcap -o /var/sil/runs/{run_id}/bag.mcap \
            /sil/own_ship_state /sil/target_vessel_state /sil/asdr_event \
            /l3/asdr_record /sil/scoring /l3/checker_veto

T-7s    L3 kernel containers（M1-M6 + M8 Doer 容器 + M7 Checker 容器）
        ros2 run m{1..6,8}_* node
        ros2 run m7_safety_supervisor m7_node   # 独立进程

T-0     FE Screen ① Simulation-Scenario 加载
        GET /api/v1/scenarios → store.list()
        显示场景库（含 IMAZU22 + COLREGs + head_on）

T+1s    用户点 "Run →"
        POST /api/v1/lifecycle/configure { scenario_id: "head_on" }
        → bridge.configure → rclpy ChangeState(CONFIGURE)
        → scenario_lifecycle_mgr 转 INACTIVE
        → 各节点 on_configure 加载 YAML 参数

T+1.5s  FE Screen ② Simulation-Check
        POST /api/v1/selfcheck/probe → 5/6 检查
        SHA256 验证 + UTC PTP drift + ASDR ready + ENC loaded + Doer-Checker 隔离验证
        all_clear = true → 3-2-1 倒数

T+5s    POST /api/v1/lifecycle/activate
        → bridge.activate → rclpy ChangeState(ACTIVATE)
        → scenario_lifecycle_mgr ACTIVE + 50Hz tick 起
        → 各节点 on_activate 启 timer / sub / pub
        → _seed_run_dir 创建 runs/{run_id}/scenario.yaml + sha256 + scoring.json stub

T+5s+ε  FE Screen ③ Simulation-Monitor
        WS 订阅 8765：/sil/own_ship_state @ 50Hz / /sil/target_vessel_state @ 10Hz / 
                       /sil/asdr_event event / /sil/module_pulse @ 10Hz
        ENC 海图 + 双视图（船长 / God）渲染

T+end   用户点 "⏹ Stop" 或 TIMEOUT 或 FAULT_FATAL
        POST /api/v1/lifecycle/deactivate
        → bridge.deactivate → rclpy ChangeState(DEACTIVATE)
        → scenario_lifecycle_mgr INACTIVE + 50Hz tick 停
        → rosbag2 recorder 收尾 MCAP

T+end+1 FE Screen ④ Simulation-Evaluator
        GET /api/v1/scoring/last_run → 读 runs/{run_id}/scoring.json
        显示 KPI cards + Timeline + ASDR Ledger + Trajectory Replay

T+end+N 用户点 "[Export]"
        POST /api/v1/export/marzip { run_id }
        → 后台 task _build_marzip:
          1. read scenario.yaml + sha256
          2. (Phase 2) read MCAP → polars DataFrame → arrow.ipc
          3. (Phase 2) compute derived KPIs
          4. zip → exports/{run_id}_evidence.marzip
        FE 轮询 GET /api/v1/export/status/{run_id} → 完成后下载
```

---

## 11. 错误处理 + 重启策略 + ASDR

### 11.1 节点错误处理

[W12] ROS2 lifecycle 标准过渡态 🟢：

- **ERRORPROCESSING** 状态：on_configure / on_activate / on_deactivate 任一返回 FAILURE → 节点进 ERRORPROCESSING
- on_error 回调可重试或转 UNCONFIGURED
- scenario_lifecycle_mgr 监听全部节点 `/lifecycle/state` topic（TRANSIENT_LOCAL QoS）

### 11.2 容器级重启

`docker-compose.yml` 各 service：

```yaml
restart: unless-stopped       # 全部 service 默认
```

但 `sil-component-container` + L3 kernel container 不应自动重启（避免数据丢失），改 `restart: on-failure:0`。

### 11.3 ASDR 日志（IMO MASS Code 4 级模式）

- kernel M8 `l3_msgs/ASDRRecord` 10 Hz 记入 MCAP
- 关键事件 `sil_msgs/ASDREvent` 含 verdict(PASS/RISK/FAIL) 写 `asdr_events.jsonl`（人类可读）
- Marzip 容器导出（Doc 4 §10）

### 11.4 lifecycle 5-state machine 现状

`sil_lifecycle/lifecycle_mgr.py`（126 行）实现 pure-Python FSM：

```
UNCONFIGURED ─ configure(scenario_id, hash) → INACTIVE
INACTIVE     ─ activate()                   → ACTIVE
ACTIVE       ─ deactivate()                 → DEACTIVATING → INACTIVE (transient)
INACTIVE     ─ cleanup()                    → UNCONFIGURED
```

转换硬约束（lifecycle_mgr.py:67-95）：
- configure 仅在 UNCONFIGURED
- activate 仅在 INACTIVE
- deactivate 仅在 ACTIVE
- cleanup 仅在 {INACTIVE, FINALIZED}

orchestrator `LifecycleBridge.cleanup()`（main.py:124-139）做了贴心处理：发现 ACTIVE → 自动先 deactivate 再 cleanup（idempotent + tolerant of stale ACTIVE state）。

### 11.5 sim_clock + tick_hz

`lifecycle_mgr.py`：
- `tick_hz` 默认 50.0（50 Hz tick）
- `sim_rate` 默认 1.0，可改 0.5/1/2/4/10
- `sim_time` 在 ACTIVE 时按 `sim_time += (1/tick_hz) * sim_rate` 累加
- "Pause" = `set_sim_rate(0)`，节点不停 tick 但时间冻结
- "Speed × 10" = `set_sim_rate(10)`，wall 1s = sim 10s

### 11.6 单进程 OOM 降级（GAP-007）

`component_container_mt` 单进程拓扑：30 天稳定测试若发现 OOM/crash → 降级多进程拓扑（牺牲零拷贝换隔离）。

降级触发条件：
- RSS > 4 GB 持续 5 min
- callback 延迟 > 100 ms 持续 30 s
- crash > 1 次/24 h

降级路径：用 `composable_node_descriptions` 拆分到 3 个独立 container（perception / dynamics / orchestration），同 ROS_DOMAIN_ID 通信。

---

## 12. 差距台账（GAP 增量，跨 Doc 1 编号续接）

| GAP | 描述 | 现状 | 修复路径 | 责任 D-task |
|---|---|---|---|---|
| **GAP-015** | telemetry_bridge :8765 vs foxglove-bridge :8765 端口冲突 | docker-compose.yml + telemetry_bridge.py 同时占 8765 | 选 A：telemetry_bridge 退役，前端改 foxglove WS 标准协议 / 选 B：换端口 | Doc 3 §8 |
| **GAP-016** | orchestrator 两个 SingleThreadedExecutor → GIL 竞争风险 | main.py:39-45 双 Thread + 双 Executor | 改 MultiThreadedExecutor + ReentrantCallbackGroup | D1.3b.3 优化 |
| **GAP-017** | scenario.validate 仅查空字符串 | scenario_store.py:116-121 stub | 接 maritime-schema JSON Schema + cerberus 双校验 | D1.6 |
| **GAP-018** | 9 业务节点全部为 Python 数据类 stub，未继承 rclpy.LifecycleNode；lifecycle_mgr 是 pure-Python，未实现 ROS2 service 端点 | sil_nodes/*/node.py + sil_lifecycle/lifecycle_mgr.py | 逐节点升级为 LifecycleNode + 实现 on_configure/activate/deactivate/cleanup + publisher/timer | D1.3a + D1.3b.3 |
| **GAP-019** | Protobuf IDL `sil_proto/` 空 — 现行实现是 ROS2 .msg + JSON WS（与 2026-05-12 设计偏离）| 实际仅 `sil_msgs/msg/*.msg`（10 个）| v1.0 选项 A：接受 ROS2 .msg + JSON 桥；Phase 2 评估 Protobuf 收益 | Doc 4 §10 复评 |
| **GAP-020** | ship_dynamics_node kinematic-only stub，非 4-DOF MMG | node.py:10-17 简化 | 实施 Yasukawa 2015 4-DOF MMG（surge/sway/yaw/roll）+ RK4 dt=0.02s + FCB 系数 | D1.3a |
| **GAP-021** | scoring stub 写入 `_seed_run_dir`（main.py:67-83），非 scoring_node 真输出 | 硬编码 KPI {min_cpa_nm: 0.42, ...} | scoring_node 启 Arrow append → orchestrator MCAP→Arrow 后处理 | D2.4 |

跨文档汇总（Doc 1 GAP-001 ~ GAP-014 + Doc 2 GAP-015 ~ GAP-021）共 **21 GAP**。

---

## 13. 文件谱系 + 调研记录（增量）

继承 Doc 1 §13.2 [Ex] / [R-NLM] / [Wx] 引用编号。本 Doc 新增源（subagent 2026-05-15）：

- [W37] FastAPI 官方 docs (fastapi.tiangolo.com) — A 🟢
- [W38] Yasukawa & Yoshimura, *Introduction of MMG standard method for ship maneuvering predictions*, J. Marine Sci. Tech. 20(1):37–52 (2015), **DOI: 10.1007/s00773-015-0299-0** — A 🟢（100+ 引用，canonical 4-DOF X/Y/N + roll）
- [W39] IEC 61508-1/2 *Functional safety of E/E/PE safety-related systems* — A 🟡（标准文献，付费访问）
- [W40] *ROS2 callback execution timing 实测 benchmark*（CSDN 社区，2024）— B 🟡（无 peer-reviewed maritime-specific 验证；须项目级 prototype 校准）
- [W41] rclpy `MultiThreadedExecutor` + `concurrent.futures.ThreadPoolExecutor` 源码（github.com/ros2/rclpy）— A 🟢
- [W42] Nav2 `lifecycle_manager`（github.com/ros-navigation/navigation2/tree/humble/nav2_lifecycle_manager）— A 🟢（生产级 robotics ROS2 lifecycle 编排参考）
- [W43] *ROS2 LifecycleNode 状态机 教程*（design.ros2.org/articles/node_lifecycle.html）— A 🟢
- [W44] *DDS multicast RFC + RTPS DDSI v2.5* — A 🟢
- [W45] *Foxglove DDS discovery troubleshooting* (docs.foxglove.dev) — A 🟢
- [W46] *rosbag2_storage_mcap v0.15.16* (ROS Index Humble snapshot 2026-03-09) + zstd message-mode 配置 — A 🟢
- [W47] *MCAP spec*（mcap.dev/spec）— A 🟢
- [W48] *IACS UR E26/E27 cybersecurity*（host mode + OT vessel networks）— A 🟡

**置信度分布**：3× 🟢 High + 4× 🟡 Medium。🟡 项均集中在"实测性能 / 工程实施模式"维度，规范文献（A 级）覆盖完整，仅需项目级 prototype 在目标硬件上 profiling 闭环。

**确认决定**（与 W37-W48 调研一致）：

1. ✅ Yasukawa 2015 MMG 4-DOF 锁定（DOI 已修正），FCB 系数 D1.3a 期间项目级标定
2. ✅ Doer-Checker 选"独立进程 + 同 DDS Domain ID 0" 落地（独立 container 拆分 Container 3 + 4）
3. ✅ FastAPI + rclpy 模式确认改 MultiThreadedExecutor(4) + ReentrantCallbackGroup（GAP-016）
4. ✅ lifecycle_manager 按 Nav2 模式实施（参 [W42] 源代码）
5. ✅ MCAP zstd message-mode level 10（D1.3.1 鉴定要求）
6. ✅ host network 强制；ROS_DOMAIN_ID 0 主用（M7 Cybersec 强分段可拆）
7. ✅ rclpy 在 50 Hz 下 MultiThreaded(4) p99 ~20 ms 可行；目标硬件 profiling 待 D1.3a/b 落地

---

## 14. 修订记录

| 版本 | 日期 | 改动 | 责任 |
|---|---|---|---|
| v1.0 | 2026-05-15 | 基线建立。整合 Doc 1 §3/§7/§8 + 实际代码读（sil_orchestrator 9 文件 693 行 + sim_workbench 11 colcon 包 + sil_msgs 10 IDL）+ 决策记录 §2/§3/§5/§7 + subagent web 调研 [W37–W48]（Yasukawa MMG DOI 修正 + MultiThreadedExecutor 50 Hz 实测 + Doer-Checker IEC 61508 独立进程 + Nav2 lifecycle_manager + MCAP 0.15.16 + DDS multicast host mode）。21 GAP 入完整台账。 | 套件维护者 |

---

*Doc 2 后端 v1.0 · 2026-05-15 · 与 Doc 1 联动交付。Doc 3 前端将在用户评审通过后启动。*
