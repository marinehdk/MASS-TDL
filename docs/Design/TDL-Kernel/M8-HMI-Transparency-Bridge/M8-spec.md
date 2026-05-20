# M8 · HMI / Transparency Bridge · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M8 |
| 职责一句话 | 唯一对 ROC/船长说话的实体，SAT-1/2/3 透明性聚合 + ToR + ASDR + dual-role |
| 时间尺度 | 实时（50–100 Hz）|
| SIL 等级 | SIL 1（外向接口，安全关键路径但非 IEC 61508 核心）|
| 实现路径分类 | PATH-D（M8 裁剪 ~120 MISRA 规则，比 PATH-D 稍紧）|
| colcon 包 | `src/l3_tdl_kernel/m8_hmi_transparency_bridge`（C++ ROS LifecycleNode + Python FastAPI 双进程拓扑）|
| 真实发布 topic | `/sil/own_ship/target_vessel/module_pulse/asdr_event/scoring/lifecycle/control_cmd/env/...` 共 12 个 |
| 缺失发布 | `/sil/sat2_data` + `/sil/sat3_data` + `/sil/sotif_metrics` ← **DEMO-2 P0** |
| 完整详设 | [Archive/Old Modules/M8-HMI-Transparency-Bridge/01-detailed-design.md](../../Archive/Old%20Modules/M8-HMI-Transparency-Bridge/01-detailed-design.md) |

---

## v3.0 工时上调原因

v2.0 → v3.0：4.0 → 5.5 pw（+1.5），含：
- active_role 对称双角色完整实现（MUST-7 stub → 本 D 实现）
- UI 50Hz 拆 3 档（data_stream_50hz / display_state_4hz / alert_burst_event）
- ToR 自适应矩阵 4 场景
- BNWAS-equivalent stub
- Y-axis Reflex Arc → M8 通告通路
- ECDIS 集成 stub
- URGENT_TOR < 30s 子分支
- BackupChannel_DisplayMsg
- 5s 强制等待条件分支

---

## v3.1 Web HMI Phase 3 增项（D3.4）

- Trajectory ghosting（M5 提议 vs L2 计划虚实双轨叠加）
- ToR countdown panel（独立非雷达内）+ 4 状态适配
- M7 Doer-Checker verdict badge + 决策链文字 rationale
- S-Mode 完整对齐（IMO MSC.1/Circ.1609）
- 1000 场景 evidence pack 全自动产出
- Doer-Checker 独立可视化通道（`/m7/sil_observability` 只读 ROS2 topic）
- ECDIS 完整集成（IHO S-100 / IEC 61174 + S-57 双兼容）

---

## DEMO-2 P0：SAT-2/3/SOTIF 桥接

**问题**：前端 [SimulationMonitor.tsx](../../../web/src/screens/SimulationMonitor.tsx) 已 mount Engineer 视图 4 个组件（IvpRiskGradientLayer / MpcTrajectoryLayer / ColregsRationaleTree / SotifMonitorStrip），Zustand store [sat.ts](../../../web/src/store/sat.ts) 类型齐备；M4/M6/M7 上游 source data 可获得；但 M8 未提供 SAT-2/3 + SOTIF metrics 桥接 topic，前端渲染全 null。

**P0 需求（拆出 D3.4 提前到 7/31 前）**：
- IDL：`l3_msgs/SAT2Data` + `l3_msgs/SAT3Data` + `l3_msgs/SotifMetrics`
- 发布：`/sil/sat2_data`（ivp_contributions[] + colregs_chain[5]）+ `/sil/sat3_data`（trajectory_candidates[]）+ `/sil/sotif_metrics`（6 指标）
- 与 `web/src/store/sat.ts` 类型一一对齐
- 估算：1.5 pw（从 D3.4 整体 5.5pw 拆出）

---

## 接口契约

### 上游订阅（聚合）
- M1 ODD_StateMsg / M2 WorldStateMsg / M4 BehaviorPlanMsg / M5 AvoidancePlanMsg / M6 ColregsConstraintMsg / M7 Safety_AlertMsg

### 下游发布
- ROS2 native：12 个 `/sil/*` topic（DEMO-2 P0 后扩至 15）
- WebSocket：`foxglove_bridge` subprotocol foxglove.sdk.v1（前端 `useFoxgloveLive.ts` 订阅）
- REST：FastAPI `/api/v1/*`（demo telemetry / lifecycle / fault inject 等）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| C++ ROS LifecycleNode | ✅ |
| Python FastAPI 后端 | ✅ |
| 12 个 SAT-1 级 topic 真实发布 | ✅ |
| foxglove_bridge subprotocol foxglove.sdk.v1 | ✅ |
| ToR Modal ≥2s 物理锁（前端 [TorModal.tsx](../../../web/src/components/TorModal.tsx)）| ✅ |
| active_role `active_role.py` 存在 | 🟡 完整对称未验 |
| ToR 自适应矩阵 4 状态 | 🔴 未做 |
| **SAT-2/3/SOTIF 桥接 topic** | 🔴 **DEMO-2 P0 阻塞** |

---

## 关联 D 任务（详见 [M8-progress.md](M8-progress.md)）

- **Closed in**：D0.1 MUST-7（active_role stub）
- **计划中**：D3.4 M8 完整（目标 8/24；SAT 桥接子项必须 7/31 前到位，拆出独立 1.5pw）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
