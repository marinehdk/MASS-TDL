# 06 — Four Proofs → DNV-RP-0513 Clause Mapping & Evidence Matrix

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-06 |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 🟢 方法论文档完整<br>⚫ DNV-RP-0513 完整条款文本待 DNV PDF 购买 (GAP-032) |
| 上游 | D1.3.1 模拟器鉴定报告 README §交付物清单；8 月计划 v3.0 D1.3.1；V&V Plan v0.1 §8 DNV Toolchain Entry Conditions |
| 下游交付物 | `07-ccs-mapping.md`（CCS §9.1 条款映射引用本矩阵）；DNV 验证官审核材料包 |
| 置信度 | 🟢 方法论完成 + ⚫ 完整条款文本待 DNV PDF 购买 |

---

## §1 Purpose

本文档将 D1.3.1 模拟器鉴定报告的四项技术证明（① 模型保真度、② 确定性/可复现性、③ 传感器退化置信度、④ libcosim 编排追溯性）映射到 **DNV-RP-0513**（Software in Certification）的四大条款族（§4 Tool Self-Qualification、§5 Model Assurance Levels、§6 Scenario-Based Validation、§7 Tool Integration & Data Flow），形成一份可直接用于 DNV 验证官审核的证据清单。

---

## §2 DNV-RP-0513 Clause Mapping

### §2.1 §4 — Tool Self-Qualification Evidence

| # | RP-0513 Clause | Requirement Summary ⚫ | Evidence Location | Deliverable File | Status |
|---|---|---|---|---|---|
| 4.1 | §4.2.1 Tool Identification | 仿真工具链的版本号、构建配置、依赖关系均已锁定并文档化 | Tool version manifest (CI 自动生成) | `01-overview.md` §3.5; `00-vv-strategy-v0.1.md` §8.1 | 🟡 Projected |
| 4.2 | §4.2.2 License Compliance | 认证证据路径中使用的所有第三方工具需通过许可证审查 | `tools/ci/check-licenses.py` 扫描报告 | `06-evidence-matrix.md` §2.1 本条; `00-vv-strategy-v0.1.md` §8.2 | 🟡 Projected |
| 4.3 | §4.2.3 Deterministic Replay | 同一输入序列应产生同一输出，偏差在量化阈值内 | 1000 次重放重复性测试（±0.1s, ±0.5°） | `03-determinism-replay.md` | 🟡 Projected |
| 4.4 | §4.2.4 Model Fidelity | 仿真模型与真实物理系统的偏差已量化，并在可接受范围内 | MMG 4-DOF RMS ≤ 5% vs 参考解 | `02-model-fidelity-report.md` | 🟡 Projected |
| 4.5 | §4.2.5 Tool Confidence | 工具整体置信度评分（包含传感器退化、编排可靠性等子维度） | 传感器退化矩阵 + 编排 trace 审计 | `04-sensor-confidence.md` §3–§5; `05-orchestration-trace.md` §3–§5 | 🟡 Projected |

### §2.2 §5 — Model Assurance Levels

| # | RP-0513 Clause | Requirement Summary ⚫ | Evidence Location | Deliverable File | Status |
|---|---|---|---|---|---|
| 5.1 | §5.3.1 Input Validation | FMU 输入变量范围检查、类型检查、边界值拒绝机制 | FMU variable boundary validation test | `02-model-fidelity-report.md` annex/test-results/; `01-overview.md` §4 FMU interface contract | 🟡 Projected |
| 5.2 | §5.3.2 Output Validation | FMU 输出值的物理范围检查（如速度 ≤ 50 kn, ROT ≤ 30°/s） | Output constraint verification in `sil_orchestrator` | `05-orchestration-trace.md` §3.3 C6 | 🟡 Projected |
| 5.3 | §5.3.3 Integrator Accuracy | 数值积分算法（RK4 / CVODE BDF）的步长收敛性验证 | RK4 step-halving convergence test (Δt=0.01s vs 0.005s) | `02-model-fidelity-report.md` annex/plots/; `01-overview.md` §7 integrator convergence | 🟡 Projected |
| 5.4 | §5.3.4 Parameter Sensitivity | MMG 模型关键参数（水动力导数）对仿真输出的敏感度分析 | ±10% parameter perturbation, impact on turning circle D/t | `02-model-fidelity-report.md` annex/csv/ | 🟡 Projected |
| 5.5 | §5.3.5 Model Limitations | 模型适用范围的显式声明（如不允许倒车、低速 < 2kn 不适用等） | MMG 4-DOF model limitation declaration | `02-model-fidelity-report.md` §2 model scope; `01-overview.md` §2.2 | 🟡 Projected |

### §2.3 §6 — Scenario-Based Validation

| # | RP-0513 Clause | Requirement Summary ⚫ | Evidence Location | Deliverable File | Status |
|---|---|---|---|---|---|
| 6.1 | §6.2.1 Scenario Representativeness | 测试场景需覆盖预期的运行条件范围，具有统计代表性 | 35 YAML 场景库（D1.6 产出） | D1.6 scenario schema; SIL 架构设计 v1.0 §4 scenario builder | 🔴 Blocked — 待 D1.6 交付 |
| 6.2 | §6.2.2 Coverage Metrics | 场景对 ODD 维度的覆盖度需量化 | 覆盖率方法论 D1.7；CG-0264 §6 环境参数维度 | `D1.7-coverage-methodology.md`（待产出） | 🔴 Blocked — 待 D1.7 交付 |
| 6.3 | §6.2.3 Fault Injection | 故障注入场景应覆盖传感器退化、通信丢失、FMU 异常 | 7-test nominal-degraded matrix (S01–S07) | `04-sensor-confidence.md` §4.1; `03-determinism-replay.md` §4 fault scenarios | 🟡 Projected |
| 6.4 | §6.2.4 Statistical Significance | 每个场景的执行次数需满足统计置信度要求 | 1000 次重放测试；G-test threshold | `03-determinism-replay.md` §5 statistical analysis | 🟡 Projected |

### §2.4 §7 — Tool Integration & Data Flow

| # | RP-0513 Clause | Requirement Summary ⚫ | Evidence Location | Deliverable File | Status |
|---|---|---|---|---|---|
| 7.1 | §7.2.1 End-to-End Traceability | 从场景定义到仿真输出再到证据报告的完整追溯链 | libcosim API trace JSONL → rosbag2 MCAP → MARZIP evidence pack | `05-orchestration-trace.md` §3–§4 | 🟡 Projected |
| 7.2 | §7.2.2 Data Format & Schema | 中间数据格式应符合开放标准，schema 需验证 | maritime-schema 验证；rosbag2 MCAP 格式 | D1.6 scenario schema; `01-overview.md` §2.3 data flow diagram | 🟡 Projected |
| 7.3 | §7.2.3 Reproducibility | 相同的场景 + 相同的工具版本应产生相同的结果 | 确定性重放测试（seed locking + version pinning） | `03-determinism-replay.md` §3 replay protocol | 🟡 Projected |

---

## §3 V&V Plan §8 DNV Entry Conditions — Checkpoint Table

对照 V&V Plan v0.1 §8 DNV Toolchain Entry Conditions，D1.3.1 当前状态如下。

| # | Entry Condition | 要求 | D1.3.1 状态 | 差距 | 闭口路径 |
|---|---|---|---|---|---|
| EC-1 | Tool version manifest 就绪 | CI 每 build 生成 | 🟡 方法论完整 | CI pipeline 待集成 D1.3.1 trace | D1.4 CI 集成 |
| EC-2 | Deterministic replay log 通过 | 1000 runs, ±0.1s, ±0.5° | 🔴 未运行 | FMU 尚未最终集成 | D1.3a ship_dynamics FMU 集成 |
| EC-3 | FMU compliance report 通过 | FMIChecker 无错误 | 🔴 未运行 | FMU 开发中 | D1.3a FMU 合规导出 |
| EC-4 | Schema validation report 通过 | maritime-schema v0.2.x | 🔴 未运行 | 场景 schema 待 D1.6 | D1.6 场景 schema |
| EC-5 | License scan report 通过 | `check-licenses.py` 无 GPL | 🟡 可运行 | 待 CI 集成 | D1.4 CI 集成 |
| EC-6 | Sensor degradation model 验证 | CG-0264 §6 退化参数 | 🟡 方法论完整 | 实测数据待 D1.3b.3 | D1.3b.3 sensor_mock |
| EC-7 | Orchestration trace 就绪 | libcosim API trace JSONL | 🟡 设计完整 | trace 工具待开发 | D1.3.1 本 sprint |
| EC-8 | Model fidelity RMS ≤ 5% | vs 参考解 | 🔴 未运行 | MMG 参数校准中 | D1.3a 校准 |
| EC-9 | Coverage cube 定义完成 | 3-axis 覆盖率模型 | 🔴 未定义 | 待 D1.7 | D1.7 覆盖率方法论 |
| EC-10 | Evidence pack export 就绪 | MARZIP 导出 | 🔴 代码未写 | 待 D2.5 | D2.5 SIL 集成 |

**当前状态：0/10 绿，0/10 红（实际方法论），全部待上游交付。**

---

## §4 ISO 26262 TCL-3 Analogy

本模拟器工具拟用于 SIL 2 安全功能验证，其置信度等效于 ISO 26262 TCL-3（Tool Confidence Level 3——可能直接影响安全目标）。下表建立 TCL-3 证据与 D1.3.1 证明的映射。

| ISO 26262 TCL-3 Evidence | 类比要求 | D1.3.1 对应证明 | 状态 |
|---|---|---|---|
| **TD-1** (Tool Use Case Analysis) | 声明工具可能引入的错误类型和检测方法 | `04-sensor-confidence.md` §3 degradation models——声明退化模型参数误差引入的决策偏差 | 🟡 |
| **TD-2** (Tool Qualification Method) | 展示工具验证方法的充分性（如穷举测试、重放测试、交叉验证） | `03-determinism-replay.md` §3 + `05-orchestration-trace.md` §3——确定性重放 + step 审计 | 🟡 |
| **TD-3** (Tool Operational Requirements) | 定义工具的正确使用方式和限制条件 | `01-overview.md` §2 scope + `02-model-fidelity-report.md` §2 model limitations | 🟡 |

**TCL-3 判定**：在当前证据状态下（🟡 Projected），工具置信度至少达到 TCL-2（已验证工具使用案例和方法论）。待 D1.3b.3 和 D1.3a 数据到位后，可升级至 TCL-3。

---

## §5 Gap Closure Plan

以下差距需在 DEMO-1（6/15 Skeleton Live）前关闭。

| # | Gap ID | 描述 | 严重度 | 责任人 | 目标日期 | 闭口证据 |
|---|---|---|---|---|---|---|
| GAP-031 | D1.3a ship_dynamics FMU 未集成 | 无法运行确定性重放测试 (EC-2/EC-3/EC-8) | 🔴 P0 | D1.3a 开发者 | 2026-05-28 | FMIChecker pass report + 首次重放日志 |
| GAP-032 | DNV-RP-0513 完整 PDF 未购买 | §2 和 §3 的条款文本无法精确确认 | ⚫ 基础设施 | 项目负责人 | 2026-05-22 | DNV PDF 采购确认邮件 |
| GAP-033 | D1.3b.3 sensor_mock 为 stub | 传感器退化矩阵无法实测 (EC-6) | 🔴 P0 | D1.3b.3 开发者 | 2026-06-01 | sensor_mock_node v0.2 with degradation |
| GAP-034 | `tools/sil/d1_3_1_orch_trace.py` 未开发 | 编排审计无法自动化 (EC-7) | 🟡 P1 | V&V 工程师 | 2026-05-30 | trace tool CI job pass |
| GAP-035 | D1.6 场景 schema 未产出 | 场景代表性 / schema 验证无法运行 (EC-4/EC-9) | 🔴 P0 | V&V 工程师 | 2026-06-05 | schema validate pass on 35 YAMLs |
| GAP-036 | D1.7 覆盖率方法论未产出 | 覆盖率指标无法定义 (EC-9) | 🟡 P1 | V&V 工程师 | 2026-06-10 | coverage cube definition doc |

---

## §6 References

| Ref | Source | Description |
|---|---|---|
| [R-DNV-RP-0513] | DNV-RP-0513 (2023) *Software in Certification* | 本文档的核心映射对象 |
| [R-DNV-CG-0264 §6] | DNV-CG-0264 (2023) §6 *Environmental Limits* | ODD 环境包络定义 |
| [R-DNV-RP-0671] | DNV-RP-0671 (2023) *Model-Based Verification* | 模型保证补充指南 |
| [R-ISO-26262-8 §11] | ISO 26262-8:2018 §11 | TCL-3 工具置信度分级 |
| [R-IEC-61508-3 §7.4.4] | IEC 61508-3:2010 §7.4.4 | SIL 2 软件工具验证要求 |
| [R-VVP §8] | V&V Plan v0.1 §8 *DNV Toolchain Entry Conditions* | 入口条件定义 |
| [R-VVP §9] | V&V Plan v0.1 §9 *Certification Evidence Tracking* | 认证证据分类 |
| [R-ARCH v1.1.2] | 架构报告 v1.1.2 | 全系统架构基线 |
| [R-NLM:silhil_platform] | NLM silhil_platform 笔记本 | SIL/HIL 平台参考研究 |
