# SIL 框架总体架构设计

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-DD-003-01 |
| 版本 | v2.0 |
| 日期 | 2026-05-13 |
| 父文档 | [03-sil-detailed-design.md](./03-sil-detailed-design.md) |

---

## 1. 系统拓扑

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│ ⑥ HMI 前端层（Web：React 18 + MapLibre GL JS + Zustand + RTK Query）        │
│    ┌───────────────────────────────┐  ┌──────────────┐  ┌──────────────┐ │
│    │ 场景构建器 (Builder)          │  │  预检        │  │ Captain HMI  │ │
│    │ - 单一场景 (Single Scenario)  │  │ (Pre-flight) │  │ (Bridge)     │ │
│    │ - 蒙特卡洛实验 (Monte Carlo)  │  │              │  │              │ │
│    │ - 深度学习环境 (RL Playground)│  │ 运行报告     │  │              │ │
│    └───────────────────────────────┘  └──────────────┘  └──────────────┘ │
└──────────────────────────────────────────────────────────────────────────┬──────────┘
                                                                   │
                                              REST / WS / foxglove_bridge
                                                                   │
┌──────────────────────────────────────────────────────────────────▼──────────┐
│ ⑤ 控制编排层（FastAPI + rclpy / ROS2 Service 桥接）                              │
│    sil_orchestrator: 场景CRUD · 仿真生命周期 · 证据导出 · 场景哈希签名          │
└─────────────────────────────────────────────────────────────────────┬──────────────┘
                                                                      │
                                              foxglove_bridge (Protobuf, C++)
                                                                      │
┌──────────────────────────────────────────────────────────────────▼──────────┐
│ ④ 数据桥接层                                                                │
│    50Hz 状态转发 · ROS2 Service 透传 · MCAP 录制 · WebSocket 推送           │
└─────────────────────────────────────────────────────────────────────┬──────────┘
                                                                      │
                                              ROS2 msg (l3_external_msgs)
                                                                      │
┌─────────────────────────────────────────────────────────────────────▼──────────┐
│ ③ 仿真工作台层（rclcpp_components 单进程零拷贝 IPC）                            │
│    ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐             │
│    │ lifecycle_mgr   │  │ ship_dynamics   │  │ env_disturbance │             │
│    │ (LifecycleNode) │  │ (FCB 4-DOF MMG) │  │ (Gauss-Markov)  │             │
│    └─────────────────┘  └─────────────────┘  └─────────────────┘             │
│    ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐             │
│    │ target_vessel   │  │ sensor_mock     │  │ tracker_mock    │             │
│    │ (AIS Replay/NCDM)│ │ (AIS+Radar)     │  │ (God/KF)       │             │
│    └─────────────────┘  └─────────────────┘  └─────────────────┘             │
│    ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐             │
│    │ fault_injection  │  │ scoring_node    │  │ rosbag2_recorder│             │
│    │ (故障注入)       │  │ (6维评分)       │  │ (MCAP录制)      │             │
│    └─────────────────┘  └─────────────────┘  └─────────────────┘             │
└─────────────────────────────────────────────────────────────────────┬──────────────┘
                                                                      │
                                              l3_external_msgs / l3_msgs
                                                                      │
┌─────────────────────────────────────────────────────────────────────▼──────────┐
│ ② L3 TDL Kernel 层（C++/MISRA/ROS2 native / DNV-RP-0513 frozen）               │
│    ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐ ┌─────┐          │
│    │ M1  │ │ M2  │ │ M3  │ │ M4  │ │ M5  │ │ M6  │ │ M7  │ │ M8  │          │
│    │ODD  │ │ WM  │ │MIS  │ │BEH  │ │PLN  │ │COL  │ │SAF  │ │HMI  │          │
│    └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘ └─────┘          │
└───────────────────────────────────────────────────────────────────────────────┘
                                                                      │
                                              maritime-schema YAML
                                                                      │
┌─────────────────────────────────────────────────────────────────────▼──────────┐
│ ① 存储层                                                                       │
│    场景库（YAML+Imazu-22+AIS） · MCAP录像 · Arrow KPI · Marzip证据包          │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. 模块责任矩阵

| D-task | 模块/节点 | colcon 包 | 类型 | 责任 | 关键参数 |
|--------|---------|----------|------|------|---------|
| **D1.3a** | `ship_dynamics_node` | `fcb_simulator` | std Node | FCB 4-DOF MMG（Yasukawa 2015, RK4 dt=0.02s）| hull_class=SEMI_PLANING, dt=0.02 |
| **D1.3a** | `env_disturbance_node` | `env_disturbance` | std Node | Gauss-Markov wind + current（恒定/脉冲/Monte Carlo）| tau_wind=300s, sigma=2.0 |
| **D1.3a** | `sensor_mock_node` | `perception_mock` | std Node | AIS Class A/B 转发器（含 dropout）+ Radar 模型| ais_drop_pct=0, radar_R_ne |
| **D1.3a** | `tracker_mock_node` | `perception_mock` | std Node | God tracker（ground truth）/KF tracker 二选一| tracker_type=god\|kf, P_0, q |
| **D1.3b.1** | `scenario_authoring_node` | `scenario_authoring` | std Node | YAML CRUD + maritime-schema + cerberus-cpp 双校验 + Imazu-22 | scenario_dir, imazu22_lock_hash |
| **D1.3b.2** | `scenario_authoring_node` (扩展) | `scenario_authoring` | std Node | AIS 5阶段管线 + ais_replay_vessel 模式 | ais_db_dsn |
| **D1.3b.3** | `foxglove_bridge` + Web HMI | `foxglove_bridge` + `sil_web` | C++/React | 50Hz桥接 + 4-screen Web HMI | — |
| **D1.3c** | FMI bridge | `fmi_bridge` | std Node | ShipMotionSimulator FMU 接口（DDS-FMU）| — |
| **D1.3.1** | `self_check_node` | `sil_lifecycle` | std Node | M1-M8健康聚合 + ENC加载验证 + 场景SHA256签名 | health_window_ms=500 |
| — | `scenario_lifecycle_mgr` | `sil_lifecycle` | LifecycleNode | 4-screen状态机 + 9节点调度 | tick_hz=50, settle_timeout_ms=2000 |
| — | `fault_injection_node` | `fault_inject` | std Node | 故障注入（ais_dropout/radar_spike/dist_step）| fault_dict_path |
| — | `target_vessel_node` | `target_vessel` | std Node | 3种模式：ais_replay（必）/ncdm/intelligent | mode∈{replay,ncdm,intel} |
| — | `scoring_node` | `scoring` | std Node | 6维评分（Hagen 2022 / Woerner 2019）| weights_yaml |
| — | `rosbag2_recorder` | `rosbag2` (官方) | 进程 | MCAP格式录制 `/own_ship_state` `/targets/*` 等 | storage_id=mcap, compression=zstd |

---

## 3. 技术栈选型

| 层级 | 技术选型 | 版本 | 说明 |
|------|---------|------|------|
| **前端框架** | React 18 + TypeScript | 18.x | 组件化开发 |
| **状态管理** | Zustand（50Hz遥测）+ RTK Query | 5.x / 2.x | 选择性订阅防帧丢 |
| **地图引擎** | MapLibre GL JS | 4.x | S-57 MVT矢量渲染 |
| **后端编排** | FastAPI + rclpy | 0.15+ / Jazzy | REST封装+ROS2集成 |
| **数据桥接** | foxglove_bridge（C++） | 1.x | Protobuf 50Hz WS |
| **仿真内核** | rclcpp_components | Jazzy | 单进程零拷贝IPC |
| **积分方法** | RK4 | dt=0.02s | 4-DOF MMG |
| **消息IDL** | ROS2 .msg（kernel内）+ Protobuf（边界） | — | buf管理 |
| **场景格式** | DNV maritime-schema | v0.2.x | YAML |
| **证据格式** | MCAP + Arrow + Marzip | — | CCS/DNV认证 |

---

## 4. Lifecycle 状态机

```
         ┌─ FE state ─┐    ┌─ ROS2 Lifecycle ─┐    ┌─ Active nodes ──────────────────┐
① 场景构建器    IDLE         Unconfigured         scenario_authoring_node only
   ↓ "Run →"                ↓ configure()        
② 预检          ARMING       Inactive            + self_check + sensor + tracker
   ↓ countdown=0             ↓ activate()          + ship_dyn + env_dist + target +
③ Captain HMI    RUNNING      Active (50Hz)        + fault_inject + scoring +
   ↓ "⏹ Stop"                ↓ deactivate()       + rosbag2 + L3 kernel
   ↓ TIMEOUT                                    
   ↓ FAULT_FATAL                                 
④ 运行报告      REPORT       Inactive            scoring + scenario_authoring
   ↓ "New Run"                                  (MCAP→Arrow 后处理在 orchestrator)
   → ①           IDLE         Unconfigured
```

---

## 5. 分阶段实施

| 阶段 | 时间 | D-task | 内容 | Demo |
|------|------|--------|------|------|
| **Phase 1** | 5/13–6/15 | D1.1–D1.3b.3 | ROS2工作区 + 4-DOF MMG + YAML场景 + Web HMI | **DEMO-1（6/15）** Skeleton Live |
| **Phase 2** | 6/16–7/31 | D1.3b.2–D1.3c | AIS导入 + FMI桥接 + 回放分析 | **DEMO-2（7/31）** Decision-Capable |
| **Phase 3** | 8/1–8/31 | D2.4–D3.6 | 6维评分 + 1000场景 + ToR | **DEMO-3（8/31）** Full-Stack |
| **Phase 4** | 9/1–12/31 | D4.x | HIL + SIL 2认证 + AIP提交 | 展望 |