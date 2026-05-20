# M4 · Behavior Arbiter · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M4 |
| 职责一句话 | 行为字典 + IvP 多目标仲裁（5 行为权重→胜出行为）|
| 时间尺度 | 中时（1–4 Hz）|
| SIL 等级 | SIL 1 |
| 实现路径分类 | PATH-D |
| colcon 包 | `src/l3_tdl_kernel/m4_behavior_arbiter` |
| 当前 LOC | ~476（含 `ivp_solver.cpp`）|
| 真实发布 topic | `/l3/m4/behavior_plan` ⚫ 实际未验证发布 |
| 完整详设 | [Archive/Old Modules/M4-Behavior-Arbiter/01-detailed-design.md](../../Archive/Old%20Modules/M4-Behavior-Arbiter/01-detailed-design.md) |

---

## 接口契约

### 上游订阅
- M1 ODD_StateMsg
- M2 WorldStateMsg
- M6 ColregsConstraintMsg

### 下游发布
- `l3_msgs/BehaviorPlanMsg` @ 4 Hz（含胜出行为名 + 权重分布 + confidence）
- **DEMO-2 P0 NEW**：`/sil/sat2_data.ivp_contributions[]` @ 4 Hz（前端 IvpRiskGradientLayer 8 方向风险梯度向量数据源）

### 行为字典（架构 §8.3）
| 行为 | 权重 |
|---|---|
| Transit | 0.3 |
| COLREGs_Avoidance | 0.7 |
| Restricted_Visib. | 0.6 |
| DP_Hold | 0.8 |
| MRC_Drift | 1.0 |

---

## 关键决策

- **IvP 实现路径**：D0.2 RFC-009 拍板 = **自实现方向**（避开 libIvP GPL/LGPL 许可不确定性）
- D2.1 sign-off 工时修订：M4 实装从 5.5pw 升至 **7.0pw**（+1.5pw）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node | 🔴 未验证 |
| `ivp_solver.cpp` 文件存在 | ✅ |
| `/l3/m4/behavior_plan` 发布 | ⚫ 未验 |
| `/sil/sat2_data.ivp_contributions[]` 发布（DEMO-2 P0）| 🔴 未做 |
| confidence 字段 ASDR 写入 | 🔴 未见 |

---

## DEMO-2 P0 提前要求

D3.1 原计划 7/13 起，目标 7/26 提供至少 stub 输出（哪怕固定权重表，不跑真实 IvP 求解），以供 7/31 DEMO-2 现场 Engineer 视图 IvpRiskGradientLayer 渲染非 null。

---

## 关联 D 任务（详见 [M4-progress.md](M4-progress.md)）

- **Closed in**：D0.2 RFC-009 决议
- **Currently Implementing**：`feat/d3.1-m4-behavior-arbiter` 分支（Phase 3 提前到 Phase 2 末）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
