# Phase E3 CCS 认证证据存档

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-CERT-E3-001 |
| 版本 | v1.0 |
| 日期 | 2026-05-07 |
| 阶段 | Phase E3 — HIL 仿真测试证据存档 |
| 目标入级 | CCS i-Ship (I, N, R1) — 阶段二申请 |
| 参照标准 | DNV-CG-0264 (2025.01) §4 导航子功能；CCS《智能船舶规范》(2024) |
| 关联报告 | SANGO-ADAS-L3-TEST-E3-REPORT-001 |

---

## §1 目的

本文档将 Phase E3 HIL 仿真测试交付物映射至 CCS 和 DNV-CG-0264 认证证据要求，依据架构报告 v1.1.2 §14（CCS 入级路径映射）。本文档作为 CCS i-Ship 阶段二申请的阶段性证据存档，在 HAZID RUN-001 完成（2026-08-19）、Phase E4 实船试航后补充完整。

---

## §2 DNV-CG-0264 §4 导航子功能覆盖矩阵

| DNV-CG-0264 §4 子功能 | TDL 模块 | E3 证据产物 | 覆盖状态 |
|---|---|---|---|
| §4.2 Planning prior to voyage | M3 Mission Manager | 本阶段聚焦碰撞规避，M3 航次规划由 E2 INT-002 覆盖 | 部分（E4 补充） |
| §4.3 Condition detection | M2 World Model + M1 Envelope Manager | `tests/hil/scenarios/scenarios_all.json`（3209 个环境状态场景）；`tests/hil/target_injector.py`（感知降级注入） | ✅ HIL 覆盖 |
| §4.4 Condition analysis | M1 EM + M7 Safety Supervisor | `tests/hil/test_hil_e2e.py::test_hil_rule19_degraded`（M1 ODD 健康度降级验证）；`tests/hil/conftest.py`（`target_injector_node` fixture） | ✅ HIL 覆盖 |
| §4.5 Deviation from planned route | M3 Mission Manager + M4 Behavior Arbiter | E2 INT-002 覆盖重规划触发；E3 `scenarios_100.json` 扩展含重规划触发场景 | 部分（E2+E3 合计） |
| §4.6 Contingency plans | M7 Safety Supervisor + M1 EM | `tests/hil/test_hil_e2e.py::test_hil_rule19_degraded`（DEGRADED/CRITICAL ODD 场景下 MRC 触发链验证） | ✅ HIL 覆盖 |
| §4.7 Safe speed | M6 COLREGs Reasoner + M5 Tactical Planner | 100 场景套件（`test_hil_100_scenario_suite`）；`test_hil_rule17_standon`（Rule 17 hold-course — 无不当机动） | ✅ HIL 覆盖 |
| §4.8 Manoeuvring | M5 Tactical Planner | `src/fcb_simulator/`（FCB MMG 4-DOF 仿真器，RK4 50 Hz）；`tests/hil/benchmarks/test_timing_e3.py`（Mid-MPC P99 <100ms，BC-MPC P99 <50ms） | ✅ HIL 覆盖 |
| §4.9 Docking | M4 Behavior Arbiter (Docking) + M5 TP | 不在 E3 范围（E4 实船演示） | 待 E4 |
| §4.10 Alert management | M8 HMI/Transparency Bridge + M7 SS | `tools/roc_simulator/`（ROC HMI ToR 交互流）；`tests/hil/test_hil_e2e.py`（含 ToR 响应路径） | ✅ HIL 覆盖 |

---

## §3 CCS i-Ship 阶段二申请证据清单

参照架构报告 v1.1.2 §14.4（关键证据文件清单）。

| 证据文件 | 产生阶段 | E3 贡献 | 状态 |
|---|---|---|---|
| COLREGs 覆盖率测试报告 | E3 | `tests/hil/scenarios/scenarios_all.json`（3209 场景）；`tests/hil/scenarios/scenarios_100.json`（100 基准场景） | ✅ 就绪 |
| HIL 测试报告 | E3 | `docs/Test Plan/integration-test-report-phase-e3.md`（本 Phase E3 报告）| ✅ 就绪（待硬件执行数据回填） |
| 系统架构报告 | Design | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` v1.1.2 | ✅ 就绪 |
| 安全分析 / HAZID | Design | `docs/Design/HAZID/RUN-001-kickoff.md`（25+ `[TBD-HAZID]` 参数待 FCB 实测后回填） | ⏳ 2026-08-19 |
| SIL 评估报告 | E1 | `docs/Design/Architecture Design/audit/` M7 Doer-Checker 独立性证明；IEC 61508 SIL 2 路径静态核查 E1 已完成 | ✅ 就绪 |
| 集成测试报告（E2） | E2 | `docs/Test Plan/integration-test-report-phase-e2.md`；INT-001–008 GTest 22 用例 | ✅ 就绪 |
| ASDR 日志分析报告 | E2 | `tests/integration/test_int_007_asdr_full.cpp`（8 模块各 ≥1 条带 SHA-256 签名记录验证） | ✅ E2 就绪 |
| 实船试航报告 | E4 | 待 2026-06 Phase E4 | ⏳ 待 E4 |

---

## §4 IEC 61508 SIL 2 路径证据

M7 Safety Supervisor 是 Doer-Checker 架构中的 Checker，须满足 IEC 61508 SIL 2 独立路径要求：

- **独立实现路径**：M7 与 M1–M6 无共享代码、无共享库、无共享数据结构——E1 静态检查已验证
- **F-INT-002（继承自 E2）**：M7 订阅 `/l3/m2/odd_state`（错误），应订阅 `/l3/m1/odd_state`；修复分支 `fix/integration-topic-mismatches` 就位。此缺陷使 M7 在 ODD 降级时无法接收状态更新，是 SIL 2 路径的已知阻断项，须在 HIL 认证执行前修复。
- **E3 新增**：`tests/hil/test_hil_e2e.py::test_hil_rule19_degraded` 验证 F-INT-002 修复后 M1→M7 ODD 健康信号路径的端到端完整性

---

## §5 ASDR 证据链（IMO MASS Code 白盒可审计性）

| 证据要素 | 实现位置 | 状态 |
|---|---|---|
| 全模块 SHA-256 签名记录 | `tests/integration/test_int_007_asdr_full.cpp`（E2 INT-007） | ✅ E2 已验证 |
| 场景决策事件可追溯性 | `tests/hil/target_injector.py`（timestamp + scenario_id 注入）；`tests/hil/hil_runner.py`（scenario_id 随发布消息传播） | ✅ E3 就绪 |
| M5 计算时延合规性 | `tests/hil/benchmarks/timing_verifier.py`（P99 时延捕获，供 ASDR Lτ 时序要求证明） | 待 HIL 硬件执行 |

---

## §6 下一步（认证路径）

| 优先级 | 任务 | 目标时间 |
|---|---|---|
| P0 | 修复 F-INT-001 + F-INT-002（`fix/integration-topic-mismatches`）后，在 HIL 环境重跑 100 场景套件，将实测通过率回填至 §3 HIL 测试报告行 | 2026-05 |
| P0 | 修复 M8 ToR callsite（`fix/m8-tor-callsite`），完成 §4.10 Alert management 路径全量验证 | 2026-05 |
| P1 | 在实物 FCB 硬件采集 M5 Mid-MPC / BC-MPC / Reflex Arc / L4 模式切换 P99 时序数据，回填性能证据 | 2026-06（Phase E4 同期） |
| P1 | Phase E4 实船试航完成 → 实船试航报告（DNV-CG-0264 §4.9 Docking + §4.8 Manoeuvring 实证） | 2026-06 |
| P2 | HAZID RUN-001 完成 → v1.1.3 参数回填（25+ `[TBD-HAZID]` 项）→ CCS FMEA/HARA 完整 | 2026-08-19 |
| P2 | CCS i-Ship (I, N, R1) 阶段二申请正式提交（HAZID + E4 完成后） | 2026-08–09 |
