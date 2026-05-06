# 阶段 1 · 引用预扫

> 对 v1.0 §16 的 [R1]–[R24] 共 24 条引用做真实性核验：是否存在、DOI/URL 是否有效、原文是否支持 v1.0 内联引用处的论断。
>
> 验证方式：quick-researcher subagent（Haiku 4.5）× 2 并行 + WebSearch + WebFetch  
> Subagent A（R1–R12）：`a970c3409527b98bb`  
> Subagent B（R13–R24）：`ad9ec9fc6ab4ce12e`  
> **核验版本**：WebSearch/WebFetch 实际验证（非知识库生成），2026-04-30 15:50  

---

## 汇总

| 状态 | 数量 | 占比 | 引用 |
|---|---|---|---|
| 🟢 High（exists ✅ + doi_valid ✅ + supports_claim ✅） | 11 | 46% | R1, R5, R6, R7, R13, R14, R15, R16, R18, R19, R20 |
| 🟡 Medium（一项或多项有轻微问题） | 8 | 33% | R2, R4, R10, R11, R17, R22, R23, R24 |
| 🔴 Low（不支持论断 / 强度不足 / 无法验证关键细节） | 5 | 21% | R3, R8, R9, R12, R21 |
| ⚫ Unknown（查不到） | 0 | 0% | — |

**spec DoD 阈值**：🔴/⚫ 数 ≤ 5 → ✅ **通过**（5 = 阈值边界）

---

## Batch 1 · R1–R12

| ref_id | title_short | exists | doi_valid | supports_claim | confidence | domain_notebook | notes |
|---|---|---|---|---|---|---|---|
| R1 | CCS 智能船舶规范 2024/2025 | ✅ | N/A | ✅ | 🟢 | maritime_regulations | CCS 官网确认 2024 及 2025 版均存在；i-Ship 标志组合（Nx/Ri/Ai）+ 白盒可审计要求对应 §1.3。[源](https://www.ccs.org.cn/ccswz/specialDetail?id=202312061041161178) |
| R2 | IMO MSC 110 MASS Code Ch.15 | ✅ | N/A | ⚠️ | 🟡 | maritime_regulations | MSC 110 (2025-06) 确认召开；但 MASS Code Ch.15 仍为草案（预计 MSC 111/2026-05 采纳）；v1.0 引用时效性有限。[源](https://www.imo.org/en/mediacentre/meetingsummaries/pages/msc-110th-session.aspx) |
| R3 | MOOS-IvP Benjamin et al. 2010 | ✅ | ✅ (10.1002/rob.20370) | ⚠️ | 🔴 | colav_algorithms | 论文存在（J. Field Robotics 27(6):834–875）；确认涉及 IvP 多目标优化和 Backseat Driver 范式；但**无法从公开文本确认直接论证"多船型参数化扩展"**——§1.3 C3 引用错位嫌疑成立，需人工阅读原文第 4 章。[源](https://www.robots.ox.ac.uk/~mobile/Papers/2010IJFR_newman.pdf) |
| R4 | Veitch et al. 2024 Ocean Engineering | ✅ | ⚠️ (Elsevier 重定向) | ✅ | 🟡 | maritime_human_factors | 论文存在（Ocean Engineering 299:117257）；摘要确认 n=32 + 60s 时窗实验设计；全文 paywall，无法从 web 逐条核验所有数字。[源](https://www.sciencedirect.com/science/article/pii/S0029801824005948) |
| R5 | IEC 61508-3:2010 | ✅ | N/A（规范） | ✅ | 🟢 | safety_verification | IEC 官方 webstore 确认存在；SIL 等级 + 可分性要求是标准核心。[源](https://webstore.iec.ch/en/publication/5517) |
| R6 | ISO 21448:2022 SOTIF | ✅ | N/A（规范） | ✅ | 🟢 | safety_verification | ISO 官网确认存在；functional insufficiency 是核心概念；海事类比应用合理。[源](https://www.iso.org/standard/77490.html) |
| R7 | Yasukawa & Yoshimura 2015 MMG | ✅ | ✅ (10.1007/s00773-014-0293-y) | ✅ | 🟢 | ship_maneuvering | 论文存在（J. Marine Sci. Tech. 20(1):37–52）；MMG 标准方法 + 水动力参数预测确认。[源](https://link.springer.com/article/10.1007/s00773-014-0293-y) |
| R8 | Rødseth et al. 2022 ODD | ✅ | ✅ (10.1007/s00773-021-00815-z) | ❌ | 🔴 | safety_verification | **关键错误（PMC 全文确认）**：原文定义 ODD 为 **2 轴（State Space O × Function Mapping FM）**，不是 v1.0 §2.2 / §3.2 的**三轴（E×T×H）**。TMR/TDL 人机责任分配维度确认存在，但三轴框架是 v1.0 改写。[源](https://pmc.ncbi.nlm.nih.gov/articles/PMC8065322/) |
| R9 | DNVGL-CG-0264 2018 | ✅ | ✅ (PDF URL 有效) | ⚠️ | 🔴 | maritime_regulations | PDF 文件可下载；**但 PDF 文本提取失败（403/PDF 格式限制），无法验证 §4 是否真有 9 个子功能（§4.2–§4.10）**；DNV 官方分类表述为 4 大类别而非 9 个子功能。需直接阅读原文。[源](https://maritimesafetyinnovationlab.org/wp-content/uploads/2020/09/DNVGL-CG-0264-Autonomous-and-remotely-operated-ships.pdf) |
| R10 | Urmson et al. 2008 CMU Boss | ✅ | ✅ (10.1002/rob.20255) | ⚠️ | 🟡 | colav_algorithms | 论文存在（J. Field Robotics 25(8):425–466）；三层规划栈（Mission / Behavioral / Motion）确认；**PDF 全文提取失败，"Perception & World Modeling 独立子系统"论断无法从摘要层面验证**。[源](https://www.cmu.edu/traffic21/pdfs/urmson_christopher_2008_1.pdf) |
| R11 | Chen et al. 2014 SAT | ✅ | ⚠️ (DTIC 403) | ⚠️ | 🟡 | maritime_human_factors | 报告存在；**报告号错误：v1.0 写 ARL-TR-7180，实际是 ARL-TR-6905**（2014 年 4 月，DTIC ADA600351 可找到 PDF）；SAT 三层透明度模型确认；"全部可见时工作负荷不增"为定性描述，非严格量化结论。[源](https://apps.dtic.mil/sti/pdfs/ADA600351.pdf) |
| R12 | Jackson et al. 2021 Certified Control | ✅ | ✅ (arXiv:2104.06178) | ❌ | 🔴 | safety_verification | 论文存在；Doer-Checker 分离论证正确；**但原文无"100×简单"量化数字**，仅说"smaller and formally verifiable"。v1.0 §2.5 / CLAUDE.md §4 的"简单 100× 以上"无来源。[源](https://arxiv.org/abs/2104.06178) |

### Batch 1 小结

🟢 可信引用（6 条）：R1, R5, R6, R7 完全通过；R4, R10 存在 paywall / PDF 限制但核心论断初步确认。  
🔴 需要立即处理（4 条）：**R8**（二轴 vs 三轴，PMC 全文确认为 2 轴）、**R12**（"100×"无量化来源）、**R3**（§1.3 C3 引用错位嫌疑）、**R9**（9 子功能无法验证）。  
🟡 小修复（2 条）：R2（MSC 110 时效）、R11（报告号改正）。

---

## Batch 2 · R13–R24

| ref_id | title_short | exists | doi_valid | supports_claim | confidence | domain_notebook | notes |
|---|---|---|---|---|---|---|---|
| R13 | Albus NIST RCS 1991 | ✅ | ⚠️ (IEEE paywall) | ✅ | 🟢 | colav_algorithms | IEEE Trans. SMC 21(3):473–509 确认；NIST RCS 分层 World Model 原则支持引用。[源](https://discovery.researcher.life/article/outline-for-a-theory-of-intelligence/d4846ba66ce63d7aa5876e58ef990cf0) |
| R14 | Fjørtoft & Rødseth 2020/2021 ODD | ✅ | ✅ | ✅ | 🟢 | safety_verification | **注意**：实际发表于 J. Marine Sci. Technol. 2021（Springer）而非单纯 NMDC 2020 会议；标题"Towards approval of autonomous ship systems..."直接支持 ODD-as-safety 主张。[源](https://link.springer.com/article/10.1007/s00773-021-00815-z) |
| R15 | MUNIN D5.2 ASM 2015 | ✅ | N/A | ✅ | 🟢 | ship_maneuvering | FP7 Project 314286 确认（CORDIS）；Fraunhofer CML 主持；D5.2 Advanced Sensor Module 设计文档经学术引用核实。[源](https://cordis.europa.eu/project/id/314286) |
| R16 | Pirjanian 1999 BCM | ✅ | N/A | ✅ | 🟢 | colav_algorithms | USC IRIS-99-375 确认为 **1999 年 10 月**（Semantic Scholar 等）；v1.0 §16 "1999" 正确，§8.2/§8.5 写"1998"是文档内部错误，须 patch。[源](https://www.semanticscholar.org/paper/Behavior-Coordination-Mechanisms-State-of-the-art-Pirjanian/8c96ec7c7ce1416af21155a444255cc97dee182d) |
| R17 | Wang 2021 JMSE Stand-On Vessel | ✅ | ⚠️ (MDPI 403) | ⚠️ | 🟡 | colav_algorithms | **关键发现（标题不符）**：DOI 10.3390/jmse9060584 对应真实论文，但标题是 *"A Decision-Making Method for Autonomous CA for the Stand-On Vessel Based on Motion Process and COLREGs"*，而非 v1.0 R17 中的"Dynamic Window Method and Quantum Particle Swarm Optimization"。论文涉及四阶段时间分析；但 **T_standOn 8/6/10 min 和 T_act 4/3/5 min 具体数字无法从摘要层验证**（全文 paywall）。 |
| R18 | IMO COLREGS 1972/2002 | ✅ | N/A（公约） | ✅ | 🟢 | maritime_regulations | IMO 公约确认；Rules 5,6,7,8,9,13–19 覆盖无误。[源](https://www.imo.org/en/about/conventions/pages/colreg.aspx) |
| R19 | Bitar/Eriksen et al. 2019 arXiv | ✅ | ✅ (arXiv:1907.00198) | ✅ | 🟢 | colav_algorithms | arXiv 确认；**注意**：一作是 Eriksen，不是 Bitar（v1.0 §16 写"Bitar et al."有误，应改为 Eriksen et al. 2019）；COLREGs Rules 8 & 13–17 内容完全支持。[源](https://arxiv.org/abs/1907.00198) |
| R20 | Eriksen et al. 2020 Frontiers | ✅ | ✅ (10.3389/frobt.2020.00011) | ✅ | 🟢 | colav_algorithms | Frontiers Robotics & AI 7:11 确认；BC-MPC 分支树算法在第 2 层中层避碰模块有详述；完全支持架构引用。**注**：R19 是本文 arXiv 预印版，R20 是期刊终版，两者重复引用（建议只保留 R20）。[源](https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2020.00011/full) |
| R21 | Yasukawa & Sano 2024 MMG 4-DOF | ⚠️ | N/A | ❌ | 🔴 | ship_maneuvering | **可能引用幻觉**：Web 搜索完全找不到"Yasukawa & Sano 2024"论文。现存最新相关论文：**Yasukawa et al. 2019**（J. Marine Sci. Tech. 24(4):1280–1296，4-DOF 含 roll motion）和 Yasukawa & Yoshimura 2015（R7，3-DOF）。"2024 年论文"极可能不存在；§10.5 的 Hs > 1.5m 波浪扰动项主张**无引用支撑**。 |
| R22 | Neurohr et al. 2025 SOTIF ROC | ✅ | ✅ (arXiv:2508.00543) | ✅ | 🟡 | safety_verification | arXiv 2508.00543 确认（v1 2025-08-01, v2 2025-11-14）；标题与内容完全匹配；**但仍为预印本，未见正式期刊发表**。建议注明预印本状态；6–12 个月后 re-verify。[源](https://arxiv.org/abs/2508.00543) |
| R23 | Veitch & Alsos 2022 | ✅ | ⚠️ (NTNU Open 重定向失败) | ⚠️ | 🟡 | maritime_human_factors | 论文存在（J. Phys. Conf. Ser. 2311(1)，NTNU Open）；标题"From captain to button-presser"确认；**v1.0 作者列不完整（实际含 Christensen 等多人）**；会议论文级别置信度偏低。[源](https://ntnuopen.ntnu.no/ntnu-xmlui/handle/11250/3041898) |
| R24 | Sea Machines SM300-NG 2024 | ✅ | N/A（官网） | ⚠️ | 🟡 | colav_algorithms | SM300-NG 系统存在且有船级社认证（ABS 验证）；**"200+ 艘、20 国"来自营销材料，无第三方独立验证**；v1.0 用此数据作工业验证论据需注脚标明来源为厂商自述。[源](https://sea-machines.com/sm300-ng/) |

### Batch 2 小结

🟢 可信（7 条）：R13, R14, R15, R16, R18, R19, R20 全部通过。  
🔴 需立即处理（1 条）：**R21**（2024 年论文很可能不存在，建议改引 Yasukawa et al. 2019）。  
🟡 待修（4 条）：R17（标题不符 + 数字待验）、R22（预印本）、R23（作者不完整）、R24（营销数据）。

---

## 升入阶段 3 反证议题清单

🔴 + 高影响 🟡 进入阶段 3 NLM 深度调研。

### P0 级（动摇顶层架构决策）

- **R8（二轴 vs 三轴）**：v1.0 §2.2/§3.2 用 R8 定义 Operational Envelope 为三轴（E×T×H），但 PMC 全文确认原文是二轴（O×FM）。**联动阶段 2 R3（ODD 工程实践）**：海事 ODD 主流是几轴？v1.0 的三轴表述是合理扩展还是改写？→ v1.1 需在引用处注明"在 R8 二轴基础上扩展"或改引其他三轴来源。

- **R12（Doer-Checker "100×简单" 无量化来源）**：v1.0 §2.5 + CLAUDE.md §4 中"Checker 比 Doer 简单 100× 以上"无学术支撑。**联动阶段 2 R4（Doer-Checker 落地）**：业界/航空/海事 SIL 认证中是否有量化的 Checker 简化倍率？如无，v1.1 改为定性描述 + `[Design Goal]` 标注。

- **R9（DNV-CG-0264 9 子功能无法验证）**：v1.0 §2.3 将 8 模块映射到 DNV §4 的 9 个子功能。PDF 文本提取失败，无法确认 §4.2–§4.10 结构是否真有 9 个子功能。**联动阶段 2 R7（CCS+DNV 规范分析）**：需直接阅读原文或由团队确认。

### P1 级（引用可能幻觉，需替换）

- **R21（Yasukawa & Sano 2024）**：论文未找到；§10.5 高速波浪扰动项无来源。建议改引 Yasukawa et al. 2019（4-DOF）或删除该论断。

### P1 级（措辞/数据修订，v1.1 patch list）

- **R3（§1.3 C3 引用松散）**：多船型参数化扩展非 R3 核心，建议 §1.3 改引 Capability Manifest 设计范式 + R24 工业实例。
- **R11（报告号错误）**：ARL-TR-7180 → 改为 ARL-TR-6905。
- **R16（§8.2/§8.5 年份错误）**：1998 → 1999。
- **R17（标题不符 + 阈值待验）**：获取全文核查 8/6/10 min 数字；若确认无此数字则替换来源或删除。
- **R19（一作错误）**：§16 引用改 "Bitar et al. 2019" → "Eriksen et al. 2019"。
- **R20（R19/R20 重复引用）**：建议只保留 R20（期刊终版），删除 R19 或注明 arXiv 预印版。
- **R22（预印本）**：注脚注明"arXiv preprint; re-verify upon journal publication"。
- **R23（作者不完整）**：补全作者列表。
- **R24（营销数据）**：标注来源为"厂商自述"。

---

## 已知可疑点最终状态

| 序号 | 可疑点 | 核验结论 |
|---|---|---|
| 1 | R3 §1.3 C3 引用错位 | **成立**：论文存在但不直接支持"多船型参数化"主张 |
| 2 | R8 三轴 vs 原文表述 | **严重成立**：原文 2 轴（O×FM），v1.0 三轴（E×T×H）是改写 |
| 3 | R12 "100×简单" 量化 | **成立**：原文无此数字，仅定性描述 |
| 4 | R11 报告号 | **成立**：应为 ARL-TR-6905，非 ARL-TR-7180 |
| 5 | R16 年份不一致 | **成立**：正确年份 1999，§8.2/§8.5 误写 1998 |
| 6 | R17 阈值数字来源 | **标题不符**：DOI 真实存在但对应不同标题的论文；数字无法从摘要验证 |
| 7 | R19/R20 重复引用 | **成立**：同一工作 arXiv + 期刊两版本，建议保留 R20 |
| 8 | R21 2024 发表时点 | **成立**：极可能引用幻觉，未找到对应论文 |
| 9 | R22 arXiv 预印本 | **确认预印本**：arXiv 2508.00543 存在但非正式发表 |
| 10 | R24 数字可信度 | **部分成立**：厂商营销数字，无第三方核实 |

---

## DoD 检查（spec §11）

- [x] 24 条引用全部预扫完成（Batch 1：12 条 + Batch 2：12 条 = 24 条）
- [x] 🔴/⚫ 数 ≤ 5（实际 5 = 阈值边界，✅ 通过）
- [x] 每条 finding 含 ref_id / title / exists / doi_valid / supports_claim / confidence / domain_notebook / notes 八字段
- [x] 每条结论有 WebSearch/WebFetch 来源支撑（非知识库生成）

**阶段 1 完成 ✅**（2026-04-30 16:00）
