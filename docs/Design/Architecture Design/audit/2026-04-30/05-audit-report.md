# 审计报告

> v1.0 (`MASS_ADAS_L3_TDL_架构设计报告.md`) 全维度审计结论。
> Severity 模型见 spec §4。每条 finding 含 §5 八字段。
>
> 完成时间：2026-05-05
> 总 finding 数：40

---

## 摘要统计

| Severity | 数量 | 主要分布 |
|---|---|---|
| **P0 阻断** | 5 | 决策四 (3) + 决策一 (1) + ODD 框架 (1) |
| **P1 重要** | 18 | §15 接口契约 (5) + 决策一/二/三 (4) + M2/M3/M5/M6/M7/M8 各 1–3 |
| **P2 改进** | 15 | 跨章节分布 |
| **P3 备忘** | 2 | §16 孤立引用 + FCB 高速在线辨识 |
| **总计** | **40** | — |

**全表综合分**：2.35 / 3.0（详见 `08-audit-grid.md`）

**三态结论**：**B 档 — 结构性修订 + ADR**（详见 `_temp/decision-trace.md`；P0=5 满足 [3,5]，全表 2.35 满足 [1.5,2.5]）

---

## P0 Findings

### F-P0-D3-001 · M7 ARBITRATOR 直接注入安全轨迹（违反 Doer-Checker）

- **locator**：`MASS_ADAS_L3_TDL_架构设计报告.md` §11.2 图11-1 注释（行 778）
- **what**：M7 ARBITRATOR 有权"直接向 M5 注入安全轨迹"——Checker 承担了 Doer 职能（轨迹生成），违反 Doer-Checker 职责边界。
- **why**：顶层决策四规定 Checker 逻辑须比 Doer 简单 100×；v3.0 X-axis Deterministic Checker 仅有 VETO 权（"回退 nearest compliant"）。M7 若动态生成轨迹，则需包含规划逻辑，无法满足简单性约束。Boeing 777 Monitor / Airbus COM MON 均为投票/VETO，不生成替代轨迹；NASA Auto-GCAS 是"简化预定义拉起机动"而非完整 MPC。
- **evidence**：
  - internal: §11.2 图11-1 注释；CLAUDE.md §4 决策四
  - external: R4（Boeing 777 ~50:1 SLOC, Monitor 不生成轨迹；Simplex/Certified Control HA 控制器为预定义安全集）
- **confidence**：🟢 High
- **severity**：P0
- **recommended_fix**：约束注入行为
  - target: §11.2 + 新增 §11.6 MRC 命令集
  - proposed_text: 将"直接注入安全轨迹"改为"触发预定义 MRM 命令集（M7 VETO 权）"。MRM 命令集在 §11.6 中枚举（如减速至 4 kn 维持航向、停车抛锚序列），由 M5 执行而非 M7 动态计算。

### F-P0-D3-002 · M7 与 X-axis Deterministic Checker 协调机制缺失

- **locator**：§11 全章（行 738–809）+ §15.2 接口矩阵
- **what**：v1.0 §11 全章未提及 v3.0 X-axis Deterministic Checker。M7（L3 内部 Checker）与 X-axis Checker（系统级跨层 Checker）的协调机制、MRC 触发优先级、VETO 冲突处理均未定义。Phase 5 跨层对照实证：实际 Deterministic Checker doc §1.3 + §17 已有独立 VETO 协议（CheckerOutput），但完全无 M7 ↔ Checker 桥接。
- **why**：CLAUDE.md §2 明确 X-axis Checker 对 L3 有 VETO 权。若 M7 和 X-axis 同时触发 MRC，优先级未知；若 X-axis 已 VETO，M7 的仲裁器是否仍运行？两个 Checker 层级的设计缺失是架构完整性的 P0 缺口。
- **evidence**：
  - internal: CLAUDE.md §2（X-axis VETO 权）；§11 全章（无 X-axis 对照）
  - external: R4（Doer-Checker 独立性：两级 Checker 需显式协调）；`docs/Init From Zulip/MASS ADAS Deterministic Checker/Deterministic_Checker §1.3 + §17`
- **confidence**：🟢 High（跨层文档实证强化）
- **severity**：P0
- **recommended_fix**：补充架构描述
  - target: §11（新增 §11.7 与 X-axis Checker 的协作关系）
  - proposed_text: 明确分层：M7 = L3 内部 IEC 61508 + SOTIF Checker；X-axis = 系统级确定性 VETO 层（更高优先级）。M7 触发 MRC 请求送 M1；X-axis 触发时直接 VETO L3 输出，M7 不干预。M7 与 X-axis 通过独立总线通信，不共享状态。具体协调协议另起 ADR-001。

### F-P0-D1-006 · §2.2 三轴 ODD H 轴无权威来源

- **locator**：§2.2（行 95）
- **what**：§2.2 引用 [R8]（Rødseth 2022）支持"海事 ODD 必须包含人机责任分配维度（H 轴）"，暗示 v1.0 三轴（E×T×H）来自 Rødseth 2022。但 R8 实际只定义二轴：O = S_ext × S_int + FM（Function Mapping）。H 轴（Human Availability）无权威来源，是 v1.0 无标注的内部扩展。
- **why**：三轴 ODD 是 §3.2 + §5 M1 的基础；无来源的轴定义无法通过 CCS 审计（白盒可审计要求）。R3 研究三轮交叉确认：Rødseth 二轴（PMC 全文）、SAE/ISO 属性树（无轴）、DNV 三维是认证空间（非 ODD）。
- **evidence**：
  - internal: §2.2（"Rødseth...明确指出"[R8]）；§3.2 三轴定义（引用 §2.2 决策）
  - external: R3 ODD 工程实践（Rødseth 2022 = 二轴 PMC 全文确认）；R6（IMO MASS Code 目标型，无轴线）；R7（DNV AROS 三维认证空间，非 ODD 定义）
- **confidence**：🟢 High（三轮研究一致；R8 Phase 1 预扫已标 Phase 2 反证议题）
- **severity**：P0
- **recommended_fix**：修订引用 + 框架（详见 ADR-002 + ADR-003）
  - target: §2.2 决策理由段 + §3.2
  - proposed_text: 改为"Rødseth 等（2022）定义二轴 OE：O = S_ext × S_int + FM [R8]。人机任务分配（FM）通过 Function Mapping 而非独立 H 轴表达。本设计在 Rødseth 框架上扩展，将 FM 具体化为 D1–D4 四级自主度（见 §3.2），参考 IMO MASS Code D1–D4 定义 [R2]。"

### F-P0-D1-008 · §2.5 "Checker 简单 100×" 无文献依据

- **locator**：§2.5（行 126–128）
- **what**：§2.5 引用 [R12]（Jackson et al. 2021 Certified Control）支持"Checker 的验证逻辑须比 Doer 简单 100 倍以上"。但 R12 原文（Phase 1 预扫确认）无此量化值，仅称 Monitor 须"smaller and formally verifiable"。"100×"无文献依据。
- **why**：100× 是整个决策四、§11 设计、以及 CLAUDE.md §4 的核心约束来源。若无依据，CCS 审计时该约束无法被验证，可能被质疑为自设要求。R4 研究确认：工业实践 50:1~100:1，但这是设计目标，非规范强制。
- **evidence**：
  - internal: §2.5（"[R12]...100 倍以上"）
  - external: R4（Doer-Checker 落地：100× 无 IEC 61508/DO-178C 规范依据；工业实践约 50:1~100:1）；Phase 1 R12 预扫
- **confidence**：🟢 High（两轮独立确认）
- **severity**：P0
- **recommended_fix**：修改措辞（详见 ADR-001）
  - target: §2.5 + CLAUDE.md §4
  - proposed_text: "Checker 的代码规模须显著小于 Doer（工业实践目标：SLOC 比例 50:1~100:1，如 Boeing 777 Monitor ~50:1、Airbus FBW COM/MON >100:1）[R4]，以支持 DO-178C Level A / IEC 61508 SIL 2 形式化验证。此为设计目标，非规范强制值。"

### F-P0-D6-015 · §3.2 三轴公式 O = E×T×H 无来源

- **locator**：§3.2（行 140–149）+ §3.6 Conformance_Score
- **what**：§3.2 明文称"本设计采纳 Rødseth（2022）的 Operational Envelope 扩展定义"，随即给出三轴公式 O = E × T × H。但 Rødseth 2022 实际定义是二轴（O = S_ext × S_int + FM），无 H 轴。§3.6 Conformance_Score 公式（w_E + w_T + w_H）完全依赖三轴框架，若 H 轴来源无效，整个合规评分机制的引用链断裂。
- **why**：三轴 ODD 框架贯穿 §3–§5 全部设计，是 M1 调度逻辑的基础。无权威来源的公理无法通过 CCS 白盒可审计要求（CLAUDE.md §5 合规约束）。
- **evidence**：
  - internal: §3.2；§3.6 Conformance_Score 三轴权重
  - external: R3（Rødseth 2022 = 二轴 PMC 全文；H 轴不存在）；F-P0-D1-006（§2.2 同根问题）
- **confidence**：🟢 High（R3 PMC 全文核验）
- **severity**：P0
- **recommended_fix**：修订框架来源（详见 ADR-003）
  - target: §3.2 + §3.6
  - proposed_text: §3.2 改为基于 Rødseth 2022 二轴 + DNV-CG-0264 AROS 三维认证空间 + SAE J3259 属性树三层概念组合表述。§3.6 权重补注：H 轴权重为 FCB 设计值，须 HAZID 校准。

---

## P1 Findings

### 维度 D1 决策可证伪 / 引用真实（5 条）

#### F-P1-D1-007 · §2.2 IMO MASS Code 章节错引

- **locator**：§2.2（行 95）
- **what**：§2.2 称 "IMO MASS Code 草案（MSC 110，2025）在第 15 章明确要求...识别 OE 越界" [R2]。但 R6 研究确认：MSC 草案 Part 3 第 15 章是"维护与维修"（Maintenance），ODD/OE 要求在 Part 2 Chapter 1。
- **why**：错误章节引用会导致 CCS/DNV 审查员查不到原始依据，产生可信度损失。
- **evidence**：internal: §2.2；external: R6 IMO MASS Code 结构
- **confidence**：🟢 High
- **recommended_fix**：将 "在第 15 章明确要求" 改为 "在 Part 2 Chapter 1（运行环境/运行包络 OE）明确要求"

#### F-P1-D1-009 · §2.3 + §14.2 DNV-CG-0264 版本错 + 弃用方案缺 Kongsberg 5 模块

- **locator**：§2.3（行 116）+ §14.2（行 980）
- **what**：(a) "DMV-CG-0264（2018）" 应为 DNV-CG-0264（2025.01 现行版）；(b) §2.3 弃用方案对比表仅列候选 A（7 模块）+ 候选 B（4+1 模块），未与 v3.0 Kongsberg 5 模块对照——后者正是 v3.0 基线 L3 的工业实现方案。同一版本错在 §14.2 重复。
- **why**：(a) 版本错误损害可信度；(b) 缺失与最直接工业对标的对比。
- **evidence**：internal: §2.3 + §14.2；CLAUDE.md §2；external: R1 工业 COLAV (Kongsberg)；R7 (DNV-CG-0264 2025.01)
- **confidence**：🟢 High
- **recommended_fix**：(a) "DMV/DNV-CG-0264（2018）" 改为 "DNV-CG-0264（2025.01）"（§2.3 + §14.2 同步）；(b) §2.3 对比表增加候选 C "Kongsberg K-MATE / v3.0 基线 5 组件"

#### F-P1-D1-010 · §2.4 SAT 全展示工作负荷断言违反实证

- **locator**：§2.4（行 122）
- **what**：§2.4 断言 "SAT-1+2+3 全部可见时...工作负荷不增加" [R11]。但 R5 研究（USAARL 2026-02 + NTNU 2024）发现"透明度悖论"：全展示反而升高认知负荷。
- **why**：若 M8 按"全部可见"设计，实际会损害 ROC 接管绩效。
- **evidence**：internal: §2.4；external: R5
- **confidence**：🟢 High
- **recommended_fix**：改为 "SAT-3 是操作员最高优先级；SAT-2 按需触发（冲突检测/信心下降/操作员请求），不默认全时展示 [R5]。"

#### F-P1-D1-022 · §7 图7-1 上游 "战略层L2" 旧术语

- **locator**：§7 图7-1（行 521）
- **what**：M3 输入来源标注为 "战略层L2"。v3.0 中 L2 = 航路规划层（Voyage Planner）。
- **evidence**：internal: §7 图7-1；CLAUDE.md §2
- **confidence**：🟢 High
- **recommended_fix**：将 "战略层L2" 改为 "L2 航路规划层（Voyage Planner）"

### 维度 D3 Doer-Checker（1 条）

#### F-P1-D3-003 · M7 SOTIF 轨道 CPA 计算独立性未明

- **locator**：§11.3 PERF 行（行 788）
- **what**：M7 SOTIF PERF 监控使用"CPA 趋势恶化"触发条件。若 M7 复用 M2 CPA，则 M2 失效时 M7 同步失效（单点故障）；若 M7 独立计算，则与 M6 部分重叠。两种情况均未明确。
- **why**：决策四要求 Checker"不共享代码/库/数据结构"。
- **evidence**：internal: §11.3；CLAUDE.md §4；external: R4
- **confidence**：🟢 High
- **recommended_fix**：明确选择 (a) M7 独立最小化感知路径，或 (b) M7 读 M2 CPA 但 M2 失效时降级保守阈值

### 维度 D4 接口契约（6 条 — §15 五条 + M2 一条）

#### F-P1-D4-019 · §6 图6-1 "L1 感知层" 旧术语

- **locator**：§6 图6-1（行 447–448）
- **what**：图6-1 中 RADAR/CAMERA 输入标注为"(L1 感知层)"。v3.0 中 Multimodal Fusion 是独立感知子系统。
- **evidence**：internal: §6 图6-1；CLAUDE.md §2
- **confidence**：🟢 High
- **recommended_fix**：所有 "(L1 感知层)" 改为 "(Multimodal Fusion)"

#### F-P1-D4-031 · M2 ← Multimodal Fusion 频率/schema 不匹配

- **locator**：§15.2 + §6（行 1090–1108 + 422–508）
- **what**：v1.0 §15.2 期望 World_StateMsg @ 4 Hz 含目标 + 自身状态 + ENC 约束。实际：TrackedTargetArray @ 2 Hz（无 CPA/TCPA）+ FilteredOwnShipState @ 50 Hz（自身独立话题）+ EnvironmentState @ 0.2 Hz（含 ENC）。
- **why**：M2 内部聚合策略未明示；CCS 审计无法验证。
- **evidence**：internal: §15.2 + §6.3；external: docs/Init From Zulip/MASS ADAS Multimodal Fusion {Multi_Sensor_Fusion §2.2 + Navigation_Filter §2.2 + Situational_Awareness §2.2}
- **confidence**：🟢 High
- **recommended_fix**：§15.1 明确 World_StateMsg IDL（M2 聚合 2 Hz 输入插值至 4 Hz；含 cpa_m / tcpa_s 由 M2 计算；坐标系明示）；§6.4 增加"M2 内部数据聚合"小节

#### F-P1-D4-032 · M5 → L4 层级架构错位

- **locator**：§10 + §15.2（行 655–736 + 1100）
- **what**：v1.0 §10/§15 描述 M5 输出 (ψ, u, ROT) @ 10 Hz 直送下游。实际 L4（WOP_Module）订阅 L2 PlannedRoute + SpeedProfile，自身用 LOS+WOP 生成 (ψ, u, ROT) → L5。M5 输出与 L4 重叠/冲突。
- **why**：L5 收到两套指令；架构闭环性破坏。
- **evidence**：internal: §10 图 10-1；§15.2；F-P1-D5-012；external: docs/Init From Zulip/MASS ADAS L4/WOP_Module §2 + §7.2
- **confidence**：🟢 High
- **recommended_fix**：跨团队对齐——方案 A（保留 v1.0）：M5 输出 (ψ, u, ROT)；L4 在避让模式下旁路自身 LOS。**方案 B（建议）**：M5 输出 Avoidance_WP + speed_adj；L4 始终用 LOS+WOP

#### F-P1-D4-033 · §15 矩阵缺 ASDR 接口

- **locator**：§15（行 1026–1108）
- **what**：v1.0 §15.2 完全无 ASDR 接口。实际 ASDR 子系统订阅 L3 多模块（COLREGs/Risk Monitor/Avoidance/Checker/Target Tracker）@ 事件 + 2 Hz，schema AiDecisionRecord（ASDR §5.2）。
- **why**：CCS i-Ship 白盒可审计要求接口契约完整；ASDR 是决策追溯证据载体。
- **evidence**：internal: §15.2；external: docs/Init From Zulip/MASS ADAS ASDR §3.2 + §5.2 + §13
- **confidence**：🟢 High
- **recommended_fix**：§15.1 增加 ASDR_RecordMsg IDL；§15.2 矩阵增加 M2/M4/M6/M7 → ASDR @ 事件 + 2 Hz

#### F-P1-D4-034 · §15 矩阵缺 Reflex Arc 协议

- **locator**：§15（行 1026–1108）
- **what**：v1.0 §15.2 无 Reflex Arc 协议。实际 Hardware Override Arbiter §3.1 P2 通过 DDS 到 L5（"<500 ms"），但触发阈值未量化、感知输入源未明、L3 通知未定义。
- **why**：(1) Reflex 触发时 M5 仍输出，L5 收到两套指令；(2) M1 不知 Reflex 已激活；(3) 认证无法验证 <500 ms。
- **evidence**：internal: §15.2；CLAUDE.md §2；external: docs/Init From Zulip/MASS ADAS Deterministic Checker/Hardware_Override_Arbiter §3.1 + §14.1
- **confidence**：🟢 High
- **recommended_fix**：跨团队补 Reflex Arc spec（量化阈值/感知源/时序）；v1.0 §15.1 增 EmergencyCommand + ReflexActivationNotification IDL

#### F-P1-D4-035 · §15 矩阵缺 L3 → L2 反向 route_replan_request

- **locator**：§15.2 + §7
- **what**：v1.0 §15.2 矩阵无 L3 → L2 反向通道。早期裁决 §B-01 已识别。M3 重规划触发须能向 L2 发起重规划。
- **evidence**：internal: §15.2 + §7；external: docs/Doc From Claude/L3 实现框架裁决汇总.md §B-01
- **confidence**：🟢 High
- **recommended_fix**：§15.1 增加 RouteReplanRequest IDL（含 reason/deadline_s/context_summary/exclusion_zones）；§15.2 矩阵增加 M3 → L2 @ 事件

### 维度 D5 时间尺度（1 条）

#### F-P1-D5-012 · §4 图4-1 + §15 错写 M5 输出层级 L2

- **locator**：§4 图4-1（行 281）+ §15.2 + §10 图10-1
- **what**：图4-1 + §10 图10-1 + §15.2 矩阵 中 M5 输出指向"L2 执行层"。应为 L4 引导层。
- **why**：F-P1-D4-032 是其架构层延伸（不仅是文字错误）。
- **evidence**：internal: §4 图4-1 + §10 + §15.2；CLAUDE.md §2
- **confidence**：🟢 High
- **recommended_fix**：将 "L2 执行层" 改为 "L4 引导层（Guidance Layer）"（§4 + §10 + §15.2 同步）；与 F-P1-D4-032 协同重新定义 M5 输出 schema

### 维度 D6 降级路径（4 条）

#### F-P1-D6-004 · M7 自身失效行为未定义

- **locator**：§11 全章
- **what**：§11 未定义 M7 自身失效时行为：Fail-Silent？告警到 M8？强制 MRC？M7 心跳由谁监控？
- **why**：IEC 61508 SIL 2 要求所有安全功能失效模式显式分析；M7 失效时 Doer 链无 Checker 监督。
- **evidence**：internal: §11；external: R4；R7
- **confidence**：🟢 High
- **recommended_fix**：新增 §11.6：M7 心跳由 X-axis 或 M1 监控；Fail-Safe（触发保守 MRM），不允许 Fail-Silent；M7 失效后系统降级 D2

#### F-P1-D6-016 · §3.4 TDL 公式 0.6 系数无来源

- **locator**：§3.4（行 173）
- **what**：TDL 公式 `min(TCPA_min × 0.6, T_comm_ok, T_sys_health)` 中系数 0.6 无说明。
- **fix**：在 0.6 后加注 "[设计值：TCPA 60% 留 40% 余量供操作员接管，基于 Veitch 2024 估算；HAZID 校准]"

#### F-P1-D6-017 · §3.3 + §9.3 ODD 阈值无来源

- **locator**：§3.3（行 155–160）+ §9.3
- **what**：四个 ODD 子域阈值（CPA/TCPA）无引用来源。R2 研究文献 TCPA 范围 5–20 min，v1.0 12 min/4 min 在范围内但无来源。
- **fix**：每行加注 "[FCB 特定设计值，参考 Wang 2021 + TCPA 文献范围；HAZID 校正]"

### 维度 D7 合规映射（2 条）

#### F-P1-D7-005 · §11.4 SIL 2 SFF 要求缺失

- **locator**：§11.4（行 798）
- **what**：§11.4 SIL 2 仅列"单通道 + DC ≥ 90%"。IEC 61508-2 Table 3 还要求 SFF ≥ 60%（Type A）或 ≥ 70%（Type B）。
- **fix**：补 "SFF ≥ 60%（Type A）或 HFT=1 双通道（关键路径推荐 HFT=1）"

#### F-P1-D7-028 · §14.4 ODD-Spec 认证链断点

- **locator**：§14.4（行 1014–1016）
- **what**：§14.4 ODD-Spec 描述为"三轴包络定义，CCS 审查基础"，但 H 轴无来源（F-P0-D1-006 + F-P0-D6-015）。
- **fix**：改为"OE 框架定义（Rødseth 二轴 + IMO D1–D4 扩展，见 §3.2 修订版）。[前置依赖：F-P0-D1-006 + F-P0-D6-015 修订完成方可提交 AIP]"

### 维度 D9 方案最优（1 条）

#### F-P1-D9-024 · §10.5 [R21] Yasukawa & Sano 2024 疑似幻觉

- **locator**：§10.5（行 728–734）+ §10.6 + §16 [R21]
- **what**：§10.5 引用 [R21] FCB 波浪扰动修正。Phase 1 预扫已标"可能幻觉"。§16 [R21] 含 "（引用于 Patch-B §6.10 注记）"，明示来源于未集成补丁。
- **fix**：(a) 用 [R7] Yasukawa & Yoshimura 2015 4-DOF MMG 章节替代；或 (b) 标注 "[待验证：JMSE/JMSTech 2024 数据库确认]"

---

## P2 Findings

### F-P2-D1-011 · §2.2 TMR/TDL 60s 引用错位
- **locator**：§2.2；**fix**：在 [R8] 后补 [R4]（Veitch 2024 实证 60s 基线）

### F-P2-D5-013 · §4 Y-axis Reflex Arc 接口缺失
- **locator**：§4 全章；**fix**：§4 新增 §4.5 Reflex Arc 接口（M1 订阅激活信号）。后续被 F-P1-D4-034 升级为 P1 接口契约层

### F-P2-D6-018 · §3.5 EDGE/Conformance 阈值无来源
- **fix**：标注 "[初始值，待 HAZID 校正]"

### F-P2-D4-020 · §6.3.1 几何预分类主语模糊
- **fix**：注释 "# 以下从 own ship 视角"

### F-P2-D6-021 · M1 频率 0.1–1 Hz 突变延迟
- **fix**：补"事件驱动型 odd_state 发布"

### F-P2-D1-025 · §12.5 [R23] 作者归属错
- **what**：标注 "Hareide et al."，§16 [R23] = Veitch & Alsos (2022)
- **fix**：改为 "Veitch & Alsos (2022)"

### F-P2-D1-026 · §12 SAT 设计未实现自适应触发
- **what**：§12.2 全层全时展示，未按场景自适应触发；F-P1-D1-010 在 §2.4 识别根因，§12 未同步
- **fix**：§12.2 新增触发表（SAT-1 全时；SAT-2 按需；SAT-3 在 TDL 压缩时优先）

### F-P2-D4-027 · §13.5 / §9 M6 规则库插件矛盾
- **what**：§13.5 注 "长江内河船：仅 M6 规则库" 修改，但 §9 无规则库插件设计
- **fix**：(a) §9 补规则库接口 + Manifest 加 rules_lib_path；或 (b) §13.5 改为"架构级修改"

### F-P2-D1-030 · §1.2 设计边界表旧术语
- **fix**：(a) "L2 战略层" → "L2 航路规划层"；(b) "L1 感知层" → "Multimodal Fusion 子系统"

### F-P2-D3-036 · M7 ↔ Checker 信息流缺失
- **what**：M7 SOTIF 假设违反检测未涵盖 Checker 否决率
- **fix**：§11.3 增加 "Checker 否决率 > 20% → SOTIF 告警升级"；跨团队 Checker doc §17 增加 CheckerVetoNotification

### F-P2-D6-037 · Override 激活时 M5 冻结未定义
- **fix**：新增 §11.8 — 接管时 M5 冻结 MPC 求解（保持状态供回切）；ASDR 标 "overridden"

### F-P2-D4-038 · M3 漏 L1 VoyageTask 消费
- **fix**：§7 + §15.2 增加 L1 → M3 入口

### F-P2-D1-039 · Stage→Module 映射文档化缺失
- **fix**：v1.1 加附录 A 术语对照（Stage → Module）+ 附录 B（Checker-D/T → M7 双轨）

### F-P2-D9-040 · M4 多目标冲突消解 CSP 算法删除
- **fix**：§8 决策依据说明"v1.0 仅 IvP；CSP/图搜索 fallback 留 v1.1/spec part 2 评估"

### F-P2-D9-041 · §10 MPC 缺 TSS Rule 10 多边形约束
- **fix**：§10 增加 "EnvironmentState.in_tss = true 时 MPC 加 TSS lane polygon 约束"

---

## P3 Findings

### F-P3-D1-029 · §16 孤立引用 [R22] / [R13]
- **what**：[R22] Neurohr 2025 + [R13] Albus 1991 NIST RCS 在通读 §1–§15 全章时未见引用
- **fix**：详细说明见 99-followups.md

### F-P3-D9-042 · FCB 高速段无在线参数辨识
- **what**：早期 spec §3.3 建议 Vessel Dynamics Plugin 在 FCB 高速段（25–35 节）支持在线 GP/SI 参数辨识。v1.0 §13.4 仅静态预测
- **fix**：详细说明见 99-followups.md

---

## 跨章节关联映射

| Finding 群 | 同根问题 / 协同修订需求 |
|---|---|
| F-P0-D1-006 + F-P0-D6-015 + F-P1-D7-028 | 三轴 ODD H 轴根因；触及 §2.2 + §3.2 + §3.6 + §5 + §14.4；ADR-002 + ADR-003 |
| F-P0-D3-001 + F-P0-D3-002 + F-P0-D1-008 + F-P1-D3-003 + F-P1-D6-004 + F-P1-D7-005 + F-P2-D3-036 | Doer-Checker 双轨重构；§11 全章重写 + §2.5 措辞修订；ADR-001 |
| F-P1-D5-012 + F-P1-D4-032 | M5 → L4 层级架构错位；同时影响 §4 图 4-1 + §10 图 10-1 + §15.2 矩阵 |
| F-P1-D4-031 + F-P1-D4-033 + F-P1-D4-034 + F-P1-D4-035 + F-P2-D5-013 | §15 接口契约总表完整性；M2 ← Fusion + ASDR + Reflex + L3→L2 反向 |
| F-P1-D4-019 + F-P1-D1-022 + F-P2-D1-030 | "L1 感知层" / "L2 战略层" 旧术语遗留；§1.2 + §6 + §7 同步修复 |
| F-P1-D6-016 + F-P1-D6-017 + F-P2-D6-018 | ODD/TDL 阈值无来源（§3.x + §9.3）；HAZID 校准任务集中 |
| F-P1-D1-009 + F-P2-D1-039 | 弃用方案分析 + Stage→Module 映射缺失 |

---

## 输出与决策路径

- **决策树落点**：B 档（结构性修订 + ADR） — 详见 `_temp/decision-trace.md`
- **B 档输出物**：
  - `08-audit-grid.md` 14×9 评分矩阵
  - `08a-patch-list.md` 35 条 P1/P2/P3 finding 字段级 diff
  - `08c-adr-deltas.md` 3 条 ADR 增量（ADR-001 / 002 / 003）
- **followup 项**：`99-followups.md`
- **执行摘要**：`00-executive-summary.md`
