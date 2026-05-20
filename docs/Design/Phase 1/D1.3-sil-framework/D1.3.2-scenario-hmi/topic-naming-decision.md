# Topic Naming Arbitration: SIL ↔ L3 Kernel Topic Mapping

> **权威引用**: 架构报告 v1.1.3-pre-stub §15.2 接口契约总表
> **适用范围**: DEMO-1 SIL↔L3 桥接实现（ros2_topic_bridge）
> **对应任务**: D1.3.2 Task 1
> **状态**: 定稿（2026-05-20 更新：§4 扩展至 F1-F11）

---

## 1. 文档目的

本文档是 bridge 节点实现的**唯一权威引用**。它列出每个 `/sil/*` 主题与对应 L3 内核期望主题的 1:1 映射关系，标注需要类型转换的配对、透传配对,以及当前不桥接的主题及理由。

---

## 2. 完整主题映射表

所有 12 个与 SIL↔L3 桥接相关的 `/sil/*` 主题（基于 docker-compose.yml foxglove topic_whitelist + 前端 `useFoxgloveLive.ts` TOPIC_MAP + bridge 计划新增主题）：

### 2.1 SIL → L3（仿真数据输入 L3 内核）

| # | SIL 主题 | SIL 消息类型 | L3 主题 | L3 消息类型 | 桥接方向 | 消费模块 | 类型转换? |
|---|----------|-------------|---------|------------|---------|---------|----------|
| 1 | `/sil/own_ship_state` | `sil_msgs/OwnShipState` | `/fusion/own_ship_state` | `l3_external_msgs/FilteredOwnShipState` | SIL→L3 | M1, M2 | **CONVERT** (flat → EKF-filtered + covariance) |
| 2 | `/sil/target_vessel_state` | `sil_msgs/TargetVesselState` | `/fusion/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | SIL→L3 | M2 | **CONVERT** (single vessel → array wrapper) |
| 3 | `/sil/environment` | `sil_msgs/EnvironmentState` | `/fusion/environment_state` | `l3_external_msgs/EnvironmentState` | SIL→L3 | M1, M2 | **CONVERT** (field rename + unit conversion) |
| 4 | `/sil/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | `/fusion/tracked_targets` | `l3_external_msgs/TrackedTargetArray` | SIL→L3 | M2 | **PASSTHROUGH** (same type, bridge relays) |

> **说明**: #4 `/sil/tracked_targets` 已由 `tracker_mock` 以 `l3_external_msgs/TrackedTargetArray` 格式发布。Bridge 仅做主题中继 (`/sil/tracked_targets` → `/fusion/tracked_targets`),无需字段转换。M2 World Model 消费此主题。

### 2.2 L3 → SIL（L3 决策输出回 SIL / 前端）

| # | L3 源主题 | L3 消息类型 | SIL 目标主题 | SIL 消息类型 | 桥接方向 | 消费方 | 类型转换? |
|---|----------|------------|------------|-------------|---------|-------|----------|
| 5 | `/l3/m5/avoidance_plan` | `l3_msgs/AvoidancePlan` | `/sil/actuator_cmd` | `ship_sim_interfaces/ActuatorCmd` | L3→SIL | ship_dynamics | **CONVERT** (WP list → rudder/throttle cmd) |
| 6 | `/l3/asdr/record` | `l3_msgs/ASDRRecord` | `/sil/asdr_event` | `sil_msgs/ASDREvent` | L3→SIL | 前端 ASDR 日志 | **CONVERT** (record → event) |
| 7 | `/l3/m7/heartbeat` + 各 M 节点 | 多源 | `/sil/module_pulse` | `sil_msgs/ModulePulse` | L3→SIL | 前端 ModulePulse | **AGGREGATE** (多心跳→聚合脉冲) |
| 8 | `/l3/m8/ui_state` | `l3_msgs/UIState` | `/sil/m8_ui_state` | `l3_msgs/UIState` | L3→SIL | 前端 SAT-1 | **PASSTHROUGH** (same type, bridge relays) |

### 2.3 透传主题（不经过 bridge——已在正确格式和命名空间中）

| # | 主题 | 消息类型 | 来源 | 消费方 | 说明 |
|---|------|---------|------|-------|------|
| 9 | `/sim_clock` | `builtin_interfaces/Time` | lifecycle_mgr | 全部 M 节点 + foxglove | 已在正确命名空间,所有节点直接消费 |
| 10 | `/sil/scoring` | `sil_msgs/ScoringRow` | scoring_node | 前端 ScoringRow | 已在 SIL 命名空间,前端直接消费 |
| 11 | `/sil/lifecycle_status` | `sil_msgs/LifecycleStatus` | lifecycle_mgr | 前端 | 已在 SIL 命名空间,前端直接消费 |

### 2.4 当前不桥接的主题（DEMO-1 范围外）

| # | 主题 | 消息类型 | 来源 | 不桥接理由 |
|---|------|---------|------|-----------|
| 12 | `/sil/radar_meas` | `sil_msgs/RadarMeasurement` | sensor_mock (5 Hz) | 仅用于 tracker_mock 内部融合;L3 M2 通过 `/fusion/tracked_targets` 消费已处理的轨迹数据 |
| 13 | `/sil/ais_msg` | `sil_msgs/AISMessage` | sensor_mock (0.1 Hz) | 同上;仅用于 tracker_mock 内部融合 |
| 14 | `/sil/fault/ais_dropout` | `sil_msgs/FaultEvent` | fault_injection | DEMO-1 不涉及故障注入场景;故障注入在 Phase 3 D3.x 覆盖 |
| 15 | `/sil/fault/radar_spike` | `sil_msgs/FaultEvent` | fault_injection | 同上 |
| 16 | `/sil/fault/dist_step` | `sil_msgs/FaultEvent` | fault_injection | 同上 |
| 17 | `/sil/sensor_status` | `sil_msgs/ModulePulse` | (待生命周期管理) | 前端专用;生成逻辑在 lifecycle_mgr 内,不涉及 L3 数据;DEMO-1 期间用 stub |
| 18 | `/sil/commlink_status` | `sil_msgs/ModulePulse` | (待生命周期管理) | 同上 |
| 19 | `/sil/control_cmd` | `ship_sim_interfaces/ActuatorCmd` | bridge → 前端 | ⚠️ 见下方主题名称不一致说明 |

> **关于 #19 `/sil/control_cmd` vs `/sil/actuator_cmd`**: 前端 TOPIC_MAP 中名为 `/sil/control_cmd`,但 `ship_dynamics` 节点订阅 `/sil/actuator_cmd`。Bridge 的 L3→SIL 映射表(上表 #5)使用 `/sil/actuator_cmd` 作为桥接目标,匹配 ship_dynamics 的订阅。前端 `/sil/control_cmd` handler 保持不动,或后续与 `/sil/actuator_cmd` 合并。此离散在 Task 5 bridge 实现中解决。

---

## 3. 类型转换详情

### 3.1 SIL → L3 转换

**OwnShipState → FilteredOwnShipState:**
| SIL 字段 | L3 字段 | 转换 |
|---------|--------|------|
| `lat`, `lon` (deg) | `pose_lat`, `pose_lon` | 直传 |
| `heading` (deg) | `heading_rad` | deg → rad |
| `sog` (kn) | `sog_mps` | kn → m/s (×0.5144) |
| `cog` (deg) | `cog_rad` | deg → rad |
| `u` (m/s) | `surge_mps` | 直传 |
| `v` (m/s) | `sway_mps` | 直传 |
| `r` (rad/s) | `yaw_rate_rps` | 直传 |
| (无) | `covariance[49]` | DEMO-1: 单位矩阵(无 EKF) |
| (无) | `confidence` | DEMO-1: 0.9 stub |

**TargetVesselState → TrackedTargetArray:**
| SIL 字段 | L3 字段 | 转换 |
|---------|--------|------|
| `mmsi` | `target_id` | 直传 |
| `lat`, `lon` (deg) | `pose_lat`, `pose_lon` | 直传 |
| `heading` (deg) | `heading_rad` | deg → rad |
| `sog` (kn) | `sog_mps` | kn → m/s |
| `cog` (deg) | `cog_rad` | deg → rad |
| (无) | `classification` | DEMO-1: 0 (unknown) |
| (无) | `confidence` | DEMO-1: 0.85 stub |

**EnvironmentState:**
| SIL 字段 | L3 字段 | 转换 |
|---------|--------|------|
| `wind_speed_kn` | `wind_speed_mps` | kn → m/s |
| `wind_direction_deg` | `wind_direction_rad` | deg → rad |
| `current_speed_kn` | `current_speed_mps` | kn → m/s |
| `current_direction_deg` | `current_direction_rad` | deg → rad |
| `visibility_m` | `visibility_m` | 直传 |
| (无) | `sea_state` | DEMO-1: 0 (calm) |
| (无) | `confidence` | DEMO-1: 0.9 stub |

### 3.2 L3 → SIL 转换

**AvoidancePlan → ActuatorCmd:**
从 avoidance plan 的第一个航点提取 `rudder_angle_deg` 和 `throttle`,写入 ActuatorCmd。

**ASDRRecord → ASDREvent:**
从 `ASDRRecord` 的 `record_id`, `module_id`, `event_type`, `decision_summary` 字段映射到 `ASDREvent` 对应字段。

**ModulePulse 聚合:**
Bridge 订阅 8 个 L3 模块的发布主题:
- M1: `/l3/m1/odd_state` → 心跳信标
- M2: `/l3/m2/world_state` → 心跳信标
- M3: `/l3/m3/mission_goal` → 心跳信标
- M4: `/l3/m4/behavior_plan` → 心跳信标
- M5: `/l3/m5/avoidance_plan` → 心跳信标
- M6: `/l3/m6/colregs_constraint` → 心跳信标
- M7: `/l3/m7/heartbeat` → 显式心跳
- M8: `/l3/m8/ui_state` → 心跳信标

桥接器聚合 `module_id[]` + `is_active[]` 列表,以 1 Hz 频率发布到 `/sil/module_pulse`。

---

## 4. 发现的 L3 内部主题不匹配（共 11 项：F1-F5 基础 + F6-F11 扩展）

在探索阶段（2026-05-15）发现 L3 内核 C++ 源代码中 5 个硬编码主题字符串错误。2026-05-20 全量 `grep` M1-M8 源码后又发现 6 个额外不一致，总计 **11 项**。这些错误必须在 bridge 工作前修复，否则 M4→M5→M7/M8 内部链路不通。

### 4.1 基础修复项 F1-F5（2026-05-15 发现）

| # | 文件 | 当前值(错误) | 应改为 | 影响 |
|---|------|------------|-------|------|
| F1 | `mid_mpc_node.cpp:39-43` | `/m2/own_ship_state` | `/l3/m2/world_state` | M5 收不到 M2 世界状态 |
| F2 | `mid_mpc_node.cpp`（已随 F1 合并） | `/m2/tracked_targets` | `/l3/m2/world_state` (world_state 已含 targets) | M5 收不到目标 |
| F3 | `mid_mpc_node.cpp:45-49` | `/m4/behavior_plan` | `/l3/m4/behavior_plan` | M5 收不到 M4 行为计划 |
| F4 | `mid_mpc_node.cpp:51-55` | `/m6/colregs_constraint` | `/l3/m6/colregs_constraint` | M5 收不到 COLREGs 约束 |
| F5 | `safety_supervisor_node.cpp:125-128` | `/l3/m2/odd_state` | `/l3/m1/odd_state` (M1 发布,非 M2) | M7 收不到 ODD 状态 |

> F1+F2 修复涉及类型变更:从两个独立订阅(`OwnShipState` + `TrackedTargetArray`)合并为单个 `WorldState` 订阅。`WorldState.msg` 已包含 `own_ship` 和 `targets` 字段(架构报告 §6.3)。实测:2026-05 版本 `mid_mpc_node.cpp` 已正确订阅 `/l3/m2/world_state`（单独一行），`/m2/own_ship_state` 和 `/m2/tracked_targets` 旧行已删除。**F1-F5 中多数已在 2026-05 版本修复，需逐项确认。**

### 4.2 扩展发现 F6-F11（2026-05-20 全量 grep 实测）

| # | 文件 | 当前值(实测) | 架构标准 | 影响 | 严重度 |
|---|------|-------------|---------|------|--------|
| F6 | `bc_mpc_node.cpp:34-38` | `/m2/world_state` | `/l3/m2/world_state` | M5-BC 收不到 M2 WorldState（BC-MPC 使用无 `/l3/` 前缀的主题名，与系统其他 7 个模块不一致）| 🔴 高 |
| F7 | `mid_mpc_node.cpp:69` → vs `safety_supervisor_node.cpp:140-143` + `hmi_transparency_bridge_node.cpp:76-78` | **Pub**: `/m5/avoidance_plan` → **Sub**: `/l3/m5/avoidance_plan` | `/l3/m5/avoidance_plan` | M5-Mid 发布 `/m5/avoidance_plan`，但 M7 和 M8 订阅 `/l3/m5/avoidance_plan` — **发布/订阅不匹配**。`m5_mid_mpc.launch.py:17` 有 remap `("/m6/colregs_constraint", "/l3/m6/colregs_constraint")` 但缺少 avoidance_plan 的 remap | 🔴 高 |
| F8 | `mid_mpc_node.cpp:70-71` → vs M1/M2/M3/M6/M7/M8 | **Pub**: `/m5/asdr_record`, `/m5/sat_data` → 其余模块 **Pub/Sub**: `/l3/asdr/record`, `/l3/sat/data` | `/l3/asdr/record`, `/l3/sat/data` | M5-Mid 的 ASDR 和 SAT 输出不走标准主题，其余模块无法消费。另 `bc_mpc_node.cpp:48-49` 发布 `/m5/asdr_record_bc`（同样偏离）| 🔴 高 |
| F9 | `odd_envelope_manager_node.cpp:327-334` → vs `safety_supervisor_node.cpp:177-182` | M1 Sub: `/reflex/activation_notification` → M7 Sub: `/l3/reflex/activation` | 应统一为单一主题 | 两个模块监听同一 Reflex Arc 事件但使用不同主题名。若 Reflex 发布者用任一主题，另一模块将收不到 | 🟡 中 |
| F10 | `odd_envelope_manager_node.cpp:340-347` + `hmi_transparency_bridge_node.cpp:89-92` → vs `safety_supervisor_node.cpp:184-189` | M1 Sub: `/override/active_signal`, M8 Sub: `/override/active_signal` → M7 Sub: `/l3/override/active` | 应统一为单一主题 | Override 信号在三模块间使用两种主题名 — M1+M8 与 M7 不互通 | 🟡 中 |
| F11 | `bc_mpc_node.cpp:46-47` → vs `safety_supervisor_node.cpp:163-168` | **Pub**: `/m5/reactive_override_cmd` → **Sub**: `/l3/m4/reactive_override_cmd` | `/l3/m4/reactive_override_cmd` | BC-MPC 发布到 `/m5/` 前缀，但 M7 Safety Supervisor 从 `/l3/m4/` 前缀订阅 — **完全不匹配** | 🔴 高 |

### 4.3 修复决策

| 项 | DEMO-1 修复? | 修复方式 | 理由 |
|----|-------------|---------|------|
| F1-F5 | ✅ 已在 2026-05 源码版本修复 | 直接修改 C++ 字符串 + 合并订阅 | L3 内核内部链路基础前提 |
| F6 | ✅ 必须修复 | 修改 `bc_mpc_node.cpp:34` → `/l3/m2/world_state` | BC-MPC 无合理理由使用无前缀主题名 |
| F7 | ✅ 必须修复 | 在 `m5_mid_mpc.launch.py` 增加 remap `("/m5/avoidance_plan", "/l3/m5/avoidance_plan")` | 最小改动，launch remap 不改源码；或修改 `mid_mpc_node.cpp:69` 直接改为 `/l3/m5/avoidance_plan` |
| F8 | 🟡 DEMO-1 可选 | 在 bridge 中增加 `/m5/asdr_record` → `/l3/asdr/record` 和 `/m5/sat_data` → `/l3/sat/data` 中继 | DEMO-1 中 ASDR/SAT 为日志用途，非关键实时链路。Bridge 已实现 ASDR 中继（`/l3/asdr/record` → `/sil/asdr_event`），增加 `/m5/*` 源订阅即可 |
| F9 | 🟡 DEMO-1 低优先 | 在 bridge 中增加 `/reflex/activation_notification` ↔ `/l3/reflex/activation` 双向中继 | Reflex Arc 触发频率极低；DEMO-1 无 Reflex 场景 |
| F10 | 🟡 DEMO-1 低优先 | 在 bridge 中增加 `/override/active_signal` ↔ `/l3/override/active` 双向中继 | Override 触发频率极低；DEMO-1 无 Override 场景 |
| F11 | ✅ 必须修复 | 修改 `bc_mpc_node.cpp:46-47` → `/l3/m4/reactive_override_cmd` | Reactive Override 是 M7 Doer-Checker 关键链路，不匹配将导致 Safety Supervisor 无法监视 BC-MPC 快速避碰指令 |

---

## 5. Bridge 方案决策理由

选择 ros2_topic_bridge 桥接方案(非侵入式)而非修改 M1-M8 源码,基于以下理由:

### 5.1 否决的方案:直接修改 L3 内核主题字符串

| 否定因素 | 详情 |
|---------|------|
| 源码变更风险 | M1-M8 C++ 主题名称全部硬编码为字符串字面量,无 `declare_parameter` 可覆盖。修改字符串可导致意外行为 |
| 同时需修复 5 个内部不匹配 | F1-F5 的修复与主题重命名交叠,问题定位复杂度倍增 |
| DEMO-1 后回滚成本 | 评审后发版(Phase 1→2 过渡)需改回架构标准命名,回滚触发全量 colcon rebuild |
| 与 §15.2 不一致 | 架构文档接口契约表已固化 `/fusion/*` 命名。改源码使其与文档不一致,审计风险 |

### 5.2 选用的方案:ros2_topic_bridge 桥接

| 优势 | 详情 |
|------|------|
| L3 内核零修改 | Dockerfile 中 M1-M8 C++ 包 colcon build,但源码不因桥接改动一行 |
| 独立可测 | Bridge 节点可在 SIL 容器内单独测试:注入 `/sil/*` 模拟数据,验证 `/fusion/*` 输出 |
| 可旁路 | `SIL_L3_ENABLE=0` 环境变量可完全绕过 bridge,回退到纯 sim 模式 |
| 集中管理 | 所有 topic 映射关系集中在 `docker/sil_topic_bridge.py` 一处,修改无需触及 L3 内核 |
| 成本 | 单个 Python 进程(~5 MB 内存,~1-5ms 额外 DDS 延迟),对 DEMO-1 可忽略 |

### 5.3 架构一致性

- SIL 侧 topic 统一 `/sil/*` 前缀 — 前端只消费 `/sil/*`,foxglove whitelist 已限定
- L3 侧 topic 保持架构文档 §15.2 定义 — `/fusion/*` (外部输入) + `/l3/m{N}/*` (模块间)
- Bridge 在 sil-nodes 容器内运行,与 sim 节点和 L3 节点共享同一 DDS domain,不需要跨容器 DDS 通信

---

## 6. 架构报告 §15.2 接口契约引用

以下 L3 接口在本映射中涉及的架构报告 §15.2 行列:

| §15.2 行 | 发布者→订阅者 | 本映射中对应 |
|---------|------------|------------|
| Multimodal Fusion → M2 (FilteredOwnShipState) | 外部感知融合→M2 | Bridge 将 `/sil/own_ship_state` 转换为 `/fusion/own_ship_state`,模拟 Fusion 输入 |
| Multimodal Fusion → M2 (TrackedTargetArray) | 外部感知融合→M2 | Bridge 将 `/sil/target_vessel_state` 转换为 `/fusion/tracked_targets`,模拟 Fusion 输入 |
| Multimodal Fusion → M2 (EnvironmentState) | 外部感知融合→M2 | Bridge 将 `/sil/environment` 转换为 `/fusion/environment_state`,模拟 Fusion 输入 |
| M5 Mid-MPC → L4 (AvoidancePlanMsg) | M5→下游引导层 | Bridge 将 `/l3/m5/avoidance_plan` 转换为 `/sil/actuator_cmd`,模拟 L4→执行器 |
| M1-M7 → ASDR (ASDR_RecordMsg) | 各 M 模块→ASDR | Bridge 将 `/l3/asdr/record` 转换为 `/sil/asdr_event`,供前端消费 |
| M1-M7 → M8 (SAT_DataMsg) | 各 M 模块→M8 | Bridge 将 `/l3/m8/ui_state` 转发到 `/sil/m8_ui_state`,供前端消费 |
| M8 → ROC/Captain (UI_StateMsg) | M8→操作员 | 通过 `/sil/m8_ui_state` 到达前端,架构定义的透明性链路 |

---

## 7. 主题命名决策原则总结

1. **SIL 侧统一 `/sil/*` 前缀**:前端只消费 `/sil/*`,foxglove whitelist 限定
2. **L3 侧保持架构标准**:不修改 L3 内核源码(F1-F5 修复除外),保持与 §15.2 一致
3. **Bridge 负责转换**:类型/单位/结构差异在 bridge 节点内适配
4. **透传不经过 bridge**:`/sim_clock`, `/sil/scoring`, `/sil/lifecycle_status`, `/sil/tracked_targets` 已在正确格式
5. **故障相关主题 DEMO-1 排除**:`/sil/fault/*` (3 个子主题) 不涉及故障注入场景
6. **前端专用主题 DEMO-1 stub**:`/sil/sensor_status`, `/sil/commlink_status` 由生命周期管理,不桥接
