# 设计日志: L3 TDL → L4 GNC 跨层对接契约

> **模式**: 重构        **创建**: 2026-07-17
> **关联 spec**: docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md (待 Step6 产出)
> **外部输入**: 附件 `01-L3_L4_GNC_Interface_Contract_Discussion_Draft.md`(用户外部评审草案)
> **既有 spec**: `docs/superpowers/specs/2026-06-27-tdl-gnc-avoidance-interface-contract.md`(主 checkout,已部分 stale)
> **并行设计**: worktree `m5-design-grounding` 的 M5 MPC design(`design-logs/2026-07-16-m5-mpc-colav-design-log.md` + `specs/2026-07-16-m5-mpc-colav-solution-pack.md`)。P0(manifest/Nomoto 几何修正)已完成;P1b-1a staging 已完成;P1b-1b/c(生产 acados backend + Rule14 HO benchmark)实施中(2026-07-17)。
> **状态**: Step6 完成(设计值版);已交付 Spec 同步;SIL 校准后定稿

---

## 0. 决策树状态(权威索引 · 可变快照)

### 0.1 决策点注册表 [DP]

> **组织原则**: 全部决策点围绕**一个核心架构原则 DP-00(L3 控制器无关)**展开。DP-00 是父决策,DP-01~DP-09 是使其落地的子决策(参考原语/能力公布/反馈/约束边界/转换器归属/版本语义/实施路线)。每个子决策的存在理由都是"不裁决它,DP-00 就会退化成 L3 为每种 L4 重复开发"。DP-10/11 是本设计顺带修复的真实断点(BC-MPC 断链、包络漂移),与 DP-00 相关但非其分解。

| ID | 描述 | 类型 | 父/分解 | 状态 | 详见 |
|----|------|------|---------|------|------|
| **DP-00** | **核心原则:L3 TDL 控制器无关**——同一 L3 输出驱动 PID/LOS/ILOS-L4 和 tracking-MPC-L4,L3 不分支判断 controller backend、不硬编任一 L4 内部参数 | 架构(父) | 分解→DP-01..09 | 未决 | Step2 |
| DP-01 | L3→L4 参考原语形态:waypoint / timed trajectory / corridor / guidance-extension(Δχ,Δu) | 架构 | DP-00 子1 | 已裁决 | VR-01 |
| DP-02 | 控制器无关性的具体落地机制:planner 输出统一原语 + L4 各自的 reference adapter,vs planner 分支 vs guidance-extension Δ | 架构 | DP-00 子2 | 已裁决 | VR-02 |
| DP-03 | 承诺前缀 / 相对轨迹跟踪 / material-change 判据——避免"chasing moving trajectory"导致 L3 为每个 L4 重算频率 | 架构 | DP-00 子3 | 已裁决(设计值180s);SIL校准延后 | VR-06 |
| DP-04 | L4 执行能力公布机制(runtime capability msg vs config-only)——消除 L3 硬编 L4 包络(当前 preflight 硬编 0.5/3.5/45m 是反例) | 接口 | DP-00 子4 | 已裁决 | VR-05 |
| DP-05 | accept/reject/degrade 状态语义 + enum——L3 用统一状态机响应,不为每种 L4 写不同反馈处理 | 接口 | DP-00 子5 | 已裁决 | VR-07 |
| DP-06 | Effective trajectory/route 反馈通道——L3 基于实际执行重算 CPA,而非用 L4 私有轨迹表示 | 接口 | DP-00 子6 | 已裁决 | VR-09 |
| DP-07 | COLREGs intent 不可变边界(passing side/direction/past-and-clear)——所有 L4 共享的硬约束,L3 不因 L4 不同而放宽 | 约束 | DP-00 子7 | 已裁决 | VR-10 |
| DP-08 | 轨迹→航点/控制参考转换器归属(L3 side vs L4 reference adapter)——决定 L3 是否要维护多套 L4 私有适配 | 架构 | DP-00 子8 | 已裁决 | VR-04 |
| DP-09 | 实施分阶段路线(Phase 1..4):先兼容当前 PID-L4,再向 MPC-L4 平滑迁移,中途不重写 L3 | 架构 | DP-00 子9 | 已裁决 | VR-11 |
| DP-10 | ReactiveOverrideCmd / BC-MPC → L4 接入路径(当前断链;M5 设计声称"drives L4 directly"但 gnc_bridge/gnc_ws 零消费者) | 架构 | (真实断点) | 已裁决(契约);L4接线归DP-09 | VR-08 |
| DP-11 | M5 preflight 与 GNC overlay 包络参数漂移(lateral_accel 0.5 vs 0.25;yaw 3.5 vs 4.7;注释自标 [TBD-HAZID])——L3 侧硬编 L4 参数的反例修复 | 约束 | (DP-04 的现存证据) | 已裁决(合并入DP-04) | VR-05 |

**已合并**(原细粒度列表中,以下被并入上方以聚焦核心):
- 原 DP-10(plan_id/version)→ 并入 **DP-03**(committed-prefix 语义的一部分,单独成 DP 会割裂)
- 原 DP-11(时序/频率/TTL)→ 并入 **DP-03**(material-change 判据与时序不可分)
- 原 DP-12(bridge 丢 radius/decel 字段)→ 并入 **DP-06**(effective 反馈通道的字段实现细节,Step6 技术规约处理)
- 原 DP-14(实施路线)→ 即 **DP-09**(重命名,语义未变)

### 0.2 流程分解注册表 [FB](L3→L4 控制器无关契约六大模块)

> **组织层**: DP-00(核心原则)→ 六大模块 → 11 个 DP。模块是端到端数据流的自然边界,DP 是模块内的决策。Step2 grilling 按**模块顺序**推进(非 DP 编号),一个模块内 DP 联动 pressure-test。

| 模块 | 职责(为何是大模块) | 归属 DP | 控制器无关命题 | 模块间逻辑关联 |
|------|---------------------|---------|----------------|----------------|
| ① 参考编码 | M5 NLP 解(ψ,r,u,ξ)如何编码成 L4 可消费原语 | DP-01/02/08 | 换 L4 时 M5 输出格式不变;轨迹↔航点转换由 L4 各自 reference adapter 做 | 依赖②(须先知能力才能生成可执行原语) |
| ② 能力协商 | L4 执行包络(yaw/radius/decel/speed)如何被 M5 知晓 | DP-04/11 | M5 不硬编任一 L4 参数(preflight 硬编 0.5/3.5 是反例) | ①的前置;②+①→③ |
| ③ 可行性接受 | L4 如何回应计划 + 计划身份/承诺前缀如何稳定 | DP-03/05/10 | 状态机统一,L4 换型不改 M5 响应逻辑 | 前向链终点+反向链起点;闭合①+② |
| ④ 执行反馈 | L4 实际执行如何回传 M5 重算 CPA | DP-06 | M5 不依赖 L4 私有轨迹表示;短时域前向仿真降带宽 | 闭合整回路→回到① |
| ⑤ 战术意图边界 | passing side/direction/past-and-clear 不可改(横切) | DP-07 | 所有 L4 共享硬约束,不因 L4 不同放宽 | 横向贯穿①③④(声明+接受不违反+M7校验) |
| ⑥ 迁移兼容 | PID-L4→MPC-L4 分阶段路线(纵切) | DP-09 | 迁移期 M5 稳定不重写 | 纵向覆盖①-⑤演进 |

**端到端数据流(代码事实基线)**:
```
前向: M5 NLP → TailBuilder/committed_route → /l3/m5/avoidance_plan(航点)
  → gnc_bridge → /colav/avoidance_plan
  → ActiveRouteManager(可行性门) → /gnc/active_route
  → coordinate_transform(emergency 全旁路平滑) → /ship/waypoints
  → ship_guidance(fly-by/限速) → heading/speed 参考
  → ship_control(PID surge/sway/yaw/speed) → cmd_tau → thrust_allocation → 执行器
反向(残缺): RouteExecutionStatus(含 radius/decel) → gnc_bridge(丢8字段) → /l3/gnc/execution_status → M5/M7/M8
反向(断): ship_guidance /gnc/smoothed_waypoints(实际执行路径) → ❌ 未桥接
断链: BC-MPC /l3/m5/reactive_override_cmd → ❌ gnc_bridge 零订阅 → L4 永远收不到
```

### 0.3 技术分解注册表 [TD]

| ID | 技术 | 分解子模块(→DP) | 触发步骤 |
|----|------|------------------|----------|
| (无技术型决策点触发分解——本任务全部为架构/接口/约束型,答案形态不是"采用某算法/框架") | | | |

注:本任务是接口契约设计,不触发机制C 的技术分解。机制C 上游(M5 MPC 本身的预测模型/求解器/约束层级)已在 2026-07-16 的 M5 design-grounding 中裁决,本任务不重复。

### 0.4 盲区注册表 [BL]

| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-01 | IMO MASS Code / DNV / CCS 是否对 GNC↔COLAV 实时反馈有硬要求(影响 DP-06 是否合规必需) | DP-06 | 高 | 已闭环→[R1][R2][R3] |
| BL-02 | Academic NTNU line 对 "corridor as GNC reference" 的立场(影响 DP-01 corridor 选项) | DP-01 | 中 | 已闭环→[R4][R5] |
| BL-03 | Committed-prefix 长度 N 的工程取值区间(影响 DP-03) | DP-03 | 中 | 已闭环→[R4][R23]:设计值 180s(NLM 推荐 120-180s 取上端),SIL 校准延后 |
| BL-04 | BC-MPC ReactiveOverrideCmd 当前断链是设计意图还是遗漏(影响 DP-09) | DP-09 | 高 | 已闭环(定性)→[R12]+M5 TS-09:设计意图("drives L4 directly")未落地,接线缺失;解决方案归 DP-09 |
| BL-05 | 既有 ship_guidance 的 /gnc/smoothed_waypoints 是否可复用为 effective trajectory(影响 DP-06 实现成本) | DP-06 | 高 | 已闭环(代码侧)→[R9] |
| BL-06 | accept/reject/degrade 是否存在 marine 标准 enum(影响 DP-05 是否自造) | DP-05 | 中 | 已闭环→[R1][R2][R7] |
| BL-07 | M5 preflight 包络漂移是测试覆盖遗漏还是有意保守(影响 DP-13) | DP-13 | 高 | 待 Step3(需读 gnc_avoidance_preflight 注释) |
| BL-08 | relative-trajectory tracking 在既有 ship_guidance(absolute-time waypoint)上是否可落地(影响 DP-03 可行性) | DP-03 | 高 | **已闭环(澄清)**:relative-trajectory tracking t_b 是 M5 内部机制(每周期投影回 nominal route 生成参考),L4 无需感知/实现;L4 仍按收到的 trajectory 跟踪。非盲点 |
| BL-09 | L2 既有 RoutePlan 消息是否应作为 CommittedRoute 的正式兼容接口(影响 DP-01/DP-08) | DP-01 | 中 | 待 Step3 |
| BL-10 | **900s 完整避碰生命周期 vs 360s horizon**:用户仿真观测避碰到回归最长 900s,360s 可能仅覆盖避让阶段。receding horizon + 相对跟踪 t_b 在 horizon < 完整生命周期时如何保证返航收敛?是否需要 horizon 延长(600-900s)或保留某种 post-horizon 延伸(非老 TailBuilder) | DP-02 | **高** | **已闭环→[R20][R21-修订]**:NLM high 证实 horizon<生命周期致 myopia(premature return/chattering/稳定性丢失);收敛保证两路=长horizon 或 终端ingredients;用户裁决**方案A:horizon延长到1200s**(45m FCB 18kn 巡航,20分钟覆盖一般避碰+返航;Johansen 用 600-1200s),保持无终端集→C10/C11 可安全废除 |
| BL-11 | **MPC-L4 执行包络语义**:tracking-MPC 的执行极限(max yaw rate/turn radius/decel)是硬约束还是软代价?若是软代价,M5 preflight 按硬限校验会过保守。决定 capability 消息(GncExecutionOdd)的字段语义如何定义才能同时表达 PID 硬限与 MPC 软代价 | DP-04 | **高** | **已闭环→[R22]**:NLM high 基于 MPC 理论+NTNU实践:物理执行器极限=硬约束(non-relaxable),状态约束才软化;MPC 经 constraint tightening 给上游**保守硬界**(物理硬限−tube margin);**capability 消息语义统一=HARD guaranteed-feasible bound**,PID 发布物理饱和极限,MPC 发布收紧后保守硬界;M5 只读硬界不关心 L4 内部软硬→控制器无关。NTNU SB-MPC 把动力学内嵌规划器原生过滤不可行动作(M5 现状一致) |

### 0.5 证据矩阵 [EV]

| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | 标准/规则 | IMO MSC.595(111) MASS Code §9 | 高 | 高 | 高 | DP-06/DP-07 |
| [R2] | 船级社 | DNV-CG-0264 Autonomous and Remotely Operated Ships | 高 | 高 | 高 | DP-06/DP-07 |
| [R3] | 船级社 | CCS Rules for Intelligent Ships (i-Ship N 模块) | 高 | 高 | 高 | DP-07 |
| [R4] | 学术 | Eriksen 2020 Hybrid COLAV (IEEE T-IV) | 高 | 高 | 高 | DP-01/DP-03/DP-07/DP-08 |
| [R5] | 学术 | Hagen et al. 2018 MPC COLAV for Existing Guidance (ICRA) | 高 | 高 | 高 | DP-02/DP-07 |
| [R6] | 学术 | Kufoalor et al. 2018 Proactive RVO (IROS) | 高 | 高 | 中 | DP-04/DP-05 |
| [R7] | 跨域 | ROS2 Nav2 FollowPath action | 高 | 中 | 中 | DP-05 |
| [R8] | 商业 | Kongsberg K-MATE Autonomy Controller | 中 | 高 | 中 | DP-01/DP-02 |
| [R9] | 代码库 | ship_guidance_node.cpp /gnc/smoothed_waypoints | 高 | 高 | 高 | DP-06 |
| [R10] | 代码库 | gnc_bridge translators.cpp(字段映射 + 丢失) | 高 | 高 | 高 | DP-12/DP-06 |
| [R11] | 代码库 | active_route_manager_node.cpp 可行性公式 | 高 | 高 | 高 | DP-13/DP-04 |
| [R12] | 代码库 | mid_mpc_node.cpp publish_committed_route_ + BC-MPC 断链 | 高 | 高 | 高 | DP-03/DP-09/DP-10 |
| [R13] | 代码库 | gnc-ship-config-overlay.yaml(yaw/decel 已改) | 高 | 高 | 高 | DP-13 |
| [R14] | 代码库 | gnc_avoidance_preflight.hpp(M5 侧保守包络) | 高 | 高 | 高 | DP-13/DP-04 |
| [R15] | NLM colav_algorithms(high) | 控制器无关机制:SB-MPC guidance-extension 偏移量(χ_ca/u_ca 叠加 LOS,结束置零平滑归航,Hagen2018 可 retrofit 不依赖底层动力学)+ Eriksen 双层级联最终输出抽象为连续时间速度+航向轨迹(底层=纯速度航向跟踪器,PID/MPC 通用)+ 工业 RTZ 标准(IEC61174 Annex S)+ 高密度动态航路点(每10s带速度) | 高 | 高 | 高 | DP-00/01/02 |
| [R16] | NLM ship_maneuvering(high) | 执行包络公布=config-only+离线 CADCA 数据库(无 runtime 动态公布标准);反馈靠实时状态向量+短时域前向仿真预测轨迹重算 CPA(Enevoldsen/Blanke;Marley 虚拟 autopilot 前向仿真;He MPC 内部预测=有效轨迹) | 高 | 高 | 高 | DP-04/06 |
| [R17] | NLM maritime_regulations(high) | COLREGs/IMO MASS Code/DNV/CCS 均 goal-based,**不规定** COLAV-GNC 内部契约;immutable intent 是**工程设计选择非法规强制**;S/T/O 分层 + OEDR→autopilot 概念支撑"战术层输出命令值给 autopilot" | 高 | 高 | 高 | DP-07 |
| [R18] | 学术原文 | Eriksen & Breivik "MPC-based Mid-level COLAV for ASVs using NLP"(附 PDF):单一 NLP horizon 同时做避让+返航;reference 始终是 nominal trajectory(相对跟踪 t_b,从不切换成"避让参考");位置误差 Huber 损失(近原点二次/远处线性,防指数回拉);无单独几何尾段模块 | 高 | 高 | 高 | DP-02/DP-07 |
| [R19] | NLM colav_algorithms(high,2026-07-17 ASK) | **Eriksen 返航在 NLP 内部完成,无 TailBuilder**;机制A 相对轨迹跟踪 Eq16 p̄_d(t)=p_d(t+t_b) 每周期投影回 nominal route 找最近点;机制B Huber 损失 Eq20-21 近原点二次远处线性;阶段是涌现的(障碍近→COLREGs势函数主导→偏开;障碍过→势函数衰减→位置误差主导→回归),非离散调度;**TailBuilder 冗余的前提=NLP 能在足够长 horizon 内求解完整返航** | 高 | 高 | 高 | DP-02/DP-07 |
| [R20] | PROJECT_FACT(用户仿真观测,2026-07-17) | 用户 SIL 仿真:完整避碰生命周期(避让→保持→返航回归)最长 **900s**;360s horizon 可能仅覆盖避让阶段,返航需后续迭代覆盖 | 高 | 高 | 高 | DP-02(BL-10) |
| [R21] | NLM colav_algorithms(high,2026-07-17 ASK) | horizon<完整生命周期致 **myopia**:premature return(过早回航CPA不足)/chattering(拓扑切换振荡)/稳定性丢失;收敛保证仅两路=**(a)长horizon覆盖整个避让机动** 或 **(b)终端ingredients(终端集+Lyapunov终端代价)**;无终端集则 horizon 须超避让机动时长;Johansen SB-MPC 用 600s 甚至 1200s(dt=30s/N=40);Eriksen 靠长horizon(非终端集)保收敛 | 高 | 高 | 高 | DP-02(BL-10) |
| [R22] | NLM colav_algorithms(high,2026-07-17 ASK) | MPC capability 语义:物理执行器极限(input constraints)=**硬约束 non-relaxable**(舵角/推力→yaw rate/decel 物理饱和);状态约束(state/output)=常软化带 slack;MPC 经 **constraint tightening** 给上游**保守硬界**(物理硬限−tube margin);capability 消息统一语义=**HARD guaranteed-feasible bound**:PID 发物理饱和极限,MPC 发收紧后保守硬界;M5 只读硬界不关心 L4 软硬→控制器无关;NTNU SB-MPC 把动力学内嵌规划器原生过滤不可行(M5 现状一致) | 高 | 高 | 高 | DP-04(BL-11) |
| [R23] | NLM colav_algorithms(high,2026-07-17 ASK) | 承诺前缀长度:tracking-MPC 最小预测 horizon **90s**(覆盖瞬态+CPA);PID/LOS 需 **≥3 航点**(prev/current/next)+ wheel-over 距离(数倍船长);**60s 对双 L4 都不够**(MPC<90s,PID 航点不足);推荐**承诺前缀 120-180s**(双 L4 满足+planner 60s replan 失败余量);Eriksen:planner 60s replan→承诺前缀须严格>60s,planner 失败则 reuse last solution | 高 | 高 | 高 | DP-03(BL-03) |

### 0.6 场景注册表 [SC]

| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| SC-01 | COLREGs head-on/crossing 主动避碰中,M5 每 60s 重规划,L4 持续跟踪分钟级轨迹 | Mid-MPC 60s 重规划,360s horizon | DP-03/DP-10/DP-11 |
| SC-02 | NLP 失败 → BC-MPC takeover → ReactiveOverrideCmd 需直接驱动 L4 | BC-MPC 10Hz,validity 1-3s | DP-09 |
| SC-03 | L4 接受计划但限速(3.2 cap)→ M5 必须用实际速度重算 CPA | emergency_speed_cap=3.2 | DP-05/DP-06 |
| SC-04 | 同一 plan_id 版本更新不应无条件重置 ship_guidance 跟踪器 | route_update_guard min_interval=2s | DP-10/DP-11 |
| SC-05 | GNC 拒绝(turn_radius_too_small)→ M5 必须确定性 replan 或降级 | ARM feasibility gate | DP-05/DP-12 |
| SC-06 | past-and-clear 后 return-to-route,必须保持 COLREGs 语义(不穿越前方) | return_to_route branch | DP-07/DP-08 |
| SC-07 | 未来 L4 切换为 tracking-MPC 时,同一 L3 输出无需分支判断 | controller-agnostic | DP-02/DP-01 |
| SC-08 | 认证审查需要 L3 计划/L4 执行/实际闭环的可追溯证据链 | CCS i-Ship / IEC 61508 SIL2 | DP-06/DP-07 |
| SC-09 | M5 内部 LifecycleState(MID_NORMAL/BC_TAKEOVER/HANDOVER_NEUTRAL/FINAL_DEGRADE)切换时,L4 须区分"承诺前缀微调"vs"全量重规划"vs"BC 接管" | M5 四状态机 TS-14 | DP-10/DP-09 |

### 0.7 裁决注册表 [VR]

| ID | 裁决对象 | 结论 | 采纳/弃用 | 理由 | 时间 |
|----|----------|------|-----------|------|------|
| VR-01 | DP-01 参考原语形态 | **timed trajectory(时间参数化连续轨迹)为主原语**;各 L4 自带 reference adapter 适配(PID→降采样航点,MPC→直接跟踪) | 采纳 | [R4][R15] Eriksen 连续 SOG+course 轨迹底层=纯跟踪器 PID/MPC 通用;意图保真由 M5 单方保证不依赖 L4 插值;trajectory 是 M5 NLP 解[ψ,r,u,x,y]的自然产物;是航点超集向下兼容。代价:M5 须保证曲率/加速度连续可执行(用户接受为必要代价) | Step2 2026-07-17 |
| VR-02 | DP-02 落地机制 + TailBuilder 去留 | **淘汰老 TailBuilder**;NLP 内部完成返航(相对跟踪 t_b + Huber 损失);废弃 DP-07 人工参考轨迹;M5 单一输出 trajectory(NLP 解直接就是端到端轨迹,无尾段拼接);**horizon 延长 360s→1200s**(BL-10 闭环,方案A) | 采纳 | [R18][R19] Eriksen 单一 NLP 相对跟踪+Huber 内部返航,无几何续貂;[R20] 用户仿真 900s 生命周期;[R21] NLM myopia 证据→方案A 长 horizon | Step2 2026-07-17 |
| VR-03 | DP-02 内部实现(人工参考轨迹/Huber/终端)— 非 DP-07(意图边界,未裁决) | **废弃人工参考轨迹**(因果倒置误读);改 Eriksen 相对跟踪 t_b(reference 始终 nominal);位置代价纯二次→**Huber 损失**;**终端 C10(同侧)/C11(横向)废除**(VR-02 horizon→1200s 长 horizon 保证收敛,不需终端集)。注:此裁决针对 M5 NLP 内部代价/终端,与 M5 设计树 VR-07b 衔接,已同步反馈 M5 实现侧 | 采纳(C10/C11 确认废除) | [R18][R19] Eriksen reference 始终 nominal+Huber;不用终端集靠长 horizon+stage cost;C10/C11 是"防过早归航"误读连带产物;[R21] 长 horizon 是无终端集收敛保证的前提 | Step2 2026-07-17 |
| VR-04 | DP-08 转换器归属 | **trajectory→{航点/LOS参考/控制参考} 转换器归 L4 各自的 reference adapter**(每 L4 一个);M5 单一输出 trajectory 不分支;过渡期可在 M5 仓库内放可迁移 sampler(遵守草案§13.2 六条纪律:独立于 solver、以 capability 为输入、参数不散落、可迁移、独立单测、与 L4 可行性公式镜像验证) | 采纳 | [R15] Hagen retrofit 友好;用户认同"保证 M5 一致性,换 L4 只改 adapter" | Step2 2026-07-17 |
| VR-05 | DP-04 能力公布机制 + DP-11(合并) | **单一真相 = GNC overlay(船厂/HAZID 标定)→ GncExecutionOdd runtime 投射 → M5 preflight 只读订阅值,删除头文件硬编码默认**;一处改全局生效,无漂移;控制器无关(PID/MPC 各发自己的 capability,M5 只读不关心类型)。DP-11(M5 preflight 0.5/3.5 vs GNC 0.25/4.7 漂移)合并为 DP-04 现存反例,机制解决即自动修复。**BL-11(MPC-L4 执行包络语义:硬限 vs 软代价)待 Step3**——决定 capability 消息字段语义 | 采纳(机制);BL-11 待 Step3 | [R16] NLM:config-only+离线CADCA 为常态,但本项目已订阅 /gnc/execution_odd;两处维护是漂移根因,对齐仅治标 | Step2 2026-07-17 |
| VR-06 | DP-03 承诺前缀 / material-change / 相对跟踪 | **承诺前缀 = 180s**(NLM 推荐 120-180s 取上端:双 L4 满足 MPC 90s 最小预测窗口+buffer、PID wheel-over+航点充足;planner 60s replan 失败余量 2 周期 120s);**预测尾段 = 1020s**(1200−180,参考用);**material-change 判据**:role/direction 变 / CPA risk 变 / GNC REJECTED / 偏航超阈 / M7 VETO / 紧急升级;**plan_id 会遇级稳定 + version 递增**(承诺前缀不变→version 不变);**relative-trajectory tracking t_b = M5 内部机制**(每周期投影回 nominal,L4 无需感知,BL-08 澄清非盲点)。**SIL 校准延后**(本周 M5 优化,一周后跑 900s 场景验证双 L4 跟踪稳定) | 采纳(设计值180s);SIL校准延后 | [R4][R15][R23] Eriksen committed prefix + planner-failure reuse;NLM:MPC 最小90s/PID 需3航点+wheel-over/推荐120-180s/planner失败须>60s余量 | Step2 2026-07-17 |
| VR-07 | DP-05 accept/reject/degrade enum | **execution_state enum 复用**(GNC 现有:ACCEPTED/EXECUTING/EXECUTING_WITH_LIMIT/REJECTED/FAIL_SAFE_STOP/COMPLETED + DEFERRED/SUPERSEDED),状态机不变,控制器无关;**reason 词汇表扩展**:waypoint 词汇(segment_too_short/turn_radius_too_small/decel_distance_tight,PID-L4 发)+ trajectory 词汇(curvature_excessive/acceleration_excessive/infeasible_dynamics/solver_fail,MPC-L4 发),M5 统一处理;**M5 确定性响应矩阵**:REJECTED→重规划/降级,EXECUTING_WITH_LIMIT→读 applied 值重算 CPA,ACCEPTED→监控 COLREGs,FAIL_SAFE_STOP→M7 VETO;bridge 字段补全(radius/decel evidence)归 DP-06 | 采纳 | [R10] GNC 现有 enum;[R17] 无 marine 标准 enum,Nav2 最近类比;[R22] MPC 拒绝语义=solver_fail/infeasible | Step2 2026-07-17 |
| VR-08 | DP-10 BC-MPC→L4 override 接入 | **ReactiveOverrideCmd 保留独立 emergency override 通道**(非 trajectory):紧急避碰需直接 heading/speed/ROT 指令,逼 BC-MPC 输出 trajectory 会丧失紧急响应速度(Eriksen BC-MPC 即直接出控制修改);**本设计只定义 override 契约**(语义/时序/优先级/validity 过期回到 trajectory 跟踪而非 hold-last);**实际 L4 侧接线(ship_guidance/ship_control 加 override 仲裁)归 DP-09 迁移兼容下游实施**,L4 内部后续对齐;控制器无关(PID 直接驱动/MPC 作硬约束参考,语义一致) | 采纳(契约);L4接线归DP-09 | [R4][R15] Eriksen BC-MPC 直接控制修改;[R12] 当前断链零消费者;用户:紧急避碰需直接控制指令 | Step2 2026-07-17 |
| VR-09 | DP-06 effective trajectory 反馈通道 | **三层反馈(最小带宽)**:层1 即时状态向量(已有,L4 current_pos/heading/speed/ROT,M2 算即时 CPA,零改动);层2 applied 限幅值 + radius/decel evidence(补全 bridge 丢的 8 字段,M5 知道 EXECUTING_WITH_LIMIT 的 root cause);层3 **短时域前向仿真预测轨迹 30s**(L4 reference adapter 用自己动力学模型发布,M5 只消费重算前瞻 CPA,不维护 L4 模型→控制器无关);**M5/M2/M7 职责**:M2=即时权威(actual state),M5=前瞻(effective trajectory),M7=独立校验两者。不采用全量回传长轨迹(带宽浪费);M5 不维护 L4 动力学模型 | 采纳 | [R16] NLM:短时域前向仿真足够(Marley虚拟autopilot/He MPC内部预测);[R9] smoothed_waypoints 现成可作recent history验证;[R10] bridge丢8字段 | Step2 2026-07-17 |
| VR-10 | DP-07 COLREGs intent 不可变边界 | trajectory 消息含 **safety_intent 元数据块**(immutable:L4 不得改 passing_side/primary_avoidance_direction/active_rules/primary_role/forbidden_regions/minimum_clearance_m/no_return_before)+ **execution_policy**(容差内可调:局部位置/速度/航向/曲率/时间/平滑)+ **advisory**(仅参考:推荐航向速度/非承诺尾段);M7 独立校验 immutable 项未被 L4 effective trajectory 违反。**理由=工程纪律**(防 GNC 改写战术意图致 COLREGs 违规 + 可审计 CCS i-Ship 证据链),**非法规强制**([R17] COLREGs/IMO/DNV/CCS 均 goal-based 不规定内部契约;S/T/O+OEDR 概念支撑) | 采纳 | [R17] NLM maritime_regulations high:goal-based 不规定内部契约,immutable 是工程设计选择;S/T/O 分层 + OEDR→autopilot 支撑 | Step2 2026-07-17 |
| VR-11 | DP-09 实施分阶段路线 | **P1 锁 PID-L4(当前)**:M5 输出 trajectory(新 TimedTrajectory)+ preflight 改读 GncExecutionOdd 删硬编码 + PID-L4 加 reference adapter(trajectory→航点 sampler)+ bridge 补全反馈字段。**P2 契约收敛**:plan_id/version/committed-prefix(180s)语义 + execution_state enum + reason 词汇表 + override 契约定义 + 三层反馈(层2 bridge 补全 + 层3 短时域预测)。**P3 切 MPC-L4**:MPC-L4 加 adapter(直接吃 trajectory)+ capability 发 constraint-tightening 硬界 + 短时域预测复用 MPC 内部预测。**P4 控制器无关验证**:同组 COLREGs 场景跑 PID vs MPC 验证行为一致。**关键:M5 在 P1 一次改到位(trajectory 单一输出+控制器无关),P2/P3 零 M5 改动**=DP-00 兑现。本周 M5 重构与 P1 重叠,先定决策再改 Spec 同步 | 采纳 | 前 5 模块裁决综合;用户确认本周 M5 重构不冲突 | Step2 2026-07-17 |

### 0.8 备选/弃用方案 [ALT]

| ID | 方案 | 弃用理由 | 对比于 |
|----|------|----------|--------|
| ALT-01 | 纯离散航点(无时间,现状简化)作主原语 | 无法表达速度剖面/曲率/ROT/加速度;段内变速靠 L4 插值→意图 dilution;换 MPC-L4 需反向重构连续性 | DP-01 (VR-01) |
| ALT-02 | corridor 作主原语 | [R15] 无任何标准/学术/商业先例;当前 PID-L4 无 corridor consumer;空中楼阁 | DP-01 (VR-01) |
| ALT-03 | guidance-extension 偏移量(Δχ,Δu)作主原语 | 需 L4 暴露标称 LOS 参考→碰 L4 内部违反"不碰 L4"原则 | DP-01 (VR-01) |
| ALT-04 | **老 TailBuilder(几何 hold+rejoin 尾段拼接)** | [R18][R19] Eriksen 范式下冗余:单一 NLP 经相对跟踪 t_b+Huber 损失内部完成返航,无几何续貂;老 TailBuilder 是 DP-07"人工参考轨迹防过早归航"误读的因果倒置补丁 | DP-02 (VR-02) |
| ALT-05 | **DP-07 人工参考轨迹(避让期用避让参考做 J_dist 基准)** | [R18][R19] 因果倒置:Eriksen reference 始终是 nominal(相对跟踪 t_b),从不切换成"避让参考";人工参考是"防过早归航"误读产物,是老 TailBuilder 存在的根因 | DP-07 (VR-03) |
| ALT-06 | M5 头文件硬编码 L4 包络(现状 gnc_avoidance_preflight.hpp:26-29) | 两处各维护一份(M5 头文件 + GNC overlay)必漂移(现状已漂移 0.5/3.5 vs 0.25/4.7);换 MPC-L4 包络语义变,硬编码彻底失效 | DP-04 (VR-05) |
| ALT-07 | M5 侧维护多套 L4 配置(if PID use config_A, if MPC use config_B) | 违反 DP-00 控制器无关,planner 分支判断 controller backend | DP-04 (VR-05) |
| ALT-08 | 全量回传 L4 实际执行长轨迹作 effective feedback | [R16] 带宽浪费,短时域前向仿真预测(10-30s)足够重算 CPA | DP-06 (VR-09) |
| ALT-09 | M5 侧维护 L4 动力学模型做前向仿真预测 | M5 耦合 L4 内部,换 L4 要改模型;违反控制器无关 | DP-06 (VR-09) |

### 0.9 技术规约注册表 [TS]

| ID | 类别 | 规约内容 | 单位/定义 | 来源 | 关联DP/接口 | 与现状差异 |
|----|------|----------|-----------|------|-------------|-----------|
| TS-01 | 坐标系 | 全局 WGS84(lat/lon);当地 NED(原点=本船当前位置);body(x艏前/y左舷) | rad,deg,m | [R6] 与 M5 TS-01 一致 | DP-01/全接口 | 一致(无差异) |
| TS-02 | 符号约定 | 艏向 ψ 右舷(顺时针)正;ROT 右转正;cross-track l 右舷正 | rad | [R6] 与 M5 TS-03 一致 | DP-01/DP-07 | 一致 |
| TS-03 | 物理量单位 | 内部:角度 rad,速度 m/s,距离 m;消息边界:度;trajectory timestamp:ROS2 steady | rad,m/s,m,s | [R6] 与 M5 TS-04 一致 | 全接口 | 一致 |
| TS-04 | 接口语义(M5→L4 主原语) | **TimedTrajectory**:时间参数化连续轨迹,{t, north_m, east_m, heading_deg, speed_mps, yaw_rate_deg_s, curvature_1pm, acceleration_mps2} per step;含 safety_intent(immutable)+ execution_policy(容差)+ segment_source(COMMITTED_PREFIX/PREDICTION_TAIL)+ plan_id+version | 见 VR-01/VR-10 | DESIGN_DECISION[VR-01/10] | M5→L4 | **新建消息(替代 avoidance_plan 航点为主)** |
| TS-05 | 时序约定 | Mid-MPC horizon=**1200s**,dt 可调 10/15/20s(Np=120/80/60 benchmark 定),replan=60s;**承诺前缀=180s**(L4 必跟),预测尾段=1020s(参考);BC-MPC replan=5s | s | DESIGN_DECISION[VR-02/06] | DP-02/DP-03 | **90s→1200s(跨树 VR-06b);承诺前缀新增语义** |
| TS-06 | 数值边界(trajectory 字段) | heading∈[0,360)deg;speed∈[0,max_transit]mps;yaw_rate∈[−rot_max,rot_max];curvature 连续(无尖角);acceleration∈[−decel_max,accel_max];NaN→故障标记 | deg,m/s,rad/s | [R22] capability 硬界 | DP-01/DP-04 | trajectory 须满足 capability 硬界 |
| TS-07 | 接口语义(capability) | **GncExecutionOdd** = HARD guaranteed-feasible bound:PID 发物理饱和极限,MPC 发 constraint-tightening 收紧后保守硬界;字段 emergency_avoidance_speed_cap/cruise_min/max_transit_speed/max_lateral_accel/max_decel/emergency_min_turn_radius/cruise+emergency_max_yaw_rate;QoS TRANSIENT_LOCAL+RELIABLE(latched) | m/s,m/s²,m,deg/s | [R16][R22] DESIGN_DECISION[VR-05] | DP-04,L4→M5 | **M5 preflight 改读订阅删硬编码** |
| TS-08 | 接口语义(反馈层1-3) | 层1 即时状态(position/heading/speed/ROT,已有零改动);层2 applied 限幅值+radius/decel evidence(bridge 补全 8 字段);层3 短时域前向仿真预测轨迹 **30s**(L4 adapter 发布) | 见 VR-09 | DESIGN_DECISION[VR-09] | DP-06,L4→M5 | **层2 补字段+层3 新增** |
| TS-09 | 接口语义(accept/reject) | execution_state enum:ACCEPTED/EXECUTING/EXECUTING_WITH_LIMIT/REJECTED/FAIL_SAFE_STOP/COMPLETED/DEFERRED/SUPERSEDED;reason 词汇表:waypoint(segment_too_short/turn_radius_too_small/decel_distance_tight)+ trajectory(curvature_excessive/acceleration_excessive/infeasible_dynamics/solver_fail) | enum+string | [R10][R17][R22] DESIGN_DECISION[VR-07] | DP-05,L4→M5 | **enum 复用+reason 扩展** |
| TS-10 | 接口语义(override) | ReactiveOverrideCmd 独立 emergency 通道:{heading_cmd_deg, speed_cmd_kn, rot_cmd_deg_s, validity_s(1-3s), trigger_reason};validity 过期→回 trajectory 跟踪(非 hold-last);L4 高优先级覆盖正常跟踪 | deg,kn,deg/s,s | [R4][R15] DESIGN_DECISION[VR-08] | DP-10,M5(BC)→L4 | **契约定义;L4 接线下游** |
| TS-11 | 约束(safety_intent) | immutable(L4 不得改):passing_side/primary_avoidance_direction/active_rules/primary_role/forbidden_regions/minimum_clearance_m/no_return_before;adjustable(execution_policy 容差内):局部位置/速度/航向/曲率/时间/平滑;advisory:推荐航向速度/非承诺尾段 | 见 VR-10 | [R17] DESIGN_DECISION[VR-10] | DP-07,M5→L4 元数据 | **新增 trajectory 元数据块** |
| TS-12 | plan_id/version | plan_id 会遇级稳定(非每 publish 新建);version:承诺前缀不变→version 不变,material-change(role/direction/risk/GNC reject/偏航/M7 VETO/紧急升级)→version+1 | string+uint | DESIGN_DECISION[VR-06] | DP-03,M5→L4 | **version 语义新增** |
| TS-13 | M5/M2/M7 职责 | M2=即时 CPA 权威(actual state 线性投影);M5=前瞻 CPA(effective trajectory 30s 预测);M7=独立校验两者+safety_intent immutable 未被违反 | — | DESIGN_DECISION[VR-09/10] | DP-06/DP-07 | 职责划分明确化 |

---

## 参考文献

- [R1] IMO Resolution MSC.595(111) — Code of Safety for Maritime Autonomous Surface Ships (MASS Code), §9 (VDR/SOLAS V/20 applies to all MASS). 2026. https://www.deutsche-flagge.de/de/redaktion/dokumente/rechtsvorschriften/msc-595-111-mass-code.pdf
- [R2] DNV-CG-0264 — Autonomous and Remotely Operated Ships (class guideline). 2021. https://img.antpedia.com/standard/files/pdfs_ora/20230614/DNVGL/DNV-CG-0264-2021.pdf
- [R3] CCS — Rules for Intelligent Ships (智能船舶规范), i-Ship(N) 模块. https://www.ccs.org.cn/ccswzen/file/download?fileid=202405080712174186
- [R4] Eriksen, B.-O. H. (2020). "Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17." IEEE T-IV. https://pmc.ncbi.nlm.nih.gov/articles/PMC7805726/
- [R5] Hagen, Kufoalor, Brekke, Johansen (2018). "MPC-based Collision Avoidance Strategy for Existing Marine Vessel Guidance Systems." ICRA. https://torarnj.folk.ntnu.no/icra18.pdf
- [R6] Kufoalor, Brekke, Johansen (2018). "Proactive Collision Avoidance for ASVs using A Dynamic Reciprocal Velocity Obstacles Method." IROS. https://torarnj.folk.ntnu.no/IROS18_paper_final.pdf
- [R7] ROS2 Nav2 FollowPath action (accept/reject/feedback 跨域类比). https://docs.nav2.org/configuration/packages/bt-plugins/actions/FollowPath.html
- [R8] Kongsberg K-MATE Autonomy Controller(adaptive waypoint following). https://www.kongsberg.com/news/stories/2017/9/autonomous-obstacle-avoidance/
- [R9] 代码库: `third_party/gnc_ws/src/gnc/ship_guidance/src/ship_guidance_node.cpp:474,1729-1744` — `/gnc/smoothed_waypoints` (nav_msgs/Path,实际执行路径,未 bridge 到 L3)
- [R10] 代码库: `src/sim_workbench/gnc_bridge/src/translators.cpp:35-162` — 字段映射 + radius/decel/heading/course 丢失
- [R11] 代码库: `third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp:338-468` — 可行性公式
- [R12] 代码库: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:500,1414-1452,1683-1982` — M5 输出 + committed route + BC-MPC keep-last 释放走廊 + 断链注释
- [R13] 代码库: `docker/gnc-ship-config-overlay.yaml:632-684` — yaw_rate=4.7 / decel=0.20(与 2026-06-27 spec 的 1.2/2.0 与 0.08 不一致)
- [R14] 代码库: `src/l3_tdl_kernel/m5_tactical_planner/.../gnc_avoidance_preflight.hpp:15-40,169-205` — M5 侧保守包络(lateral_accel=0.5 vs 0.25;yaw=3.5 vs 4.7)
- [R15] NLM domain:colav_algorithms(2026-07-17 ASK,confidence high). 控制器无关机制三路径:(a) SB-MPC guidance-extension 偏移量 χ_ca/u_ca 叠加 LOS 参考,避让结束置零→平滑归航无需重规划;Hagen/Kufoalor/Brekke/Johansen(2018) "MPC-based COLAV for Existing Marine Vessel Guidance Systems" 证明可 retrofit 到既有 GNC,不依赖底层动力学精确先验 → 支持控制器无关但限:简化预测模型不含 LOS 闭环动力学,对侧滑敏感;(b) Eriksen/Bitar/Breivik/Lekkas(2020) "Hybrid COLAV ASVs COLREGs Rules 8 and 13-17" 双层级联:中层 NLP 出修改轨迹→短期 BC-MPC 经 LOS+采样出连续 SOG+course 轨迹给底层控制器,底层=纯速度航向跟踪器,PID/MPC 通用;(c) 工业界 RTZ 标准格式(IEC 61174 Annex S)+ 高密度动态航路点(每10s一点带速度,Burmeister;Safety Assessment MASS Scenario-Based 贝塞尔样条插值);Kongsberg K-MATE 经 ROS/CANBus/OPC-UA/SIMRAD 标准接口对接硬件。核心思想:**COLAV 输出=船舶应有的运动学状态(航向/速度/位置),"如何打舵改转速"完全委托封装在 GNC**。
- [R16] NLM domain:ship_maneuvering(2026-07-17 ASK,confidence high). 执行包络公布:**无 runtime 动态公布标准**,config-only + 离线预算为常态。三法:(a) 静态配置 R_min(Enevoldsen/Blanke/Galeazzi "Informed Sampling-based Collision and Grounding Avoidance",own ship constraints 主接口=min turning radius,航点间距=2×wheel-over);(b) 离散行为候选+简化运动学(Hagen2018 固定候选集,简化 ṅ=R(χ)υ 假设瞬时转向无漂移,执行细节留 autopilot);(c) 离线数据库 CADCA(Marley/Skjetne "4-DOF Azipod" 预算 advance/transfer/MDTC 入库,运行时查表)。反馈重算 CPA:**实时状态向量(位置+速度,线性投影)**最常见;**前向仿真虚拟轨迹**(Marley 虚拟 autopilot 4-DOF 前向仿真预测真实未来轨迹;He "speed adaptive track control MPC" 内部预测=有效轨迹)。结论:**最干净契约=静态几何约束 R_min + 离线水动力限 CADCA,反馈=即时速度/ROT 向量 + 短时域前向仿真轨迹数组**。
- [R17] NLM domain:maritime_regulations(2026-07-17 ASK,confidence high). **无**法规/船级社语言把"通过侧/避让方向/past-and-clear 时机"法定归属 COLAV 层使 GNC 不得改。IMO MASS Code/DNV/CCS 全 goal-based(MASS Code"high-level goals and functional objectives, independent of technology bias",不规定内部 operating model/系统边界/控制语义);COLREGs 适用"船舶整体",人本中心,不涉及 planner-autopilot 内部命令委托。**概念支撑(非强制)**:S/T/O 分层(战术层"plan manoeuvres/decide alteration",操作层"executes orders sent from tactical level, sends orders to autopilot")+ OEDR(战术功能环只输出命令值给 Auto Pilot)。结论:**immutable intent 是工程设计选择,不是合规必需**——但 S/T/O+OEDR 概念强支持此架构。

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step1 · 行业调研·发现决策点  [2026-07-17]

- **模式判定**: 重构。既有实现完整存在(M5 → gnc_bridge → ARM → coordinate_transform → ship_guidance),既有 spec(2026-06-27)+ 用户外部评审草案(附件 MD)。目标是"评审查漏补缺 + 锁定契约",不全盘否定既有成果。既有代码/设计是主证据之一。
- **既有 spec 与代码的重大偏差已识别**(5 条,均影响决策基础):
  1. 2026-06-27 spec + 外部草案仍写 `/l3/m5/avoidance_waypoints` / `AvoidanceWaypoints.msg`;代码实际是 `/l3/m5/avoidance_plan` / `l3_msgs/AvoidancePlan`,后者 schema_version 已到 116 含审计字段。`AvoidanceWaypoints.msg` 已 vestigial。
  2. 2026-06-27 spec 把 M5 描述为"5 个 collinear 航点从当前位置 rolling 生成";代码已重构为**会遇锚定冻结走廊**(12 点阶梯 {0,150,300,600,1000,1500,2200,3200,4500,6000,7500,9000}),`plan_id` 会遇级稳定,hash-gated 去重。rolling-anchor 风险已基本消除。
  3. 2026-06-27 spec 的 yaw_rate 1.2/2.0 deg/s、decel 0.08 已被 overlay 改为 **4.7/4.7 deg/s、0.20 m/s²**(注释引用 IMO MSC.137(76) 与 NLM crash-astern 下界)。spec 里 91.7m@3.2m/s 的半径算例已失效。
  4. **M5 preflight 与 GNC overlay 包络漂移**:M5 用 lateral_accel=0.5 / yaw=3.5,GNC 用 0.25 / 4.7。M5 自验证比 GNC 实际执行的横向加速度更松、偏航率更紧 → preflight 通过不代表 GNC 通过(反之亦然)。header 注释自己标了 `[TBD-HAZID]`。
  5. **BC-MPC → L4 断链**:`mid_mpc_node.cpp:1414` 注释声称 "BC-MPC drives L4 directly via ReactiveOverrideCmd",但 `/l3/m5/reactive_override_cmd` 在 gnc_bridge 与整个 third_party/gnc_ws 中**零消费者**。BC-MPC takeover 时 M5 只能发空 AvoidancePlan "释放走廊",实际机动命令无处落地。这是真实的契约断点,不是文档问题。
- **bridge 字段丢失确认**(与 2026-06-27 spec §"Contract gap" 一致):`translators.cpp:135-162` 丢失 `required_turn_radius_m / estimated_available_turn_radius_m / required_decel_distance_m / available_decel_distance_m / suggested_min_distance_m / requested_heading_deg / applied_heading_deg / current_course_deg`。L3 看不到 GNC 的 radius/decel 证据。
- **已有但未利用的 effective route**:`ship_guidance_node.cpp:474,1729-1744` 发布 `/gnc/smoothed_waypoints`(nav_msgs/Path,含 turn angle in position.z、speed override in orientation.z、is_arc_point in orientation.w),这是**实际执行的本地路径**,但 bridge 不转发、spec 不提。
- **行业/标准关键发现**(支持/削弱外部草案的部分论断):
  - 外部草案"TimedMotionPlan 主接口 + MotionCorridor + CommittedRoute"五消息方案:**部分有据,部分超前**。学术 NTNU line(Eriksen/Hagen/Kufoalor)支持"短时参数化轨迹 + 承诺前缀 + 相对轨迹跟踪";但**"corridor as GNC reference" 在任何标准/学术/商业系统中均无先例**(BL-02 已闭环),corridor 是设计选项不是既成实践。DNV/IMO/CCS 对 COLAV↔GNC 实时反馈(BL-01)、capability advertisement(BL-06)、accept/reject enum(BL-06)均**无标准**,这几项是该设计在"定义"而非"遵从"。
  - Hagen 2018 的 **guidance-extension 模式**(COLAV 输出 Δχ/Δu 修正既有 guidance 参考而非替换)是"控制器无关"的强证据(R5),支持 DP-02 不走 planner 分支。
  - Eriksen 2020 的**承诺前缀 6 步/60s + 相对轨迹跟踪 + solver-fail hold-last** 是 DP-03 的直接先例(R4)。
  - Hagen 2018 + Kufoalor 2018 明确记录 **two-layer cascade chattering**(guidance re-intercept → COLAV re-issue → 振荡)是本接口首要失效模式,修法是把 guidance 行为纳入 COLAV 预测 + transitional cost(R5/R6)。
  - 商业实践(Kongsberg K-MATE):**adaptive waypoint following 是 de-facto 接口**(R8),与学术轨迹派并存。
- **新增决策点**: DP-01..DP-14(14 个),全部为架构/接口/约束型,不触发机制C 技术分解(技术分解已在 2026-07-16 M5 design-grounding 完成)。
- **新增盲区**: BL-01..BL-09。BL-01/02/03/05/06 已在 Step1 调研中闭环;BL-04/07/08/09 需 Step3 代码深读。
- **新增场景**: SC-01..SC-08。

#### Step1 续 · 与 worktree M5 设计最新裁决的协调  [2026-07-17 · 用户提示后]

用户提示:最新 M5 设计在 worktree `m5-design-grounding`,P0 已完成、P1b 实施中,可能与代码/旧设计有出入,以最新 Design 为准。已读 M5 方案包 + P1b-1 spec + P1b-1b/c plan + design-log 0.8 TS 注册表 + Step6。协调结论:

**1. M5 P1b acados 迁移 / Path B 双积分器 dynamics 不影响本设计树的决策点。**
- P1b-1 spec §输出契约 + plan Global Constraints 明确:"输出契约不变,acados backend 必须产出与 IPOPT 相同的 MidMpcSolution 字段;trajectory 的 ψ/u/x/y 序列从 acados 状态/控制重构;**下游(M4/L4/tail_gate 检查)收到的字段与 IPOPT 一致,无感知 backend 切换**"。
- 即 P1b 是 **M5 solver 内部** 重构(状态变量/dynamics/求解器),**对外仍输出同一 AvoidancePlan 航点序列**。本 L3→L4 契约设计面对的接口形态(waypoint/timed/corridor)、反馈通道、capability、accept/reject enum 全部不受影响。
- 结论:本设计树 14 个决策点全部仍然有效,无矛盾,无需重做 Step1 发现。

**2. M5 设计的 TS-09 "ReactiveOverrideCmd(BC 接管时)→L4" 是设计意图,不是已实现事实 → 强化 DP-09。**
- M5 方案包术语表 + TS-09 + TS-14(四状态交接机)声称:BC-MPC takeover 时发 ReactiveOverrideCmd → L4。
- 但代码调查([R12],agent 完整证据)证实:`/l3/m5/reactive_override_cmd` 在 gnc_bridge 与整个 third_party/gnc_ws **零消费者**;BC-MPC takeover 时 M5 只能发空 AvoidancePlan(`command_source="m5_bcmpc_override"`,status="BcMpcFollow")"释放走廊",机动命令无处落地。`mid_mpc_node.cpp:1414` 注释 "BC-MPC drives L4 directly via ReactiveOverrideCmd(架构 §L4)" 与实际接线不符。
- 这是 M5 设计的**未实现接口意图**,不是矛盾 —— 恰恰证实 DP-09 是真实断点。本设计树须把"ReactiveOverrideCmd 如何真正到达 L4 ship_guidance/ship_control"作为 DP-09 的核心裁决内容(裁决后,这条接线是本设计的**产出物**,反馈给 M5 实现侧)。

**3. M5 设计的四状态交接机(TS-14)与 L3→L4 契约的 plan_id/version/committed-prefix 语义(DP-10)需对齐。**
- M5 内部 `LifecycleState`:`MID_NORMAL → BC_TAKEOVER → HANDOVER_NEUTRAL → FINAL_DEGRADE`(committed_route.cpp)。
- 对外:这些状态目前通过 AvoidancePlan 的 `command_source`/`behavior_mode`/`status` 字段隐式表达,无显式的 plan version / committed_until 语义。
- DP-10 须裁决:是否把 M5 内部 LifecycleState 映射为对外可见的 plan version / committed_until 字段,使 L4 能区分"承诺前缀更新"vs"全量重规划"。这是两设计树的接口对齐点。

**4. P1b 的 `MidMpcSolution` 输出契约字段(status/trajectory[N]/cost_*/cpa_slack/solve_duration_ms/ipopt_iterations)是 L4 不可见的 L3 内部** —— 确认 M5→L4 之间仍有"内部轨迹→对外 AvoidancePlan 航点"的转换层(TailBuilder/committed_route),本设计 DP-08(转换器归属)仍有效。

**5. 已闭环盲区 BL-04**:BC-MPC 断链是**设计意图未落地**(非遗漏),代码注释自证,M5 设计树明确声称"drives L4 directly"但接线缺失。BL-04 状态:已闭环(定性),解决方案归 DP-09 裁决。

**对 Step1 决策点表的修订**:无增删,14 个 DP 全部有效。新增 1 个场景 SC-09(M5 LifecycleState↔对外 plan version 映射),归 DP-10。

#### Step1 续 · NLM 领域知识补全(controller-agnostic 契约核心)  [2026-07-17 · 用户要求用 NLM]

用户聚焦核心目标:**规范 M5(L3 战术避碰层)对外接口,使 L4 GNC 组件(PID↔MPC)替换时 L3 不跟着频繁改**。已用 NLM ASK 三 domain(colav_algorithms / ship_maneuvering / maritime_regulations,均 high confidence)补全。关键领域知识:

**A. 控制器无关的两条公认路径([R15])**
1. **SB-MPC guidance-extension 偏移量**:COLAV 输出 χ_ca/u_ca 叠加在标称 LOS 参考上,避让结束置零→平滑归航无需重规划。Hagen2018 证明可 retrofit 到既有 GNC,**不依赖底层动力学精确先验**。→ 强支持 DP-02 不走 planner 分支。
   - **限(性能/工程)**:MPC 内部简化预测模型**不含 LOS 闭环动力学**,复杂环境预测轨迹与实际偏差;仅输出偏移对侧滑敏感(需底层 ILOS 配合)。
2. **Eriksen 双层级联**:最终输出抽象为**连续时间 SOG+course 轨迹**,底层视作纯"速度航向跟踪器",PID/MPC 通用。→ 支持 DP-01 选 timed/连续轨迹原语。
3. **工业 RTZ 标准(IEC 61174 Annex S)+ 高密度动态航路点(每10s带速度)**:Kongsberg K-MATE 经标准接口(ROS/CANBus/OPC-UA)对接。→ 支持"航点+速度剖面"作为跨控制器统一基元的工程可行性。

**B. 执行包络公布=config-only+离线预算,无 runtime 标准([R16])**
- 无 runtime 动态公布标准;静态配置 R_min + 离线 CADCA 数据库为常态。
- **性能启示**:runtime capability message 是设计**扩展**非遵从既有实践——意味着 DP-04 若选 runtime 公布,L3 须承担"定义+维护"成本,且不能引用现成标准。

**C. 反馈=实时状态向量+短时域前向仿真([R16])**
- 最干净契约:静态 R_min + 离线 CADCA;反馈=即时速度/ROT 向量 + 短时域前向仿真轨迹数组(虚拟 autopilot 4-DOF 前向仿真 = effective trajectory 的文献原型)。
- **性能启示**:DP-06 effective trajectory 不必是"L4 全量回传实际轨迹",**短时域前向仿真预测**(L3 侧或 L4 侧)即可满足重算 CPA。这降低 L4 反馈带宽需求。

**D. immutable intent 是工程设计选择,非法规强制([R17])**
- COLREGs/IMO/DNV/CCS 均 goal-based,不规定 COLAV-GNC 内部契约。但 S/T/O 分层 + OEDR→autopilot 概念强支持此架构。
- **工程启示**:DP-07 的"COLREGs intent 不可变"是**架构纪律**(防 GNC 改写战术意图),不是合规义务——裁决理由须是工程合理性(可审计/可验证/防 chattering),不能声称"标准要求"。

**对决策点的精化影响**:DP-01/02 证据增强(guidance-extension + 连续轨迹 + RTZ 三路径都有文献支持);DP-04 应倾向 config+capability snapshot 而非 runtime 动态;DP-06 应考虑"短时域前向仿真"作 effective trajectory 的轻量实现;DP-07 理由从"合规"改为"工程纪律"。

### Step2 · grilling 压力测试  [2026-07-17 · 按模块顺序推进]

#### 模块 ① 参考编码

**DP-01 参考原语形态** — 三视角 grilling 后用户裁决(VR-01):
- 专家:[R4][R15] Eriksen 连续轨迹底层=纯跟踪器;新手:当前已用航点为何不维持;悲观:timed 喂 PID-L4 要转换器,corridor 无 consumer,guidance-extension 碰 L4 内部。
- 默认最简版(纯离散航点)失效:段内变速无法表达,MPC-L4 需反向重构连续性→航点 dilution。
- **用户裁决:选 B timed trajectory**。理由:M5 单方保证核心意图完整保留,不同 GNC 仅需补充各自 adapter;接受"保证曲率/加速度连续可执行"为必要代价。
- 弃用:ALT-01(纯离散航点)/ALT-02(corridor 作主原语)/ALT-03(guidance-extension 作主原语)。
- **遗留关联**:DP-01 选 trajectory 使 DP-02(落地机制)和 DP-08(转换器归属)成为强耦合——trajectory→航点/控制参考的转换器归 L4 adapter。

**DP-02 落地机制 + TailBuilder 去留 + DP-07(人工参考/Huber/终端)** — 用户质疑"360s 时域下 TailBuilder 几何续貂是否冗余"触发高置信度三方查证:
- 三方证据一致([R18] Eriksen PDF 原文 + [R19] NLM high + session sess_b504f293):**Eriksen 返航在单一 NLP 内部完成,无几何尾段模块**。机制:相对轨迹跟踪 Eq16 p̄_d(t)=p_d(t+t_b) 每周期投影回 nominal route(reference 始终是原始航线,从不切换成"避让参考")+ Huber 损失 Eq20-21(近原点二次/远处线性,防被障碍推开时指数回拉)。阶段是**涌现**的(障碍近→COLREGs 势函数主导→偏开;障碍过→势函数衰减→位置误差主导→回归),非离散调度。
- **主代理前一轮错误已诚实更正**:曾论断"TailBuilder 填补 NLP 因防过早归航故意留出的空白"——因果倒置。真相:DP-07"人工参考轨迹防过早归航"是对 Eriksen 范式的**误读**(Eriksen 用相对跟踪+Huber,非人工避让参考),老 TailBuilder 是这个误读的补丁。
- **用户裁决(2026-07-17)**:
  1. 淘汰老 TailBuilder,改 Huber + 相对跟踪 + 废弃人工参考轨迹(VR-02/VR-03)
  2. **关键风险[R20]**:用户 SIL 仿真观测完整避碰生命周期最长 **900s**,360s horizon 可能仅覆盖避让阶段,返航需后续迭代覆盖 → BL-10(高优先,Step3 必查)
  3. 终端 C10/C11 同意调研后废除,留决策痕迹(VR-03 标"降为辅助并调研后废除")
- **DP-08 转换器归属**:用户认同归 L4 reference adapter(M5 一致性,换 L4 只改 adapter)。
- **模块①裁决链**:DP-01(VR-01 trajectory 主原语)→ DP-02(VR-02 淘汰 TailBuilder,NLP 内部返航)→ DP-08(归 L4 adapter)。M5 输出流程简化为:NLP 解[ψ,r,u,x,y]→ 直接 trajectory → preflight → publish(无尾段拼接)。
- **BL-10 未闭环** = DP-02 方向已定但 horizon 充分性(360s vs 900s)待 Step3。这是淘汰 TailBuilder 安全性的最终判据。

**BL-10 闭环 + 模块①收口(2026-07-17 用户最终裁决)**:
- NLM[R21] high 证实 horizon<生命周期致 myopia,收敛保证仅两路(长 horizon 或 终端 ingredients)。
- **用户裁决方案 A:horizon 360s→1200s**(45m FCB 18kn 巡航,20 分钟覆盖一般避碰+返航;Johansen 用 600-1200s 实证可行)。保持无终端集 → C10/C11 安全废除(VR-03 确认)。
- **跨树同步已完成**:M5 设计树 log + solution pack 已追加"跨树反馈修订"VR-06b(horizon 1200s)/VR-07b(废弃人工参考+Huber+废除C10/C11+淘汰TailBuilder),append-only 不抹原裁决。
- **模块①全部闭环**:DP-01(VR-01 trajectory 主原语)+ DP-02(VR-02 淘汰 TailBuilder,NLP 内部端到端,horizon 1200s)+ DP-08(VR-04 转换器归 L4 adapter)+ VR-03(废弃人工参考+Huber+废除C10/C11)。
- **M5 输出流程最终态**:NLP 解[ψ,r,u,x,y](1200s horizon,相对跟踪 t_b+Huber,无终端集)→ 直接 TimedTrajectory(单一真相,含避让+保持+返航完整生命周期)→ preflight → publish。无 TailBuilder,无尾段拼接,无人工参考。L4 adapter 各自适配(PID→降采样航点,MPC→直接跟踪)。

#### 模块 ② 能力协商

**DP-04 能力公布机制 + DP-11(合并)** — 三视角 grilling 后用户裁决(VR-05):
- 现状问题:[R14] M5 preflight 硬编 L4 包络(gnc_avoidance_preflight.hpp:26-29,lateral_accel=0.5/yaw=3.5),GNC overlay 实际(0.25/4.7),两处维护必漂移;注释自标 [TBD-HAZID]。
- 默认最简版(改硬编码值对齐)失效:今天对齐明天又漂,换 MPC-L4 彻底失效;根因=两处各维护一份。
- **用户裁决:单一真相 = GNC overlay → GncExecutionOdd runtime 投射 → M5 preflight 只读订阅,删硬编码**;拒绝硬编码(ALT-06 弃用),拒绝多套配置分支(ALT-07 弃用)。DP-11 合并入 DP-04 作现存反例。
- **BL-11(MPC capability 语义)闭环→[R22]**:NLM high 基于 MPC 理论+NTNU实践。物理执行器极限=硬约束(non-relaxable);状态约束才软化;MPC 经 constraint tightening 给上游保守硬界。**capability 消息统一语义 = HARD guaranteed-feasible bound**:PID 发物理饱和极限,MPC 发收紧后保守硬界。M5 只读硬界不关心 L4 软硬→控制器无关。NTNU SB-MPC 把动力学内嵌规划器(M5 现状一致)。
- **遗留 Step3 核实**:GncExecutionOdd 的 QoS 是否 latched/transient_local(M5 启动晚于 GNC 要能收最新 snapshot);capability 运行时热改通知机制。

#### 模块 ③ 可行性接受

**DP-03 承诺前缀 / material-change / 相对跟踪** — 三视角 grilling 后用户裁决(VR-06):
- 用户关键洞察:承诺前缀长度不能只看 M5 replan 周期,必须看 **L4 跟踪需求**——PID 对航点有距离/数量要求(太短剩 1-2 点不够),MPC 需更长预测窗口。这是 L4 类型相关,正是控制器无关要解的。
- **BL-03/BL-08 闭环→[R23]**:NLM high。MPC 最小预测 horizon 90s;PID/LOS 需≥3 航点+wheel-over 距离;**60s 对双 L4 都不够**;推荐 120-180s;Eriksen planner 60s replan→承诺前缀须>60s 且 planner 失败 reuse last solution 需余量。
- **用户裁决:承诺前缀 180s**(取区间上端:双 L4 满足 + planner 失败 2 周期余量 120s);预测尾段 1020s;material-change 判据(role/direction/risk/GNC reject/偏航/M7 VETO/紧急升级);plan_id 会遇级稳定+version 递增;relative-trajectory tracking t_b=M5 内部机制 L4 无需感知(BL-08 澄清)。
- **SIL 校准延后**:本周 M5 优化为主,一周后跑 900s 场景验证双 L4 跟踪稳定。设计值 180s 不阻塞推进。

**DP-05 accept/reject/degrade enum** — 三视角 grilling 后用户裁决(VR-07):
- 现状:GNC RouteExecutionStatus 已有 execution_state(ACCEPTED/EXECUTING/EXECUTING_WITH_LIMIT/REJECTED/FAIL_SAFE_STOP/COMPLETED)+reason+suggested_action;但 bridge 丢 8 字段(radius/decel/heading/course);NLM[R17]确认无 marine 标准 enum。
- **用户裁决:enum 复用(状态机不变)+ reason 词汇表扩展**(trajectory 词汇 curvature_excessive/acceleration_excessive/infeasible_dynamics/solver_fail 补 waypoint 词汇)+ M5 确定性响应矩阵 + bridge 字段补全归 DP-06。控制器无关(PID/MPC 发不同 reason,M5 统一处理)。

**DP-10 BC-MPC→L4 override 接入** — 三视角 grilling 后用户裁决(VR-08):
- 现状:BC-MPC 发 /l3/m5/reactive_override_cmd(heading/speed/ROT,validity 1-3s,10Hz),gnc_bridge/gnc_ws 零消费者;takeover 时只能发空 AvoidancePlan 释放走廊,机动命令无处落地([R12])。
- **用户裁决:保留独立 emergency override 通道(非 trajectory)**;紧急避碰需直接控制指令,逼 BC-MPC 出 trajectory 会丧失响应速度;**本设计只定义 override 契约**(语义/时序/优先级/validity 过期回 trajectory 跟踪),**L4 侧接线归 DP-09 迁移兼容下游**,L4 内部后续对齐。
- **模块③全部闭环**:DP-03(VR-06 承诺前缀180s/material-change)+ DP-05(VR-07 enum复用+reason扩展)+ DP-10(VR-08 override独立通道,契约定义,L4接线下游)。

#### 模块 ④ 执行反馈

**DP-06 effective trajectory 反馈通道** — 三视角 grilling 后用户裁决(VR-09):
- 现状问题:[R10] bridge 反向映射丢 8 字段(radius/decel/heading/course evidence),M5 只看 applied_speed 不知道为何限;[R9] ship_guidance 已发 /gnc/smoothed_waypoints(实际执行路径)但未桥接。
- **用户裁决:三层反馈(最小带宽)**:层1 即时状态向量(M2 算即时 CPA,已有零改动)+ 层2 applied 限幅值+radius/decel evidence(补全 bridge)+ 层3 短时域前向仿真预测轨迹 **30s**(L4 reference adapter 用自己动力学模型发布,M5 只消费,不维护 L4 模型→控制器无关)。
- **M5/M2/M7 职责**:M2=即时权威(actual state),M5=前瞻(effective trajectory),M7=独立校验。
- 弃用:ALT-08(全量回传长轨迹,带宽浪费)/ALT-09(M5 侧维护 L4 动力学模型,耦合)。

#### 模块 ⑤ 战术意图边界 + 模块 ⑥ 迁移兼容

**DP-07 COLREGs intent 不可变** — 用户裁决(VR-10):trajectory 消息含 safety_intent 元数据块(immutable:L4 不得改 passing_side/avoidance_direction/active_rules/role/forbidden_regions/min_clearance/no_return_before)+ execution_policy(容差内可调)+ advisory(参考);M7 独立校验。**理由=工程纪律非法规强制**([R17] goal-based 不规定内部契约,诚实标注)。

**DP-09 实施分阶段路线** — 用户裁决(VR-11):P1 锁 PID-L4(M5 输出 trajectory + preflight 读 capability + PID-L4 adapter + bridge 补全)/ P2 契约收敛(version/enum/override/反馈)/ P3 切 MPC-L4(MPC adapter + capability 硬界)/ P4 控制器无关验证。**M5 P1 一次改到位,P2/P3 零 M5 改动 = DP-00 兑现**。本周 M5 重构与 P1 重叠,先定决策再改 Spec 同步。

**Step2 全部闭环(2026-07-17)**:11 个决策点全部裁决(DP-00 父原则 + DP-01..11),归属 6 大模块全部完成。落盘 VR-01..VR-11 + ALT-01..09。BL-01..11 多数闭环,残余 Step3 核实项:SIL 校准承诺前缀(一周后)、GncExecutionOdd QoS、capability 热改通知。

### Step3 · 残余盲区核实  [2026-07-17]

代码核实三个残余盲区(agent 完整证据):

**Q1 GncExecutionOdd QoS** — ✅ **已 LATCHED**。`active_route_manager_node.cpp:140-142` QoS=TRANSIENT_LOCAL+RELIABLE+KeepLast(1),构造函数即 `publish_execution_odd()`。M5 启动晚于 GNC 立即收最新 snapshot。**机制已就绪,无需改 QoS**。注:bridge 跨 domain 42→50 是否保端到端 TRANSIENT_LOCAL 待确认(M5 侧 `mid_mpc_node.cpp:492-498` 订阅也请求 TRANSIENT_LOCAL+RELIABLE,匹配)。

**Q2 热改通知** — ❌ **不支持**。ODD 参数启动读一次(`active_route_manager_node.cpp:87-108`),`publish_execution_odd()` 只构造函数调一次(line 142),无 `add_on_set_parameters_callback`,无 param-change 重发。改 overlay 须重启节点。**真实 gap,但优化项非阻塞**(船厂标定包络通常不 mid-mission 改)。标 Step3 核实项闭环:当前不支持,P1 实现时若需 mid-mission 感知再加 param-change callback + 重发。

**Q3 M5 用订阅值 vs 硬编码** — ⚠️ **混合(精确反例定位)**。
- TailBuilder + MPC 输入**用了**订阅值:`mid_mpc_node.cpp:1611-1620`(TailBuilder 包络)、`:774/776`(MPC rot_max/decel)经 `effective_gnc_odd_()`(:2162-2179)。
- **GNC preflight 用硬编码** `GncAvoidancePreflightConfig{}`(0.5/3.5/45/0.20),4 个调用点从不收订阅值:`mid_mpc_node.cpp:1942-1944`(degraded preflight)、`:2052-2053`(return preflight)、`mid_mpc_waypoint_generator.cpp:144-146`(canonical route preflight)、`gnc_avoidance_preflight.hpp:27-29`(默认值)。
- **诚实修正**:之前"M5 完全没用订阅值"不准确,真相是部分用(TailBuilder/MPC)preflight 没用。DP-04/DP-11 修复点 = preflight 4 调用点改传 `effective_gnc_odd_()`。

**Step3 残余项**:SIL 校准承诺前缀 180s(延后一周,用户跑 900s 场景)。其余盲区全部闭环。

### Step4 · 汇总分析·推荐方案  [2026-07-17]

按模块综合 11 个裁决(VR-01..11)+ 证据链 + 弃用 + 风险量化。技术分解完整性:本任务无 TD(技术型)分解,11 DP 全为架构/接口/约束型,无 DECOMPOSITION_INCOMPLETE。

#### 综合推荐表(供 Spec 同步用)

| 模块 | DP(VR) | 推荐(一句话) | 证据链 | 弃用(ALT) | 实现风险 | 失效边界 |
|------|---------|--------------|--------|-----------|---------|---------|
| ① | DP-01 (VR-01) | M5 输出 timed trajectory 主原语 | [R4][R15] Eriksen 连续轨迹 PID/MPC 通用 | ALT-01/02/03 | 中:M5 须保曲率/加速度连续 | 非连续→L4 拒绝 |
| ① | DP-02 (VR-02) | 淘汰 TailBuilder,NLP 内部端到端返航(horizon 1200s) | [R18][R19][R21] Eriksen 相对跟踪+Huber,长 horizon 保收敛 | ALT-04/05 | 高:hubris horizon 计算量 | 1200s 实时性靠 acados |
| ① | (VR-03) | 废弃人工参考→相对跟踪 t_b+Huber;废除 C10/C11 | [R18][R19][R21] | — | 低 | — |
| ① | DP-08 (VR-04) | trajectory→各形态转换器归 L4 reference adapter | [R15] Hagen retrofit | — | 低:过渡期 sampler | — |
| ② | DP-04/11 (VR-05) | 单一真相 GNC overlay→GncExecutionOdd,M5 只读删硬编码 | [R16][R22] config 常态;本项目已订阅 | ALT-06/07 | 低:QoS 已 latched | 热改不支持(优化项) |
| ③ | DP-03 (VR-06) | 承诺前缀 180s + material-change + version | [R4][R23] Eriksen+NLM | — | 中:SIL 校准 | 180s 设计值待 SIL |
| ③ | DP-05 (VR-07) | enum 复用 + reason 词汇表扩展 | [R10][R17][R22] | — | 低 | — |
| ③ | DP-10 (VR-08) | override 独立通道,契约定义,L4 接线下游 | [R4][R15][R12] | — | 低(契约) | L4 接线是下游 |
| ④ | DP-06 (VR-09) | 三层反馈(即时+applied补全+30s L4 adapter 预测) | [R16][R9][R10] | ALT-08/09 | 中:L4 adapter 加预测器 | — |
| ⑤ | DP-07 (VR-10) | safety_intent immutable + execution_policy + M7 校验 | [R17] 工程纪律非法规 | — | 低 | — |
| ⑥ | DP-09 (VR-11) | P1-P4,M5 一次改到位 P2/P3 零改动 | 前 5 模块综合 | — | 低(路线) | — |

#### 关键风险与缓解

| 风险 | 级别 | 缓解 |
|------|------|------|
| 1200s horizon 实时性(IPOPT 不可承受) | 高 | acados RTI(VR-05)正是为此;P1b 迁移中 |
| 承诺前缀 180s 未 SIL 验证 | 中 | 一周后跑 900s 场景双 L4 校准 |
| L4 adapter(PID→航点 sampler)是新代码 | 中 | 草案§13.2 六条纪律;独立单测+与 GNC 可行性公式镜像验证 |
| L4 短时域预测器(层3)是新代码 | 中 | L4 adapter 负责,M5 不维护;PID 用其模型,MPC 复用内部预测 |
| 热改包络不支持(Q2) | 低 | 优化项非阻塞;船厂标定不 mid-mission 改;需要时加 param-change callback |
| bridge 跨 domain TRANSIENT_LOCAL 端到端未确认 | 低 | Step3 已查 M5 侧订阅匹配;bridge 侧待确认 |

#### Spec 同步指引(用户改 Spec 时的关键改动点)

**M5 侧(P1,本周重构重叠)**:
1. NLP horizon 360s→**1200s**(VR-02/跨树 VR-06b)
2. 废弃人工参考→**相对跟踪 t_b + Huber 损失**(VR-03/跨树 VR-07b)
3. 废除终端 C10/C11(VR-03)
4. **淘汰 TailBuilder**(VR-02)
5. preflight 4 调用点改传 `effective_gnc_odd_()` 删硬编码(VR-05,Step3 精确定位)
6. 新 trajectory 输出(NLP 解直接 trajectory,VR-01)

**接口侧(P1-P2)**:
7. 新建 `l3_msgs/TimedTrajectory` 消息(含 safety_intent/execution_policy/segment_source/plan_id+version)
8. bridge 补全反向反馈 8 字段(radius/decel/heading/course)(VR-09 层2)
9. execution_state enum 复用 + reason 词汇表扩展(VR-07)
10. override 契约定义(VR-08,L4 接线下游)

**L4 侧(P1 adapter + P3 MPC)**:
11. PID-L4 reference adapter(trajectory→航点 sampler)(VR-04)
12. PID-L4 短时域预测器(30s,VR-09 层3)
13. P3 MPC-L4 adapter(直接吃 trajectory)+ capability 发 constraint-tightening 硬界(VR-05/R22)

### Step5 · DESIGN-IT-TWICE  [2026-07-17 · 关键 DP 对抗验证]

重点对抗 2 高风险 DP,其余低风险快速过。

**DP-02 horizon 1200s 实时性 — 对抗方案(360s+终端 ingredients)**:
- 压测:实时性(acados RTI Np60-120 可承受,[R21] Johansen 1200s 实证)/收敛(长 horizon vs 终端集,终端集非线性 COLAV 难算 Eriksen 不用)/返航完整性(360s<900s 返航在 horizon 外)/实现复杂度(改参数 vs 设计终端集)。
- **对抗不更强**:终端集偏离 Eriksen+难算+返航仍在 horizon 外。当前推荐(1200s)维持。
- **新增缓解项**:dt 可调——1200s horizon 下 dt=10s(Np120)/15s(Np80)/20s(Np60),P1b benchmark 三档测实时性,取达标最大分辨率。COLREGs ample-time 分钟级,dt=15-20s 足够。dt 具体值留 M5 实现侧(跨树反馈"开放项"已记)。

**DP-06 L4 adapter 预测器 — 对抗方案(M5 侧通用模型预测)**:
- 压测:精度(L4 自模型高 vs M5 通用低)/控制器无关(L4 预测强 vs M5 维护 N 模型弱,违反 DP-00)/成本(PID 加预测器增量 vs M5 N 模型分支)/失效(L4 bug vs M5 模型不符)。
- **对抗不更强**:M5 侧预测违反 DP-00 + 精度低。当前推荐(L4 adapter 预测)维持。工程代价诚实:仅 PID-L4 adapter 加预测器(MPC-L4 复用内部预测零成本)。

**低风险 DP 快速对抗**:DP-01/03/04/05/07/08/09/10 各构造对抗方案,均不更强(理由见日志),全部维持。

**Step5 结论:11 裁决经 DESIGN-IT-TWICE 无回炉。新增缓解项:DP-02 dt 可调(10/15/20s benchmark)。**

### Step6 · 术语+技术规约+方案包  [2026-07-17 · 设计值版,SIL 校准后定稿]

- **术语表**:10 术语(见方案包组件 1),每术语含定义/本方案含义/边界/关联DP。
- **技术规约表**:13 条 TS-01..13(见注册表 0.9),六类(坐标系/单位/符号/时序/capability/反馈/accept/override/intent/职责),重构模式标与现状差异。
- **方案包八组件**:独立成文 `docs/superpowers/specs/2026-07-17-l3-l4-gnc-contract-solution-pack.md`。
- **状态**:设计值版,关键值(承诺前缀 180s / horizon 1200s / dt 可调)待 SIL 校准定稿。用户本周进 Spec 同步(13 改动点见 Step4)。

**方案包契约**:工程细节设计可做;推翻已裁决/重提弃用/改技术规约须回炉 design-grounding。

**移交**:本方案核心技术决策已裁决。用户进对应 Spec 修改同步。SIL 校准一周后回来定稿。
