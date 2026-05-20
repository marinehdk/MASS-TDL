# M1 · ODD / Envelope Manager · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M1 |
| 职责一句话 | 整个 TDL 的调度枢纽，唯一的"当前安全语境"权威 |
| 时间尺度 | 长时（0.1–1 Hz）|
| SIL 等级 | **SIL 2**（核心安全功能：模式仲裁、ToR 触发）|
| 实现路径分类 | PATH-S（严格路径，与 M7 并列）|
| colcon 包 | `src/l3_tdl_kernel/m1_odd_envelope_manager` |
| 当前 LOC | ~1669 |
| 真实发布 topic | `/l3/m1/odd_state` |
| 完整详设 | [Archive/Old Modules/M1-ODD-Envelope-Manager/01-detailed-design.md](../../Archive/Old%20Modules/M1-ODD-Envelope-Manager/01-detailed-design.md)（未复活为现役）|

---

## 接口契约（IDL）

### 上游订阅
- `l3_msgs/EnvironmentMsg`（M2 + 外部传感器融合）
- `l3_msgs/Safety_AlertMsg`（M7 SOTIF / IEC 61508 告警）
- `l3_external_msgs/CapabilityManifest`（Parameter Database）

### 下游发布
- `l3_msgs/ODD_StateMsg` @ 1 Hz（含 odd_zone {A/B/C/D} + health_state {NOMINAL/DEGRADED/CRITICAL/OUT_of_ODD}）
- `l3_msgs/ToR_RequestMsg`（M8 → ROC/船长）含 `assumed_operator_state` 字段（v3.0 修订）

### 关键字段
- `tor_deadline_s` 自适应矩阵（ROC 已坐席 60s / 桥楼 30s / 餐厅 90s / 睡舱 120s）— **v3.0 D2.1 新增，HAZID 8/19 后校准**
- `ROT_max(u)` 曲线读 Capability Manifest（**严禁硬编**）

---

## 顶层约束

- ODD 状态变化是**唯一**的行为切换权威源（ADR-001）
- 进入 OUT_of_ODD 强制触发 MRM（通过 M7 路径，v3.0 MUST-9 修订）
- PATH-S CI 独立性检查：M1 不得依赖 M7 内部头文件（反之亦然）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| ROS2 node 启动 + heartbeat | ✅ |
| `/l3/m1/odd_state` topic 发布 | ✅ |
| Capability Manifest 集成 | 🔴 未见 |
| FMEDA M1 表 v0.1（≥ 20 失效模式）| 🔴 未见 |
| ToR 自适应矩阵（4 场景）| 🔴 未见 |
| OUT_of_ODD → MRM 走 M7 路径 | ⚫ 未验 |

---

## 关联 D 任务（详见 [M1-progress.md](M1-progress.md)）

- **当前活跃**：（无）
- **计划中**：D2.1 M1 完整实现（Phase 2，目标 7/6）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub（v3.2 重构时新建）|
