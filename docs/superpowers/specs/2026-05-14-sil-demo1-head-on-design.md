# SIL Demo-1: Head-On 4-Screen Full Stack

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-SPEC-DEMO1 |
| 版本 | v1.0 |
| 日期 | 2026-05-14 |
| 状态 | Approved for implementation |
| D-task | D1.3b.3 |
| Demo milestone | DEMO-1 (6/15 Skeleton Live) |

---

## 1. 范围与目标

本文档规定 **SIL Demo-1** 的实现边界：在本地单机环境中展示一条从 **场景构建器 → 预检 → Captain HMI → 运行报告** 的完整 4-screen 操作流，运行 Imazu-01（Rule 14 Head-On）场景。

### 1.1 Done 判据

| 判据 | 可验证条件 |
|---|---|
| 两船可见 | Builder 地图显示 OS（红三角，63.000°N/5.000°E）+ TS（蓝三角，63.117°N/5.000°E） |
| 动画运行 | Bridge 地图显示两船移动，OS 在 T≈150s 右转约 35° |
| 4-screen 流 | Builder → [Run→] → Preflight（5 checks PASS + 3-2-1）→ Bridge（live HUD）→ [Stop] → Report（KPI 数据，非硬编码）|
| 右侧栏可用 | Builder 右侧栏默认折叠（48px），展开后 4 Tab 各自显示高保真字段 |
| 无外部依赖 | `pip install fastapi uvicorn websockets` + 现有 npm 包即可运行 |

---

## 2. 系统边界

```
Browser (React 18)
  │
  ├── RTK Query → REST  → :8000  demo_server.py  (FastAPI)
  │
  └── useFoxgloveLive → WS    → :8765  demo_ws_server.py (websockets)
                                  └── analytical Head-On trajectory
                                        10 Hz broadcast, 700s × 2 vessels
```

**不在本 spec 范围**：
- ROS2 / foxglove_bridge 集成
- colav-simulator 物理引擎
- 真实 ENC tile 服务（地图 fallback 到 OSM 或本地 stub tile）
- 任何认证、持久化存储、多用户并发

---

## 3. 后端

### 3.1 目录结构

```
tools/demo/
  demo_server.py        FastAPI REST :8000
  demo_ws_server.py     WebSocket :8765
  trajectory.py         Head-On 解析轨迹计算
  run_demo.sh           一键启动两个进程
```

### 3.2 REST API（demo_server.py）

FastAPI，CORS `allow_origins=["*"]`，端口 8000。

所有路径均以 `/api/v1` 为前缀（与 `web/vite.config.ts` proxy 配置对齐）。

#### 3.2.1 场景管理

| Method | Path | Response | 说明 |
|--------|------|----------|------|
| GET | `/api/v1/scenarios` | `ScenarioCard[]` | 返回单条 Imazu-01 卡片 |
| GET | `/api/v1/scenarios/{id}` | `ScenarioDetail` | 返回完整 YAML 内容 |
| POST | `/api/v1/scenarios` | `ScenarioCard` | stub: echo back with generated id |
| PUT | `/api/v1/scenarios/{id}` | `ScenarioCard` | stub: accept & return |
| DELETE | `/api/v1/scenarios/{id}` | `204` | stub: no-op |
| POST | `/api/v1/scenarios/validate` | `{valid: bool, errors: str[]}` | stub: always valid |

`ScenarioCard`:
```json
{
  "id": "imazu-01-ho-v1.0",
  "name": "Imazu-01 Head-on (Rule 14)",
  "encounter_type": "head_on",
  "enc_region": "trondelag",
  "duration_s": 700,
  "status": "ready"
}
```

`ScenarioDetail` 包含 `card` 字段 + `yaml_content`（原始 YAML 字符串，从 `scenarios/imazu22/imazu-01-ho-v1.0.yaml` 读取）。

#### 3.2.2 生命周期

| Method | Path | Request | Response | 说明 |
|--------|------|---------|----------|------|
| POST | `/api/v1/lifecycle/configure` | `{scenario_id}` | `{run_id, state: "inactive"}` | 生成 UUID run_id，记录 scenario_id |
| POST | `/api/v1/lifecycle/activate` | `{run_id}` | `{run_id, state: "active"}` | 标记 WS server 开始广播 |
| POST | `/api/v1/lifecycle/deactivate` | `{run_id}` | `{run_id, state: "finalized"}` | 标记停止广播，写入 scoring 缓存 |
| POST | `/api/v1/lifecycle/cleanup` | `{run_id}` | `{run_id, state: "unconfigured"}` | 重置状态 |

激活信号通过共享内存变量（`asyncio.Event`）传递给 WS 进程；两进程间不使用 IPC，改为 **WS 服务器轮询** REST 服务器的 `/api/v1/lifecycle/status` 端点（1Hz），或者将两个服务器合并到同一个 `asyncio` 事件循环（推荐，见 §3.3）。

#### 3.2.3 预检

| Method | Path | Response |
|--------|------|----------|
| POST | `/api/v1/selfcheck/probe` | `SelfCheckResult[]` |

`SelfCheckResult[]`（5 条，延迟 100ms/条）：
```json
[
  {"check_id": "enc_load",    "status": "PASS", "detail": "ENC trondelag loaded, 1 chart"},
  {"check_id": "asdr_start",  "status": "PASS", "detail": "ASDR ledger initialized"},
  {"check_id": "utc_sync",    "status": "PASS", "detail": "UTC offset 0ms"},
  {"check_id": "module_health","status": "PASS", "detail": "M1-M8 all GREEN"},
  {"check_id": "scenario_sig","status": "PASS", "detail": "SHA-256 abc123… verified"}
]
```

#### 3.2.4 评分

| Method | Path | Response |
|--------|------|----------|
| GET | `/api/v1/scoring/last_run` | `ScoringResult` |
| GET | `/api/v1/export/marzip/{run_id}` | stub 204 |

`ScoringResult`（由 `trajectory.py` 在 deactivate 时计算）：
```json
{
  "run_id": "...",
  "verdict": "PASS",
  "min_cpa_nm": 0.52,
  "colregs_compliant": true,
  "max_rud_deg": 35.0,
  "tor_time_s": null,
  "decision_p99_ms": 312,
  "faults_injected": 0,
  "scenario_sha256": "abc123...",
  "scores": {
    "safety": 0.91, "rule": 0.95, "delay": 0.88,
    "magnitude": 0.82, "phase": 0.90, "plausibility": 0.87
  }
}
```

> **保留接口**：上述所有 API 路径和 schema 对齐 `docs/Design/SIL/Design v2.0/03-07-backend-api.md`，供后续真实 FastAPI + ROS2 服务器直接替换 demo 实现，无需改动前端。

### 3.3 WS 服务器（demo_ws_server.py）

与 `demo_server.py` 合并到同一 Python 进程（单一 `asyncio` 事件循环），通过 `threading` 或 `uvicorn` 的 `lifespan` 钩子启动后台广播任务。端口 8765。

**广播协议**：JSON 帧，格式 `{"topic": "<name>", "payload": {...}}`

| topic | 频率 | payload schema |
|-------|------|----------------|
| `own_ship_state` | 10 Hz | `OwnShipState` (见 §3.3.1) |
| `target_vessel_state` | 10 Hz | `TargetVesselState` (见 §3.3.1) |
| `module_pulse` | 1 Hz × 8 | `{moduleId, state, latencyMs}` |
| `lifecycle_status` | 1 Hz | `{current_state, sim_time, sim_rate}` |
| `asdr_event` | event-driven | `{t_sim, severity, rule, decision, rationale}` |

广播仅在 `lifecycle_status.current_state == ACTIVE (3)` 时发送 `own_ship_state` + `target_vessel_state`；其余 topic 始终发送。

#### 3.3.1 Payload Schemas

**OwnShipState**（对应 `web/src/types/sil/own_ship_state.ts`）：
```json
{
  "stamp": 1715000000.0,
  "pose": {"lat": 63.000, "lon": 5.000, "heading": 0.0},
  "kinematics": {"sog": 5.14, "cog": 0.0, "rot": 0.0, "u": 5.14, "v": 0.0, "r": 0.0},
  "controlState": {"rudderAngle": 0.0, "throttle": 1.0}
}
```
单位：lat/lon 度，heading/cog/rot 弧度，sog m/s（10kn = 5.144 m/s）。

**TargetVesselState**（对应 `web/src/types/sil/target_vessel_state.ts`）：
```json
{
  "stamp": 1715000000.0,
  "mmsi": 100000001,
  "pose": {"lat": 63.117, "lon": 5.000, "heading": 3.14159},
  "kinematics": {"sog": 5.14, "cog": 3.14159, "rot": 0.0},
  "shipType": 70,
  "mode": "linear"
}
```

### 3.4 解析轨迹（trajectory.py）

Head-On Imazu-01，开阔挪威海（63°N/5°E 周围 50nm 内无陆地）：

| 时段 | OS 行为 | TS 行为 |
|------|---------|---------|
| T = 0–150s | 航向 0°（正北），SOG 10kn 直行 | 航向 180°（正南），SOG 10kn 直行 |
| T = 150–180s | COG 从 0° 线性增至 35°（右转），ROT ≈ +1.17°/s | 继续直行 |
| T = 180–420s | 航向 35°（东北），SOG 10kn | 继续直行 |
| T = 350s | CPA 发生（横向间距 ≈ 0.52nm） | — |
| T = 420–450s | COG 从 35° 线性回到 0°（左转），ROT ≈ −1.17°/s | 继续直行 |
| T = 450–700s | 恢复航向 0°，SOG 10kn | 继续直行 |

坐标转换：纬度 1° ≈ 111,320m；经度 1°（63°N）≈ 50,540m。SOG 10kn = 5.144 m/s。

在程序启动时预计算 7000 帧（700s × 10Hz），存为列表，广播时按帧索引取值。

**ASDR 事件**（5 条，固定时间点）：

| T(s) | severity | decision |
|------|----------|----------|
| 0 | INFO | SCENARIO_START |
| 120 | INFO | TARGET_DETECTED |
| 148 | WARN | COLLISION_RISK_ASSESSED – DCPA 0.08nm, TCPA 3.5min |
| 150 | ACTION | COLREG_R14_EXECUTE – turn starboard 35° |
| 355 | INFO | CPA_CLEAR – 0.52nm |

---

## 4. 前端变更

### 4.1 ScenarioBuilder.tsx — 右侧折叠栏

**当前状态**：右侧有 4 个可点击 section 标题，但无实际输入字段。

**目标状态**：右侧栏改为与左侧 domain nav rail 镜像对称的折叠栏。

#### 4.1.1 折叠栏结构

```
右侧边缘（默认 48px）：
  ┌──────────────┐
  │ [A] Encounter │  ← 竖排 tab 图标 + 短标签
  │ [B] Environment│
  │ [C] Run       │
  │ [D] Summary   │
  │               │
  │ [›] 展开按钮  │  ← 底部或顶部
  └──────────────┘

展开时（320px）：
  ┌───────────────────────────────┐
  │ [‹]  Encounter                │  ← 当前 Tab 标题 + 收起按钮
  ├───────────────────────────────┤
  │  [A] [B] [C] [D]             │  ← Tab 切换 strip
  ├───────────────────────────────┤
  │  < Tab 内容区域 >             │  ← 仅显示选中 Tab 内容
  └───────────────────────────────┘
```

样式继承：`bg-[#0a0f1e]`，Tab 激活使用 phos accent（`text-[#00e5ff]`），字体 `font-mono text-xs`，与左侧 domain rail 一致。

CSS transition：`width` 48px ↔ 320px，`transition-all duration-200`。

#### 4.1.2 Tab A — Encounter

内容来自 `docs/Design/SIL/reference-prototype/04-sil-builder.jsx` Tab A：

- **Imazu-22 案例网格**：22 张小卡片（4 列 grid），每张含 mini SVG 几何图（己船/目标船向量）+ 编号 + 名称（如 "01 Head-on"）。选中高亮。
- **选中案例详情**（在网格下方展开）：较大 SVG 几何图 + DCPA 目标值 + TCPA 目标值
- **目标船参数调节**：相对方位（deg）、相对距离（nm）、目标船 SOG（kn）、目标船 COG（deg）— 四个 `<input type="number">` 字段

#### 4.1.3 Tab B — Environment

字段来自 reference prototype Tab B：

**风/浪/流**：
- 风力（Beaufort 0–12，下拉）、风向（deg，数字输入）
- 有效波高 Hs（m，数字输入）、浪向（deg，数字输入）
- 流速（kn，数字输入）、流向（deg，数字输入）

**能见度/传感器**：
- 昼夜（Day / Night / Dawn，下拉）、能见度（nm，数字输入）
- 雾（切换）、雨（切换）
- GNSS 噪声 σ（m，数字输入）、AIS 丢帧率（%，数字输入）、雷达量程（nm，数字输入）

**ODD 区域**：
- 主区域（文本输入）、边界事件（文本输入）、包络半径（nm）、一致性阈值（%）

**本船动力学**：
- 船体类型（FCB / ASV，下拉）、排水量（t）、初始航向 HDG0（deg）、初始航速 SOG0（kn）、最大舵角 RudMax（deg）、最大旋回率 ROTMax（deg/s）

#### 4.1.4 Tab C — Run

字段来自 reference prototype Tab C：

**时序/时钟**：
- 仿真时长（s）、仿真倍率（×）、随机种子（整数）、M2 频率（Hz）、M5 频率（Hz）

**模式/IvP 权重**：
- 自主等级（D2 / D3 / D4，下拉）、操作员角色（Observer / Supervisor / Commander，下拉）
- COLREG_AVOID 权重（0–100 滑块）、MISSION_ETA 权重（0–100 滑块）
- CPA 最小阈值（nm）、TCPA 触发阈值（min）

**预定故障注入表**（4 行固定故障）：

| 故障类型 | 触发时间(s) | 持续(s) | ON/OFF |
|----------|------------|---------|--------|
| AIS_DROPOUT | 60 | 30 | Toggle |
| RADAR_SPIKE | 120 | 10 | Toggle |
| DIST_STEP | 200 | 60 | Toggle |
| SENSOR_FREEZE | 300 | 20 | Toggle |

**通过判据表**（6 行）：

| 指标 | 阈值 | 状态 badge |
|------|------|-----------|
| CPA_min | 0.5nm | — |
| COLREGs 合规 | 100% | — |
| 路径偏差 | ≤2nm | — |
| ToR 响应时间 | ≤60s | — |
| M5 求解 p95 | ≤500ms | — |
| ASDR 完整性 | 100% | — |

badge 在报告屏渲染时联动 `ScoringResult`，Builder 中仅显示阈值配置。

#### 4.1.5 Tab D — Summary

只读面板，聚合当前 YAML 解析结果：
- 场景 ID、ENC 区域、OS 坐标/航向/航速、TS 坐标/航向/航速、仿真时长
- 底部三个操作按钮：**[保存]**（`useCreateScenarioMutation`）、**[验证 YAML]**（`useValidateScenarioMutation`）、**[Run →]**（跳转 Preflight）

### 4.2 BridgeHMI.tsx — "等待遥测"覆层

当 `telemetryStore.ownShip` 尚未收到首帧时，在地图中央显示半透明覆层：

```
┌─────────────────────────────┐
│    ◌  Waiting for           │
│       simulation data…      │
│    ws://localhost:8765      │
└─────────────────────────────┘
```

首帧到达后覆层消失（`opacity-0 pointer-events-none`）。

### 4.3 RunReport.tsx — 移除硬编码 KPI fallback

当前：`useGetLastRunScoringQuery` 失败时显示硬编码数值。

目标：移除硬编码。若查询 loading → 显示 skeleton；若查询失败 → 显示 `"— 暂无数据"` + 错误说明。

KPI strip 7 张卡片数据源全部切换为 `ScoringResult` 字段：

| 卡片 | 字段 |
|------|------|
| Verdict | `verdict` |
| Min CPA | `min_cpa_nm` |
| COLREGs | `colregs_compliant` |
| Max Rudder | `max_rud_deg` |
| ToR Time | `tor_time_s` |
| Decision P99 | `decision_p99_ms` |
| Faults | `faults_injected` |

---

## 5. 启动方式

```bash
cd tools/demo
./run_demo.sh
# 等价于：
# uvicorn demo_server:app --port 8000 &
# (WS server 已集成到同一进程)

cd web
npm run dev
# 访问 http://localhost:5173
```

`run_demo.sh` 也负责检查 `fastapi` / `uvicorn` / `websockets` 是否安装，缺失时打印 `pip install fastapi uvicorn websockets` 并退出。

---

## 6. 非目标（不在本 spec 实现）

- 真实 ROS2 集成或 foxglove_bridge
- 持久化场景存储（SQLite/Postgres）
- 多场景并发运行
- 用户认证
- ENC 真实 tile 服务（地图使用现有本地 tile server 或 fallback）
- 蒙特卡洛 / RL 域功能
- AIS 历史数据导入

---

## 7. API 合规性声明

本 demo 服务器的所有路径、HTTP method、request body、response schema 严格对齐：
- `docs/Design/SIL/Design v2.0/03-07-backend-api.md`（REST + WS 完整规范）
- `web/src/api/silApi.ts`（RTK Query endpoint 定义）
- `web/src/types/sil/own_ship_state.ts` + `target_vessel_state.ts`（Protobuf schema）

后续真实 FastAPI + ROS2 服务器替换 demo 服务器时，前端**零改动**。

---

## 8. 实现顺序建议（供 writing-plans 参考）

1. `trajectory.py`：Head-On 帧预计算 + ASDR 事件列表
2. `demo_server.py`：FastAPI app + 所有 REST endpoints（单文件，~150 行）
3. `demo_ws_server.py`：集成到同一进程的 WS 广播（~80 行）
4. `run_demo.sh`：启动脚本
5. `ScenarioBuilder.tsx`：右侧折叠栏（最大工作量）
6. `BridgeHMI.tsx`：等待覆层（~20 行）
7. `RunReport.tsx`：移除硬编码（~15 行）
8. 端到端冒烟测试：完整 4-screen 流走一遍

---

*本文档由 brainstorming 设计流程产出，版本 v1.0，2026-05-14。*
