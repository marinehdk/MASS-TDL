# 架构评审请求：M5 Tactical Planner 模块功能划分完备性

你是一位资深的自主船舶（MASS）系统架构评审专家，精通 COLAV（碰撞避免）、MPC、COLREGs、Doer-Checker 安全架构、IEC 61508 / ISO 21448 SOTIF / DNV-CG-0264 / IMO MASS Code 认证。请对以下 M5 Tactical Planner 模块功能划分的**决策记录和方案包**进行严格的多轮架构评审，目标是发现架构漏洞、职责遗漏、边界冲突、契约不完整、认证风险等问题。

---

## 一、项目背景（评审上下文）

这是一个自主船舶的五层决策链项目：
```
L1 Mission[hrs~days] → L2 Voyage[min~hrs] → L3 Tactical[sec~min] → L4 Guidance[100ms~1s] → L5 Control[10ms~100ms]
```

**M5 是 L3 战术层的避碰航线规划核心**，职责一句话：**"在 ODD 约束下，把 M4 行为决策 + M6 COLREGs 规则语义 + M2 世界模型，转化为 L4 可执行的避碰轨迹，并承担求解失败时的有序降级。"**

模块交互：
- **M1 ODD Envelope Manager**：唯一 ODD authority，行为切换唯一来源（ADR-1）
- **M2 World Model**：本船状态 + 目标船数组 + CPA/TCPA/covariance（唯一权威世界视图）
- **M4 Behavior Arbiter**：行为仲裁（TRANSIT/COLREG_AVOID）+ heading/speed 框
- **M6 COLREGs Reasoner**：规则推理（role/phase/preferred_direction/past_clear）
- **M7 Safety Supervisor**：独立 Doer-Checker 中的 Checker（VETO + MRM 触发权）
- **M8 HMI**：透明性（SAT/ROC/Captain）
- **L4 Guidance**：LOS/PID 或 tracking-MPC，接受 M5 轨迹执行

## 二、已裁决的架构前提（本评审遵循，不质疑）

以下是在并行对话中已裁决的前提，本次评审**不质疑这些前提本身**（如发现矛盾可指出）：

1. **双层 MPC 架构**：Mid-MPC（NLP 长时域）+ BC-MPC（短时域紧急反应）
2. **horizon 1200s**：单一 NLP 端到端覆盖避让+保持+返航完整生命周期（基于 Eriksen 相对跟踪 t_b + Huber 损失范式）
3. **TailBuilder 已淘汰**：原几何尾段拼接模块被 NLP 端到端替代
4. **TimedTrajectory 主原语**：M5→L4 输出时间参数化连续轨迹（控制器无关）
5. **承诺前缀 180s**：L4 跟踪稳定性 + planner replan（60s）余量
6. **acados 求解器**（从 IPOPT 迁移）：1200s horizon 实时性使能器
7. **per-target per-step slack ξ∈R^{M·N}**：多船 CPA 约束独立性，混合 L1/L2 惩罚
8. **四状态交接机**：MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE
9. **M5 不持 safety authority**：只 emit SafetyConcernEvent 给 M7，不直接发 MRM（Doer-Checker）
10. **Doer-Checker 架构**：M5 是 Doer（复杂非凸 NLP），M7 是独立 Checker（IEC 61508 SIL2）

## 三、评审要求（重点发现什么）

请按以下维度严格评审，**优先发现架构漏洞而非细节问题**：

### A. 职责完备性（最重点）
- 11 个职责维度（DP-01~DP-11）是否真的覆盖了 M5 作为完整模块的所有必要职责？
- 有无遗漏的职责？（如：速度管理、多船死锁、TSS/zone、感知退化、通信故障、时钟同步、目标身份跳变等）
- 9 维度（DP-02~DP-11）的边界是否清晰无重叠？

### B. 架构断裂修复方案（DP-06/08/09）
- **DP-06** TailBuilder 拼接清理 + committed_route 演化为契约管理器：清理后 NLP 单段直生全 horizon 几何，这个方案有漏洞吗？
- **DP-08** MUST-9 两端断修复：M5 加 SafetyConcernEvent publish + M7 on_safety_concern 接入 MRM 仲裁 + 冻结返回路径——契约完整吗？有无竞态、丢失、时延超 TMR 风险？
- **DP-09** ADR-1 ODD 门控补全：M1 6状态FSM→M5 行为映射——映射会漏什么场景？长 horizon 1200s 跨 ODD 边界真的能靠 60s replan 防御吗？

### C. 边界冲突与所有权
- M5 候选健康状态机（候选选择）vs M7 MRM 触发权——边界清晰吗？
- M5 preflight（Doer 自检）vs L4 GNC feasibility authority——优先级裁决（GNC reject > M5 preflight > heartbeat）有漏洞吗？
- M5 约束编译器（M6 语义→数学）vs M6 语义推理——M5 会不会"越界推理语义"？

### D. 认证风险（IEC 61508 / ISO 21448 / DNV / MASS Code）
- Doer-Checker 独立性在 M5→M7 concern 链路修复后是否真的满足 SIL2？
- M5 长 horizon NMPC（学术界 Eriksen/Johansen 有争议）的认证路径是否站得住？
- 审计可观测性的字段 gap（NLP waypoint CMM 空、cost 恒 0、SAT 硬编）对认证审查的真实影响？

### E. 学术争议的处理（[R14]）
- 长 horizon 单一 NMPC 被 Eriksen/Johansen 普遍不鼓励（主张分层），本项目采用 1200s 端到端——这个张力在方案里只是"诚实记录"而非"深度论证"，是否够？

### F. 未闭环盲区的合理性
- 6 个未闭环盲区（BL-05/07/10/11/16/18）是否真的低优先级？还是其中有隐藏的高风险？

## 四、评审输出格式

请按以下格式输出：

```
## 评审结论（总体）
[接受 / 有条件接受 / 需重大修订] + 一句话理由

## 发现的架构漏洞（按严重度排序）
### 漏洞 1（严重/中/轻）
- 涉及：DP-XX 或某契约
- 问题：...
- 证据：...（引用具体证据或学术/标准）
- 建议：...

### 漏洞 2 ...

## 职责完备性专项
[遗漏的职责清单，或确认完备]

## 架构断裂修复专项（DP-06/08/09）
[每个修复方案的漏洞或确认]

## 认证风险专项
[认证路径的漏洞或确认]

## 建议补充评审的问题
[你认为还应深入讨论的问题]
```

请进行**多轮评审**（至少 2-3 轮自反思），每轮自问"还有什么漏洞？"，不要急于下"接受"结论。

---

# 以下为决策记录和方案包全文

# 决策记录：M5 模块功能划分完备性

## A. 决策点注册表（11 个职责维度）

| DP | 职责 | 架构级审查问题 |
|---|---|---|
| **DP-01** | 框架完备性 | 9 职责维度是否覆盖完整？速度管理归 DP-02 决策变量、多船归 DP-04+DP-05，不单列 |
| **DP-02** | 战术机动生成（Mid-MPC NLP 核心） | 仅产裸候选轨迹 MidMpcSolution，组装归 DP-06。4 内部子组件（预测模型/约束编译/目标函数/求解器）已在另对话裁决。[R14]长 horizon NMPC 学术争议须正视 |
| **DP-03** | 紧急反应（BC-MPC） | 短 horizon 分支枚举 argmax-min CPA，独立 emergency override 通道。连续级联交接归 [R8] P6 phase |
| **DP-04** | COLREGs 约束编译 | M6 语义→M5 数学翻译边界（constraint_compiler）。独立成熟子功能之一 |
| **DP-05** | 候选源仲裁与降级链 | M5 做候选健康状态机（选择生效候选），MRM 触发权归 M7。四状态机+门控（stale≤45s/航向>15°/CPA漂>20%） |
| **DP-06** | 航线所有权与输出组装 | **架构级重组**：TailBuilder 拼接须清理，committed_route 演化为契约管理器（只管 version/承诺前缀冻结/publish 门控）。承诺前缀冻结=几何冻结（NLP equality 行强制） |
| **DP-07** | GNC 可执行性预检 | Doer 自检（非 authority）。裁决优先级：GNC reject > M5 preflight > heartbeat。两套 preflight 须收敛 |
| **DP-08** | fail-safe 上报 | **架构级修复 MUST-9 两端断**：M5 加 SafetyConcernEvent publish + M7 接入仲裁 + 冻结返回路径 |
| **DP-09** | ODD 门控 | **架构级修复 ADR-1 违背**：M5 加 ODD 订阅 + M1 6状态→行为映射 + M5_RESUME 回切 |
| **DP-10** | 审计与可观测性 | ASDR/SAT/CMM 三接口。框架已落地，字段 gap 是工程债 |
| **DP-11** | 求解连续性与稳定性 | warm-start（prefix 重投影+suffix 冷启动）与承诺前缀冻结正交无冲突。三层反 chatter（warm-start+混合范数+符号翻转） |

## B. 裁决注册表（11 个 VR）

| VR | 裁决 | 理由 |
|---|---|---|
| VR-01 | DP-01：9 维度完备 | [R14]4内部子组件+[R16]5辅助职责 cross-check |
| VR-02 | DP-02：Mid-MPC 仅产裸候选轨迹，组装归 DP-06 | [R1][R5][R8][R14] |
| VR-03 | DP-03：双层分工+独立 override 已裁决，半活归 P6 | [R2][R7] |
| VR-04 | DP-04：M6→M5 翻译边界，最成熟子功能 | [R5][R8]VR-04 |
| VR-05 | DP-05：M5 做候选健康状态机，MRM 触发归 M7 | [R1][R7][R8]+[R16] |
| VR-06 | DP-06：**架构级重组**——TailBuilder 清理+committed_route→契约管理器+前缀冻结保留 | [R8]VR-02/07b+Step3 代码核实 |
| VR-07 | DP-07：Doer 自检，两套收敛+4 调用点改读订阅 | [R5][R9][R16] |
| VR-08 | DP-08：**架构级修复 MUST-9 两端断** | [R3][R16]+Step3+arch:934 纠正 |
| VR-09 | DP-09：**架构级修复 ADR-1 违背** | [R4][R16]+[R17]+Step3 |
| VR-10 | DP-10：审计必需，框架在字段 gap 工程债 | [R16] |
| VR-11 | DP-11：warm-start 与前缀冻结正交无冲突 | [R8]VR-TBD7+[R13]+Step3 |

## C. 弃用方案（ALT-01~12，关键项）

- **ALT-04** M5 自决 MRM：违反 Doer-Checker，planner 不持 safety authority
- **ALT-08/09** 保留 TailBuilder 拼接 / committed_route 仍拼接：与新架构端到端冲突/双重真相源
- **ALT-10** 仅 M5 侧加 concern 不改 M7：on_safety_concern 空操作不构成链路
- **ALT-11/12** M5 自判 ODD / 仅订阅不定义行为：违反外部 manager 原则

## D. 技术规约（TS-A01~09）

- **TS-A01** Mid-MPC 仅产裸候选轨迹，组装归 DP-06
- **TS-A02** COLREGs 语义归 M6，数学编译归 M5（只翻译不推理）
- **TS-A03** 候选选择归 M5，MRM 触发归 M7，M5 只 emit concern
- **TS-A04** SafetyConcernEvent：M5 publish /l3/safety/concern；M7 接入仲裁；failure_type enum（NLP_INFEASIBLE/CPA_BREACH/BC_ALSO_FAILED/SOLVER_TIMEOUT）+severity+context
- **TS-A05** M7/M1→M5 冻结返回路径（/l3/m5/freeze 或 M1 mode_cmd）
- **TS-A06** ODD 门控：M5 订阅 /l3/m1/odd_state；M1 6状态（In/Edge/Out/MrCPrep/MrCActive/Overridden）→M5 行为映射（NORMAL/DEGRADED/OUT/MRC）；M5_RESUME 回切
- **TS-A07** preflight=Doer 自检；裁决优先级 GNC reject > M5 preflight > heartbeat
- **TS-A08** TailBuilder 拼接清理；NLP 单段直生；committed_route→契约管理器
- **TS-A09** warm-start 与前缀冻结正交无冲突

## E. 代码核实关键发现（Step3）

1. **承诺前缀冻结已正确实装**（几何冻结+NLP equality 行 mid_mpc_solver.cpp:200-203）
2. **warm-start 与前缀冻结正交无冲突**（warm-start 是 prefix 重投影+suffix 冷启动，非 shift 整条）
3. **TailBuilder 拼接仍是活跃执行路径**（非残留死代码，mid_mpc_node.cpp:1773 optimized 分支每周期执行四段拼接）
4. **MUST-9 两端断**：M5 safety_concern_event 是内存字符串+spdlog，无 ROS2 publish；M5 无任何→M7 publisher；M7 已订阅 /l3/safety/concern 但 on_safety_concern 是空操作
5. **重要纠正（arch:934 过时）**：M7 HC-1~6 硬约束**已实现非死代码**（safety_supervisor_node.cpp:670-698）；on_avoidance_plan 派生**真实指标非全0**（:405-428 消费 nlp_solver_status/kkt/tail_gate_failed）；MRM 触发已实现（通过 monitor 路径，mrm_selector.cpp）
6. **M5 ODD 零订阅**（mid_mpc_node.cpp 构造函数无 sub_odd_）；M1 输出 6 状态 FSM 与 M5-spec §4.4 的 5 状态不对齐；M2/M3/M4/M6/M7/M8 全订阅，M5 是规划模块唯一缺失；M5_RESUME 零命中
7. **长 horizon ODD 前瞻**（NLM[R17]）：学术界承认长 horizon 内条件变化是已知简化，receding horizon replan（60s）是第一道防御，PSB-MPC 前瞻留 P7 演进

## F. 证据矩阵（关键项）

| ID | 来源 | 关键结论 |
|---|---|---|
| [R1] | NLM colav_algorithms (high) | COLAV 战术层应分解为独立 route manager + optimizer |
| [R2] | NLM colav_algorithms (high) | two-tier MPC（Eriksen/Brekke）双层分工 |
| [R3] | NLM colav_algorithms (high) | Doer-Checker：planner 不持 safety authority，emit concern |
| [R4] | NLM safety_verification (high) | ODD 外部门控（AOC/EVAA）；多 checker 冗余（1oo2/2oo3） |
| [R5] | NLM colav_algorithms (high) | 职责分解原则：语义隔离/预检独立/三职责独立/时空分层+Doer-Checker |
| [R6] | WebSearch Vagale 2021 (171+引) | COLAV 分层架构：全局→中层制导/避碰→低层控制 |
| [R7] | WebSearch Eriksen 2020 (IEEE T-IV) | 三层混合 COLAV：中层NLP+短期BC-MPC+连续级联 |
| [R8] | 本项目已裁决设计 | M5 MPC 核心 9 DP+11 VR（双层/Nomoto/acados/1200s/per-target slack/四状态机） |
| [R9] | 本项目已裁决设计 | L3→L4 契约 11 VR（TimedTrajectory/承诺前缀180s/capability/三层反馈） |
| [R14] | NLM colav_algorithms (high，纯外部) | 长 horizon NMPC 4 必备子组件；**学术界对单一长 horizon NMPC 普遍不鼓励（Eriksen/Johansen 主张分层）** |
| [R16] | NLM safety_verification (high，纯标准) | IEC61508/ISO21448/DNV/MASS Code：独立checker/降级链归外部supervisor/ODD外部门控/预检/solver审计**全是标准硬要求** |
| [R17] | NLM colav_algorithms (high，纯学术) | 长 horizon ODD 前瞻：receding horizon replan 防御+PSB-MPC 演进 |

## G. 未闭环盲区（6 项）

- **BL-05** constraint_compiler Rule14/15 硬编码迁移落地度（中，[R8]VR-04 已裁决方向）
- **BL-07** §9.12 heading/drift 触发死代码（中，已知 bug）
- **BL-10** 新架构预检范围（整条 1200s vs 承诺前缀 180s）（中）
- **BL-11** 两套 preflight 收敛方案（中）
- **BL-16** acados 后 solver status 审计字段适配（中，P1b 处理）
- **BL-18** 稳定性逻辑集中 vs 分散（中，代码组织）

## H. 3 个架构断裂的 DESIGN-IT-TWICE 对抗验证（Step5）

### DP-06 航线所有权重组
- **方案 A（采纳★★★★☆）**：NLP 单段直生 + committed_route 契约管理化
- 方案 B（弃用★★☆☆☆）：保留拼接但参数化——过渡期永远技术债+双重真相源

### DP-08 MUST-9 两端断修复
- **方案 A（采纳★★★★☆）**：完整两端修复（M5 publish + M7 仲裁 + 冻结返回）
- 方案 B（弃用★☆☆☆☆）：仅 M5 日志增强——不构成 fail-safe 链路

### DP-09 ADR-1 ODD 门控补全
- **方案 A（采纳★★★★☆）**：完整门控（订阅+FSM 映射+行为+回切）
- 方案 B（弃用★☆☆☆☆）：M5 内部保守降级——违反外部 manager 原则+视角不全

**三张卡片共同特点**：外部基座（M1 FSM/M7 HC/M7 MRM）已就绪，M5 是缺失的连接方。

---

## 五、评审的最后请求

请以"魔鬼代言人"视角评审，**不要客气**。这个模块要过 CCS i-Ship / IEC 61508 SIL2 / ISO 21448 SOTIF 认证，任何架构漏洞都可能导致认证失败或运行时安全事故。

特别关注：
1. 是否有**未被任何 DP 覆盖的职责**潜伏在 M5 与其他模块的边界？
2. 3 个架构断裂的修复方案（DP-06/08/09）是否**只是"补缺失连接"而忽略了新引入的风险**？
3. 长 horizon NMPC 学术争议（[R14]）在方案里只是"诚实记录"而非"深度论证"——**这对认证够吗**？
4. fail-safe 链路（DP-08）修复后，**端到端时延、消息丢失、竞态、M7 过载**等运行时风险是否被考虑？
5. TailBuilder 拼接清理（DP-06）后，**新架构的过渡期**（从四段拼接到 NLP 单段）如何保证不引入新的轨迹不连续？

请给出可操作的、具体的、有证据的评审意见。
