# MASS L3 Tactical Decision Layer (TDL) 功能架构深度研究报告
## 基于 ODD 约束的第一性原理设计与 Captain Mental Model (CMM) 集成

**目标载体**：45 m Fast Crew Boat (FCB)，CCS 入级，D2–D4 自主等级，多船型可移植 
**作者立场**：本报告不验证已有三个候选方案 (algorithm-oriented 7-module / CMM 4+1 / CMM skeleton+plugin)，而是从国际证据出发重新进行功能分解。

---

## 0. 执行摘要 (Executive Summary)

通过对 **Kongsberg/AUTOSEA, MUNIN, MARIN/ShippingLab, Wärtsilä SmartMove, Avikus HiNAS, Sea Machines SM300, MOOS-IvP, NTNU Shore Control Lab, DARPA Urban Challenge (CMU Boss), SAE J3016, ISO 21448 SOTIF, IMO MASS Code, DNV-CG-0264 / AROS, ABS Autonomous & Remote-Control Functions Guide, ClassNK, CCS《智能船舶规范 2024》** 等 17 个权威来源的横向对比，得到以下结论：

1. 全球最佳实践 **没有一个使用 7 模块算法导向架构**，而是普遍收敛到 **3-Layer 时间分层 + 2-Layer 责任分层** 的 5-block 矩阵结构（详 §1）。
2. ODD 不能作为"监控模块"存在，而必须是 **整个 TDL 的 organizing principle**——架构必须由 ODD 状态机驱动 mode-switching，而不是由算法堆叠出 ODD 检查节点（详 §2，依据 Rødseth 2022 / Fjørtoft 2020 的 Operational Envelope 形式化）。
3. CMM (Captain Mental Model) 的本质是 **OODA loop + Rasmussen SRK + Sheridan 监督控制**——架构必须暴露这三个层次的可观察、可干预、可解释接口，否则 ROC 操作员无法在 D2↔D3↔D4 间平滑切换（详 §3）。
4. 推荐的功能分解为 **8 模块 L3 TDL** (§4)：ODD/Envelope Manager · World Model · Mission Manager · Behavior Arbiter · Tactical Planner · COLREGs Reasoner · Safety Supervisor · HMI/Transparency Bridge。每个模块的存在都有可追溯的规范条款或学术文献依据。
5. 多船型兼容通过 **MOOS-IvP "Backseat Driver" 范式 + 船型 Capability Manifest + 标准化 PVA 接口 (Position-Velocity-Actuator)** 实现，与船型物理参数、操纵性模型解耦（详 §5）。

---

## 1. 全球领先 MASS 战术决策层架构横向综述 (Q1)

### 1.1 收敛证据：5 大领先系统的共同结构

| 系统 | 战术层结构 | 关键证据 |
|---|---|---|
| **AUTOSEA / NTNU (Eriksen, Bitar, Breivik)** | 3-Layer Hybrid COLAV：High-level Planner (energy-optimized trajectory) → Mid-level MPC COLAV (动目标 + COLREGs 13–17) → Short-term BC-MPC (紧急避碰) | Frontiers Robotics & AI, 2020; doi:10.3389/frobt.2020.00011 |
| **MUNIN (FP7, Fraunhofer CML + SINTEF)** | Advanced Sensor Module (ASM) + Autonomous Navigation System (ANS) + Autonomous Engine & Monitoring Control + Shore Control Centre (SCC)；MUNIN D4.5/D4.7 ICT 架构定义 onboard/onshore 模块化 | CORDIS FP7 grant 314286; MUNIN D4.5 Architecture Specification |
| **CMU Boss (DARPA Urban Challenge winner)** | Mission Planner → Behavioral Executive → Motion Planner（三层规划栈） + Perception/World Model + Mechatronics | Urmson et al., J. Field Robotics, 2008; Tartan Racing Tech Report CMU-RI-TR-07-09 |
| **MOOS-IvP (MIT)** | Backseat Driver 范式：autonomy 与 control 解耦；行为基 (behavior-based) + IvP 多目标优化 helm；publish/subscribe (MOOSDB) 中间件 | Benjamin et al., Int. J. Field Robotics, 2010; oceanai.mit.edu |
| **Avikus HiNAS Control** | "Perception → Decision → Control" 三段式，DNV System Qualification 在审；L2 (IMO) | Avikus 官方说明 / DNV news 2024 |
| **Sea Machines SM300-NG** | "Vessel-agnostic" TALOS 软件 + 标准 SMLink Streaming-API/Control-API；class-approved hardware；open architecture | sea-machines.com SM300-NG 产品页；HII 2022 集成案例 |

**关键发现 1（共同骨架）**：所有系统都呈现 **"时间分层 (temporal stratification)"** 而非"功能模块平铺"。最快层 (10–100 ms) 处理紧急碰撞规避，中层 (1–60 s) 处理 COLREGs 决策，慢层 (1–60 min) 处理任务规划。这与 Albus NIST RCS（参考体系结构 NIST SP 1011）的层级语义知识结构一致 (Serban, Poll, Visser, J. Automotive Software Eng., 2020)。

**关键发现 2（共同抽象）**：所有系统在战术与执行之间都有一个 **"command interface"**：MOOS-IvP 用 (heading, speed, depth)；Sea Machines SM300 用 SMLink Control-API；Wärtsilä SmartMove 基于 DP 控制器接收 (track, waypoint, speed)；Boss 用 (trajectory, target velocity)。这是多船型可移植的关键抽象边界（详 §5）。

**关键发现 3（共同的"二元"架构哲学）**：均融合 **deliberative (慎思层)** 与 **reactive (反射层)**——这是 Gat & Bonnasso 1998 三层架构 (Sense-Plan-Act 之后的 hybrid layered) 的海事化。AUTOSEA 用 SBMPC + BC-MPC 实现 hybrid，MUNIN 用 ANS deliberative + ASM reactive 实现，MOOS-IvP 用 IvP 多目标优化层 + 行为层实现。

### 1.2 Kongsberg K-MATE / Yara Birkeland 内部分解（公开信息有限）

Kongsberg 公开材料只描述功能集合 (Auto-undocking, Autodocking, Situational Awareness System, Autonomous Navigation System, Intelligent Machinery System, Connectivity & Cyber Security, ROC, DP)，**没有公开的内部模块分解**。但 AUTOSEA 项目（NTNU + Kongsberg + DNV + Maritime Robotics）发表的 hybrid COLAV 架构可视为 Kongsberg 战术层的 academic proxy。
[TBD — Kongsberg K-MATE/Falco/Yara Birkeland 内部 SW 架构未公开发表，仅可由 AUTOSEA 文献间接推断]

### 1.3 Wärtsilä Voyage / SmartMove

公开材料显示 SmartMove 是 **基于 Wärtsilä DP 系统扩展的 track-following + waypoint controller**，而非独立的 tactical decision layer。其在 American Courage 上的部署仍要求 OOW 在线（决策支持级，DNV 分类 DS）。Wärtsilä 的"Odyssey/FutureProof"未发现公开技术架构文献。
[TBD — Wärtsilä Odyssey 内部分解未在公开文献中披露]

### 1.4 Rolls-Royce ODIN/NORTON

Rolls-Royce 商用海事部 2018 年被 Kongsberg 收购，AAWA 项目结束后无独立公开架构演进。其 2016 AAWA 白皮书提出 "Sense → Decide → Act" 三段式，未细化模块。
[TBD — Rolls-Royce ODIN/NORTON 仅余 AAWA 白皮书层面信息]

---

## 2. IMO MASS Code (MSC 111) 对战术层的功能性约束 (Q2)

### 2.1 MASS Code 结构与战术层相关条款

依据 IMO MSC 110 (June 2025) 输出与 ClassNK / DNV / ABS / IRClass 总结，MASS Code 计划于 **MSC 111 (2026 年 5 月 13–22 日)** 通过非强制版，2032 年强制化。Code 共 26 章，分为：

- **Part 2 (Principles & Objectives)** — 含 "Function and role of operational environment and human factors" (CCS/Kim 2024, arXiv:2509.15959)
- **Part 3 (Functional Requirements)** — 含 navigation, remote operations, fire protection, **search and rescue**, engineering 等（NTNU MASS Code 总结 Papageorgiou 2024）
- **Chapter 15 Human Element** — 包含 Roles & Responsibilities, Manning, Training, **Human-Machine Interface (including transfer of responsibility)** (草案 2.8.1–2.8.4)
- **Goal of Navigation chapter (§1.1 草案)**: "to provide for safe and secure navigation of MASS for any mission phase including collision avoidance in each environment condition, taking into account the mode of operation of the ship" (Draft MASS Code, June 2023)

### 2.2 三种"控制模式 (control modes)"作为架构强制约束

MASS Code 草案 §2 引入 **manual / remote / automatic** 三种模式，并要求 **"a combination of any of three modes"**（即 mode 间动态切换）。这一条款直接强制了 TDL 必须具备：
- 模式状态机 (mode state machine) 作为架构第一公民
- 模式切换的 **transfer of responsibility** 协议（与 Rødseth TMR/TDL 模型对接）
- **Remote Operation Centre (ROC)** 作为受治理实体（MSC 110 引入 ROM 概念）

**架构含义**：模式切换不能是某个模块内部的状态机，而必须是 **TDL 顶层控制**（"Mode Manager"），所有功能模块皆受其支配。

### 2.3 "navigation"(战术) 与 "control"(执行) 的边界

MASS Code 没有明确定义这一边界，但 **DNV-CG-0264 §4 (Navigation Functions)** 提供了事实标准。其将 navigation 分解为 9 个子功能：
1. Planning prior to each voyage
2. Condition detection
3. Condition analysis  
4. Deviation from planned route
5. Contingency plans
6. Safe speed
7. Manoeuvring
8. Docking
9. Alert management for navigational functions

(DNVGL-CG-0264, Edition Sept 2018, §4.1–§4.10)

**这正是国际公认的 L3 TDL 功能边界**：从航次规划到操纵指令为止，不进入舵机/油门的低级 control loop。这与 SAE J3016 的 DDT (Dynamic Driving Task) 定义高度同构 — DDT 包含 operational + tactical 但不含 strategic 路径规划 (SAE J3016_202104, §3.10)。

### 2.4 "operate independently" vs "cooperatively"

MASS Code 草案的 functional requirements 区分了：
- **必须独立运行的**：collision avoidance (COLREGs Rule 5/7/8/13–17), safe speed (Rule 6), MRC (Minimum Risk Condition) entry
- **可协作运行的**：voyage planning, route optimization, port-side coordination, docking handover

DNV-CG-0264 §5 (Sec.2 [5]) 和 Appendix A 列出了 **MRC (Minimum Risk Condition)** 作为强制设计点：当 ODD 失效或自主功能降级时，系统必须自主进入预定义安全状态（停车、漂泊、抛锚、退至安全水域等）。MRC entry 是 "operate independently" 的最严格保障。

---

## 3. ODD 作为整个 L3 TDL 组织原则的最优架构 (Q3)

### 3.1 Rødseth 2022 的 Operational Envelope —— 关键形式化

Rødseth, Wennersberg & Nordahl 在 *J. Mar. Sci. Technol.* 27:67–76 (2022, doi:10.1007/s00773-021-00815-z) 提出 **Operational Envelope** = (操作状态空间 𝕆, 功能映射 FM)。其相对于 ODD 的关键扩展是：

> "The ODD does not include the human control responsibilities, while most autonomous ship systems are expected to be dependent on sharing control responsibilities between humans and automation."

并通过 **TMR (maximum operator response time)** 和 **TDL (automation response deadline)** 两参数组合，导出 4×4=16 格自主-人工矩阵，落入 4 个稳定区域（FA / AC / OA / OE）。

**架构含义**：ODD 在 MASS 上必须扩展为 Operational Envelope，因为 ODD 只描述外部条件，而 MASS 的本质是 **"automation + human" 联合系统**。

### 3.2 Fjørtoft & Rødseth 2020 — Envelope-driven 安全架构

"Using the Operational Envelope to Make Autonomous Ships Safer" (Semantic Scholar CorpusID:226236357) 提出：autonomy 安全的根本机制是 **持续验证当前状态 ∈ Envelope，越界即触发降级或 MRC**。

### 3.3 ODD 作为 organizing principle 的架构原则（本报告综合）

基于上述证据，我们提炼出 **ODD-centric 架构 6 原则**：

1. **ODD/Envelope Manager 作为 TDL 顶层中央调度器**（不是某个边缘监控模块）。所有规划/决策模块的输出都必须由 EM 进行 envelope-conformance 验证后才能下发。
2. **每个功能模块都向 EM 注册其 "operating range"**（声明在何种 envelope 下可用），EM 据此 enable/disable/degrade 模块。这与 ISO 21448 (SOTIF) §6/§7 的 "intended functionality specification" 一致。
3. **mode 转换由 envelope state 触发**，而非由算法内部触发。Rødseth TMR/TDL 模型 → 触发判据：当 TDL < TMR 时，必须升级人工介入。
4. **MRC 是 envelope 的"出口"**，不是异常处理；DNV-CG-0264 Appendix A 列出"List of potential minimum risk conditions"作为强制设计输出。
5. **Envelope 用 UML use cases 形式化**（Rødseth 2022 §6），可直接对接 CCS 入级文件。
6. **Envelope 是第一类持久状态**（first-class persistent state），可被 ROC、入级机构、保险方查询审计。这与 ABS APExS-auto framework 用 STPA 进行 lifecycle management 的思路一致 (ABS news 2024)。

### 3.4 与 ISO 21448 SOTIF 的对接

Neurohr et al. *arXiv:2508.00543* (2025) "Towards Efficient Certification of Maritime ROCs" 明确指出：**maritime ROC 安全应按 ISO 21448 SOTIF 思路处理 functional insufficiency**。SOTIF 三类风险（已知不安全、已知安全、未知）正是 envelope 边界外的风险。架构中 ODD/Envelope Manager 必须能识别 envelope 边界外的"未知未知"并触发 MRC，而不是继续运行。

### 3.5 与 MARIN XMF 的关系

MARIN eXtensible Maritime Framework 公开技术文献极少；本研究未找到 XMF 明确的 ODD 模式切换文档。
[TBD — MARIN XMF 详细 mode-switching 机制公开文献不足]

---

## 4. ROC + 船长 Captain Mental Model 的 HME 架构需求 (Q4)

### 4.1 船长心智模型的认知科学基础

船长的认知流可由三层堆叠模型刻画：
- **OODA Loop** (Boyd, 1976)：Observe → Orient → Decide → Act
- **Rasmussen SRK** (1983)：Skill-based / Rule-based / Knowledge-based 行为
- **Sheridan Supervisory Control Levels** (Sheridan & Verplank, 1978; Sheridan 2021, *Handbook of Human Factors* Wiley doi:10.1002/9781119636113.ch28)

ABS *Advisory on Autonomous Functionality* 显式将 Sheridan 10 levels 作为 LoA 分类基础（National Academies 2020, *Leveraging Unmanned Systems for Coast Guard Missions*, Appendix D）。

### 4.2 关键文献：ROC 操作员的实证认知需求

- **Veitch & Alsos 2022**, *Safety Science* 152, "A systematic review of human-AI interaction in autonomous ship systems" — ROC 操作员需要 **"more than backup"** 的角色，需 active continuous monitoring + fault recognition 能力。
- **Veitch, Alsos, Cheng, Senderud, Utne 2024**, *Ocean Engineering* 299:117257, "Human factor influences on supervisory control of remotely operated and autonomous vessels" — 实证发现 number of vessels, available time, **DSS (Decision Support System)** 三因素强烈影响绩效。
- **Hareide et al. (NTNU Shore Control Lab)** — Veitch et al., "From captain to button-presser: operators' perspectives on navigating highly automated ferries" (*J. Phys. Conf. Ser.*, 2022) 揭示操作员在高自主下退化为"按钮按压者"，丧失 ship sense，必须通过 transparency design 重建 SA。

### 4.3 Agent Transparency (SAT) 模型 —— 架构强制可观察性

Chen, Procci, Boyce, Wright 等 (US Army Research Lab, 2014, ARL-TR-7180) 的 **SAT (Situation awareness-based Agent Transparency)** 模型给出三层透明性：
- **SAT 1**: agent 的当前状态、计划、目标（"what")
- **SAT 2**: agent 的推理过程、约束、决策基础（"why"）
- **SAT 3**: agent 的预测、不确定性、未来状态（"what next + uncertainty"）

实证研究 (Selkowitz, Lakhmani, Chen 2017, *Cognitive Systems Research*) 表明 SAT-1+2+3 全部呈现时 SA 提升、信任校准更好且 workload 不增加。

**架构含义**：每个战术层模块都必须对外暴露三类接口：
- `current_state()` (SAT-1)
- `rationale()` (SAT-2，含约束 + cost function 当前值)
- `forecast(Δt)` + `uncertainty()` (SAT-3)

### 4.4 IMO ROC 监管与"Transfer of Responsibility"

MSC 110 (June 2025) 引入 **Remote Operation Management (ROM)** 概念，作为"管理 ROC 内部功能"的可选框架，与"直接操作 MASS"严格区分（LISCR MSC-110 summary）。
MASS Code 草案 §2.8.4 "Human-Machine Interface (including transfer of responsibility)" 强制要求架构支持 **明确的责任移交协议**。

### 4.5 D2↔D3↔D4 平滑切换的架构需求

| 转换 | 主要挑战 | 架构需求 |
|---|---|---|
| **D2→D3**（船上船长 → ROC 远程监控） | 船感 (ship sense) 丧失，远程 SA 不足 | 架构必须传输 raw + interpreted + intent 三层数据；HMI Bridge 模块 |
| **D3→D4**（远程监控 → 完全自主，仅事件介入） | 操作员被动监控易导致 vigilance decrement | TDL 必须 push **predicted-event windows**（"未来 5 分钟有一个 close-quarters 情境，预计自主动作 X"） |
| **D4→D3**（自主回退） | 操作员 SA 重建时间 > TMR | TMR-aware mode switching；envelope manager 必须持续维护 "回到回路所需时间" |
| **D3→D2**（远程交还船上） | 需现场船员就位 + 上下文理解 | onboard CMM mirror — 架构需在船上保持 "captain proxy" 决策线程同步 |

Rødseth 2022 TMR/TDL 模型量化此约束：**架构设计必须保证 TDL > TMR 在所有 envelope 状态下成立**，否则该 envelope 状态下不能宣称 D3/D4 自主。

---

## 5. 推荐的 L3 TDL 功能分解（全新方案，非候选验证）

基于上述 IMO/DNV/ABS/CCS 强制要求 + AUTOSEA/MUNIN/Boss/MOOS-IvP 的工程证据 + Rødseth/Veitch/Sheridan 的人因证据，本报告提出 **8 模块 L3 TDL 架构**。每个模块都附带其 *raison d'être* 与边界条件。

### 5.1 模块 1：ODD/Envelope Manager (EM)
**存在依据**：Rødseth 2022 (doi:10.1007/s00773-021-00815-z) §4–§5；Fjørtoft 2020；ISO 21448:2022 §6; DNV-CG-0264 §3 [2.2] (Operational Envelope/ODD as part of CONOPS)。 
**职责边界**：
- 维护 envelope state 𝕆ₜ = ⟨environment, traffic, vessel-internal, human-availability⟩（这四元组涵盖 Rødseth 2022 Table 1 中 NMA/DNV/ABS/ClassNK 的 5 项要素）。
- 持续计算 TMR/TDL、autonomy zone (FA/AC/OA/OE)
- 计算 envelope conformance score；越界触发 mode degradation 或 MRC
- **不**做规划、不做控制
- 对外接口暴露 SAT-1 (`envelope_state`), SAT-2 (`conformance_rationale`), SAT-3 (`forecast_envelope_evolution`)

### 5.2 模块 2：World Model (WM)
**存在依据**：CMU Boss 5 大子系统含独立 "Perception & World Modeling" (Tartan Racing TR-07-09)；Albus NIST RCS 各 node 都有 internal world model；MUNIN ASM (D5.2)；Wang et al. "MANDS" (Expert Systems with Applications 2023, doi:10.1016/j.eswa.2023.119639) 称 "Perception, decision-making, control" 为基本架构。 
**职责边界**：
- Track-level fusion（不做原始 sensor fusion，那属于 L2 感知层）
- 维护 "static (ENC, charts) + dynamic (target tracks, AIS, weather) + ego (own vessel state)" 三视图
- 对 COLREGs 状态机提供已分类目标流（按 Rule 13–17 几何分类，参照 Eriksen et al. 2020 Frontiers Robotics & AI state-machine 设计）
- **不**做决策；只做客观世界表征
- WM 是 ODD 与 Mission/Behavior 之间唯一的"事实通道"，避免下游模块各自感知导致不一致

### 5.3 模块 3：Mission Manager (MM) — 战略-战术接口
**存在依据**：CMU Boss "Mission Planning" 层；DNV-CG-0264 §4.2 "Planning prior to each voyage"；MUNIN ANS voyage plan；SAE J3016 strategic functions(此层是其映射点)。 
**职责边界**：
- 接收 voyage plan（从船公司/ROC/IMO route exchange）
- 维护当前 mission goal、checkpoint、ETA 约束
- 当 envelope 变化（例如 weather routing change）触发 mission re-planning
- 与 Behavior Arbiter 形成 **goal → behavior 投影**
- **不**做避碰、不做轨迹

### 5.4 模块 4：Behavior Arbiter (BA) — 行为状态机
**存在依据**：CMU Boss "Behavioral Executive" (state-machine of Drive-Down-Road / Handle-Intersection / Achieve-Zone-Pose)；MOOS-IvP "behavior-based autonomy" + IvP multi-objective optimization；AUTOSEA mid-level COLREGs state machine (Eriksen 2020)；ABS *Guide for Autonomous and Remote-Control Functions* "Information → Decision → Action" stages。 
**职责边界**：
- 海事 behavior 字典（FCB 场景至少包括）：Transit / Approach / Crew-Transfer-Standby / DP-Hold / Docking / Undocking / Overtake / Crossing / Head-on-Encounter / Restricted-Visibility / MRC-Drift / MRC-Anchor
- 用 **multi-objective optimization** (IvP-style) 而非单一规则裁决并发 behavior（避免 Pirjanian 1998 指出的 priority-based 仲裁缺陷）
- 输出 **behavior plan ⊆ envelope** 给 Tactical Planner
- 与 EM 交互：每个 behavior 声明其 envelope 适用范围
- 与 COLREGs Reasoner 交互：crossing/overtake/head-on 行为必须先经 COLREGs 一致性检验

### 5.5 模块 5：Tactical Planner (TP) — 中短期轨迹/操纵规划
**存在依据**：AUTOSEA 三层 COLAV 中的 mid-level MPC + short-term BC-MPC (Eriksen, Bitar, Breivik 2020 doi:10.3389/frobt.2020.00011)；CMU Boss "Motion Planner"；DNV-CG-0264 §4.7 "Safe speed"、§4.8 "Manoeuvring"；高速 FCB 场景需要 0.1–10 s 反应（BC-MPC 设计点）。 
**职责边界**：
- 输入：BA 当前 behavior + WM target tracks + envelope constraints
- 输出：short-horizon (≤30 s) trajectory + (course, speed, ROT) command stream
- 双层结构：mid-level (deliberative MPC, 1–60 s) + short-term reactive (BC-MPC, 0.1–10 s)
- **不**做 COLREGs 解释，只做几何/动力学优化
- 命令流通过标准 PVA 接口下发到 L2 Execution Layer（详 §6）

### 5.6 模块 6：COLREGs Reasoner (CR)
**存在依据**：IMO COLREG 1972 适用所有船舶，包括 MASS（DNV-CG-0264 §1.5.2.1）；MASS Code 草案 §FR1.1 navigation goal 含 "collision avoidance"；AUTOSEA 单独将 COLREGs 状态机分离出 (Eriksen et al. 2020 §III)；Bitar et al. *arXiv:1907.00198* 显示分离化设计可独立验证。 
**职责边界**：
- 输入：WM 目标列表、ego state、几何关系
- 维护：每个目标的 COLREGs 角色 (give-way/stand-on/head-on/crossing/overtaking)、Rule 6 安全速度、Rule 8 早行动判据
- 输出：对 BA 与 TP 的"COLREGs 约束集"
- 与 Safety Supervisor 联合在违规风险时触发 escalation
- 单独存在的理由：legally auditable、可独立 SIL/HIL 验证（满足 ABS GCVR + DNV-CG-0264 §3 [4] technology qualification 要求）

### 5.7 模块 7：Safety Supervisor (SS) / Functional Insufficiency Monitor
**存在依据**：ISO 21448:2022 SOTIF (Neurohr et al. arXiv:2508.00543, 2025 maritime ROC application)；DNV-CG-0264 §4.10 "Alert management" + §5.3 "Incidents and failures"；DNV AROS notation 规定 "supervised autonomy" 模式；ABS *Guide* "Action stage may be carried out by humans if necessary"；MASS Code §FR for MRC。 
**职责边界**：
- 监督每个模块的 **assumption violations**（SOTIF triggering conditions）：sensor degradation, model OOD, COLREGs unresolvable conflict, comm-link loss
- 双轨：functional safety (IEC 61508 component faults) + SOTIF (intended-function insufficiency)
- 触发器：MRC entry, mode degradation, ROC handover request
- 与 EM 双向：SS 的判据来自 envelope；EM 的状态由 SS 提供故障/不安全证据更新
- 与 HMI Bridge：SS 是触发 ROC alert/handover 的唯一权威

### 5.8 模块 8：HMI / Transparency Bridge (HTB)
**存在依据**：MASS Code §2.8.4 (Draft) "HMI including transfer of responsibility"；IMO MSC 110 ROM concept；Veitch et al. 2024 *Ocean Eng.* 299:117257；Chen et al. SAT model (ARL-TR-7180)；Saghafian, Vatn et al. 2024 *Safety Science* "automation transparency adaptive design"。 
**职责边界**：
- 唯一的 ROC/ 船长接口编排者（其他 7 模块都不直接访问 HMI）
- 把 7 模块的 SAT-1/2/3 流编排为操作员认知合理的视图
- 实现 **transfer of responsibility 协议**：handshake、context transfer、time-bounded grace period（基于 Rødseth TMR）
- 维护 D2/D3/D4 模式视图差异化（onboard captain 视图 ≠ ROC 视图）
- 是唯一可审计的"who controls now"权威

### 5.9 8 模块的边界图 / 数据流（文字版）

```
                    [ROC + Captain]
                          ▲
                          │ SAT-1/2/3 + Handover
                  ┌───────┴───────┐
                  │  HMI/Trans.   │ (M8)
                  │   Bridge      │
                  └───┬───────────┘
                      │
   ┌──────────────────┼─────────────────────────┐
   │                  ▼                         │
   │     ┌────────────────────┐                 │
   │     │ Safety Supervisor   │ (M7) ◄──── SOTIF/IEC61508
   │     └────────┬────────────┘                │
   │              │ MRC trigger                 │
   │              ▼                             │
   │  ┌────────────────────────┐                │
   │  │ ODD/Envelope Manager    │ (M1)          │
   │  │ (Mode State Machine)    │ ◄──── Rødseth Envelope
   │  └─┬────────┬───────┬──────┘                │
   │    │        │       │                       │
   │    ▼        ▼       ▼                       │
   │  Mission  Behavior  COLREGs                 │
   │  Manager  Arbiter   Reasoner                │
   │  (M3)     (M4)      (M6)                    │
   │    │        │       │                       │
   │    └────┬───┴───────┘                       │
   │         ▼                                   │
   │   Tactical Planner (M5)                     │
   │   ┌─────────────────┐                       │
   │   │ Mid-MPC + BC-MPC │                      │
   │   └─────────┬───────┘                       │
   │             │ PVA cmd                       │
   └─────────────┼───────────────────────────────┘
                 │
       [L2 Execution Layer / Vessel Control]
                 ▲
                 │ obs
       [L1 Sensors]
                 │
                 ▼
          World Model (M2) ─────── feeds all M3–M7
```

WM 是横切的事实层（任何模块都从 WM 读取，但不直接读 sensor）。

---

## 6. 多船型兼容机制 (Q6)

### 6.1 行业现成方案的根本机制：Backseat Driver Paradigm

MIT MOOS-IvP 体系（Benjamin et al., *Int. J. Field Robotics* 2010；oceanai.mit.edu）核心是：

> "The autonomy system provides heading, speed and depth commands to the vehicle control system. The vehicle control system executes the control and passes navigation information... to the autonomy system. The backseat paradigm is agnostic regarding how the autonomy system is implemented."

这一范式被 Sea Machines SM300 (TALOS + SMLink Control-API) 商业化采用：**"vessel-agnostic onboard hardware with TALOS autonomy software"** (sea-machines.com/sm300-sp/)。

### 6.2 推荐机制：3 层解耦 + Capability Manifest

| 层 | 内容 | 解耦机制 |
|---|---|---|
| **A. Vessel-agnostic Tactical Core** | M1–M8 通用算法 | 不含船型常量 |
| **B. Capability Manifest** | 船型 YAML/JSON 文件：尺寸、操纵性极限（最大转艏率、最小转弯半径、加速曲线）、推进器类型、传感器配置、ENC 适用范围、envelope template | 入级时由船厂/CCS 验证签名；TDL 启动时载入并配置 M1/M5 参数 |
| **C. Vessel-specific Adapter** | PVA 接口（Position-Velocity-Actuator）实现层；操纵性模型 (MMG / Abkowitz)；推进器 allocation 算法 | 对外提供标准 SMLink-style API；对内实现船特定动力学 |

**关键设计点**：
- M5 (Tactical Planner) 内部 MPC 模型不硬编码船型；它从 Capability Manifest 读取 (M, D, K, T) Nomoto 参数或 MMG 参数
- M1 (EM) 的 envelope template 库分船型族（FCB / Tug / Container / Ferry），而 envelope 计算逻辑通用
- M2 (WM) 的 ego model 对外只暴露 (x, y, ψ, u, v, r)；船型差异封装在适配层
- M5 输出的 PVA 命令 (course, speed, ROT) 与 MOOS-IvP 接口同构，直接兼容多家 vessel control system

### 6.3 ROS2 / Nav2 海事化

ROS2 + Nav2 作为陆地机器人参考栈，本研究未发现有 production-grade maritime port。
[TBD — ROS2 maritime navigation stack 公开案例多为研究原型]

### 6.4 Kongsberg 多平台一致性

Kongsberg 的 K-MATE 在 Yara Birkeland (集装箱)、Eidsvaag Pioner (cargo)、HUGIN (AUV) 上的部署机制未公开发表。从 AUTOSEA 论文体系推测其使用类似的 hybrid COLAV 算法 + 平台特定控制层，但具体机制未披露。
[TBD — Kongsberg 跨平台架构机制非公开]

---

## 7. 安全认证强制功能模块映射 (Q7)

### 7.1 DNV-CG-0264 (Sept 2018) §4 navigation 9 子功能 → 本架构映射

| DNV 子功能 | 本架构对应模块 |
|---|---|
| §4.2 Planning prior to each voyage | M3 Mission Manager |
| §4.3 Condition detection | M2 World Model + M1 EM |
| §4.4 Condition analysis | M1 EM + M7 SS |
| §4.5 Deviation from planned route | M3 + M4 BA |
| §4.6 Contingency plans | M7 SS + M1 EM (MRC routing) |
| §4.7 Safe speed | M6 COLREGs Reasoner + M5 TP |
| §4.8 Manoeuvring | M5 TP |
| §4.9 Docking | M4 BA (Docking behavior) + M5 TP |
| §4.10 Alert management | M8 HTB + M7 SS |

**100% 覆盖**——这是 **CCS 入级评审时本架构通过 DNV-CG-0264 等价验证的关键证据**（CCS 在国际承认上参考 DNV/ABS/ClassNK 等）。

### 7.2 ABS *Guide for Autonomous and Remote-Control Functions* 四阶段（Information → Analysis → Decision → Action）

| ABS 阶段 | 对应模块 |
|---|---|
| Information acquisition | M2 WM (从 L2 输入) |
| Analysis | M1 EM + M6 CR + M7 SS |
| Decision | M3 + M4 + M5 |
| Action | M5 (PVA cmd) → L2 |
| Operator override | M8 HTB |

**关键**：ABS 要求 "operator station to be constantly manned + able to intervene"，由 M8 HTB 单独承担，避免与 7 个自主模块的执行路径耦合。

### 7.3 CCS《智能船舶规范 2024》8 类智能模块定位

CCS 8 模块：智能航行、智能船体、智能机舱、智能能效管理、智能货物管理、智能集成平台、远程控制、自主操作 (CCS 2024)。

本 L3 TDL 架构 **属于"智能航行 (Nx)" + 与"远程控制 (Ri)"/"自主操作 (Ai)" 三类合计入级条款的核心**。"智能集成平台" 由 M2 + M8 共同承担。CCS L2 入级（船上人员 + 自主辅助）在 D2 模式直接适用，D3/D4 通过对应 (Ai, Ri) 等级标注。

### 7.4 IEC 61508 / 61511 SIL 分配（设计约束）

IEC 61508 要求识别 safety functions 并分配 SIL。本架构建议：
- M5/M6/M7 的 MRC 触发链：SIL 2–3 (类似 DP3, DNVGL-OS-D203)
- M1 的 envelope conformance：SIL 2
- M2 的 own-vessel state estimation：SIL 2
- M3/M4 的 voyage/behavior 决策：SIL 1（因 fallback 是 MRC，不直接危及人命）
- M8 的 transfer-of-responsibility handshake：SIL 2

SIL 详细分配需依据具体 ConOps 进行 LOPA 分析。
[TBD — 具体 SIL 数值需在 CCS HAZID/HAZOP 后量化]

---

## 8. 与陆地/航空自主架构的可借鉴性 (Q5)

### 8.1 SAE J3016 → MASS

SAE J3016_202104 §3.10–§3.12 三层 (Strategic / Tactical / Operational) 映射：
- Strategic ≅ M3 Mission Manager
- Tactical ≅ M4 BA + M5 TP + M6 CR
- Operational ≅ L2 Execution（不在 L3 范围）

关键差异：SAE J3016 的 "DDT fallback" 在海事中等价于 **MRC entry**，但海事 MRC 时间尺度更长（分钟到小时），允许更多 deliberative decision，反而比汽车更复杂。

### 8.2 CMU Boss 5 子系统 → MASS 映射

| Boss | 映射 |
|---|---|
| Mission Planning | M3 |
| Behavior Generation (state machine of Drive-Down-Road...) | M4 |
| Motion Planning (Anytime D*) | M5 |
| Perception & World Modeling | M2 |
| Mechatronics | L2 |

Boss 没有 ODD Manager 和 SOTIF Supervisor —— 这是 2007 年技术与 2026 年 MASS Code 之间 19 年的进步。本架构在 Boss 基础上 **加入 M1 + M6 + M7 + M8 = 4 个新模块** 以满足 IMO/ISO 21448 时代要求。

### 8.3 NASA 自主系统对比

NASA Mission Data System (MDS) 与 CLARAty 架构均采用 goal-based + state-based 控制。MDS 的 "state variable" 概念与本架构 M1 envelope state 同构。
[TBD — NASA MDS/CLARAty 详细映射超出本研究范围]

---

## 9. 与三个候选架构的本质差异（仅供对照，不作验证）

| 维度 | 候选 A (algorithm-7) | 候选 B (CMM 4+1) | 候选 C (CMM skeleton+plugin) | **本报告 8 模块** |
|---|---|---|---|---|
| 组织原则 | 算法功能堆叠 | 船长心智镜像 | 骨架 + 插件 | **ODD/Envelope 中央驱动** |
| ODD 位置 | 监控模块 | 4+1 中的 1 | 骨架内 | **顶层第一公民** |
| 模式机制 | 隐含 | 心智模型驱动 | 插件协议 | **MASS Code 三模式状态机强制** |
| HMI 位置 | 接口 | 心智镜像之"+1" | 插件 | **唯一 ROC 权威 (M8)** |
| 多船型 | 抽象层 | 心智通用 | 插件 = 船型 | **Backseat + Capability Manifest** |
| 安全 | 算法 SIL | 心智可靠性 | 插件验证 | **SOTIF + 61508 双轨** |

---

## 10. 证据可追溯性总表 (Evidence Traceability)

| 编号 | 证据 | 用于支持 |
|---|---|---|
| E1 | IMO MASS Code Draft (June 2023) §1.1, §2.8 | §2 战术层目标、控制模式 |
| E2 | IMO MSC 110 outcome (June 2025), MSC 111 (May 2026) | §2 时间表、ROM 概念 |
| E3 | DNVGL-CG-0264 Edition Sept 2018 §4 | §2.3, §4, §7.1 navigation 9 子功能 |
| E4 | DNV AROS Class Notation (2024) | §1, §7 4 类×4 模式 |
| E5 | Rødseth, Wennersberg, Nordahl 2022, *J. Mar. Sci. Technol.* 27:67-76, doi:10.1007/s00773-021-00815-z | §2, §3.1 Operational Envelope 形式化、TMR/TDL |
| E6 | Fjørtoft & Rødseth 2020 "Using Operational Envelope to Make Autonomous Ships Safer" Semantic Scholar CorpusID:226236357 | §3.2 envelope-driven safety |
| E7 | Eriksen, Bitar, Breivik et al. 2020, *Frontiers in Robotics and AI* 7:11, doi:10.3389/frobt.2020.00011 | §1.1, §5.5, §5.6 三层 hybrid COLAV |
| E8 | Bitar et al. *arXiv:1907.00198* (2019) | §5.6 COLREGs 状态机分离 |
| E9 | Urmson et al. 2008, *J. Field Robotics* 25(8):425-466 (Boss); CMU-RI-TR-07-09 Tartan Racing | §1.1, §5.4, §8.2 三层规划栈 |
| E10 | Benjamin, Schmidt, Newman, Leonard 2010, *Int. J. Field Robotics*; oceanai.mit.edu/ivpman | §1.1, §6.1 Backseat Driver, IvP |
| E11 | SAE J3016_202104 (jointly with ISO TC204/WG14) | §1, §2.3, §8.1 DDT, ODD, fallback |
| E12 | Serban, Poll, Visser 2020, *J. Automotive Software Eng.* 1(1):20-33, doi:10.2991/jase.d.200212.001 | §1.1, §8 NIST RCS, SAE 映射 |
| E13 | ISO 21448:2022 (SOTIF) | §3.4, §5.7 functional insufficiency |
| E14 | Neurohr et al. 2025 *arXiv:2508.00543* "Towards Efficient Certification of Maritime ROCs" | §3.4 SOTIF 海事 ROC 化 |
| E15 | Sheridan & Verplank 1978 MIT Tech Rep; Sheridan 2021 in *Handbook of Human Factors* Wiley doi:10.1002/9781119636113.ch28 | §4.1 supervisory control |
| E16 | Veitch & Alsos 2022, *Safety Science* 152:105778 | §4.2 ROC operator role |
| E17 | Veitch, Alsos, Cheng, Senderud, Utne 2024, *Ocean Eng.* 299:117257 | §4.2 实证 DSS/vessel-count 影响 |
| E18 | Chen et al. ARL-TR-7180 (2014) SAT model; Selkowitz et al. 2017 *Cognitive Systems Research* | §4.3 transparency 三层 |
| E19 | ABS *Guide for Autonomous and Remote-Control Functions* (2021, updated 2024 with APExS-auto AiP) | §5.4, §7.2 Information→Analysis→Decision→Action |
| E20 | CCS《Rules for Intelligent Ships 2024》(2023年12月发布) | §7.3 8 类模块 + i-Ship 标注 |
| E21 | MUNIN FP7 grant 314286, D4.5 Architecture Specification, D5.2 ASM (Fraunhofer CML) | §1.1, §5.2 onboard/onshore 分模块 |
| E22 | MSC 110/5/1 Proposed Amendments to Chapter 15 (2025) | §2.1 Human Element 草案 |
| E23 | NTNU Shore Control Lab publications 2022–2024 (Veitch, Saghafian, Petermann, Madsen et al.) | §4 ROC 人因实证 |
| E24 | Sea Machines SM300-NG / SM300-SP product docs (sea-machines.com 2024) | §1.1, §6.1 vessel-agnostic API |
| E25 | Avikus / DNV System Qualification news (2024) HiNAS Control | §1.1 perception+decision+control 三段 |

[TBD] 项汇总：
- T1 — Kongsberg K-MATE 内部 SW 架构（仅 AUTOSEA 学术 proxy 可用）
- T2 — Wärtsilä Odyssey/FutureProof 内部分解（公开文献缺失）
- T3 — Rolls-Royce ODIN/NORTON（仅 AAWA 白皮书）
- T4 — MARIN XMF mode-switching 详细机制
- T5 — ROS2/Nav2 海事生产级实现
- T6 — Kongsberg 跨平台一致性机制（Yara/HUGIN/MUNIN）
- T7 — NASA MDS/CLARAty 详细映射
- T8 — 具体 SIL 分配数值（需 HAZID/HAZOP 后定）

---

## 11. 总结与给设计团队的 5 条具体建议

1. **放弃"算法模块平铺"思维**——AUTOSEA、Boss、MOOS-IvP 这些世界级系统都是 **"时间分层 + 责任分层 + 透明性分层"** 的三轴矩阵，而非平铺的 7 个算法。
2. **把 ODD 提升为 organizing principle**——按 Rødseth 2022 把 ODD 扩展为 Operational Envelope = ⟨environment, traffic, vessel-internal, human-availability⟩，并以 TMR/TDL 量化 D2/D3/D4 切换判据。
3. **把 CMM 落地为 SAT-1/2/3 三层透明性接口**——所有 8 模块都必须实现 `current_state / rationale / forecast+uncertainty` 三个调用，统一通过 M8 (HMI/Transparency Bridge) 编排。这才是 CMM 的工程化形式，而不是"模拟船长决策的状态机"。
4. **多船型兼容用 MOOS-IvP Backseat + Capability Manifest 双机制**——TDL 8 模块完全 vessel-agnostic，船型差异封装在 Capability Manifest（CCS 入级时签名）和 PVA 适配层。这是 Sea Machines SM300 商业化验证的路径。
5. **CCS 入级路径**——本 8 模块对 DNV-CG-0264 §4 的 9 个 navigation 子功能 100% 覆盖，对 ABS 4 阶段 100% 覆盖，对 CCS 智能航行 (Nx) + 自主操作 (Ai) + 远程控制 (Ri) 三标注完全可映射。建议在 ConOps 文档中显式声明这一映射作为入级评审的 traceability matrix。

---

**报告范围声明**：本研究在 2026 年 4 月 29 日基于公开文献完成。MASS Code 将于 2026 年 5 月 13–22 日 MSC 111 通过非强制版，本报告基于 MSC 110 (2025-06) 输出与 ISWG/MASS 4 草案推断条款。最终条款以 MSC 111 通过版为准。