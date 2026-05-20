# M7 · Safety Supervisor · Spec

| 属性 | 值 |
|---|---|
| 模块代号 | M7 |
| 职责一句话 | Doer-Checker 中的 Checker，IEC 61508 + SOTIF 双轨；6 类硬约束 + 5 类假设违反监控 |
| 时间尺度 | 短时（< 10 ms 端到端）|
| SIL 等级 | **SIL 2**（核心安全功能）|
| 实现路径分类 | **PATH-S**（严格路径，与 M1 并列）|
| colcon 包 | `src/l3_tdl_kernel/m7_safety_supervisor`（含 arbitrator/checker/sotif/mrm/iec61508 八子目录）|
| 当前 LOC | `assumption_monitor.cpp` + 多子目录 |
| 真实发布 topic | `/l3/m7/safety_alert` ✅ |
| 完整详设 | [Archive/Old Modules/M7-Safety-Supervisor/01-detailed-design.md](../../Archive/Old%20Modules/M7-Safety-Supervisor/01-detailed-design.md) + [02-effort-split-v2.1.md](../../Archive/Old%20Modules/M7-Safety-Supervisor/02-effort-split-v2.1.md) |

---

## v3.0 拆分（MUST-11，6→9 pw）

- **M7-core**（6 pw，D3.3a）：6 类硬约束 + IEC 61508 watchdog + ASDR SHA-256 + Doer-Checker 三量化矩阵 + FMEDA M7 表
- **M7-sotif**（3 pw，D3.3b）：5 类假设违反检测 + 100 周期 = 15s 滑窗（RFC-003）+ enum-only veto + SOTIF area mapping + ISO 21448 §6 穷举证据

---

## 接口契约

### 上游订阅（监督所有 Doer 模块）
- M2 WorldStateMsg / M4 BehaviorPlanMsg / M5 AvoidancePlanMsg / M6 ColregsConstraintMsg

### 下游发布
- `l3_msgs/Safety_AlertMsg` → M1（仲裁）+ M8（HMI 告警）
- `l3_msgs/CheckerVetoNotification` → M4/M5（强制退回 nominal）
- **DEMO-2 P1 NEW**：`/sil/sotif_metrics` 6 指标聚合（前端 SotifMonitorStrip 数据源）

### 6 类硬约束（< 10ms 端到端）
1. CPA 最小距离
2. UKC（under-keel clearance）
3. ROT 上限
4. 速度上限
5. ODD 边界
6. MRM 触发条件

### 5 类 SOTIF 假设违反
1. AIS / Radar 不一致 > 2σ
2. 目标可预测性 RMS > 50m / 30s
3. 感知盲区超比 > 20%
4. COLREGs 解析失败 ≥ 3 次连续
5. 通信链路 RTT > 2s / 丢包 > 20%

---

## Doer-Checker 三量化矩阵（D3.3a）

- LOC 比 ≥ 50:1（M7 比 Doer 模块极简）
- 圈复杂度比 ≥ 30:1
- SBOM ∩ = ∅（实现路径完全独立，不共享代码/库/数据结构）

---

## 当前实现状态（2026-05-20）

| 子能力 | 状态 |
|---|---|
| 独立 ROS2 进程 + heartbeat（Gate 6 验证）| ✅ |
| `/l3/m7/safety_alert` 真实发布 | ✅ |
| `assumption_monitor.cpp` 存在（`feat/d3.3b-m7-sotif` 分支，1 commit）| ✅ |
| 6 类硬约束完整 | 🔴 未做 |
| FMEDA M7 表 ≥ 20 失效模式 | 🔴 未做 |
| PATH-S CI 0 violation 自动验证 | ⚫ 未验 |
| `/sil/sotif_metrics` 6 指标聚合（DEMO-2 P1）| 🔴 未做 |

---

## 关联 D 任务（详见 [M7-progress.md](M7-progress.md)）

- **Currently Implementing**：`feat/d3.3b-m7-sotif`（1 commit）
- **计划中**：D3.3a M7-core（目标 8/10）+ D3.3b M7-sotif（目标 8/16）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-20 | 初版 spec stub |
