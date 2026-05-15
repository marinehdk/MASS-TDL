# 后端 API 与数据流设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003-07 |
| 版本 | v2.0 |
| 日期 | 2026-05-13 |
| 父文档 | [03-sil-detailed-design.md](./03-sil-detailed-design.md) |

---

## 1. 系统分层架构

```
┌──────────────────────────────────────────────────────────────────────────────────────┐
│ ⑥ HMI 前端层（React + Zustand + RTK Query + MapLibre）                             │
│    4-screen: /builder → /preflight → /bridge → /report                          │
└────────────────────────────────────────────────────┬─────────────────────────────┘
                                                       │ REST / WebSocket
                                                ┌─────▼─────────┐
                                                │   ⑤ 控制编排层   │
                                                │  FastAPI + rclpy │
                                                │  sil_orchestrator │
                                                └─────┬─────────┘
                                                  │ REST + ROS2 Service
                                                ┌─▼──────────────────┐
                                                │   ④ 数据桥接层      │
                                                │ foxglove_bridge(C++)│
                                                └─┬──────────────────┘
                                                  │ Protobuf + ROS2 msg
                                                ┌─▼──────────────────┐
                                                │  ③ 仿真工作台层     │
                                                │ rclcpp_components   │
                                                │ 单进程零拷贝IPC     │
                                                └─┬──────────────────┘
                                                  │ l3_external_msgs
                                                ┌─▼──────────────────┐
                                                │  ② L3 TDL Kernel    │
                                                │  M1-M8 C++/MISRA    │
                                                └─────────────────────────────────────────────┘
```

---

## 2. 后端服务架构

### 2.1 服务组件

| 服务 | 技术栈 | 职责 |
|------|--------|------|
| **sil_orchestrator** | FastAPI + Python | REST API、场景管理、生命周期控制 |
| **foxglove_bridge** | C++ + Protobuf | 50Hz WS 推送、ROS2 Service 透传 |
| **sim_workbench** | rclcpp_components | 仿真节点单进程容器 |
| **rosbag2_recorder** | 官方 C++ | MCAP 录制 |

### 2.2 通信模式

| 连接 | 协议 | 数据 |
|------|------|------|
| FE ↔ Orchestrator | REST (HTTP) | 场景 CRUD、配置、导出 |
| FE ↔ foxglove_bridge | WebSocket | 50Hz 遥测、控制命令 |
| Orchestrator ↔ sim_workbench | rclpy / ROS2 Service | 生命周期控制 |
| sim_workbench ↔ L3 Kernel | ROS2 msg | 传感器数据、决策输出 |

---

## 3. REST API 设计

### 3.1 API 版本

所有 API 使用 `/api/v1` 前缀。

### 3.2 场景管理 API

#### GET /scenarios
获取场景列表

**响应**：
```json
{
  "scenarios": [
    {
      "id": "uuid",
      "name": "Head-on R14",
      "type": "head_on",
      "region": "trondheim",
      "target_count": 1,
      "created_at": "2026-05-13T10:00:00Z",
      "updated_at": "2026-05-13T10:00:00Z"
    }
  ]
}
```

#### GET /scenarios/{id}
获取场景详情

**响应**：
```json
{
  "id": "uuid",
  "name": "Head-on R14",
  "yaml_content": "...",
  "metadata": {
    "hash": "sha256...",
    "author": "user",
    "imazu_scenario": "R14"
  }
}
```

#### POST /scenarios
创建场景

**请求**：
```json
{
  "name": "New Scenario",
  "yaml_content": "...",
  "region": "trondheim"
}
```

#### PUT /scenarios/{id}
更新场景

#### DELETE /scenarios/{id}
删除场景

#### POST /scenarios/validate
验证 YAML + maritime-schema

**请求**：
```json
{
  "yaml_content": "..."
}
```

**响应**：
```json
{
  "valid": true,
  "errors": [],
  "warnings": []
}
```

### 3.3 生命周期 API

#### POST /lifecycle/configure
配置仿真

**请求**：
```json
{
  "scenario_id": "uuid",
  "run_id": "uuid"
}
```

#### POST /lifecycle/activate
激活仿真

#### POST /lifecycle/deactivate
暂停仿真

#### POST /lifecycle/cleanup
清理资源

### 3.4 仿真控制 API

#### POST /sim_clock/set_rate
设置仿真速率

**请求**：
```json
{
  "rate": 2.0
}
```

#### GET /sim_clock/status
获取时钟状态

### 3.5 自检 API

#### POST /self_check/start
启动自检

#### GET /self_check/status
获取自检状态

**响应**：
```json
{
  "checks": {
    "enc_loading": "passed",
    "asdr_startup": "passed",
    "utc_sync": "passed",
    "module_health": {
      "M1": "GREEN",
      "M2": "GREEN",
      "M3": "GREEN",
      "M4": "GREEN",
      "M5": "GREEN",
      "M6": "GREEN",
      "M7": "GREEN",
      "M8": "GREEN"
    },
    "scenario_signature": "passed"
  },
  "overall": "passed"
}
```

### 3.6 故障注入 API

#### POST /fault/trigger
触发故障

**请求**：
```json
{
  "type": "ais_dropout",
  "duration_s": 10,
  "payload": {}
}
```

### 3.7 导出 API

#### POST /export/marzip
导出证据包

**请求**：
```json
{
  "run_id": "uuid",
  "format": "marzip"
}
```

**响应**：
```json
{
  "status": "processing",
  "download_url": null
}
```

轮询状态直到完成：

#### GET /export/status/{export_id}
获取导出状态

### 3.8 运行报告 API

#### GET /runs/{runId}/report
获取完整报告

**响应**：
```json
{
  "run_id": "uuid",
  "scenario_id": "uuid",
  "scenario_hash": "sha256...",
  "start_time": "2026-05-13T10:00:00Z",
  "end_time": "2026-05-13T10:10:00Z",
  "duration_s": 600,
  "kpis": {
    "min_cpa_nm": 0.8,
    "max_rot_dps": 3.2,
    "avg_sog_kn": 11.8
  },
  "scoring": {
    "safety": 0.92,
    "rule": 0.87,
    "delay": 0.95,
    "magnitude": 0.88,
    "phase": 0.90,
    "plausibility": 0.93,
    "overall": 0.91
  },
  "verdict": "PASS",
  "colregs_chain": [
    {"rule": "R5", "time": 150, "status": "passed"},
    {"rule": "R7", "time": 195, "status": "passed"},
    {"rule": "R14", "time": 240, "status": "passed"},
    {"rule": "R17", "time": 330, "status": "passed"},
    {"rule": "R8", "time": 405, "status": "passed"}
  ],
  "events": [
    {"time": 222, "type": "tor", "module": "M8", "detail": "..."},
    {"time": 315, "type": "colreg", "module": "M6", "detail": "..."}
  ]
}
```

---

## 4. WebSocket 协议

### 4.1 foxglove_bridge 端点

| 端点 | 说明 |
|------|------|
| `ws://host/ws/telemetry` | 50Hz 遥测数据 |
| `ws://host/ws/control` | 控制命令 |
| `ws://host/ws/modules` | 模块状态 |
| `ws://host/ws/asdr` | ASDR 事件流 |

### 4.2 遥测消息格式（Protobuf）

```protobuf
message OwnShipState {
  builtin_interfaces.Time stamp = 1;
  double lat = 2;
  double lon = 3;
  double heading_deg = 4;
  double sog_kn = 5;
  double cog_deg = 6;
  double rot_dps = 7;
}

message TargetVesselState {
  string mmsi = 1;
  builtin_interfaces.Time stamp = 2;
  double lat = 3;
  double lon = 4;
  double sog_kn = 5;
  double cog_deg = 6;
}

message TelemetryFrame {
  OwnShipState own_ship = 1;
  repeated TargetVesselState targets = 2;
  EnvironmentState environment = 3;
  double sim_time_s = 4;
  double sim_rate = 5;
}
```

### 4.3 控制消息格式

```protobuf
message ControlCommand {
  string command = 1;  // "play", "pause", "stop", "reset"
  optional double rate = 2;
  optional double seek_to_s = 3;
}

message ControlResponse {
  bool success = 1;
  string message = 2;
  double current_time_s = 3;
  string sim_state = 4;  // "IDLE", "ARMING", "RUNNING", "REPORT"
}
```

---

## 5. ROS2 Service 接口

### 5.1 Lifecycle 服务

| 服务 | 类型 | 说明 |
|------|------|------|
| `/lifecycle_mgr/configure` | `LifecycleControl` | 配置仿真 |
| `/lifecycle_mgr/activate` | `LifecycleControl` | 激活仿真 |
| `/lifecycle_mgr/deactivate` | `LifecycleControl` | 暂停仿真 |
| `/lifecycle_mgr/cleanup` | `LifecycleControl` | 清理资源 |
| `/lifecycle_mgr/get_state` | `LifecycleGetState` | 获取状态 |

### 5.2 仿真时钟服务

| 服务 | 类型 | 说明 |
|------|------|------|
| `/sim_clock/set_rate` | `SimClockSetRate` | 设置速率 |
| `/sim_clock/get_time` | `SimClockGetTime` | 获取时间 |

### 5.3 故障服务

| 服务 | 类型 | 说明 |
|------|------|------|
| `/fault_inject/trigger` | `FaultTrigger` | 触发故障 |
| `/fault_inject/list` | `FaultList` | 列出可用故障 |
| `/fault_inject/cancel` | `FaultCancel` | 取消故障 |

---

## 6. 数据流设计

### 6.1 50Hz 仿真闭环

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         50Hz 仿真闭环                                         │
│                                                                              │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐    ┌─────────────┐   │
│  │ship_dynamics│───→│sensor_mock  │───→│tracker_mock│───→│ L3 Kernel   │   │
│  │  (FCB MMG)  │    │ (AIS+Radar) │    │ (God/KF)   │    │   M2        │   │
│  └─────────────┘    └─────────────┘    └─────────────┘    └──────┬──────┘   │
│          ↑                                                              │      │
│          │                                                              │      │
│  ┌───────▼───────┐    ┌─────────────┐    ┌─────────────┐    ┌──────┴──────┐   │
│  │env_disturbance│    │target_vessel│───→│sensor_mock │    │ L3 Kernel   │   │
│  │(Wind+Current) │    │(AIS Replay)│    │            │    │  M5/M6/M7   │   │
│  └─────────────┘    └─────────────┘    └─────────────┘    └──────┬──────┘   │
│                                                                    │         │
│  ┌───────▼──────────────────────────────────────────────────────┴──────┐    │
│  │                              scoring_node                          │    │
│  │                        (6维评分 → Arrow)                          │    │
│  └──────────────────────────────┬───────────────────────────────────┘    │
│                                │                                          │
│  ┌────────────────────────────▼───────────────────────────────────┐    │
│  │                        rosbag2_recorder                         │    │
│  │                      (MCAP 录制 50Hz)                            │    │
│  └───────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 6.2 证据数据流

```
仿真结束
    ↓
┌─────────────────────────────────────────────────────────────────────────────┐
│ MCAP 文件（完整 50Hz 录制）                                                 │
│ /own_ship_state, /targets/*, /scoring/*, /asdr/*                          │
└────────────────────────────────┬──────────────────────────────────────────┘
                                 ↓
                        ┌─────────▼─────────┐
                        │   Arrow 后处理    │
                        │ scoring → KPI     │
                        └─────────┬─────────┘
                                 ↓
┌────────────────────────────────▼──────────────────────────────────────────┐
│ Marzip 证据包                                                              │
│ ├── scenario.yaml (SHA256 签名)                                           │
│ ├── scenario.sha256                                                         │
│ ├── results.arrow (KPI 列存)                                               │
│ ├── results.bag.mcap (原始录像)                                            │
│ ├── asdr_events.jsonl (事件流)                                             │
│ ├── verdict.json (PASS/FAIL + 理由)                                        │
│ └── manifest.yaml (版本/工具链)                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 7. 生命周期状态机

### 7.1 状态定义

| 状态 | 说明 | 可用操作 |
|------|------|---------|
| **Unconfigured** | 初始状态，未配置 | configure() |
| **Inactive** | 已配置，未激活 | activate() |
| **Active** | 运行中，50Hz tick | deactivate() |
| **Deactivating** | 正在停止 | — |
| **Finalized** | 已清理 | configure()（重新开始）|

### 7.2 状态转换

```
Unconfigured ──configure()──→ Inactive ──activate()──→ Active
                                          ↑
                                          │
                                          deactivate()
                                          │
                                          ↓
                                      Inactive ──cleanup()──→ Finalized
                                                         ↑
                                                         │
                                                         configure()
                                                         │
                                                         （重新开始）
```

---

## 8. IDL 策略

### 8.1 三语言生成

```
                    ┌──────────────────────────┐
                    │  /idl/proto/*.proto      │  ← 主 IDL（人工维护）
                    │  (sim_workbench/idl)     │
                    └─────────┬────────────────┘
                              │  buf generate
              ┌───────────────┼────────────────┐
              ▼               ▼                ▼
    ┌────────────────┐  ┌──────────────┐  ┌──────────────────┐
    │ proto2ros      │  │ protobuf-ts  │  │ python_protobuf  │
    │ → ROS2 .msg    │  │ → web/types/ │  │ → orchestrator   │
    │ + conversion   │  │   *.ts       │  │   pydantic-like  │
    │ stubs          │  │              │  │                  │
    └────────────────┘  └──────────────┘  └──────────────────┘
```

### 8.2 边界规则

| 边界 | IDL | 原因 |
|------|-----|------|
| L3 Kernel 内 M1-M8 | ROS2 .msg | frozen 认证内核 |
| L3 Kernel ↔ sim_workbench | ROS2 .msg | l3_external_msgs RFC 锁定 |
| sim_workbench ↔ foxglove_bridge | Protobuf | greenfield 边界 |
| foxglove_bridge ↔ 前端 | Protobuf | WebSocket |

---

## 9. Protobuf 消息字典（Phase 1）

| 消息 | 频率 | 来源 | 消费 |
|------|------|------|------|
| `sil.OwnShipState` | 50Hz | ship_dynamics_node | FE map + KER M2 |
| `sil.TargetVesselState` | 10Hz | target_vessel_node | FE map + KER M2 |
| `sil.RadarMeasurement` | 5Hz | sensor_mock_node | KER M2 + FE Phase 2 |
| `sil.AISMessage` | 0.1Hz | sensor_mock_node | KER M2 |
| `sil.EnvironmentState` | 1Hz | env_disturbance_node | KER M1 + FE HUD |
| `sil.FaultEvent` | event | fault_injection_node | FE ASDR log |
| `sil.ModulePulse` | 10Hz | self_check / KER M1-M8 | FE Module Pulse |
| `sil.ScoringRow` | 1Hz | scoring_node | FE Report + Arrow |
| `sil.ASDREvent` | event | KER M8 + scoring | FE ASDR log |
| `sil.LifecycleStatus` | 1Hz | scenario_lifecycle_mgr | FE 状态条 |