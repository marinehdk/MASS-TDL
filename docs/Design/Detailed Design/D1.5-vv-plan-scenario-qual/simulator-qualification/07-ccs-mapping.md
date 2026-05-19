# 07 — CCS《智能船舶规范 2024》§9.1 性能验证条款映射

| 属性 | 值 |
|---|---|
| 文档编号 | SANGO-ADAS-L3-SIL-D1.3.1-07 |
| 版本 | v0.1-draft |
| 日期 | 2026-05-15 |
| 状态 | 在制 — 🟢 方法论完成 | ⚫ CCS §9.1 完整条款文本待 PDF 送达后逐条确认 |
| 上游 | D1.3.1 01–06 交付物；V&V Plan v0.1 §9.1（证据类别 C1–C9）|
| 下游 | CCS AIP 提交（D4.4 11/1）；CCS surveyor 现场审核清单 |
| 规范基线 | CCS《智能船舶规范 2024》§9.1（性能验证）；DNV-CG-0264 §3（Verification）；IMO MSC 110/111 MASS Code |

---

## §1 目的

本文档将 D1.3.1 仿真器鉴定报告的各项证据**逐条映射到 CCS《智能船舶规范 2024》§9.1 性能验证条款**，确保 CCS surveyor 在 AIP 审核中能够：

1. **快速定位**：每条 §9.1 条款 → D1.3.1 对应章节 → 证据所在交付物
2. **逐条打钩**：通过 §4 CCS surveyor audit checklist 逐项确认
3. **完整性自检**：通过 §5 acceptance checkboxes 确保 12 条 §9.1 条款全覆盖

⚫ **重要声明**：当前映射基于 CCS《智能船舶规范 2024》公开目录中 §9.1 的 12 个子条款编号（§9.1.1–§9.1.12）及其已知标题。CCS 完整 PDF 正文送达后，本文件须逐条核实并回填条款原文引用。当前条款摘要栏标注了基于公开信息的推定内容，置信度参见各子节标注。

---

## §2 CCS §9.1 条款映射

### §2.1 自主航行功能验证（§9.1.1–§9.1.4）

| CCS 条款 | 要求摘要 | D1.3.1 证据 | 对应交付物章节 |
|---|---|---|---|
| §9.1.1 — 避碰功能验证 | 仿真器须具备 COLREGs 避碰场景的完整验证能力，覆盖 Rule 13–18 全部相遇局面，验证决策逻辑在 ODD 边界内的正确性 | 02 模型保真度报告：MMG 4-DOF 模型在 35 个 YAML 避碰场景中的 RMS ≤ 5% 跟踪精度验证 [W1] | 02 §3 Model Test Coverage（含 COLREGs 场景分类矩阵）；03 §2 1000-run Determinism（含避碰子集）；01 §3.4 35 YAML Scenarios |
| §9.1.2 — 航路跟随验证 | 仿真器须验证航路点跟踪能力，包括 LOS 引导律、cross-track error ≤ 船宽 0.5×、转向半径符合操纵性约束 | 02 模型保真度报告中航路转弯段 RMS 动态精度 [W2]；03 确定性重放中航路跟随子集的 repeatability ±0.5° heading | 02 §4 CFD vs MMG Path Comparison；03 §2 Determinism Results；01 §3.4 WP-following Scenario Subset |
| §9.1.3 — 靠离泊验证 | 靠离泊自动控制功能的模拟验证 | **N/A** — L3 TDL 不承担靠离泊控制（该项属 L4 Guidance / L5 Control 范畴）[R-NLM:1]，在 D1.3.1 仿真器鉴定范围内标记为 N/A 并提供接口边界说明 | 01 §2 Scope Exclusions |
| §9.1.4 — 锚泊验证 | 锚泊状态检测与控制功能的模拟验证 | **N/A** — L3 TDL 不承担锚泊控制（该项属 L1 Mission 范畴 + L5 Control 执行）[R-NLM:1]，在 D1.3.1 鉴定范围内标记为 N/A | 01 §2 Scope Exclusions |

**置信度**：🟢 条款编号与标题来自公开目录，摘要为基于 DNV-CG-0264 §3 对应条款的推定。⚫ 条款完整正文待 CCS PDF 送达后确认。

### §2.2 模拟逼真度要求（§9.1.5–§9.1.8）

| CCS 条款 | 要求摘要 | D1.3.1 证据 | 对应交付物章节 |
|---|---|---|---|
| §9.1.5 — 船舶运动模型 | 仿真器采用的运动模型须经 CF（计算流体力学）数据或实船试验数据标定，模型精度满足验证目的，4-DOF（surge/sway/yaw/roll）响应与参考解偏差在可接受范围 | 02 模型保真度报告：MMG 4-DOF vs CFD BA-LSTM & RANS 参考解 RMS ≤ 5%（surge/sway/yaw），roll ≤ 10% [W3] | 02 §4 CFD Reference Path；02 §5 Fidelity Metrics Table；02 §2.2 MMG Parameter Baseline |
| §9.1.6 — 环境模型 | 仿真器须模拟风、流、浪等环境载荷，环境参数可配置，模型逼真度须经 IACS Rec.145 或等效标准验证 | 02 §2.1 环境建模：风采用 Isherwood 系数 / 流采用定常均匀流 / 浪采用 JONSWAP + 二阶漂移力 [W4] | 02 §2.1 Environment Model Specification；01 §3.4 Scenario YAML 环境参数模板 |
| §9.1.7 — 传感器模型 | 仿真器须模拟 GNSS、Radar、AIS 等传感器特性（噪声、延迟、遮挡、分辨率退化），模型退化行为与 DNV-CG-0264 §6 一致性 | 04 传感器置信度报告：GNSS 降级（SPS→DGNSS→RTK）误差包络 vs CG-0264 Table 6-1；Radar 检测概率 vs 距离/海况；AIS 延迟/丢包模型 [W5] | 04 §3 Sensor Degradation Matrices；04 §2 Degradation Mapping to CG-0264 §6 |
| §9.1.8 — 重复性与可复现性 | 仿真器须保证同输入产生同输出（deterministic replay），多次运行偏差在可接受容差内 | 03 确定性重放报告：1000 次 replay 全部在容差 ±0.1s（时间）、±0.5°（heading）、±0.2m（位置）[W6] | 03 §2 Determinism Results；03 §3 Repeatability Statistics |

**置信度**：🟢 条款编号与标题来自公开目录；§9.1.7 传感器条款有 DNV-CG-0264 §6 可作交叉引用锚点。⚫ 条款完整正文 + IACS Rec.145 具体章节待 CCS PDF 送达后确认。

### §2.3 验证可信度（§9.1.9–§9.1.12）

| CCS 条款 | 要求摘要 | D1.3.1 证据 | 对应交付物章节 |
|---|---|---|---|
| §9.1.9 — 仿真工具鉴定 | 仿真器须经正式工具鉴定（tool qualification），证明其满足特定验证目标的准确性、可靠性 | 06 证据矩阵：4 项证明（PM-1 模型保真度 / PM-2 确定性 / PM-3 传感器 / PM-4 追溯）→ DNV-RP-0513 Model Assurance Level MAL-2 逐条映射 [W7] | 06 §2 Proof Map to RP-0513；06 §3 MAL-2 Compliance Checklist |
| §9.1.10 — 验证场景覆盖 | 仿真验证场景须覆盖 ODD 范围内全部工况，覆盖率经方法论证明充足（包括正常、降级、边界、失效模式） | 01 §3.4 场景清单：35 个 YAML 场景覆盖 12 类别（COLREGs × 环境 × 故障）[W8]；06 §4 Coverage→CCS Mapping 矩阵 | 01 §3.4 Scenario Inventory & Classification；01 §3.5 Coverage Rationale |
| §9.1.11 — 统计显著性 | 仿真结果须具备统计显著性（足够数量的蒙特卡洛运行），不确定性量化（UQ）须提供置信区间 | 03 确定性重放 + 统计检验：1000-run CI(95%) [W9]；02 §6 UQ 量化中 MMG 参数敏感度的 Sobol' 指数 | 03 §4 Statistical Summary；02 §6 Uncertainty Quantification |
| §9.1.12 — 验证记录与追溯 | 仿真验证全流程须有完整追溯链：场景定义 → 执行参数 → 原始输出 → 后处理 → 结论，支持独立审计重现 | 05 编排追溯报告：libcosim API trace + 通信步长审计 + 每 run 的 Git 版本/SHA/host 信息 [W10] | 05 §3 End-to-End Trace Chain；05 §4 Auditability Checklist |

**置信度**：🟢 条款编号与标题来自公开目录；§9.1.9 工具鉴定有 DNV-RP-0513 可作交叉引用锚点；§9.1.10 覆盖有 IMO MSC 110/111 可交叉引用。⚫ 条款完整正文待 CCS PDF 送达后确认。

---

## §3 CCS i-Ship N 功能标志映射

CCS 智能船舶附加标志 i-Ship (Nx, Ri/Ai) 要求 L3 TDL 证据覆盖以下功能项。下表列出各标志与 D1.3.1 交付物的对应关系。

| CCS i-Ship 标志 | 功能要求 | D1.3.1 覆盖 | 说明 |
|---|---|---|---|
| **N1** — 自主避碰 | COLREGs Rule 13–18 全工况避碰决策 | **02 + 03** | 02 模型保真度覆盖 ship dynamics 对避碰决策的影响；03 确定性重放覆盖避碰子集 |
| **N2** — 自主航路 | 航路点自动跟踪 + cross-track 控制 | **02 + 03** | 02 CFD 路径对比覆盖航路跟随精度；03 覆盖 WP-following scenario 确定性 |
| **N3** — 自主遥控** | 岸基遥控接管 | **N/A** | L3 TDL 不承担遥控接管通道（属 Shore Link + L1 Supervisor 范畴），在 D1.3.1 范围外 [R-NLM:1] |
| **Ri** — 可靠性 | 系统可靠性证据（故障率、MTBF、降级策略） | **03** | 1000 次 replay 可靠性统计 + 降级模式注入场景（sensor fail, ODD boundary）|
| **Ai** — 自主等级 | 自主能力从辅助到完全自主的渐进验证 | **01–06 全覆盖** | Phase 1 (Skeleton)→Phase 2 (Decision-Capable)→Phase 3 (Full-Stack) 渐进递进，每 Phase 产出对应证据增量 |

**注**：N3（自主遥控）标记 N/A 仅对 L3 TDL 本仓库适用。全船级认证时，该标志由 Shore Link + L1 Supervisor + Cybersecurity 团队独立举证，CCS AIP 提交包中须提供跨层接口声明（待 D3.8 v1.1.3 完整版补充）。

---

## §4 CCS Surveyor 审核清单

以下清单用于 CCS surveyor 在 AIP 审核中逐项确认 D1.3.1 证据完整性。每项对应 §2 条款映射中的一条。

| # | 核查项 | 对应 §9.1 条款 | D1.3.1 对应章节 | 审核签名 |
|---|---|---|---|---|
| A1 | 避碰功能验证：COLREGs 场景覆盖是否完整，决策逻辑是否在 ODD 边界内验证 | §9.1.1 | 02 §3 + 03 §2 + 01 §3.4 | |
| A2 | 航路跟随验证：WP 跟踪精度是否可接受，cross-track error 是否在阈值内 | §9.1.2 | 02 §4 + 03 §2 + 01 §3.4 | |
| A3 | 靠离泊验证：N/A 声明是否合理，接口边界是否有充分说明 | §9.1.3 | 01 §2 | |
| A4 | 锚泊验证：N/A 声明是否合理，接口边界是否有充分说明 | §9.1.4 | 01 §2 | |
| B1 | 船舶运动模型：MMG 4-DOF 标定数据是否可追溯，RMS ≤ 5% 是否满足 | §9.1.5 | 02 §4 + 02 §5 + 02 §2.2 | |
| B2 | 环境模型：风/流/浪模型是否经 IACS Rec.145 或等效验证 | §9.1.6 | 02 §2.1 + 01 §3.4 | |
| B3 | 传感器模型：退化行为是否与 CG-0264 §6 一致 | §9.1.7 | 04 §3 + 04 §2 | |
| B4 | 重复性：1000-run determinism 结果是否在容差内 | §9.1.8 | 03 §2 + 03 §3 | |
| C1 | 仿真工具鉴定：RP-0513 MAL-2 映射是否完成 | §9.1.9 | 06 §2 + 06 §3 | |
| C2 | 场景覆盖：35 场景是否充分覆盖 ODD，分类逻辑是否合理 | §9.1.10 | 01 §3.4 + 01 §3.5 + 06 §4 | |
| C3 | 统计显著性：1000-run CI(95%) 是否计算，UQ 是否提供 | §9.1.11 | 03 §4 + 02 §6 | |
| C4 | 验证记录追溯：libcosim trace + Git/SHA 是否可审计重现 | §9.1.12 | 05 §3 + 05 §4 | |

---

## §5 验收检查项（AC7.1–AC7.4）

以下 4 项为 D1.3.1 向 CCS surveyor 提交前的自检验收检查项，由 V&V 工程师在 DEMO-1（6/15）前确认。

| 编号 | 检查项 | 通过判据 | 状态 |
|---|---|---|---|
| **AC7.1** | 12 条 §9.1 条款全部可映射到至少一个 D1.3.1 交付物章节 | §2 三个子节表格中所有行在"对应交付物章节"列非空（N/A 条款须有 §1 声明） | ☐ |
| **AC7.2** | i-Ship N 功能标志全表无遗漏 | §3 表中 N1/N2/N3/Ri/Ai 全部有对应行，N3 N/A 声明包含跨层接口说明 | ☐ |
| **AC7.3** | CCS surveyor 审核清单 12 项全部可独立核查 | §4 表中每项"对应章节"包含具体子节编号，surveyor 可无上下文定位 | ☐ |
| **AC7.4** | 所有 ⚫ 标记项（CCS 完整 PDF 待确认）已建立追踪 | ⚫ 标记在 §2 三子节中仅限"条款完整正文"列，其余列均为 🟢 或 🟡；已建立 D3.8 CCS-PDF-received 回填任务 | ☐ |

---

## §6 参考文献

| 编号 | 引用 | 说明 |
|---|---|---|
| [R1] | CCS《智能船舶规范 2024》§9.1 性能验证 | ⚫ 完整 PDF 待送达，当前映射基于公开目录 |
| [R2] | DNV-CG-0264 §3 Autonomous and remotely operated ships — Verification | 🟢 传感器退化锚点（§2.1.7）+ 运动模型锚点（§2.1.5）|
| [R3] | DNV-RP-0513 Model Quality Assurance for Simulation Models | 🟢 §9.1.9 工具鉴定映射基准 |
| [R4] | IMO MSC 110/111 — MASS Code | 🟢 场景覆盖充分性论证锚点（§9.1.10）|
| [R5] | IACS Rec.145 — Guidelines for the Modelling of Environmental Loads | ⚫ 环境模型验证引用，待确认 CCS 是否明确引用该 Rec |
| [R6] | SANGO-ADAS-L3-VVP-001 V&V Plan v0.1（2026-05-31）§9.1 证据类别 C1–C9 | 🟢 证据分类框架来源 |
| [R-NLM:1] | SIL/colav NLM 笔记本查询：L3 TDL Scope Boundary → 靠离泊/锚泊/遥控接管归口 | 🟢 接口边界裁决 |
