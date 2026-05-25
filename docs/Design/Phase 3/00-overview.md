# Phase 3 · M4/M5/M7/M8 + SIL 1000 + 架构终稿 · Overview

| 属性 | 值 |
|---|---|
| 时间 | 2026-07-13 → 2026-08-31（7.5 周，与 Phase 2 重叠 2.5 周）|
| 估计人周 | 37.5（v3.1 baseline）|
| 阶段目标 | 完成 4 个剩余业务模块（M4 IvP 行为仲裁 / M5 双 MPC 轨迹规划 / M7-core+sotif Doer-Checker / M8 HMI 完整）+ HAZID 8/19 完成 → v1.1.3 完整回填 + SIL 1000+ CCS 证据 + RFC-007 cyber 接口 + 架构 v1.1.3 完整化 |
| **里程碑** | 🎬 **DEMO-3 Full-Stack with Safety + ToR（2026-08-31）** |
| 进度日期 | 2026-05-25（D3.1/D3.2/D3.3a/D3.3b/D3.4 关闭后快照）|
| 当前阶段状态 | 🟢 **D3.1–D3.4 已关闭**（5/25 提前完成；D3.5–D3.9 待启动）|

> **L1 总账**：[../00-master-plan.md](../00-master-plan.md)（v3.2-master，2026-05-22）
> **架构权威**：[../Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md](../Architecture%20Design/MASS_ADAS_L3_TDL_架构设计报告.md) v1.1.3-pre-stub
> **完整 DoD 细则**：[../Architecture Design/gantt/archive/v3.2_2026-05-20_archived.md](../Architecture%20Design/gantt/archive/v3.2_2026-05-20_archived.md) §5 Phase 3 D3.1–D3.9

---

## D 任务清单 + 状态

| D 编号 | 主题 | 目录 | 人周 | 截止 | Affects | 状态 |
|---|---|---|---|---|---|---|
| **D3.1** | M4 BehaviorArbiter IvP + ivp_contributions[] | [D3.1-m4-behavior-arbiter/](D3.1-m4-behavior-arbiter/) | 4.0 | **7/26** (DEMO-2 P0 子集) | M4 | ✅ **Closed 5/25** — 813 LOC src + 916 LOC test; [report](D3.1-m4-behavior-arbiter/D3.1-report.md) |
| **D3.2** | M5 TacticalPlanner BC-MPC+Mid-MPC + trajectory_candidates[] | [D3.2-m5-tactical-planner/](D3.2-m5-tactical-planner/) | 6.5 | **7/27** (DEMO-2 P0 子集) → **8/10** (完整) | M5 | ✅ **Closed 5/25** — 2751 LOC src + 2218 LOC test; [report](D3.2-m5-tactical-planner/D3.2-report.md) |
| **D3.3a** | M7-core Doer-Checker 三量化矩阵 + 6 硬约束 + FMEDA M7 | [D3.3a-m7-core/](D3.3a-m7-core/) | 6.0 | **8/10** | M7 | ✅ **Closed 5/25** — 6 HC + FMEDA + MRM + Resume + PATH-S CI; [report](D3.3a-m7-core/D3.3a-report.md) |
| **D3.3b** | M7-sotif 6 类假设违反检测 + sotif_metrics topic | [D3.3b-m7-sotif/](D3.3b-m7-sotif/) | 3.0 | **8/16** | M7 | ✅ **Closed 5/25** — 459 LOC src + SotifMetrics @10Hz; [report](D3.3b-m7-sotif/D3.3b-report.md) |
| **D3.4** | M8 HMI 完整（7 Phase 3 增项 + 双角色 + ToR 矩阵）| [D3.4-m8-hmi-full/](D3.4-m8-hmi-full/) | 5.5 | **8/24** (SAT 桥接子集 7/31 前) | M8 | ✅ **Closed 5/25** — 1220 LOC src + 1595 LOC test; SAT-2/3/SOTIF 桥接; [report](D3.4-m8-hmi-full/D3.4-report.md) |
| **D3.5** | 架构 v1.1.3 HAZID 132 [TBD] 回填 | [D3.5-arch-hazid-backfill/](D3.5-arch-hazid-backfill/) | 2.0 | **8/31** | 架构 | 🔵 spec❌ |
| **D3.5'** | 模拟器培训课程大纲 v1.0 + 培训胜任力矩阵 | [D3.5p-training-curriculum/](D3.5p-training-curriculum/) | 1.5 | **8/23** | HF, Cert | 🔵 spec❌ |
| **D3.6** | SIL 1000+ 场景 COLREGs 覆盖率报告（V&V） | [D3.6-sil-1000-scenario-coverage/](D3.6-sil-1000-scenario-coverage/) | 2.5 | **8/31** | V&V, SIL | 🔵 spec❌ |
| **D3.7** | 8 模块 SIL 全系统 8h 集成测试报告 | [D3.7-sil-8module-integration/](D3.7-sil-8module-integration/) | 3.0 | **8/31** | 全模块 | 🔵 spec❌ |
| **D3.8** | 架构 v1.1.3 完整化（§17–§21 + 算法矩阵 + 仲裁图）| [D3.8-arch-v1.1.3-complete/](D3.8-arch-v1.1.3-complete/) | 2.5 | **8/31** | 架构 | 🔵 spec❌ |
| **D3.9** | RFC-007 L3 ↔ Z-TOP/Cybersec 接口（IACS UR E26/E27）| [D3.9-rfc007-cybersec/](D3.9-rfc007-cybersec/) | 1.0 | **8/16** | Cybersec | 🔵 RFC 仍在 archive / spec❌ |

**合计**：37.5 人周（D3.1/D3.2 共 10.5 pw 在 7/13–7/27 前置执行）

---

## DEMO-2 P0 提前拉取（Phase 2 末强依赖，来自 Phase 3）

以下子集从 D3.x 拆出，必须在 **7/31 DEMO-2 前**就绪：

| 拉取项 | 来源 | 人周 | 截止 | 当前状态 |
|---|---|---|---|---|
| M4 IvP → `/sil/sat2_data.ivp_contributions[]` @4Hz | D3.1 | 4.0 | **7/26** | ✅ **已实装**（D3.1 Closed 5/25）|
| M5 → `/sil/sat3_data.trajectory_candidates[]` @2Hz | D3.2 | 6.5 | **7/27** | ✅ **已实装**（D3.2 Closed 5/25）|
| M8 增发 3 topic + IDL（sat2_data/sat3_data/sotif_metrics）| 拆自 D3.4 | 1.5 | **7/31** | ✅ **已实装**（D3.4 Closed 5/25）|
| 前端 useFoxgloveLive 3 handler（IvP/Trajectory/SOTIF）| D1.3.2.3 后段 | 0.5 | **7/31** | ✅ **已实装**（D1.3.2.3 Closed 5/25）|

> **关键路径**：✅ D3.1/D3.2/D3.4 已提前完成，DEMO-2 P0 阻塞全部解除。

---

## Phase 3 甘特（v3.2 archived 版）

| 工作流 | W12 7/13-19 | W13 7/20-26 | W14 7/27-8/2 | W15 8/3-9 | W16 8/10-16 | W17 8/17-23 | W18 8/24-31 |
|---|---|---|---|---|---|---|---|
| **D3.1 M4** | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓ | | | | |
| **D3.2 M5** | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓ | | | |
| D3.3a M7-core | | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓ | | |
| D3.3b M7-sotif | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓ | | |
| D3.4 M8 完整 | | | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ | ▓▓▓ | |
| D3.5 v1.1.3 回填 | | | | | | ▓▓▓ | ▓▓ |
| D3.5' 培训课程 | | | | ▓ | ▓▓▓ | ▓ | |
| D3.6 SIL 1000+ | | | | 场景扩展 | ▓▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ |
| D3.7 8模块全集 | | | | | ▓▓ | ▓▓▓▓▓ | ▓▓▓▓▓ |
| D3.8 v1.1.3 完整 | | | | | ▓▓ | ▓▓▓ | ▓▓▓ |
| D3.9 RFC-007 | | | | ▓▓ | ▓▓ | | |
| ⟋ HAZID RUN-001 | W12 ⑧ | — | W14 ⑨ full-day | — | W16 ⑩ | **★ 8/19 完成** | → 回填 D3.5 |
| 📋 HIL 采购需求 | **★ 提交** | | | | | | |
| 🎬 **DEMO-3** | | | | | | | **★ 8/31** |

---

## DEMO-3 Charter（2026-08-31）

**Scenario**（端到端约 12 min）：

1. **1000 场景批量结果总览**（≥ 98% pass + 立方体覆盖 ≥ 80% + SOTIF ≥ 80% + Adversarial 比例验证）
2. **8h 全模块集成 live 节选**（≥ 50 high-stakes 场景注入 → D3.7）
3. **M7 Doer-Checker veto live demo**：6 类硬约束故障注入 + Doer-Checker 独立性证明（LOC/圈复杂度/SBOM 三矩阵）
4. **ToR 接管 live**：操作员从睡舱 → 120s 内接管 → ASDR 完整签名链 + trajectory ghosting 同步显示
5. **MRC 触发 live**：M7 → M1 → MRM-01/02/03/04 执行链
6. **Y-axis Reflex Arc 模拟触发** + M8 红屏全屏蜂鸣
7. **v1.1.3 完整文档全文展示** + RFC-007 + 算法选型矩阵 + L3 仲裁优先级图
8. **132 [TBD-HAZID] 全部回填**：5 个代表参数 [TBD] → calibrated 对比展示
9. **培训课程大纲 + ToR drill 脚本 + 培训胜任力矩阵 v1.0**
10. **端到端延迟 KPI 实测仪表盘**（vs DEMO-2 进步）
11. **失败场景根因分析**（不掩饰，∈ CCS 证据包）

**Audience**：CCS 验船师（正式查看）/ DNV-TÜV-BV SIL 2 评估初步会议 / 业主决策层 / 资深船长 ≥2

**Visible Success**：
- D3.1–D3.9 全部 DoD ✅
- 1000 场景 ≥ 98% pass + 立方体 ≥ 80%
- CCS 邮件回执"v1.1.3 + 1000 场景 + HARA + 培训矩阵 完整收到，准备 11 月 AIP 提交"
- DNV/TÜV/BV ≥ 1 家书面意向继续 SIL 2 评估
- 资深船长 ≥ 2 签字"可用性认可"
- 8h 集成 0 P0 崩溃

---

## 模块实装基线（D3.1–D3.4 关闭后，2026-05-25）

| 模块 | SIL | 代码基线 | Topic 状态 | Phase 3 主 D 任务 |
|---|---|---|---|---|
| **M4** BehaviorArbiter | SIL 1 | 813 LOC src + 916 LOC test ✅ | `/l3/m4/behavior_plan` ✅；`/sil/sat2_data.ivp_contributions[]` ✅ @4Hz | D3.1 ✅ Closed |
| **M5** TacticalPlanner | SIL 1 | 2751 LOC src + 2218 LOC test ✅ | `/l3/m5/avoidance_plan` ✅；`/sil/sat3_data.trajectory_candidates[]` ✅ @2Hz | D3.2 ✅ Closed |
| **M7** SafetySupervisor | SIL 2 | 827 LOC src (core 368 + sotif 459) + 3046 LOC test ✅ | `/sil/sotif_metrics` ✅ @10Hz；6 硬约束 ✅；FMEDA M7 v1.0 ✅ | D3.3a ✅ + D3.3b ✅ Closed |
| **M8** HMI Bridge | SIL 1 | 1220 LOC src + 1595 LOC test ✅ | `/sil/sat2_data` ✅ `/sil/sat3_data` ✅ `/sil/sotif_metrics` ✅；ToR 自适应 ✅ | D3.4 ✅ Closed |

**共享 IDL 状态**（D3.1–D3.4 关闭后）：

| IDL 消息类型 | Topic | 数据源 | 消费端 | 状态 |
|---|---|---|---|---|
| `l3_msgs/SAT2Data` | `/sil/sat2_data` | M4 ivp_contributions[] + M6 colregs_chain[5] | 前端 `IvpRiskGradientLayer` + `ColregsRationaleTree` | ✅ 已创建 + 已发布 |
| `l3_msgs/SAT3Data` | `/sil/sat3_data` | M5 trajectory_candidates[13] + primary trajectory | 前端 `MpcTrajectoryLayer` | ✅ 已创建 + 已发布 |
| `l3_msgs/SotifMetrics` | `/sil/sotif_metrics` | M7 6 类假设违反指标 | 前端 `SotifMonitorStrip` | ✅ 已创建 + 已发布 |

---

## 关键风险

| # | 风险 | 等级 | 缓解 |
|---|---|---|---|
| R3.1 | M5 IPOPT 求解 p99 > 500ms | 🟡→中 | ✅ NomotoFallback 兜底已实装（D3.2 Closed）|
| R3.2 | M7 PATH-S CI 发现跨边界引用 | 🟢→低 | ✅ PATH-S CI 通过（D3.3a Closed）|
| R3.3 | HAZID 8/19 滑期 → D3.5 回填窗口紧 | 中 | 行为分支无关参数 7/31 先锁；分支相关参数等 8/19 |
| R3.4 | M4 IvP libIvP/自实现路径未决 | 🟢→低 | ✅ 自实现 IvP solver 已完成（D3.1 Closed）|
| R3.5 | DEMO-3 12min 节奏太赶 | 中 | 8/24 起每日 dry-run；8/29 完整 dry run；8/30 业主彩排 |
| R3.6 | 3 IDL 缺失阻塞 M4/M5/M7/M8 前端联调 | 🟢→低 | ✅ 3 IDL + publisher 已全部实装（D3.1/D3.2/D3.4 Closed）|

---

## 任务启动流程（按 CLAUDE.md §7.1）

每个 D 任务启动时：
1. 在对应 `D3.x-*/` 目录下运行 `superpowers:brainstorming` → 产出 `D3.x-spec.md`
2. 运行 `superpowers:writing-plans` → 产出 `D3.x-plan.md`
3. 运行 `superpowers:executing-plans` → 产出 `evidence/` + 闭口后 `D3.x-report.md`
4. 同步更新 `TDL-Kernel/M{n}/M{n}-progress.md` 联动表

**当前最高优先级**（按开工时点顺序）：
- **7/13 必须开工**：D3.1（M4）、D3.2（M5）
- **7/31 前**：D3.4 SAT 桥接子集（IDL 创建 + stub publisher）
- **8/10**：D3.3a（M7-core）、D3.2 完整
- **8/16**：D3.3b（M7-sotif）、D3.9（RFC-007）
- **8/23**：D3.5'（培训课程）
- **8/24**：D3.4 完整
- **8/31**：D3.5（HAZID 回填）、D3.6（SIL 1000）、D3.7（8h 集成）、D3.8（架构完整化）

---

## 修订记录

| 版本 | 日期 | 变更 |
|---|---|---|
| v0.1 | 2026-05-25 | Phase 3 overview 初建（从 v3.2 archived 计划 §5 提取 + 实测基线补充）|
| v0.2 | 2026-05-25 | D3.1/D3.2/D3.3a/D3.3b/D3.4 关闭：状态表 ✅、DEMO-2 P0 全部解除、模块基线更新、IDL 缺口→已实装、6 风险降级 |
