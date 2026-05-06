# 决策调研对话记录

> **用途**：记录架构核心决策调研过程中的问答要点，以及顺带发现的文档问题。  
> **范围**：只记录本对话中实际问答的内容；发现文档问题则附修改建议。修复统一在调研结束后进行。

---

## Q1：Operational Envelope 在 IMO MASS Code 中是什么含义？MSC 110(2025) §15 为何要求系统独立识别 OE 越界？

**调研方式**：`nlm-research --depth fast`，入库 8 个来源（2026-04-30）

### 答复摘要

**OE 定义**（来源：MSC 108/4 Report of the MASS Correspondence Group，[PDF](https://rs-class.org/upload/iblock/01f/01f10869d178a34e3d02234862c56fad.pdf)）

> Operational Envelope = 船舶"操作能力与限制及船型专属能力与限制的集合，用以指示自主或遥控船舶功能在所有运行条件（含所有合理可预见降级状态）下能安全运行的条件"。

OE 须显式包含：MASS 功能定义与用例、地理作业范围（含通信覆盖与交通条件）、环境限制、各航次阶段操作限制、人机功能分配与运行模式管理、其他重大影响因素。

ISO/TS 23860 互补定义（经 [Rødseth et al. 2022, PMC](https://pmc.ncbi.nlm.nih.gov/articles/PMC8065322/) 引用）：
> "自主船舶系统被设计运行的条件及相关操作控制模式，包括所有可容忍事件。"

**Fallback state**（同上来源）：
> "当 MASS 无法继续保持在其 Operational Envelope 内时所进入的设计状态。"

**OE 识别要求的实质条款位置**（MSC 108/4 草案）：

| 位置 | 要求 |
|---|---|
| Part 2, Ch.1（Operational Context） | 系统须能检测当前运行状态是否满足 ODD；偏离时须进入 fallback state 并采取适当措施 |
| Part 3, Ch.6（Alert Management） | 须在进入或即将进入 fallback state 时生成报警；任何条件即将偏离或已偏离预定运行条件时生成报警 |

**调研置信度**：🟡 Medium  
（MSC 108/4 PDF = A 级；MSC 110/5/1 Scribd = C 级；IMO MSC 110/23 最终文本未直接获取）

---

### 顺带发现的文档问题

#### 问题 1：[R2] §15 章节号与"OE 识别"要求的对应关系存疑

架构报告 §2.3 第 95 行 + 参考文献第 416、1116 行的表述：
> "IMO MASS Code 草案（MSC 110，2025）**在第 15 章明确要求**：系统必须能识别船舶是否处于 Operational Envelope 之外"

调研发现 Chapter 15 的标题在不同草稿中并不相同：

| 草案版本 | Ch.15 标题 |
|---|---|
| MSC 108/4（[PDF](https://rs-class.org/upload/iblock/01f/01f10869d178a34e3d02234862c56fad.pdf)） | **MAINTENANCE AND REPAIR** |
| MSC 110/5/1 法德印修正提案（[Scribd](https://www.scribd.com/document/857460983/MSC-110-5-1-Proposed-Amendments-to-chapter-15-Human-Element-of-the-MASS-Code-France-Germany-Indonesi)） | **HUMAN ELEMENT** |

"OE 识别"的实质条款位于 Part 2 Ch.1 + Part 3 Ch.6（见上方答复），而非 Ch.15。

**受影响行**：报告第 63、95、416、1116 行

**修改建议**：  
- 若 MSC 110/5/1（Ch.15 Human Element）已正式通过，需将引用论断对应到该章具体条款，而非泛称。  
- 若 Ch.15 正式文本不直接覆盖"OE 识别"，建议改引 Part 2 Ch.1 + Part 3 Ch.6，附 [R2] 作为 MSC 110 定稿的佐证。  
- **验证路径**：从 IMO IMODOCS 获取 MSC 110/23 正文，确认最终章节号与条款内容。

---

#### 问题 2："独立于任何具体的导航算法"缺乏来源支撑

架构报告第 95 行在 [R2] 引用处附加：
> "这一判断必须**独立于任何具体的导航算法**"

MSC 108/4、MSC 110/5/1、ISO/TS 23860、VOCAX 术语表、Rødseth et al. 2022 均未出现此措辞。  
此限定疑为设计意图混入了规范引用。

**调研置信度**：🔴 Low（来源不支持此具体论断）

**受影响行**：报告第 95 行

**修改建议**：将设计意图与规范要求分句，例如：
> "IMO MASS Code 要求系统必须能识别是否处于 OE 之外 [R2]。本架构进一步要求该判断与具体导航算法解耦——由 M1 独立承担，而非由 M5/M6 内嵌——以满足 IEC 61508 功能独立性约束 [Rx]。"

---

---

## Q2：三层职责架构和 8 个模块是怎么来的？DNV-CG-0264 的 9 个功能 L3 需要全部覆盖吗？

**调研方式**：`nlm-research --depth fast`，新入库 10 个来源（含 DNV-CG-0264 原文 PDF）；`nlm-ask` 精准查询（2026-04-30）

### 答复摘要

**三层架构来源**（来源：架构报告 §2.3 第 116 行 [R10]）

来自 CMU Boss（DARPA Urban Challenge 冠军）三层规划栈——Mission Planning / Behavioral Executive / Motion Planning——的工业最佳实践。海事适配将最底层扩展为"安全接口层"（M7 Checker + M8 HMI），因为这两者在无人车架构中没有同等地位的对应物。

**8 个模块的来源**

从 7 个候选模块（算法导向）到 8 个，实质是将分散在各模块的安全监督逻辑单独拉出为 M7——Doer-Checker 模式的强制要求（Checker 不能与 Doer 共享实现路径）。架构报告 §2.3 第 108–113 行有与两个候选方案的对比表。

**DNV-CG-0264 9 个子功能的实际范围**（来源：DNVGL-CG-0264 PDF，[SMASH Roadmap](https://www.smashroadmap.com/files/dnvgl-cg-.pdf)）

DNV-CG-0264 Ch.4 定义这 9 个功能为"整船航行系统正常运营期间须覆盖的主要导航功能"，覆盖整个决策栈（航次规划 → 机动规划，但不含推力执行器控制）。DNV 明确说最底层 Action control 是"传统系统，不提供进一步指导"。

各子功能的层级归属（基于 DNV 原文）：

| DNV 子功能 | DNV 原文定位 | L3 实际负责？ |
|---|---|---|
| §4.2 Planning prior to voyage | 航次规划层（出发前验证航路） | ❌ 应为 L2 |
| §4.3 Condition detection | 态势感知 | ✅ M2 |
| §4.4 Condition analysis | 碰撞风险分析，COLREGs 核心 | ✅ M1+M6+M2 |
| §4.5 Deviation from planned route | 避碰规划、偏航处理 | ✅ M4+M3 |
| §4.6 Contingency plans | 应急预案触发 | ✅ M7+M1 |
| §4.7 Safe speed | 安全船速（Rule 6） | ✅ M6+M5 |
| §4.8 Manoeuvring | 机动指令输出 | ⚠️ M5 出指令，执行在 L4 |
| §4.9 Docking | 靠泊运动控制（舵角/转速/侧推） | ❌ 应为 L4/L5 |
| §4.10 Alert management | 报警管理 | ✅ M8+M7（L4/L5 亦产生报警） |

**调研置信度**：🟢 High（DNVGL-CG-0264 原文 PDF 已入库，A 级来源）

---

### 顺带发现的文档问题

#### 问题 3：架构报告声称"L3 TDL 8 模块对 DNV 9 个子功能实现 100% 覆盖"，但 §4.2 和 §4.9 应属其他层

**问题描述**

架构报告 §2.3 第 116 行 + 第 14 章（§14.2，第 975–989 行）的覆盖矩阵将 9 个功能全部映射到 L3 TDL 模块。但 DNV 原文明确：
- §4.2 Planning prior to voyage = 航次规划层职责（本项目 L2 已有 Voyage Planner 承担）
- §4.9 Docking = 靠泊运动控制职责（本项目 L4/L5 承担）

将这两项强行映射到 L3 的 M3 和 M4/M5，与 MASS 整体分层架构形成**职责边界冲突**。

**受影响行**：报告第 116 行（决策理由）、第 979–989 行（覆盖矩阵）

**修改建议**

有两条可选路径，需要用户决策：

**路径 A（覆盖矩阵调整）**：将 §4.2 标注为"L2 主责，L3 M3 负责接收重规划触发和 ETA 跟踪"；将 §4.9 标注为"L4/L5 主责，L3 M4/M5 负责生成靠泊行为计划和轨迹指令"。覆盖矩阵改为"L3 直接负责 §4.3–§4.7（5个），参与协作 §4.2/§4.8/§4.9/§4.10（4个）"，不再声称"100% 独立覆盖"。

**路径 B（重新定义覆盖语义）**：在覆盖矩阵前增加说明：DNV-CG-0264 的 9 个功能是整船系统功能，L3 是其中负责战术决策的主责层；覆盖矩阵表达的是"L3 在各功能中的职责贡献"，而非"L3 独立实现全部功能"。CCS 认证时，需联合 L2/L4/L5 的设计文档共同形成完整证据包。

*最后更新：2026-04-30*
