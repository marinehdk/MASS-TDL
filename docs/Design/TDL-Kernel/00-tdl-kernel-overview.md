# TDL Kernel · 8 模块设计 + 进度联动 · Overview

| 属性 | 值 |
|---|---|
| 范围 | MASS L3 战术决策层 8 个 ROS2 native 模块（M1–M8）|
| 对应代码 | `src/l3_tdl_kernel/m{1..8}_*` colcon packages |
| 文档双轨说明 | 本目录是**模块设计导向**第二套文档；**任务开发导向**第一套在 `Phase 1/D{x.y}-*/`、`Phase 2/`、`Phase 3/`（按需建）。两套通过 `M{n}-progress.md` 中的 "Currently Implementing / Closed in / Blocks" D 任务联动表互联。|

---

## 8 模块全景

| M | 模块 | 职责 | 时间尺度 | 主 ROS topic | 当前实现 |
|---|---|---|---|---|---|
| M1 | ODD/Envelope Manager | 调度枢纽 + "当前安全语境"权威 | 长时（0.1–1 Hz）| `/l3/m1/odd_state` | 🟡 1669 LOC |
| M2 | World Model | 唯一权威世界视图 + COLREG 几何预分类 | 短时（10–50 Hz）| `/l3/m2/world_state` | 🟡 1860 LOC |
| M3 | Mission Manager | 航次计划、ETA、重规划触发 | 长时 | `/l3/m3/mission_goal` | 🟡 1488 LOC |
| M4 | Behavior Arbiter | IvP 多目标行为仲裁 | 中时（1–4 Hz）| `/l3/m4/behavior_plan` | 🔴 476 LOC + IvP solver；未发 SAT-2 |
| M5 | Tactical Planner | Mid-MPC + BC-MPC，输出 (ψ, u, ROT) | 中时 + 短时 | `/l3/m5/avoidance_plan` | 🔴 CasADi 集成规划；未发 SAT-3 |
| M6 | COLREGs Reasoner | Rule 推理（ODD-aware）| 中时 | `/l3/m6/colregs_constraint` | 🟡 976 LOC（Rule 5-19 真实）|
| M7 | Safety Supervisor | Doer-Checker Checker，IEC 61508 + SOTIF | 短时 | `/l3/m7/safety_alert` | 🟡 assumption_monitor 在；PATH-S 独立性未验 |
| M8 | HMI/Transparency Bridge | 唯一对 ROC/船长说话的实体，SAT-1/2/3 | 实时（50–100 Hz）| `/sil/own_ship/target_vessel/module_pulse/...` 12 个 | 🟡 C+++FastAPI 双进程；缺 SAT-2/3/SOTIF 桥 |

**状态图例**：✅ 完整 / 🟡 部分 / 🔴 未做或核心缺失 / ⚫ 未验证

---

## 每模块文档结构

```
M{n}-XXX/
├── M{n}-spec.md      模块功能 + 接口契约 + 当前实现状态摘要（指回 Archive 全文）
└── M{n}-progress.md  D 任务联动：Closed in / Currently Implementing / Blocks 三栏表
```

**M{n}-spec.md** 不重写完整详设，只提炼"接口契约 / SIL 等级 / 关键 ADR / 当前 LOC + 真实 topic"四块要点 + 指针指向 `Archive/Old Modules/M{n}-*/01-detailed-design.md` 完整版（暂不复活成现役）。

**M{n}-progress.md** 是 D 任务联动表，PR 合并到对应 M 模块时更新：
- **Closed in D{x.y}**：该 D 任务已结束并产出此模块的某能力
- **Currently Implementing D{x.y}**：当前活跃 D 任务正在加码
- **Blocks D{x.y}**：本模块功能未到位会卡住的下游 D 任务

---

## 跨模块强约束（IDL 契约）

所有模块间消息强制：
- `stamp`（rclpp Time）
- `schema_version`（v3.0 D0.1 后强制）
- `confidence ∈ [0, 1]`（多源融合时携带）
- `rationale`（M8 SAT-2 汇聚时需要）

详见 [Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) §15 IDL 矩阵。

---

## 顶层架构决策（影响所有模块的 4 条不可让步项）

1. **ODD 是组织原则，不是监控模块** — M1 ODD 状态变化是行为切换的唯一权威源
2. **Doer-Checker 双轨** — M1–M6 是 Doer，M7 是 Checker，逻辑简化 ≥100× + 实现路径独立
3. **CMM 通过 SAT-1/2/3 接口对外可见** — 每模块实现 `current_state() / rationale() / forecast(Δt) + uncertainty()` 三调用，M8 聚合
4. **多船型 = Capability Manifest + PVA + 水动力插件**（Backseat Driver） — 决策核心零船型常量

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版（v3.2 重构时新建 TDL-Kernel 第二套文档树）|
