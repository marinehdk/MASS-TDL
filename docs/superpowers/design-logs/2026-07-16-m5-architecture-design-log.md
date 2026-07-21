# 设计日志: M5 架构级职责划分与系统契约审查

> **模式**: 重构(M5 已有完整 Mid-MPC + CommittedRouteManager + TailBuilder + BC-MPC 实现，需评审查漏补缺)
> **创建**: 2026-07-16
> **本日志范围**: M5 **架构级**——子模块职责划分、系统契约边界、fail-safe/ODD 门控。**不含 MPC 内部算法**(预测模型/约束/求解器/时域等归 `2026-07-16-m5-mpc-design-log.md` DP-01~09)。
> **关联 spec**: `docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md`(权威设计), `docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-spec.md`
> **状态**: Step1 进行中

---

## 0. 决策树状态(权威索引 · 可变快照)

> **决策点组织原则(自顶向下, 用户要求)**: 不照搬现有代码目录, 从 M5 **职责**出发按 NLM 权威分解原则推导。NLM [R1][R5] 给出 4 个独立职责维度(时空分层/Doer-Checker/感知vs决策/约束编译独立性), 架构报告 §10 给出权威职责定义。决策点按**职责维度(A-H)**组织, 每个 DP 审查该职责的:边界合理性 / 所有权归属 / 契约完整性 / 是否有遗漏或重叠。

### 0.0 M5 职责分解框架(自顶向下, 8 职责维度)

基于架构报告 §10.2(M5 职责一句话: 双层MPC将M4行为+M6规则转化为可执行轨迹) + NLM [R1][R5] 分解原则:

| 职责 | 一句话定义 | NLM/架构依据 | 现有代码载体 |
|---|---|---|---|
| **A. 战术机动生成** | 有限时域NLP产出ψ/u序列+terminal state | §10.4 Mid-MPC; NLM "maneuver generation" | mid_mpc_solver/nlp_formulation |
| **B. COLREGs约束编译** | M6语义+M2几何+船能力→CasADi约束/代价行(翻译,非推理) | NLM [R5] "约束编译独立性"; M6语义独占 | constraint_compiler |
| **C. 确定性尾段延伸** | terminal state→hold+rejoin,消费M6 release信号 | §10.4; NLM "tail building distinct" | tail_builder |
| **D. 航线真相源与生命周期** | 完整航线对象+prefix/suffix+lifecycle+publish门控 | committed-route v2 §3; NLM "route manager独立" | committed_route |
| **E. GNC可执行性预检** | turn-radius/segment/continuity校验(Doer自检,非GNC authority) | committed-route v2 §9.10; NLM [R5] "preflight gatekeeper独立" | gnc_preflight/gnc_avoidance_preflight |
| **F. 候选源仲裁与降级链** | 多候选选择+DegradedHold+stale gate+振荡抑制 | committed-route v2 §8/§9.12; NLM "candidate arbitration distinct" | mid_mpc_node回退链+committed_route仲裁 |
| **G. 紧急反应层(BC-MPC)** | 10Hz分支枚举+Rule17 stand-on机动+接管 | §10.5; NLM [R2] "two-tier MPC" | bc_mpc |
| **H. 系统契约面** | ODD门控(M1)/fail-safe上报(M7)/语义消费(M6)/输出统一(L4/M8) | NLM [R4] "外部门控/concern event"; ADR-1/2/3 | mid_mpc_node构造函数+publish |

### 0.1 决策点注册表 [DP]

| ID | 职责 | 架构级审查问题 | 类型 | 状态 | 详见 |
|----|------|---------------|------|------|------|
| **DP-A01** | (框架) | M5 职责分解为A-H是否完整?有无遗漏/重叠/不该在M5的职责? | 架构 | 未决 | Step2 |
| **DP-A02** | A.机动生成 | Mid-MPC边界:仅产H_pred机动+terminal state,不持完整航线?与B/C/D/F的接口 | 架构 | 未决 | Step2 |
| **DP-A03** | B.约束编译 | constraint_compiler职责边界:翻译(✓)vs推理(✗)?与M6语义独占契约一致性 | 约束 | 未决 | Step2 |
| **DP-A04** | C.尾段延伸 | TailBuilder定位:terminal延伸器(✓)vs独立planner(✗)?route-end bug是否职责层问题 | 架构 | 未决 | Step2 |
| **DP-A05** | D.航线真相源 | CommittedRouteManager是否唯一航线真相源?optimizer/corridor/BC-MPC是否都经它 | 架构 | 未决 | Step2 |
| **DP-A06** | E.可执行性预检 | preflight定位:Doer自检(非GNC authority);两套并存如何收敛;裁决优先级 | 架构 | 未决 | Step2 |
| **DP-A07** | F.候选仲裁+降级 | 候选源优先级+DegradedHold出口+振荡根治;§9.12触发死代码;stale gate完整性 | 架构 | 未决 | Step2 |
| **DP-A08** | G.紧急反应层 | BC-MPC接管契约:触发条件(failures vs CPA validity)+与D/F的协调+lifecycle | 架构 | 未决 | Step2 |
| **DP-A09** | H.ODD门控 | M1外部门控:M5冻结(OVERRIDDEN)vs降级参数;当前缺失如何补;ADR-1 | 接口 | 未决 | Step2 |
| **DP-A10** | H.fail-safe上报 | M5→M7 concern event(不直接MRM);MUST-9断裂修复;MRM时延<TMR60s | 接口 | 未决 | Step2 |
| **DP-A11** | H.语义消费+输出统一 | M6语义独占(确认)+单一输出真相源+event-driven+heartbeat;lifecycle状态机 | 接口 | 未决 | Step2 |

### 0.2 技术分解注册表 [TD]

| ID | 架构 | 分解职责(→DP) | 触发步骤 |
|----|------|---------------|----------|
| TD-A01 | M5 职责分解(自顶向下) | 机动生成(DP-A02), 约束编译(DP-A03), 尾段延伸(DP-A04), 航线真相源(DP-A05), 可执行性预检(DP-A06), 候选仲裁(DP-A07), 紧急反应(DP-A08), ODD门控(DP-A09), fail-safe(DP-A10), 语义/输出(DP-A11) | Step1 |

### 0.3 盲区注册表 [BL]

| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-A01 | BC-MPC 接管触发应基于 consecutive_failures 还是 CPA validity 还是两者OR？Eriksen/Brekke 原始 BC-MPC 的激活条件是什么？ | DP-A05 | 高 | 未闭环 |
| BL-A02 | MUST-9 safety_concern_event 的具体 msg 契约与 M7 消费路径——M7 当前是否已有 concern event 入口？emit 后 M7 到 MRM 的时延是否 < TMR 60s？ | DP-A08 | 高 | 未闭环 |
| BL-A03 | ODD 门控补全——M5 应冻结输出(status=OVERRIDDEN)还是仅降级参数？M1 ODD 状态机的 DEGRADED/OUT-of-ODD 对 M5 的具体要求？ | DP-A09 | 高 | 未闭环 |
| BL-A04 | 两套 preflight(通用 gnc_preflight.cpp 未接线 + 实用 gnc_avoidance_preflight.hpp)应如何收敛——合并、分工、还是删除一套？ | DP-A07 | 中 | 未闭环 |
| BL-A05 | §9.12 heading/drift 触发死代码(mid_mpc_node.cpp:287-288 硬编0)——target maneuver 检测应放 M5 还是 M2？persisted history 在哪维护？ | DP-A06 | 中 | 未闭环 |
| BL-A06 | Normal/DEGRADED 振荡(optimized↔corridor 交替致 route_hash 高频变化)的架构级根治——是前缀冻结不够、还是候选仲裁滞回缺失？ | DP-A06 | 高 | 未闭环 |
| BL-A07 | TailBuilder route-end spacing bug(tail_spacing_invalid 119次,根因已定位未改)——属架构层职责归属问题还是实现bug？是否影响 DP-A04 定位裁决？ | DP-A04 | 中 | 未闭环 |

### 0.4 证据矩阵 [EV]

| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | NLM | colav_algorithms 笔记本: COLAV 模块分解(route manager 独立于 optimizer) | high | 高 | 高 | DP-A02/A03 |
| [R2] | NLM | colav_algorithms 笔记本: two-tier MPC(Eriksen/Brekke NTNU)分工 | high | 高 | 高 | DP-A05 |
| [R3] | NLM | colav_algorithms 笔记本: Doer-Checker fail-safe(planner 不持 safety authority, emit concern) | high | 高 | 高 | DP-A08 |
| [R4] | NLM | safety_verification 笔记本: watchdog/liveness, ODD 外部门控(AOC/EVAA), 多 checker 冗余(1oo2/2oo3) | high | 高 | 高 | DP-A09/A10 |
| [R5] | NLM | colav_algorithms 笔记本: 职责分解原则——(1)COLREGs语义隔离(M6)vs数学约束嵌入optimizer,constraint_compiler是翻译边界;(2)GNC预检独立gatekeeper;(3)机动生成/尾段延伸/候选仲裁三独立职责;(4)时空分层+Doer-Checker+感知vs决策三原则 | high | 高 | 高 | DP-A01(框架) |
| [R6] | DOCUMENTED_INTENT | committed-route spec v2 §3/§5/§9.7/§9.10/§9.12/§10/§13/§14 | — | 高(本项目权威) | — | 全部 |
| [R6] | DOCUMENTED_INTENT | M5-spec.md §2/§3/§4/§6 | — | 高 | — | 全部 |
| [R7] | PROJECT_FACT | committed_route.cpp/hpp: 9态(含BcMpcFollow)+try_revise+FNV hash+45s门控 | — | 仅本项目 | — | DP-A02/A06 |
| [R8] | PROJECT_FACT | tail_builder.cpp: 完整实现§9.7(constant-offset hold+曲率rejoin+消费M6/M2) | — | 仅本项目 | — | DP-A04 |
| [R9] | PROJECT_FACT | bc_mpc_node.cpp: consecutive_failures wired + CI launch含bc_mpc; 但 production launch单节点 + self-activation key on CPA(非failures) + 无ODD订阅 | — | 仅本项目 | — | DP-A05 |
| [R10] | PROJECT_FACT | committed_route.cpp:392 + mid_mpc_solver.cpp: MRM 仍 spdlog::critical, safety_concern_event 字符串不离开M5进程(MUST-9断裂) | — | 仅本项目 | — | DP-A08 |
| [R11] | PROJECT_FACT | mid_mpc_node.cpp 构造函数无 sub_odd_(M5 无 /l3/m1/odd_state 订阅, ODD门控缺失) | — | 仅本项目 | — | DP-A09 |
| [R12] | PROJECT_FACT | gnc_preflight.cpp(274行,未接线,仅单测) + gnc_avoidance_preflight.hpp(实用版,mid_mpc_node调用); 两套并存 | — | 仅本项目 | — | DP-A07 |
| [R13] | PROJECT_FACT | mid_mpc_node.cpp:287-288 heading/drift 硬编0.0(§9.12 触发死代码) | — | 仅本项目 | — | DP-A06 |
| [R14] | PROJECT_FACT | COLREGsConstraint.msg schema115 已含 past_clear/encounter_state/release_predicted(Slice G落地) | — | 仅本项目 | — | DP-A10 |
| [R15] | PROJECT_FACT | AvoidancePlan.msg schema116 单一执行真相源 + segment_source + L2 historical prefix; 旧 avoidance_waypoints 已删 | — | 仅本项目 | — | DP-A11 |
| [R16] | PROJECT_FACT | committed_route.cpp Fix#7: DegradedHold↔Committed 乒乓缓解; 但 optimized↔corridor 交替仍致 route_hash 高频变化 | — | 仅本项目 | — | DP-A06 |
| [R17] | PROJECT_FACT | rule14-ho RED 根因: TailBuilder route-end spacing(tail_spacing_invalid 119次) + crossing_ahead(13次); 代码未改 | — | 仅本项目 | — | DP-A04 |
| [R18] | PROJECT_FACT | publish_avoidance_plan_: event-driven + 60s heartbeat 门控已实装; 但计算仍锁1Hz timer, 无"比发布更频繁计算"; 无10s危险水域profile | — | 仅本项目 | — | DP-A11 |

### 0.5 场景注册表 [SC]

| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| SC-A01 | COLREGs 完整避让生命周期(onset→active→release→rejoin→completed) | ≥10min航线, 跨多个NLP时域, 要求航线连续稳定 | DP-A02/A03/A04/A06 |
| SC-A02 | NLP 反复失败→BC-MPC 接管 | Mid-MPC 不可用时紧急反应层须无缝接管 L4 | DP-A05/A08 |
| SC-A03 | target 突然机动(heading>15°/CPA漂移>20%) | keep-last-route 须识别并转 DegradedHold, 不能盲驶过期世界模型 | DP-A06 |
| SC-A04 | OUT-of-ODD(感知退化/海况超限) | M5 须冻结/降级, 由 M1 外部触发 | DP-A09 |
| SC-A05 | NLP 局部最优/不收敛 | M7 须独立检测并 veto→MRM, 时延<TMR 60s | DP-A08 |

### 0.6 裁决注册表 [VR]
| ID | 裁决对象 | 结论 | 采纳/弃用 | 理由 | 时间 |
|----|----------|------|-----------|------|------|
| (Step4/5 填充) | | | | | |

### 0.7 备选/弃用方案 [ALT]
| ID | 方案 | 弃用理由 | 对比于 |
|----|------|----------|--------|
| (Step4/5 填充) | | | |

### 0.8 技术规约注册表 [TS]
| ID | 类别 | 规约内容 | 单位/定义 | 来源 | 关联DP/接口 | 与现状差异 |
|----|------|----------|-----------|------|-------------|-----------|
| (Step6 填充) | | | | | | |

---

## 参考文献

- [R1] NLM colav_algorithms (id 9387989c): COLAV 战术层应分解为独立 route manager(航线生命周期) + optimizer(轨迹生成); route manager 持 stale_route_max_age 超时门控
- [R2] NLM colav_algorithms: two-tier MPC(NTNU Eriksen/Brekke colav-simulator)——mid-level NLP 长时域 COLREGs 合规 + BC-MPC 10Hz 紧急反应(Rule 17 stand-on 机动)
- [R3] NLM colav_algorithms: Doer-Checker——planner 不持 safety authority, 不可直接发 MRM, 须 emit concern event 给独立 supervisor
- [R4] NLM safety_verification (id 5d3d717a): Simplex/watchdog fail-safe; ODD 由高层外部 manager(AOC/EVAA)门控非 planner 自判; 多独立 checker(1oo2/2oo3 voting)优于单 checker; IEC61508 SIL2/ISO21448 SOTIF/IMO MASS Code
- [R5] NLM colav_algorithms (id 9387989c): 职责分解原则——(1)COLREGs语义隔离(M6)vs数学约束嵌入optimizer,constraint_compiler是翻译边界;(2)GNC预检独立gatekeeper≠optimizer≠route lifecycle;(3)机动生成/尾段延伸/候选仲裁三独立职责;(4)时空分层+Doer-Checker+感知vs决策三原则
- [R6] DOCUMENTED_INTENT: docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md §3(CommittedAvoidanceRoute)/§9.7(TailBuilder)/§9.10(preflight)/§9.12(keep-last门控)/§10(lifecycle八态)/§13(非凸policing)/§14(M6信号契约)
- [R6] DOCUMENTED_INTENT: docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-spec.md §2(职责)/§3(接口)/§4(流程)/§6(降级)
- [R7] PROJECT_FACT: committed_route.hpp:11-39 9态(Idle/CandidateEvaluating/Committed/HeartbeatOnly/KeepLast/Stale/DegradedHold/Released/BcMpcFollow); committed_route.cpp try_revise/hash/should_enter_degraded_hold(45s)
- [R8] PROJECT_FACT: tail_builder.cpp:341-490 build() 完整§9.7; 消费 m6_past_clear/encounter_state/release_predicted + M2 cpa covariance 3σ
- [R9] PROJECT_FACT: bc_mpc_node.cpp:34-38 topic已修正/l3/m2/world_state; :51-56 订阅consecutive_failures; CI full_l3_stack.launch.py:36-41含bc_mpc; 但 launch/m5_mid_mpc.launch.py单节点; self-activation key on CPA validity
- [R10] PROJECT_FACT: committed_route.cpp:392-396 enter_degraded_hold 设safety_concern_event字符串; mid_mpc_solver.cpp:124-125,149-153 spdlog::critical only; 无ROS2 publish到M7
- [R11] PROJECT_FACT: mid_mpc_node.cpp 构造函数无 sub_odd_ 成员; M5 全程无视 /l3/m1/odd_state
- [R12] PROJECT_FACT: gnc_preflight.cpp(通用,validate(),未在mid_mpc_node调用); gnc_avoidance_preflight.hpp(header-only,validate_gnc_avoidance_plan,实际调用版); 两套并存
- [R13] PROJECT_FACT: mid_mpc_node.cpp:287-288 target_heading_delta_deg/cpa_drift_fraction 硬编0.0(注释需persisted history)
- [R14] PROJECT_FACT: l3_msgs/msg/COLREGsConstraint.msg schema115 含 encounter_state/past_clear/release_predicted
- [R15] PROJECT_FACT: l3_msgs/msg/AvoidancePlan.msg schema116 含 segment_source[]/route_hash/L2_HISTORICAL_PREFIX; 旧avoidance_waypoints已删
- [R16] PROJECT_FACT: committed_route.cpp Fix#7 was_degraded_hold重置consecutive_nlp_failures 允许DegradedHold退出
- [R17] PROJECT_FACT: docs/Design/Review/rule14-ho-RED-chain-triage-20260706.md: tail_spacing_invalid 119次(NLP terminal pN.s≈5220m≈route_len, 剩余<spacing_m); crossing_ahead 13次
- [R18] PROJECT_FACT: mid_mpc_node.cpp publish_avoidance_plan_ event-driven+60s heartbeat门控; solve_timer_仍1Hz

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step1 · 行业调研·发现决策点  [2026-07-16]

**模式判定**: 重构。M5 已有完整实现(Mid-MPC + CommittedRouteManager 9态 + TailBuilder + BC-MPC 半活 + 双preflight + 候选回退链)。现有代码/spec 是主证据之一(机制B),外部权威(NLM)用于验证/补强/纠偏。progress.md(2026-06-08)已严重过时,不作基线;以当前代码 + committed-route spec v2 为准。

**与 MPC 对话的边界划分**: 本日志(架构级,DP-Axx)与 `2026-07-16-m5-mpc-design-log.md`(算法级,DP-01~09)互补不重叠。MPC 日志管 optimizer 内部(预测模型/约束/求解器/时域/参考跟踪/回退/不确定性);本日志管子模块职责划分、系统契约、fail-safe/ODD 门控。

**快调来源**:
- NLM domain:colav_algorithms(266 sources) + domain:safety_verification(113 sources),2条查询全 high 置信
- 代码库: 2 个 Explore agent 全量摸底 M5 源码(22805行) + 9份近期spec/plan + 4份评审报告

**主干架构维度提取**(NLM 3轴 + spec/代码现状对照):

1. **COLAV 战术层模块分解** [R1]: route manager 应独立于 optimizer, 持完整航线对象+生命周期+超时门控。现状: CommittedRouteManager 已独立实现9态[R7]——**架构正确,需确认非退化**。
2. **双 MPC 分层** [R2]: mid-NLP(长时域COLREGs合规) + BC-MPC(10Hz紧急, Rule17 stand-on机动)。现状: 分层存在但 BC-MPC 半活(self-activation未完整, production launch单节点)[R9]——**分层架构正确,接管契约有缺陷**。
3. **Doer-Checker fail-safe** [R3]: planner 不持 safety authority, emit concern 给独立 supervisor。现状: MUST-9 断裂, concern event 不离开 M5 进程[R10]——**架构违背, 须修**。
4. **ODD 外部门控** [R4]: 高层 manager(AOC/EVAA)freeze planner, 非 planner 自判。现状: M5 无 ODD 订阅, 全程无视 ODD[R11]——**架构违背(ADR-1), 须修**。
5. **多独立 checker 冗余** [R4]: 1oo2/2oo3 voting 优于单 checker。现状: M7+X-axis 双 checker 设计存在, 但 M7 policing 死代码(Slice K未接线)——**冗余架构正确, 运行时未落地**。

**决策点提取(11个, 分主线子模块定位 DP-A02~A07 + 系统契约 DP-A08~A11)**: 见注册表 0.1。

**技术分解触发(机制C)**: TD-A01 M5子模块架构 → 分解为航线真相源/优化器/尾段延伸器/紧急反应层/候选仲裁/可行性预检/fail-safe链路/ODD门控/语义契约/输出契约 共10个子模块决策点。不可停留在"M5 用双MPC"这一层。

**[用户反馈修正] 自顶向下职责重构**: 用户指出"现有子模块不一定合理",要求从 M5 **职责**调研合适的子功能划分,而非照搬代码目录。补发第3条 NLM 查询 [R5] 获取职责分解原则。据此将决策点组织从"自底向上映射代码目录"重构为"自顶向下按职责维度(A-H)组织":
- 职责 A 机动生成 / B 约束编译(新增独立维度,NLM [R5] 论证其翻译边界) / C 尾段延伸 / D 航线真相源 / E 可执行性预检 / F 候选仲裁降级 / G 紧急反应 / H 系统契约面
- 每个 DP 审查问题改为: 该职责的**边界合理性/所有权归属/契约完整性/遗漏或重叠**, 而非"现有子模块定位对不对"
- 确认 constraint_compiler(职责B)是独立成熟子模块(NLM 论证的翻译边界), 之前漏列, 现补为 DP-A03
- 决策点表见注册表 0.1(已重写); 职责分解框架见 0.0
