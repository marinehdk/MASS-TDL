# MASS-L3 TDL V&V 计划 v0.1

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-VV-PLAN-001 |
| 版本 | v0.1 |
| 日期 | 2026-05-12 |
| 状态 | 草稿（D1.5 产出，待 CCS 邮件回执确认框架可接受性）|
| 适用范围 | MASS-L3 TDL 8 模块（M1–M8）全生命周期 V&V，ROS2 Humble + Ubuntu 22.04 + PREEMPT_RT |
| 上游依赖 | 架构报告 v1.1.2（`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md`）<br>SIL 架构决策记录（`docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md`）<br>8 月开发计划 v3.1（`docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`）|
| 下游依赖 | D1.6 场景 schema / D1.7 覆盖率方法论 / D2.7 HARA+FMEDA / D3.3a Doer-Checker 三量化 |
| 作者 | 架构师（D1.5 兼任；V&V 工程师 FTE 到岗后接手 D1.6/D1.7 后续迭代）|

---

## §1 V&V 策略总览

MASS-L3 TDL 采用 **SIL → HIL → Sea Trial 三阶段渐进式 V&V 链路**，对应 DNV-CG-0264 范式。测试目标即部署目标——Production C++/MISRA ROS2 Humble 节点直接运行于 SIL 内核（选项 D 混合架构，SIL decisions §1）。

### §1.1 三阶段链路总览

| 阶段 | 时间 | 目的 | 主要工具 | 里程碑 |
|---|---|---|---|---|
| **SIL**（软件在环仿真）| Phase 1–3（5/13–8/31）| 功能验证 + COLREG 规则覆盖 + 延迟 KPI + IEC 61508 SIL 2 指标 | pytest / maritime-schema / libcosim / farn / dds-fmu | DEMO-1/2/3 |
| **HIL**（硬件在环仿真）| Phase 4（9–11月）| 实时性验证 + 硬件接口 + 800h 耐久 | FCB onboard 计算单元 + ROS2 RT 内核 + IEC 61508 SIL 2 第三方评估 | TÜV/DNV/BV 报告 |
| **Sea Trial**（实船）| D4.5（12月，非认证级）| AIS 数据采集 + 端到端技术验证 | FCB 实船 + ASDR + VDR | 技术验证报告（不作认证证据）|

> **认证说明**：2026-12 实船为**非认证级技术验证**（用户决策 2026-05-07）；认证级实船 D5.x 延 2027 Q1/Q2 AIP 受理后启动。

### §1.2 两条测试轨道

- **功能验证轨道**：COLREG 规则符合度 + ODD 覆盖 + KPI 延迟 + 场景通过率。由 V&V 工程师 FTE 主导（D1.6/D1.7/D2.5/D3.6）。
- **SIL 2 安全功能轨道**：M1 ODD 仲裁 + M7 Checker + MRC 触发三条核心安全功能（SIF）专属 V&V。见 §2.5。两条轨道独立评估，**不共用通过率指标**。

---

## §2 Phase Gate 判据

每个 Phase Gate 分 **Entry Conditions**（进入本阶段的前提）和 **Exit Conditions**（退出本阶段的判据）。SIL 2 专属条目标注 `[SIL2→§2.5]`，完整验证方法见 §2.5。

Phase Gate 自动校验：`python tools/check_entry_gate.py --phase {1|2|3|4}`

---

### §2.1 Phase 1 Gate（5/13–6/15 → DEMO-1）

**Entry Conditions（进入 Phase 1 的前提）**

| 条件 | 状态 | 来源 |
|---|---|---|
| D0 must-fix sprint 11 项全闭（5/12）| ✅ 已闭 | 甘特 §2 |
| D1.1 Python 包结构 + colcon 构建 ✓ | ✅ | D1.1 DoD |
| D1.2 CI/CD 39 tests passed，ruff 0 violations | ✅ | D1.2 CLOSED |
| D1.3a MMG 仿真器 v3.1 CLOSED | ✅ | D1.3a CLOSED |

**Exit Conditions（Phase 1 完成判据，6/15 DEMO-1 硬约束）**

| ID | 条件 | 阈值 | 测量 | 来源 |
|---|---|---|---|---|
| E1.1 | Imazu-22 全量通过 | **22/22** | `pytest imazu` | SIL decisions §6 [E2] |
| E1.2 | CPA ≥ 200m 比例（Imazu-22）| **≥ 95%** | imazu22_results.json | SIL decisions §6 [E2] |
| E1.3 | COLREG 分类率（Imazu-22）| **≥ 95%** | imazu22_results.json | SIL decisions §6 [E2] |
| E1.4 | Coverage cube 亮灯数 | **≥ 10 / 1100 cells** | coverage_cube.json | SIL decisions §6 |
| E1.5 | D1.3a MMG 旋回误差 | **< 5%**（vs 实测旋回直径）| d1_3a_validation.json | D1.3a DoD |
| E1.6 | CI 绿灯（全量测试）| **0 failed，≥ 39 passed** | pytest-report.json | D1.2 |
| E1.7 | `00-vv-strategy-v0.1.md` commit 存在 | 文件存在 | `ls` | D1.5 DoD |
| E1.8 `[SIL2→§2.5]` | M7 watchdog 白盒测试 | **≥ 1 case PASS** | `pytest m7` | 架构 §11 + §2.5 |

> **🟡 注**：E1.8 是 §2.5 中 M7 SIF 在 Phase 1 的最小可验证里程碑。完整 SIL 2 指标（SFF / 三量化矩阵）在 Phase 2–3。

---

### §2.2 Phase 2 Gate（6/16–7/31 → DEMO-2）

**Entry Conditions**

| 条件 | 来源 |
|---|---|
| Phase 1 Exit 全部通过 | §2.1 |
| HAZID RUN-001 第①次会议已完成（5/13）| 甘特 §11.6 |
| V&V 工程师 FTE 已到岗（目标 5/8，最迟 6/1）| 甘特 R0.2 |

**Exit Conditions（Phase 2 完成判据，7/31 DEMO-2 硬约束）**

| ID | 条件 | 阈值 | 测量 | 来源 |
|---|---|---|---|---|
| E2.1 | Coverage cube 亮灯数 | **≥ 220 / 1100 cells** | coverage_cube.json | 甘特 D2.5 |
| E2.2 | 50 场景通过率（首跑）| **≥ 90%** | sil_results.json | 甘特 D2.4 DoD |
| E2.3 | 50 场景通过率（修缮后，Phase 2 Exit 最终判据）| **≥ 95%** | sil_results.json（D2.5 末次 CI 跑）| 甘特 D2.5 DoD |
| E2.4 | TMR ≥ 60s 合规率 | **100%**（非 MRC 状态）| sil_results.json | 架构 §3.1 [R40] |
| E2.5 | AvoidancePlan 端到端延迟 | **P95 ≤ 1000ms** | latency_report.json | 甘特 D1.5 scope |
| E2.6 | ReactiveOverrideCmd 延迟 | **P95 ≤ 200ms** | latency_report.json | 甘特 D1.5 scope |
| E2.7 | Mid-MPC 求解时延 | **< 500ms** | latency_report.json | 甘特 D1.5 scope |
| E2.8 | BC-MPC 求解时延 | **< 150ms** | latency_report.json | 甘特 D1.5 scope |
| E2.9 | dds-fmu 桥接延迟（D1.3c）| **P95 ≤ 10ms / P99 ≤ 15ms** | latency_report.json | SIL decisions §2.4 |
| E2.10 | HARA v1.0 可用（D2.7）| 文档存在 + ≥ 30 危险源 | D2.7 DoD | 甘特 D2.7 |
| E2.11 `[SIL2→§2.5]` | M7 端到端延迟 | **P95 < 10ms** | latency_report.json | SIL decisions §2.4 + 架构 §11 |
| E2.12 `[SIL2→§2.5]` | M1 ODD 仲裁三量化（LOC/CYC/SBOM）| 见 §2.5 | doer_checker_metrics.json | D3.3a scope |
| E2.13 `[SIL2→§2.5]` | M7 Checker 三量化（LOC/CYC/SBOM）| 见 §2.5 | doer_checker_metrics.json | D3.3a scope |

> **🟡 注**：E2.9（dds-fmu 延迟）依赖 D1.3c FMI bridge 完成。D1.3c 是 v3.1 最大单项增量（12–16 pw）；若实测 P95 > 10ms 触发推翻信号（SIL decisions §2.5），退回纯 ROS2 native，不阻塞 DEMO-2 其余条目。

---

### §2.3 Phase 3 Gate（7/13–8/31 → DEMO-3）

**Entry Conditions**

| 条件 | 来源 |
|---|---|
| Phase 2 Exit 全部通过 | §2.2 |
| HAZID RUN-001 进行中（8/19 截止）| 甘特 §11.7 |
| D3.3a Doer-Checker 三量化全达标 | D3.3a DoD |

**Exit Conditions（Phase 3 完成判据，8/31 DEMO-3 硬约束）**

| ID | 条件 | 阈值 | 测量 | 来源 |
|---|---|---|---|---|
| E3.1 | SIL 总场景数 | **≥ 1000**（D3.6）| sil_results.json | 甘特 D3.6 |
| E3.2 | Coverage cube 亮灯数 | **≥ 880 / 1100 cells（80%）** | coverage_cube.json | 甘特 D3.6 |
| E3.3 | SOTIF triggering condition 覆盖 | **≥ 80%**（≥ 50 条基线中）| sotif_coverage.json | 甘特 D3.6 |
| E3.4 | 全场景 Phase score 通过率 | **≥ 95%**（6 维度评分全 PASS）| sil_results.json | D1.7 rubric |
| E3.5 | FMEDA M7 表 v1.0 完成 | 文档存在 + SFF 字段填入 | D2.7 DoD | 甘特 D2.7 |
| E3.6 `[SIL2→§2.5]` | M7 SFF 达标 | **≥ [TBD-HAZID-SFF-001]**（IEC 61508 SIL 2 目标）| FMEDA M7 表 | §2.5 + IEC 61508 |
| E3.7 `[SIL2→§2.5]` | MRC 触发延迟 | **≤ [TBD-HAZID-MRC-001]** | 故障注入测试报告 | §2.5 + 架构 §11 |
| E3.8 `[SIL2→§2.5]` | MRC 激活率（注入场景）| **100%** | 故障注入测试报告 | §2.5 + 架构 §11 |

> **[TBD-HAZID-SFF-001]**：M7 SFF 目标值。关闭路径：HAZID RUN-001 完成（8/19）→ IEC 61508 SIL 2 配件分析 → v1.1.3 §17 回填。阻塞度：E3.6 仅阻塞 Phase 3 Gate sign-off，不阻塞其余 E3.x。
>
> **[TBD-HAZID-MRC-001]**：MRC 触发延迟容限。关闭路径同上。来源：架构 §11 + HAZID RUN-001 场景工作包。

---

### §2.4 Phase 4 Gate（9–11月 → AIP）

**Entry Conditions**

| 条件 | 来源 |
|---|---|
| Phase 3 Exit 全部通过 | §2.3 |
| HIL 硬件采购单确认（≤ 7/13）| 甘特 D4.1 |
| SIL 2 第三方评估机构接洽（建议 ≤ 6月）| 甘特 §11.9.4 |

**Exit Conditions（Phase 4 完成判据，11月 AIP 提交）**

| ID | 条件 | 阈值 | 验证方式 | 来源 |
|---|---|---|---|---|
| E4.1 | HIL 测试时长 | **≥ 800h** | HIL runner log | 甘特 D4.1/D4.2 |
| E4.2 | HIL 无崩溃连续运行 | 验收标准见 D4.2 DoD | HIL test report | 甘特 D4.2 |
| E4.3 | TÜV/DNV/BV SIL 2 评估报告 | 报告存在 + CCS 认可机构出具 | D4.3 evidence store | 甘特 D4.3 |
| E4.4 | CCS AIP 提交回执 | 提交确认函 | D4.4 DoD | 甘特 D4.4 |
| E4.5 `[SIL2→§2.5]` | 第三方评估意见：M1 ODD 仲裁 | **"可接受"**（acceptable）| D4.3 报告 §M1 节 | §2.5 |
| E4.6 `[SIL2→§2.5]` | 第三方评估意见：M7 Checker | **"可接受"** | D4.3 报告 §M7 节 | §2.5 |
| E4.7 `[SIL2→§2.5]` | 第三方评估意见：MRC 触发路径 | **"可接受"** | D4.3 报告 §MRC 节 | §2.5 |

---

### §2.5 SIL 2 核心安全功能专属 V&V Registry

> 依据 CLAUDE.md §4 顶层决策四："M1 ODD 仲裁 + M7 Checker + MRC 触发三条路径为 IEC 61508 SIL 2 核心安全功能（SIF），须有**专属 Exit Gate，不能笼统合并进通用测试通过率**"。

本节定义三条 SIF 各自的验证方法、验收准则、Phase 里程碑。§2.x Phase Gate 中 `[SIL2→§2.5]` 标注条目均以本节为权威。

#### SIF-1：M1 ODD/Envelope Manager 仲裁

| 要素 | 内容 |
|---|---|
| **功能说明** | ODD 状态唯一权威来源；MRC 触发唯一路径；不得与 M2–M6 共享代码/库/数据结构 |
| **架构来源** | 架构报告 §5 + §2.5 顶层决策一/四 |
| **验证方法** | (1) 白盒状态机测试：ODD 状态转换表穷举（NORMAL / DEGRADED / CRITICAL / OUT / MRC_PREP / MRC_ACTIVE / EDGE）；(2) MC/DC 路径覆盖：每个判断条件独立组合覆盖（IEC 61508-3 §7.4.10 Table A.3）；(3) 独立代码路径验证：PATH-S 工具检测 M1 与 M2–M6 零共享依赖（0 violation） |
| **验收准则（Phase 2）** | ODD 状态转换 100% 路径覆盖；MC/DC 覆盖 100%；PATH-S 0 violation；MRC 触发模拟延迟 ≤ 100ms（非 HAZID 参数，为工程估算；HAZID 实值见 [TBD-HAZID-MRC-001]）|
| **验收准则（Phase 3）** | MRC 触发延迟 ≤ [TBD-HAZID-MRC-001]（HAZID 校准后回填）|
| **Phase 里程碑** | Phase 1 E1.8（白盒 ≥ 1 case）→ Phase 2 E2.12（三量化达标）→ Phase 4 E4.5（第三方认可）|
| **置信度** | 🟡 Medium（MRC 延迟容限 [TBD-HAZID-MRC-001] 待填）|

#### SIF-2：M7 Safety Supervisor Checker

| 要素 | 内容 |
|---|---|
| **功能说明** | Doer-Checker 中的 Checker；6 类硬约束（P95 < 10ms）；实现路径独立（不共享 M1–M6 代码/库/数据结构）；严格留 ROS2 native，不过 FMI 边界（SIL decisions §2.4）|
| **架构来源** | 架构报告 §11 + ADR-001 + SIL decisions §2.4 |
| **验证方法** | (1) **三量化矩阵**（D3.3a DoD）：<br>  · LOC 比例：Doer(M1–M6) / M7 ≥ 50:1（Boeing 777 Monitor 先例，架构 §2.5 [R4]）<br>  · 圈复杂度比例 CYC：Doer avg / M7 avg ≥ 30:1<br>  · SBOM 交集：Doer SBOM ∩ M7 SBOM = ∅（零共享第三方库）<br>(2) 6 类硬约束注入测试：对每类约束场景注入 ≥ 10 场景，验证 P95 响应 < 10ms；(3) FMEDA M7 表（D2.7）：安全失效分数（SFF）分析；(4) IEC 61508 watchdog 自测完整性验证 |
| **验收准则（Phase 2）** | LOC≥50:1；CYC≥30:1；SBOM∩=∅；M7 P95 < 10ms；PATH-S 0 violation |
| **验收准则（Phase 3）** | SFF ≥ [TBD-HAZID-SFF-001]（IEC 61508 SIL 2 目标；HAZID 校准后回填）|
| **Phase 里程碑** | Phase 1 E1.8（watchdog ≥ 1）→ Phase 2 E2.11/E2.13（三量化 + 延迟）→ Phase 3 E3.5/E3.6（FMEDA + SFF）→ Phase 4 E4.6（第三方认可）|
| **置信度** | 🟡 Medium（SFF 目标值 [TBD-HAZID-SFF-001] 待 HAZID 校准）|

> **[TBD-HAZID-SFF-001]**：IEC 61508 SIL 2 要求 SFF（Safe Failure Fraction）≥ 90%（1oo1 架构）或 ≥ 99%（1oo2 架构）；具体 M7 FMEDA 分析结果须 HAZID RUN-001（8/19）后产出 FMEDA M7 表（D2.7）。关闭路径：RUN-001 完成 → D2.7 FMEDA → 本节 §SIF-2 回填。🔴

#### SIF-3：MRC 触发路径

| 要素 | 内容 |
|---|---|
| **功能说明** | Minimum Risk Condition 触发（IMO MASS Code 要求，架构 §3.1）；M1 → L4/L5 MRC 命令集；任何状态均可触发，不经 M5 规划路径 |
| **架构来源** | 架构报告 §5 + §11 + ADR-001（MRM 命令集）|
| **验证方法** | (1) 故障注入测试：M1 单点失效 / M2 单点失效 / M7 单点失效 / M5-M7 联合失效 各 ≥ 10 场景注入，验证 MRC 激活率 100%；(2) 端到端时序追踪：故障注入到 MRC_RequestMsg 发出的完整路径追踪；(3) SIL 专用 MRC 场景 ≥ 10 个（ODD_OUT 触发 + CRITICAL 触发各 ≥ 5）|
| **验收准则（Phase 3）** | MRC 激活率 100%@注入场景；触发延迟 ≤ [TBD-HAZID-MRC-001]；端到端路径可追踪（ASDR 日志中可见 MRC_RequestMsg 时戳）|
| **Phase 里程碑** | Phase 2（MRC 路径白盒设计验证）→ Phase 3 E3.7/E3.8（故障注入 + 延迟）→ Phase 4 E4.7（第三方认可）|
| **置信度** | 🔴 Low（触发延迟容限 [TBD-HAZID-MRC-001] 未知；MRC 激活条件部分依赖 HAZID 场景工作包）|

> **[TBD-HAZID-MRC-001]**：MRC 触发延迟容限（ms）。关闭路径：HAZID RUN-001（8/19）产出 MRC 触发场景工作包 → 安全外包分析 → v1.1.3 §17 回填 + 本节 §SIF-3 更新。🔴

---

## §3 KPI 目标矩阵

> 每行引用具体来源。来源标注：SIL decisions §x = `docs/Design/SIL/00-architecture-revision-decisions-2026-05-09.md §x`；架构 §x = 架构报告 §x；[Rx] = 架构报告参考文献编号。

| KPI | 单位 | Phase 1 目标 | Phase 2 目标 | Phase 3 目标 | Phase 4 目标 | 测量工具 | 置信度 | 来源 |
|---|---|---|---|---|---|---|---|---|
| SIL 场景覆盖率（cells 亮）| cells/1100 | **≥ 10** | **≥ 220** | **≥ 880** | **1100**（D4.6 目标）| coverage_cube.json | 🟢 | SIL decisions §6 |
| Imazu-22 通过率 | %（22 场景）| **100%** | 保持 | 保持 | 保持 | pytest | 🟢 | SIL decisions §6 [E2] |
| CPA ≥ 200m 比例（Imazu-22）| % | **≥ 95%** | **≥ 95%**（50 场景）| **≥ 95%**（1000 场景）| — | scenario evaluator | 🟢 | SIL decisions §6 [E2] |
| COLREG 分类率 | % | **≥ 95%** | **≥ 95%** | **≥ 95%** | — | M6 分类器 | 🟢 | SIL decisions §6 [E2] |
| SIL 场景通过率 | % | —（首建）| **≥ 90%**（首跑）/ **≥ 95%**（修缮）| **≥ 95%** | — | sil_results.json | 🟡 | 甘特 D2.4/D2.5 DoD |
| SOTIF 触发条件覆盖 | %（≥ 50 条）| — | — | **≥ 80%** | — | sotif_coverage.json | 🟡 | 甘特 D3.6 scope |
| TMR ≥ 60s 合规率 | %（非 MRC 状态）| — | **100%** | **100%** | **100%** | SIL timeline | 🟢 | 架构 §3.1 [R40 Veitch 2024] |
| AvoidancePlan latency | ms | — | **P95 ≤ 1000** | **P95 ≤ 1000** | **P95 ≤ 100**（HIL）| latency_report.json | 🟢 | 甘特 D1.5 scope |
| ReactiveOverrideCmd latency | ms | — | **P95 ≤ 200** | **P95 ≤ 200** | **P95 ≤ 50**（HIL）| latency_report.json | 🟢 | 甘特 D1.5 scope |
| Mid-MPC 求解时延 | ms | — | **< 500** | **< 500** | **< 200**（HIL）| latency_report.json | 🟡 | 甘特 D1.5 scope |
| BC-MPC 求解时延 | ms | — | **< 150** | **< 150** | **< 50**（HIL）| latency_report.json | 🟡 | 甘特 D1.5 scope |
| **M7 端到端 latency** `[SIL2]` | ms | — | **P95 < 10** | **P95 < 10** | **P95 < 10** | latency_report.json | 🟢 | SIL decisions §2.4 + 架构 §11 |
| **dds-fmu bridge latency** | ms | — | **P95 ≤ 10 / P99 ≤ 15** | 同 Phase 2 | 同 Phase 2 | libcosim timing | 🟢 | SIL decisions §2.4 |
| **M7 SFF** `[SIL2]` | % | — | — | **≥ [TBD-HAZID-SFF-001]** | 验证 | FMEDA M7 表 | 🔴 | IEC 61508 SIL 2 + §2.5 |
| HIL 测试时长 | h | — | — | — | **≥ 800** | HIL runner log | 🟢 | 甘特 D4.1/D4.2 |

> **HIL latency 目标**（Phase 4）：SIL latency budget 加严 5×（SIL 软件仿真无实时性约束，HIL 有硬件中断 + RT 内核约束）。具体值在 D4.1 HIL entry-gate 确认。当前标注为工程估算值（🟡）。

---

## §4 V&V 方法论引用

### §4.1 场景 Schema（D1.6）

场景文件格式采用 **`dnv-opensource/maritime-schema` v0.2.x `TrafficSituation` 扩展**，FCB 项目专属字段放入 `metadata.*` 节点（SIL decisions §5）。

- Python 验证：`cerberus` + `pydantic`（maritime-schema 原生）[E14]
- C++ 验证：`cerberus-cpp`（同 schema 文件，避免双套逻辑）[E15]
- CCS 接受度：[TBD-CCS-SCHEMA-001]（D1.8 末早期发函 CCS 技术中心确认；SIL decisions §5.5）

> **[TBD-CCS-SCHEMA-001]**：CCS 是否接受 maritime-schema v0.2.x 作 evidence container。关闭路径：D1.8（6/15）向 CCS 技术中心 + Brinav 联络人发函（路径：CCS-DNV-Brinav 2024 MoU + Brinav Armada 78 03 案例先例）。退路：maritime-schema 退为内部表示 + 加导出器至 CCS 要求格式（SIL decisions §5.5 推翻信号）。🟡

### §4.2 覆盖立方体（D1.7）

```
覆盖立方体 = 11 COLREG Rules × 4 ODD subdomains × 5 disturbance bins × 5 seeds
           = 1100 concrete cells
```

- 11 COLREG Rules：Rule 5/6/7/8/9/13/14/15/16/17/19
- 4 ODD：open_sea / coastal_traffic_separation / port_approach / offshore_wind_farm
- 5 disturbance：Beaufort 0–1 / 2–3 / 4–5 / 6–7 / sensor_degraded
- 5 seeds：PRNG 种子 1–5（Monte Carlo 变体）
- 场景比例：Adversarial : Nominal : Boundary = 60 : 25 : 15（**内部启发式，非外部标准**，CCS 提交时不引为外部规范；SIL decisions §6.1 [E1]）

覆盖率 rubric 完整规约在 D1.7。

### §4.3 SIL Latency Budget

| 路径 | 组件 | 预算 | 强制规则 | 来源 |
|---|---|---|---|---|
| dds-fmu 桥接 | dds-fmu mediator + libcosim async_slave | P95 ≤ 10ms / P99 ≤ 15ms | **M7 严格 ROS2 native，不过 FMI 边界**（M7 端到端 KPI < 10ms，dds-fmu 单次 exchange 即可吃掉 KPI）| SIL decisions §2.4 + 架构 §11 |
| Own-ship FMU 步长 | libcosim co-sim master | Δt = 0.02s（50Hz）| 对齐 Nav Filter 50Hz 主频 | SIL decisions §1.1 选项 D |
| ROS2 M1–M6 节点 | native DDS | best-effort + jitter buffer 补偿 | — | SIL decisions §2.4 |

### §4.4 RL Artefact Rebound Path（Phase 4 规约，Phase 1–3 不执行）

Phase 4 D4.6（B2 启动）启动时，RL 产物回注流程须遵循：

```
ONNX (训练产物)
  → mlfmu build → target_policy.fmu
  → libcosim 加载（co-sim loop）
  → FMU 进 evidence store（DNV-RP-0671 [R30] 鉴定）
```

**L3 Repo 隔离三层**（SIL decisions §4，当前已在 D1.3a/b 仓库结构落地）：
- L1 Repo：`/src/l3_tdl_kernel/**`（C++/MISRA certified）vs `/src/rl_workbench/**`（Python/Phase 4）
- L2 Process：RL 训练独立 Docker container，只读挂载 certified binaries
- L3 Artefact：仅 `target_policy.fmu` 经 DNV-RP-0671 鉴定后方可入 certified loop

### §4.5 Monte Carlo LHS/Sobol（D3.6 大样本扫描）

在 1100-cell deductive cube 之外，对关键参数跑大样本扫描：
- 方法：拉丁超立方采样（LHS）/ Sobol 序列
- 参数：target ship 初始方位角 / SOG / 感知噪声 σ / 风流强度
- 样本量：**10000 samples**（D3.6）
- 输出：pass rate 95% CI + CPA_min 分布 + Rule violation 频率
- 工业先例：[R32] Hassani et al. 2022（Sobol sampling + COLREG geometric filter）
- 工具：`farn` v0.4.2+（case folder generator，MIT）

---

## §5 工具链

### §5.1 DNV MUST 三件（Phase 1 必须采用）

| 工具 | 版本锁定 | 用途 | License | 来源 |
|---|---|---|---|---|
| `dnv-opensource/maritime-schema` | **v0.2.x** | Scenario YAML + output Apache Arrow schema | MIT | SIL decisions §2 [E8] |
| `open-simulation-platform/libcosim` | latest MPL-2.0 | FMI 2.0 co-sim master（C++）| MPL-2.0 | SIL decisions §2 [E3] |
| `dnv-opensource/farn` | **v0.4.2+** | n-dim case folder generator（farn sweep → ospx）| MIT | SIL decisions §2 [E7] |

DNV-RP-0513（[R25]）§4 自鉴定证据收集流程须在 D1.3.1 Simulator Qualification Report 中记录。

### §5.2 DNV NICE-deferred（Phase 2/4 引入）

| 工具 | 引入阶段 | 用途 |
|---|---|---|
| `dnv-opensource/ship-traffic-generator` (`trafficgen`)| Phase 2 D2.4 | 50→200 场景扩展；参数化扫频 |
| `dnv-opensource/mlfmu` | Phase 4 D4.6（B2）| RL ONNX → FMU 打包 |

### §5.3 辅助工具链

| 工具 | 版本 | 用途 | 来源 |
|---|---|---|---|
| `ospx` | latest MIT | OSP system structure author（与 farn 配套）| SIL decisions §2 |
| `cerberus` | latest ISC | Python schema 验证 | SIL decisions §5.4 [E14] |
| `cerberus-cpp` | latest MIT | C++ schema 验证（同 schema 文件）| SIL decisions §5.4 [E15] |
| `dds-fmu` | latest | DDS ↔ FMI 桥接 mediator | SIL decisions §2.4 |
| `pytest` | current | 单元/集成测试 | D1.2 |
| `ruff` | current | Lint（0 violations gate）| D1.2 |
| `mypy` | current | 类型检查 | D1.2 |
| `Puppeteer` | current | Headless 浏览器 GIF/PNG batch render | SIL decisions §9 [E33] |
| `ffmpeg` | current | Evidence video 编译 | SIL decisions §9 |
| `pandoc` | current | Markdown → PDF（DEMO 现场打印）| §9 本文档 |

---

## §6 角色与责任

### §6.1 在岗期与主要承担

| 角色 | 在岗期 | 主要 D 任务 |
|---|---|---|
| **V&V 工程师 FTE** | 5/8–8/31（默认延 9/14 闭口）| D1.5/D1.6/D1.7 / D1.3.1 / D2.5 / D3.6 |
| **技术负责人（架构师）** | 全程 | D1.3a/b/c / D2.1–D2.4 / D3.1–D3.4 / D2.8/D3.8（架构）|
| **安全外包** | 5/15–7/10 | D2.7 HARA+FMEDA / D3.3a 三量化 / D3.3b SOTIF |
| **HF 咨询** | 6/16–7/27 | D2.6 船长访谈 / Figma / 培训课程 |
| **PM** | 全程（兼任）| D0.3 HTML 同步 / D1.8 / D2.6 / D3.5' / DEMO sync |
| **CCS 联络人** | 全程（兼任）| D0.2 / D1.8 早期发函 / D2.7 / D3.5/D3.8/D3.9 / AIP 11月 |
| **资深船长（≥ 1）** | 6/16–8/31（1d/月 × 3 + 3 DEMO）| D2.6 ground truth / DEMO-1/2/3 反馈 |

### §6.2 RACI（V&V 关键任务）

| 任务 | V&V FTE | 技术负责人 | 安全外包 | CCS 联络 |
|---|---|---|---|---|
| V&V Plan v0.1（本文档）| A/R | C | I | I |
| 场景 schema D1.6 | R | A | I | I |
| 覆盖率方法论 D1.7 | R | A | C | I |
| HARA + FMEDA D2.7 | I | C | R/A | C |
| SIL 集成 D2.5 | R | A | I | I |
| Doer-Checker 三量化 D3.3a | C | A | R | I |
| SIL 1000 D3.6 | R | A | C | I |
| CCS 早期发函 D1.8 | I | C | I | R/A |
| DEMO 证据包（3 档）| R | A | C | I |

---

## §7 CCS AIP 证据包骨架 v0.1

> v0.1 仅标坑位（placeholder），不填满。标注 `CCS 早期发函` 的为 D1.8 节点（6/15）须向 CCS 技术中心发函确认的高优先项。

| # | 证据件 | D 编号 | 截止 | CCS 早期发函 | 状态 |
|---|---|---|---|---|---|
| 1 | **ConOps v0.1**（核心场景 + 角色 + 边界 + 操作模式，5–10 页）| D1.8 | 6/15 | ✅ **最优先**（AIP 阶段 #1 必需件）| 🔲 待产出 |
| 2 | **maritime-schema 接受度函复** | D1.8 | 6/15 | ✅ 优先（[TBD-CCS-SCHEMA-001]）| 🔲 待发函 |
| 3 | **V&V Plan 框架可接受性回执**（本文档）| D1.5 | 5/31 | ✅ 优先 | ✅ 本文档（待回执）|
| 4 | HARA v1.0（≥ 30 危险源 → SIF → SIL → 缓解）| D2.7 | 7/31 | 🔲 | 🔲 待产出 |
| 5 | FMEDA M1/M7 表 | D2.7 | 7/31 | 🔲 | 🔲 待产出 |
| 6 | Sim Qualification Report（D1.3.1，DNV-RP-0513 §4）| D1.3.1 | 6/15 | 🔲 | 🔲 待产出 |
| 7 | V&V Report（SIL 阶段，D3.6/D3.8 后）| D3.8 | 8/31 | 🔲 | 🔲 待产出 |
| 8 | 场景 YAML library（D1.6 + D3.6，maritime-schema 格式）| D3.6 | 8/31 | 🔲 | 🔲 待产出 |
| 9 | ASDR JSONL 样本（D1.3b+ 滚动积累）| D1.3b | 滚动 | 🔲 | 🔲 待完整 |
| 10 | SIL 2 第三方评估报告（TÜV/DNV/BV，D4.3）| D4.3 | 10月 | 🔲（⚠️ 建议 **6月** 接洽机构）| 🔲 Phase 4 |

> **CCS 早期发函优先顺序**（D1.8 节点 6/15 执行）：
> 1. ConOps（AIP 流程 #1 必需件，CCS 最先看）
> 2. maritime-schema 接受度（若 CCS 否定，需加导出器；早确认早退路）
> 3. V&V Plan 框架可接受性（本文档 PDF，请 CCS 技术中心出具邮件回执）
>
> ⚠️ **P0 风险提醒**：甘特 §11.9.4 识别"9 月接洽 SIL 2 评估机构过晚"；建议 **6 月前** 联系 TÜV/DNV/BV 确认 CCS 认可机构资质，避免 11 月 AIP 提交时证据不被认可。

---

## §8 风险与缓解

| ID | 风险 | 概率 | 影响 | 缓解 | 触发阈值 |
|---|---|---|---|---|---|
| R-VV-001 | V&V FTE 延迟到岗（6/1 才到）| **高**（R0.2 已识别）| D1.5/D1.6 推 1 周；DEMO-1 拆简版 6/15 + 完整版 6/22 | 架构师 + PM 先行完成 D1.5 文档主体；DEMO-1 文档类交付（PDF）不依赖 FTE | FTE 未在 5/31 前到岗 |
| R-VV-002 | ChAIS 商业 AIS 数据授权未获 | 中 | D1.3b.2 场景真实性降级 | **已决策**：Phase 1–3 用 Kystverket + NOAA MarineCadastre 开放数据（SIL decisions §7.3）；无需额外授权 | Phase 1 末 AIS pipeline 评估失败 |
| R-VV-003 | HIL 硬件下单延迟（7/13 DDL）| 中 | D4.1/D4.2 推迟；Phase 4 Gate 延后 | 7/13 前确认采购单；备选 DNV 模拟器租用 | 7/1 无确认函 |
| R-VV-004 | HAZID RUN-001 8/19 deadline miss | 中 | [TBD-HAZID-SFF-001] / [TBD-HAZID-MRC-001] 无法回填；Phase 3 Gate E3.6/E3.7 空缺 | 5/13 第①次会议已安排；CCS 持续介入；[TBD-HAZID-*] 参数 stub 保留到 8/31（架构 §6 设计 DoD）| 8/1 前未产出中间报告 |
| R-VV-005 | maritime-schema CCS 接受度未确认 | 低-中 | evidence container 需重做导出器 | D1.8（6/15）早期发函 CCS；退路见 SIL decisions §5.5 推翻信号 | CCS 6/15 回函否定 |
| R-VV-006 | dds-fmu 实测 latency > 10ms | 中 | Phase 2 Gate E2.9/E2.11 不达标；M7 KPI 威胁 | 触发推翻信号（SIL decisions §2.5）：退回纯 ROS2 native + maritime-schema/Arrow 序列化；不阻塞 DEMO-1 | D1.3c 实测 P95 > 10ms |
| R-VV-007 | SIL 2 第三方机构接洽过晚 | **高**（甘特 §11.9.4 已标警告）| 11月 AIP 提交时证据不被 CCS 认可 | **建议立即（≤ 6月）** 接洽 TÜV/DNV/BV 确认 CCS 认可机构资质 | 7/31 前未确认机构 |
| R-VV-008 | M5 IPOPT 求解 p99 > 500ms | 中 | Phase 2 Gate E2.7 不达标 | 先用线性化 Nomoto 一阶模型 + 预测步长从 18 → 12 降级；D1.3.1 + §3 KPI 已含实测计划 | D2.1/D2.2 实测 p99 > 500ms |

---

## §9 Markdown → PDF 转换（DEMO-1 现场打印）

```bash
# 前提：安装 pandoc + XeLaTeX + CJK 字体
brew install pandoc
brew install --cask mactex          # 或 basictex（较小）
# CJK 字体（macOS 系统自带 PingFang，也可用 Noto）

# 转换命令（在仓库根目录执行）
pandoc "docs/Design/V&V_Plan/00-vv-strategy-v0.1.md" \
  --pdf-engine=xelatex \
  -V mainfont="PingFang SC" \
  -V monofont="Menlo" \
  -V geometry:"margin=2cm,a4paper" \
  -V fontsize=10pt \
  --toc --toc-depth=3 \
  -o "docs/Design/V&V_Plan/00-vv-strategy-v0.1.pdf"

# 验证
open "docs/Design/V&V_Plan/00-vv-strategy-v0.1.pdf"
```

> **DEMO-1 打印清单**：`00-vv-strategy-v0.1.pdf`（本文档）+ `docs/Design/V&V_Plan/README.md`（目录索引）。

---

## 参考文献

> 以下 [Rx] 编号与架构报告 v1.1.2 参考文献编号对齐。[Ex] 来源于 SIL decisions doc。

| 编号 | 文献 |
|---|---|
| [R4] | Jackson, S. (2021). *Certified Control: Safety-Critical Software Handbook*（Doer-Checker SLOC 比例工业先例）|
| [R25] | DNV-RP-0513 (2024 ed.). *Assurance of simulation models* |
| [R27] | dnv-opensource/maritime-schema v0.2.x（MIT）|
| [R30] | DNV-RP-0671 (2024). *Assurance of AI-enabled systems* |
| [R32] | Hassani, V. et al. (2022). *Automatic traffic scenarios generation for autonomous ships collision-avoidance system testing*. Ocean Eng. DOI:10.1016/j.oceaneng.2022.111864 |
| [R33] | Hagen, T. (2022). *Risk-based Traffic Rules Compliant Collision Avoidance for Autonomous Ships*. NTNU MS thesis |
| [R34] | Woerner, K. (2019). *COLREGS-Compliant Autonomous Surface Vessel Navigation*. MIT PhD thesis |
| [R38] | Sawada, R., Sato, K., Majima, T. (2021). *Automatic ship collision avoidance using deep reinforcement learning with LSTM in continuous action spaces*. J. Mar. Sci. Technol. 26（引 Imazu 1987 22 canonical encounters）|
| [R40] | Veitch, E. (2024). TMR ≥ 60s 接管时窗基线（架构 §3.1）|
| [E1] | *SIL Framework Architecture for L3 TDL COLAV — CCS-Targeted Engineering Recommendation*（深度研究合成报告，2026-05-09）|
| [E2] | *SIL Simulation Architecture for Maritime Autonomous COLAV Targeting CCS Certification*（同期独立报告，2026-05-09）|
| [E3] | *Technical Evaluation of DNV-OSP Hybrid SIL Toolchains for CCS i-Ship N Certification*（NLM Deep Research，2026-05-09）|
| [E7] | farn v0.4.2 GitHub Releases（2025-late）|
| [E8] | maritime-schema + ship-traffic-generator 2025-02 仓库合并，PyPI v0.2.x |
| [E14] | cerberus Python（cerberus.readthedocs.io）|
| [E15] | cerberus-cpp（github.com/dokempf/cerberus-cpp）|
| [E33] | Puppeteer headless 浏览器 CI batch GIF/PNG render |

---

*文档版本 v0.1。D1.5 产出，5/31 commit + 向 CCS 发函。V&V 工程师 FTE 到岗后负责 D1.6/D1.7 迭代；本文档 v0.2 在 D2.8（7/31）与架构 v1.1.3 stub 同步更新。*
