# M7 · Safety Supervisor · Progress

> **定位声明**：本文是 M7 的**实现现状**，对照 M7-spec.md 的设计目标。所有偏离 / 创可贴 / MOCK / 死代码记录于此，均附当前代码 file:line。审计基线：`docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（本模块条目已并入下表，但以**当前代码**为准，已经 codegraph 核查）。

---

## 1. 头部状态

| 维度 | 值 |
|---|---|
| 最近更新 | 2026-06-08 |
| Currently Implementing | — |
| 当前分支 | `fix/m6-colregs-convergence`（M7 主节点文件在此分支有修改）|
| 节点入口 | `src/l3_tdl_kernel/m7_safety_supervisor/src/safety_supervisor_node.cpp` |
| 参考审计 | `docs/Doc From Claude/2026-06-08-m1-m8-systemwide-gap-audit.md`（基线 158bba9d；M7 条目已并入本文）|

---

## 2. 实现状态矩阵

| 设计职责（对应 spec §） | 状态 | 证据 file:line | 备注 |
|---|---|---|---|
| **HC-1 CPA 最小距离检查** | STUB | `safety_supervisor_node.cpp:548-552`（`run_hard_constraint_checks` = `(void)now;`）；`core/hard_constraint_cpa.cpp` 有 `compute_cpa_m7()` 但从不被生产路径调用 | 函数体完全空，单行 no-op |
| **HC-2 UKC（under-keel clearance）检查** | MISSING | 无 `hard_constraint_ukc.cpp/.hpp`；`mrm_chain_executor.cpp` HC 编号映射中无 UKC 条目 | 文件完全不存在 |
| **HC-3 ROT 上限检查** | STUB | `core/hard_constraint_rot.cpp` 有 `check_rot_limit()`；从不被生产路径调用（仅单测调用）| 逻辑存在，死代码 |
| **HC-4 速度上限检查** | STUB | `core/hard_constraint_speed.cpp` 有 `check_speed_limit()`；同 HC-3 | 逻辑存在，死代码 |
| **HC-5 ODD 边界独立检查** | MISSING | M7 源码无 ODD 边界 HC 函数；`mrm_chain_executor.cpp` 无对应条目 | M1 做 ODD 评分，但 M7 的独立二次校验不存在 |
| **HC-6 MRM 触发条件** | PARTIAL | `mrm/mrm_selector.cpp` `MrmSelector.select()` 有完整 MRM 逻辑，`run_monitor_evaluation:452` 调用 | 但 HC 输入（CPA/ROT/speed）因 HC stub 永不传入；MRM 仅由 SOTIF/看门狗驱动 |
| **IEC 61508 看门狗（M1–M6 心跳检测）** | REAL | `iec61508/watchdog_monitor.cpp:66-99` `evaluate()`；每条订阅回调调用 `on_message_received()`；`run_monitor_evaluation:425` 调用 | 实现完整，超时配置合理（期望周期 × 1.5）|
| **IEC 61508 故障监控（FaultMonitor）** | REAL | `iec61508/fault_monitor.cpp:65-89` `run()`；`run_monitor_evaluation:426` 调用；检查 ODD 合规域/CPA 符号/COLREGs 一致性 | 3 项检查均有实现 |
| **SOTIF 假设 #1 AIS/雷达一致性** | PARTIAL | `sotif/assumption_monitor.cpp:27-43` 有实现；由 `run_monitor_evaluation` 驱动 | 阈值为代理指标（WorldState.confidence），非真实 AIS 残差 |
| **SOTIF 假设 #2 运动可预测性** | PARTIAL | `assumption_monitor.cpp:49-74`；proxy = WorldState.confidence < 0.4；注释明确 `[TBD-HAZID-SOTIF-002]` | 待 M2 暴露预测 RMSE 字段 |
| **SOTIF 假设 #3 感知覆盖** | PARTIAL | `assumption_monitor.cpp:80-98`；proxy = unknown 目标比例；注释 `[TBD-HAZID-SOTIF-003]` | 待 M2 暴露 blind_zone_fraction |
| **SOTIF 假设 #4 COLREGs 可解析性** | REAL | `assumption_monitor.cpp:104-114`；colregs.confidence 连续失败计数 | 实现真实 |
| **SOTIF 假设 #5 通信链路** | STUB | `safety_supervisor_node.cpp:432` `CommLinkState const kCommLink{};`（零初始化）；注释：`[TBD-comm-monitor]`；函数 `check_comm_link()` 存在但输入永远为零 | 结构已接入但无发布方 |
| **SOTIF 假设 #6 X-axis 否决率** | PARTIAL | `sotif/checker_veto_counter.cpp` 实装；`run_monitor_evaluation:427` 调用；但 `/l3/checker/veto` 无任何生产发布方 → rate 恒为 0 | 统计逻辑完整，数据源断流 |
| **MRM 命令集（4 类）** | REAL | `mrm/mrm_01_drift.cpp` 至 `mrm_04_mooring.cpp`；`MrmSelector.select()` 真实实现 | 完整 |
| **ASDR SHA-256 审计轨迹** | REAL | `common/sha256.cpp`；`alert_generator.cpp:44-47`；2 Hz 定时 `on_asdr_periodic_tick()` | 完整 |
| **ResumeHandler（接管回切）** | REAL | `core/resume_handler.cpp`；`on_override_signal()` 调用；`mrm_selector_->reset()` 在 override 退出时调用 | 完整 |
| **CheckerVetoNotification 发布（VETO 硬门）** | MISSING | `safety_supervisor_node.hpp:68-71` 仅 4 个 publisher：pub_alert_, pub_asdr_, pub_sat_, pub_heartbeat_；无 pub_veto_；M4/M5 无订阅 `/l3/checker/veto` | ADR-2 核心功能缺失 |
| **SotifMetricsPublisher（/sil/sotif_metrics）** | STUB | `sotif/sotif_metrics_publisher.hpp:21` `stub_mode_{true}`；`sotif_metrics_publisher.cpp:29-37` stub 分支硬零所有数值；`set_stub_mode(false)` 无生产调用方 | 格式正确但语义为空 |
| **CMM 三接口（current_state/rationale/forecast）** | MISSING | M7 节点无 `current_state()` ROS2 服务/topic；`/l3/sat/data` 提供部分 rationale 代理但非标准 CMM 接口；forecast 字段硬编码空 | M8 无法按 CMM 契约聚合 M7 状态 |
| **Doer-Checker LOC/CC 比自动验证** | MISSING | SBOM 独立性由 `checker_verification.py:66-86` 检查（no OR-Tools）；LOC/CC 比无 CI 工具 | 审计基线 spec 标 ⚫ 未验 |
| **SafetyAlert schema_version 填充** | MISSING | `alert_generator.cpp:17-30` 无 schema_version 赋值；`safety_arbitrator.cpp` 亦无；字段默认 0 | CMM 合规缺口 |
| **M4 BehaviorPlan 内容检查** | MISSING | `safety_supervisor_node.cpp:285-290` `on_behavior_plan` 参数为 `/*msg*/`（丢弃）；仅记 watchdog 心跳 | M7 从不检查 M4 行为约束违反 |

---

## 3. 接口实现对照

| topic | 设计（spec） | 实际 file:line | schema_version | confidence | rationale | 状态 |
|---|---|---|---|---|---|---|
| `/l3/m7/safety_alert` | 事件发布，severity > INFO | `safety_supervisor_node.cpp:461-468`；`publish_hard_constraint_alert:554-563`（死代码）| ❌ 永久 0（alert_generator.cpp:17-30 无赋值）| ✅ AlertCandidate.confidence 传入 | ✅ 静态字面量 rationale | 部分连通；HC 路径死代码；schema_version 缺失 |
| `/l3/m7/heartbeat` | 10 Hz | `safety_supervisor_node.cpp:526-530` | N/A（Header 无此字段）| N/A | N/A | 连通 |
| `/l3/asdr/record` | 2 Hz SHA-256 | `on_asdr_periodic_tick():495-517` | [TBD：需确认填充]| N/A | SHA-256 decision_json | 连通 |
| `/l3/sat/data` | 10 Hz SAT-2/3 | `on_sat_tick()` | [TBD：需确认]| ✅（sat2/3 各有 confidence）| sat2.reasoning_chain 周期 tick 时为空 | 部分连通；sat3 forecast 硬编码空 |
| `/sil/sotif_metrics` | 10 Hz，6 × SotifMetricEntry | `sotif_metrics_publisher.cpp:29-37` | ✅ schema_version=113（stub 分支）| is_violated 布尔 | assumption_id 字段 | STUB：violation_score/window_count/raw_value 全零 |
| `/l3/checker/veto` | 事件，HC 违反时发布 | **无对应 publisher 代码** | — | — | — | MISSING：M7 只订阅，不发布 |
| 订阅 `/l3/m4/behavior_plan` | 检查行为约束 | `safety_supervisor_node.cpp:285-290`（`/*msg*/` 丢弃）| — | — | — | 断流（内容丢弃）|
| 订阅 `/l3/m5/avoidance_plan` | 触发 HC 检查 | 订阅 ✅ `safety_supervisor_node.cpp:144-147`；但提取标量后立即清零 `:298-301`；HC stub | — | — | — | 断流（标量清零 + HC stub）|
| 订阅 `/l3/checker/veto` | 接收 X-axis 否决 | `safety_supervisor_node.cpp:174-178` ✅；VetoHandler + CheckerVetoCounter 处理 | — | — | — | 结构连通，但无生产发布方 → rate 恒 0 |

---

## 4. 已知缺陷（按严重度）

| 严重度 | 缺陷 | file:line | 类型 |
|---|---|---|---|
| CRITICAL | `/l3/checker/veto` 从不发布：M7 无 VETO 权，ADR-2 Doer-Checker 核心功能缺失 | `safety_supervisor_node.hpp:68-71`（4 publishers，无 veto）| MISSING |
| CRITICAL | `run_hard_constraint_checks()` 完全空壳：6 个 HC 函数全为死代码 | `safety_supervisor_node.cpp:548-552`（`(void)now;`）| 死代码/断流 |
| CRITICAL | M4/M5 均未订阅 `/l3/checker/veto`，无安全门（即使 M7 修复发布，下游无接收）| M4/M5 源码 grep: 0 结果 | 断流 |
| HIGH | HC-2 UKC 实现文件完全缺失（无 `hard_constraint_ukc.cpp/.hpp`）| 文件树确认缺失 | MISSING |
| HIGH | `/sil/sotif_metrics` 永久 stub_mode：violation_score/window_count/raw_value 全零 | `sotif_metrics_publisher.hpp:21`；`sotif_metrics_publisher.cpp:29-37` | STUB/MOCK |
| HIGH | SOTIF 假设 #5 CommLink 永久零输入：assumption 结构上永不触发 | `safety_supervisor_node.cpp:432`（`CommLinkState{}`）| 断流 |
| HIGH | AvoidancePlan 标量到达后立即被清零，HC 函数即使解 stub 也将读到全零输入 | `safety_supervisor_node.cpp:298-301` | 断流 |
| MEDIUM | M4 BehaviorPlan 内容完全丢弃，M7 不检查行为约束 | `safety_supervisor_node.cpp:285-290`（`/*msg*/`）| 脱节 |
| MEDIUM | SafetyAlert.schema_version 永久为 0：CMM 字段合规缺口 | `alert_generator.cpp:17-30`；`safety_arbitrator.cpp` 均无赋值 | 字段空 |
| MEDIUM | `/l3/safety/concern` 回调仅打日志，无实际处理：M5 Doer→Checker 事件上报链路断流 | `safety_supervisor_node.cpp:385-394` | 断流 |
| MEDIUM | Gate-6 veto 延迟测试硬编码 PASS：`run_veto_latency_test()` 无条件返回 True | `checker_verification.py:123-125` | MOCK/伪装 |
| LOW | `publish_hard_constraint_alert()` 无任何调用方（仅由 HC stub 路径"应当"调用）| `safety_supervisor_node.cpp:554-563` | 死代码 |
| LOW | `veto_enabled: false` 参数在 `l3_params.yaml` 中设置，但 M7 节点从不读取该参数 | `l3_params.yaml:39`；M7 节点 grep: 0 匹配 | 脱节 |

---

## 5. 创可贴 / 越界逻辑

与 M7 相关、当前在错误位置的逻辑：

| 当前位置 | 越界内容 | 正确归属 | 类型 |
|---|---|---|---|
| `docker/sil_topic_bridge.py`（无订阅 `/l3/m7/safety_alert`）| M7 SafetyAlert **不是** bridge 的硬门；CRITICAL 告警后 bridge 继续转发执行器命令 | M7→M1→M4/M5 的软门路径 + M7 发布 `/l3/checker/veto` 硬门 | CRITICAL/FLOW_GAP |
| `docker/sil_topic_bridge.py:477-479`（订阅 `/l3/checker/veto` 仅用于 debug trace）| Checker veto 仅记录不执行 | M4/M5 应订阅 `/l3/checker/veto` 并实施硬门（停止执行未校验 plan）| CRITICAL/FLOW_GAP |
| `src/sil_orchestrator/checker_verification.py:123-125` | Gate-6 veto 延迟测试永远返回 PASS | 应测量真实 M7→M4/M5 round-trip 延迟 < 50 ms | MOCK/伪装 |
| `src/sim_workbench/mock_publishers/l3_external_mock_publisher/l3_external_mock_publisher/external_mock_publisher.py:58` | mock 发布到 `/checker/veto_notification`（错误 topic 名）| 正确 topic 应为 `/l3/checker/veto` | MOCK/namespace 错 |

---

## 6. 设计-实现脱节（overclaim 修正）

旧 progress / spec 声称 ✅ 但实际不符：

| 声称 | 实际 | 证据 |
|---|---|---|
| D3.3a ✅ 2026-05-25：M7-core 6 硬约束 + FMEDA M7 v1.0 + MRM chain + ResumeHandler + PATH-S CI 通过 | 6 HC 函数**全为死代码**（run_hard_constraint_checks no-op）；HC-2 UKC 文件完全缺失；FMEDA 文件 spec 标注为 🔴 未做；PATH-S CI ⚫ 未验；MRM chain / ResumeHandler 确实实装 | `safety_supervisor_node.cpp:548-552`；spec:67-68 状态标注；文件树无 ukc |
| D3.3b ✅ 2026-05-25：SotifMetricsPublisher @10Hz 已实装 | SotifMetricsPublisher 以 **stub_mode=true（默认，永不关闭）** 运行；所有 violation_score / window_count / raw_value = 0.0F | `sotif_metrics_publisher.hpp:21`；`sotif_metrics_publisher.cpp:29-37`；grep `set_stub_mode` 无生产调用 |
| DEMO-2 阻塞 ✅ 已解除：M7 `/sil/sotif_metrics` @10Hz 已实装 | Topic 以正确频率发布但数据全零；前端 SotifMonitorStrip 显示全绿但无真实违反信息；**解除声明虚假** | 同上；frontend 类型与 backend SotifMetrics.msg 字段结构亦存在 schema mismatch |

---

## 7. D 任务联动表

| D 任务 | 关系 | 状态（真实）| 详情 / 偏差备注 |
|---|---|---|---|
| D0.3 | Closed in | ✅ | MUST-11 工时拆 6→9pw（core 6 + sotif 3）|
| D1.4 | Closed in | ✅ 2026-05-20 | 编码规范 v1.2：PATH-S 严格规则 |
| D3.3a | Closed in | 🔴 **OVERCLAIM** | 声称 ✅ 但：6 HC 全为死代码（run_hard_constraint_checks no-op）；HC-2 UKC 文件缺失；FMEDA 文件 spec 标 🔴 未做；PATH-S CI ⚫ 未验；MRM chain + ResumeHandler 真实实装 |
| D3.3b | Closed in | 🔴 **OVERCLAIM** | 声称 ✅ 但：SotifMetricsPublisher 永久 stub_mode；SOTIF 假设 #5 CommLink 永久零；veto 否决率因无发布方恒 0 |
| D2.5 | Blocks | 🔴 阻塞中 | D2.5 依赖 M7 `/sil/sotif_metrics` 真实数据，当前全零（stub_mode）+ HMI schema mismatch |
| D2.7 | Closed in | ✅ 2026-05-22 | HARA 32 危险源 + FMEDA M1 v1.0（M7 FMEDA 另行处理）|
| D3.8 | Closed in | ✅ 2026-08-25（Wave 1）| §11.10 仲裁优先级瀑布图完整化；置信度升级 |

---

## 8. DEMO 阻塞贡献（真实状态）

| DEMO | 阻塞来源 | 状态 |
|---|---|---|
| DEMO-1 | M7 VETO 硬门缺失（ADR-2）→ CheckerVetoNotification 从不发布 → M4/M5 无安全门；但 DEMO-1 范围不含 Gate-6 全量测试，影响有限 | 🟡 低优先级（DEMO-1 未 gate on veto）|
| DEMO-2 | `/sil/sotif_metrics` stub_mode 永久 → SotifMonitorStrip 全零；HMI TypeScript 类型与 backend SotifMetrics.msg 字段 schema mismatch（需前后端对齐）| 🔴 **阻塞中**（之前声称已解除为虚假）|

---

## 参考 D 任务文档

- D3.3a: `docs/Design/Phase 3/D3.3a-m7-core/`（⚠️ report 声称完成，但实现存在以上 CRITICAL gap）
- D3.3b: `docs/Design/Phase 3/D3.3b-m7-sotif/`（⚠️ 同上）

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-05-25 | 初版 progress（D3.3a/b 标 ✅，后经审计发现为 overclaim）|
| 2026-06-08 | 依系统审计（2026-06-08 @158bba9d）+ codegraph 核查重写 progress：状态矩阵 + gap 矩阵 + 创可贴 + overclaim 修正；D3.3a/b 降为 🔴 OVERCLAIM；DEMO-2 阻塞恢复为 🔴 阻塞中 |
