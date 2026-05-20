# M6 · COLREGs Reasoner · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M6 |
| 职责一句话 | Rule 推理（ODD-aware 参数）+ 5 层决策溯源 |
| 时间尺度 | 中时（1 Hz）|
| SIL 等级 | SIL 1 |
| 实现路径分类 | PATH-D |
| colcon 包 | `src/l3_tdl_kernel/m6_colregs_reasoner` |
| 当前 LOC | ~976 |
| 真实发布 topic | `/l3/m6/colregs_constraint` ✅ |
| 完整详设 | [Archive/Old Modules/M6-COLREGs-Reasoner/01-detailed-design.md](../../Archive/Old%20Modules/M6-COLREGs-Reasoner/01-detailed-design.md) |

---

## 接口契约

### 上游订阅
- M1 ODD_StateMsg（决定适用规则集）
- M2 WorldStateMsg（几何预分类输入）

### 下游发布
- `l3_msgs/ColregsConstraintMsg` @ 1 Hz（含 5 层 decision chain）
- **DEMO-2 P0 NEW**：`/sil/sat2_data.colregs_chain[5]` 序列化（前端 ColregsRationaleTree 数据源）— M6 推理已真实，**仅缺 schema 序列化**

### 5 层决策溯源（架构 §9）
1. ODD → 适用规则集（如 ODD-A → Rule 5,6,7,8,13,14,15,16,17,18）
2. 会遇识别（Rule 13/14/15 判定 + 几何参数）
3. 责任划分（GIVE_WAY / STAND_ON / Rule 16,17）
4. 方向（STARBOARD ≥30° / PORT / Rule 8 "大幅早行动"）
5. 时机（STAGE_1/2/3，TCPA 阈值）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node + topic | ✅ |
| Rule 5-19 推理引擎 | ✅ |
| 5 层 decision chain 内部生成 | ✅ |
| **SAT-2 colregs_chain[5] 序列化输出** | 🔴 **DEMO-2 P0 缺失（M6 已算，缺序列化）** |

---

## DEMO-2 P0 需求

D2.4 Scope 新增子条目（v3.2 patch）："M6 5 层 colregs_chain SAT-2 序列化输出"——估算 0.5pw 在 D2.4 内吸收。M6 推理已真，仅需 IDL schema + publish 接口。

---

## 关联 D 任务（详见 [M6-progress.md](M6-progress.md)）

- **Closed in**：（无 v3.0 修订）
- **计划中**：D2.4 M6（Phase 2，目标 7/31；含 SAT-2 序列化子条目）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
