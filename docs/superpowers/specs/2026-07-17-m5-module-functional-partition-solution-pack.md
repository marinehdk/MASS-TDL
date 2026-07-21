# M5 模块功能划分完备性 方案包

> **产出**: 2026-07-17 design-grounding Step6
> **决策树日志**: `docs/superpowers/design-logs/2026-07-17-m5-module-functional-partition-design-log.md`
> **范围**: M5 作为 TDL 核心模块的功能职责划分——完备性审查、子功能边界、TDL内部对接、L4对接。**不含** MPC 内部算法（归 `2026-07-16-m5-mpc-colav` DP-01~09）和 L3→L4 接口细节（归 `2026-07-17-l3-l4-gnc-contract` 11 VR）。
> **遵循前提**: 两个并行对话的已裁决架构（TailBuilder淘汰/horizon1200s/双层MPC/TimedTrajectory/承诺前缀180s/acados/四状态机）。推翻需回炉对应决策树。

## 方案包契约（brainstorming 权限边界）

- ✅ 可做：M5 模块功能划分的工程细节设计（子功能边界/数据流/控制流/错误处理/测试/分阶段实施），已裁决方案内优化
- ❌ 不可做：推翻已裁决核心方案（双层MPC/horizon1200s/TimedTrajectory/TailBuilder淘汰），除非发现新矛盾证据则回炉 design-grounding
- ❌ 不可做：重提已弃用方案（ALT-01~12）
- ❌ 不可做：修改 MPC 内部算法裁决（归另对话）和 L3→L4 接口裁决（归另对话）

---

## 组件 1: 术语表

| 术语 | 定义 | 本方案含义 | 边界（不是什么） | 关联DP |
|---|---|---|---|---|
| 职责维度 | M5 必须承担的一级功能领域 | 自顶向下从M5核心职责推导的9个领域 | 不是代码目录（代码目录是"怎么实现"） | DP-01 |
| 候选健康状态机 | M5内部的四状态机(MID_NORMAL→BC_TAKEOVER→HANDOVER_NEUTRAL→FINAL_DEGRADE) | 选择哪个候选源生效 | 不是MRM决策权（归M7）；只做候选选择 | DP-05 |
| 契约管理器 | committed_route演化后的角色：只管version/承诺前缀冻结/publish门控 | TailBuilder淘汰后的新角色 | 不做几何拼接（旧"拼接器"角色已废） | DP-06 |
| 承诺前缀冻结 | NLP equality行强制前K步=冻结WGS84几何 | 几何冻结（非publish语义冻结） | 不是冻结publish（几何可变但不发新版）——是几何真的不变 | DP-06/DP-11 |
| SafetyConcernEvent | M5→M7的fail-safe上报消息 | M5不持safety authority，只emit concern | 不是MRM命令（MRM决策归M7）；不是日志（须ROS2 publish） | DP-08 |
| ODD门控映射 | M1 6状态FSM→M5行为的映射 | NORMAL全速/DEGRADED降参数/OUT冻结OVERRIDDEN/MRC进concern链 | 不是M5自判ODD（ODD authority归M1） | DP-09 |
| Doer自检 | M5 preflight（turn-radius/segment/continuity） | 发布前的快速失败自门控 | 不是feasibility authority（L4 GNC有最终accept/reject权） | DP-07 |

## 组件 2: 技术规约表（职责边界/所有权/契约层面）

> 坐标系/单位/符号等已在 `2026-07-16-m5-mpc-colav` TS-01~04 和 `2026-07-17-l3-l4-gnc-contract` TS-01~13 裁决，本方案不重复。本表聚焦**职责边界/所有权/契约**层面。

| ID | 类别 | 规约内容 | 来源 | 关联DP | 与现状差异 |
|---|---|---|---|---|---|
| TS-A01 | 职责边界 | Mid-MPC(DP-02)仅产裸候选轨迹MidMpcSolution，组装成TimedTrajectory归DP-06。DP-02不持航线所有权/不做仲裁/不做预检 | DESIGN_DECISION[VR-02] | DP-02/06 | 现状mid_mpc_node混在一起(2196行)，须拆解 |
| TS-A02 | 所有权 | COLREGs语义推理归M6，数学约束编译归M5(constraint_compiler)。M5只翻译不推理 | [R5]+[R8]VR-04 | DP-04 | 已基本落地 |
| TS-A03 | 所有权 | 候选选择归M5(四状态机)，MRM触发权归M7。M5只emit SafetyConcernEvent | [R3][R16]+[R8]VR-08 | DP-05/08 | **M5无publish，须加** |
| TS-A04 | 契约 | SafetyConcernEvent: M5 publish到/l3/safety/concern；M7 on_safety_concern接入MRM仲裁(非空操作)；failure_type enum(NLP_INFEASIBLE/CPA_BREACH/BC_ALSO_FAILED/SOLVER_TIMEOUT)+severity+context | DESIGN_DECISION[VR-08]+Step3代码核实 | DP-08 | **两端都缺，须建** |
| TS-A05 | 契约 | M7/M1→M5冻结返回: ODD/MRM触发后M5须冻结(status=OVERRIDDEN)。返回路径须定义(/l3/m5/freeze或经M1 mode_cmd) | DESIGN_DECISION[VR-08/09] | DP-08/09 | **完全缺失** |
| TS-A06 | 契约 | ODD门控: M5订阅/l3/m1/odd_state；M1 6状态(In/Edge/Out/MrCPrep/MrCActive/Overridden)→M5行为映射(NORMAL/DEGRADED/OUT/MRC)；M5_RESUME回切 | DESIGN_DECISION[VR-09]+[R4][R16] | DP-09 | **零订阅+FSM不对齐，须建** |
| TS-A07 | 职责边界 | preflight=Doer自检(非authority)。裁决优先级: GNC reject > M5 preflight reject > heartbeat | [R5][R9][R16] | DP-07 | 方向已定，4调用点硬编码须改 |
| TS-A08 | 职责边界 | TailBuilder拼接须清理；NLP单段直生全horizon几何；committed_route演化为契约管理器 | [R8]VR-02/07b+DESIGN_DECISION[VR-06] | DP-06 | **拼接仍活跃，须清理** |
| TS-A09 | 所有权 | warm-start(prefix重投影+suffix冷启动)与承诺前缀冻结(equality行)正交无冲突 | Step3代码核实 | DP-11 | 已正确实装，无差异 |

## 组件 3: 决策卡片集（Step5 最终裁决）

### DP-06 航线所有权重组 ★★★★☆
- 采纳：NLP单段直生 + committed_route契约管理化（方案A）
- 弃用：保留拼接参数化（ALT-08/09）——过渡期永远技术债+双重真相源

### DP-08 MUST-9两端断修复 ★★★★☆
- 采纳：完整两端修复 M5 publish + M7仲裁 + 冻结返回（方案A）
- 弃用：仅M5日志增强（ALT-10）——不构成fail-safe链路

### DP-09 ADR-1 ODD门控补全 ★★★★☆
- 采纳：完整门控 订阅+FSM映射+行为+回切（方案A）
- 弃用：M5内部保守降级（ALT-11/12）——违反外部manager原则+视角不全

### 其余8 DP（低/中风险，用户授权跳过DESIGN-IT-TWICE）
- DP-01框架完备✓ / DP-02机动生成边界✓ / DP-03双层分工(归P6)✓ / DP-04约束编译✓ / DP-05候选仲裁(MRM归M7)✓ / DP-07预检(Doer自检)✓ / DP-10审计(工程债)✓ / DP-11稳定性(正交无冲突)✓

## 组件 4: 证据矩阵

见决策树日志注册表 0.4 [EV]（[R1]-[R17]）。关键证据：
- [R14] NLM colav_algorithms: 长 horizon MPC 4内部子组件 + 学术界对单一长horizon NMPC的争议
- [R16] NLM safety_verification: IEC61508/ISO21448/DNV/MASS Code 确认5辅助职责标准硬要求
- [R17] NLM colav_algorithms: 长 horizon ODD前瞻学术实践（receding horizon replan防御）
- Step3代码核实: 承诺前缀冻结已实装 / warm-start正交 / TailBuilder活跃拼接 / MUST-9两端断 / M7 HC已实现(纠正arch:934) / ODD零订阅

## 组件 5: 技术分解完整树

TD-01 M5模块功能划分(自顶向下) → 11子决策点(DP-01~11)，全为架构/接口型，无DECOMPOSITION_INCOMPLETE。
- DP-02含4内部子组件(预测/约束/代价/求解器)，已在[R8]的TD-01(mpc-colav)裁决。

## 组件 6: 弃用方案及理由

见决策树日志注册表 0.7 [ALT-01~12]。关键弃用：
- ALT-04 M5自决MRM（违反Doer-Checker）
- ALT-08/09 保留TailBuilder拼接/committed_route仍拼接（与新架构冲突）
- ALT-10 仅M5侧concern不改M7（不构成链路）
- ALT-11/12 M5自判ODD/仅订阅不定义行为（违反外部manager）

## 组件 7: 需求场景 + 验收边界

| SC | 场景 | 验收边界 | 驱动DP |
|---|---|---|---|
| SC-01 | COLREGs完整避碰生命周期(onset→active→release→rejoin) | NLP单段1200s端到端覆盖，无TailBuilder拼接，轨迹连续稳定 | DP-06 |
| SC-02 | NLP持续失败→BC-MPC→FINAL_DEGRADE | M5 emit SafetyConcernEvent→M7 MRM→M5冻结，端到端时延<TMR60s | DP-08 |
| SC-03 | OUT-of-ODD(感知退化/海况超限) | M5收M1 ODD→冻结OVERRIDDEN不发新plan；DEGRADED→CPA_safe切ODD-B | DP-09 |
| SC-04 | target突然机动(heading>15°/CPA漂>20%) | heading/drift触发须激活(当前死代码须修)→DegradedHold | DP-05 |
| SC-05 | TailBuilder拼接清理后新架构落地 | NLP单段直生全horizon，committed_route只管version/prefix/publish | DP-06 |

## 组件 8: 已知冲突与未闭环盲区

| 项 | 状态 | 处理 |
|---|---|---|
| [R14]长horizon NMPC学术争议(Eriksen/Johansen不鼓励) | 诚实记录 | 由[R8]acados RTI+四状态机兜底；本对话不裁决（归另对话） |
| BL-05 硬编码迁移落地度 | 待代码核实 | [R8]VR-04已裁决方向 |
| BL-07 heading/drift触发死代码 | 已知bug | 须修 |
| BL-10 预检范围(整条vs前缀) | 待定 | 新架构落地时定 |
| BL-16 acados后solver status审计适配 | 待[R8]P1b | 实施时处理 |
| BL-18 稳定性逻辑集中vs分散 | 代码组织 | 实施时定 |

---

## 移交 brainstorming

本方案的核心技术决策（11 DP职责划分 + 3架构断裂修复方向）已通过 design-grounding 裁决。brainstorming 负责：
- M5模块功能划分的工程细节设计（子功能拆解/数据流/控制流/错误处理/测试）
- 3个架构断裂修复的实施细化（DP-06清理顺序/DP-08 concern msg定义/DP-09 ODD映射表）
- 已裁决方案内优化

不得推翻已裁决方案/重提弃用方案/修改技术规约，除非发现新矛盾证据则回炉 design-grounding。
