# 设计日志: M5 MPC 避碰方案设计审查(Mid-MPC + BC-MPC)

> **模式**: 重构(M5 已有 Mid-MPC NLP + BC-MPC 枚举实现,避碰航线输出反复出问题)
> **创建**: 2026-07-16
> **关联 spec**: 待 Step6 产出方案包后写入
> **状态**: **已交付 brainstorming**(2026-07-16 用户接受方案包)。Step1-6 全部完成并经用户逐门确认(Step2 11DP 逐点 + Step3 TBD-5/6/7 逐个 + Step4 判别 + Step6 接受)。HARD-GATE 解除。
> **范围声明**: 本决策树聚焦 MPC 避碰核心(Mid-MPC NLP + BC-MPC 分支)。TailBuilder / AvoidancePlan 组装 / ReactiveOverrideCmd / committed-route 状态机 / Normal-DEGRADED 振荡 等 M5 其他子模块开新决策树另行处理。
> **修订记录(2026-07-16)**: 初版 Step2-6 曾被批量完成且未逐决策点向用户确认,违反 Skill 逐点确认硬门。现回退:Step2 之后的 VR 裁决/ALT 弃用/方案包**全部降级为草稿**,仅在用户逐点确认 Step2 各 DP 后重新定案。证据矩阵[R1-R17]、盲区[BL]、场景[SC]为事实采集,保留有效;裁决[VR]待逐点重获确认。

---

## 0. 决策树状态(权威索引 · 可变快照)

### 0.1 决策点注册表 [DP]
| ID | 描述 | 类型 | 父/分解 | 状态 | 详见 |
|----|------|------|---------|------|------|
| DP-01 | MPC 整体架构:连续 NLP 主 + 离散分支备 的双层混合是否成立 | 架构 | TD-01 | **已裁决(用户2026-07-16)** | VR-01:采纳双层+激活BC-MPC;子决策DP-01a/b待确认 |
| DP-02 | 预测模型保真度:恒速直线 / Nomoto / 3-DOF / MMG | 技术 | TD-01 | **已裁决(用户2026-07-16)** | VR-02:Nomoto-扩展(Mid+BC同一套);恒速弃用;3-DOF因缺水动力系数阻塞;manifest几何待修正 |
| DP-03 | 约束层级:物理量硬约束 + 状态/安全软约束+slack,slack 结构如何选 | 约束 | TD-01 | **已裁决(用户2026-07-16)** | VR-03:per-target per-step slack(ξ∈R^{MN});废单标量;w_slack待BL-12调研 |
| DP-04 | COLREGs 规则 13-17 编码方式:代价惩罚 vs 几何硬约束 vs 硬编码航向偏移 | 约束 | TD-01 | **已裁决(用户2026-07-16,转移代价待BL-13)** | VR-04:Eriksen混合(M6几何hard+Rule8/17 soft+转移代价);移除硬编码Rule14/15;VDM删除;C5/C9/C12数据源TBD |
| DP-05 | 求解器与数值方法:IPOPT NLP / SB-MPC 枚举 / RTI-SQP | 技术 | TD-01 | **已裁决(用户2026-07-16)** | VR-05:NLP建模维持,求解器迁移IPOPT→acados(理论[R18],实测对比用户后续单开) |
| DP-06 | 预测时域长度:90s vs 长时域(360-600s) | 阈值 | TD-01 | **已裁决(用户2026-07-16)** | VR-06:采纳Eriksen实测参数(R19);Mid360s/dt10s/replan60s + BC短/replan5s;RFC-001推翻 |
| DP-07 | 参考跟踪与终端约束:代价软实现 vs 硬终端约束 | 技术 | TD-01 | **已裁决(用户2026-07-16)** | VR-07:状态x=[ψ,r,u]含ROT;Eriksen终端路线(stage cost+转移代价+长horizon);人工参考轨迹防归航;T1作辅助 |
| DP-08 | 求解失败回退与层间交接策略(连续失败计数阈值是否成立) | 技术 | TD-01 | **已裁决(用户2026-07-16)** | VR-08:BC连续级联+stale45s/15°/20%门控[R5]+废空plan[R17c];geo-fallback/recovery演进为DP-07人工参考轨迹 |
| DP-09 | 不确定性处理:Nominal / Robust / Stochastic / 目标意图建模 | 技术 | TD-01 | **已裁决(用户2026-07-16)** | VR-09:Mid用A+(OU+intent_confidence);BC用Nominal;SB-MPC+GPU完整C标注为待选演进(瓶颈时回炉DP-05) |
| DP-01a | 双层职责划分:Mid-MPC vs BC-MPC 各自 owns 什么(用户指定BC-MPC承担"同步验证"职责,非纯兜底) | 架构 | DP-01 | **已裁决(用户2026-07-16)** | VR-01a:选项1 Eriksen标准(执行+兜底Doer,验证归M7) |
| DP-01b | 连续级联交接语义:BC-MPC 同步验证与 Mid-MPC 长期航线的关系;NLP失败/目标突变时如何接管不振荡 | 架构 | DP-01 | **已裁决(用户2026-07-16)** | VR-01b:四状态交接机(MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE);交还hysteresis连续2周期;FINAL_DEGRADE报M7 |

### 0.2 技术分解注册表 [TD]
| ID | 技术 | 分解子模块(→DP) | 触发步骤 |
|----|------|------------------|----------|
| TD-01 | MPC 避碰核心(M5) | 架构(DP-01), 预测模型(DP-02), 约束层级(DP-03), COLREGs编码(DP-04), 求解器(DP-05), 时域(DP-06), 参考跟踪(DP-07), 回退交接(DP-08), 不确定性(DP-09) | Step1 |

### 0.3 盲区注册表 [BL]
| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-01 | 90s 时域是否合理?长时域(360-600s)如何权衡预测误差爆炸/chattering? | DP-06 | 高 | 已闭环→[R4] |
| BL-02 | 恒速直线在大转向(≥30°)+变速下失效程度?Nomoto/3-DOF 哪个够用?VesselDynamicsModel(4-DOF MMG)为何未接入 NLP? | DP-02 | 高 | 已闭环→[R1] |
| BL-03 | 单标量 σ slack 共享所有目标/步,在多船场景是否令一目标松弛拖垮全部 CPA?是否需 per-target slack? | DP-03 | 高 | 已闭环→[R2] |
| BL-04 | 现有 Rule13 无约束 + Rule14/15/16/17 硬编码 5°/10° 偏移,在 SIL/斜遇场景导致哪些规则违反? | DP-04 | 高 | 已闭环→[R3] |
| BL-05 | 连续失败计数阈值交接是否引起 NLP↔fallback 振荡 / 冻结计划? | DP-08 | 高 | 已闭环→[R5] |
| BL-06 | 本 ODD 是否真需双层(BC-MPC 激活),还是单层 NLP+回退链够? | DP-01 | 中 | 已闭环→需激活(用户2026-07-16) |
| BL-07 | 本 ODD 多船同时避碰频率?决定单 slack 是否够 | DP-03 | 高 | 已闭环→核心场景,需per-target slack(用户2026-07-16) |
| BL-08 | SIL 场景中具体哪些 COLREGs 规则违反了?(需 trace 证据) | DP-04 | 高 | 已闭环→[R16](Rule13/15系统性失效,5/12PASS) |
| BL-09 | RFC-001 为何锁 90s?本 ODD 开放海域(需长前瞻)还是受限水域(90s 够)? | DP-06 | 高(阻断) | 已闭环→混合ODD,需分层时域,90s单层不够(用户2026-07-16) |
| BL-10 | SIL 中 NLP↔fallback 振荡/冻结实际发生率/典型 trace? | DP-08 | 高 | 已闭环→[R16](rule15-ot-boundary SOLVER=1/FALLBACK=2121) |
| BL-11 | Nomoto 参数(nomoto_T_s/nomoto_K_inv_s)的物理含义、辨识方法、典型船型取值范围?如何从试航数据辨识? | DP-02 | 高 | **已闭环→[R22]+用户裁决 TBD-5 选项A**(接受缩律估算初始值,海试后补) |
| BL-12 | w_slack 初值(现状1e8/v2.3设计1e4)是否有理论公式(exact-penalty Kerrigan)或实验依据?还是纯结果调试?合理取值范围? | DP-03 | 高 | **已闭环→[R23]+用户裁决 TBD-6 选项B**(升级混合 L1/L2) |
| BL-13 | 转移代价 J_transition=w_trans·Σ_k||ψ[k]-ψ_prev[k]||² 的公式合理性?Eriksen 文献中的具体形式?w_trans 取值依据? | DP-04 | 中 | **已闭环→[R24]+用户裁决 TBD-7 选项C**(warm-start+混合范数+符号翻转检测) |
| BL-14 | DP-03 per-target per-step slack(+M·N 维)在 IPOPT 下 O(n³) 复杂度增长,3s CPU 预算下是否实时可达?需 benchmark | DP-05 | 高 | 已闭环→[R18]+用户裁决acados(迁移规避IPOPT O(n³)风险);实测benchmark TBD-4 |
| BL-15 | 是否有覆盖需求(非凸COLREGs+实时+开源)的求解器替代 IPOPT?acados/HPIPM 可行性? | DP-05 | 高 | 已闭环→[R18] acados+HPIPM 最可行;用户采纳(选项B) |
| BL-16 | ample-time 时域计算方案/规则?Mid/BC 时域+重规划频率的合理设置?用户提议(Mid600s/30s,BC60s/30s)是否合理? | DP-06 | 高 | 已闭环→[R19] 概念澄清+业界数据 |

### 0.4 证据矩阵 [EV]
| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | NLM(colav_algorithms,high) | MPC 预测模型保真度对比 + chattering/infeasibility 机理 | 高 | 高 | 高 | DP-02 |
| [R2] | NLM(colav_algorithms,high) | 单标量 slack 失效(masking/free-riding)+ Eriksen per-target slack ξ∈R^{MNp} | 高 | 高 | 高 | DP-03 |
| [R3] | NLM(colav_algorithms,high) | COLREGs 编码两派 + 硬编码航向偏移"fundamentally unsound"+ 规则可硬/需软分类 | 高 | 高 | 高 | DP-04 |
| [R4] | NLM(colav_algorithms,high) | 90s 偏短 + Johansen/Eriksen/Hagen 用 360-600s + SB-MPC/OU 过程处理不确定性 | 高 | 高 | 高 | DP-06,DP-09 |
| [R5] | NLM(colav_algorithms,high) | Eriksen 三层混合架构(NLP 中层 + BC-MPC 短期)+ 连续失败阈值冻结计划风险 + 振荡/迟交接失效 | 高 | 高 | 高 | DP-01,DP-05,DP-08 |
| [R6] | PROJECT_FACT | mid_mpc_nlp_formulation.cpp:136-162,326-393 恒速直线运动学预测 pos[k+1]=pos[k]+u[k]dt(cos,sin)(psi[k]) | — | 仅本项目 | — | DP-02 |
| [R7] | PROJECT_FACT | constraint_compiler.cpp:29-37,166-239 Rule13 无约束(仅 marker),Rule14/15/16/17=5°/5°/10°/5° 硬编码 | — | 仅本项目 | — | DP-04 |
| [R8] | PROJECT_FACT | mid_mpc_nlp_formulation.hpp:121-122,cpp:620-624 单标量 σ slack,w_slack=1e8(从1e4调升) | — | 仅本项目 | — | DP-03 |
| [R9] | PROJECT_FACT | mid_mpc_node.hpp + cpp: bc_mpc_takeover/consecutive_failures/committed_route_manager/tail_gate/keep_last/geometric_fallback 多层回退 | — | 仅本项目 | — | DP-08 |
| [R10] | PROJECT_FACT | config/m5_params.yaml + hpp:87-90 N=18/dt=5s/horizon=90s(RFC-001 锁定) | — | 仅本项目 | — | DP-06 |
| [R11] | PROJECT_FACT | vessel_dynamics_model.cpp:37-60 4-DOF MMG 线性近似存在,仅用于 ROT 查表,未接入 NLP 预测 | — | 仅本项目 | — | DP-02 |
| [R12] | PROJECT_FACT | nomoto_fallback.cpp:40-46 Nomoto fallback 但 r₀=0 → 退化为恒向直线(无额外物理) | — | 仅本项目 | — | DP-02 |
| [R13] | PROJECT_FACT | bc_mpc_branch_formulation.cpp:50-70,24-48 3 档急迫度分支(5/7/13 候选),恒向恒速 dead-reckoning,无速度优化 | — | 仅本项目 | — | DP-05 |
| [R14] | PROJECT_FACT | mid_mpc_nlp_formulation.cpp:633-664 决策变量 x=[psi;u](无 ROT 状态),J=colreg+dist+vel+route+asym+terminal+slack | — | 仅本项目 | — | DP-07 |
| [R15] | PROJECT_FACT | mid_mpc_nlp_formulation.cpp:649-664 IPOPT L-BFGS/mumps,max_iter=800,max_cpu_time=3s,adaptive mu | — | 仅本项目 | — | DP-05 |
| [R16] | PROJECT_FACT(SIL run 2026-06-22) | Strict12 诊断:5/12 PASS;rule15-ot-boundary SOLVER_CONVERGED=1/GEOMETRIC_FALLBACK=2121;rule15-cs 系列 SOLVER 36-65/FALLBACK 791;失效家族 CPA不足(Rule15)/追越(Rule13)/相位/释放/风险 | 高 | 仅本项目 | 高(本系统SIL) | DP-04,DP-08 |
| [R17a] | DOCUMENTED_INTENT | v2.3 spec(2026-07-05)§2.1 设计裁决 per-target slack x=[psi(N);u(N);sigma(Nt)],J=w_slack·Σσ_t² | — | 本项目设计 | — | DP-03 |
| [R17b] | PROJECT_FACT | formulation.cpp:621,hpp:112 — 实现仍是单标量 σ(MX::sym 1,1),per-target 挂 [TBD-MULTI-SHIP] 推迟 | — | 仅本项目 | — | DP-03 |
| [R17c] | DOCUMENTED_INTENT | v2.3 spec §1 — v2.1 纯硬约束无 slack 支柱已坍塌(NLP INFEAS 371×,frozen corridor heading 0°,CPA 5.8m vs floor 180m) | — | 本项目设计 | — | DP-03,DP-08 |
| [R18] | NLM(colav_algorithms,high) | slack +M·N 维致 IPOPT O(n³)+破坏块带状稀疏→3s CPU 实时风险;DP 维度诅咒不适用;DDP/iLQR 反向传播不可处理非凸硬约束;acados+HPIPM 是最可行开源结构利用 RTI 路径(μs-ms) | 高 | 高 | 高 | DP-05 |
| [R19] | NLM(colav_algorithms,high) | ample-time 无刚性公式,规则是"horizon >> 大幅转向/变速所需时间";Johansen SB-MPC 600s/dt0.5-1s/replan5s;Eriksen MidNLP 360s(Np36,dt10s)/replan60s + BC-MPC 5s;30-60s replan 开阔水域合理(遭遇慢发展+L4持续跟踪+事件触发带外重规划);BC紧急层应高频(5s非30s) | 高 | 高 | 高 | DP-06 |
| [R20] | NLM(colav_algorithms,high) | 终端约束/代价: 标准三种(等式约束 x_N=x* 收可行域/终端集+LQR代价/无终端约束+长horizon+重权终端代价近似无穷horizon);Eriksen/Johansen 不用传统终端集,靠 stage cost + 转移代价防 chattering + per-step steady-state 可行性;Nomoto 模型推荐状态 x=[x,y,ψ,r],控制 u=[δ,n];排除 r 用差分会失旋转惯性精度;防过早归航靠转移代价/hysteresis + 长 horizon + 人工参考轨迹,非靠终端硬约束 | 高 | 高 | 高 | DP-07 |
| [R21] | NLM(colav_algorithms,high) | 意图分支+Monte Carlo 在 NLP MPC 内高度计算密集通常不实用;Johansen/Eriksen 用 SB-MPC+GPU 并行才实现(离散行为集+DBN 意图+GPU 评估);Monte Carlo 需 CE 方法+KF 平滑才达 ~1ms/1000 样本;轻量替代:OU 过程有界化不确定性 + intent_confidence 标量缩放 CPA 代价;生产建议:独立态势感知层(DBN/KF)+SB-MPC 短中 horizon + OU 预测 + GPU 并行 + 几何 fallback 终端集 | 高 | 高 | 高 | DP-09 |
| [R22] | NLM(ship_maneuvering+colav_algorithms,high)+Web | **BL-11 Nomoto 辨识**。T=偏航惯性/阻尼比(航向稳定性+响应快慢,大=迟钝),K=转向力矩/阻尼比(转向能力,大=回转快);K∝U,T∝1/U(存无量纲 T',K' 运行时缩放 T=(L/U)T',K=(U/L)K')。辨识:zigzag(10/10或20/20,最推荐,激励双舷瞬态)+ 系统辨识最小二乘/KF/ML;PMM 测水动力导数再解析换算;turning circle 主要验 K。IMO MSC.137(76)(≥100m 强制,FCB<100m 非强制但为参考框架):回转 advance≤4.5L/tactical dia≤5L,10/10 zigzag 一超调≤10-20°,20/20 一超调≤25°。典型值缺口:无 45m/18kts/双桨双舵 FCB 公开数据;最近似 Carrillo2018 河巡(K=-0.1724/T≈2.22s);缩律估算 T≈2-10s,K≈0.1-0.6/s(数量级);Inoue/Clarke 回归可估但高速双桨船超出排水船数据库(2x 误差)。**manifest 字段名歧义**:nomoto_K_inv_s=0.08 若为 1/K 则 K≈12.5(不合理大) | 中 | 高 | 低-中 | DP-02 |
| [R23] | NLM(colav_algorithms+safety_verification,high)+Web | **BL-12 w_slack 理论依据**。Kerrigan&Maciejowski(2000)"Soft Constraints and Exact Penalty Functions in MPC"是基础:**精确性条件 ρ>‖λ*‖∞**(λ*=硬约束最优 Lagrange 乘子,提供下界计算法)。**关键:精确性仅对 L1(线性)惩罚 ρ·s 成立;纯 L2(二次)½w·s² 在任何有限 w 下都不能精确**(s=0 处梯度=0,主目标略受益即接受小 s>0)。DP-03 现裁决 J_slack=w·Σξ² 是纯 L2→理论上不能精确,只能 w→∞ 渐近。acados 原生支持混合 L1/L2(线性项 zl/zu + 二次项 Zl/Zu),这是标准实践。Eriksen 用同伦增序列 K_ξ=[0.1,1,10,100,∞];Johansen κ_i=10-25;绝对量级无意义(取决于主目标/约束尺度)。失效:w 太小→自愿激活 slack(安全塌陷);w 太大→数值病态(IPOPT 乘子随 w 线性增→KKT 病态→MA57/restoration 失败);acados QP 子问题略优但仍受 L1/L2 精确性原理约束 | 高 | 高 | 中 | DP-03 |
| [R24] | NLM(colav_algorithms,high)+Web | **BL-13 转移代价公式**。提议 J_transition=w_trans·Σ_k‖ψ[k]-ψ_prev[k]² 作为通用跨周期连续性项合理,但**弱于 Eriksen 文献混合范数形式**:Eriksen BC-MPC tran_χ=K_Δχ·(χ_m-χ_{m,last})²(L2,航向控制修改量,K_Δχ≈2.5),tran_U=K_ΔU·|U_m-U_{m,last}|(L1,速度);Eriksen 中层 NLP 控制代价 φ_c=Σ(K_Ū·q_Ū(Ū_k)+K_χ̇·q_χ̇(χ̇_k)),K_χ̇=2.5,K_Ū=0.3(层面:控制修改量非预测航向状态 ψ[k])。Johansen/Edmund 非对称转移(k_Δψ,port=0.55,k_Δψ,stbd=0.2,ΔPr=13)。**最针对 port/stbd flip 的是条件符号翻转检测器**:Tengesdal PSB-MPC h=K_sgn·exp(-t/T_sgn),仅在 sign(χ_1)≠sign(χ_2) 触发(K_sgn=5);Johansen DRVO q_T·T²(二值翻转)。**关键:warm-start shift-init 是首要反 chattering 机制**(保持同伦类,正交于代价项);文献组合使用 warm-start+非对称势(K_HO=K_GW=40)+符号翻转检测+FSM hysteresis+neutral safe state+Δu-L2。纯 L2-on-heading 是这些机制中最弱的孤立选项。w_trans 无闭式理论,经验相对权(转移权 0.2-5 vs 碰撞势 40,差1-2数量级,确保风险总能压过惯性);太大→锁死前一计划无法反应,太小→chattering 持续 | 高 | 高 | 中 | DP-04 |

### 0.5 场景注册表 [SC]
| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| SC-01 | COLREGs 避让机动(右舵≥30°+变速) | 要求"尽早、明显",Rule8 ample time | DP-02,DP-06 |
| SC-02 | 多船同时避碰 | 多目标 CPA 约束同时激活,单 slack 拖累风险 | DP-03 |
| SC-03 | 斜遇/非常规相遇几何 | 硬编码航向偏移失效 | DP-04 |
| SC-04 | NLP 连续失败 / 目标突变 | 冻结计划风险 + NLP↔fallback 振荡 | DP-08 |

### 0.6 裁决注册表 [VR]
| ID | 裁决对象 | 结论 | 采纳/弃用 | 理由 | 时间 |
|----|----------|------|-----------|------|------|
| VR-01 | DP-01 架构 | 双层连续级联(NLP中+BC短),激活BC-MPC | **已裁决(Step2用户确认)** | [R5]Eriksen+[R16]单层证伪 | Step2 |
| VR-01a | DP-01a 职责 | Eriksen标准(执行+兜底Doer,验证归M7) | **已裁决(Step2用户确认)** | [R5]+用户;同步=并行运行非交叉检验 | Step2 |
| VR-01b | DP-01b 交接 | 四状态机(MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE);交还hysteresis连续2周期;FINAL_DEGRADE报M7 | **已裁决(Step2用户确认)** | [R5]连续级联+neutral+hysteresis | Step2 |
| VR-02 | DP-02 预测模型 | Nomoto-扩展(Mid+BC同一套);manifest几何修正;VDM删除 | **已裁决(Step2用户确认)+TBD-5精化** | [R1][R22]恒速90s失真;3-DOF缺数据阻塞;TBD-5参数辨识+字段语义 | Step2 |
| VR-03 | DP-03 slack | **per-target per-step slack ξ∈R^{M·N}**(比v2.3 σ(Nt)更细);废单标量σ;**TBD-6惩罚形式升级混合L1/L2** | **已裁决(Step2用户确认)+TBD-6精化** | [R2]masking+[R17a/b]+用户多船核心;[R23]纯L2不精确→混合L1/L2 | Step2 |
| VR-04 | DP-04 COLREGs编码 | Eriksen混合(M6几何hard Rule13/14/15+Rule8/17soft+转移代价);移除硬编码Rule14/15;VDM删除;C5/C9/C12标TBD;**TBD-7反chattering补强(warm-start+混合范数)** | **已裁决(Step2用户确认)+TBD-7精化** | [R3]硬编码unsound+[R16]Rule13/15失效;[R24]warm-start首要+混合范数 | Step2 |
| VR-05 | DP-05 求解器 | NLP建模维持,求解器迁移IPOPT→acados(RTI+HPIPM);SB-MPC标待选演进 | **已裁决(Step2用户确认,头号裁决)** | [R18]acados结构利用O(n)vs IPOPT O(n³);用户选项B;TBD-4实测 | Step2 |
| VR-06 | DP-06 时域 | Eriksen实测:Mid360s/dt10s(Np36)/replan60s+BC短/replan5s;RFC-001推翻 | **已裁决(Step2用户确认)** | [R19]概念澄清+业界实测 | Step2 |
| VR-07 | DP-07 终端约束 | **状态升级x=[ψ,r,u]含ROT**(弃差分);**Eriksen终端路线**(无终端集+stage cost+转移代价+长horizon+per-step可行性);**人工参考轨迹**防过早归航;T1降辅助 | **已裁决(Step2用户确认)** | [R20]Nomoto推荐含r;Eriksen/Johansen无终端集 | Step2 |
| VR-08 | DP-08 回退交接 | BC连续级联+stale45s/15°/20%门控+废空plan;geo-fallback降BC后最终层;recovery启发DP-07;committed_route语义更新 | **已裁决(Step2用户确认)** | [R5][R16][R17c]命中症状 | Step2 |
| VR-09 | DP-09 不确定性 | **Mid用A+(OU+intent_confidence)**;BC Nominal;SB-MPC+GPU完整C标待选演进 | **已裁决(Step2用户确认)** | [R21]选项C在NLP内不实用;A+是NLP内可行替代 | Step2 |
| VR-TBD5 | DP-02 Nomoto 参数辨识(TBD-5) | **选项A:接受缩律/回归估算作初始值(T≈2-10s,K≈0.1-0.6/s 数量级),标海试补真实参数待办**;manifest 几何参数修正;存无量纲 T',K' 运行时缩放;字段 nomoto_K_inv_s 语义须澄清 | **已裁决(Step3用户确认2026-07-16)** | [R22]本船型无公开数据,缩律2x误差;海试后补;不阻塞设计推进 | Step3 |
| VR-TBD6 | DP-03 slack 惩罚形式(TBD-6) | **选项B:升级为混合 L1/L2**(ρ·ξ+½w·ξ²,线性项 ρ>‖λ*‖∞ 保精确性 + 二次项保 Hessian 正定);acados zl/Zu 原生支持;ρ 用 Eriksen 同伦 K_ξ=[0.1,1,10,100,∞] 或下界法 | **已裁决(Step3用户确认2026-07-16)** — 修改 Step2 纯L2形式 | [R23]Kerrigan精确性仅L1成立,纯L2不精确;混合L1/L2精确性更高 | Step3 |
| VR-TBD7 | DP-04 反chattering机制(TBD-7) | **选项C:warm-start shift-init(首要,保持同伦类)+ 转移代价含速度通道混合范数(L2航向控制+L1速度,对齐Eriksen tran_χ/tran_U)+ 条件符号翻转检测器(Tengesdal K_sgn·exp,仅port/stbd flip触发)**;配合 M6 RuleLatch+FSM hysteresis+neutral safe state(已DP-01b/08) | **已裁决(Step3用户确认2026-07-16)** — 修改 Step2 纯L2-on-ψ提议 | [R24]warm-start首要;混合范数+符号翻转检测最针对实际失效模式,更合理 | Step3 |

### 0.7 备选/弃用方案 [ALT]
| ID | 方案 | 弃用理由 | 对比于 |
|----|------|----------|--------|
| ALT-01 | 单层NLP+纯几何回退 | [R16]几何fallback粗糙(rule15-ot-boundary SOLVER=1) | VR-01 |
| ALT-02 | 保持恒速直线预测 | [R1][BL-02]大转向+90s短时域失真 | VR-02 |
| ALT-03 | 全MMG预测模型 | [R1]太慢不适实时NLP | VR-02 |
| ALT-04 | Nomoto fallback r₀=0 | [R12]退化为恒向无额外物理 | VR-02 |
| ALT-05 | 单标量σ slack | [R2]masking/free-riding+用户多船核心场景 | VR-03 |
| ALT-06 | 纯硬约束无slack | [R17c]v2.1证伪(NLP INFEAS 371×,CPA5.8m) | VR-03 |
| ALT-07 | 硬编码5°/10°Rule偏移 | [R3]"fundamentally unsound"+[R16]Rule13/15失效 | VR-04 |
| ALT-08 | 全软代价COLREGs | [R3]可被压过、早归航 | VR-04 |
| ALT-09 | 90s单层时域 | [R4]偏短+[BL-02]两头不靠 | VR-06 |
| ALT-10 | 连续失败计数为主交接 | [R5]冻结计划风险+[R16]frozen | VR-08 |
| ALT-11 | keep-last空plan fallback | [R17c]GNC丢弃(latitude.size()<2) | VR-08 |
| ALT-12 | 方案B: SB-MPC整体转型(Johansen离散枚举600s) | 多船行为集组合爆炸(k^N_target)击中用户BL-07核心场景 + 弃现有NLP投资~5000行 + COLREGs布尔代价可被压过([R3]);可取处(receding horizon/意图建模)吸收进A+;用户Step2选项B裁决维持NLP→acados非SB-MPC | VR-05 |

### 0.8 技术规约注册表 [TS]
| ID | 类别 | 规约内容 | 单位/定义 | 来源 | 关联DP/接口 | 与现状差异 |
|----|------|----------|-----------|------|-------------|-----------|
| TS-01 | 坐标系 | 全局 WGS84(lat/lon);当地 NED(原点=本船当前位置);body(x艏前/y左舷) | rad,deg,m | [R6]units.hpp | DP-02,L3→L4 | 一致(无差异) |
| TS-02 | 坐标系 | NED 轴:psi=0→北(+x),顺时针;cos(psi)=北分量,sin(psi)=东分量 | rad | [R6]formulation:328-330 | DP-02 | 一致(无差异) |
| TS-03 | 符号约定 | 艏向 ψ 右舷(顺时针)正;ROT 右转正;cross-track l 右舷正(n_hat=(-sinψ,cosψ)) | rad | [R6]formulation:117-118 | DP-02,DP-04 | 一致(无差异) |
| TS-04 | 物理量单位 | 内部:角度 rad,速度 m/s,距离 m;消息边界:度;YAML:节 | rad,m/s,m,deg,kn | [R6]units.hpp | 全接口 | 一致(无差异) |
| TS-05 | 时序约定 | stamp=ROS2 steady;**Mid-MPC replan=60s(horizon360s/dt10s,Np36)**;**BC-MPC replan=5s**;事件触发带外重规划 | Hz,s | DESIGN_DECISION[VR-06] | DP-01,DP-06 | **从 1Hz solve/90s→60s replan/360s(RFC-001推翻)** |
| TS-06 | 决策变量 | **x=[ψ;r;u;ξ_{M·N}]** 含 ROT(r)+per-target per-step slack ξ;控制量 u=[δ,n](舵角+转速);Np=36,dt=10s | rad,rad/s,m/s,m² | DESIGN_DECISION[VR-03/05/07] | DP-03,DP-07,NLP内部 | **重大重构:从[psi(2N)+σ(1)]→[ψ,r,u+ξ(M·N)]** |
| TS-07 | 数值边界 | ξ_{m,n}≥0;u∈[speed_min,speed_max];ψ∈[heading_min,heading_max];ROT≤ROT_max·dt;**slack 惩罚混合 L1/L2:ρ·ξ+½w·ξ²,ρ>‖λ*‖∞** | m/s,rad | [R8][R15][R23] | DP-03,DP-05 | **ξ 维度+slack 惩罚 L1/L2(TBD-6)** |
| TS-08 | 数值边界 | 无效值:ψ/u 缺失→保持上一值;NaN→故障标记;ξ=0→正常(feasible) | — | [R6] | DP-08 | 一致 |
| TS-09 | 接口语义 | psi_cmd/u_cmd→L4;AvoidancePlan(waypoints WGS84+turn_radius+target_speed)→L4/M7/M8;**ReactiveOverrideCmd(BC 接管时)→L4** | deg,kn,m | [R6]M5-spec §3.2 | L3→L4 | **waypoint CMM字段+Override 路径待补** |
| TS-10 | 接口语义 | CPA floor:cpa_hard_m(硬下限,un-bumped);cpa_safe_m(软代价基准,bumped in conflict) | m | [R17a]§2.2 | DP-03,DP-04 | 一致(已修Bug C) |
| TS-11 | 时序约定 | **Mid-MPC 时域:horizon=360s,dt=10s,Np=36,replan=60s(Eriksen 实测);BC:短 horizon,replan=5s** | s | DESIGN_DECISION[VR-06] | DP-06 | **从单90s/dt5s→Eriksen分层(RFC-001推翻)** |
| TS-12 | 预测模型 | **NLP 预测:Nomoto-扩展 Tṙ+r=Kδ(Mid+BC同一套),存无量纲 T',K' 运行时缩放 T=(L/U)T',K=(U/L)K'**;VDM 4-DOF MMG 删除 | — | DESIGN_DECISION[VR-02][R22] | DP-02 | **恒速→Nomoto;manifest 几何修正(28→45m);TBD-5 参数辨识/字段语义** |
| TS-13 | 约束层级 | 物理(ROT/速度box)硬;**CPA per-target per-step ξ 混合 L1/L2 软**;Rule13/14/15 M6 几何 hard(可软化);Rule8/17 soft;**反 chattering:warm-start shift-init(首要)+转移代价混合范数+可选符号翻转** | — | DESIGN_DECISION[VR-03/04] | DP-03,DP-04 | **Rule硬编码→M6几何;反chattering分层(TBD-7)** |
| TS-14 | 回退交接 | **四状态机(MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE)**;BC连续级联;stale≤45s/航向>15°/CPA漂>20%出锁;交还hysteresis连续2周期;废空plan;geo-fallback=BC后最终层;FINAL_DEGRADE报M7 | s,deg,% | DESIGN_DECISION[VR-01b/08] | DP-01b,DP-08 | **失败计数→四状态机+连续级联+门控** |

---

## 参考文献
- [R1] NLM colav_algorithms 笔记本(id 9387989c, confidence high): MPC 预测模型保真度对比。Johansen et al.(2016): 主动 COLREGs 机动下恒速运动学与 3-DOF 差异小(转向时间相对长时域可忽略);但近距离/瞬态忽略 drift 致过保守或不可行。Nomoto 仅捕获主导偏航、忽略速度变化与非线性;3-DOF 为有效中间档;MMG 精度高但系数难辨识、太慢不适实时 MPC。chattering 机理(Hagen 2018 SB-MPC、Eriksen&Breivik 2019 缺零加速样本)、infeasibility 机理(硬约束/恒速预测过保守)。
- [R2] NLM colav_algorithms(high): 单标量共享 slack 的 masking/free-riding 失效——一目标松弛则全部目标 CPA 同步放宽。Eriksen&Breivik&Bitar 用 per-target per-step slack ξ∈R^{MNp},独立惩罚 + homotopy 增 K_ξ;Johansen SB-MPC 完全回避 slack(每障碍独立 hazard penalty)。
- [R3] NLM colav_algorithms(high): COLREGs 编码两派——软代价(可被其他代价压过、早归航)vs 硬几何(递归可行性丢失/死锁)。硬编码航向偏移(psi≥psi0+5°)"fundamentally unsound",斜遇/动态几何/任务冲突下失效(可能违 Rule17 stand-on)。规则可硬/需软分类:Rule13/14/15 可硬(清晰空间义务),Rule8/17 需软(ample time/readily apparent + stand-on 悖论)。Kuwata VO=硬约束;Johansen SB-MPC=软;Eriksen NLP+状态机=混合软代价+转移代价防 chattering。
- [R4] NLM colav_algorithms(high): 90s 偏短(COLREGs Rule8 ample time 要求大型船前瞻 10-20 分钟)。Johansen/Eriksen/Hagen 用 360-600s + SB-MPC(离散行为集避免非凸 NLP chattering)+ receding horizon(只执行前 5-10s)+ OU 过程约束横向不确定性 + 目标意图建模(分支 give-way/stand-on)。长时域代价:预测误差爆炸、求解时间、chattering。
- [R5] NLM colav_algorithms(high): Eriksen&Breivik&Bitar&Lekkas(2017/2020)三层混合 COLAV:中层 NLP 长期 + 短期 BC-MPC 采样兜底。交接为连续级联(短期层持续跟踪中层轨迹),NLP 失败/目标违规则短期层接管。连续失败阈值(N=3)是实用触发但 keep-last-route 致冻结计划风险(尾段确定性外推、无动态避碰)。失效模式:committed-route latching(需 max stale age 45s / 目标航向变>15° / CPA 漂移>20% 出锁)、振荡 shattering(需 neutral safe state + hysteresis + 转移代价)、交接时序(过早/过迟/过短)。
- [R6] PROJECT_FACT: src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:136-162,326-393 — 恒速直线运动学,pos[k+1]=pos[k]+u[k]dt(cos,sin)(psi[k]),无 Nomoto/动力学接入 NLP。
- [R7] PROJECT_FACT: src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp:29-37,166-239 — Rule13 无约束(仅 audit marker),Rule14/15/16/17=5°/5°/10°/5° 硬编码航向偏移。
- [R8] PROJECT_FACT: src/l3_tdl_kernel/m5_tactical_planner/include/.../mid_mpc_nlp_formulation.hpp:121-122, cpp:620-624 — 单标量 σ slack,w_slack=1e8(2026-07-05 从 1e4 调升,run-19f3102d92c 证据)。
- [R9] PROJECT_FACT: mid_mpc_node.hpp + cpp — bc_mpc_takeover(consecutive_failures≥阈值)/committed_route_manager/tail_gate/keep_last/geometric_fallback 多层回退链 + ASDR 审计 emit。
- [R10] PROJECT_FACT: config/m5_params.yaml + hpp:87-90 — N=18/dt=5s/horizon=90s(RFC-001 锁定,声明"不接受 HAZID 调整")。
- [R11] PROJECT_FACT: src/.../shared/vessel_dynamics_model.cpp:37-60 — 4-DOF MMG 线性近似(k_prop/k_drag/k_y_rudder/k_n_rudder 均 [TBD-HAZID]),RK4 积分,仅用于 ROT 查表与 TrajectoryPropagator,未接入 NLP 预测模型。
- [R12] PROJECT_FACT: src/.../mid_mpc/nomoto_fallback.cpp:40-46 — Nomoto fallback 但 r₀=0(hold heading)→ 退化为恒向直线,无额外物理信息。
- [R13] PROJECT_FACT: src/.../bc_mpc/bc_mpc_branch_formulation.cpp:50-70,24-48 — 3 档急迫度分支(urgency>0.95→13 分支±60°;>0.80→7 分支±30°;else→5 分支±20°),恒向恒速 dead-reckoning;bc_mpc_solver.cpp:24-25 rot_cmd=0、optimal_speed=输入(无速度优化)。
- [R14] PROJECT_FACT: mid_mpc_nlp_formulation.cpp:625-643 — 决策变量 x=[psi;u](2N,+1 when slack),无 ROT/r 状态;J=w_col·colreg+w_dist·dist+w_vel·vel+w_route·route+asym+terminal+w_slack·σ²。
- [R15] PROJECT_FACT: mid_mpc_nlp_formulation.cpp:649-664 — IPOPT,L-BFGS Hessian,mumps 线性求解,max_iter=800,max_cpu_time=3s,adaptive mu,constr_viol_tol=1e-3。
- [R16] PROJECT_FACT(SIL run): docs/Design/Review/2026-06-22/COLREGs_Generalized_Repair_Strict12_Diagnosis.md(2026-06-22,5x 仿真,strict restart 12-probe)。5/12 PASS。M5 planner health:rule15-ot-boundary SOLVER_CONVERGED=1/GEOMETRIC_FALLBACK=2121/EMPTY_TRANSIT=90;rule15-cs SOLVER=65/FALLBACK=791;rule13-ot SOLVER=0/FALLBACK=1046。失效家族:CPA不足(rule15-cs/cs-2/cs-intel,差22-31m)/追越(rule13-ot,CPA94.6m)/相位(rule15-cs-edge no-cross-ahead)/释放归航(rule15-ot-boundary M4滞留)/风险门(rule13-ot-giveway)。证据 runs/batch_20260622_222636_clean12_l4_trace_5x.json。
- [R17a] DOCUMENTED_INTENT: docs/superpowers/specs/2026-07-05-m5-nlp-as-core-link-fix-design-v2.3.md §2.1(2026-07-05)— 设计裁决 per-target slack:x=[psi(N);u(N);sigma(Nt)],每目标独立 σ_t,维度 +Nt,J_slack=w_slack·Σ_t σ_t²,w_slack 初值 1e4。
- [R17b] PROJECT_FACT: formulation.cpp:621 `sigma_=MX::sym("cpa_slack",1,1)`,hpp:112-113 注释"single scalar shared across all targets...Per-target/window slack is [TBD-MULTI-SHIP]"—实现只落地单标量,per-target 未实现。
- [R17c] DOCUMENTED_INTENT: v2.3 spec §1 — v2.1"suffix-hard 无 slack"三支柱之"Pillar3(fallback 覆盖 INFEAS)"坍塌:NLP INFEAS 371×,publish_committed_route 四分支无一在近距 INFEAS 触发,BcMpcFollow 无对应 publish 分支,keep_last 空 plan 被 GNC 丢弃,ship 跟随 frozen corridor heading 0°,CPA 5.8m vs floor 180m。
- [R18] NLM colav_algorithms(high): ① slack +M·N 维使决策变量从 2N→2N+MN,IPOPT 内点法 O(n³) 复杂度 + 破坏 MPC 块带状稀疏结构 → 1Hz/3s CPU 预算下随目标数增长有严重实时性风险;exact-penalty slack 引入差数值条件,多船约束冲突时易触发 restoration failure。② DP 维度诅咒(<5 状态)不适用实时船控;DDP/iLQR 反向传播(Riccati 递归,线性 horizon 复杂度)但原始形式仅适用无约束,COLREGs 非凸硬约束需 barrier 启发式→局部最优/chattering。③ 开源求解器对比:acados(开源,code-gen RTI/SQP,μs-ms,HPIPM 后端,最推荐);HPIPM/HPMPC(开源 LGPL,块结构 QP 内点,BLASFEO 加速);qpOASES(开源,active-set,warm-start 强但 condensed 致 O(N²)/O(N³),长 horizon 瓶颈);FORCES Pro(闭源商业);SNOPT(商业,通用 SQP,不利用 MPC 块结构)。结论:开源+实时+非凸 → acados+HPIPM 最可行。
- [R19] NLM colav_algorithms(high): ample-time 无刚性公式,工程规则是"horizon 须显著大于完成大幅转向/变速所需时间"。45m 船 18kts 需评估回转圈/执行器响应/减速时间。业界实测:Johansen SB-MPC 600s/dt0.5-1s/replan5s(偶10s);Eriksen Mid-NLP 360s(Np=36,dt=10s)/replan60s;Eriksen BC-MPC 短horizon/replan5s。replan 30-60s 开阔水域合理(遭遇慢发展+L4 持续跟踪多分钟轨迹+事件触发带外重规划,非盲飞);1Hz replan 对雷达/AIS 微噪声过反应→舵chattering。BC 紧急层应高频 5s(replan),非 30s。
- [R20] NLM colav_algorithms(high): 终端约束/代价三种标准方法:① 等式约束 x_N=x*(保证稳定但收可行域、短horizon致数值刚性/deadbeat振荡);② 终端集+LQR终端代价(平衡可行域与稳定,标准方法);③ 无终端约束+长horizon+重权终端代价近似无穷horizon(practical 稳定,免算终端集)。Eriksen/Johansen COLAV 不用传统终端集——靠 stage cost + 转移代价防 chattering + per-step steady-state 可行性约束(h_xr,k≤0)保动态稳定。Nomoto(Tṙ+r=Kδ)推荐状态 x=[x,y,ψ,r],控制 u=[δ,n](舵角+转速);排除 r 用差分会失旋转惯性精度,无法原生遵守执行器速率限制。防过早归航:转移代价/hysteresis + 长 horizon + 人工参考轨迹,非终端硬约束。
- [R21] NLM colav_algorithms(high): 意图分支建模(give-way/stand-on 分支)+Monte Carlo 碰撞概率在**梯度 NLP MPC 内高度计算密集、通常不实用**(非凸+局部最优+维度诅咒指数增长)。Johansen/Eriksen **回避 NLP**,用 SB-MPC:独立态势感知层(DBN 推断意图概率)→ SB-MPC 离散行为集(39 组航向/速度偏移)对多场景前向仿真 → **GPU 并行**评估(线性 scaling,毫秒级千组合)。Monte Carlo 需 Cross-Entropy 方法(自适应重要性采样)+ KF 平滑 → ~1ms/1000 样本才实时可行(纯 Monte Carlo 太慢)。轻量替代:OU 过程有界化横向不确定性(CV 模型爆炸→OU 收敛到巡航速度均值)+ intent_confidence 标量缩放 CPA 代价(高让路概率→低风险权重→免过保守)。生产建议:独立态势感知层(DBN/KF)+ SB-MPC 短中 horizon(60-150s)+ OU 预测 + GPU 并行 + 几何 fallback 终端集。**关键张力**:选项C(意图+Monte Carlo)在 NLP 框架内(Johansen/Eriksen 用 SB-MPC+GPU 实现)与 DP-05 维持 NLP(acados)冲突。
- [R22] NLM ship_maneuvering + colav_algorithms (high) + Web (IMO/Carrillo/ABS/ITTC): BL-11 Nomoto 辨识。Nomoto 一阶 Tṙ+r=Kδ: T=偏航惯性/阻尼比(航向稳定性+响应,大=迟钝),K=转向力矩/阻尼比(转向能力),|K/T|=总体机动指数,速度依赖 K∝U/T∝1/U(存 T',K' 运行时缩放)。辨识首选 zigzag(10/10、20/20,激励双舷瞬态)+ 系统辨识(LS/KF/ML);PMM 测水动力导数解析换算;turning circle 主验 K。IMO MSC.137(76)(≥100m 强制,FCB<100m 非强制但参考):advance≤4.5L/tactical dia≤5L,10/10 一超调≤10-20°,20/20≤25°。45m/18kts/双桨双舵 FCB 无公开数据;最近似 Carrillo&Contreras(2018)河巡(K=-0.1724/s,T≈2.22s);缩律估算 T≈2-10s/K≈0.1-0.6/s(数量级);Inoue/Clarke 回归可估但高速双桨船超排水船库(2x误差)。manifest 字段 nomoto_K_inv_s 语义存疑(1/K 则 K≈12.5 不合理)。来源:IMO MSC.137(76)(2002);MSC/Circ.1053;ABS Vessel Maneuverability Guide(2017);Carrillo&Contreras(2018)Ship Sci&Tech;Lan et al.(2023)JMSE 11(5):903;Alexandersson(2022)Chalmers;ITTC Manoeuvring;Kazerooni&Seif(2017);Mansim DTMB5415 双桨。
- [R23] NLM colav_algorithms + safety_verification (high) + Web (Kerrigan/acados/Hovd/arXiv): BL-12 w_slack 理论依据。Kerrigan&Maciejowski(2000)"Soft Constraints and Exact Penalty Functions in MPC"是基础:**精确性条件 ρ>‖λ*‖∞**(λ*=硬约束最优 Lagrange 乘子)。**精确性仅 L1(线性 ρ·s)成立;纯 L2(½w·s²)任何有限 w 都不精确**(s=0 梯度=0)。DP-03 现裁决 J_slack=w·Σξ² 是纯 L2→理论不精确只能 w→∞ 渐近;现状 1e8 即"推 L2 极大"经验法。acados 原生混合 L1/L2(zl/zu 线性 + Zl/Zu 二次)是标准。Eriksen 同伦 K_ξ=[0.1,1,10,100,∞];Johansen κ_i=10-25。失效:太小→自愿激活 slack(安全塌陷);太大→病态(IPOPT 乘子随 w 增→KKT 病态→MA57/restoration 失败);acados QP 略优但仍受 L1/L2 原理约束。来源:Kerrigan&Maciejowski(2000)CUED/F-Ctrl-TR;acados docs Problem Formulation;acados Discourse #951/#1021;Hovd(2011)IFAC;arXiv 2403.18235(2024)。
- [R24] NLM colav_algorithms (high) + Web (Eriksen arXiv/Tengesdal/Johansen): BL-13 转移代价。提议 J_transition=w_trans·Σ‖ψ[k]-ψ_prev[k]² 作为通用跨周期连续性合理但弱于 Eriksen 混合范数:Eriksen BC-MPC tran_χ=K_Δχ·(χ_m-χ_{m,last})²(L2,K_Δχ≈2.5)+ tran_U=K_ΔU·|U_m-U_{m,last}|(L1);Eriksen 中层 φ_c=Σ(K_Ū·q_Ū(Ū_k)+K_χ̇·q_χ̇(χ̇_k)),K_χ̇=2.5/K_Ū=0.3(作用控制修改量非预测 ψ[k])。Johansen/Edmund 非对称(k_Δψ,port=0.55/k_Δψ,stbd=0.2/ΔPr=13)。**最针对 flip 的是条件符号翻转检测器**:Tengesdal PSB-MPC h=K_sgn·exp(-t/T_sgn) 仅 sign 翻转时触发(K_sgn=5,T_sgn=4·t_ts,t_ts=25s);Johansen DRVO q_T·T²(二值)。**warm-start shift-init 是首要反 chattering 机制**(保持同伦类,正交于代价项);文献组合:warm-start+非对称势(K_HO=K_GW=40)+符号翻转检测+FSM hysteresis+neutral safe state+Δu-L2;纯 L2-on-heading 孤立最弱。w_trans 无闭式理论,相对权(0.2-5 vs 碰撞 40,差1-2数量级);太大锁死/太小 chattering 持续。来源:Eriksen et al.(2019)J Field Robotics 36(7),arXiv:1907.00039;Eriksen&Breivik(2017)IEEE CCTA;Tengesdal&Johansen PSB-MPC;Johansen/Edmund scenario-MPC;Johansen DRVO;NTNU Autosea BC-MPC。

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step1 · 行业调研·发现决策点  [2026-07-16]

**模式判定**: 重构。M5 已有完整 Mid-MPC(CasADi/IPOPT NLP,N=18/90s,决策变量 [psi;u],恒速直线预测,平滑指数 COLREGs barrier + 单标量 σ slack)+ BC-MPC(3 档急迫度分支枚举,恒向恒速 dead-reckoning,无速度优化)。现有代码/设计是主证据之一(机制B 重构模式),外部权威(NLM colav_algorithms,266 源)用于验证/补强/纠偏。

**调研来源**:
- NLM domain:colav_algorithms(266 sources),5 条查询(Q1-Q5)全部 high 置信 [R1]-[R5]
- 代码库: 完整阅读 mid_mpc_nlp_formulation.{hpp,cpp}、constraint_compiler.cpp、bc_mpc_{solver,branch_formulation}.cpp、vessel_dynamics_model.cpp、nomoto_fallback.cpp、mid_mpc_node.hpp、M5-spec.md、M5-progress.md

**主干决策维度提取**(NLM 7 轴 + 代码库现状对照):

1. **整体架构连续 NLP 主 + 离散分支备的双层混合**(业界头号争议)[R5] — 现状:Mid-MPC=IPOPT NLP,BC-MPC=枚举(已是混合),但 BC-MPC 整层在生产为死代码(M5-progress §5)、且层间交接靠连续失败计数阈值
2. **预测模型保真度** kinematic/Nomoto/3-DOF/MMG [R1] — 现状:恒速直线 [R6],Nomoto fallback 但 r₀=0 退化 [R12],VesselDynamicsModel(4-DOF MMG)存在但未接入 NLP [R11]
3. **约束层级与 slack 结构** [R2] — 现状:混合,单标量 σ slack w_slack=1e8 [R8];NLM 证实单标量 slack masking/free-riding 失效
4. **COLREGs 编码** 代价惩罚 vs 几何硬约束 [R3] — 现状:Rule13 无约束,Rule14/15/16/17=5°/10° 硬编码 [R7];NLM 证实硬编码偏移"fundamentally unsound"
5. **求解器与数值方法** IPOPT/SQP/SB-MPC [R5] — 现状:CasADi/IPOPT L-BFGS max_iter=800/max_cpu_time=3s [R15]
6. **时域长度与 chattering** [R4] — 现状:N=18/90s(RFC-001 锁定)[R10];NLM 证实 90s 偏短,Johansen/Eriksen/Hagen 用 360-600s
7. **求解失败回退与层间交接** [R5] — 现状:多层回退(失败计数→BC-MPC 接管→committed_route→tail_gate→keep_last→geometric→degraded)[R9];NLM 证实连续失败阈值致冻结计划风险 + 振荡 shattering
8. **(次级)不确定性处理** Nominal/Robust/Stochastic/目标意图 [R4] — 现状:Nominal(目标恒速直线,未建模意图)
9. **(次级)参考跟踪与终端约束** — 现状:决策变量无 ROT [R14],代价软实现 + T1 terminal softplus/硬行混合

**技术分解触发**(机制C):
- TD-01 MPC 避碰核心 分解为 9 个子模块决策点(DP-01~DP-09)
- 不可停留在"用 MPC"这一层——这正是之前返工的根因:预测模型/优化问题/约束层级/COLREGs 编码从未对齐

**关键矛盾(冲突标注,留 Step5 用 DESIGN-IT-TWICE 解决)**:
- 冲突 A: RFC-001 锁定 90s"不接受 HAZID 调整" vs NLM [R4] 证实 90s 偏短(Rule8 ample time 要求 10-20 分钟前瞻)。是 RFC-001 锁定错,还是本系统 ODD 不需要长前瞻?
- 冲突 B: [R1] Johansen 2016 认为主动 COLREGs 下恒速与 3-DOF 差异小(转向时间相对长时域可忽略) vs 现状 90s 短时域 + 大转向(≥30°)下恒速是否仍可忽略?时域越短,恒速近似的"长时域可忽略"前提越不成立。
- 冲突 C: 现状"用 NLP" + 单标量 slack + 硬编码 Rule vs NLM 主流(Johansen SB-MPC / Eriksen NLP+per-target slack)——是否应整体转向 SB-MPC?还是修 NLP 内部?

### Step2 · grilling 压力测试  [2026-07-16]

逐决策点三视角(专家/新手/悲观)+ 机制C"默认最简版失效"追问 + **逐决策点用户确认门**(2026-07-16 修订:每 DP 写完当场展示,用户确认/授权略过后才定案)。

---

**[grilling] DP-01 整体架构(NLP 主 + 分支备双层混合) — ✅ 用户已确认(2026-07-16)**

三视角结论(已展示用户):
- [专家] [R5] Eriksen 三层混合是公认架构:中层 NLP 长期 ample-time + 短期 BC-MPC 采样兜底,交接为连续级联。
- [新手] 现状是混合(Mid NLP + BC 枚举)但 BC-MPC 整层死代码(M5-progress §5:未launch/namespace错/bridge无消费),NLP失败实际靠粗糙几何fallback。
- [悲观] 双层只挂名不激活=单层NLP+粗糙几何fallback,兜底价值为零。
- [机制C] 默认最简版(名义BC不激活):NLP失败→几何弧形→粗糙抖动。

**用户裁决(2026-07-16,逐点确认)**:
1. **采纳双层 + 激活 BC-MPC 路线**。
2. **用户明确补充**:职责划分 + 连续级联交接**必须作为子决策点单独确认**(DP-01a/b),防止"方案与实现不符"。
3. **优先级与BC-MPC角色澄清**:第一步让 Mid-MPC 实现正常(单层先做对),同时 BC-MPC 也需完整实现;长期航线输出时 **BC-MPC 承担"同步验证"职责**(非纯兜底)。

→ 子决策点拆出:DP-01a(职责划分,含同步验证角色)、DP-01b(连续级联交接语义)。
→ DP-01 裁决记 VR-01(草稿→定案待 DP-01a/b 确认后完整)。
→ **DP-01a 提前 grilling**(用户 2026-07-16 主动提出职责不明确,且 DP-02 预测模型选择依赖职责划分——"Mid-MPC/BC-MPC 是否同一套预测模型"需先定职责)。

---

**[grilling] DP-01a 双层职责划分(Mid-MPC vs BC-MPC 各自 owns 什么) — 用户主动提出,提前确认**

背景:用户指出"不明确 Mid-MPC 和 BC-MPC 的职责最优是如何,例如是否需要 Mid-MPC 用于中长期输出,BC-MPC 对 Mid-MPC 的结果是否有必要进行同步验证"。用户在 DP-01 已明确 BC-MPC 承担"同步验证"职责(非纯兜底)。

- [专家] [R5] Eriksen 三层架构的职责划分:
  - 中层(NLP):长期(分钟级)COLREGs ample-time 轨迹规划,输出修改后的参考轨迹给短期层
  - 短期层(BC-MPC):持续**跟踪**中层轨迹;仅在 NLP 失败/目标违章/近距紧急时**接管**为兜底
  - Eriksen 架构里短期层**不验证**中层——它是执行+兜底,不是 checker。验证是独立 M7 Checker 的职责(ADR-2 doer-checker)
  - **关键区分**:Eriksen 的 BC-MPC 是"doer 的近距执行层",不是"checker"。把验证职责放进 BC-MPC 会模糊 doer-checker 边界。
- [新手] 用户提出的"BC-MPC 同步验证 Mid-MPC 结果"在 Eriksen 架构里没有直接对应。两种可能解读:
  - 解读 A(验证=安全检查):BC-MPC 检查 Mid-MPC 输出的航线是否近距安全 → 这其实是 M7 X-axis Checker 的职责(ADR-2),BC-MPC 做=职责越界
  - 解读 B(验证=一致性交叉检验):BC-MPC 用自己的独立预测(分支枚举)算一遍近距 CPA,与 Mid-MPC 的近距投影对比,若分歧大则告警 → 这是"双解算器交叉检验",有价值但增加复杂度
- [悲观]
  - 若按解读 A(BC-MPC 做 checker):违反 ADR-2 doer-checker 独立性,M7 已是独立 SIL2 checker,BC-MPC 重复验证=职责冗余 + 破坏 doer-checker 简单性原则
  - 若按解读 B(交叉检验):两个 doer 解算器都可能错,交叉检验不能替代 M7 独立 checker;且 BC-MPC 与 Mid-MPC 用不同预测模型/时域时,"分歧大"的阈值难定
- [机制C 默认最简版失效] 若不明确职责:BC-MPC 既被期望"兜底"又被期望"验证"又被期望"执行",实现时会混乱(M5-progress §5 BC-MPC 死代码部分原因就是职责未定→没接入)。

→ **此 DP 需用户判断的核心**:BC-MPC 的"同步验证"具体指哪种?
  - 选项1: Eriksen 标准职责(执行+兜底,不验证;验证归 M7)
  - 选项2: 交叉检验(BC-MPC 独立近距 CPA,与 Mid-MPC 对比告警)
  - 选项3: 用户有其他含义

(待用户确认后定 DP-01a 裁决,再回 DP-02)

**用户裁决 DP-01a(2026-07-16)**: **同意选项1(Eriksen标准职责)**。BC-MPC = 执行+兜底 Doer,不做验证(验证归 M7)。"同步"= 每周期并行运行、随时准备接管,非交叉检验。

→ 职责定案:
- **Mid-MPC**: 中长期 ample-time 轨迹规划(NLP),输出参考航线给 BC-MPC 跟踪
- **BC-MPC**: 持续跟踪 Mid-MPC 航线;NLP 失败/近距紧急时接管为兜底;**不做验证**
- **M7**: 独立 SIL2 checker(ADR-2),验证归此
→ **DP-02 前置已解**: Mid-MPC 与 BC-MPC 预测模型**可以不同**(职责是执行+兜底,非交叉检验,无独立性强制要求)。各自选最适合自身时域/算力的档位。

---

**[grilling] DP-02 预测模型保真度 — 三视角展开(用户三个输入已纳入)**

用户输入(2026-07-16):
1. 评审 3-DOF vs 现有 4-DOF MMG(计算效率/准确性),回答长时域/短时域是否同一套
2. Nomoto 参数是盲点 → BL-11
3. 删除恒速直线(完全不够用) → ALT-02 明确弃用

- [专家] [R1] 四档权衡(结合用户"效率/准确性"评审要求):
  | 档位 | 准确性 | 计算效率(NLP内) | 参数获取 | 适用层 |
  |---|---|---|---|---|
  | 恒速直线 | 差(大转向失真) | 最高(O(N)) | 无需 | **已弃用** |
  | Nomoto-扩展 | 中(捕获偏航瞬态,忽略sway) | 高(1阶ODE,O(N)) | T,K(现有manifest有,未辨识) | Mid候选/BC |
  | 3-DOF | 高(surge/sway/yaw耦合,drift) | 中(3阶ODE+Coriolis,O(N)但每步贵) | 需PMM/拖曳水池 | Mid候选 |
  | 4-DOF MMG(现有) | 最高(物理分解) | 低(RK4 4阶,每步4子步,[R11]证实仅ROT查表用) | 多系数[TBD-HAZID]未辨识 | 不适实时NLP([R1]) |

  **关键洞察([R1] + 现状[R11])**:
  - 现有 VesselDynamicsModel 是 4-DOF MMG **线性近似** + RK4——[R1] 明确"MMG 太慢不适实时 NLP",这正是它**只用于 ROT 查表、未接入 NLP** 的原因。代码现状与 [R1] 结论一致。
  - 3-DOF 是 [R1] 推荐的"有效中间档",但需要 Coriolis/阻尼矩阵系数(PMM 测试),本系统当前没有这些辨识数据。
  - Nomoto-扩展 只需 T,K 两参数(现有 manifest 有 `nomoto_T_s`/`nomoto_K_inv_s`),是 Mid-MPC NLP 内可承受的最简"有物理"模型。
  - **长时域/短时域不同套的合理性**:Mid-MPC(中长期,horizon长)需要更准的预测(误差随horizon放大);BC-MPC(短期近距,horizon极短)可用更简模型(短horizon下误差小)。[R1] 的"长时域下恒速可忽略"反向成立——短时域下简化模型可接受。

- [新手] 现状恒速直线已被用户判定"完全不够用"。可选升级路径:
  - 路径 A: Mid-MPC 用 Nomoto-扩展(低成本,参数已有但未辨识); BC-MPC 用 Nomoto-扩展或恒速(短horizon可接受)
  - 路径 B: Mid-MPC 用 3-DOF(更准但需辨识 Coriolis/阻尼); BC-MPC 用 Nomoto(分层简化)
  - 路径 C: Mid-MPC 直接用现有 4-DOF MMG(最准但 [R1] 说不适实时,需验证实时性)

- [悲观]
  - 路径 A 风险:Nomoto 忽略 sway,横移大的机动(急转向+减速)下 drift 角误差;但优于恒速。参数未辨识(BL-11)→ 先用 manifest 默认值,HAZID 校准。
  - 路径 B 风险:3-DOF 系数无辨识数据→阻塞实现,需拖曳水池/PMM;Mid-MPC NLP 内 3-DOF 每步 Coriolis 矩阵计算可能触及 IPOPT 实时边界。
  - 路径 C 风险:[R1] 明确 MMG 太慢;现有 RK4 每步 4 子步,CasADi MX 符号图会爆炸;极可能超 3s/cpu([R15])。

- [机制C 默认最简版失效] 若不选明确档位→默认回到恒速(已弃用)或 Nomoto r₀=0([R12]退化无效)。

→ **此 DP 需用户判断**:Mid-MPC 预测模型档位?
  - 选项1: Nomoto-扩展(低成本,参数已有待辨识,推荐起步)
  - 选项2: 3-DOF(更准但需辨识数据,阻塞)
  - 选项3: 现有4-DOF MMG([R1]说不适实时,需验证)
  - BC-MPC 档位(可后续定,短horizon容差大)

**用户裁决 DP-02(2026-07-16,附4份FCB文档调研)**:
用户倾向 3-DOF,但缺参数→要求从附件4份45m FCB文档找已有参数;找不到则退 Nomoto-扩展。BC-MPC 用同一套(避免维护两套)。

**FCB 文档参数调研结论(Step3性质,现场完成)**:
| 来源 | 可得参数 | 3-DOF/MMG 所需但缺失 |
|---|---|---|
| 01-DataSheet | LOA 45m, LBP 44.1m, Beam 8.0m, Draft 1.55m, 3×Cummins K38-M 1007kW/1900rpm, 减速比3.048, 25kts@100%MCR, 18kts@85%MCR, DWT~100T | — |
| 02-Hybrid | 混合动力系统(电池321.4kWh, E-motor 2×150ekW) | 水动力系数全无 |
| 03-Engine | 主机详细规格(K38-M 1007kW, 干重, 缸径冲程, 油耗) | 推进效率/伴流/推力减额 |
| 04-TechSpec(51p) | 建造规格:2舵(不锈钢,面积"合适尺寸"未给), 舵机17kNm, 定距桨3叶, **试航要求**(回转圈/zigzag,但无实测数据) | 回转圈/zigzag 实测值(只有"要测"没有"已测") |
| **结论** | **几何+推进+航速有**,但 **3-DOF 所需水动力系数(附加质量/阻尼/Coriolis)全无**;建造规格书不含水动力辨识 | 需 PMM/拖曳水池 或 试航系统辨识 |

**关键发现 — manifest 与实际 FCB 严重不符**:
| 参数 | manifest 现值 | 实际 FCB(数据表) | 差异 |
|---|---|---|---|
| length_m | **28.0** | **45.0(LOA)/44.1(LBP)** | 严重偏小 |
| beam_m | **6.5** | **8.0** | 偏小 |
| mass_kg | **95000(95t)** | DWT~100T→**排水量~130-160T** | 偏小 |
| draft_m | 1.4 | 1.55 | 偏小 |
| Nomoto T_s | 15.0 | 无辨识(占位) | 待辨识 |
| Nomoto K_inv_s | 0.08 | 无辨识(占位) | 待辨识 |
| mmg added mass factors | 0.05/0.40/0.07 | 文献估算(Yasukawa 2015),非 FCB 辨识 | 待辨识 |

→ manifest 描述的是一艘 ~28m/95t 船,实际是 45m/130T+ 船。**无论选 3-DOF 还是 Nomoto,manifest 几何参数必须先修正。**

**DP-02 裁决(基于调研)**:
- 3-DOF 不可选:4份文档无水动力系数,需 PMM/拖曳水池 或 试航辨识,**阻塞实现**。
- **退而求其次:选 Nomoto-扩展**(用户备选)。理由:只需 T,K 两参数;manifest 已有占位值(待 HAZID 辨识);[R1] Nomoto 虽忽略 sway 但优于恒速,且在 NLP 内计算效率高。
- **前置**:① 修正 manifest 几何参数(28m→45m 等);② Nomoto T,K 辨识(BL-11,Step3 调研辨识方法);③ BC-MPC 用同一套 Nomoto(用户要求,避免两套)。
- **MMG 系数**:manifest 已有 Yasukawa 文献估算值,作为 4-DOF VesselDynamicsModel 的 ROT 查表用保留,但**不接入 NLP 预测**([R1]太慢,与代码现状一致)。

**用户终认 DP-02(2026-07-16)**: 同意 Nomoto-扩展(Mid+BC同一套),manifest 几何参数修正纳入方案范围。Nomoto T,K 辨识方法交 Step3(BL-11)。

---

**[grilling] DP-03 约束层级与 slack 结构 — ✅ 用户已确认(2026-07-16)**

- [专家] [R2]: 硬约束只用于物理量(舵角/推力/ROT);软约束+slack 用于状态/路径/安全。单标量 slack 共享 = masking/free-riding:一目标 σ>0 → 全部目标 CPA 同步放宽。Eriksen 用 per-target per-step ξ∈R^{MN_p} 独立惩罚 + homotopy 增 K_ξ。
- [新手] 现状单标量 σ(w_slack=1e8,从1e4调升)[R8]。关键发现[R17]: v2.3 spec §2.1 已设计 per-target slack(x=[psi;u;σ(Nt)])但实现未落地(挂[TBD-MULTI-SHIP])。用户确认多船核心场景(BL-07)→ per-target 是必须项。
- [悲观] 单标量 σ 多船失效:船A迫使σ=500m→船B/C 的 CPA 约束也被允许侵入500m(免费)→安全边界整体崩塌。[R17c] 纯硬约束无slack已坍塌(INFEAS 371×,CPA5.8m)。
- [机制C] 默认单标量:多船任一目标松弛→全目标CPA退化。

**用户裁决(2026-07-16)**:
1. ✅ 采纳 per-target slack,废弃单标量 σ。
2. ✅ **采用 per-target per-step slack**(Eriksen ξ∈R^{M·N},非 v2.3 的 per-target-only)。维度增长从 +Nt 变为 +M·N(多船下增长快,但用户明确选此粒度)。
3. w_slack 初值(1e4 vs 1e8)合理性 → **BL-12 Step3 调研**:是否有 exact-penalty 理论公式(Kerrigan 2000)/实验依据,还是纯结果调试。

→ VR-03: per-target per-step slack ξ∈R^{M·N}; J_slack=w_slack·Σ_{m,n}ξ²_{m,n}; w_slack 待 BL-12。
→ 注: per-target per-step 比 v2.3 §2.1 的 per-target-only 更细,需更新设计(从 σ(Nt)→ξ(M·N))。

---

**[grilling] DP-04 COLREGs 规则 13-17 编码 — 调研阶段(用户要求先调研后下结论)**

**前置调研: M2/M6 模块职责划分(用户疑问:相对态势由谁维护)**

调研结论(代码 msg + M6-spec):
| 职责 | 归属 | 证据 |
|---|---|---|
| CPA/TCPA 几何计算 | **M2** | M6-spec:56 "CPA/TCPA 几何计算属于 M2 World Model";TrackedTarget.cpa_m/tcpa_s 由 M2 计算 |
| COLREG 几何预分类(head-on/overtaking/crossing + relative_bearing + aspect + is_giveway) | **M2** | EncounterClassification.msg: "由 M2 World Model 在 TrackedTarget 内置;M6 在此基础上做规则推理" |
| 规则推理(role/phase/preferred_direction/min_alteration) | **M6** | COLREGsConstraint.msg: phase/primary_role/primary_preferred_direction/active_rules;M6-spec:128 规则评估循环 |
| 遭遇生命周期(CLEAR/ONSET/ACTIVE/RELEASE + past_clear + release_predicted) | **M6** | COLREGsConstraint.msg:20-27 encounter_state/past_clear/release_predicted "M6-owned" |
| RuleLatch 滞后(Rule14/15 锁定不重分类) | **M6** | M6-spec:32 RuleLatch.update() |
| risk_of_collision 门控 | **M6** | M6-spec:33 tcpa>=0 && cpa<cpa_safe |
| 时机判定(Rule17 三阶段 STAGE_1/2/3) | **M6** | M6-spec:30 timing_stage by TCPA vs t_standOn/t_act 阈值 |

→ **态势几何由 M2 算(CPA/TCPA/encounter_type/relative_bearing/aspect),规则语义由 M6 推(role/phase/direction/lifecycle)**。M5 消费 M6 结论(formulation 已有 kIdxPreferredDir/kIdxRole/kIdxMinAlterationRad),不应重新推理。

**[TBD-HAZID 警告]** M6-spec:42 记录:当前 cpa_m/tcpa_s 由 SIL bridge 置零,M2 未推算,M6 收到的值为 0.0,Rule 推理基于错误输入。这是 M2 侧已知缺陷,影响 DP-04 编码的输入正确性。

---

**全约束罗列(用户要求)+ 推荐 hard/soft + 转移代价设计**:

约束分三类来源:① 物理可行域(船体能力) ② CPA 安全 ③ COLREGs 规则语义。

| # | 约束 | 来源 | 现状 | 推荐(hard/soft) | 理由 |
|---|---|---|---|---|---|
| C1 | 航向 box [heading_min,heading_max] | M4 窗口 | lbx/ubx(per-var bound) | **hard**(lbx/ubx) | 物理可行域,IPOPT 典型稳健 case |
| C2 | 速度 box [speed_min,speed_max] | M4 窗口 | lbx/ubx | **hard**(lbx/ubx) | 同上 |
| C3 | ROT 限值 \|psi[k+1]-psi[k]\|≤ROT_max·dt | VesselDynamicsModel | g≥0(平滑线性 hi/lo) | **hard**(g) | 物理量(舵角/角速度),[R3]物理量应硬 |
| C4 | 减速率 \|u[k]-u[k-1]\|≤decel_max·dt | Fix D-2 | g≥0 | **hard**(g) | 物理量,同 C3 |
| C5 | CPA floor d²-cpa_hard²+ξ≥0(per-target per-step) | M2 cpa_hard | g≥0+slack | **soft+slack**(per-target per-step ξ) | [R2]多船需独立松弛;[R17c]纯硬已坍塌(INFEAS 371×);物理可达时 ξ=0,不可达时 ξ 保可行 |
| C6 | Rule13/14/15 方向同侧 pref_dir·l[k]≥0 | M6 preferred_direction | formulation g_dir(D1 slice) | **hard**(g,可软化) | [R3]空间义务清晰可硬;但多船冲突时需 slack(DP-03)保可行 |
| C7 | Rule13/14/15 最小偏转 pref_dir·(ψ[k]-ownψ)≥min_alt | M6 min_alteration | formulation g_minalt(D1) | **hard**(g,可软化) | 同 C6;Rule8 "readily apparent" 的量化 |
| C8 | Rule17 stand-on 保向 \|ψ[k]-ψ0\|≤ε | M6 role=STAND_ON | constraint_compiler rule17(硬编码5°) | **soft**(代价 penalty) | [R3]Rule17 stand-on→evasive 有逻辑悖论,硬约束致多船 infeasible;软代价允许阈值突破后转向 |
| C9 | Rule8 ample time(尽早明显) | 定性 | 无显式约束 | **soft**(转移代价,见下) | [R3]定性概念,无法硬约束 |
| C10 | 终端同侧 pref_dir·l[N-1]≥l_min | M6 | formulation g_term_side(T1) | **hard**(g) | 避免尾段漂回错侧 |
| C11 | 终端横向上下界 \|l[N-1]\|≤l_max | GncExecutionOdd | formulation g_term_lo/hi(T1) | **hard**(g) | ODD 约束 |
| C12 | Zone 多边形禁入/禁出 | ENC | constraint_compiler zone(Boost geometry) | **hard**(g) | 空间义务 |

**Rule14/15 硬编码偏移(现状 constraint_compiler rule14/15 = ψ≥ψ0+5°)**: **移除**,被 C6/C7(M6 几何驱动的 direction/min_alt)替代。现状的 formulation g_dir/g_minalt 已是正确形态,只需确保 constraint_compiler 的硬编码不再叠加。

**转移代价防 chattering 设计(Eriksen 路线 [R3])**:

问题: COLREGs 避让是非凸问题(左舷 vs 右舷过)。NLP 每周期可能在不同局部最优间切换 → 航向 chattering(反复跳变)。

转移代价方案:
```
J_transition = w_trans · Σ_k ||ψ[k] - ψ_prev[k]||²
```
其中 ψ_prev[k] 是上一周期已执行轨迹的对应步(从 last_solution_ 取)。这不是简单的控制平滑(那已是 C3 ROT),而是**跨周期轨迹连续性**——惩罚本周期规划与上周期的偏差,迫使 NLP 除非有充分理由(COLREGs 代价压过)否则不切换避让侧。

配合机制(R3 Eriksen):
- **RuleLatch**: M6 已有(Rule14/15 锁定不重分类),避免态势反复重判。M5 应尊重 M6 latch,不自行重判。
- **state machine**: M6 phase(T_standOn/T_act/T_postAvoid)驱动代价权重——T_act 阶段 COLREGs 代价权重应高于 T_standOn,避免过早机动或过早归航。

→ **此 DP 待用户判断(不下结论,用户要求先调研)**:用户需确认上述约束分类 + 转移代价设计是否合理,再裁决。

**用户反馈(2026-07-16)+ 逐项核实**:

**① C3 两套模型疑问(用户核心关切)**:
用户问"C3 是否还需要 VesselDynamicsModel?这算不算维护 3-DOF 和 4-DOF 两套模型?"
核实结论(mid_mpc_node.cpp:772-775):
- **C3 ROT 约束的 ROT_max 实际来自 GncExecutionOdd**(cruise_max_yaw_rate=1.2°/s),**不是** VesselDynamicsModel。代码 line 772 明确 `vessel_model_.rot_max_rad_s no longer drives NLP ROT`。
- DP-02 裁决 NLP **预测模型**用 Nomoto-扩展。
- **VesselDynamicsModel(4-DOF MMG) 目前既不用于预测、也不用于 ROT** — 是死代码(line 772 "kept for future HAZID")。
- → **不需要维护两套**。约束表 C3 来源应修正为 GncExecutionOdd。
- → **新问题(记录,DP-02 延伸)**: VesselDynamicsModel(4-DOF MMG)的去留?现状死代码;选项:(a)删除减少混乱;(b)保留作 HAZID 后高保真验证基准。倾向 (b) 但明确标注"非 NLP 预测模型,仅验证用"。

**② 约束来源核实(用户要求逐条核实数据链路)**:
- **C12 Zone/ENC**: 用户指出"据我所知 ENC 是没有的"。**核实确认**: zone_constraints 在 constraint_compiler 有 compile_zone_constraints 代码,但 mid_mpc_node.cpp **从未填充 inputs.zone_constraints** → 始终为空 → **C12 是死代码,无数据源**。需标注为"无数据源,待 ENC 接入后启用"。
- **C4 Fix D-2 是什么**: Fix D-2 是 2026-07-03 Codex review(session 019f266b)发现 NLP 自由选 u[0] 忽略物理减速限制 → tail-gate 拒绝每个 (own_u→u[0]) 减速超限的解。修复:在 NLP 内加 g_speed_rate[k]=decel_max·dt-(u_prev-u[k])≥0。**但 decel_max=0.08 m/s² 是 types.hpp:227 硬编码默认值**,非 manifest 派生;现 line 776 已改为从 GncExecutionOdd.max_decel_mps2 取。
- **C1/C2 航向/速度 box**: 来自 M4 BehaviorPlan — 数据链路存在(mid_mpc_node 订阅 /l3/m4/behavior_plan)。
- **C3 ROT**: 来自 GncExecutionOdd(已修正,非 VesselDynamicsModel)。
- **C5 CPA**: 来自 M2 cpa_hard_m — 数据链路存在,但 [TBD-HAZID] M2 当前 cpa_m/tcpa_s 置零(已知缺陷)。
- **C6/C7/C8 Rule**: 来自 M6 — 数据链路存在。
- **C10/C11 终端**: 来自 M6(pref_dir)+ GncExecutionOdd(l_max)。
- **C12 Zone**: **无数据源**(见上)。

**修正后约束表(来源列已核实)**:

| # | 约束 | 来源(核实后) | 有数据源? | 推荐 |
|---|---|---|---|---|
| C1 | 航向 box | M4 BehaviorPlan | ✓ | hard |
| C2 | 速度 box | M4 BehaviorPlan | ✓ | hard |
| C3 | ROT 限值 | **GncExecutionOdd**(非VesselDynamicsModel) | ✓ | hard |
| C4 | 减速率 | **GncExecutionOdd**.max_decel(原硬编0.08) | ✓ | hard |
| C5 | CPA floor | M2 cpa_hard | ✓(但M2当前置零,已知缺陷) | soft+slack |
| C6 | Rule13/14/15 方向 | M6 preferred_direction | ✓ | hard可软化 |
| C7 | Rule13/14/15 最小偏转 | M6 min_alteration | ✓ | hard可软化 |
| C8 | Rule17 stand-on | M6 role=STAND_ON | ✓ | soft |
| C9 | Rule8 ample time | 定性 | — | soft(转移代价) |
| C10 | 终端同侧 | M6 pref_dir | ✓ | hard |
| C11 | 终端横向 | GncExecutionOdd | ✓ | hard |
| C12 | Zone | **无数据源(死代码)** | **✗** | 待ENC接入 |

**③ 转移代价公式合理性 → BL-13 Step3 调研**:
用户认可逻辑但不懂公式,要求调研。J_transition=w_trans·Σ_k||ψ[k]-ψ_prev[k]||² 的合理性 + w_trans 取值 → BL-13。

**④ 移除硬编码 Rule14/15**: 用户同意。→ 约束表已体现(C6/C7 替代)。

**用户追加要求(2026-07-16)**: 调研所有约束的数据链路,无来源/不可信的给 TBD 或修改意见,避免引入无意义约束导致不收敛。

**全约束数据链路终审(12 条逐一追到底层来源)**:

| # | 约束 | msg 字段来源 | 上游是否真填可信值? | 链路结论 | TBD/修改 |
|---|---|---|---|---|---|
| C1 航向 box | M4 heading_min/max_deg | ✓ M4 BehaviorPlan publish;M5 line 574-582 消费 | **可信**(M4 IvP 输出) | — | — |
| C2 速度 box | M4 speed_min/max_kn | ✓ M4 BehaviorPlan;M5 line 594-595 消费 | **可信** | — | — |
| C3 ROT 限值 | GncExecutionOdd.cruise_max_yaw_rate_deg_s(1.2°/s) | ✓ active_route_manager latched publish;M5 effective_gnc_odd_() 消费,fallback 1.2°/s | **可信**(执行包络权威源) | — | — |
| C4 减速率 | GncExecutionOdd.max_decel_mps2(0.20) | ✓ 同 C3;fallback 0.20(types.hpp 原 0.08 已弃) | **可信** | — | — |
| C5 CPA floor | kCpaSafeFallback_m=**1852.0(1nm 硬编)** | ✗ **不是 M2 的 cpa_m**;是 M5 node 硬编常量 1852;M2 的 cpa_m/tcpa_s 当前 **置零**(M6-spec:42 已知缺陷) | **不可信/硬编** | **TBD-1**: cpa_hard 应来自 M1 ODD(cpa_safe_m 按 ODD 域),非硬编 1852;M2 必须实现 CPA 推算(当前置零) |
| C6 Rule 方向 | M6 primary_preferred_direction | ✓ M6 COLREGsConstraint publish;M5 line 612-613 消费 | **可信**(M6 规则推理输出) | — | — |
| C7 Rule 最小偏转 | M6 min_alteration + M4 min_alt_required_rad | ✓ 双路:M6 colregs_min_alteration(line 642);M4 min_alt_required_rad(line 589-590);有 fallback_min_alteration_rad | **可信**(双交叉验证) | — | — |
| C8 Rule17 stand-on | M6 primary_role=STAND_ON | ✓ M6 COLREGsConstraint.primary_role;M5 line 611 消费 | **可信** | — | — |
| C9 Rule8 ample time | **定性,无显式数据源** | ✗ M5 不消费 M6 timing_stage/phase 做 ample-time 约束;仅 colregs_conflict_active(line 608)做 bump | **无数据源** | **TBD-2**: ample time 若要硬约束化,需 M6 phase(t_standOn/t_act)驱动;当前仅靠转移代价(C9)软实现,不引入无源硬约束 |
| C10 终端同侧 | M6 preferred_direction | ✓ 同 C6 | **可信** | — | — |
| C11 终端横向 | GncExecutionOdd max_lateral_offset(400m) | ✓ 同 C3/C4 | **可信** | — | — |
| C12 Zone | **mid_mpc_node 从未填充 zone_constraints** | ✗ constraint_compiler 有代码但 inputs.zone_constraints 始终空 | **死代码无数据源** | **TBD-3**: 保留代码,标注"待 ENC 接入";ENC 是安全航路核心保障(用户确认),接入后启用 |

**结论**: 12 条约束中 **8 条可信(C1/C2/C3/C4/C6/C7/C8/C10/C11)**,**3 条有问题(C5/C9/C12)**:
- C5 CPA:硬编 1852 非 ODD 派生 + M2 CPA 置零 → **TBD-1**(M2 实现 CPA + cpa_hard 从 M1 ODD 取)
- C9 ample time:无显式数据源 → **TBD-2**(不引入无源硬约束,仅转移代价软实现)
- C12 Zone:死代码 → **TBD-3**(待 ENC 接入,用户确认为安全核心保留)

**避免不收敛原则(用户要求)**: C5/C9/C12 在数据源补齐前,**不作为硬约束引入 NLP**。C5 用 per-target per-step slack 软化(ξ 保可行,不致 INFEAS);C9 仅转移代价软实现;C12 保持空(inputs.zone_constraints 为空时 compile_zone_constraints 返回空 g,不贡献约束行)。

**VesselDynamicsModel(4-DOF MMG)去留(用户裁决 2026-07-16)**: 删除或用 Nomoto 内容替换。DP-02 已裁决 NLP 预测用 Nomoto → VesselDynamicsModel 4-DOF MMG **删除**,其 ROT 查表功能已被 GncExecutionOdd 取代(C3),无残留依赖。

---

**[grilling] DP-05 求解器与数值方法 — 调研阶段(用户要求搜索+评估)**

用户输入(2026-07-16):
1. 维持 NLP/IPOPT 路线
2. 评估 DP-02/03/04 增强后求解复杂度 + "DP/反向传播"模式调研
3. 搜索覆盖需求的开源求解器(高效+稳定+开源)

**调研结论 [R18] NLM colav_algorithms(high)**:

**① 增强后复杂度评估(BL-14)**:
- 决策变量从 2N → 2N+M·N(N=18步,M=3目标=+54 slack vars)。
- IPOPT 内点法复杂度 **O(n³)** + per-target per-step slack **破坏 MPC 块带状稀疏结构**(每 ξ_{m,n} 耦合到 target-m 的多行)→ 求解时间随目标数增长**显著上升**。
- 1Hz/3s CPU 预算下,随 M 增长有**严重实时性风险**。
- exact-penalty slack 引入**差数值条件**,多船约束冲突时易 restoration failure。
- → **结论:维持 IPOPT + per-target per-step slack 有实时性风险,需 benchmark 验证**。

**② DP/反向传播模式(BL-14 用户记得的"DP 反向传播")**:
- **DP(动态规划)**: 全局最优、原生处理非凸,但**维度诅咒**(实用<5 状态),不适用实时船控。
- **DDP/iLQR(微分动态规划/迭代 LQR)**: 你记得的"反向传播"= Riccati 递归 backward-pass,线性 horizon 复杂度。DDP 用精确 Hessian(二次收敛),iLQR 用 Gauss-Newton(线性收敛)。
- **但**:DDP/iLQR 原始形式**仅适用无约束问题**。COLREGs 非凸硬约束(强制右舷过)需 barrier 启发式 → 局部最优/trapping/chattering。**不能稳健处理非凸硬约束**。
- → **结论:DDP/iLQR 不适合 COLREGs 非凸硬约束场景**。

**③ 开源求解器选型(BL-15)**:
| 求解器 | 开源? | 实时性 | 非凸? | 评价 |
|---|---|---|---|---|
| **acados** | ✓(开源) | μs-ms(code-gen RTI/SQP) | ✓(SQP/RTI) | **最推荐**:结构利用+code-gen,HPIPM 后端 |
| HPIPM/HPMPC | ✓(LGPL) | 块结构 QP 内点,BLASFEO 加速 | QP 子问题 | acados 的后端,不独立处理非凸 |
| qpOASES | ✓(LGPL) | active-set,warm-start 强 | QP | condensed 致 O(N²)/O(N³),长 horizon 瓶颈 |
| FORCES Pro | ✗(商业) | 极快(code-gen IP) | ✓(NLP) | 闭源,许可费 |
| SNOPT | ✗(商业) | 通用 SQP | ✓ | 不利用 MPC 块结构,慢 |
| **IPOPT(现状)** | ✓(EPL) | 通用 NLP,无结构利用 | ✓ | 3s/cpu,无实时保证 |

**关键矛盾(必须交用户)**:
用户 2026-07-16 决定"维持 NLP/IPOPT 路线",但 [R18] 调研显示:
- DP-03 per-target per-step slack(+M·N)在 IPOPT 下有 **O(n³) 实时性风险**
- acados(开源+code-gen RTI+结构利用)是**更优的 NLP 路线**(μs-ms vs IPOPT 3s),且同为 NLP/SQP 框架(不违背"维持 NLP"原则,只是换 NLP 求解器)
- **"维持 NLP/IPOPT" ≠ "维持 NLP"**——用户可能指"NLP 建模路线"(不转 SB-MPC),而非锁定 IPOPT 具体求解器
- acados 仍是 NLP/SQP(NLP 建模不变),只是求解器从通用 IPOPT 换成结构利用 code-gen RTI → 实时性大幅改善

→ **此 DP 需用户判断(矛盾不静默消化)**:
  - 选项A: 维持 IPOPT + 接受 per-target per-step slack 实时性风险(需 benchmark 验证,不达标则回炉)
  - 选项B: "维持 NLP 路线"但求解器从 IPOPT 迁移到 acados(NLP 建模不变,换结构利用求解器,μs-ms)
  - 选项C: 两者并行——先 IPOPT benchmark,不达标再迁移 acados

**用户裁决 DP-05(2026-07-16)**: **选项B — 维持 NLP 建模,求解器从 IPOPT 迁移到 acados**。用户对求解器无锁定,要求"快速求解+输出稳定";理论证据[R18] acados 结构利用 O(n) vs IPOPT O(n³)、μs-ms vs 3s,判定 acados 更优。实测对比(IPOPT vs acados Rule14 HO benchmark)**用户后续单开任务**,不阻塞当前设计推进。

→ VR-05: NLP 建模维持(Nomoto 预测 + per-target per-step slack + Eriksen 几何/转移代价),求解器迁移 IPOPT→acados(code-gen RTI + HPIPM 后端)。
→ **TBD-4(实现阶段验证)**: acados 安装(全栈 CMake) + M5 NLP 用 acados OCP interface 重表述(stage cost/path constraint/dynamics 形式) + code-gen + Rule14 HO benchmark 对比 IPOPT。若 acados 实测不达标,回炉 DP-05。
→ BL-14/BL-15 闭环到 [R18] + 用户裁决。

---

**[grilling] DP-06 预测时域长度 — ✅ 用户倾向已获 + 概念澄清(2026-07-16)**

用户输入: 采纳分层时域;倾向 Mid 600s/30s 重规划, BC 60s/30s 重规划(盲点,需澄清);RFC-001 可推翻(90s 仅避免开发期数值反复)。

**概念澄清(用户盲点 + 纠正主 agent 前误判) [R19]**:

三个独立概念:
- **预测时域(horizon)**: MPC 向前优化的总时长
- **控制步长(dt)**: horizon 内部离散化时间分辨率(决定 Np=horizon/dt)
- **重规划频率(replan rate)**: 多久重新求解整个优化问题

主 agent 前误判"30s 重规划=1/30Hz 反应不足" → **[R19] 纠正**: 30-60s replan 在开阔水域**完全合理**(遭遇慢发展;L4 持续跟踪多分钟轨迹非盲飞;事件触发带外重规划)。1Hz replan 反而对雷达/AIS 微噪声过反应→舵 chattering。

**业界实测数据 [R19]**:
| 系统 | horizon | dt | replan |
|---|---|---|---|
| Johansen SB-MPC | 600s(或200/150s) | 0.5-1s | 5s(偶10s) |
| Eriksen Mid-NLP | 360s(Np=36) | 10s | 60s |
| Eriksen BC-MPC | 短 | 紧 | **5s** |

**用户提议 vs 业界对照 + 修正建议**:
| 参数 | 用户提议 | Eriksen实测 | 评估/修正 |
|---|---|---|---|
| Mid horizon | 600s | 360s | 偏长(Johansen 用过600s可行,但需更强DP-09不确定性建模) |
| Mid replan | 30s | 60s | 更频繁=更安全(acados μs-ms 算力够) |
| Mid dt | 未提 | 10s | 待定(Np=horizon/dt) |
| BC horizon | 60s | 短 | 偏长(BC 是紧急层) |
| BC replan | 30s | **5s** | **太慢**——BC 紧急层 30s 无法应对近距突变 |

**关键修正点**: BC-MPC replan 30s 与紧急兜底职责矛盾,Eriksen BC-MPC = 5s。建议 BC-MPC replan ≤5s。

**用户终认 DP-06(2026-07-16)**: **完全采纳 Eriksen 实测参数,以 [R19] 为准**:
- **Mid-MPC**: horizon=360s, dt=10s (Np=36), replan=60s
- **BC-MPC**: 短 horizon, replan=5s
- **RFC-001(90s 锁定)推翻**: 用户确认 90s 仅避免开发期数值反复,非工程依据。

→ VR-06: 分层时域,Eriksen 参数。来源 [R19] NLM colav_algorithms Eriksen&Breivik&Bitar&Lekkas(2017/2020)。
→ 注: Mid 从现状 90s→360s,dt 从 5s→10s,replan 从 1s→60s;BC replan 5s。均为重大变更,brainstorming 阶段需更新所有相关参数接线(m5_params.yaml/resolve_horizon_config/solve_timer)。

---

**[grilling] DP-07 参考跟踪与终端约束 — 调研(用户要求不妥协现状,2026-07-16)**

用户输入: 对终端约束理解不深,要求调研;不以现状妥协,目标是鲁棒 MPC。

**调研结论 [R20] NLM colav_algorithms(high)**:

**① 终端约束/代价的标准方法(三种)**:
- **等式约束 x_N=x***: 保证稳定但收可行域、短 horizon 致数值刚性/deadbeat 振荡 → 不推荐
- **终端集 + LQR 终端代价**: 标准方法,平衡可行域与稳定;但非线性约束下终端集难算
- **无终端约束 + 长 horizon + 重权终端代价**: practical 稳定,免算终端集,扩大吸引域 → **Eriksen/Johansen 实际采用此路线**

**② Eriksen/Johansen 的实际做法(不靠终端硬约束)**:
- 不用传统终端集/终端等式约束
- 靠 **stage cost**(每步代价)+ **转移代价**(防 chattering)+ **per-step steady-state 可行性约束**(h_xr,k≤0 保动态稳定)
- 防过早归航靠**转移代价/hysteresis**(与 DP-04 C9 一致)+ 长 horizon(DP-06 360s)+ 人工参考轨迹

**③ 状态向量应否包含 ROT/r(关键改变)**:
- 现状 x=[psi;u] **无 r 状态**,ROT 用差分 → [R20] 明确: 差分会**失旋转惯性精度,无法原生遵守执行器速率限制**
- **Nomoto 模型(Tṙ+r=Kδ)推荐状态 x=[x,y,ψ,r],控制 u=[δ,n]**(舵角+转速)
- DP-02 已裁决用 Nomoto → 状态向量应**升级为含 r**: x=[x,y,ψ,r,u],或至少 x=[ψ,r,u]
- 这比现状(ψ,u 差分 ROT)更鲁棒——原生约束角加速度,非差分近似

**④ 防过早归航的鲁棒设计(用户目标:鲁棒)**:
- 转移代价(DP-04 C9,已裁决)防终止避让机动
- 长 horizon(DP-06 360s,已裁决)避免 stage cost 过早拉回航线
- 人工参考轨迹: 避让期间用"避让参考"而非原航线做 J_dist 基准,避免 optimizer 为最小化 XTE 过早归航
- 现状 T1 softplus + 硬行(g_term_side/lo/hi)可保留作为**额外保险**,但不应是唯一的防漂移机制

**DP-07 初步倾向(基于调研,非现状妥协)**:
1. **状态向量升级**: x=[ψ,r,u] 含 ROT(DP-02 Nomoto 自然要求),弃差分近似 → 更鲁棒
2. **终端策略**: 采纳 Eriksen 路线——无传统终端集,靠 stage cost + 转移代价(DP-04)+ 长 horizon(DP-06)+ per-step 可行性;现状 T1 softplus+硬行作为辅助保险保留
3. **防过早归航**: 转移代价 + 人工参考轨迹(避让期用避让参考做 J_dist 基准)

→ **此 DP 需用户判断**:
  - 状态向量是否升级含 r(ψ,r,u)?这增加维度但更鲁棒([R20]推荐)
  - 终端策略采纳 Eriksen 路线(无终端集+stage cost+转移代价)?
  - 人工参考轨迹(防过早归航)是否纳入?

**用户终认 DP-07(2026-07-16)**: **完全同意三项升级**。
→ VR-07:
1. 状态向量升级 **x=[ψ,r,u]** 含 ROT(DP-02 Nomoto 自然要求;弃差分近似;原生约束角加速度)
2. 终端策略采纳 **Eriksen 路线**(无传统终端集;stage cost + 转移代价(DP-04)+ 长 horizon(DP-06)+ per-step 可行性);现状 T1 softplus+硬行降为辅助保险
3. **人工参考轨迹**纳入(避让期用避让参考做 J_dist 基准,防 optimizer 为最小化 XTE 过早归航)
→ 来源 [R20] NLM colav_algorithms Eriksen/Johansen 实践。
→ 注: 状态向量从 [psi(2N)+slack] → [ψ,r,u 含 r +slack],维度增加;控制量从隐式 ψ 序列 → 显式 u=[δ,n](舵角+转速)。这是 formulation 层重大重构,brainstorming 阶段核心工作。

---

**[grilling] DP-08 求解失败回退与层间交接 — ✅ 用户已确认(2026-07-16)**

- [专家] [R5]: Eriksen 连续级联;连续失败阈值(N=3)致冻结计划风险(keep-last-route 尾段确定性外推无动态避碰);需 max stale age(45s)/目标航向变>15°/CPA漂移>20% 出锁;失效三族:latching/shattering/时序。
- [新手] 现状多层回退 [R9]: 失败计数→BC-MPC接管→committed_route→tail_gate→keep_last→geometric→degraded;但 BC-MPC 死代码(DP-01a已裁决激活);keep-last空plan被GNC丢弃([R17c] CPA5.8m)。
- [悲观] [R16] SIL铁证: rule15-ot-boundary SOLVER=1/FALLBACK=2121(NLP全程不收敛→冻结);rule15-cs SOLVER36-65/FALLBACK791(chattering)。
- [机制C] 默认失败计数交接:冻结+振荡;无门控出锁。

**用户裁决(2026-07-16)**:
1. ✅ BC-MPC 连续级联替代失败计数为主交接(与DP-01a一致)
2. ✅ stale-age(45s)+航向突变(15°)+CPA漂移(20%)门控出锁,**注明来源[R5]**
3. ✅ 废弃 keep-last 空 plan([R17c] GNC丢弃证伪)

**用户追加问题(2026-07-16): 两套 fallback(corridor/geo-fallback)是否保留?能否用于DP-07人工参考轨迹?否则剔除。**

核实结论(mid_mpc_node.cpp:1000-1156):
- **build_geometric_fallback_plan_**(line 1002): NLP失败时圆弧避让航线(status=DEGRADED conf=0.6),用M6 min_alteration+risk_aware_direction算目标航向,圆弧积分10WP
- **build_recovery_plan_**(line 1099): 避让后渐近归航(status=RECOVERY conf=0.8),XTE线性衰减回航线,6WP
- 两者共享 geometric_fallback_* 工具函数(turn_radius/waypoint_time)

**分析: 两套 fallback 与 DP-08/DP-07 的关系**:
- DP-08 裁决后,NLP失败由 **BC-MPC连续级联接管**(非几何fallback)。所以 build_geometric_fallback_plan_ **不再作为NLP失败的首选回退**——它的位置被BC-MPC取代。
- 但 BC-MPC 也是 Doer(DP-01a),它也可能失败。BC-MPC失败时需最后兜底 → geometric_fallback 可保留为**BC-MPC之后的最终降级层**(DEGRADED),但不再是主要回退路径。
- build_recovery_plan_ 的"渐近归航"逻辑(XTE线性衰减)与 **DP-07 人工参考轨迹**理念一致——避让后平滑回归航线。但其实现是独立几何航线(非NLP内参考)。
- **DP-07 人工参考轨迹**是 NLP **目标函数内**的 J_dist 基准(避让期用避让参考替代原航线),与 build_recovery_plan_(NLP外的独立几何plan)是**不同层级**的概念。

**结论**:
- build_geometric_fallback_plan_: **保留为BC-MPC后的最终降级层**(BC-MPC也失败时),但降级为第三梯队(非首选)。现状的"失败计数→geometric"主路径被"失败计数→BC-MPC连续级联"取代。
- build_recovery_plan_: **不直接复用为DP-07人工参考轨迹**(层级不同:一个是NLP外独立plan,一个是NLP内J_dist基准)。但其XTE衰减逻辑可**启发DP-07人工参考轨迹的设计**——避让期参考从原航线平滑过渡到避让航向,归航期再平滑过渡回。具体复用关系留brainstorming阶段定。
- keep_last空plan: **废弃**(用户裁决3)。
- corridor(committed_route_manager): DP-08连续级联后,committed_route职责调整为"BC-MPC跟踪的参考轨迹源",保留但语义更新。

→ VR-08: BC连续级联(主)+stale/突变门控(出锁)+废空plan;geometric_fallback降为BC后最终降级层;recovery启发DP-07人工参考但不直接复用;committed_route语义更新为BC参考轨迹源。

---

**[grilling] DP-09 不确定性处理 — 用户倾向C + 关键张力(2026-07-16)**

用户输入: 配置不确定性建模;倾向选项C(意图分支+Monte Carlo);BC-MPC短时域Nominal。

**调研结论 [R21] NLM colav_algorithms(high) — 关键张力**:

选项C(目标意图分支建模 + Monte Carlo 碰撞概率)在**梯度 NLP MPC 内高度计算密集、通常不实用**:
- 意图分支(give-way/stand-on 多场景)+ NLP → 非凸+局部最优+维度诅咒指数增长
- Johansen/Eriksen **回避 NLP**,用 SB-MPC + **GPU 并行**(39 组离散行为 × 多目标 × 多场景,GPU 线性 scaling 毫秒级)
- Monte Carlo 需 Cross-Entropy 方法 + KF 平滑才达 ~1ms/1000 样本(纯 MC 太慢)

**张力**: 用户选C,但C的业界实现(SB-MPC+GPU)与 **DP-05 已裁决维持 NLP(acados)** 冲突。在 NLP 框架内实现完整C不实用。

**[R21] 轻量替代(在 NLP 内可行)**:
- **OU 过程**: 约束目标横向不确定性(CV 模型长 horizon 爆炸→OU 有界化到巡航速度均值)— 选项B
- **intent_confidence 标量**: 已有 TrackedTarget 字段,缩放 CPA 代价(高让路概率→低风险权重→免过保守)— 选项A 的增强
- 两者可在 acados NLP 内实现,不需 GPU/SB-MPC

**选项对照(修正后)**:
| 选项 | 内容 | NLP内可行? | 实现量 | 安全收益 |
|---|---|---|---|---|
| A+ | OU 过程有界化 + intent_confidence 标量缩放 CPA 代价 | ✓(acados NLP 内) | 中 | 高(长horizon有界+意图感知) |
| B | OU 过程(同A+子集) | ✓ | 中 | 中高 |
| C | 意图分支建模 + Monte Carlo | **✗ NLP内不实用**(需SB-MPC+GPU) | 极高(与DP-05冲突) | 最高(但实现不可行) |

→ **此 DP 需用户判断(张力不静默消化)**:
  - 选项C 在 NLP(acados)框架内**业界证实不实用**;Johansen/Eriksen 用 SB-MPC+GPU 实现 C,但 DP-05 已裁决维持 NLP。
  - **选项 A+(OU + intent_confidence)** 是 NLP 框架内能捕获 C 大部分收益的可行方案:OU 有界化长 horizon 误差 + intent_confidence 缩放 CPA 代价实现意图感知。
  - 选项C 的完整实现(意图分支+MC)若要做,需回炉 DP-05(转 SB-MPC+GPU),推翻已裁决。
  - **建议**: 采纳 A+(NLP内可行),C 的完整能力作为"未来演进"(若转 SB-MPC)。或用户坚持 C → 回炉 DP-05。

**用户终认 DP-09(2026-07-16)**: **采纳选项 A+**(OU 过程有界化 + intent_confidence 标量缩放 CPA 代价);BC-MPC 短时域 Nominal。
→ VR-09: Mid-MPC 用 A+(OU + intent_confidence,在 acados NLP 内可行);BC-MPC 用 Nominal(短时域误差小)。
→ **待选演进路线(标注备查)**: SB-MPC + GPU 并行 + 完整意图分支建模(选项C)。触发条件: A+ 在 SIL 多船极端场景验证中证明不足以应对(意图感知/不确定性有界不够) → 回炉 DP-05 重评 SB-MPC。GPU 可消除 SB-MPC 组合爆炸劣势(线性 scaling),但代价是推翻 NLP 投资 + 需 GPU 硬件 + 与 Eriksen 中层 NLP 架构偏离。
→ 来源 [R21] NLM colav_algorithms。

**DP-05 维持 NLP(非 SB-MPC)的 5 条理由(备查,用户提问)**:
1. 多船组合爆炸 k^N_target(用户核心场景) — 注:GPU 可消除此劣势,但需硬件
2. 弃现有 NLP ~5000 行投资 + v2.x 多轮设计
3. Eriksen 架构中层即 NLP(方案A 符合,SB-MPC 偏离)
4. SB-MPC 布尔代价可被压过([R3]);NLP per-target slack+几何约束更稳([R2][R3])
5. v2.3 已设计 per-target slack 等,方案A 是落地已裁决设计

---

**[grilling] DP-01b 连续级联交接语义 — ✅ 用户已确认(2026-07-16,Step2 最后确认点)**

基于 DP-01a(职责) + DP-02~09(各子模块) + DP-08(回退链) 全部裁决后的交接状态机:

**四状态交接机(VR-01b)**:
```
状态1: MID_NORMAL (Mid-MPC 主导)
  Mid-MPC 每60s 发布 AvoidancePlan → L4
  BC-MPC 每5s 跟踪 Mid-MPC 航线(不发布 Override)
  → 接管触发: NLP失败 / 目标航向>15° / CPA漂移>20% → 状态2

状态2: BC_TAKEOVER (BC-MPC 接管)
  BC-MPC 发布 ReactiveOverrideCmd → L4
  Mid-MPC 暂停发布(stale 计时启动,max 45s)
  → 交还触发: Mid-MPC 恢复收敛 AND CPA 回升过 floor → 状态3

状态3: HANDOVER_NEUTRAL (缓冲,防 shattering)
  BC-MPC 继续 Override(hysteresis)
  Mid-MPC 恢复发布,BC-MPC 验证一致性
  → 连续2周期 Mid 收敛 + BC 无近距告警 → 状态1
  → Mid 又失败 → 状态2

状态4: FINAL_DEGRADE (BC-MPC 也失败)
  geometric_fallback (DEGRADED 圆弧) → L4
  ASDR 记录 + 向 M7 发布 safety_concern_event (MUST-9)
```

**用户裁决(2026-07-16)**:
1. ✅ 四状态交接机合理
2. ✅ 交还 hysteresis"连续2周期 Mid 收敛 + BC 无近距告警"
3. ✅ FINAL_DEGRADE 向 M7 上报(MUST-9,M5 发 safety_concern_event 不直接下 MRM)

→ 来源 [R5] Eriksen 连续级联 + neutral safe state + hysteresis;门控阈值(stale45s/15°/20%)来源 [R5]。
- [专家] Eriksen&Breivik&Bitar&Lekkas(2017/2020)三层混合是公认架构 [R5]:中层 NLP 长期 COLREGs 轨迹 + 短期 BC-MPC 采样兜底。前置:两层职责清晰、交接为连续级联(短期层持续跟踪中层轨迹,NLP 失败/目标违规则短期层接管)。
- [新手] 现状已是混合(Mid NLP + BC 枚举),但 BC-MPC 整层在生产为死代码(M5-progress §5:未 launch、namespace 错、bridge 无消费)。为何不直接用 SB-MPC 单层(Johansen 路线)?——因为 RFC-001/D3.2 已选 NLP 为主。
- [悲观] 双层交接点选错→振荡/冻结。现状交接靠连续失败计数阈值 [R9]→NLM [R5] 证实冻结计划风险。若"双层"只挂名(BC-MPC 不激活),则 NLP 失败时无真正采样兜底→退化到几何 fallback(粗糙)。
- [机制C 默认最简版失效] 双层若只实现"NLP + 名义 BC"但不真激活: NLP 失败→几何弧形 fallback(status=DEGRADED conf=0.6)→航线粗糙抖动,且无短期层平滑过渡。
- → 已有证据可答大部分。**真盲区**:本系统 ODD 是否真需要双层(BC-MPC 激活),还是单层 NLP + 好的回退链就够?→ BL-06(运营/架构)

**[grilling] DP-02 预测模型保真度** ★返工根因
- [专家] [R1] kinematic/Nomoto/3-DOF/MMG 权衡规则。Johansen(2016):主动 COLREGs 长时域下恒速运动学与 3-DOF 差异小(转向时间相对长时域可忽略);近距离/瞬态忽略 drift→过保守或不可行。3-DOF 为有效中间档;MMG 精但慢不适实时 NLP。
- [新手] 现状恒速直线 [R6](比 kinematic 还简),Nomoto fallback 但 r₀=0→退化为恒向直线无额外物理 [R12],VesselDynamicsModel(4-DOF MMG 线性近似)存在但仅用于 ROT 查表,未接入 NLP [R11]。
- [悲观] 恒速直线在大转向(≥30°)+ 变速下预测偏离→NLP"以为可行"的解实际偏离→轨迹 chattering/重新规划抖动。这是 M5 返工核心机制之一。
- [冲突 B 量化] [R1] Johansen 说"长时域下恒速 OK",但前提是**长时域**;现状 90s 短时域 + 大转向,该前提不成立。时域越短,转向占总时域比例越大,恒速近似越失真。
- [机制C 默认最简版失效] 默认恒速直线:大转向角下预测轨迹严重偏离实际→求解器基于错误预测选解→执行后偏差→下周期重选→chattering。
- → BL-02 已闭环到 [R1],但**90s 下恒速偏离的量化 crossover**需 Step3 深调。

**[grilling] DP-03 约束层级与 slack 结构** ★返工根因
- [专家] [R2] 硬约束只用于物理量(舵角/推力/ROT),软约束+slack 用于状态/路径/安全。Eriksen 用 per-target per-step slack ξ∈R^{MNp} 独立惩罚 + homotopy 增 K_ξ;Johansen SB-MPC 完全回避 slack(每障碍独立 hazard penalty)。
- [新手] 现状单标量 σ slack 共享所有目标/所有步 [R8],w_slack=1e8(2026-07-05 从 1e4 调升,run-19f3102d92c 证据)。
- [悲观] [R2] 直接证实单标量 slack masking/free-riding 失效:一目标需松弛→σ>0→全部目标 CPA 同步放宽(无额外代价)→安全边界整体崩塌。多船下尤其危险。
- [机制C 默认最简版失效] 默认单标量 slack:多船场景任一目标松弛令所有目标 CPA floor 同步退化。
- → BL-03 已闭环到 [R2]。**真盲区**:本 ODD 多船同时避碰频率?决定是否必须上 per-target slack。→ BL-07(运营)

**[grilling] DP-04 COLREGs 规则 13-17 编码** ★返工根因
- [专家] [R3] 两派:软代价(可被其他代价压过、早归航)vs 硬几何(递归可行性丢失/死锁)。**硬编码航向偏移(psi≥psi0+5°)"fundamentally unsound"**:斜遇/动态几何/任务冲突下失效,可能违 Rule17 stand-on。规则可硬/需软分类:Rule13/14/15 可硬(清晰空间义务),Rule8/17 需软。Kuwata VO=硬;Johansen SB-MPC=软;Eriksen NLP+状态机=混合软代价+转移代价防 chattering。
- [新手] 现状 Rule13 无约束(仅 audit marker),Rule14/15/16/17=5°/5°/10°/5° 硬编码航向偏移 [R7]——正是 [R3] 判定"fundamentally unsound"的形态。且 formulation 层另有 direction/min_alt g 行(R1/D1)职责重叠。
- [悲观] 硬编码度数无几何感知,斜遇/高速/窄水域不足或过度;职责重叠可能导致求解器约束冲突。
- [机制C 默认最简版失效] 默认硬编码偏移:斜遇几何下既不保证安全通过又可能违反 stand-on 义务。
- → BL-04 已闭环到 [R3]。**真盲区**:SIL 场景中具体哪些规则违反了?→ BL-08(需 SIL 日志证据,Step3)。

**[grilling] DP-05 求解器与数值方法**
- [专家] IPOPT(精但慢,NLP 非凸局部最优)、SB-MPC 枚举(避非凸 chattering)、RTI-SQP(快 4-5 倍)。
- [新手] 现状 CasADi/IPOPT L-BFGS Hessian/mumps,max_iter=800/max_cpu_time=3s/adaptive mu [R15];Fix#8 缓存 constraint signature 避免每周期 rebuild。
- [悲观] NLP 非凸→随新数据局部最优切换→chattering;IPOPT 在约束冲突时频繁 Restoration_Failed/Max_Iter(M5-progress §6 记录过 50 次)。
- [机制C 默认最简版失效] 默认无 warm-start/每周期 rebuild:L-BFGS Hessian 从零→500+ 迭代→超时。已部分修(Fix#8 缓存)。
- → 相对成熟,盲区较少。不新增 BL。

**[grilling] DP-06 预测时域长度** ★关键 + 权限冲突
- [专家] [R4] **90s 偏短**——COLREGs Rule8 ample time 要求大型船前瞻 10-20 分钟。Johansen/Eriksen/Hagen 用 360-600s + SB-MPC(离散行为集避非凸 chattering)+ receding horizon(只执行前 5-10s)+ OU 过程约束横向不确定性。
- [新手] 现状 N=18/90s [R10],RFC-001 **锁定"不接受 HAZID 调整"**。
- [悲观] 90s 太短→无法 ample time 机动(Rule8 违规风险);且 90s 短时域令恒速近似前提失效(与 DP-02 冲突 B 交织)。
- [冲突 A] RFC-001 锁 90s vs NLM [R4] 90s 偏短。这是**架构权限冲突**——RFC-001 是已锁定决策,但 NLM 证据挑战它。
- [机制C 默认最简版失效] 默认 90s:无法满足 Rule8 ample time;长时域需求被锁死。
- → **真盲区(最高优先级)**:RFC-001 为何锁 90s?本系统 ODD 是开放海域远距遭遇(需长前瞻)还是受限水域近距(90s 够)?→ BL-09(架构权限+ODD,必须用户答)。

**[grilling] DP-07 参考跟踪与终端约束**
- [专家] 终端约束保证递归可行性;代价软实现更柔但无可行性保证。
- [新手] 现状决策变量 x=[psi;u] **无 ROT/r 状态** [R14],T1 terminal softplus(平滑)+ §5.5 硬行(l_min/l_max)混合;route cost 含 λ_terminal=2 终端加强。
- [悲观] 无 ROT 状态→无法直接约束角加速度(只能差分 ROT);终端约束若不全→尾段漂出 ODD。
- [机制C 默认最简版失效] 默认无终端约束:horizon 末端自由→尾段漂移→下周期大修正→chattering。已部分修(T1 softplus+硬行)。
- → 相对成熟。不新增 BL。

**[grilling] DP-08 求解失败回退与层间交接** ★关键
- [专家] [R5] Eriksen 交接为连续级联;连续失败阈值(N=3)是实用触发但 **keep-last-route 致冻结计划风险**(尾段确定性外推、无动态避碰)。需 max stale age(45s)/目标航向变>15°/CPA 漂移>20% 出锁。失效模式:committed-route latching、振荡 shattering(需 neutral safe state + hysteresis + 转移代价)、交接时序(过早/过迟/过短)。
- [新手] 现状多层回退:失败计数→BC-MPC 接管→committed_route→tail_gate→keep_last→geometric→degraded [R9],含 ASDR 审计 emit。
- [悲观] [R5] 几乎逐条命中现场症状:committed-route latching→航线"卡住";NLP↔fallback 振荡 shattering→航线"反复跳变"。这正是用户报告的"避碰航线输出各式各样问题"的机理。
- [机制C 默认最简版失效] 默认连续失败计数交接:冻结计划 + 振荡;无 stale-age/突变门控出锁。
- → BL-05 已闭环到 [R5]。**真盲区**:现状 SIL 中 NLP↔fallback 振荡/冻结实际发生率/典型 trace?→ BL-10(需 SIL 日志,Step3)。

**[grilling] DP-09 不确定性处理**
- [专家] [R4] OU 过程约束横向不确定性(防 CV 模型 10 分钟误差爆炸);目标意图建模(分支 give-way/stand-on 加权)。
- [新手] 现状 Nominal(目标恒速直线,未建模意图)。
- [悲观] Nominal + 若放长时域(DP-06)→目标轨迹误差爆炸→过度保守或 chattering。与 DP-06 强耦合:若保持 90s 短时域则 Nominal 风险小;若放长时域则必须上不确定性建模。
- [机制C 默认最简版失效] 默认 Nominal:长时域下不确定性爆炸。短时域下可接受。
- → 依赖 DP-06 裁决。不单独新增 BL,随 DP-06 定。

---

**Step2 新增盲区汇总**(在 Step1 BL-01~05 基础上新增 BL-06~10,均为运营/ODD/SIL 证据类):

| 新 ID | 问题 | 归属 | 为什么答不了 | 优先级 |
|---|---|---|---|---|
| BL-06 | 本 ODD 是否真需双层(BC-MPC 激活),还是单层 NLP+回退够? | DP-01 | 运营/架构判断 | 中 |
| BL-07 | 本 ODD 多船同时避碰频率?决定单 slack 是否够 | DP-03 | 运营场景 | 高 |
| BL-08 | SIL 场景中具体哪些 COLREGs 规则违反了?(trace 证据) | DP-04 | 需 SIL 日志 | 高 |
| BL-09 | RFC-001 为何锁 90s?本 ODD 开放海域(需长前瞻)还是受限水域(90s 够)? | DP-06 | 架构权限+ODD | **高(阻断)** |
| BL-10 | SIL 中 NLP↔fallback 振荡/冻结实际发生率/典型 trace? | DP-08 | 需 SIL 日志 | 高 |

**新增场景**(SC-01~04 已在 Step1):
- (Step1 场景已覆盖:SC-01 大转向、SC-02 多船、SC-03 斜遇、SC-04 NLP 失败)

### Step3 · 自主深度调研  [2026-07-16]

针对 Step2 剩余盲区(BL-08/10 需 SIL 证据 + BL-02 的 90s 恒速量化 crossover + 设计-实现偏差查证)。证据来源:SIL 诊断报告 + v2.3 设计 spec + 代码现状核对。

---

**BL-08 闭环 → [R16] SIL Strict12 诊断(COLREGs 规则违规 trace)**

证据 [R16] `COLREGs_Generalized_Repair_Strict12_Diagnosis.md`(2026-06-22,5x 仿真 12-probe):
- 总体 5/12 PASS,7/12 RED——**系统性失效,非孤立**。
- 失效家族与对应规则:
  1. **CPA 不足边距(rule15-cs/cs-2/cs-intelligent)**: CPA 875.7/869.2/877.9 m < 900 m 门,差 22-31 m。诊断指向"缺 M6 约束意图↔M5 可执行轨迹的广义安全边距契约"——即 DP-04 硬编码偏移/软代价不足以保证 CPA floor。
  2. **追越物理间隙(rule13-ot)**: CPA 94.6 m < 180 m,overtake 完成未达成。
  3. **相位语义(rule15-cs-edge)**: CPA 过但 no-cross-ahead 门失败——Rule15 几何边界分类失效(DP-04 硬编码无几何感知)。
  4. **释放/归航(rule15-ot-boundary)**: M4 全程滞留 avoidance,route return 失败——M6 release + M5 fallback lifecycle 链断裂(DP-08)。
  5. **风险门(rule13-ot-target-giveway)**: risk gate 失(M7 评分)。
- **结论**: 规则违反集中在 Rule13/15(硬编码偏移失效)+ 释放语义,与 [R3] 预测一致。Rule14/17(对头/让路)在多数场景 PASS,因 starboard 偏移在正横对头下恰好有效。

**BL-10 闭环 → [R16] M5 planner health(NLP↔fallback 振荡/冻结 trace)**

证据 [R16] M5 planner health 计数(每场景循环内):
| 场景 | SOLVER_CONVERGED | GEOMETRIC_FALLBACK | EMPTY_TRANSIT | RECOVERY |
|---|---|---|---|---|
| rule13-ot | 0 | 1046 | 1010 | 152 |
| rule15-cs | 65 | 791 | 113 | 149 |
| rule15-cs-2 | 36 | 793 | 109 | 149 |
| rule15-cs-edge | 18 | 270 | 109 | 144 |
| rule15-ot-boundary | **1** | **2121** | 90 | 0 |
- **rule15-ot-boundary:SOLVER_CONVERGED=1, GEOMETRIC_FALLBACK=2121**——NLP 几乎全程不收敛,整场跑粗糙几何 fallback。这是 DP-08 冻结/振荡的现场铁证。
- **rule15-cs 系列:SOLVER_CONVERGED 36-65 vs GEOMETRIC_FALLACK 791-793**——NLP 偶尔收敛但多数周期 fallback,NLP↔fallback 频繁切换(chattering 源)。
- 诊断 §"Failure Taxonomy"4: rule15-ot-boundary = "M6 release + M4 滞留 avoidance + M5 fallback 持久化"——与 [R5] frozen-plan/latching 完全吻合。

**BL-02 量化 crossover(90s 短时域下恒速失效)**

- [R1] Johansen 2016: "proactive COLREGs 长时域下恒速与 3-DOF 差异小(转向时间相对长时域可忽略)"——前提是**长时域**。
- 90s 时域 + dt=5s = 18 步;大转向 ≥30°。一次 30° 转向在 ROT_max≈12°/s 下需 ~2.5s 完成,占单步 dt 的一半;但转向后速度/漂角瞬态在恒速假设下完全缺失。
- 关键洞察:BL-09 用户答"混合 ODD"→ 近距执行场景下 90s 内必须完成大机动,恒速直线预测的"长时域可忽略"前提不成立;远距 ample-time 场景下 90s 又太短看不远。**90s 是"两头不靠"**:近距恒速失真 + 远距看不远。
- → 与 DP-06 强耦合:90s 既不能让恒速近似成立(需长时域),又不能满足 ample time(需更长)。

**设计-实现偏差查证(关键发现)→ [R17]**

- [R17a] v2.3 spec(2026-07-05)§2.1 **明确设计了 per-target slack**: `x=[psi(N);u(N);sigma(Nt)]`,每个目标独立 σ_t,维度增长 +Nt。
- [R17b] 但**实际代码仍是单标量 σ**(formulation.cpp:621 `MX::sym("cpa_slack",1,1)`;hpp:112 注释"single scalar shared across all targets...Per-target/window slack is [TBD-MULTI-SHIP]")。
- 即:**设计已裁决 per-target slack,但实现只做了单标量,per-target 被挂为 [TBD-MULTI-SHIP] 推迟**。
- 用户 BL-07 答"多船是核心场景" → [TBD-MULTI-SHIP] 不能再推迟,per-target slack 是必须项,且设计已存在(v2.3 §2.1)只需落地实现。
- 同理 [R17c] v2.3 §1 记录了 v2.1"suffix-hard 无 slack"支柱已坍塌(NLP INFEAS 371× + frozen corridor heading 0° + CPA 5.8m),证明纯硬约束路线在本系统已被证伪。

**新增证据条目**:
| ID | 来源 | 内容 | 归属 |
|---|---|---|---|
| [R16] | PROJECT_FACT(SIL run 2026-06-22) | Strict12 诊断:5/12 PASS,M5 planner health(rule15-ot-boundary SOLVER_CONVERGED=1/GEOMETRIC_FALLBACK=2121) | DP-04,DP-08,BL-08,BL-10 |
| [R17a] | DOCUMENTED_INTENT(v2.3 spec 2026-07-05 §2.1) | 设计已裁决 per-target slack x=[psi;u;sigma(Nt)] | DP-03 |
| [R17b] | PROJECT_FACT(formulation.cpp:621,hpp:112) | 实现仍是单标量 σ,per-target 挂 [TBD-MULTI-SHIP] | DP-03 |
| [R17c] | DOCUMENTED_INTENT(v2.3 spec §1) | v2.1 纯硬约束无 slack 支柱已坍塌(NLP INFEAS 371×,frozen corridor CPA 5.8m) | DP-03,DP-08 |

**所有盲区状态更新**: BL-01~05(NLM)、BL-06/07/09(用户)、BL-08/10(SIL)、BL-02 量化 全部闭环或可裁决。无 EXTERNAL_CONFIRMATION_REQUIRED 残留。

---

#### Step3 续 · 用户盲点 BL-11/12/13 深度调研 + TBD-5/6/7 用户裁决  [2026-07-16]

针对 Step2 用户确认时显式留出的 3 个盲点(BL-11 Nomoto 辨识 / BL-12 w_slack 取值 / BL-13 转移代价公式),按 design-grounding Step3 协议派 3 个**只读证据 subagent**(返回证据不做裁决,裁决在主线程)。证据来源:NLM ship_maneuvering(54源)+colav_algorithms(266源)+ Web 兜底。三类置信度分列。

**BL-11 闭环 → [R22] Nomoto 辨识(DONE_WITH_CONCERNS)**
- 物理含义:T=偏航惯性/阻尼比(航向稳定性+响应快慢,大=迟钝),K=转向力矩/阻尼比(转向能力);|K/T|=总体机动指数;速度依赖 K∝U、T∝1/U → **存无量纲 T',K' 运行时缩放** T=(L/U)·T',K=(U/L)·K'(MPC 实践)。
- 辨识方法:**zigzag(10/10 或 20/20,最推荐,激励双舷瞬态)**+ 系统辨识(LS/KF/ML);PMM 测水动力导数解析换算;turning circle 主验 K(步态信息少)。IMO MSC.137(76)(≥100m 强制,**FCB 45m<100m 非强制但为参考框架**)。
- 本船型数值**缺口**:无 45m/18kts/双桨双舵 FCB 公开数据;最近似 Carrillo(2018)河巡 K=-0.1724/s、T≈2.22s(排水/航速域不同);缩律估算 T≈2-10s、K≈0.1-0.6/s(数量级);Inoue/Clarke 回归可估但高速双桨船超出排水船数据库(2x 误差)。
- **manifest 字段歧义(新发现)**:`nomoto_K_inv_s=0.08` 若为 1/K 则 K≈12.5/s(不合理大)→ **字段语义本身需澄清**(可能存的是 K 而非 1/K)。

**BL-12 闭环 → [R23] w_slack 理论依据(DONE,引发新张力)**
- Kerrigan&Maciejowski(2000)是基础:**精确性条件 ρ > ‖λ*‖∞**(λ*=硬约束最优 Lagrange 乘子)。
- **关键矛盾**:精确性**仅对 L1(线性 ρ·s)成立**;纯 L2(½w·s²)在任何有限 w 下**都不精确**(s=0 处梯度=0,主目标略受益即接受小 s>0)。
- DP-02 Step2 裁决的 J_slack=w·Σξ² 是**纯 L2** → 理论上不能精确,只能 w→∞ 渐近(现状 w=1e8 即"推 L2 极大"经验法)。
- acados **原生支持混合 L1/L2**(线性项 zl/zu + 二次项 Zl/Zu)→ 标准实践应采用混合形式。Eriksen 用同伦增序列 K_ξ=[0.1,1,10,100,∞]。
- 失效:w 太小→自愿激活 slack(安全塌陷);w 太大→数值病态(IPOPT 乘子随 w 线性增→KKT 病态→MA57/restoration 失败)。

**BL-13 闭环 → [R24] 转移代价公式(DONE,引发新张力)**
- 提议 J_transition=w_trans·Σ‖ψ[k]-ψ_prev[k]² 作为通用跨周期连续性**合理但弱于 Eriksen 文献混合范数**:
  - Eriksen BC-MPC:tran_χ=K_Δχ·(χ_m-χ_{m,last})²(L2 航向控制修改量,K_Δχ≈2.5)+ tran_U=K_ΔU·|U_m-U_{m,last}|(L1 速度)
  - Eriksen 中层 NLP:φ_c=Σ(K_Ū·q_Ū(Ū_k)+K_χ̇·q_χ̇(χ̇_k)),K_χ̇=2.5/K_Ū=0.3(**作用于控制修改量,非预测 ψ[k]**)
  - Johansen/Edmund 非对称(k_Δψ,port=0.55/k_Δψ,stbd=0.2/ΔPr=13)
- **最针对 port/stbd flip 的是条件符号翻转检测器**:Tengesdal PSB-MPC h=K_sgn·exp(-t/T_sgn) 仅 sign 翻转时触发(K_sgn=5);Johansen DRVO q_T·T²(二值翻转)。
- **关键洞察:warm-start shift-init 是首要反 chattering 机制**(保持同伦类,正交于代价项);文献**组合使用** warm-start + 非对称势(K_HO=K_GW=40)+ 符号翻转检测 + FSM hysteresis + neutral safe state + Δu-L2。纯 L2-on-heading 孤立最弱。
- w_trans 无闭式理论,相对权(转移 0.2-5 vs 碰撞 40,差 1-2 数量级,确保风险总能压过惯性)。

**Step3 新增 TBD(3 项,调研暴露的新设计点 — 用户已逐个裁决 2026-07-16)**:
| ID | 问题(原盲点)+ 调研发现 | 来源证据 | 用户裁决 |
|---|---|---|---|
| TBD-5 | DP-02 Nomoto 参数辨识:无试航数据,缩律/回归估算 2x 误差;manifest 字段 nomoto_K_inv_s 语义存疑(1/K 还是 K?) | [R22] | **选项A:接受缩律/回归估算作初始值(T≈2-10s,K≈0.1-0.6/s 数量级),海试后补真实参数**;字段语义须澄清;存 T',K' 缩放 |
| TBD-6 | DP-03 slack 惩罚形式:Step2 裁决的纯 L2 w·Σξ² 在 Kerrigan 精确性理论下不精确(仅 L1 精确);acados 原生支持混合 L1/L2 | [R23] | **选项B:升级为混合 L1/L2**(ρ·ξ+½w·ξ²,精确性更高) |
| TBD-7 | DP-04 转移代价:Step2 提议的纯 L2-on-heading ψ[k] 弱于 Eriksen 混合范数;warm-start shift-init 是首要反 chattering 机制 | [R24] | **选项C:warm-start shift-init(首要)+ 混合范数转移(L2航向控制+L1速度)+ 条件符号翻转检测器(Tengesdal)**;更合理 |

**注(2026-07-16 用户裁决后)**: TBD-5/6/7 经用户逐个裁决,记 VR-TBD5/6/7。TBD-6/7 修改了 Step2 提议的纯 L2 slack/转移代价**形式**,但经用户明确确认,现为权威。Step4/5/6 可基于 11 主裁决 + 3 TBD 裁决重做(此前自主草稿中 TBD-6/7 精化方向与用户裁决一致,但流程违规已纠正;内容可复用但须重新经用户判别/接受)。

### Step4 · 汇总分析·推荐方案  [2026-07-16 · 草稿,待用户判别]

> **状态(2026-07-16 TBD 裁决后)**: 本节原由主 agent 自主完成(流程违规,已纠偏)。TBD-5/6/7 已经用户逐个裁决(VR-TBD5/6/7),方向与本草稿精化一致。本草稿内容现作为**待用户判别的汇总推荐材料**(非自主定案)展示。用户判别/接受后才进 Step5/6。

> **(原注)重要修订(2026-07-16 Step3 后)**: 本节原为 Step2 批量草稿,VR 表基于未逐点确认的旧裁决。现按 Step2 逐点用户确认的**权威裁决**重写。关键差异(旧草稿→权威):DP-03 σ(Nt)→ξ(M·N);DP-05 维持IPOPT→NLP建模维持但IPOPT→acados;DP-06 候选180-300s→Eriksen实测360s/dt10s/replan60s+BC5s;DP-07 维持T1→x=[ψ,r,u]含ROT+Eriksen终端路线+人工参考;DP-09 长OU+意图→Mid用A+(OU+intent_confidence)/BC Nominal。另含 Step3 新增 TBD-5/6/7 精化点(非方案推翻,是惩罚/参数内部形式精化)。

逐决策点综合(按 DP 编号):推荐=Step2 逐点裁决(权威)+ 证据链 + 弃用理由 + 风险量化 + Step3 TBD 精化。机制C 硬门:技术分解完整性校验。

---

**DP-01 整体架构(NLP 主 + BC-MPC 备双层)— 推荐与 Step2 裁决一致**
- 推荐(=裁决 VR-01): **双层连续级联 + 激活 BC-MPC**。Mid-MPC NLP 中层主规划 ample-time 轨迹 → BC-MPC 短期跟踪+接管兜底(DP-01a 裁决为 Eriksen 标准执行+兜底职责,不做验证;验证归 M7)。
- 证据链: [R5]Eriksen 三层混合公认架构 + [R16]单层证伪(rule15-ot-boundary SOLVER=1/几何 fallback 粗糙)+ [R9]现状 BC-MPC 死代码。
- 弃用: ① 单层 NLP+纯几何回退(ALT-01,[R16]证伪);② 单层 SB-MPC(ALT-12,见 DP-05)。
- 风险: 中(BC-MPC 集成债:M5-progress §5 launch/namespace/bridge);失效边界:BC-MPC 激活后仍振荡(需 DP-08 配套);验证:SC-04 rule15-ot-boundary 无冻结。

**DP-02 预测模型保真度 — 推荐与 Step2 裁决一致 + TBD-5 精化**
- 推荐(=裁决 VR-02): **Nomoto-扩展(Mid+BC 同一套)**;manifest 几何参数修正(28m→45m 等);VesselDynamicsModel(4-DOF MMG)删除。
- 证据链: [R1]恒速90s失真 + [R6/R12]现状恒速/Nomoto-r₀=0退化 + [R22]Nomoto辨识方法 + FCB文档无水动力系数(3-DOF阻塞)。
- 弃用: ① 恒速直线(ALT-02);② 全 MMG(ALT-03,[R1]太慢);③ Nomoto r₀=0(ALT-04,[R12]退化);④ 3-DOF(无辨识数据阻塞)。
- 风险: 中;**TBD-5(Step3 精化)**: ① Nomoto T,K 无试航数据,[R22]缩律估算 2x 误差,需 HAZID 校准或补 zigzag 试航;② **manifest 字段 `nomoto_K_inv_s` 语义存疑**(1/K 则 K≈12.5 不合理,可能是 K 本身),brainstorming 须澄清字段含义;③ [R22]建议存无量纲 T',K' 运行时缩放(适应变速)而非固定 T,K。失效边界:参数不准→预测误差反噬;验证:单测预测轨迹 vs 基准。

**DP-03 约束层级与 slack 结构 — 推荐与 Step2 裁决一致 + TBD-6 精化(惩罚形式)**
- 推荐(=裁决 VR-03): **per-target per-step slack ξ∈R^{M·N}**(比 v2.3 §2.1 的 per-target-only σ(Nt)更细);废弃单标量 σ。
- 证据链: [R2]单 slack masking/free-riding + [R8/R17b]现状单标量 + [R17a]v2.3 设计 + 用户 BL-07 多船核心场景 + [R17c]纯硬约束坍塌。
- 弃用: ① 单标量 σ(ALT-05,[R2]masking);② 纯硬约束无 slack(ALT-06,[R17c]INFEAS 371×)。
- 风险: 低-中;**TBD-6(Step3 精化 — 惩罚*形式*,非结构推翻)**: [R23]Kerrigan&Maciejowski 精确性条件 ρ>‖λ*‖∞ **仅对 L1 成立**,现裁决纯 L2 `w·Σξ²` 不精确(s=0 梯度=0)。**精化建议:升级为混合 L1/L2**(线性项 ρ·ξ 保精确性 + 二次项 ½w·ξ² 保 Hessian 正定),acados 原生 zl/Zl 支持。w 取值用 Eriksen 同伦 K_ξ=[0.1,1,10,100,∞]或 ρ>max|λ*| 下界法。失效边界:维度增长 M·N(acados 结构利用规避 O(n³),见 DP-05);验证:多船单测 ξ_{m,n} 独立性 + slack 精确性(feasible 时 ξ=0)。

**DP-04 COLREGs 规则 13-17 编码 — 推荐与 Step2 裁决一致 + TBD-7 精化(转移代价形式)**
- 推荐(=裁决 VR-04): **Eriksen 混合**(M6 几何 hard Rule13/14/15 + Rule8/17 soft + 转移代价);移除硬编码 Rule14/15 偏移;VDM 删除;C5/C9/C12 标 TBD(数据源)。
- 证据链: [R3]硬编码"fundamentally unsound" + [R7]现状硬编码 + [R16]Rule13/15 系统性失效 + 12 约束数据链路终审(8 可信/C5·C9·C12 有问题)。
- 弃用: ① 硬编码 5°/10°(ALT-07);② 全软代价(ALT-08,[R3]可被压过)。
- 风险: 高(几何约束推导正确性);**TBD-7(Step3 精化 — 反 chattering 机制补强,非方案推翻)**: [R24]提议纯 L2-on-heading `w_trans·Σ‖ψ[k]-ψ_prev[k]²` **弱于 Eriksen 混合范数**(tran_χ L2 航向控制修改量 + tran_U L1 速度);且 **warm-start shift-init 是首要反 chattering 机制**(代价项是次要的)。**精化建议**: ① NLP 必须实现 warm-start shift-init(首要);② 转移代价升级为含速度通道的混合范数(ψ 控制 + 速度 L1);③ 可选加条件符号翻转检测器(Tengesdal K_sgn·exp)针对 port/stbd flip;④ 配合 M6 RuleLatch + FSM hysteresis + neutral safe state(已 DP-01b/08)。失效边界:几何冲突 infeasible(per-target slack 兜底);验证:SC-03 Rule15-cs/edge GREEN。

**DP-05 求解器与数值方法 — 推荐与 Step2 裁决一致(头号裁决已在 Step2 完成)**
- 推荐(=裁决 VR-05): **NLP 建模维持,求解器迁移 IPOPT→acados**(code-gen RTI + HPIPM 后端,μs-ms)。SB-MPC 标注待选演进(非采纳)。
- 证据链: [R18]acados 结构利用 O(n) vs IPOPT O(n³);per-target per-step slack +M·N 在 IPOPT 破坏块带状稀疏有实时性风险;acados 是开源+实时+非凸最可行。用户 Step2 选项B 裁决。
- 弃用: ① 裸 IPOPT 无 warm-start(ALT,现状);② SB-MPC 整体转型(ALT-12,多船组合爆炸+弃投资)。
- 风险: 中;**TBD-4(实现阶段验证,Step2 已记)**: acados 安装(CMake 全栈)+ NLP 用 acados OCP interface 重表述 + code-gen + Rule14 HO benchmark 对比 IPOPT。若实测不达标回炉 DP-05。失效边界:acados RTI 在 360s/36 步长下实时性需 benchmark;验证:TBD-4 benchmark。

**DP-06 预测时域长度 — 推荐与 Step2 裁决一致(RFC-001 推翻已用户授权)**
- 推荐(=裁决 VR-06): **Eriksen 实测参数**:Mid horizon=360s/dt=10s(Np=36)/replan=60s;BC 短 horizon/replan=5s。RFC-001(90s 锁定)推翻。
- 证据链: [R19]概念澄清(horizon/dt/replan 三独立)+ 业界实测(Johansen 600s,Eriksen Mid 360s/replan60s,BC 5s);用户 BL-09 混合 ODD 授权重裁;BL-02 90s 两头不靠。
- 弃用: ① 90s 单层(ALT-09);② 单纯放长 NLP 无分层(实时性)。
- 风险: 高(从 90s→360s/dt5→10s/replan1→60s 均重大变更,需更新 m5_params.yaml/solve_timer 等);失效边界:360s horizon 需配 DP-09 不确定性建模(OU)+ DP-02 Nomoto 速度缩放;验证:SC-05 ample-time 门 GREEN。

**DP-07 参考跟踪与终端约束 — 推荐与 Step2 裁决一致(重大重构)**
- 推荐(=裁决 VR-07): **状态升级 x=[ψ,r,u] 含 ROT**(Nomoto 自然要求,弃差分近似,原生约束角加速度);**Eriksen 终端路线**(无传统终端集;stage cost + 转移代价 DP-04 + 长 horizon DP-06 + per-step 可行性);**人工参考轨迹**(避让期用避让参考做 J_dist 基准,防 optimizer 为最小化 XTE 过早归航);现状 T1 softplus+硬行降为辅助保险。
- 证据链: [R20]终端约束三标准方法(Eriksen/Johansen 用无终端集+stage cost+转移代价)+ Nomoto 推荐状态 x=[ψ,r,u] 含 r(差分失旋转惯性精度)。
- 弃用: ① 维持 x=[psi;u] 无 ROT(旧草稿 VR-07,**被 Step2 裁决推翻**);② 传统终端等式约束(收可行域)。
- 风险: 中-高(formulation 层重大重构:决策变量从 [psi(2N)+slack]→[ψ,r,u+slack],控制量从隐式 ψ 序列→显式 u=[δ,n]);失效边界:状态维增 + acados 需重表述;验证:单测 ROT 原生约束 + 防过早归航 SIL。

**DP-08 求解失败回退与层间交接 — 推荐与 Step2 裁决一致**
- 推荐(=裁决 VR-08): **BC 连续级联 + stale45s/15°/20% 门控 + 废空 plan**;geometric_fallback 降为 BC 后最终降级层;recovery 启发 DP-07 人工参考但不直接复用;committed_route 语义更新为 BC 参考轨迹源。
- 证据链: [R5]Eriksen 连续级联 + [R16]rule15-ot-boundary SOLVER=1/FALLBACK=2121 frozen + [R17c]keep-last 空plan GNC丢弃 + DP-01b 四状态交接机。
- 弃用: ① 连续失败计数为主交接(ALT-10);② keep-last 空 plan(ALT-11,[R17c]GNC丢弃)。
- 风险: 中(门控阈值需调);失效边界:四状态机(DP-01b)需正确实现防 shattering;验证:SC-04 rule15-ot-boundary 无 SOLVER=1/FALLBACK=2121。

**DP-09 不确定性处理 — 推荐与 Step2 裁决一致(选项A+)**
- 推荐(=裁决 VR-09): **Mid 用 A+(OU 过程有界化 + intent_confidence 标量缩放 CPA 代价),在 acados NLP 内可行**;BC 用 Nominal(短时域误差小)。SB-MPC+GPU 完整意图分支(选项C)标待选演进。
- 证据链: [R21]选项C 在 NLP 内不实用(需 SB-MPC+GPU,与 DP-05 维持 NLP 冲突);A+ 是 NLP 框架内能捕获 C 大部分收益的可行方案。
- 弃用: ① Nominal(现状,长时域误差爆炸);② 完整选项C 意图分支+Monte Carlo(与 DP-05 NLP 冲突)。
- 风险: 中(OU 参数 + intent_confidence 缩放需设计);失效边界:A+ 若 SIL 多船极端场景不足→回炉 DP-05 转 SB-MPC+GPU;验证:SC-05 长时域 CPA 不爆炸。

---

**技术分解完整性校验(机制C 硬门)**:
| TD-01 子模块 | 决策点 | 就绪度(权威) |
|---|---|---|
| 架构 | DP-01 (+DP-01a/b) | ✓已裁决(Step2 用户确认) |
| 预测模型 | DP-02 | ✓已裁决 + TBD-5 参数精化 |
| 约束层级 | DP-03 | ✓已裁决 + TBD-6 惩罚形式精化(L1/L2) |
| COLREGs编码 | DP-04 | ✓已裁决 + TBD-7 反chattering精化 |
| 求解器 | DP-05 | ✓已裁决(Step2 头号裁决:NLP→acados) + TBD-4 实测 |
| 时域 | DP-06 | ✓已裁决(Eriksen 实测参数,RFC-001 推翻) |
| 参考跟踪 | DP-07 | ✓已裁决(x=[ψ,r,u]+Eriksen终端+人工参考) |
| 回退交接 | DP-08 | ✓已裁决 + DP-01b 四状态机 |
| 不确定性 | DP-09 | ✓已裁决(A+ / Nominal) |

**DECOMPOSITION 状态**: **全部 9 子模块 + DP-01a/b 已裁决(Step2 逐点确认)。无 INCOMPLETE 阻塞。** TBD-4/5/6/7 是实现阶段验证/内部形式精化点,**非方案推翻、非结构盲区**,不阻塞 Step5/6。

**Step5 残余 DESIGN-IT-TWICE 需求评估**:
- DP-05 头号裁决(NLP vs SB-MPC)**已在 Step2 逐点确认完成**(用户选项B:NLP→acados),无需 Step5 重做对比。
- TBD-6(L1/L2 惩罚形式)/TBD-7(转移代价形式)是**惩罚内部形式精化**,证据一致指向单一更优选项([R23]混合 L1/L2 / [R24]warm-start+混合范数),无两候选对抗 → **标低风险直接采纳**,无需完整 DESIGN-IT-TWICE。
- 结论:**Step5 仅需对 TBD-6/7 做低风险采纳裁决**,无头号对抗对比。

**冲突标注(Step5 处理)**:
- TBD-6: DP-03 slack 惩罚纯 L2→混合 L1/L2([R23]单源指向,低风险采纳)
- TBD-7: DP-04 转移代价纯 L2-on-ψ→warm-start+混合范数+可选符号翻转检测([R24]单源指向,低风险采纳)

### Step5 · DESIGN-IT-TWICE  [2026-07-16 · 已确认无残余对抗点]

> **状态(2026-07-16 用户判别 Step4 后)**: Step4 汇总推荐经用户判别通过。本节原自主 TBD-6/7 裁决已由用户在 Step3 逐个确认(TBD-6 选项B/TBD-7 选项C),方向一致。**Step5 无残余 DESIGN-IT-TWICE 对抗点**:
> - DP-05 头号裁决(NLP vs SB-MPC)= Step2 用户选项B(维持 NLP→acados),无需重做对比。
> - TBD-6/7 = Step3 用户裁决(混合 L1/L2 / warm-start+混合范数+符号翻转),无两候选对抗。
> - 11 决策点 + DP-01a/b + 3 TBD 全部裁决,无 INCOMPLETE。
>
> 原 NLP(A) vs SB-MPC(B)完整对比保留作弃 SB-MPC(ALT-12)证据链备查(见下方"原 Step5 备查")。回炉触发条件仍有效。

> **修订说明(Step3 后)**: 原版 Step5 做了 DP-05 NLP(方案A)vs SB-MPC(方案B)的完整对比并裁决方案A。但该裁决**现已被 Step2 逐点确认取代**——DP-05 头号裁决在 Step2 经用户逐点确认为"选项B:NLP 建模维持,IPOPT→acados"(非原 Step5 的"维持 IPOPT")。原 A/B 对比与裁决理由(弃 SB-MPC)结论方向一致(均维持 NLP 拒绝 SB-MPC),但求解器细节(IPOPT→acados)以 Step2 为准。原对比分析保留作为弃 SB-MPC 的证据链备查(见下方"原 Step5 备查")。

#### 当前 Step5 范围:Step3 TBD 精化点的低风险采纳裁决

Step3 暴露 3 个 TBD(TBD-5/6/7),均经 Step4 判定为**单源指向、低风险、非方案推翻**的内部形式精化,无需完整 DESIGN-IT-TWICE 对抗对比。逐项裁决:

**TBD-5(DP-02 Nomoto 参数辨识)— 标 HAZID/试航待办,非 Step5 裁决**
- 性质: 参数可得性缺口,非方案选择。Nomoto-扩展结构(Step2 裁决)不变。
- 处置: ① manifest 字段 `nomoto_K_inv_s` 语义(K vs 1/K)须 brainstorming 澄清(代码字段名歧义);② 建议存无量纲 T',K' 运行时缩放([R22]MPC 实践);③ 初始值用 [R22] 数量级估算(T≈2-10s,K≈0.1-0.6/s)+ HAZID 校准或补 zigzag 试航。**标 HAZID/试航待办**,不阻塞设计。

**TBD-6(DP-03 slack 惩罚形式:纯 L2 → 混合 L1/L2)— 低风险采纳**
- DESIGN-IT-TWICE 卡片:
```
┌─ TBD-6: slack 惩罚形式 ─────────────────────────────────┐
│ 选项1(Step2 裁决原文) │ 纯 L2: J_slack=w·Σξ²           │
│   来源 [R23]           │ Kerrigan 精确性条件不满足(s=0   │
│                        │ 梯度=0,任何有限 w 不精确)      │
│   失效                 │ feasible 时仍 ξ>0(安全边距缩水)│
│   实现                 │ 简单,现状即是                   │
│ 选项2(精化)           │ 混合 L1/L2: ρ·ξ + ½w·ξ²         │
│   来源 [R23]           │ Kerrigan 精确性: ρ>‖λ*‖∞ 保证  │
│                        │ feasible 时 ξ=0;acados zl/Zl 原生│
│   失效                 │ ρ 过大致病态(但可同伦/上界缓解)│
│   实现                 │ acados 原生,中等                │
│ 裁决                   │ **采纳选项2(混合 L1/L2)**       │
│ 理由                   │ [R23]单源明确:精确性原理要求 L1│
│                        │ 成分;acados 原生支持;不推翻    │
│                        │ Step2 per-target per-step 结构  │
│ 风险                   │ 低(惩罚形式精化,非结构变更)    │
└────────────────────────────────────────────────────────┘
```
- 裁决: **VR-03 精化 — slack 惩罚升级为混合 L1/L2**(线性项 ρ·ξ 保精确性 + 二次项 ½w·ξ² 保 Hessian 正定),ρ 用 Eriksen 同伦 K_ξ=[0.1,1,10,100,∞] 或 ρ>max|λ*| 下界法。per-target per-step 结构不变。

**TBD-7(DP-04 转移代价:纯 L2-on-ψ → warm-start 首要 + 混合范数 + 可选符号翻转)— 低风险采纳**
- DESIGN-IT-TWICE 卡片:
```
┌─ TBD-7: 反 chattering 机制 ─────────────────────────────┐
│ 选项1(Step2 草案)     │ 纯 L2-on-heading: w·Σ‖ψ[k]-    │
│                        │ ψ_prev[k]²                       │
│   来源 [R24]           │ 弱于 Eriksen 混合范数;孤立最弱  │
│   失效                 │ 不针对 port/stbd flip 事件       │
│ 选项2(精化)           │ warm-start shift-init(首要)+    │
│                        │ 混合范数(L2 航向控制+L1 速度)+  │
│                        │ 可选符号翻转检测(Tengesdal)    │
│   来源 [R24]           │ 文献组合实践;warm-start 保持同伦│
│                        │ 类是首要机制                     │
│   失效                 │ 机制多需协调(warm-start 正交)   │
│ 裁决                   │ **采纳选项2(分层组合)**         │
│ 理由                   │ [R24]明确 warm-start 是首要;纯  │
│                        │ L2-on-ψ 孤立最弱;混合范数+符号  │
│                        │ 翻转更针对实际失效模式           │
│ 风险                   │ 低-中(机制补强,非方案推翻)      │
└────────────────────────────────────────────────────────┘
```
- 裁决: **VR-04 精化 — 反 chattering 升级为分层组合**:① warm-start shift-init(首要,保持同伦类);② 转移代价含速度通道的混合范数(L2 航向控制修改量 + L1 速度,对齐 Eriksen tran_χ/tran_U);③ 可选条件符号翻转检测器(Tengesdal K_sgn·exp)针对 port/stbd flip;④ 配合 M6 RuleLatch + FSM hysteresis + neutral safe state(已 DP-01b/08)。w_trans 相对权(0.2-5 vs 碰撞 40)。

**Step5 结论**:
- 头号裁决 DP-05(NLP vs SB-MPC)= Step2 逐点确认(维持 NLP→acados),原 Step5 对比备查(弃 SB-MPC 证据链有效)。
- TBD-6/7 低风险采纳(混合 L1/L2 slack 惩罚 + 分层反 chattering),更新 VR-03/04。
- TBD-5 标 HAZID/试航待办,不阻塞。
- **全部 9 子模块 + DP-01a/b 裁决完整,无 DESIGN-IT-TWICE 残余对抗点。可进 Step6。**

---

#### 原 Step5 备查:NLP(方案A)vs SB-MPC(方案B)对比(裁决结论被 Step2 取代,弃 SB-MPC 证据链保留)

> 注:以下原 Step5 A/B 对比在 Step2 逐点确认前完成。其结论"弃 SB-MPC、维持 NLP"与 Step2 裁决方向一致,但 Step2 进一步精化为"IPOPT→acados"。保留此段作为弃 SB-MPC(ALT-12)的完整证据链。

**裁决标准**:①技术分解完整性 ②工程验证 ③失效边界已知可测 ④实现风险低。

- 方案 A(NLP 重构):Eriksen 中层 NLP 生产验证 + 本项目 ~5000 行投资 + v2.3 设计;失效边界 chattering(分层+warm-start 缓解);实现风险中。
- 方案 B(SB-MPC 转型):Johansen/Hagen/colav-simulator 验证;失效边界多船组合爆炸 k^N_target(击中用户 BL-07 核心场景)+ COLREGs 布尔代价可被压过([R3]);实现风险高(弃 5000 行重写)。
- 弃 SB-MPC 理由(证据链):①[R2]Johansen hazard penalty=[R3]"可被压过"风险;②用户 BL-07 多船核心场景,B 组合爆炸硬伤;③弃现有 NLP 投资;④[R4]ample-time 用分层(Mid 长+BC 短)而非单层 600s;⑤[R5]Eriksen 中层即 NLP。
- 方案 B 可取处吸收进 A(现 VR-09 A+):receding horizon(执行前 5-10s)+ 目标意图建模(intent_confidence)。
- 回炉触发(仍有效): 若 NLP+acados 落地后实时性无法支撑 360s horizon ample-time(SIL 证伪),带新矛盾证据回炉 DP-05 重评 SB-MPC+GPU。

### Step6 · 术语+技术规约+方案包  [2026-07-16 · 内容已裁决,待用户接受交付]

> **状态(2026-07-16)**: 术语表/TS 注册表/方案包内容基于 Step2(11DP 逐点确认)+ Step3(TBD-5/6/7 用户裁决)+ Step4(用户判别通过)。TS-06/07/13 中 TBD-6/7 精化(混合 L1/L2 slack、warm-start+混合范数反 chattering)已经用户确认,为权威。方案包 `solution-pack.md` 待用户接受后交付 brainstorming(本轮已重写,内容与已裁决一致)。

> **修订(Step3 后)**: 术语表/方案包原为 Step2 批量草稿,基于未逐点确认的旧裁决。现按 Step2 逐点用户确认的**权威裁决** + Step3 TBD-5/6/7 精化重写。关键变更:per-target-only σ→per-target per-step ξ;维持IPOPT→IPOPT→acados;90s→Eriksen360s/dt10s/replan60s;x=[psi;u]→x=[ψ,r,u]含ROT;维持T1→Eriksen终端路线+人工参考;长OU+意图→A+(OU+intent_confidence);纯L2 slack→混合L1/L2;纯L2-on-ψ转移→warm-start首要+混合范数。

#### 1. 术语表

| 术语 | 定义(引用) | 本方案含义 | 边界(不是什么) | 关联DP |
|---|---|---|---|---|
| Mid-MPC | 中层 NLP MPC,Eriksen ample-time 规划 [R5] | acados 求解 [ψ;r;u;ξ_{M·N}] 序列 → AvoidancePlan;horizon360s/dt10s/replan60s | 不是 L4 控制(出航向/速度命令);不是 SB-MPC | DP-01,05,06 |
| BC-MPC | 短期分支枚举 MPC,replan5s [R5] | 3 档急迫度候选,近距紧急跟踪+接管兜底;不做验证(验证归M7) | 不是主规划(NLP失败/近距时接管);不是 checker | DP-01,01a,08 |
| per-target per-step slack ξ | 每目标每步独立松弛 ξ_{m,n}≥0 [R2][R17a] | CPA 约束 d²-cpa_hard²+ξ_{m,n}≥0;**混合 L1/L2 惩罚 ρ·ξ+½w·ξ²**(TBD-6) | 不是 per-target-only(共享 horizon 步);不是纯 L2(精确性) | DP-03 |
| masking/free-riding | 单 slack 共享→一目标松弛全目标 CPA 放宽 [R2] | per-target per-step ξ 要消除的失效 | 不是求解器 bug,是公式结构缺陷 | DP-03 |
| exact-penalty 精确性 | Kerrigan 条件 ρ>‖λ*‖∞,仅 L1 成立 [R23] | slack 线性项 ρ>max\|λ*\| 保 feasible 时 ξ=0 | 不是 L2 可达(L2 s=0 梯度=0) | DP-03 |
| ample time | COLREGs Rule8:避让"尽早且明显" [R3][R4] | 需长前瞻(horizon >> 大幅转向/变速时间);360s | 不是单纯延长时域(需配 OU 不确定性) | DP-06 |
| SB-MPC | Simulation-Based MPC,离散行为集枚举 [R1][R4] | 本方案弃用(ALT-12),标待选演进;BC-MPC 是其近距版 | 不是 NLP(不做梯度优化) | DP-05 |
| receding horizon | 只执行 horizon 首 5-10s,余下重算 [R4] | A+ 吸收自 SB-MPC 的增强项 | 不是固定轨迹跟踪 | DP-05 |
| warm-start shift-init | 上周期解 shift 一步作本周期初值 [R24] | **首要反 chattering 机制**(保持同伦类) | 不是代价项(正交于转移代价) | DP-04,05 |
| 转移代价(混合范数) | 跨周期轨迹连续性惩罚 [R24] | Eriksen tran_χ(L2 航向控制)+tran_U(L1 速度);w_trans 0.2-5 vs 碰撞40 | 不是纯 L2-on-ψ(弱);不是唯一反chatter机制 | DP-04 |
| Nomoto 模型 | 1-DOF 偏航 Tṙ+r=Kδ [R1][R22] | NLP/BC 预测模型;存 T',K' 运行时缩放 T=(L/U)T',K=(U/L)K' | 不是完整动力学(忽略 sway);TBD-5 参数辨识 | DP-02 |
| OU 过程 + intent_confidence | 长时域有界化横向不确定性 + 意图标量 [R4][R21] | DP-09 A+:缩放 CPA 代价(高让路概率→低风险权重) | 不是完整意图分支(选项C需SB-MPC+GPU) | DP-09 |
| 四状态交接机 | MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE [R5][VR-01b] | BC 连续级联+交还hysteresis连续2周期+FINAL_DEGRADE报M7 | 不是失败计数为主(冻结风险) | DP-01b,08 |
| committed-route latching | 冻结计划:锁定上次 NLP 轨迹外推 [R5] | stale-age 门控(45s/15°/20%)要消除 | 不是正确保形(无动态避碰) | DP-08 |
| 人工参考轨迹 | 避让期用避让参考做 J_dist 基准 [R20] | 防 optimizer 为最小化 XTE 过早归航 | 不是 NLP 外独立 plan(是 NLP 内 J_dist 基准) | DP-07 |

#### 2. 技术规约表(六类,重构模式标与现状差异)

写入注册表 0.8 [TS](见下)。所有规约标来源 `[RNN]` 或 `DESIGN_DECISION`。

#### 注册表 0.8 [TS] 填充见下表(已含与现状差异列)。

---

**状态**: **已交付 brainstorming**(2026-07-16 用户接受方案包)。方案包 `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md` 为权威交付物。

---

## 跨树反馈修订(2026-07-17,来自 L3→L4 契约设计树)

> **来源**: `docs/superpowers/design-logs/2026-07-17-l3-l4-gnc-contract-design-log.md`(L3→L4 GNC 契约设计,Step2 模块①)
> **触发**: L3→L4 契约设计 grilling DP-02 时,用户质疑"360s 时域下 TailBuilder 几何续貂是否冗余",触发 Eriksen 原文 + NLM 三方高置信度查证,推翻本树 VR-06(360s)与 VR-07(人工参考轨迹/终端 C10/C11)的部分假设。
> **处理纪律**: append-only。不抹除原 VR-06/VR-07,新增 VR-06b/VR-07b 修订行 + 反馈块。原裁决保留作历史溯源。

### 新增裁决(修订 VR-06 / VR-07)

| ID | 裁决对象 | 结论(覆盖原 VR) | 采纳/弃用 | 理由 | 时间 |
|----|----------|------------------|-----------|------|------|
| VR-06b | DP-06 时域(**修订 VR-06**) | **horizon 从 360s 延长到 1200s**(dt=10s → Np=120,或 dt 适当加大);replan 保持 60s;BC 5s 不变 | 采纳(用户 2026-07-17) | [R19-修订] Johansen SB-MPC 用 600s 甚至 1200s(dt=30s/N=40)覆盖完整遭遇;用户 SIL 仿真观测完整避碰生命周期(避让→保持→返航)最长 900s;45m FCB 18kn 巡航下 20 分钟(1200s)覆盖一般避碰+返航;360s < 900s 致 myopia(premature return/chattering/稳定性丢失) | 2026-07-17 |
| VR-07b | DP-07 终端 + 人工参考(**修订 VR-07**) | **(a) 废弃"人工参考轨迹"**(因果倒置误读:Eriksen reference 始终是 nominal,用相对跟踪 t_b,从不切换成"避让参考");**(b) 位置代价纯二次 → Huber 损失**(Eq20-21,近原点二次/远处线性,防被障碍推开时指数回拉);**(c) 废除终端 C10(同侧)/C11(横向)**(VR-06b 长 horizon 1200s 下靠 horizon 保证收敛,不需终端集);**(d) 改相对轨迹跟踪 t_b**(每周期投影回 nominal route 找最近点) | 采纳(用户 2026-07-17) | [R18] Eriksen PDF 原文 + [R19-修订] NLM high:Eriksen 单一 NLP 相对跟踪+Huber 内部完成返航,无几何续貂,无终端集;C10/C11 是"防过早归航"误读的连带产物 | 2026-07-17 |

### 连带影响(须同步本树下游)

1. **TailBuilder 淘汰**:原范围声明(行7)把 TailBuilder 划为"开新决策树"——现裁决**淘汰老 TailBuilder**(几何 hold+rejoin 尾段拼接),因 VR-06b 长 horizon + VR-07b 相对跟踪+Huber 使 NLP 内部完成端到端轨迹(避让+保持+返航),无续貂。老 TailBuilder 是 VR-07"人工参考轨迹防过早归航"误读的补丁,根因消除后补丁亦消除。
2. **TS-05/TS-11 时域规约修订**:horizon 360s/Np36 → **1200s**(Np 待定,dt=10s 则 Np=120;若计算量过大可 dt=15-20s 降 Np,replan 60s 不变)。
3. **M5 输出流程简化**:NLP 解[ψ,r,u,x,y]→ 直接 trajectory(单一真相)→ preflight → publish。**无 TailBuilder 尾段拼接步骤**。这对 L3→L4 契约(DP-01 timed trajectory 主原语)是正面支撑。
4. **acados(VR-05)价值兑现**:1200s horizon 是 IPOPT O(n³) 不可承受的,正是 acados RTI O(n) 的用武之地。VR-06b 强化 VR-05 的迁移必要性。
5. **VR-07 原文保留作历史**:原 VR-07"人工参考轨迹防过早归航 + T1 降辅助"不删除,标"被 VR-07b 修订"。

### 开放项(反馈回 L3→L4 契约设计树)

- horizon 1200s 的 dt/Np 具体取值(dt=10s/Np=120 vs dt=15s/Np=80 vs dt=20s/Np=60)需在 M5 P1b acados 实施时 benchmark 实时性后定。本树不裁决,留给 M5 实现侧。
- 相对轨迹跟踪 t_b 的投影算法(本船→nominal route 最近点)是新增实现项,非本树原范围。

### P0–P7 phase 归属映射(2026-07-17 补)

> 跨树修订(VR-06b/VR-07b + GNC VR-01/02/05)已落地为 P0–P7 执行路线图,权威文档:
> `specs/2026-07-17-m5-mpc-p0-p7-roadmap.md`
>
> 本树原 VR-06(360s)/VR-07(人工参考+C10/C11)保留作历史溯源,phase scope **以路线图(修订后)为准**。

| 跨树裁决 | 内容 | 归属 phase(路线图) | 原 VR 状态 |
|---|---|---|---|
| VR-06b | horizon 360s→1200s | **P4**(Eriksen 分层时域) | 原 VR-06 写 360s,被修订 |
| VR-07b (a)(b)(d) | 废弃人工参考→相对跟踪 t_b+Huber | **P2**(终端路线)+ **P5**(位置代价) | 原 VR-07 写人工参考,被修订 |
| VR-07b (c) | 废除终端 C10/C11 | **P2**(终端路线) | 原 VR-07 未提,被修订 |
| VR-02 | 淘汰 TailBuilder | **P2 输出流程** | 原范围声明划"开新决策树",现裁决淘汰 |
| VR-01 | 新 TimedTrajectory 输出 | **契约兑现项**(新,跨 GNC-P1/P2) | 原未归 phase |
| VR-05 | preflight 4 调用点删硬编码 | **契约兑现项**(新) | 原未归 phase |

**两套 phase 编号**: 本树 P0–P7(M5 MPC 核心) ≠ GNC 迁移路线 P1–P4(VR-11,L3↔L4 接口迁移)。详见路线图 §2。

