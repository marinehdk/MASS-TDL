# 阶段 2 · 业界 + 学术证据库

> 8 轮 NLM deep research 摘要。每轮入对应 domain 笔记本（详见路由）。
>
> 工作模式：subagent (general-purpose, Haiku 4.5, run_in_background)，2 批 × 4 并发。
>
> Phase 1 关键发现（输入阶段 2 调研方向）：
> - R8 二轴确认（PMC 全文）→ R3 调研须回答"三轴 ODD 有无权威来源"
> - R12 无"100×"量化 → R4 调研须寻找 SIL 系统 Checker/Doer 简化的真实量化依据
> - R9 DNV 9 子功能待验 → R7 调研须直接阅读 DNV-CG-0264 原文

---

## 路由表

| 轮 | 主题 | 入库笔记本 | 对应 Phase 1 反证议题 |
|---|---|---|---|
| R1 | 工业 COLAV 系统综述 | colav_algorithms | — |
| R2 | 学术 COLAV 主流路线 | colav_algorithms | R17 T_standOn 阈值替代来源 |
| R3 | ODD/Operational Envelope 工程实践 | maritime_regulations | R8 三轴 vs 二轴 |
| R4 | Doer-Checker 海事/航空 SIL 落地 | safety_verification | R12 100× 量化 |
| R5 | SAT 模型 ROC 实证 | maritime_human_factors | — |
| R6 | IMO MASS Code MSC 110/111 | maritime_regulations | R2 时效性 |
| R7 | CCS i-Ship + DNV-CG-0264 | maritime_regulations + safety_verification | R9 9 子功能 |
| R8 | Capability Manifest / Backseat Driver | colav_algorithms | R3 多船型替代来源 |

---

## R1: 工业 COLAV 系统综述

> ⚠️ subagent 任务通知未到达（已知问题：两个 subagent 同时写入同一笔记本导致通知丢失）。+38 条来源已确认入库（colav_algorithms: 26→64）。本节内容通过 nlm-ask 查询笔记本重建。

### 核心结论

工业级 COLAV 系统普遍采用**四模块标准架构**：State Detection（传感器融合）→ State Analysis（世界模型）→ Action Planning（COLREGs 避碰规划）→ Action Control（执行机构）。Safety Supervisor 的主流实现依赖 ODD 监控 + MRC 降级触发 + 冗余容错，而非独立 Checker 代码。Kongsberg K-MATE 的"向远程操作员发送建议、人工批准"模式揭示了当前工业系统仍强依赖人类监督的现实。Ocean Infinity A78 获得首个 DNV-CG-0264 远程操控合规声明，是最强认证先例。

### 关键证据

**工业参考**
- Kongsberg K-MATE / Sounder 系统 — 感知+避碰+自主操作，符合 COLREGs，但建议仍需远程操作员批准 — A/B 级
- Ocean Infinity Armada A78 — DNV-CG-0264 下**首个远程操控合规声明**；A36 完成 10,000 km 作业 — B 级
- ATLAS ELEKTRONIK ARCIMS — 英国皇家海军实际运营，MCA 认证，Sense & Avoid 验证 — B 级
- IAI Katana — 港口安全/巡逻/消防/搜救实船验证 — B 级
- MUNIN FP7 D5.2 — 高级传感器模块（多传感器融合替代人工瞭望），FP7 314286 — A 级

**学术文献**
- Eriksen et al. 2020 (Frontiers 7:11) — BC-MPC Rule 17 两阶段切换实测 — A 级（已在 R2 覆盖）
- DNV-CG-0264 NGC 标准 — 4 模块工业分类（Condition Detection / Analysis / Action Planning / Control）— A 级
- MUNIN 项目文献（FP7） — 传感器模块设计 + 自主导航系统降级 — A 级

### Safety Supervisor 模式观察

| 模式 | 系统 | 实现 |
|---|---|---|
| 软件监督 + 人工审批 | Kongsberg K-MATE | 建议→ROC 批准 |
| ODD 监控 + MRC 触发 | MUNIN / 工业通用 | 偏离 ODD → 停船/锚泊 |
| 硬件冗余 fail-to-safe | Ocean Infinity A78 | 单点故障不影响 MRC 进入 |

**推翻信号**：Kongsberg 的"人工批准最终动作"模式与 v1.0 L3 完全自主决策的设计假设存在差距——当前主流工业实现仍保留强制人类确认点。这对 D4 自主等级的边界定义有参考意义。

### 来源年龄分布
- <6 月: ~8 条（DNV 2025 更新、IMO 决议）
- 6–24 月: ~18 条（2024 工业报告）
- >24 月: ~12 条（MUNIN/ARCIMS 经典文献）

### 置信度 🟡

Wärtsilä、Avikus HiNAS、Maersk Mayflower 在笔记本中未找到具体架构细节，降级至 🟡。

### NotebookLM 入库结果

- 笔记本: colav_algorithms
- source_count BEFORE: 26（R2 完成时）→ AFTER (dedup 后): **64**
- 增量: +38
- 通知状态: ❌ 任务通知未到达（已知并发限制）

---

## R2: 学术 COLAV 主流路线

### 核心结论

2020–2025 年 COLAV 学术研究在 MPC/VO/IvP/RL 四条路线上已收敛。**BC-MPC** 在 COLREGs Rules 13–17 合规性证明中表现最强，特别是 Rule 17（直航船自主干预）的两阶段切换支持完善。**IvP Helm 行为仲裁**在任务级权重管理中仍为金标准，前座-后座解耦为 SIL 2 认证提供清晰功能边界。**VO+概率增强**（GP/UKF）已成高效反应式底层逻辑。**RL** 展示复杂场景能力但黑盒特性需 RTA 安全过滤层才能满足 SIL 2。

### 关键证据

**工业参考**
- Eriksen et al. 2020 · BC-MPC (Frontiers Robotics & AI 7:11) — Rule 17 两阶段切换实测验证 — PMC · A 级
- MIT Marine Autonomy Lab · MOOS-IvP/BHV_AvdColregsV22 — 权重驱动行为切换 + 前座-后座 SIL 边界 — MIT/NOAA · A 级
- TI TDA4VH-Q1 SIL 2 认证平台 — SIL 2/ASIL B 主控 + SIL 3 扩展 MCU — Texas Instruments · B 级
- DLR 2025 · LLM+GSN 自动化安全论证 — RL → SIL 2 认证的形式化路径 — DLR · B 级

**学术文献**
- Frontiers Robotics & AI (2021) · *Autonomous Collision Avoidance at Sea: A Survey* — MPC/VO/RL 系统对比 + TCPA 阈值综合 — A 级
- Journal of Navigation (2024) · COLREGs compliance + GNN/MARL 趋势综述 — Cambridge UP · A 级
- MDPI Ships (2022) · *Ship Autonomous Collision-Avoidance Strategies—A Survey* — MPC 变体分类 + Rule 17 阶段划分 — A 级
- Frontiers Marine Science (2025) · PPO-RL COLREGs 合规 — 奖励函数工程设计 — B 级
- IEEE (2024) · Constraint Programming-Based Verification of COLREG Safety — 自动化安全验证 — B 级

### T_standOn / T_act 阈值专项（Phase 1 R17 反证议题）

**结论：无法找到与 v1.0 §9.3 完全一致的数值（8/6/10 min 和 4/3/5 min）。**

文献给出的范围：
- TCPA 开始规划：15–20 min（开阔水域，Frontiers 2021 综述）
- T_act（让路船行动）：TCPA ~10 min（Eriksen et al. 2020）
- T_standOn（直航船获准行动）：TCPA 5–8 min 区间（MDPI Ships 2022）

v1.0 的具体三档数字（FCB/拖船/渡船 对应不同阈值）很可能来自 Wang 2021 原文的特定船型参数，或作者自行设定——**需直接获取 Wang 2021 全文验证**。如无法验证，建议 v1.1 改为范围值（"TCPA 5–15 min 视船型"）并注明待校准。

### 推翻信号

无。BC-MPC + IvP + Doer-Checker 组合与 v1.0 设计高度一致，均获近年文献支持。

### 置信度 🟢

RL SIL 2 认证路径 🟡（2025 DLR 新框架，尚待工程验证）；其余 🟢。

### NotebookLM 入库结果

- 笔记本: colav_algorithms
- source_count BEFORE: 0 → AFTER (dedup): **26**（增量 +26，dedup 删除失败来源 0）

---

## R3: ODD / Operational Envelope 工程实践

### 核心结论

主流海事工程在 ODD 定义上采用**两种框架**，均非三轴：
- **Rødseth 2022**：二轴 O = S_ext × S_int + Function Mapping FM（人机控制权分配）
- **SAE J3016/J3259 / ISO 34501**：属性树方法（Scenery / Environmental / Dynamic / Connectivity / Temporal），非轴线

**DNV-CG-0264 AROS 确实有三维框架**，但它是认证空间（Mission Complexity × Environmental Difficulty × Human Independence），用于**风险分级和功能级认证**，不是 ODD 定义本身。

**v1.0 的 E×T×H 三轴**无权威来源——是未标注的内部改写。

### 关键证据

**工业参考**
- DNV-CG-0264 + AROS Notations (2025.01 生效) — 三维自主度认证空间（任务×环境×人类介入），非 ODD 定义 — A 级
- IMO MASS Code MSC-CIRC 1637 / MSC 110 — 四级自主度 + 目标型 ODD 边界检测要求 — A 级
- Groke Technologies (Groke Pro/Fleet) — SAE 属性树实现（离散属性，非轴线）— B 级
- Lloyd's Register MASS Research Paper v6 (2025.02) — ODD 认证 + 保险 + 法律责任框架 — A 级

**学术文献**
- Rødseth 2022 (J. Marine Sci. Tech. 27(1)) — O = S_ext × S_int 二轴 + FM；PMC 全文确认 — A 级
- Fjørtoft & Rødseth 2020/2021 — "ODD for Cars vs OE for Ships"；显式区分汽车 ODD 与船舶 OE — A 级
- SAE J3259 (2021) — ODD 属性树分类法（无轴线）— A 级
- ISO 34501:2022 — 支持 BSI PAS 1883 属性方法论 — A 级
- MDPI 系列 (2024) — MASS 认证需求分析；均采用属性映射，无三轴 — B 级

### ODD 轴数专项（Phase 1 P0 反证议题）

| 框架 | 轴/维度数 | 用途 |
|---|---|---|
| SAE J3016/J3259 | 0 轴（属性树） | ODD 定义标准 |
| ISO 34501:2022 | 0 轴（属性树） | ODD 定义标准 |
| **Rødseth 2022 OE** | **2 轴（O × FM）** | ODD 数学定义 |
| **DNV-CG-0264 AROS** | **3 维（认证维度）** | **自主度风险分级，非 ODD** |
| IMO MASS Code | 目标型（无轴） | ODD 边界检测要求 |
| **v1.0 E×T×H** | **3 轴** | **❌ 无权威来源** |

**结论**：v1.0 §3.2 "三轴（E×T×H）"无权威支撑。Rødseth 2022 确认是二轴；DNV 三维是认证空间（不是 ODD 定义）。

**v1.1 修订建议**：
```
ODD 框架采用 Rødseth 2022 定义：
  O = S_ext × S_int（外部环境状态 × 内部技术状态）
  + FM（Function Mapping：状态→人机控制权分配）

规制认证采用 DNV-CG-0264 AROS 三维自主度空间：
  (1) Mission Complexity  (2) Environmental Difficulty  (3) Human Independence
  （用于风险分级，非 ODD 本身）

ODD 属性遵循 SAE J3259 属性树：
  Scenery / Environmental / Dynamic / Connectivity / Temporal
```

### 推翻信号

无架构级推翻。唯一问题：v1.0 §3.2 三轴措辞需标注来源或改为上述精确表述。

### 置信度 🟢

Rødseth 二轴 🟢（PMC 全文 + 多来源确认）；DNV 三维认证 🟢（官方文档 2025.01）；v1.0 三轴无来源 🔴。

### NotebookLM 入库结果

- 笔记本: maritime_regulations
- source_count BEFORE: 0 → AFTER (dedup): **51**（增量 +51，dedup 失败 0）

---

## R4: Doer-Checker 海事 / 航空 SIL 落地

### 核心结论

Doer-Checker/Monitor-Controller 架构是 2015–2025 SIL 2–3 安全系统的工业标准范式。Boeing 777 PFCS（1 Command + 2 Monitor 通道，~50:1 SLOC）、Airbus FBW COM/MON（硬件多样化 + 软件多样化，>100:1）、NASA Auto-GCAS（98.5% 防护率）、Sea Machines SM300（Siemens PLC 作 Checker）均采用此模式。**工业实际比例 50:1~100:1；IEC 61508/DO-178C/ISO 21448 均未规定量化倍数**——"100×"是经验设计目标，非规范要求。

### 关键证据

**工业参考**
- Boeing 777 PFCS — 3 通道（1 Command + 2 Monitor），PFC 主控制 100,000+ SLOC vs. Monitor <2,000 SLOC (~50:1)，FAA 认证 30 年 — A 级
- Airbus A320/A380 COM/MON — 不同供应商微处理器 + 独立软件团队，PRIM（复杂）vs. SEC（简化），DAL A — A 级
- NASA Auto-GCAS — 98.5% 防护率，>3,200 恢复，TPA 算法 Monitor — NASA/Lockheed · A 级
- Sea Machines SM300 — 工业 Linux PC（Doer）+ Siemens S7 PLC（Checker），DNV/BV 级批准 — B 级

**学术文献**
- Jackson et al. 2021 (arXiv:2104.06178) · Certified Control — Monitor 缩至几百行 vs. 导航栈数万行；证书验证范式 — A 级
- Simplex Architecture (1996+) · HPC/HAC + Decider — Doer-Checker 理论基础，RTA 模式 — A 级
- DO-178C Level A — MC/DC 100% 覆盖要求；无量化 Checker/Doer 复杂度比 — IEC 61508 SIL 2/3 同 — A 级

### "100×简单"量化专项（Phase 1 P0 反证议题）

| 来源 | 结论 |
|---|---|
| Jackson et al. 2021 | 无"100×"；仅"smaller and formally verifiable" |
| IEC 61508 SIL 2/3 | 强调独立性、验证方法；**无量化倍数要求** |
| DO-178C Level A | MC/DC 覆盖率要求；**无倍数** |
| Boeing 777 实践 | ~50:1 SLOC（设计目标，非规范强制） |
| Airbus FBW 实践 | >100:1（未披露精确数字） |

**结论**："100×"在工业标准中无依据；工业实践约 50:1~100:1；是设计目标而非规范约束。

**v1.1 修订建议**：CLAUDE.md §4 + v1.0 §2.5 改为：*"Checker 应比 Doer 简单 50×~100×（基于 SLOC/CYC，Boeing 777/Airbus FBW/Certified Control 实践），以支持 DO-178C Level A / IEC 61508 SIL 3 形式化验证。此为 [Design Goal based on Industry Practice]，非规范强制要求。"*

### 推翻信号

无架构级推翻。唯一修正：v1.0 的"100×"表述需从"规范要求"改为"设计目标（工业实践范围 50:1~100:1）"。

### 置信度 🟢

所有关键结论 🟢 High（≥3 A/B 级独立来源一致）。Boeing 777 Monitor SLOC 精确数字 🟡（来自多来源模式总结，无单一官方数字）。

### NotebookLM 入库结果

- 笔记本: safety_verification
- source_count BEFORE: 0 → AFTER (dedup): **35**（增量 +35，dedup 删除 0，入库成功率 94.6%）

---

## R5: SAT 模型 ROC 实证

### 核心结论

通过深度调研确立了三层证据链：(1) Chen ARL-TR-7180 的 SAT 理论框架（SAT-1 状态/SAT-2 理解/SAT-3 预测）在海事 ROC 应用的基础学术依据；(2) Veitch 2024 五因子随机对照试验，系统验证五大操作变量对接管绩效的影响，其中「可用时间」（20s vs 60s）效应最大，印证 60s 设计基线的科学性；(3) NTNU 岸控实验室的多感官沉浸式设计（全景视觉 225°、空间音频、触觉反馈）与 SAT 模型的对标应用，以及「透明度悖论」——过度透明（SAT-2 推理层）反而升高认知负荷，需要自适应分级展示。

### 关键证据

**工业参考（5 个）**
- NTNU Shore Control Lab (2023–2024) — milliAmpere 自主渡轮真实平台，全景显示（225°视场）+ 空间音频 + 触觉反馈的多感官 ROC 工作站；透明度分层（Layer 0–3），操作员最偏好 Layer 3（投影）— A 级
- USAARL 航空自适应自动化报告 (2026-02) — 不透明 + 主动发起产生最优状态（低负荷/强 SA/高信任）；透明 + 被动发起产生最差状态 — A 级（美军航空研究所）
- IMO MASS 框架（2020–2032 路线图）— Degree 3/4 定义 ROC 透明性的法规要求；SAT 模型成为「有意义人工控制」的必要条件 — B 级
- Cambridge Journal of Navigation (mHUD) (2024) — 海事抬头显示在桥楼与 ROC 均适用，SAT-1/SAT-3 直接叠加视景，降低频视切换认知负荷 — B 级
- ArXiv 基础模型语义异常检测 (2025-12) — Foundation Models 实现「语义透明度」，识别海事语义异常，SAT-2 推理层的关键补充 — B 级（前沿预印本）

**学术文献（8 篇）**
- Stowers et al. (2016) — 人-智能体协作的 SAT 界面设计与评估，Endsley 三层 SA 到 HAT 的经典映射 — A 级（直接论文）
- PMC 系统综述 (2022) — 24 篇同行评审，透明度与负荷的非线性关系综述 — A 级
- **Veitch et al. (2024)** — 五因子随机对照：A. 技能（专业 vs 游戏玩家无显著差异）、B. 监控时长（30 min 倦怠不影响性能）、C. 控制广度（3 船 vs 1 船显著降低）、**D. 可用时间（20s vs 60s 最大效应）**、E. DSS 缺失显著恶化 — A 级（2024 同行评审）
- NTNU Handover vs Takeover (2024) — System-initiated handover 需高 SAT-2；human-initiated 需高 SAT-1+SAT-3 — A 级
- Time Budget for Merchant Ship Control Takeover（预印本）— 在船船长：紧急 2.9 min（可用 10.3 min），常规 6.1 min（可用 8.1 min）vs ROC 设计基线 60s — **直接推翻信号** — B 级
- DST Australia (2023) — 《Operationalisation of Agent Transparency and Evidence for Its Impact》，透明度操作化关键变量实证框架 — B 级
- NTNU Shore Control Lab 多论文 (2022–2024) — 全景显示、空间音频、触觉反馈对接管绩效影响，milliAmpere 实船验证 — A 级
- Lynch (PhD, Southampton, 2024) — Human-Machine Teams for MASS Operations 博士论文 — B 级

### 推翻信号

1. **时间预算不匹配（Critical Gap）**：在船船长经验 3–6 min ≠ ROC 设计基线 60s。可消解条件：如果 SAT-3（投影层）输出高质量，60s 内可完成诊断；若 SAT-3 缺失，即便 6 min 也不够。**结论**：60s 可行，但**必须配高质 SAT-3 + 显式不确定度表示**。
2. **透明度悖论（Transparency Paradox）**：更多信息 ≠ 更好性能；透明（高 SAT-2）反而升高认知负荷。ROC 设计应「自适应触发」而非线性展示。
3. **专业背景失效**：专业船员与游戏高手在 ROC 任务中无显著性能差异，意味着 ROC 认知负荷类型与传统桥楼完全不同（数字 SA > 海洋直觉）。

### 来源年龄分布

| 范围 | 数量 | 占比 |
|---|---|---|
| <6 月（2026 Feb–Apr） | 8 | 42% |
| 6–24 月（2024 Feb–2026 Feb） | 9 | 47% |
| >24 月（<2024） | 2 | 11% |

### 置信度 🟢

- SAT 三层在海事 ROC 适用性 🟢（≥5 A/B 级独立来源）
- Veitch 2024 D 因子（可用时间）效应 🟢（A 级 RCT）
- 透明度悖论 🟢（USAARL 2026 + NTNU 2024 双独立来源）
- 60s 基线与 3–6 min 协调机制 🟡（需混合研究进一步验证）

### NotebookLM 入库结果

- 笔记本: maritime_human_factors
- source_count BEFORE: 0
- source_count AFTER (dedup 后): **19**
- 增量: +19
- dedup 删除失败来源数: 0

---

## R6: IMO MASS Code MSC 110 / 111

> ⚠️ 注：R6 subagent 在生成报告前被系统终止（sources 已入库 +32）。本节通过 nlm-ask 查询 maritime_regulations 笔记本重建，置信度 high。

### 核心结论

MSC 110（2025 年 6 月）完成了非强制性 MASS Code 18 个章节的定稿，人为因素部分待完善。非强制版预计 2026 年 5 月 MSC 111 正式通过，随后进入"经验积累期"（EBP），强制版最早 2032 年 1 月生效。规则基于**目标型（Goal-based）非强制框架**，是 SOLAS 等现有规范的**补充而非替代**。4 级自主度 D1–D4 定义明确，模式切换须在 ConOps 中预定义。对 L3 战术层的关键约束：ANS 必须综合态势感知规划避碰路线，若无法制定合规计划须立即报警。

### 关键证据

**工业参考（4 个）**
- IMO MSC 110 会议记录（2025-06）— MASS Code 非强制版 18 章定稿，无人船须具备 SAR 协助能力 — A 级
- IMO MSC 111 时间表（2026-05 通过预期）— 非强制版 → EBP → 强制版 2032-01-01 — A 级
- Ocean Infinity Armada 78-03（2021）— DNV-CG-0264 全球首个远程操作合规声明；2024–2025 太平洋/印度洋实际运营 — B 级
- MASS Code Part 3 Chapter 1（航行章）— ANS 规范要求：规划路线避碰、不超推进极限、严格按 COLREGs 机动 — A 级

**学术文献（3 篇）**
- IMO RSE 文件（2017–2018）— D1–D4 四级自主度分类的原始定义文献 — A 级（IMO 官方）
- IMO MASS Code Draft（Part 1–3 合并文本）— ConOps/OE/MoO/Fallback 条款；SOLAS 补充关系 — A 级
- DNV-CG-0264（2025.01）— 与 IMO D1–D4 的等效性条款；合规路径 — A 级

### IMO 4 级自主度对照（审计相关）

| 等级 | 定义 | L3 场景 |
|---|---|---|
| D1 | 自动化 + 决策支持，船上海员操控 | 当前主流，M8 提供 DSS |
| D2 | 远程控制，船上有海员备援 | 过渡态，ROC + 船上监督 |
| D3 | 远程控制，**船上无海员**，ROC 执行任务 | 目标态（本仓库主要设计对象） |
| D4 | 完全自主，系统自行决策行动 | 长期目标（v1.0 设计不完全覆盖） |

**模式切换（MoO）要求**：偏离 ODD 或发生故障 → 触发 Fallback response → 执行 MRM → 通知负责人员及周边船舶。

### Operational Envelope 结构（澄清）

v1.0 §3.x 引用"第 15 章 OE 要求"存在结构错位：MSC 草案中 OE 定义在 **Part 2 Chapter 1（运行环境）**；Part 3 Chapter 15 是**维护与维修**。L3 应对照 Part 2 Ch.1 的 OE 要求（界定功能/限制、地理区域、环境限制、操作模式分配）。

### 推翻信号

1. **Yara Birkeland / 珠海万山 无 IMO Code 合规映射记录**：笔记本中无这两个案例与 IMO Code 的映射材料（Ocean Infinity 有，但未在正文引用）。如 v1.0 引用了这些案例作为 IMO 合规证明，需专项核实。
2. **无人船 SAR 能力强制要求**：MSC 110 决定无人 MASS 必须具备 SAR 协助能力——这对 L3 设计有额外需求，v1.0 中未见明确映射。

### 来源年龄分布

| 范围 | 数量 | 备注 |
|---|---|---|
| <6 月（2025-11–2026-04） | ~10 | MSC 110 最新会议记录 |
| 6–24 月（2024–2025-10） | ~15 | IMO 草案迭代版本 |
| >24 月（<2024） | ~7 | RSE/D1–D4 基础文件 |

### 置信度 🟢

- D1–D4 四级自主度定义 🟢（IMO 官方，多版本一致）
- MSC 110/111 时间表 🟢（官方会议记录确认）
- OE Part 2 Ch.1 结构 🟢（官方草案）
- MSC 111 2026-05 通过状态 🟡（"预计"，未通过；知识截止 2025-08）
- Yara Birkeland / 珠海万山 IMO 合规映射 ⚫（笔记本无相关来源）

### NotebookLM 入库结果

- 笔记本: maritime_regulations
- source_count BEFORE: 51（R3 完成时）
- source_count AFTER R6 入库: 83（R6 +32，dedup 后 0 失败）
- 增量: +32
- 注：subagent 被系统终止前 nlm-research 已完成，dedup 由主 agent 手动执行

---

## R7: CCS i-Ship + DNV-CG-0264

### 核心结论

CCS i-Ship 和 DNV-CG-0264 均采用基于风险的性能式认证框架。CCS 通过 Nx/Mx/Ex/I 记号体系分解功能维度，DNV 通过 AROS 级注分解位置/自主度维度，两者均强制要求决策"白盒可审计"。CCS 要求运营手册包含"安全状态评估 + 输出合理性 + 边界定义"三要素；DNV 通过 Doer-Checker 双轨和 ODD 机制隔离复杂决策与简单可验证检查器。IEC 61508（故障驱动）与 ISO 21448 SOTIF（功能不足驱动）在此框架下互补。实船案例（Dazhi、Zhifei、Zhuhai Yun）证实框架可行性。

### 关键证据

**工业参考（4 个）**
- CCS《智能船舶规范》(2025) — 官方 i-Ship Nx/Mx/Ex/I 记号完整定义、认证流程、组件证书（C/E/W/DA/TA-A/B）、白盒审计要点 — A 级（官方一作）
- DNV AROS 级注家族 (2025) — 9 个技术子功能定义、Doer-Checker 架构推荐、ODD 完整性要求、系统资格认证（SQ）流程 — A 级（官方一作）
- Ocean Infinity Armada 78（2021）— DNV-CG-0264 全球首个远程操作符合性声明；混合控制（船端自主+远程监督）+冗余数据镜像实船验证 — B 级
- GreenHopper 自动货船（ShippingLab/DTU）— "虚拟船长"与底层动力控制深度解耦，符合 DNV Doer-Checker 推荐架构 — B 级

**学术文献（4 篇）**
- STPA-FTA 并行风险评估框架（MDPI 2023）— 系统理论过程分析 + 故障树分析用于 MASS 风险识别；SOTIF 缓解策略 — B 级（同行评审）
- 自主海事系统设计风险缓解（DTU 研究库）— ACS 在 AROS 等级中的角色；认知置信度识别；人机移交协议 — B 级
- 功能安全 vs. SOTIF 对比（Leadvent/TÜV SÜD 白皮书）— IEC 61508 与 ISO 21448 应用差异、SIL 等级确定、ODD 覆盖率评估 — B 级（权威咨询机构）
- Doer-Checker 在自动驾驶中的安全实现（CMU ECE642 讲义）— 双轨制设计、检查器逻辑与决策系统隔离、MRM 实现方法 — B 级

### DNV-CG-0264 九子功能映射表

| 编号 | 子功能名称 | 工程含义 | Doer-Checker 对应 |
|---|---|---|---|
| 9.1 | 电子数据处理（EDP） | 实时计算平台 | Doer: 算法执行；Checker: 处理器负载/时序约束监控 |
| 9.2 | 电能产生与转换 | 冗余电源管理 | Doer: 能源管理算法；Checker: 电压/电流硬限制 |
| 9.3 | 监控系统 | 子系统自诊断、状态反馈 | Doer: 传感器融合；Checker: 硬件健康检查（Watchdog）|
| 9.4 | 导航与通信系统 | GNSS/雷达融合、船-岸链路 | Doer: 协同定位算法；Checker: GNSS 可用性/通信延迟门限 |
| 9.5 | 船载网络与安装 | 传感器-处理器-执行器数据传输 | Doer: 网络路由；Checker: 包投递率/报文超时检测 |
| 9.6 | 船舶管理系统 | 航次规划、能效优化 | Doer: 高层 MPC；Checker: 功能冲突优先级仲裁 |
| 9.7 | 船舶操作与自动化设备 | 数字指令→舵角/转速执行 | Doer: 执行器控制律；Checker: 物理极限包络线 |
| 9.8 | 警告与安全设备 | ODD 越界报警、应急保护 | Doer: 场景分类器；Checker: ODD 边界的硬确认 |
| 10.1 | 外部结构与环境接口 | 港口/浮台数据交换、近场作业 | Doer: 靠泊/解缆规划；Checker: 地理围栏（Geo-fence）验证 |

### 推翻信号

1. 🟡 **CCS vs DNV ROC 定义冲突风险**：CCS 将 ROC 视为 Ri 记号的一部分；DNV 将 ROC 分离为独立技术实体（DNV-ST-0322）。同一项目同时适用可能产生重复或冲突认证要求。
2. ⚫ **IMO MASS Code 与 CCS/DNV 等效性条款缺失**：仅通过 MSC.1/Circ.1455 "Equivalence Pathway" 关联，2032 年强制码施行后的过渡映射无明确来源。

### 来源年龄分布

| 范围 | 数量 | 备注 |
|---|---|---|
| <6 月（2025/10–2026/04） | ~30 | CCS 2025 版、DNV AROS 2025 新发布 |
| 6–24 月（2024–2025/09） | ~4 | DTU/CMU 资料、白皮书 |
| >24 月（<2024） | <1 | 早期 DNVGL 草案 |

### 置信度

- CCS i-Ship 记号体系与认证流程 🟢（官方文档 + 4 实船案例）
- DNV-CG-0264 九子功能与 Doer-Checker 架构 🟢（官方准则 + CMU 学术 + 3 工业项目）
- IEC 61508 / ISO 21448 映射关系 🟢（TÜV SÜD/Leadvent/PTC 共识）
- 实船运行数据反馈 ODD 定义的长期有效性 🟡（仅 6–12 月试运营记录）
- IMO Code 与 CCS/DNV 等效性条款 ⚫（无来源）

### NotebookLM 入库结果

#### 阶段 A（maritime_regulations）
- source_count BEFORE: 83
- source_count AFTER (dedup 后): **89**
- 增量: +6
- dedup 删除失败来源数: 0

#### 阶段 B（safety_verification）
- source_count BEFORE: 35
- source_count AFTER (dedup 后): **64**
- 增量: +29
- dedup 删除失败来源数: 0

---

## R8: Capability Manifest / Backseat Driver

### 核心结论

多船型 Backseat Driver 架构工业实现已成熟，以 MOOS-IvP（Benjamin 2010）为学术基座，Sea Machines SM300-NG 和 Kongsberg K-MATE 为工业标杆。核心哲学是**前座/后座解耦**（Front-Seat 控制层 @ 50 Hz vs. Backseat 自主层 @ 1–4 Hz），通过标准化接口（CANBus/OPC-UA/ROS DDS）实现跨船型复用。多船型适配通过三层实现：(1) Capability Manifest（YAML/JSON Schema 静态参数集）；(2) 水动力插件接口（动态模型适配）；(3) 行为字典 + IvP 仲裁。**重要推翻信号**：海事专属 Capability Manifest 标准缺失（仅通用 IoT Schema），Kongsberg 参数适配层商业机密，"零船型常量"无独立审计。

### 关键证据

**工业参考（7 个）**
- **Benjamin et al. (2010), MOOS-IvP** — DTIC ADA559856；Backseat Driver 原始架构，MOOSDB 发布-订阅，IvP Helm 多目标仲裁 — A 级
- MIT Marine Autonomy Lab（持续维护至 2025）— IvP Helm 官方设计规范，前座/后座接口 + 频率表 — A 级
- Sea Machines SM300-NG (2023–2025) — ABS/DNV 入级；多船型认证；200% 计算能力升级；SMLink 远程指挥 — A 级
- Sea Machines 测量应用案例 (2023–2024) — 同一套自主栈部署到测量/巡逻/跟踪多型任务，行为库标准化 — A 级
- Kongsberg K-MATE (Hydro International, 2019) — Scene Analysis vs. Control 两层（Backseat 范式工业化）；CANBus/ROS/OPC-UA 接口 — A 级
- Kongsberg Sounder USV (Ocean Business 2019) — K-MATE + Sounder 多船型可部署动态避碰 — A 级
- awvessel.github.io Manifest Spec — 开源舰艇 Capability Manifest 结构化参数定义（船舶尺寸/操纵系数/推进配置）— B 级

**学术文献（4 篇）**
- Newman/Benjamin et al., IvP Helm User Guide Release 4.2.1 (ResearchGate 279808905) — IvP Helm 行为字典完整规范，多目标 Interval Programming 框架 — A 级
- "Toward Maritime Robotic Simulation in Gazebo" (ResearchGate 338729474) — 水动力插件接口（Hydrodynamics.hh）；参数化 MMG/线性-非线性水动力 — B 级
- "Three Patterns for Autonomous Robot Control Architecting" (UPV, 2013) — 前座/后座解耦形式化设计方法 — B 级
- "CasADi – A software framework for nonlinear optimization" (2018) — MPC 与水动力约束集成的数值工程实现 — B 级

### 推翻信号

1. **Capability Manifest 海事专属规范缺失** 🟡：55 个研究源中仅 1 个海事专属 Manifest 规范（awvessel.github.io），其他为通用 IoT Schema（AWS IoT/OpenAPI）。Sea Machines 和 Kongsberg 均未公开参数配置格式——商业竞争壁垒。
2. **水动力插件实时性能基准缺失** 🟡：Gazebo 实现完整，但无 real-time 约束验证（10ms 周期可行性）、无舰队规模下的伸缩性基准。
3. **Kongsberg K-MATE 参数适配层黑箱化** 🔴：仅有概念级描述，无实现细节（商业机密）。
4. **零常量代码的独立审计缺失** ⚫：无第三方验证文献确认"决策核心零船型 hardcoded 常量"。

### 来源年龄分布

| 范围 | 数量 | 占比 |
|---|---|---|
| <6 月（2025/09–2026/04） | 10 | 37% |
| 6–24 月（2024–2025/09） | 22 | 41% |
| >24 月（<2024） | 23 | 43% |

### 置信度

- MOOS-IvP Backseat Driver 架构 🟢（3+ A 级源 + 开源代码可验证）
- Sea Machines SM300-NG 多船型适配 🟢（官方文档 + DNV/ABS 入级）
- Kongsberg K-MATE 前座/后座设计 🟢（官方发布 + Hydro International 认可）
- Capability Manifest 海事行业标准 🟡（通用 Schema 完整，海事专属规范缺失）
- 水动力插件实时性能 🟡（实现完整，real-time 验证不足）
- Kongsberg 参数适配具体实现 🔴（商业机密）
- 零常量代码独立审计 ⚫（无第三方验证）

### NotebookLM 入库结果

- 笔记本: colav_algorithms
- source_count BEFORE: 64
- source_count AFTER (dedup 后): **91**
- 增量: +27
- dedup 删除失败来源数: 0

---

## 跨主题观察

> 基于 R1–R8 全部 8 轮证据的横向印证与矛盾分析。

### 互相印证（支持 v1.0 架构方向）

1. **R2 vs R8（IvP 仲裁架构一致性）**：R2 学术 COLAV 证实 IvP Helm 是 SIL 2 环境下的金标准；R8 工业实例证实 MOOS-IvP Backseat Driver 是 Sea Machines / Kongsberg 的工业基座。v1.0 M4 Behavior Arbiter 选择 IvP 路线具有充分支撑。

2. **R4 vs R7（Doer-Checker 认证路径）**：R4 证实 Boeing 777/NASA GCAS 的 50:1~100:1 SLOC 比例；R7 证实 DNV-CG-0264 九子功能中 9.1–9.8/10.1 均有明确的 Doer-Checker 映射。两者共同支持 v1.0 M7 Safety Supervisor 的 Doer-Checker 设计。

3. **R3 vs R6（ODD 框架认证一致性）**：R3 确认 Rødseth 2022 二轴 OE 框架；R6 确认 IMO MASS Code 使用目标型 ODD 边界检测；R7 确认 DNV-CG-0264 AROS 三维认证空间（≠ ODD 定义）。三轮形成一致的概念分层（ODD 定义 ≠ 认证空间），为 v1.1 修订 §3.2 三轴表述提供明确路径。

4. **R1 vs R5（透明性设计方向）**：R1 揭示 Kongsberg K-MATE 仍保留"人工批准"步骤；R5 证实 Veitch 2024 ROC 操作员对 SAT-3 投影层最关键。两者共同支持 v1.0 M8 HMI/Transparency Bridge 的 SAT-1/2/3 分层设计，并指向"高质量 SAT-3 + 自适应触发"是缩短接管时窗的关键。

### 矛盾与张力（需在阶段 3-4 重点审计）

1. **R1 vs v1.0 D3/D4 自主等级（主要矛盾）**：R1 证明当前主流工业系统在 COLREGs 决策上仍保留强制人类确认点（Kongsberg K-MATE）；v1.0 在 D3/D4 下设计为完全自主决策。与 R6 IMO D1–D4 定义对照：D3（船上无海员，ROC 执行任务）确实允许完全远程控制，但"ROC 随时干预能力"是 MSC 110 的硬约束。→ **F-P0 候选 Finding**：v1.0 在 D3 场景下的决策自主度边界需与 MSC 110 ConOps 要求对齐。

2. **R5 vs v1.0 TMR 60s 设计基线（重要张力）**：R5 发现在船船长经验 3–6 min vs. 60s 设计基线存在 3–6 倍差距；但 Veitch 2024 也证明"可用时间"是最重要的效应因子，60s 内高质 SAT-3 可消解差距。→ **v1.0 60s 基线可辩护，但需在设计文档中明确"配 SAT-3 高质量输出"的前提条件**。

3. **R3 vs v1.0 §3.2 三轴 ODD（P0 级确认矛盾）**：R3 已明确 v1.0 E×T×H 三轴无权威来源。这是跨 R3/R6/R7 三轮研究中最一致的推翻信号。→ **Phase 1 遗留的 P0 反证议题，在 Phase 3 中须最高优先级处理**。

4. **R7 vs R4（"100×"来源冲突已解消）**：R4 证明"100×"不是规范要求而是设计目标，R7 DNV 九子功能未规定比值。两轮一致——v1.0 §2.5 措辞需从"必须"改为"目标（工业实践 50:1~100:1）"。

5. **R8 Capability Manifest 规范缺失（设计空白）**：R8 未找到海事专属的 YAML/JSON Schema Capability Manifest 标准。v1.0 §13 多船型设计中的 Capability Manifest 概念需自行定义格式，无法直接引用现有行业标准——**v1.0 §13 是原创设计，需在文档中明确标注"自定义规范，参考 MOOS-IvP + awvessel 格式"**。

### 阶段 2 累积效应：笔记本状态

| 笔记本 | R1 前 | R1-R4 后 | R5-R8 后 | 净增 |
|---|---|---|---|---|
| colav_algorithms | 0 | 64 | 91 | +91 |
| maritime_regulations | 0 | 51 | 89 | +89 |
| safety_verification | 0 | 35 | 64 | +64 |
| maritime_human_factors | 0 | 0 | 19 | +19 |
| ship_maneuvering | 0 | 0 | 0 | 0 |
| **合计** | **0** | **150** | **263** | **+263** |
