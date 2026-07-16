# 设计日志: M5 MPC 方案下线

> **模式**: 重构(M5 已有 Mid-MPC + BC-MPC 实现)        **创建**: 2026-07-16
> **关联 spec**: docs/superpowers/specs/2026-07-16-design-grounding-skill-design.md(本日志是 design-grounding skill 的 smoke test)
> **状态**: Step1 进行中

## 0. 决策树状态(权威索引 · 可变快照)

### 0.1 决策点注册表 [DP]
| ID | 描述 | 类型 | 父/分解 | 状态 | 详见 |
|----|------|------|---------|------|------|
| DP-01 | MPC 整体架构:连续NLP vs 离散SB-MPC vs 混合 | 技术 | TD-01 | 未决 | — |
| DP-02 | 预测模型保真度:kinematic/Nomoto/3-DOF/MMG | 技术 | TD-01 | 未决 | — |
| DP-03 | 约束层级:硬约束 vs 软约束+slack vs 混合 | 约束 | TD-01 | 未决 | — |
| DP-04 | COLREGs规则编码方式:代价惩罚 vs 几何硬约束 | 约束 | TD-01 | 未决 | — |
| DP-05 | 求解器:IPOPT/SQP/RTI-SQP/枚举 | 技术 | TD-01 | 未决 | — |
| DP-06 | 预测时域长度(horizon) | 阈值 | TD-01 | 未决 | — |
| DP-07 | 参考跟踪方式:代价软实现 vs 硬终端约束 | 技术 | TD-01 | 未决 | — |
| DP-08 | 求解失败回退策略 | 技术 | TD-01 | 未决 | — |
| DP-09 | 不确定性处理:Nominal/Robust/Stochastic/NDO | 技术 | TD-01 | 未决 | — |

### 0.2 技术分解注册表 [TD]
| ID | 技术 | 分解子模块(→DP) | 触发步骤 |
|----|------|------------------|----------|
| TD-01 | MPC(M5 避碰) | 架构(DP-01), 预测模型(DP-02), 约束层级(DP-03), COLREGs编码(DP-04), 求解器(DP-05), 时域(DP-06), 参考跟踪(DP-07), 回退(DP-08), 不确定性(DP-09) | Step1 |

### 0.3 盲区注册表 [BL]
| ID | 问题 | 归属决策点 | 优先级 | 调研状态 |
|----|------|-----------|--------|----------|
| BL-01 | 90s时域是否合理?有研究用600s时域避碰,时域选择如何权衡? | DP-06 | 高 | 未闭环 |
| BL-02 | 大机动场景(右舵≥30°+变速)下,Nomoto vs 3-DOF/MMG哪个够用?VesselDynamicsModel为何未接入NLP? | DP-02 | 高 | 未闭环 |
| BL-03 | 单标量σ slack在多船场景的风险?本项目是否会遇到多船同时避碰?是否需要per-target slack? | DP-03 | 高 | 未闭环 |
| BL-04 | 现有Rule13无约束+Rule14/15/16/17硬编码简化,在SIL场景中导致了哪些规则违反/避让失败? | DP-04 | 高 | 未闭环 |

### 0.4 证据矩阵 [EV]
| ID | 来源类型 | 引用 | 检索置信 | 来源权威 | 场景适用 | 归属 |
|----|----------|------|----------|----------|----------|------|
| [R1] | NLM | colav_algorithms 笔记本:连续NLP vs SB-MPC | 高 | 高 | 高 | DP-01 |
| [R2] | NLM | ship_maneuvering 笔记本:模型选择权衡 | 高 | 高 | 高 | DP-02 |
| [R3] | NLM | colav_algorithms 笔记本:约束硬/软与slack优先级 | 高 | 高 | 高 | DP-03 |
| [R4] | NLM | colav_algorithms 笔记本:COLREGs两派(代价惩罚 vs 几何硬约束) | 高 | 高 | 高 | DP-04 |
| [R5] | NLM | colav_algorithms 笔记本:求解器选择(NLP/SQP/SB-MPC/凸近似/RL) | 高 | 高 | 高 | DP-05 |
| [R6] | NLM | colav_algorithms 笔记本:时域长度与chattering/精度错觉 | 高 | 高 | 高 | DP-06 |
| [R7] | NLM | colav_algorithms 笔记本:求解失败回退四路径 | 高 | 高 | 高 | DP-08 |
| [R8] | 代码库 | mid_mpc_nlp_formulation.cpp:恒速直线预测模型,NLP内部无Nomoto/动力学 | — | 仅本项目 | — | DP-02 |
| [R9] | 代码库 | constraint_compiler.cpp:Rule13无约束,Rule14/15/16/17是5°/10°硬编码简化版 | — | 仅本项目 | — | DP-04 |
| [R10] | 代码库 | mid_mpc_nlp_formulation.cpp:单标量σ slack,w_slack=1e8经验调参 | — | 仅本项目 | — | DP-03 |
| [R11] | 代码库 | mid_mpc_node.cpp:7层回退(失败计数→BC-MPC接管→几何弧形→degraded→tail-gate→NaN预检→空计划) | — | 仅本项目 | — | DP-08 |
| [R12] | 代码库 | config/m5_params.yaml:N=18, dt=5s, horizon=90s | — | 仅本项目 | — | DP-06 |
| [R13] | 代码库 | VesselDynamicsModel(4-DOF MMG)存在但未接入NLP | — | 仅本项目 | — | DP-02 |

### 0.5 场景注册表 [SC]
| ID | 场景描述 | 约束/边界 | 驱动决策点 |
|----|----------|-----------|-----------|
| SC-01 | COLREGs避让机动 | 右舵幅度≥30°,加减速明显,要求"尽早、明显" | DP-02, DP-06 |
| SC-02 | 多船同时避碰(可能) | 多目标CPA约束同时激活 | DP-03 |

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
- [R1] NLM colav_algorithms 笔记本(id 9387989c): 连续NLP vs 离散Scenario-Based MPC 争论主线
- [R2] NLM ship_maneuvering 笔记本(id 133ccbd1): kinematic/Nomoto/3-DOF/MMG 模型选择权衡
- [R3] NLM colav_algorithms 笔记本: 硬约束(物理量) vs 软约束+slack(状态/路径/安全),优先级层次
- [R4] NLM colav_algorithms 笔记本: COLREGs两派—启发式代价惩罚 vs 硬仿射/几何约束
- [R5] NLM colav_algorithms 笔记本: IPOPT(精但慢)/SQP/RTI-SQP(快4-5倍)/SB-MPC枚举/凸QP/RL
- [R6] NLM colav_algorithms 笔记本: 长时域精度错觉与trajectory chattering,趋势短时域+终端约束
- [R7] NLM colav_algorithms 笔记本: 不可行性处理/warm-start次优/多规划器冗余/时域替代表述
- [R8] PROJECT_FACT: src/mid_mpc/mid_mpc_nlp_formulation.cpp:160-162,198-203 — 恒速直线运动学,pos积分
- [R9] PROJECT_FACT: src/mid_mpc/constraint_compiler.cpp:166-282 — Rule13仅审计标记,Rule14/15/16/17为5°/10°硬编码简化版
- [R10] PROJECT_FACT: src/mid_mpc/mid_mpc_nlp_formulation.hpp:121-122, mid_mpc_nlp_formulation.cpp:620-624 — 单标量σ slack, w_slack=1e8
- [R11] PROJECT_FACT: src/mid_mpc/mid_mpc_node.cpp:819-1090 — 7层回退:失败计数→BC-MPC→几何弧形→degraded→tail-gate→NaN预检→空计划
- [R12] PROJECT_FACT: config/m5_params.yaml:4-6 — N=18, dt=5s, horizon=90s
- [R13] PROJECT_FACT: src/mid_mpc/vessel_dynamics_model.cpp:37-60 — 4-DOF MMG简化线性模型存在,仅被TrajectoryPropagator用,未接入NLP

---

## 演进日志(append-only · 时序 · 不可覆盖)

### Step1 · 行业调研·发现决策点  [2026-07-16]

**模式判定**: 重构。M5 已有完整 Mid-MPC(CasADi/IPOPT NLP)+ BC-MPC(分支枚举)实现。现有代码/设计是主证据之一(机制B重构模式),外部权威(NLM)用于验证/补强/纠偏。

**快调来源**:
- NLM domain:colav_algorithms(266 sources)+ domain:ship_maneuvering(54 sources),5条查询全 high 置信
- 代码库: codegraph_explore M5 源码 + 读 5 份 m5 spec/plan

**主干决策维度提取**(NLM 7轴 + 代码库现状对照):
1. 连续NLP vs 离散SB-MPC(业界头号争议)[R1] — 现状:Mid-MPC=NLP, BC-MPC=枚举(已是混合)
2. 预测模型保真度 kinematic/Nomoto/3-DOF/MMG [R2] — 现状:恒速直线[R8],Nomoto仅fallback且r₀=0,VesselDynamicsModel未接入NLP[R13]
3. 约束硬/软与slack优先级 [R3] — 现状:混合,单标量σ slack w_slack=1e8[R10]
4. COLREGs编码:代价惩罚 vs 几何硬约束 [R4] — 现状:Rule13无约束,Rule14/15/16/17硬编码简化[R9]
5. 求解器:IPOPT/SQP/RTI-SQP/枚举 [R5] — 现状:CasADi/IPOPT,max_iter=800,max_cpu_time=3s
6. 时域长度与chattering [R6] — 现状:N=18/90s[R12],90s曾被认为合理但问题重重
7. 求解失败回退 [R7] — 现状:7层回退[R11]
8. (次级)不确定性处理 Nominal/Robust/Stochastic/NDO — 现状:Nominal(未建模)
9. (次级)凸近似保守性 — 现状:不适用(NLP非凸)

**技术分解触发**(机制C):
- TD-01 MPC 分解为 9 个子模块决策点(DP-01~DP-09)
- 不可停留在"用 MPC"这一层——这正是之前返工的根因

### Step2 · grilling 压力测试  [2026-07-16]

对 9 个决策点逐个三视角压力测试,重点 grilling 返工根因(DP-02/03/04/06)。

**[grilling] DP-02 预测模型保真度**
- [专家] NLM ship_maneuvering [R2] 选择规则:kinematic(长程,算力低)/Nomoto(参数少,忽略sway)/3-DOF MMG(高保真,昂贵)。长视野下kinematic与3-DOF差异小。
- [新手] 现状恒速直线 [R8]——比kinematic还简。Nomoto仅fallback且r₀=0。VesselDynamicsModel存在但未接入NLP [R13]。之前没做这个决策,默认走最简版。
- [悲观] 恒速直线在大转向角/减速机动时预测严重偏离→MPC"以为可行"的解实际偏离→轨迹chattering。这是M5返工核心机制之一。
- → 用户确认:COLREGs避让右舵≥30°+加减速明显,恒速直线必然失效(场景 SC-01)
- → 盲区 BL-02:Nomoto vs 3-DOF/MMG 哪个够用?VesselDynamicsModel为何未接入NLP?

**[grilling] DP-03 约束层级**
- [专家] NLM [R3]:硬约束只用于物理量(舵角/推力),软约束+slack用于状态/路径/安全,不同slack不同权重形成优先级层次。
- [新手] 现状混合:CPA用σ slack但单标量共享所有行 [R10],w_slack=1e8经验调参。
- [悲观] 单σ在多船场景过度放松:任一目标需松弛→所有目标CPA同时放宽→安全边界崩塌。
- → 盲区 BL-03:多船场景频率?单σ风险?需per-target slack?

**[grilling] DP-04 COLREGs编码**
- [专家] NLM [R4] 两派:启发式代价惩罚(柔性应急,Rule2破规)vs 几何硬约束(保凸性,求解快)。
- [新手] 现状:Rule13无约束(仅审计标记),Rule14/15/16/17是5°/10°硬编码简化 [R9],与formulation层direction/min_alt行职责重叠。
- [悲观] 硬编码度数无几何感知,窄水域/高速场景可能不足或过度;职责重叠可能导致求解器冲突。
- → 盲区 BL-04:SIL场景中哪些规则失效了?

**[grilling] DP-06 时域长度**
- [专家] NLM [R6]:长时域精度错觉(目标船是动态智能体,15分钟后误差椭圆巨大)→trajectory chattering。趋势短时域+终端约束。
- [新手] 现状N=18/90s [R12],曾被认为合理但问题重重。
- [悲观] 90s可能太短(COLREGs要求ample time)或太长(不确定性放大)。用户指出有研究用600s时域。
- → 盲区 BL-01:90s是否合理?600s可行?如何权衡?

**新增场景**:
- SC-01: COLREGs避让机动,右舵≥30°+变速,要求尽早明显
- SC-02: 多船同时避碰(可能)

**新增盲区**(全部优先级高,因为都是返工根因):
- BL-01: 时域选择权衡(DP-06)
- BL-02: 预测模型选择Nomoto vs 3-DOF/MMG + VesselDynamicsModel接入(DP-02)
- BL-03: 单σ vs per-target slack + 多船场景(DP-03)
- BL-04: COLREGs简化规则在SIL的失效(DP-04)
