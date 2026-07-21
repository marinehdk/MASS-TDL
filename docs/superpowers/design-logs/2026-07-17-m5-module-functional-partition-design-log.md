# 设计日志: M5 模块功能划分完备性

> **模式**: 重构        **创建**: 2026-07-17
> **关联 spec**: 待 Step6 产出方案包
> **状态**: Step1 进行中
> **范围**: M5 作为 TDL 系统核心模块的**功能职责划分**——自顶向下从职责出发推导合理子功能边界，评判现有划分是否完备且工程合理。**不含** MPC 内部算法（归 `2026-07-16-m5-mpc-colav-design-log.md` DP-01~09）也不含 L3→L4 接口细节（归 `2026-07-17-l3-l4-gnc-contract-design-log.md` 11 VR）。
> **关联已裁决设计**: 本对话遵循两个并行对话的已裁决前提（TailBuilder 淘汰 / horizon 1200s / 双层 MPC / TimedTrajectory 输出 / 承诺前缀 180s / acados 求解器 / 四状态交接机）。推翻这些需回炉对应决策树。

---

## 0. 决策树状态(权威索引 · 可变快照)

### 0.1 决策点注册表 [DP]

> **组织原则（自顶向下）**: 从 M5 的核心职责（"在 ODD 约束下，把 M4 行为+M6 规则+M2 世界模型转化为 L4 可执行避碰轨迹，并承担有序降级"）出发，按 NLM[R1][R5]/Vagale[R6]/Eriksen[R7] 权威分解原则推导。每个 DP 审查一个职责维度的**完备性**：该职责是否必须、边界是否合理、所有权归属、契约完整性、是否有遗漏或重叠。
> 本对话**不重复** MPC 内部算法（DP-01~09，已裁决[R8]）和 L3→L4 接口细节（11 VR，已裁决[R9]），而是审查**这些已裁决部分如何组织成完整自洽的 M5 模块**。

| ID | 描述 | 类型 | 父/分解 | 状态 | 详见 |
|----|------|------|---------|------|------|
| **DP-01** | (框架) M5 职责分解的完备性——M5 应覆盖哪些一级职责才算完备？现有 F1-F12 清单是否遗漏？**完备性 cross-check**：[R14] MPC 内部 4 子组件（多目标代价/本船预测/障碍预测/求解器）验证 DP-02 内部；[R16] 5 个标准硬要求辅助职责（独立checker/降级链/ODD门控/预检/solver审计）验证 DP-05~DP-10 | 架构 | TD-01 | 未决 | Step1 |
| **DP-02** | 战术机动生成职责（Mid-MPC NLP 核心）——边界：仅产候选轨迹+terminal state，不含航线所有权/输出组装？与降级链/预检/输出的接口面。**grilling 素材**：[R14] 单一长horizon(1200s)NMPC被学术界(Eriksen/Johansen)普遍不鼓励,主张分层——与已裁决VR-06b的张力须正视 | 架构 | TD-01 | 未决 | Step1 |
| **DP-03** | 紧急反应职责（BC-MPC）——边界：短horizon分支枚举，独立emergency override通道；与Mid的连续级联交接所有权 | 架构 | TD-01 | 未决 | Step1 |
| **DP-04** | COLREGs 约束编译职责——M6语义→数学约束的翻译边界归属M5（非M6）；编译器是独立子功能还是嵌入optimizer | 架构 | TD-01 | 未决 | Step1 |
| **DP-05** | 候选源仲裁与降级链职责——多候选（NLP/BC/几何降级/keep-last）的选择+四状态机+门控；独立子功能vs分散主循环。**关键grilling**：[R16] 降级链所有权归外部supervisor(AOC/M7)非planner——M5内部候选选择(合理) vs 降级最终决策权(何时FINAL_DEGRADE/MRM)归谁?边界须厘清 | 架构 | TD-01 | 未决 | Step1 |
| **DP-06** | 航线所有权与输出组装职责——承诺前缀+version+material-change+TimedTrajectory发布；TailBuilder淘汰后谁拥有"完整航线" | 架构 | TD-01 | 未决 | Step1 |
| **DP-07** | GNC 可执行性预检职责——Doer自检(非authority)定位；与L4 GNC feasibility的边界；两套preflight收敛 | 架构 | TD-01 | 未决 | Step1 |
| **DP-08** | fail-safe 上报职责——concern event→M7(不直接MRM)；MUST-9断裂修复；MRM时延<TMR | 接口 | TD-01 | 未决 | Step1 |
| **DP-09** | ODD 门控职责——M1外部门控(M5冻结vs降级参数)；当前缺失如何补；ADR-1 | 接口 | TD-01 | 未决 | Step1 |
| **DP-10** | 审计与可观测性职责——ASDR/SAT/CMM三接口/forecast；每个决策可追溯；认证前置 | 接口 | TD-01 | 未决 | Step1 |
| **DP-11** | 求解连续性与稳定性职责——warm-start/同伦管理/反chattering；是独立子功能还是optimizer内部 | 架构 | TD-01 | 未决 | Step1 |

### 0.2 技术分解注册表 [TD]

| ID | 架构 | 分解职责(→DP) | 触发步骤 |
|----|------|---------------|----------|
| TD-01 | M5 模块功能划分(自顶向下职责分解) | 机动生成(DP-02), 紧急反应(DP-03), 约束编译(DP-04), 候选仲裁降级(DP-05), 航线所有权输出(DP-06), 可执行性预检(DP-07), fail-safe(DP-08), ODD门控(DP-09), 审计可观测(DP-10), 稳定性(DP-11) | Step1 |

> **注**: TD-01 的子决策点都是**架构/接口型**（答案形态是"职责归属/边界划分"而非"采用某算法"），不触发机制C 的算法技术分解。MPC 内部算法已在[R8]的 TD-01(mpc-colav) 裁决。

### 0.3 盲区注册表 [BL]

| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-01 | "速度管理/变速策略"是否应是独立职责维度？ | DP-01 | 中 | 已闭环(用户确认归入DP-02决策变量u+DP-04速度bounds,不单列) |
| BL-02 | "多船/复杂场景优先级与死锁处理"是否应是独立职责维度？ | DP-01 | 中 | 已闭环(用户确认归入DP-04 per-target slack+DP-05仲裁,不单列) |
| BL-03 | Mid-MPC产出的候选轨迹与L4要的完整TimedTrajectory之间的组装责任归DP-02还是DP-06? | DP-02/DP-06 | 高 | 已闭环(用户确认:DP-02仅产裸候选轨迹MidMpcSolution,组装成TimedTrajectory归DP-06) |
| BL-04 | 新架构下(TailBuilder淘汰/horizon1200s)BC-MPC接管时Mid行为?HANDOVER_NEUTRAL交还条件如何定义? | DP-03 | 高 | 归[R8]P6 phase记录不裁决(已裁决的归对应决策树) |
| BL-05 | constraint_compiler里Rule14/15是否已完全从硬编码5°/10°迁移到M6几何驱动? | DP-04 | 中 | Step3代码核实([R8]VR-04已裁决方向,只核实落地度) |
| BL-06 | M5四状态机候选健康判定与M7外部MRM决策权之间的契约边界——M5 emit什么信号/M7如何消费/MRM触发时延<TMR60s? | DP-05/DP-08 | 高 | 已闭环(Step3代码核实,与BL-12合并):M5→M7 concern链缺失(M5无publish,M7 on_safety_concern空操作);M7→M5冻结返回也缺失(无/l3/m5/freeze)。M7 MRM触发已实现但走monitor路径非concern。两端都断。修复须M5加publish+M7 on_safety_concern接入仲裁+M7/M1→M5冻结返回路径 |
| BL-07 | §9.12 heading/drift触发死代码(硬编0.0)——target机动检测放M5还是M2?persisted history在哪维护? | DP-05 | 中 | 已知bug,影响降级链完整性 |
| BL-08 | 新架构下承诺前缀180s的冻结语义——NLP每60s重解改变整条轨迹,"冻结前缀"是冻结几何(NLP须约束前180s不变)还是冻结publish(几何可变但不发新版)? | DP-06 | 高 | 已闭环(Step3代码核实):冻结**几何**非publish语义。committed_route.cpp:168-173,328-340 preserves_committed_prefix逐点比对容差1cm;mid_mpc_solver.cpp:200-203 NLP equality行(lbg=ubg=0)强制前K步=冻结WGS84重投影值 |
| BL-09 | committed_route从"拼接器"→"契约管理器"的演化——现有代码是否已清理TailBuilder时代拼接逻辑?route_hash新架构下如何稳定? | DP-06 | 高 | 已闭环(Step3代码核实):**TailBuilder拼接仍是活跃执行路径非残留**(mid_mpc_node.cpp:1773,1638 optimized分支每周期执行L2-prefix+NLP-body+TailBuilder-hold+L2-suffix四段拼接)。hash比较整条轨迹(committed_route.cpp:298-312),prefix rolling prune每次revise(committed_route.cpp:199-216)。新架构落地须删append_tail_waypoints_等改NLP单段直生 |
| BL-10 | 新架构下预检范围——NLP端到端1200s轨迹,preflight检整条还是只检承诺前缀180s?整条检远段不精确误拒,只检前缀漏远段不可行 | DP-07 | 中 | Step3(新架构浮现问题) |
| BL-11 | 两套preflight(gnc_preflight通用未接线+gnc_avoidance_preflight实用)收敛方案——合并/分工/删除一套? | DP-07 | 中 | Step3(代码整洁度) |
| BL-12 | safety_concern_event具体msg契约——字段(failure_type enum+severity+context)+M7订阅topic+M7消费后映射MRM类型+端到端时延<TMR60s?(与BL-06合并) | DP-08/DP-05 | 高 | 已闭环(Step3代码核实):M5的safety_concern_event是**内存字符串**(committed_route.hpp:84)+spdlog::critical(mid_mpc_solver.cpp:266,330,354,357),无ROS2 publish。M5无任何→M7 publisher(mid_mpc_node.cpp:500-509)。M7**已订阅**/l3/safety/concern(safety_supervisor_node.cpp:308-313)但on_safety_concern是**空操作**(只log不喂MRM,:507-516)。**真正发布concern的是M1非M5**(odd_envelope_manager_node.cpp:367-368)。修复=M5加SafetyConcernEvent publish到/l3/safety/concern + M7 on_safety_concern接入MRM仲裁 |
| BL-13 | M7侧是否已有concern event入口?还是须新建?决定修复在M5侧加publish还是M7侧加subscribe或两端都改 | DP-08 | 高 | 已闭环(Step3代码核实):两端都改。M5侧加publish;M7侧on_safety_concern从空操作改为喂MRM仲裁。**重要纠正(arch:934过时)**:M7 HC-1~6硬约束**已实现非死代码**(safety_supervisor_node.cpp:670-698);on_avoidance_plan派生**真实指标非全0**(:405-428消费nlp_solver_status/kkt/tail_gate_failed);MRM触发已实现(通过monitor非concern,mrm_selector.cpp)。gap:M5→M7 concern缺失+M7→M5冻结返回缺失+M1→M5冻结也缺 |
| BL-14 | M5在不同ODD状态下的具体行为契约——NORMAL(全速)/DEGRADED(降参数)/OUT-of-ODD(冻结OVERRIDDEN?)/CRITICAL(MRM?)。M5-spec§4.4/§6定义设计目标但未实装 | DP-09 | 高 | Step3代码核实:M5零订阅ODD(mid_mpc_node.cpp:453-498无sub_odd_)。M1输出6状态FSM(In/Edge/Out/MrCPrep/MrCActive/Overridden,odd_state_machine.hpp:91-108)**与M5-spec§4.4的5状态不对齐**,须定义M1→M5映射(参考M6 zone→OddDomain colregs_reasoner_node.cpp:485-497)。其他模块M2/M3/M4/M6/M7/M8全订阅,M5是规划模块唯一缺失。M5_RESUME零命中回切也断 |
| BL-15 | 长 horizon 1200s是否需ODD前瞻——预测未来可能跨越ODD边界(天气/能见度),M5是否NLP内加ODD约束?还是只响应M1当前状态? | DP-09 | 中 | 已闭环(Step3 NLM[R17]):学术界承认长horizon内条件变化是已知简化(Eriksen2020)。共识:动态安全约束应嵌入MPC(Bø&Johansen2014/Blindheim2020)非纯响应。实践:receding horizon replan(60s)是第一道防御+PSB-MPC前瞻。本项目先用replan(60s),PSB-MPC式前瞻留[R8]P7(VR-09 OU+intent)演进 |
| BL-16 | acados RTI后solver status审计字段适配——RTI收敛语义(每次QP迭代质量)vs IPOPT(全局收敛/KKT),ASDR/M7消费的solver status如何定义? | DP-10 | 中 | Step3([R8]P1b实施时处理) |
| BL-17 | warm-start shift-init(反chatter首要,保持同伦类自然shift整条轨迹)与承诺前缀冻结(前180s不动)的冲突协调——两者如何共存? | DP-11/DP-06 | 高 | 已闭环(Step3代码核实):**冲突不存在**。warm-start实际是prefix重投影+suffix冷启动(mid_mpc_solver.cpp:145-166),非shift整条轨迹(legacy shift形态保留但solve()不再用,:60-73)。前缀冻结由equality行强制,warm-start只影响收敛速度不改解。两者正交无冲突 |
| BL-14 | M5在不同ODD状态下的具体行为契约——NORMAL(全速)/DEGRADED(降参数)/OUT-of-ODD(冻结OVERRIDDEN?)/CRITICAL(MRM?)。M5-spec§4.4/§6定义设计目标但未实装 | DP-09 | 高 | Step3代码核实:M5零订阅ODD(mid_mpc_node.cpp:453-498无sub_odd_)。M1输出6状态FSM(In/Edge/Out/MrCPrep/MrCActive/Overridden,odd_state_machine.hpp:91-108)**与M5-spec§4.4的5状态不对齐**,须定义M1→M5映射(参考M6 zone→OddDomain colregs_reasoner_node.cpp:485-497)。其他模块M2/M3/M4/M6/M7/M8全订阅,M5是规划模块唯一缺失。M5_RESUME零命中回切也断 |
| BL-18 | 稳定性逻辑是否应集中为独立子功能(vs分散在solver/formulation/committed_route) | DP-11 | 中 | Step3(代码组织) |

### 0.4 证据矩阵 [EV]

| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | NLM(2026-07-16,high) | colav_algorithms: COLAV 战术层应分解为独立 route manager(航线生命周期)+optimizer(轨迹生成);route manager 持 stale_route_max_age 超时门控 | 高 | 高 | 高 | DP 全 |
| [R2] | NLM(2026-07-16,high) | colav_algorithms: two-tier MPC(NTNU Eriksen/Brekke)——mid-NLP 长时域 COLREGs 合规 + BC-MPC 10Hz 紧急反应(Rule17 stand-on 机动);双层分工 | 高 | 高 | 高 | DP(双层) |
| [R3] | NLM(2026-07-16,high) | colav_algorithms: Doer-Checker——planner 不持 safety authority,不可直接发 MRM,须 emit concern event 给独立 supervisor | 高 | 高 | 高 | DP(fail-safe) |
| [R4] | NLM(2026-07-16,high) | safety_verification: Simplex/watchdog fail-safe;ODD 由高层外部 manager(AOC/EVAA)门控非 planner 自判;多独立 checker(1oo2/2oo3 voting) | 高 | 高 | 高 | DP(ODD/checker) |
| [R5] | NLM(2026-07-16,high) | colav_algorithms: 职责分解原则——(1)COLREGs语义隔离(M6)vs数学约束嵌入optimizer,constraint_compiler是翻译边界;(2)GNC预检独立gatekeeper≠optimizer≠route lifecycle;(3)机动生成/尾段延伸/候选仲裁三独立职责;(4)时空分层+Doer-Checker+感知vs决策三原则 | 高 | 高 | 高 | DP(框架) |
| [R6] | WebSearch(2026-07-17) | Vagale et al.2021(JMST 26:1292-1306,171+引)"COLAV review Part I":分层架构=全局路径规划→中层制导/避碰(mid-level guidance+COLAV)→低层控制。M5 对应中层 | 高 | 高 | 高 | DP(框架/分层) |
| [R7] | WebSearch(2026-07-17) | Eriksen&Breivik 2020(IEEE T-IV)三层混合 COLAV:中层NLP+短期BC-MPC+交接仲裁;连续级联(continuous cascading)显式机制非分散主循环 | 高 | 高 | 高 | DP(双层/仲裁) |
| [R8] | DOCUMENTED_INTENT | `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`:M5 MPC 核心 9 DP+11 VR,已交付 brainstorming。双层MPC/Nomoto/acados/1200s/per-target slack/四状态机已裁决 | — | 高(本项目权威) | — | DP(前提) |
| [R9] | DOCUMENTED_INTENT | `docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md`:L3→L4 契约 11 VR。TimedTrajectory/承诺前缀180s/capability单一真相/三层反馈/override/控制器无关已裁决 | — | 高(本项目权威) | — | DP(前提) |
| [R10] | DOCUMENTED_INTENT | 架构报告 §10.2:M5 职责="双层MPC将M4行为+M6规则转化为可执行轨迹";§10.4 Mid-MPC;§10.5 BC-MPC;§10.8 HW Override | — | 高(本项目权威) | — | DP(职责定义) |
| [R11] | DOCUMENTED_INTENT | committed-route-design-v2(2026-06-30,743行):CommittedAvoidanceRoute lifecycle/语义分工契约/TailBuilder(已被VR-02淘汰)/preflight/keep-last门控 | — | 高(部分已被跨树修订) | — | DP(航线管理) |
| [R12] | PROJECT_FACT | M5 源码 ~22800 行:mid_mpc/bc_mpc/committed_route/tail_builder(待删)/shared(constraint_compiler/cpa_calculator/trajectory_propagator/capability_manifest)/gnc_preflight(双套) | — | 仅本项目 | — | DP(现状) |
| [R13] | PROJECT_FACT | 调研subagent(2026-07-17,WebSearch+项目文档):长horizon NLP端到端范式下,TailBuilder/终端集可删;预测模型/约束编译/slack/转移代价/仲裁级联/preflight/warm-start/承诺前缀必须保留。业界标准 vs 本项目设计选择已区分 | 高 | 高(NLM不可用,WebSearch+文档双源) | 高 | DP(完备性) |

### 0.5 场景注册表 [SC]

| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| (Step1/2 填充) | | | |

### 0.6 裁决注册表 [VR]

| ID | 裁决对象 | 结论 | 采纳/弃用 | 理由 | 时间 |
|----|----------|------|-----------|------|------|
| VR-01 | DP-01框架完备性 | 9维度完备,速度管理归DP-02/多船归DP-04+05,不单列 | 采纳 | [R14]+[R16]cross-check;用户确认 | Step4 |
| VR-02 | DP-02机动生成 | Mid-MPC仅产裸候选轨迹,组装归DP-06;4内部子组件已[R8]裁决 | 采纳 | [R1][R5][R8][R14];BL-03用户确认 | Step4 |
| VR-03 | DP-03紧急反应 | 双层分工+独立override通道已[R8][R9]裁决;半活状态归P6 | 采纳 | [R2][R7];BL-04归P6 | Step4 |
| VR-04 | DP-04约束编译 | M6语义→M5数学翻译边界,最成熟子功能之一;BL-05待核实 | 采纳 | [R5][R8]VR-04 | Step4 |
| VR-05 | DP-05候选仲裁+降级 | M5做候选健康状态机,MRM触发权归M7;heading/drift死代码须修 | 采纳 | [R1][R7][R8]+[R16];BL-06闭环 | Step4 |
| VR-06 | DP-06航线所有权+输出 | **架构级重组**:TailBuilder拼接须清理,committed_route演化为契约管理器,前缀冻结保留 | 采纳 | [R8]VR-02/07b+Step3代码核实;BL-08/09/17闭环 | Step4 |
| VR-07 | DP-07可执行性预检 | Doer自检(非authority);两套收敛+4调用点改读订阅+预检范围待定 | 采纳 | [R5][R9][R16];BL-10/11 | Step4 |
| VR-08 | DP-08 fail-safe上报 | **架构级修复MUST-9两端断**:M5加concern publish+M7接入仲裁+冻结返回路径 | 采纳 | [R3][R16]+Step3两端断+arch:934纠正;BL-06/12/13闭环 | Step4 |
| VR-09 | DP-09 ODD门控 | **架构级修复ADR-1违背**:M5加ODD订阅+M1 6状态→行为映射+M5_RESUME回切 | 采纳 | [R4][R16]+[R17]+Step3零订阅;BL-14/15闭环 | Step4 |
| VR-10 | DP-10审计可观测 | 审计是独立必需职责;框架在,字段gap是工程债;acados适配归P1b | 采纳 | [R16]+现状框架;BL-16 | Step4 |
| VR-11 | DP-11求解稳定性 | warm-start与前缀冻结正交无冲突;三层反chatter已[R8]裁决 | 采纳 | [R8]VR-TBD7+[R13]+BL-17闭环 | Step4 |

### 0.7 备选/弃用方案 [ALT]

| ID | 方案 | 弃用理由 | 对比于 |
|----|------|----------|--------|
| ALT-01 | Mid-MPC直接输出完整TimedTrajectory(含组装) | 违反职责分离,DP-02/06边界模糊 | VR-02(DP-02) |
| ALT-02 | M6直接输出优化器约束 | 换求解器改M6违反关注点分离 | VR-04(DP-04) |
| ALT-03 | 长horizon覆盖BC紧急场景可省BC | [R2]时间尺度不同+Rule17 stand-on需独立层 | VR-03(DP-03) |
| ALT-04 | M5自决MRM | 违反[R3][R16]Doer-Checker,planner不持safety authority | VR-05(DP-05)/VR-08(DP-08) |
| ALT-05 | 预检归L4 GNC | [R5]Doer自检快速失败避免占用L4;L4仍有最终accept/reject权 | VR-07(DP-07) |
| ALT-06 | 审计可选 | [R16]SOTIF/MASS认证硬要求 | VR-10(DP-10) |
| ALT-07 | warm-start shift整条轨迹 | 代码核实发现实际不是shift,是prefix重投影+suffix冷启动 | VR-11(DP-11) |
| ALT-08 | 保留TailBuilder拼接(新架构下) | 违反[R8]VR-02已裁决淘汰,与新架构端到端冲突/双重真相源 | VR-06(DP-06) |
| ALT-09 | committed_route仍做几何拼接 | 与新NLP单段端到端双重真相源 | VR-06(DP-06) |
| ALT-10 | 仅M5侧加concern publish不改M7 | on_safety_concern空操作,不接入MRM仲裁则无效 | VR-08(DP-08) |
| ALT-11 | M5自判ODD | 违反[R4][R16]外部manager原则,M5视角不全 | VR-09(DP-09) |
| ALT-12 | 仅订阅ODD不定义行为 | 须M1 6状态→M5行为映射才有效 | VR-09(DP-09) |

### 0.8 技术规约注册表 [TS]

| ID | 类别 | 规约内容 | 来源 | 关联DP | 与现状差异 |
|----|------|----------|------|--------|-----------|
| TS-A01 | 职责边界 | Mid-MPC仅产裸候选轨迹,组装归DP-06 | DESIGN_DECISION[VR-02] | DP-02/06 | 现状混在2196行须拆解 |
| TS-A02 | 所有权 | COLREGs语义归M6,数学编译归M5(只翻译不推理) | [R5]+[R8]VR-04 | DP-04 | 已基本落地 |
| TS-A03 | 所有权 | 候选选择归M5,MRM触发归M7,M5只emit concern | [R3][R16]+[R8]VR-08 | DP-05/08 | M5无publish须加 |
| TS-A04 | 契约 | SafetyConcernEvent: M5 publish /l3/safety/concern; M7接入仲裁; failure_type enum | DESIGN_DECISION[VR-08] | DP-08 | 两端都缺须建 |
| TS-A05 | 契约 | M7/M1→M5冻结返回路径(/l3/m5/freeze或M1 mode_cmd) | DESIGN_DECISION[VR-08/09] | DP-08/09 | 完全缺失 |
| TS-A06 | 契约 | ODD门控: M5订阅/l3/m1/odd_state; M1 6状态→M5行为映射; M5_RESUME回切 | DESIGN_DECISION[VR-09]+[R4][R16] | DP-09 | 零订阅+FSM不对齐 |
| TS-A07 | 职责边界 | preflight=Doer自检(非authority); GNC reject>M5 preflight>heartbeat | [R5][R9][R16] | DP-07 | 方向已定4调用点须改 |
| TS-A08 | 职责边界 | TailBuilder拼接须清理; NLP单段直生; committed_route→契约管理器 | [R8]VR-02/07b+[VR-06] | DP-06 | 拼接仍活跃须清理 |
| TS-A09 | 所有权 | warm-start与前缀冻结正交无冲突 | Step3代码核实 | DP-11 | 已正确实装 |

---

## 参考文献

- [R1] NLM colav_algorithms (2026-07-16, high): COLAV 战术层应分解为独立 route manager(航线生命周期)+optimizer(轨迹生成);route manager 持 stale_route_max_age 超时门控
- [R2] NLM colav_algorithms (2026-07-16, high): two-tier MPC(NTNU Eriksen/Brekke colav-simulator)——mid-NLP 长时域 COLREGs 合规 + BC-MPC 10Hz 紧急反应(Rule17 stand-on 机动)
- [R3] NLM colav_algorithms (2026-07-16, high): Doer-Checker——planner 不持 safety authority,不可直接发 MRM,须 emit concern event 给独立 supervisor
- [R4] NLM safety_verification (2026-07-16, high): Simplex/watchdog fail-safe;ODD 由高层外部 manager(AOC/EVAA)门控非 planner 自判;多独立 checker(1oo2/2oo3 voting);IEC61508 SIL2/ISO21448 SOTIF/IMO MASS Code
- [R5] NLM colav_algorithms (2026-07-16, high): 职责分解原则——(1)COLREGs语义隔离(M6)vs数学约束嵌入optimizer,constraint_compiler是翻译边界;(2)GNC预检独立gatekeeper≠optimizer≠route lifecycle;(3)机动生成/尾段延伸/候选仲裁三独立职责;(4)时空分层+Doer-Checker+感知vs决策三原则
- [R6] WebSearch (2026-07-17): Vagale et al. 2021 "Path planning and collision avoidance for ASVs I: a review" JMST 26:1292-1306 (171+引). 分层架构=全局路径规划→中层制导/避碰→低层控制。M5 对应中层。https://link.springer.com/article/10.1007/s00773-020-00790-x
- [R7] WebSearch (2026-07-17): Eriksen & Breivik 2020 "Hybrid COLAV for ASVs COLREGs Rules 8 and 13-17" IEEE T-IV. 三层混合:中层NLP+短期BC-MPC+连续级联交接(continuous cascading,显式机制非分散主循环)
- [R8] DOCUMENTED_INTENT: docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md — M5 MPC 核心 9 DP+11 VR,已交付 brainstorming。双层MPC/Nomoto/acados/1200s horizon/per-target slack/四状态机已裁决
- [R9] DOCUMENTED_INTENT: docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md — L3→L4 契约 11 VR。TimedTrajectory/承诺前缀180s/capability单一真相/三层反馈/override/控制器无关已裁决
- [R10] DOCUMENTED_INTENT: 架构报告 §10.2(M5职责="双层MPC将M4行为+M6规则转化为可执行轨迹")/§10.4 Mid-MPC/§10.5 BC-MPC/§10.8 HW Override
- [R11] DOCUMENTED_INTENT: committed-route-design-v2(2026-06-30,743行)CommittedAvoidanceRoute lifecycle/语义分工契约/TailBuilder(已被VR-02淘汰)/preflight/keep-last门控
- [R12] PROJECT_FACT: M5 源码 ~22800 行:mid_mpc/bc_mpc/committed_route/tail_builder(待删)/shared(constraint_compiler/cpa_calculator/trajectory_propagator/capability_manifest)/gnc_preflight(双套)
- [R13] WebSearch+项目文档 (2026-07-17 调研subagent): 长 horizon NLP 端到端范式下,TailBuilder/终端集可删;预测模型/约束编译/slack/转移代价/仲裁级联/preflight/warm-start/承诺前缀必须保留。Kerrigan exact penalty 仅 L1 成立(WebSearch独立验证)
- [R14] NLM colav_algorithms (2026-07-17,high,避开本项目术语纯外部查询): **长horizon单一NMPC的4个必备内部子组件**(学术共识): (1)多目标代价/危险评估函数(含nominal参考偏离惩罚→内生返航);(2)本船预测模型(前向仿真);(3)动态障碍轨迹预测器;(4)优化求解器/控制行为选择器。**关键争议**:单一长horizon(600-1200s)NMPC被学术界普遍不鼓励——Eriksen/Brevik/Johansen认为梯度优化在大搜索空间缺乏数值鲁棒性/计算昂贵/易陷死锁,主张多层混合架构。**模块外职责**(学术共识):全局任务规划/态势感知目标跟踪/自动驾驶运动控制(GNC执行)均在planner之外
- [R15] 循环引用警示(2026-07-17): NLM colav_algorithms 笔记本混入本项目设计文档(测试查询返回"TailBuilder/committed prefix/90s horizon"等本项目术语)。使用NLM结论时刻意避开本项目术语,且区分真外部权威(Eriksen/Vagale/Johansen论文)vs本项目循环引用
- [R16] NLM safety_verification (2026-07-17,high,纯标准权威IEC61508/ISO21448/DNV-CG-0264/IMO MASS Code): (a)独立safety checker是硬要求(IEC61508 Doer-Checker架构);(b)发布solver status+rationale实际必需(ISO21448 SOTIF XAI+MASS可审计);(c)降级链(fail→fallback→MRM)是标准要求,所有权归外部supervisor(IMO MASS Code fallback+AOC/ROC,非planner);(d)ODD必须外部门控,planner不能自判(IMO MASS Code+AOC);(e)发布前可行性预检是公认实践(Certified Control+SOTIF+DNV)。全部真外部权威非本项目循环引用
- [R17] NLM colav_algorithms (2026-07-17,high,纯学术Eriksen/Johansen/Blindheim/Bø): 长horizon内ODD边界跨越问题——学术界承认是已知简化(Eriksen 2020)。共识:动态安全约束应直接嵌入MPC优化(Bø&Johansen 2014 fault-tolerant state set / Blindheim 2020 risk-based MPC),非纯响应外部。既有实践:receding horizon频繁replan(5s)是主要防御+PSB-MPC主动处理horizon内不确定性(OU+KF+Monte Carlo)。对本项目:长horizon1200s下ODD前瞻学术推荐,但可先用replan(60s)作第一道防御,PSB-MPC式前瞻留P7([R8]VR-09)演进

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step1 · 行业调研·发现决策点  [2026-07-17]

**模式判定**: 重构（M5 已有完整实现 ~22800 行 + 大量已裁决设计文档）。现有代码/设计是主证据之一（机制B 重构模式），外部权威（NLM/WebSearch）用于验证/补强/纠偏。progress.md（2026-06-08）已严重过时，不作基线。

**与两个并行对话的边界**:
- MPC 核心算法（DP-01~09：预测模型/约束/求解器/时域/参考跟踪/回退/不确定性）→ `2026-07-16-m5-mpc-colav-design-log.md`
- L3→L4 接口契约（11 VR：timed trajectory/承诺前缀/capability/反馈/override/控制器无关）→ `2026-07-17-l3-l4-gnc-contract-design-log.md`
- **本对话**：M5 作为完整模块的功能职责划分——哪些职责必须、边界在哪、是否完备、是否工程合理。

（决策点提取见下方 Step1 续）

#### Step1 续 · 决策点提取  [2026-07-17]

**NLM 连通性**: 本机（A4000 datacenter IP）NLM 被 region/anti-abuse gate 拦截（`location=unsupported`），非登录问题，无法通过重试解决。本步骤证据来源：(a) 2026-07-16 NLM 可用时的 4 条 high 置信查询转录 [R1]-[R5]；(b) 2026-07-17 WebSearch（Vagale 2021/Eriksen 2020/COLAV 综述）[R6][R7][R13]；(c) 项目已裁决设计文档 [R8]-[R11]；(d) 代码现状 [R12]。证据基础充分。

**主干职责维度提取**（自顶向下从 M5 核心职责推导 + NLM[Vagale/Eriksen]分层原则验证）:

M5 核心职责一句话（架构报告 §10.2[R10]）："在 ODD 约束下，把 M4 行为决策 + M6 COLREGs 规则语义 + M2 世界模型，转化为 L4 可执行的避碰轨迹，并承担求解失败时的有序降级。"

逐层分解"要完成这个职责必须做哪些事"，每个"事"是一个职责维度：

1. **产生避碰轨迹** → DP-02 战术机动生成（Mid-MPC NLP）+ DP-03 紧急反应（BC-MPC）。NLM[R2][R7] Eriksen 双层 MPC 是业界标准分工。
2. **把规则语义变成优化器能消费的约束** → DP-04 COLREGs 约束编译。NLM[R5] 论证语义(M6)与数学(optimizer)的翻译边界是独立职责。
3. **多个候选怎么选、失败了怎么办** → DP-05 候选源仲裁与降级链。NLM[R1][R7] Eriksen 连续级联是显式独立机制。
4. **输出的轨迹归谁拥有、怎么稳定发布** → DP-06 航线所有权与输出组装。NLM[R1] route manager 独立于 optimizer。TailBuilder 淘汰后[R8/VR-02]这个职责的归属是关键问题。
5. **发布的轨迹L4能不能执行** → DP-07 GNC 可执行性预检。NLM[R5] preflight 独立 gatekeeper；DNV[R4] doer-checker。
6. **M5自己出问题怎么上报** → DP-08 fail-safe 上报。NLM[R3] planner 不持 safety authority，emit concern。
7. **M5在什么条件下运行/冻结** → DP-09 ODD 门控。NLM[R4] 外部 manager 门控。
8. **决策可追溯** → DP-10 审计与可观测性。CCS i-Ship / IEC 61508 SIL2 认证前置。
9. **求解跨周期稳定** → DP-11 warm-start/反chattering。NLM[R13] warm-start 是首要反 chattering 机制。
10. **整体完备性** → DP-01 框架：上述 9 个职责是否覆盖完整？有无遗漏？

**技术分解触发判定**: TD-01 的 11 个子决策点全部为**架构/接口型**（答案形态是"职责归属/边界划分"非"采用某算法"），不触发机制C 算法技术分解。MPC 内部算法已在 [R8] 的 mpc-colav TD-01 裁决，本对话不重复。

**与之前 architecture-design-log(2026-07-16) 的区别**: 那份日志基于 TailBuilder 存在的旧假设，已被 VR-02 跨树修订淘汰。本日志是重开，遵循新架构前提（TailBuilder 淘汰/horizon 1200s/TimedTrajectory）。旧日志标 superseded。

### Step2 · grilling 压力测试  [2026-07-17 · 逐DP推进]

#### DP-01 框架完备性  [2026-07-17 用户确认]

| 视角 | DP-01 M5 职责分解的完备性 |
|---|---|
| 专家 | [R6]Vagale分层/[R7]Eriksen三层/[R14]MPC内部4子组件/[R16]5辅助职责标准硬要求。完备性cross-check:DP-02~DP-11的9职责与4内部+5辅助一一对应无遗漏 |
| 新手 | 为何不是现有代码目录?代码目录是"怎么实现"非"该做什么";TailBuilder已淘汰但代码还在;committed_route职责需重新定位(DP-06) |
| 悲观 | 完备性清单漏维度会怎样?潜在遗漏:速度管理/变速(Rule8 ample time含速度)、多船/死锁处理 |
| 盲区 | BL-01速度管理(中)/BL-02多船处理(中) |

**用户裁决(2026-07-17)**: 9维度完备,不新增。BL-01归入DP-02决策变量u+DP-04速度bounds;BL-02归入DP-04 per-target slack+DP-05仲裁。两盲区闭环。

#### DP-02 战术机动生成职责(Mid-MPC NLP核心)  [2026-07-17 用户确认]

| 视角 | DP-02 |
|---|---|
| 专家 | [R14]长horizon NMPC内部必备4子组件(多目标代价/本船预测/障碍预测/求解器),已在[R8]DP-02/05/07/09裁决。本对话审查外部边界:Mid-MPC仅产候选轨迹 |
| 新手 | 为何Mid-MPC不能从头管到尾?[R1][R5]optimizer只产候选,航线生命周期/发布门控/降级链是独立职责。现状mid_mpc_node.cpp混在1Hz循环(2196行)正是要拆解的 |
| 悲观 | 边界划错:Mid-MPC仍拥有完整航线→TailBuilder淘汰后谁拼轨迹模糊;越界直接publish绕过DP-05仲裁+DP-07预检。[R14]长horizon争议(Eriksen/Johansen不鼓励单一1200s NMPC)是真实风险,已由[R8]acados RTI+四状态机兜底 |
| 机制C默认最简版失效 | ①代价仅CPA罚→不返航(老TailBuilder根因,[R8]VR-07b已修);②预测恒速→大转向chattering([R8]VR-02已修Nomoto);③障碍恒速外推→目标机动CPA误判([R8]VR-09 P7待装);④求解器无限制IPOPT→1200s O(n³)不可承受([R8]VR-05已修acados) |
| 盲区 | BL-03组装责任归DP-02还是DP-06(高) |

**用户裁决(2026-07-17)**: 边界明确。DP-02仅产裸候选轨迹(MidMpcSolution [ψ,r,u,x,y]),组装成TimedTrajectory归DP-06。BL-03闭环。4内部子组件已在[R8]裁决不重复。[R14]长horizon争议诚实记录,由[R8]acados+四状态机兜底。

#### DP-03 紧急反应职责(BC-MPC)  [2026-07-17 用户确认]

| 视角 | DP-03 |
|---|---|
| 专家 | [R2][R7]Eriksen双层业界标准:BC-MPC短horizon分支枚举argmax-min CPA,负责Mid兜不住的短时危险/Rule17 stand-on被迫机动。[R8]VR-01a Eriksen标准职责。[R9]VR-08独立emergency override通道(ReactiveOverrideCmd非trajectory) |
| 新手 | 为何独立成层?时间尺度不同:Mid 60s replan,紧急5-10s发生。现状半活:CI launch含/production单节点/self-activation key on CPA非failures([R8]P6待修) |
| 悲观 | 新架构下BC接管语义变化:旧架构发空plan释放走廊,新架构Mid产端到端轨迹,"释放"语义变了。HANDOVER_NEUTRAL交还条件在新架构下需验证。[R16]FINAL_DEGRADE的MRM决策权归M7,BC只emit concern |
| 机制C默认最简版失效 | ①分支枚举固定候选不分急迫→近距来不及([R8]VR-01已修3档);②接管单一CPA阈值→频繁振荡([R8]VR-01b已修连续级联+hysteresis);③override无validity过期→永不释放([R9]VR-08已定1-3s过期) |
| 盲区 | BL-04新架构下BC接管时Mid行为/HANDOVER交还条件(高,归[R8]P6) |

**用户裁决(2026-07-17)**: 确认。双层分工+独立override通道已在[R8][R9]裁决。BL-04归[R8]P6 phase记录不裁决。

#### DP-04 COLREGs约束编译职责  [2026-07-17 用户确认]

| 视角 | DP-04 |
|---|---|
| 专家 | [R5]约束编译是独立翻译边界:M6语义推理(role/phase/dir/min_alt/lifecycle),M5翻译成NLP约束/代价行。[R8]VR-04 Eriksen混合(M6几何hard+Rule8/17soft+转移代价)。现状constraint_compiler.cpp成熟实现 |
| 新手 | 为何M6不直接输出优化器约束?[R5]数学形态依赖M5求解器/状态量,M6决定则换求解器改M6违反关注点分离。现状已基本落地,最成熟子功能之一 |
| 悲观 | 风险1:M5越界推理语义(committed-route§14禁止,旧tail_builder风险已淘汰);风险2:硬编码偏移残留([R8]VR-04已裁决移除,落地度待BL-05核实);风险3:per-target slack编译在compiler还是formulation |
| 机制C默认最简版失效 | ①单标量约束共享→多船masking([R8]VR-03已修per-target);②全硬约束→频繁无解([R8]VR-03已修slack L1/L2);③硬编码偏移→斜遇失效([R8]VR-04已修M6几何驱动) |
| 盲区 | BL-05硬编码迁移落地度(中,Step3代码核实) |

**用户裁决(2026-07-17)**: 确认。翻译边界已落地,最成熟子功能之一。BL-05归Step3代码核实。

#### DP-05 候选源仲裁与降级链职责  [2026-07-17 用户确认]

| 视角 | DP-05 |
|---|---|
| 专家 | [R1][R7]Eriksen连续级联是显式独立机制。[R8]VR-01b/VR-08四状态机+门控+hysteresis。现状committed_route.cpp已实装四状态机+45s门控,但heading/drift触发死代码 |
| 新手 | 为何独立非分散主循环?分散重复三级判定+门控无处安放→latching+shattering。现状mid_mpc_node 2196行混回退逻辑正是要提取的 |
| 悲观 | **[R16]关键所有权**:降级链所有权归外部supervisor(AOC/M7)非planner。M5做候选选择(合理) vs 降级最终决策(何时FINAL_DEGRADE/MRM归M7)。现状M5状态机+MUST-9断裂暴露真实gap:M5降级判定与M7 MRM决策无连通契约 |
| 机制C默认最简版失效 | ①无状态机纯失败计数→冻结计划([R8]VR-08已修);②无门控直接切换→振荡([R8]VR-01b已修hysteresis);③M5自决MRM→违反[R16]Doer-Checker([R8]VR-08已修报M7但MUST-9断) |
| 盲区 | BL-06候选健康与MRM契约边界(高,Step3与DP-08交叉);BL-07 heading/drift触发死代码(中) |

**用户裁决(2026-07-17)**: 确认所有权边界。M5四状态机做候选健康状态机(选择生效候选)=合理planner职责;FINAL_DEGRADE的MRM触发权归M7,M5只emit concern。BL-06归Step3与DP-08交叉。BL-07已知bug。

#### DP-06 航线所有权与输出组装职责  [2026-07-17 用户确认 · 最关键未决]

| 视角 | DP-06 |
|---|---|
| 专家 | [R1]route manager独立于optimizer。[R9]VR-01/VR-06 TimedTrajectory+承诺前缀180s+version+material-change。TailBuilder淘汰后([R8]VR-02/07b),DP-06职责=把DP-02裸候选轨迹组装成L4可消费TimedTrajectory(前缀冻结+version+publish门控)。现状committed_route承担此角色(9态+hash+try_revise)但残留TailBuilder时代拼接逻辑 |
| 新手 | 为何组装独立非DP-02直接输出?[R9]VR-06承诺前缀+version+publish门控是契约管理逻辑,与轨迹优化不同关注点。DP-02直接publish会每60s重发让L4跟踪器重置chattering |
| 悲观 | **TailBuilder淘汰后核心未决**:①承诺前缀180s冻结逻辑——NLP端到端每60s重解改变整条轨迹,冻结几何还是只冻结publish?②committed_route角色演化:从拼接器→契约管理器,残留拼接逻辑须清理;③route_hash稳定性:NLP每60s重解轨迹几何必变,hash如何稳定? |
| 机制C默认最简版失效 | ①每周期publish裸NLP解→L4 chattering([R9]VR-06已修);②无承诺前缀冻结→L4执行到一半轨迹变([R9]VR-06已修);③残留TailBuilder拼接→与新NLP端到端冲突/双重真相源(须清理) |
| 盲区 | BL-08承诺前缀冻结语义(NLP重解vs前缀冻结张力,高,Step3);BL-09 committed_route角色演化(残留拼接清理+hash稳定,高,Step3) |

**用户裁决(2026-07-17)**: 确认。DP-06是本对话最关键未决。BL-08/09是新架构下(TailBuilder淘汰)浮现的新问题,归Step3深入。BL-08触及深层张力:NLP端到端重解vs承诺前缀冻结,可能需NLP加前缀不变性约束或重新定义"冻结"语义。

#### DP-07 GNC可执行性预检职责  [2026-07-17 用户确认]

| 视角 | DP-07 |
|---|---|
| 专家 | [R5]preflight独立gatekeeper≠optimizer≠route lifecycle。[R16]IEC61508/ISO21448/DNV确认发布前预检是公认实践。[R9]VR-05改读GncExecutionOdd删硬编码。现状两套preflight并存(通用未接线+实用4调用点硬编码) |
| 新手 | 为何M5非L4做预检?[R5][R16]Doer自检(快速失败避免不可执行plan占用L4),非L4 authority。L4仍有最终accept/reject权([R9]VR-07)。M5预检失败仅不发布,不触发MRM |
| 悲观 | 风险1:越界成feasibility authority([R9]裁决GNC reject>M5 preflight);风险2:两套参数漂移(0.5/3.5 vs 0.25/4.7,[R9]Step3已定位);风险3:预检范围——整条1200s远段不精确误拒 vs 只检前缀漏远段 |
| 机制C默认最简版失效 | ①无预检直接publish→不可执行plan占用L4 reject循环([R16]须预检);②硬编码包络→与GNC漂移([R9]VR-05已裁决改读订阅,4调用点待修);③预检整条1200s→远段误拒(须明确范围) |
| 盲区 | BL-10预检范围整条vs前缀(中,Step3);BL-11两套收敛(中,Step3) |

**用户裁决(2026-07-17)**: 确认。Doer自检(非authority)定位已三重确认[R5][R9][R16]。BL-10(预检范围)+BL-11(两套收敛)归Step3。

#### DP-08 fail-safe上报职责  [2026-07-17 用户确认 · 最严重架构断裂]

| 视角 | DP-08 |
|---|---|
| 专家 | [R3]planner不持safety authority,不可直接发MRM,须emit concern给独立supervisor。[R16]IEC61508/ISO21448/DNV/MASS Code确认降级链所有权归外部supervisor(AOC/M7)。[R8]VR-08 FINAL_DEGRADE报M7(safety_concern_event)。架构§11.6指挥链:M5发concern→M7决定MRM→M1通知M5冻结 |
| 新手 | 为何M5不能直接发MRM?[R3][R16]Doer-Checker:M5是Doer(非凸NLP不可白盒SIL2),MRM须独立简单Checker(M7)做才过认证。现状:committed_route设concern字符串不离开M5进程+spdlog::critical。MUST-9是MOCK/断裂 |
| 悲观 | **当前M5最严重架构断裂**:四状态机能进FINAL_DEGRADE但不离开M5进程→M7无法感知→MRM链断→NLP持续失败时系统既不降级也不MRM,M5可能持续输出DEGRADED兜底/keep-last过期轨迹。[R16]MRM时延须<TMR60s,当前NLP-internal-fail路径时延=∞ |
| 机制C默认最简版失效 | ①仅日志spdlog→M7无法感知MRM链断(当前现状);②M5自决MRM直接发命令→违反[R3][R16]Doer-Checker独立性认证不过;③concern无结构化字段→M7无法区分failure类型选MRM |
| 盲区 | BL-12 safety_concern_event msg契约(failure_type enum+severity+context+M7映射MRM+时延<TMR60s,高,与BL-06合并);BL-13 M7侧concern入口是否存在(高,Step3) |

**用户裁决(2026-07-17)**: 确认。DP-08暴露M5最严重架构断裂(MUST-9)。职责=M5 emit concern(非自决MRM)已在[R3][R16]确认。BL-12/13归Step3代码核实。

#### DP-09 ODD门控职责  [2026-07-17 用户确认 · 第二严重架构gap]

| 视角 | DP-09 |
|---|---|
| 专家 | [R4]ODD由高层外部manager(AOC/EVAA)门控非planner自判。[R16]IMO MASS Code确认外部AOC验证条件违规接管/交ROC。ADR-1:M1 ODD状态是行为切换唯一来源。M5-spec§4.4 ODD门控状态机(WAITING→ACTIVE→PAUSED→DEGRADED_MODE→MRM_ESCALATE) |
| 新手 | 为何M5不能自判ODD?[R4][R16]ODD是全局安全上下文(感知/海况/系统健康),M5只看避碰视角不全。现状:M5构造函数无sub_odd_,全程无视/l3/m1/odd_state——ADR-1直接违背,OUT-of-ODD下仍全速运行 |
| 悲观 | ADR-1违背后果:OUT-of-ODD(感知退化/海况超限)时M5应冻结/降级,但现状全速运行→不安全条件下仍激进避碰。[R16]SOTIF要求runtime monitor外部检测ODD breach。新架构额外问题:长horizon1200s预测可能跨越ODD边界(当前NORMAL但600s后天气超限) |
| 机制C默认最简版失效 | ①无ODD订阅全速运行→OUT-of-ODD激进避碰(当前现状ADR-1违背);②M5自判ODD→违反[R4][R16]外部manager原则+视角不全;③仅响应当前ODD无前瞻→长horizon1200s可能跨越ODD边界 |
| 盲区 | BL-14 M5不同ODD状态行为契约(NORMAL/DEGRADED/OUT-of-ODD/CRITICAL,高,Step3);BL-15长horizon是否需ODD前瞻(中,Step3) |

**用户裁决(2026-07-17)**: 确认。DP-09暴露第二严重架构gap(ADR-1违背,与DP-08 MUST-9并列)。职责=外部触发(M1)非自判已在[R4][R16]确认。BL-14(行为契约)+BL-15(长horizon前瞻)归Step3。

#### DP-10 审计与可观测性职责  [2026-07-17 用户确认]

| 视角 | DP-10 |
|---|---|
| 专家 | [R16]ISO21448 SOTIF要求XAI输出predicted safety info;MASS Code要求决策可解释可审计;IEC61508 Certified Control要求不可伪造certificate。ADR-3每条出消息stamp+schema_version+confidence+rationale。M5-spec§3.3 CMM+ASDR+SAT |
| 新手 | 为何独立职责?[R16]认证要求每决策可追溯(CCS i-Ship)+M7 SOTIF消费M5 solver status。现状:ASDR已实装,CMM plan级已填,但NLP path waypoint CMM空/cost恒0(Phase E1桩)/SAT硬编。大部分落地字段不完整 |
| 悲观 | 认证风险:CCS审查需完整证据链,solver status/active constraints/cost缺失则不过。M7 SOTIF风险:M7需消费NLP收敛/KKT([R11]Slice K未接线)。新架构:acados后solver status语义变(RTI每次QP收敛 vs IPOPT全局) |
| 机制C默认最简版失效 | ①无ASDR→认证无证据链;②CMM字段空/硬编→M7/M8拿无效confidence;③不发布solver status→M7 SOTIF无法检测NLP局部最优(Slice K未接线) |
| 盲区 | BL-16 acados后solver status审计适配(中,Step3 [R8]P1b处理) |

**用户裁决(2026-07-17)**: 确认。审计大部分落地(ASDR/CMM/SAT框架在),字段完整性gap是工程债非架构问题。BL-16归Step3。

#### DP-11 求解连续性与稳定性职责  [2026-07-17 用户确认 · Step2最后一个]

| 视角 | DP-11 |
|---|---|
| 专家 | [R8]VR-TBD7反chatter三层:warm-start shift-init(首要,保持同伦类)+转移代价混合范数(L2航向+L1速度,Eriksen tran_χ/tran_U)+符号翻转检测(Tengesdal K_sgn·exp)。[R13]warm-start首要独立验证。[R14]长horizon数值鲁棒性争议指向稳定性必要性 |
| 新手 | 为何独立非optimizer内部?反chatter是跨周期连续性管理(warm-start/hysteresis/符号翻转),与单次NLP求解不同时间尺度。现状分散在solver/formulation/committed_route三层未集中 |
| 悲观 | chattering是COLAV已知首要失效模式([R13]Hagen/Kufoalor)。新架构chatter风险变化:旧来自NLP↔TailBuilder拼接边界,新来自NLP每60s自由重解整条轨迹。**关键张力**:warm-start shift(保持同伦类shift整条)vs承诺前缀冻结(前180s不动)——BL-17 |
| 机制C默认最简版失效 | ①无warm-start冷启动→收敛慢+跳变chattering;②纯L2-on-heading转移代价→最弱孤立选项([R8]VR-TBD7已论证);③稳定性逻辑分散无统一管理→难审计难调参 |
| 盲区 | BL-17 warm-start shift vs承诺前缀冻结冲突(高,与BL-08强耦合);BL-18稳定性逻辑集中vs分散(中) |

**用户裁决(2026-07-17)**: 确认。稳定性三层组合已在[R8]VR-TBD7裁决。BL-17(warm-start vs承诺前缀冻结)是新架构关键张力,与DP-06 BL-08强耦合,归Step3。BL-18代码组织归Step3。

---

### Step2 完成总结  [2026-07-17]

**11个DP全部经三视角grilling+逐点用户确认**。核心产出:

**3个最关键架构问题(新架构下浮现,Step3必查)**:
1. **DP-06 BL-08/BL-09 + DP-11 BL-17**: TailBuilder淘汰后的航线所有权重组——承诺前缀冻结语义(NLP每60s重解vs前缀冻结张力)、committed_route角色演化(拼接器→契约管理器)、warm-start shift vs承诺前缀冻结冲突。这三者强耦合,是新架构(horizon1200s端到端)的核心未决。
2. **DP-08 BL-12/13 + DP-05 BL-06**: MUST-9 fail-safe链断裂——M5 emit concern(非自决MRM)契约定义+M7消费路径+MRM时延<TMR60s。
3. **DP-09 BL-14/15**: ADR-1 ODD门控缺失——M5行为契约(NORMAL/DEGRADED/OUT-of-ODD/CRITICAL)+长horizon ODD前瞻。

**已裁决归另两对话的(本对话只记录)**: DP-02(4内部子组件)/DP-03(双层分工)/DP-04(约束编译内部)/DP-05(四状态机)/DP-11(三层反chatter)的核心算法已在[R8][R9]裁决。

**盲区清单(Step3处理)**: BL-04(P6 phase)/BL-05(代码核实)/BL-06(MRM契约)/BL-07(死代码)/BL-08(前缀冻结)/BL-09(committed_route演化)/BL-10(预检范围)/BL-11(两套收敛)/BL-12(concern msg)/BL-13(M7入口)/BL-14(ODD行为)/BL-15(ODD前瞻)/BL-16(acados审计)/BL-17(warm-start冲突)/BL-18(稳定性集中)。共15个,其中高优先级7个(BL-06/08/09/12/13/14/17)。

### Step3 · 自主深度调研  [2026-07-17 · 聚焦3关键架构问题]

**方法**: NLM纯外部权威查询(BL-15)+ 2个Explore agent代码核实(承诺前缀冻结/ODD门控 + M5→M7 fail-safe链路)。15个盲区中:高优先级7个全部闭环,中优先级部分闭环。

#### 关键架构问题1: 承诺前缀冻结语义(DP-06 BL-08/09 + DP-11 BL-17)

**代码核实发现(比预期清晰)**:
- BL-08闭环: 承诺前缀=**冻结几何**(非冻结publish)。committed_route.cpp:168-173,328-340 preserves_committed_prefix逐点比对容差1cm;mid_mpc_solver.cpp:200-203 NLP equality行(lbg=ubg=0)强制前K步=冻结WGS84重投影值。
- BL-17闭环: **冲突不存在**。warm-start实际是prefix重投影+suffix冷启动(mid_mpc_solver.cpp:145-166),非shift整条(legacy shift保留但solve()不再用)。前缀冻结由equality行强制,warm-start只影响收敛速度不改解。两者正交。
- BL-09闭环: **TailBuilder拼接仍是活跃执行路径非残留**(mid_mpc_node.cpp:1773,1638 optimized分支每周期执行L2-prefix+NLP-body+TailBuilder-hold+L2-suffix四段拼接)。新架构(1200s端到端)落地须删append_tail_waypoints_等改NLP单段直生。hash比较整条轨迹,prefix rolling prune每次revise。

**Step3结论**: 承诺前缀冻结机制本身已正确实装(几何冻结+equality约束+warm-start正交)。真正的gap是**TailBuilder拼接仍是活跃路径**,与新架构端到端愿景冲突——这是过渡期活跃残留,新架构落地时清理。

#### 关键架构问题2: MUST-9 fail-safe链路(DP-08 BL-06/12/13)

**代码核实发现(纠正arch:934过时判断)**:
- BL-06/12闭环: M5的safety_concern_event是**内存字符串**(committed_route.hpp:84)+spdlog::critical,无ROS2 publish。M5无任何→M7 publisher(mid_mpc_node.cpp:500-509)。
- BL-13闭环: 两端都改。**重要纠正**:M7 HC-1~6硬约束**已实现非死代码**(safety_supervisor_node.cpp:670-698,arch:934过时);on_avoidance_plan派生**真实指标非全0**(:405-428消费nlp_solver_status/kkt/tail_gate_failed);MRM触发已实现(通过monitor,mrm_selector.cpp)。
- **链路断点精确化**: M5→M7 concern缺失(M5无publish) + M7→M5冻结返回缺失(无/l3/m5/freeze) + M1→M5冻结也缺。M7 on_safety_concern是空操作(只log不喂MRM,:507-516)。**真正发布concern的是M1非M5**(odd_envelope_manager_node.cpp:367-368)。
- fallback_policy.py是Python原型未集成C++。

**Step3结论**: MUST-9链路在两端都断(M5→M7 concern + M7/M1→M5冻结返回)。但M7的policing能力(HC检查+MRM触发)其实已实现,只是不走concern路径而走monitor路径。修复=(a)M5加SafetyConcernEvent publish到/l3/safety/concern (b)M7 on_safety_concern接入MRM仲裁 (c)定义M7/M1→M5冻结返回路径。

#### 关键架构问题3: ODD门控(DP-09 BL-14/15)

**代码核实+NLM发现**:
- BL-14: M5零订阅ODD(mid_mpc_node.cpp:453-498无sub_odd_)。M1输出6状态FSM(In/Edge/Out/MrCPrep/MrCActive/Overridden)**与M5-spec§4.4的5状态不对齐**,须定义M1→M5映射(参考M6 zone→OddDomain)。其他模块M2/M3/M4/M6/M7/M8全订阅,M5是规划模块唯一缺失。M5_RESUME零命中回切也断。
- BL-15闭环(NLM[R17]): 学术界承认长horizon内条件变化是已知简化。共识:动态安全约束应嵌入MPC(Bø&Johansen2014/Blindheim2020)。实践:receding horizon replan(60s)是第一道防御。本项目先用replan(60s),PSB-MPC式前瞻留P7演进。

**Step3结论**: ODD门控完全缺失(订阅+FSM+行为契约+回切)。修复须(a)M5加/l3/m1/odd_state订阅 (b)定义M1 6状态→M5行为映射 (c)实装NORMAL/DEGRADED/OUT-of-ODD行为 (d)定义M5_RESUME回切路径。长horizon ODD前瞻先用replan(60s)防御,P7演进。

#### Step3盲区闭环总表

| 盲区 | 闭环方式 | 结论 |
|---|---|---|
| BL-06 | 代码核实(与BL-12合并) | M5→M7 concern缺失+M7→M5冻结返回缺失,两端都断 |
| BL-08 | 代码核实 | 承诺前缀=冻结几何(equality约束),非publish语义 |
| BL-09 | 代码核实 | TailBuilder拼接仍是活跃路径非残留,新架构落地须清理 |
| BL-12 | 代码核实 | safety_concern_event内存字符串无publish,须M5加publish+M7接入 |
| BL-13 | 代码核实 | 两端都改;**纠正arch:934**:M7 HC已实现非死代码,on_avoidance_plan真实指标非全0 |
| BL-14 | 代码核实 | M5零订阅ODD,M1 6状态与spec§4.4 5状态不对齐,须映射 |
| BL-15 | NLM[R17] | replan(60s)第一道防御,PSB-MPC前瞻留P7 |
| BL-17 | 代码核实 | warm-start冲突不存在(prefix重投影+suffix冷启动,非shift整条) |
| BL-04 | 归[R8]P6 phase | 不裁决 |
| BL-05/07/10/11/16/18 | 低优先级代码核实/工程债 | 留实现时处理 |

### Step4 · 汇总分析·推荐方案  [2026-07-17 · 11 DP全部推荐,用户分三批确认]

**技术分解完整性校验**: TD-01的11子决策点全部为架构/接口型(非技术型),无DECOMPOSITION_INCOMPLETE。MPC内部算法已在[R8]的TD-01(mpc-colav)裁决。

**11 DP推荐分三批(用户逐批确认)**:
- 第一批(已充分裁决/成熟,VR-01/02/04/11): 确认现状/已裁决,本对话不新增裁决。风险低。
- 第二批(有gap方向明确,VR-03/05/07/10): 方向已定,工程债/P phase/代码核实修复。风险中低。
- 第三批(3关键架构断裂,VR-06/08/09): **须架构级修复**,本对话最重要产出。风险高。

**3关键架构断裂的推荐(本对话核心产出)**:
1. **VR-06 DP-06航线重组**: TailBuilder拼接仍是活跃路径须清理→committed_route演化为契约管理器(只管version/prefix冻结/publish门控)→前缀冻结保留(几何冻结+equality已正确)
2. **VR-08 DP-08 MUST-9两端断**: M5加concern publish+M7 on_safety_concern接入仲裁+M7/M1→M5冻结返回路径。**重要**:M7 HC policing+MRM已实现(走monitor),只缺concern路径接线
3. **VR-09 DP-09 ADR-1 ODD门控**: M5加ODD订阅+M1 6状态→行为映射+M5_RESUME回切

**DP-06/08/09关联**: DP-09(ODD)触发DP-08(concern);DP-08依赖DP-05(候选健康);DP-06(航线重组)与DP-08/09正交。

**风险量化**: DP-06/08/09均为高风险(架构断裂),须SIL验证。DP-03/05/07为中风险(P phase/工程债)。其余低风险。

### Step5 · DESIGN-IT-TWICE  [2026-07-17 · 3高风险DP对抗验证]

**对象选择(用户确认)**: 跳过8个低/中风险DP(DP-01/02/03/04/05/07/10/11直接采纳Step4推荐),对DP-06/08/09三个高风险架构断裂做DESIGN-IT-TWICE。

**DP-06航线所有权重组**:
- 方案A(NLP单段直生+committed_route契约管理化) ★★★★☆ vs 方案B(保留拼接参数化) ★★☆☆☆
- 裁决采纳A: 方案B保留拼接是"过渡期永远"技术债+双重真相源是当前bug根因。[R8]已裁决TailBuilder淘汰,A是落地。acados实时性由[R8]VR-05/P1b兜底。

**DP-08 MUST-9两端断修复**:
- 方案A(完整两端修复:M5 publish+M7仲裁+冻结返回) ★★★★☆ vs 方案B(仅M5日志增强) ★☆☆☆☆
- 裁决采纳A: 方案B不构成fail-safe链路,M7永远收不到。方案A两端改但M7 HC policing+MRM基座已就绪(Step3纠正),实际工作量比预期小。

**DP-09 ADR-1 ODD门控补全**:
- 方案A(完整门控:订阅+FSM映射+行为+回切) ★★★★☆ vs 方案B(M5内部保守降级) ★☆☆☆☆
- 裁决采纳A: 方案B违反[R4][R16]外部manager原则+M5视角不全(无海况/感知/系统健康)。方案A M1 6状态FSM已实现+M4/M6订阅参考模式可复用。

**三张卡片共同结论**: 方案A(完整修复)均优于方案B。**共同特点:外部基座(M1 6状态FSM/M7 HC policing/M7 MRM触发)已就绪,M5是缺失的连接方**。修复=M5加缺失的订阅/publish/行为映射,非从零建。

**Step5结论: 3裁决经DESIGN-IT-TWICE无回炉。8低风险DP用户授权跳过。全部11 DP最终态确定。**

### Step6 · 术语+技术规约+方案包  [2026-07-17 · 待用户接受]

- **术语表**: 7术语(职责维度/候选健康状态机/契约管理器/承诺前缀冻结/SafetyConcernEvent/ODD门控映射/Doer自检),每含定义+本方案含义+边界+关联DP
- **技术规约表**: 9条TS-A01~09,聚焦职责边界/所有权/契约层面(坐标系/单位等已归另两对话TS)
- **方案包八组件**: 独立成文 `docs/superpowers/specs/2026-07-17-m5-module-functional-partition-solution-pack.md`
- **状态**: 待用户接受→标"已交付brainstorming"
