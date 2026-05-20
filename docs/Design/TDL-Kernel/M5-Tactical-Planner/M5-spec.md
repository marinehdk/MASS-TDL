# M5 · Tactical Planner · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M5 |
| 职责一句话 | Mid-MPC（≥90s）+ BC-MPC（13 候选弧线），输出 (ψ_cmd, u_cmd, ROT) 至 L4 |
| 时间尺度 | 中时（Mid 1-2 Hz）+ 短时（BC 4-10 Hz）|
| SIL 等级 | SIL 1（执行路径，被 M7 Checker 兜底）|
| 实现路径分类 | PATH-D |
| colcon 包 | `src/l3_tdl_kernel/m5_tactical_planner` |
| 当前 LOC | CasADi 集成规划阶段 |
| 真实发布 topic | `/l3/m5/avoidance_plan` ⚫ 未验证 |
| 完整详设 | [Archive/Old Modules/M5-Tactical-Planner/01-detailed-design.md](../../Archive/Old%20Modules/M5-Tactical-Planner/01-detailed-design.md) |

---

## 接口契约

### 上游订阅
- M2 WorldStateMsg @ 50 Hz
- M4 BehaviorPlanMsg @ 4 Hz
- M6 ColregsConstraintMsg

### 下游发布
- `l3_external_msgs/AvoidancePlanMsg` → L4 Guidance Layer（含 ψ_cmd / u_cmd / ROT / 9 个置信度字段 / IPOPT KKT residual）
- **DEMO-2 P0 NEW**：`/sil/sat3_data.trajectory_candidates[]`（Mid-MPC 主轨迹 + BC-MPC 13 候选弧线，前端 MpcTrajectoryLayer 数据源）

### 关键参数（v3.0 修订全部锁定）
- Mid-MPC N=18 / 90s（MUST-2 三处统一，RFC-001 锁定）
- BC-MPC SLA < 150ms / 8Hz（B P0-B-04）
- ROT_max 曲线读 Capability Manifest（**严禁硬编**）
- FM-4 hardcoded fallback **删除**（MUST-5）
- MRM 走 M7 路径（MUST-9）
- urgency_level > 0.95 时 BC-MPC 扩 ±60°（P2-B-01）

---

## DEMO-2 P0 提前要求

D3.2 原计划 7/13 起，目标 7/27 提供 trajectory_candidates 输出（哪怕初版用线性化 Nomoto + N=12 兜底，13 几何分支无完整最优化），以供 7/31 DEMO-2 现场 Engineer 视图 MpcTrajectoryLayer 渲染。DEMO-3 (8/31) 升级 CasADi/IPOPT 完整。

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node | 🔴 未验证 |
| CasADi/IPOPT 集成 | 🔴 规划阶段 |
| Mid-MPC 90s 弧线 | 🔴 未做 |
| BC-MPC 13 候选 | 🔴 未做 |
| `/sil/sat3_data.trajectory_candidates[]` 发布（DEMO-2 P0）| 🔴 未做 |
| ROT_max 读 Manifest（MUST-5 验证）| ⚫ 未验 |

---

## 关联 D 任务（详见 [M5-progress.md](M5-progress.md)）

- **Closed in**：D0.1 surgical（MUST-2/5/9）
- **计划中**：D3.2 M5 完整（Phase 3 起，目标 8/10；DEMO-2 P0 提前部分输出 7/27）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
