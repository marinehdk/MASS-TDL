# A 角度评审报告 — 进度可行性 & 关键路径

| 属性 | 值 |
|---|---|
| Reviewer Persona | 资深 PM / 工程经理（船舶 + 大型嵌入式系统交付，10+ 年）|
| Scope | 8 月开发计划 v2.0（5/1–8/31 硬承诺 17 周 / ~55.5 人周）+ 8 模块详细设计的 scope-effort 一致性 + HAZID RUN-001 节奏 + Phase 4 关键依赖 |
| 总体判断 | 🔴（前 4 月硬承诺存在 ≥ 2 条 P0 不可调和的 schedule risk；critical path 无可见缓冲）|
| 评审基线 | 架构 v1.1.2 / Plan v2.0 / M5+M7 详细设计草稿（53.4 KB / 65.6 KB）|

---

## 1. Executive Summary（≤ 200 字）

8 月计划 v2.0 在纸面上自洽：~55.5 人周 ÷ 17 周 ÷ 2 人 ≈ 1.63 人/周/人，附录 A 自报 ~13% 缓冲。但拆开 critical path 看，**M5 双 MPC（7 人周）+ M7 PATH-S（6 人周）+ HAZID 8/19 完成 + v1.1.3 回填 + SIL 1000 场景报告 + D3.7 全集成**全部压在 8/10–8/31 同一窗口，且共用同一名"技术负责人"。任何一项滑动 1 周都会推倒 8/31 硬承诺。

主要红线：(a) HAZID 13 周仅靠每周 ~2h workshop，与 DNV/DMA 公开案例的 single/multi-day 集中工作模式不一致 [R1]，132 个 [TBD] 参数在 26h 总会议时间内难以全部敲定；(b) RUN-001 kickoff 文档自身写"12 周至 8/19"且把"FCB 实船 ≥50 nm + 100 h"安排在第 5–6 周（6/10–6/24），**与开发主计划"FCB 实船预期 12 月"互相冲突**；(c) HIL 需求单 7/13 提交，按公开半导体/伺服 26–52 周交期 [R2]，10 月到货为乐观档，9–11 月到货是中位档。建议立刻把 8/31 拆为"软关门 + 9/30 硬关门"，否则 12 月实船准入门槛无定义将进一步雪崩。

---

## 2. P0 阻断项（≤ 5 条）

### P0-A1 · "技术负责人"在 8/10–8/31 同时承担 5 个并行交付，单点不可承受
- **Finding**：D3.2（M5，7 人周，目标 8/10）/ D3.3（M7 PATH-S，6 人周，8/10）/ D3.5（v1.1.3 HAZID 回填，8/31）/ D3.6（SIL 1000+ 场景报告，8/31）/ D3.7（8 模块全系统集成报告，8/31）的"责任人"分别记为 M5 负责人 / M7 负责人 / 架构师 / 技术负责人 / 技术负责人；但 HAZID 校准回填、SIL 1000 场景验证、全系统集成 bug 修复在串行实施时**全部要求技术决策权**。仅 2 人团队（plan 头表第 9 行：「团队规模：2人等效」），技术负责人不可能在 3 周内同时主持这 5 条线。
- **影响**：8/31 硬承诺中至少 1 项滑落，触发整个 Phase 4 倒挂（HIL 搭建排期 / CCS AIP 申请 / 12 月实船准入）。
- **证据**：
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:354,386,422,438,461,481`（5 个 D3.x 责任人 + 目标日期）
  - `gantt/...:9`（团队规模 2 人）
  - `gantt/...:317-326`（Phase 3 甘特图同周条带重叠）
- **置信度**：🟢（4 个独立锚点 + 文档内部交叉一致）
- **整改建议**：
  1. 把 D3.5 / D3.6 / D3.7 至少其一软化到 9/30（Phase 4 第 1 月内）；或
  2. 把 D3.5（v1.1.3 回填）独立外包给"架构师"，与技术负责人解耦；并
  3. 在 plan 附录 A 新增「critical-path person-week 加载图」，明确 8/10–8/31 每周每人的小时占用上限（建议 ≤ 35h/周以容纳 HAZID 参会 + 评审）。

### P0-A2 · HAZID 进度文档与开发主计划自相矛盾，FCB 实船窗口冲突
- **Finding**：HAZID 主计划写「13 次会议，共 13 周，至 8/19 完成」；但 `RUN-001-kickoff.md:93-95` 明确把"FCB 实船试航 ≥ 50 nm + ≥ 100 h"安排在 RUN-001 周 5–6（即 **2026-06-10 → 2026-06-24**）。开发主计划头表第 13 行写「FCB 实船：预期 2026-12，以船厂进度为准」、`gantt/...:519` 把 FCB 实船放在 12 月。两份文档对同一艘船的占用时间相差 6 个月。
- **影响**：HAZID 第 7 周（7/1）「实船数据回归 + ML 阈值优化」无数据可回归 → HAZID 第 8 次会议（7/8）阈值无法初定 → 8/19 完成承诺 collapse → 8/31 v1.1.3 回填全部参数失败 → CCS AIP 提交无 HAZID 终稿。
- **证据**：
  - `docs/Design/HAZID/RUN-001-kickoff.md:78-112`（12 周时间表，FCB 实船在周 5–6）
  - `docs/Design/HAZID/RUN-001-kickoff.md:152-157`（FCB 实船「位置：（待填）」，未预定）
  - `gantt/MASS_ADAS_L3_8个月完整开发计划.md:13`（FCB 12 月）
  - `gantt/...:519`（展望甘特图 FCB 12 月）
- **置信度**：🟢（4 个仓库内文件互证）
- **整改建议**：
  1. 立即裁决：HAZID RUN-001 是否可以 **"无实船数据"** 完成 8/19 阈值固化？若不行，HAZID 时间表必须重排到 12 月实船试航之后（推迟 ≥ 4 个月）；
  2. 若 HAZID 必须先于 8/19 完成，则需提供"水池试验 + 4-DOF MMG 仿真 + 历史 AIS 回归"作为实船替代证据，并在 RUN-001 kickoff 文档显式标注「初版基于仿真，实船校准延期至 v1.1.4」；
  3. 对 CCS / DNV 同步沟通预审深度调整（这是合规决策，不是 PM 决策）。

### P0-A3 · 8/31 硬承诺缺乏 schedule slack；critical path 实际为零缓冲
- **Finding**：附录 A 自报"前 4 月总计 ~55.5 人周，低于 ~64 人周产能上限，保留约 8.5 人周缓冲（13%）"。但同一附录把"代码审查、Bug 修复、会议参与、HAZID 配合"列入这 8.5 人周——这些是已知工作不是缓冲。"船舶项目典型 rework rate"在公开数据中无定量来源（⚫ Unknown），但 IEC 61508 SIL 2 软件开发在工业界给出的安全开销系数为 2–4× [R3]，本计划对 M7 PATH-S 仅给 6 人周（占 M7 详细设计 1673 行），对照同类研究型 MPC + COLREGs ASV 实现（NTNU Bitar / Eriksen 系列 [R4]）多为多 PhD 年级别工作，**6 人周仅够 stub + 单元测试，难以覆盖 PATH-S 独立路径 + Doer-Checker 独立性 CI 强制 + SOTIF 5 类假设违反 + 6 类硬约束 + SHA-256 ASDR**。
- **影响**：M7 滑落 1–2 周，连锁推迟 D3.7（8 模块 SIL 全系统集成）、D3.6（1000 场景）、Phase 4 起跑。
- **证据**：
  - `gantt/...:627`（缓冲表述 + 已知工作混入）
  - `Detailed Design/M7-Safety-Supervisor/01-detailed-design.md`（1673 行 spec）
  - `Detailed Design/M5-Tactical-Planner/01-detailed-design.md`（1241 行 spec）
  - [R3] [R4]
- **置信度**：🟡（2 来源；rework rate 部分 ⚫ Unknown — 公开数据未找到船舶项目"典型 rework rate"定量基准）
- **整改建议**：
  1. 把 M7 effort 从 6 人周上调至 ≥ 9 人周（+50%，与 NTNU 同类系统对齐量级），或
  2. 把 M7 拆为 M7-core（IEC 61508 watchdog + 6 类硬约束，6 人周）+ M7-sotif（5 类假设违反 + 滑窗，3 人周延后到 Phase 4 第 1 月）；
  3. 在主计划新增「真实 schedule slack」字段，剔除已知工作后重算。

---

## 3. P1 重要风险（≤ 8 条）

### P1-A1 · HAZID workshop 总会议时长 ~26h vs 132 [TBD] 参数：单参数平均 11.8 min
- **Finding**：13 次会议 × 第 1 次 3 小时 + 后续按 plan 备注「每次 ~2h」≈ 27 小时总 workshop 时间，需固化 132 项 [TBD-HAZID]（架构 v1.1.2）+ 8 项 RPN ≥ 9 优先项的失效场景识别 / FMEA / LOPA / DNV 反例搜索。DNV 公开 DMA 高速船 HAZID 案例为 single-day workshop 识别 48 hazards（约 10 min/hazard）[R1]。本计划单参数平均时间 ≈ 12 min，**仅够"identify"不够"validate + calibrate"**。
- **证据**：
  - [R1] DNV-led DMA HAZID single-day, 48 hazards
  - `docs/Design/HAZID/RUN-001-kickoff.md:42-53`（第 1 次会议 3h 议程，覆盖 5 类参数 ≈ 25 项）
  - `gantt/...:31`（每次参会 2h）
  - 架构 v1.1.2 132 个 [TBD-HAZID]（D3.5 文档说明）
- **置信度**：🟡（DNV 案例非 MASS L3，但同等级海事 HAZID 公开参考；DNV/ABS 对 MASS L3 标准 workshop 天数无公开定量基准 ⚫ Unknown）
- **整改建议**：把 RPN ≥ 9 的 8 项参数集中安排 ≥ 2 个 full-day 工作会议（≥ 16h 集中讨论），其余参数保留 weekly 节奏。

### P1-A2 · M5 双 MPC（CasADi/IPOPT + BC-MPC） 7 人周 vs NTNU/Kongsberg 公开案例量级
- **Finding**：D3.2 给 M5 7 人周（含 Mid-MPC 90s 时域 N=18 + BC-MPC k=7 候选航向树 + RFC-001 双接口 + ROT_max 速度查表 + YAML 热加载）。NTNU Eriksen / Bitar 系列 [R4] 同等功能（Telemetron ASV，Hybrid COLAV Rules 13–17）为多年 PhD 工程量；Kongsberg MAROFF 项目 244116 为多机构多年规划。7 人周仅能交付"研究 prototype + 8/10 SIL 通过率冲指标"，不构成 PATH-D 工业级实现。
- **证据**：[R4] Frontiers 2020 Hybrid COLAV / [R5] arXiv 2024 Distributed MPC inland waterways / `Detailed Design/M5-Tactical-Planner/01-detailed-design.md` 1241 行
- **置信度**：🟡（行业公开论文未给 person-weeks，结论为量级推断）
- **整改建议**：把 M5 8/10 软目标改为"Mid-MPC SIL 通过 + BC-MPC stub"；BC-MPC 完整实现推到 Phase 4 第 1 月（与 HIL 搭建并行）。

### P1-A3 · HIL 7/13 下单 → 10 月到货是乐观档
- **Finding**：plan 头表第 14 行 + 关键里程碑表（gantt 第 641 行）按 3 月交期。但公开 2026 年半导体/电机控制 IC/FPGA 交期 26–52 周 [R2]，HIL testbench 含 PLC + 伺服 + RTOS 控制器，"3 个月"为最乐观估计。
- **证据**：[R2] RoboticsTomorrow 2026-01 26–52 weeks
- **置信度**：🟢（A 级公开 2026 数据）
- **整改建议**：
  1. 7/13 之前完成 HIL 选型（dSPACE / Speedgoat / 国内 HiRain / 经纬恒润）二选一；
  2. 主板 + 关键 FPGA 立即下单（不等 7/13）；
  3. 给 Phase 4 D4.1（HIL 搭建 9/30 展望）显式备份方案：SIL Cluster 横向扩展为"准 HIL"。

### P1-A4 · 12 月实船准入门槛未定义
- **Finding**：plan 第 11 行明确「12 月实船试航 — 展望，以船期为准」，但**全计划未列出"什么条件下允许上船"**。CCS AIP 反馈周期通常 3–6 月（plan 第 597 行自承），SIL 2 第三方评估也排到 11 月（D4.3 展望），即 12 月上船时既无 AIP 受理证据，也无 SIL 2 第三方报告。
- **证据**：`gantt/...:521-588`（Phase 4 展望章节，无准入门槛字段）
- **置信度**：🟢
- **整改建议**：在 plan 附录 B 之后新增「实船准入门槛」短表：（a）D3.7 8h 无崩溃 + 0 P0 bug，（b）D4.2 HIL ≥ 50h 无致命故障，（c）CCS 至少出具"中期意见"，（d）M7 SOTIF 滑窗在线监控开启。任一项不满足→延后实船。

### P1-A5 · "2 人 + AI" 持续生产率 1.63 人周/人/周 在 17 周内无回弹机制
- **Finding**：附录 A 总计 55.5 人周 ÷ 17 周 ÷ 2 人 ≈ 1.63 人周/人/周。考虑 5/1–5/5 劳动节、6/22 端午节、HAZID 13 次 × 2h 参会、6 RFC review 余波、临时 CCS 沟通等已知非编码工作，**真实 sustained productivity 接近 1.85 人周/人/周**——在两人 zero-bus-factor 的小团队场景下，这是工业界连续高位档，任何一人病假 1 周即损失 5.9% 总产能。
- **证据**：`gantt/...:603-625` 附录 A
- **置信度**：🟡（无对标 dataset，量级推断）
- **整改建议**：明确"AI 辅助"覆盖哪些任务、不覆盖哪些；把 RFC review、HAZID 配合、Bug fix 等 overhead 单独立项。

### P1-A6 · Phase 1 SIL 框架 5 人周（D1.3）含 RL 对抗生成是激进估计
- **Finding**：D1.3 含 6 个子模块（FCB 4-DOF MMG / AIS 管道 / YAML 场景 / COLREGs 覆盖率报告 / SIL HMI / RL 对抗）目标 6/8 完成，5 人周（即 1 人 ≈ 5 周，2 人 ≈ 2.5 周）。MMG + RL 对抗任一单项在公开学术工作中均为月级别工程量。
- **证据**：`gantt/...:124-141`
- **置信度**：🟡
- **整改建议**：把 RL 对抗模块降级到「v1.0 = 简化 PPO + ≥ 5 边界场景」即可（plan 已写"≥ 5 个 stub 出错的边界场景"，仍要在 5 人周内完成 stub-level 训练）；或把 RL 推到 Phase 2 末尾（不阻塞 SIL v1.0 上线）。

### P1-A7 · D2.4 M6 COLREGs 500+ 场景 7/31 完成 → D3.6 1000+ 场景 8/31 完成，场景翻倍只给 1 个月
- **Finding**：500 → 1000+ 场景在 4 周内完成，且要 ≥ 98% pass + Rule 5/6/7/8/9/13–17/19 全分支覆盖。期间 M4/M5/M7/M8 全部刚集成，每个 bug fix 都需要重跑覆盖率。
- **证据**：`gantt/...:286,470`
- **置信度**：🟢
- **整改建议**：场景库增量化 + CI 自动化每日覆盖率回归；不要等到 8 月最后一周冲指标。

### P1-A8 · M7 PATH-S Doer-Checker 独立性 CI 强制启用时点未明
- **Finding**：D1.2 DoD 第 3 项「Doer-Checker 独立性检查脚本在 CI 中强制运行（非警告模式）」目标日期 5/24，但 M7 7/20 才动工——意味着 5/24–7/20 这 8 周内 CI 上没有任何 M7 代码可被检查；R3.2 提到「Phase 3 开始 M7 时第一优先修复所有 violation」，**但 M1–M6 在 5–7 月已经写了 ~22 人周代码，可能已经引入 PATH-S 跨边界依赖**。
- **证据**：`gantt/...:120,498`
- **置信度**：🟡
- **整改建议**：D1.2 验收时把"PATH-S 独立性检查"对 M1–M6 stub 头文件先开启 dry-run（可生成报告但不阻塞），Phase 2 起逐步收紧。

---

## 4. P2 改进建议（≤ 5 条）

- **P2-A1**：附录 A 工作量表与附录 B 里程碑表的口径不一致（附录 A 写"前 4 月 ~64 人周产能上限"，但未给出每人的 weekly hours 假设）→ 建议补 productivity assumption（如 35h/人/周）。
- **P2-A2**：plan 没有 risk burndown / weekly status review 节奏定义；建议每周五 30 min 站会 + 每月最后 1 个工作日 phase review。
- **P2-A3**：D3.5 v1.1.3 回填 2 人周仅由"架构师"承担，但 132 个参数中有大量 M4/M5/M6/M7 参数，需要模块负责人共同参与；建议拆为「架构师协调 1 人周 + 4 模块负责人各 0.25 人周」。
- **P2-A4**：12 月 FCB 实船试航无 dry-run / 准入演练 plan；建议 11 月安排 1 周「码头静态测试 + 港内低速试航」缓冲。
- **P2-A5**：plan 未定义"软关门"概念（哪些 DoD 项可选择性宽松、哪些是硬条件）；建议每个 DX 标 P0/P1 DoD 分级。

---

## 5. 架构/模块缺失项（如发现）

- **缺失 1**：plan 未引用任何"FCB 占用排期"文件——HAZID kickoff 把 FCB 当作 6 月可用资源，主计划当作 12 月可用资源，**两份文档没有 single source of truth**。建议立即新增 `docs/Design/Resource Calendar/FCB-occupancy.md`。
- **缺失 2**：plan 未列「2 人是谁」+ Backup 分工（plan 9 行只写"2人等效"）。建议补 RACI，每个 D 都标 R/A 分别是谁。
- **缺失 3**：plan 无"AI 辅助边界"声明——AI 辅助哪些（代码生成、文档审稿、SIL 场景生成）、不辅助哪些（HAZID 决策、CCS 沟通、实船操作）需明确，否则"2 人等效"含糊。

---

## 6. 调研引用清单

- **[R1]** DNV GL HAZID + risk-reducing measures workshops report for Danish Maritime Authority, single-day HAZID 2016-11-08, 48 hazards / 12 topics — https://maritimecyprus.com/wp-content/uploads/2017/04/dnvgl-dma-safety-analysis-1.pdf
- **[R2]** Semiconductor Sourcing for Robotics: Managing Lead Times and Component Lifecycle Risks, RoboticsTomorrow 2026-01: "Lead times of 26 to 52 weeks are no longer exceptional" — https://www.roboticstomorrow.com/article/2026/01/semiconductor-sourcing-for-robotics-managing-lead-times-and-component-lifecycle-risks/26010
- **[R3]** IEC 61508 functional safety overview, SIL 2 effort multiplier context — https://en.wikipedia.org/wiki/IEC_61508 ; https://www.perforce.com/blog/qac/what-iec-61508-safety-integrity-levels-sils
- **[R4]** Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17, Frontiers in Robotics and AI 2020 (Eriksen et al., NTNU/Kongsberg-funded) — https://www.frontiersin.org/journals/robotics-and-ai/articles/10.3389/frobt.2020.00011/full
- **[R5]** Distributed MPC for autonomous ships on inland waterways with collaborative collision avoidance, arXiv 2024 — https://arxiv.org/html/2403.00554v1
- **[R6]** Scenario-Based Model Predictive Control for Ship Collision Avoidance, Johansen NTNU (Kongsberg/DNV-GL/Sintef MAROFF 244116) — https://torarnj.folk.ntnu.no/colregs_cams.pdf
- **[R7]** DNVGL-CG-0264 Autonomous and remotely operated ships — https://www.smashroadmap.com/files/dnvgl-cg-.pdf

仓库内部锚点：
- `docs/Design/Architecture Design/gantt/MASS_ADAS_L3_8个月完整开发计划.md`（v2.0 主计划）
- `docs/Design/HAZID/RUN-001-kickoff.md:78-112,152-157`（HAZID 时间表 + FCB 实船定位）
- `docs/Design/Detailed Design/M5-Tactical-Planner/01-detailed-design.md`（1241 行）
- `docs/Design/Detailed Design/M7-Safety-Supervisor/01-detailed-design.md`（1673 行）
- `docs/Design/Detailed Design/00-detailed-design-master-plan.md`

NLM 查询尝试：
- `safety_verification` 笔记本对"HAZID 标准 workshop 时长"返回 not_found（confidence ⚫）；触发 WebSearch fallback。

---

## 7. 行业基准对标（Industry Benchmark）

| 本计划项 | 对标对象 | 证据 | 差距 |
|---|---|---|---|
| HAZID 13 周 × ~2h | DNV-led DMA HAZID single-day 48 hazards [R1] | DMA 案例 1 day, 48 hazards | 本计划摊薄到每周 2h，单参数 ~12 min；建议至少 2 个 full-day 集中会议 |
| M5 双 MPC 7 人周 | NTNU/Kongsberg Hybrid COLAV [R4] / Bitar MPC COLREGs [R6] | 多 PhD 年；MAROFF 244116 多机构 | 7 人周仅交付 prototype 量级，无 person-weeks 工业基准 ⚫ Unknown |
| M7 PATH-S 6 人周 | IEC 61508 SIL 2 软件开发 2–4× 安全开销系数 [R3] | SIL 2 文档 + 独立性 + MC/DC 覆盖 | 6 人周对应 1673 行 spec 偏紧；建议 ≥ 9 人周 |
| HIL 3 月交期 | RoboticsTomorrow 2026-01 [R2] | 26–52 weeks for FPGA / motor IC | 3 月（13 周）为乐观档下限 |
| Critical-path slack 13% | ⚫ Unknown — 船舶项目典型 rework rate 公开数据未找到 | — | 无对标基准；建议至少补"剔除已知工作后的真 slack" |
| Kongsberg K-MATE / Yara Birkeland 时间线 | DNV / Kongsberg 公开 [R7] | Yara Birkeland 多年开发 | 本项目 17 周完成 8 模块 + SIL 1000 场景 + HAZID 是激进档 |

---

## 8. 多船型泛化检查（Multi-vessel Generalization）

> 「本 angle 下被审查的设计/计划，是否引入了**只对 FCB 成立**的隐含假设？」

发现的 FCB-only 隐含假设：

1. **进度计划全文将"FCB"作为唯一目标船**（plan 第 13 行 / 519 行 / 实船试航全部仅指 FCB）。Backseat Driver 范式（CLAUDE.md §4 决策四）要求"决策核心零船型常量"，**但 plan 没有任何"拖船 / 渡船适配验收"交付物**。这不是 P0 schedule risk（Phase 4 之后的事），但是 **P1 strategic risk**——若 8/31 v1.1.3 回填把 FCB 校准值硬编码到 YAML，后续 RUN-002（拖船）将无法 hot-swap。
   - 证据：`gantt/...:13,519`、`docs/Design/HAZID/RUN-001-kickoff.md:194-198`（RUN-002/003 仅作 4 周预告）
   - 整改：要求 D3.5 v1.1.3 回填的 YAML schema 强制使用 Capability Manifest 命名空间（`fcb.*` 而非 root level），并在 D3.5 DoD 增加「另一虚构 vessel profile（如 tug-stub.yaml）能完成 SIL boot 不报错」条目。

2. **HAZID RUN-001 第 5–6 周 FCB 实船 100h 数据采集** 被假设为 v1.1.3 回填的核心证据。若该数据无法及时采集（见 P0-A2），多船型扩展（RUN-002 拖船）将面临"FCB 都没校准、拖船怎么校准"的连锁问题。
   - 证据：`docs/Design/HAZID/RUN-001-kickoff.md:93-95`
   - 整改：见 P0-A2。

3. **未发现 plan 中存在"if vessel == FCB"代码级潜入** —— 本评审仅审 plan 文档，未审代码。建议 B/F angle 在审 M5/M6 详细设计时重点扫这一点。

---

*A 角度评审完成 · 2026-05-07 · 后续主 agent 汇总阶段比对其他 angle 的 critical-path 发现*
