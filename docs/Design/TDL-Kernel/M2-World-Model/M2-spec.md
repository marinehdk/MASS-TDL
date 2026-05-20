# M2 · World Model · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M2 |
| 职责一句话 | 唯一权威世界视图，含 COLREG 几何预分类（Head-on / Crossing / Overtaking）|
| 时间尺度 | 短时（10–50 Hz）|
| SIL 等级 | SIL 1（感知融合，非核心安全功能但是关键路径）|
| 实现路径分类 | PATH-D |
| colcon 包 | `src/l3_tdl_kernel/m2_world_model` |
| 当前 LOC | ~1860（含 9 个 .cpp）|
| 真实发布 topic | `/l3/m2/world_state` + 间接 `/sil/world_state` |
| 完整详设 | [Archive/Old Modules/M2-World-Model/01-detailed-design.md](../../Archive/Old%20Modules/M2-World-Model/01-detailed-design.md) |

---

## 接口契约

### 上游订阅
- Multimodal Fusion（外部 → TrackedTargetArray）
- Nav Filter（外部 → 自船 15-state EKF）
- `l3_external_msgs/EnvironmentMsg`（风/流/能见度/海况）

### 下游发布
- `l3_msgs/WorldStateMsg` @ 50 Hz：自船 + N 目标 + CPA/TCPA + BCR/BCT + COLREG 几何预分类（hdg_diff / bearing / range / colreg_class）
- 含 `intent_distribution[]` 字段（v3.0 修订，B P1-B-02 整改）

### 关键字段（v3.0 修订）
- `sog` 校验 `f(Manifest.max_speed × 1.2)`（D0.1 修订，**不得硬编**）
- OVERTAKING 扇区 `[112.5°, 247.5°]`（D0.1 修订，单元测试 4 边界）
- 环境字段 sanity check（visibility/Hs/current 范围 + 跨源 + staleness）
- `confidence ∈ [0, 1]` 全字段

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node + 50 Hz 发布 | ✅ |
| CPA/TCPA 真实计算（C++ Eigen）| ✅ |
| COLREG 几何预分类 | ✅ |
| sog 校验改读 Manifest（MUST-6）| ⚫ 未验证 |
| OVERTAKING 扇区修订（MUST-1）| ⚫ 未验证 |
| `intent_distribution` 字段 | 🔴 未见 |
| 环境字段 sanity check | 🔴 未见 |
| BRG/RNG per-target 输出（ARPA 表用）| 🔴 未见 |

---

## DEMO-2 P1 需求

ARPA 表 BRG/RNG 列当前前端硬编码 "—"（[ArpaTargetTable](../../../web/src/components/ArpaTargetTable.tsx)），需 M2 输出。

---

## 关联 D 任务（详见 [M2-progress.md](M2-progress.md)）

- **Closed in**：D0.1 surgical（OVERTAKING 扇区 + sog 校验）
- **计划中**：D2.2 M2 完整 v3.0 修订（Phase 2，目标 7/6）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
