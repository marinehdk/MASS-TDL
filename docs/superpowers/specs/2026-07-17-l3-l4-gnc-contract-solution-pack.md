# 方案包: L3 TDL → L4 GNC 控制器无关契约

> **产出**: design-grounding Step6,2026-07-17
> **决策树日志**: `docs/superpowers/design-logs/2026-07-17-l3-l4-gnc-contract-design-log.md`
> **核心目标**: 规范 M5(L3 战术避碰层)对外接口,使 L4 GNC 组件(PID↔MPC)替换时 L3 不跟着频繁改
> **模式**: 重构(既有 M5→gnc_bridge→ARM→ship_guidance 链路 + 既有 spec)
> **状态**: 设计值版(SIL 校准承诺前缀 180s 待一周后定稿);已与 M5 MPC 设计树跨树同步

---

## 方案包契约(brainstorming / Spec 同步权限边界)

- ✓ **可做**: 工程细节设计(消息字段/数据流/错误处理/测试/分阶段实施),已裁决方案(VR-01..11)内优化拔高
- ✗ **不可做**: 推翻已裁决核心方案,除非发现**新矛盾证据**(回炉 design-grounding)
- ✗ **不可做**: 重提已弃用方案(ALT-01..09)
- ✗ **不可做**: 擅自修改技术规约(TS-01..13),需改则回 design-grounding
- ⚠ **须推动**: SIL 校准承诺前缀 180s(一周后);dt 三档 benchmark(10/15/20s);bridge 跨 domain TRANSIENT_LOCAL 端到端确认

---

## 组件 1: 术语表

| 术语 | 定义 | 本方案含义 | 边界(不是什么) | 关联DP |
|---|---|---|---|---|
| TimedTrajectory | 时间参数化连续轨迹(Eriksen 连续 SOG+course)[R15] | M5→L4 主原语,含 safety_intent+execution_policy+segment_source | 不是离散航点(ALT-01);不是 corridor(ALT-02);不是 guidance-extension Δ(ALT-03) | DP-01 |
| reference adapter | L4 侧把 trajectory 翻译成各 L4 消费形态的适配器 [R15] | PID→航点 sampler;MPC→直接跟踪;归 L4 各自 | 不是 M5 侧转换(否则换 L4 改 M5) | DP-08 |
| 承诺前缀 (committed prefix) | L4 必跟踪、M5 保证不推翻的轨迹前段 [R4] | 180s(NLM 推荐 120-180s 上端),覆盖 MPC 90s 最小预测+PID wheel-over+planner 失败 2 周期余量 | 不是整个 trajectory;预测尾段可变 | DP-03 |
| material-change | 触发承诺前缀更新的判据 | role/direction 变/CPA risk 变/GNC REJECTED/偏航超阈/M7 VETO/紧急升级 | 非心跳重发(不变 version) | DP-03 |
| GncExecutionOdd | L4 执行能力消息(runtime capability) | **HARD guaranteed-feasible bound**:PID 发物理饱和极限,MPC 发 constraint-tightening 收紧后保守硬界;QoS latched | 不是 MPC 内部软代价(R22);不是 config 文件 | DP-04 |
| constraint tightening | robust MPC 给上游的保守硬界(物理硬限−tube margin)[R22] | MPC-L4 发布的 capability 语义 | 不是 MPC 优化用的软约束 | DP-04 |
| effective trajectory(层3) | L4 短时域前向仿真预测轨迹(30s)[R16] | L4 adapter 用自己模型预测发布,M5 据此重算前瞻 CPA | 不是全量回传长轨迹(ALT-08);不是 M5 侧预测(ALT-09) | DP-06 |
| safety_intent | trajectory 不可变元数据块 | passing_side/avoidance_direction/active_rules/role/forbidden_regions/min_clearance/no_return_before | L4 不得改;非 execution_policy(容差内可调) | DP-07 |
| ReactiveOverrideCmd | BC-MPC 紧急接管直接控制指令 | heading/speed/ROT,validity 1-3s,独立 emergency 通道 | 非 trajectory(紧急需直接指令);契约定义,L4 接线下游 | DP-10 |
| 相对轨迹跟踪 (t_b) | 每周期投影回 nominal route 找最近点 [R18] | M5 内部参考锚定机制(reference 始终 nominal) | 不是 L4 跟踪方式(L4 无需感知);非人工避让参考(已废弃) | DP-02 |

---

## 组件 2: 技术规约表(六类)

| 类别 | 规约(权威) | 来源 | 与现状差异 |
|---|---|---|---|
| 坐标系 | WGS84/NED;body(x艏前/y左舷) | TS-01/02 [R6] | 一致(与 M5 TS-01/03 对齐) |
| 物理量单位 | 内部 rad/m/s;消息 deg;timestamp ROS2 steady | TS-03 [R6] | 一致 |
| 符号 | ψ 右舷正;ROT 右转正;l 右舷正 | TS-02 [R6] | 一致 |
| **主原语** | **TimedTrajectory**: {t,north,east,heading,speed,yaw_rate,curvature,acceleration} + safety_intent + execution_policy + segment_source + plan_id+version | TS-04/11/12 [VR-01/10] | **新建消息(替代航点为主)** |
| **时序** | **horizon=1200s**(dt 可调 10/15/20s,Np=120/80/60 benchmark),replan=60s;**承诺前缀=180s**,尾段=1020s;BC=5s | TS-05 [VR-02/06] | **90s→1200s;承诺前缀新增** |
| **capability** | **GncExecutionOdd = HARD guaranteed-feasible bound**,QoS TRANSIENT_LOCAL+RELIABLE(latched);PID 发物理饱和,MPC 发收紧硬界 | TS-07 [VR-05/R22] | **M5 preflight 改读订阅删硬编码** |
| **反馈** | 三层:即时状态(已有)+applied 限幅+radius/decel(bridge 补 8 字段)+30s L4 adapter 前向仿真预测 | TS-08 [VR-09] | **层2 补字段+层3 新增** |
| **accept/reject** | execution_state enum 复用 + reason 词汇表扩展(trajectory 词汇) | TS-09 [VR-07] | enum 复用+reason 扩展 |
| **override** | ReactiveOverrideCmd 独立 emergency 通道,validity 过期回 trajectory 跟踪 | TS-10 [VR-08] | 契约定义;L4 接线下游 |
| M5/M2/M7 职责 | M2=即时 CPA;M5=前瞻 CPA;M7=独立校验+safety_intent | TS-13 [VR-09/10] | 职责明确化 |

(完整 13 条 TS-01..13 含来源/单位/关联DP/与现状差异,见决策树日志注册表 0.9)

---

## 组件 3: 决策卡片集(11 裁决,经 DESIGN-IT-TWICE 对抗验证无回炉)

| DP | VR | 裁决 | 一句话理由 | 证据链 |
|---|---|---|---|---|
| DP-00 | — | L3 控制器无关(M5 不分支 backend、不硬编 L4 参数) | 核心目标:换 L4 不改 M5 | 全 DP 分解 |
| DP-01 | VR-01 | timed trajectory 主原语,L4 adapter 各自适配 | Eriksen 连续轨迹 PID/MPC 通用 | [R4][R15] |
| DP-02 | VR-02 | 淘汰 TailBuilder,NLP 内部端到端返航(horizon 1200s) | Eriksen 相对跟踪+Huber,长 horizon 保收敛 | [R18][R19][R21] |
| (内部) | VR-03 | 废弃人工参考→相对跟踪 t_b+Huber;废除 C10/C11 | Eriksen reference 始终 nominal | [R18][R19] |
| DP-03 | VR-06 | 承诺前缀 180s + material-change + version 语义 | NLM 双 L4 需求+Eriksen planner-failure 余量 | [R4][R23] |
| DP-04/11 | VR-05 | 单一真相 GNC overlay→GncExecutionOdd,M5 只读删硬编码 | config 常态+本项目已订阅 latched | [R16][R22] |
| DP-05 | VR-07 | enum 复用 + reason 词汇表扩展 | 无 marine 标准 enum,现有够用 | [R10][R17] |
| DP-06 | VR-09 | 三层反馈(即时+applied补全+30s L4 adapter 预测) | NLM 短时域预测足够 | [R16][R9] |
| DP-07 | VR-10 | safety_intent immutable + M7 校验 | 工程纪律(非法规强制) | [R17] |
| DP-08 | VR-04 | 转换器归 L4 reference adapter | retrofit 友好,换 L4 只改 adapter | [R15] |
| DP-09 | VR-11 | P1-P4,M5 一次改到位 P2/P3 零改动 | 控制器无关兑现路径 | 前 5 模块综合 |
| DP-10 | VR-08 | override 独立通道,契约定义,L4 接线下游 | 紧急需直接指令 | [R4][R15] |

---

## 组件 4: 证据矩阵(完整溯源 R1-R23)

| ID | 来源类型 | 关键结论 | 归属 |
|---|---|---|---|
| [R1] | IMO MASS Code §9 | VDR 法规;goal-based 不规定内部契约 | DP-06/07 |
| [R2] | DNV-CG-0264 | DS/SE 元素;goal-based | DP-06/07 |
| [R3] | CCS i-Ship(N) | 实施 COLREGs 决策;goal-based | DP-07 |
| [R4] | Eriksen 2020 T-IV | 连续轨迹+committed prefix+planner-failure reuse | DP-01/02/03 |
| [R5] | Hagen 2018 ICRA | guidance-extension retrofit 友好 | DP-02/08 |
| [R6] | Kufoalor 2018 IROS | capability 参数 a_max/r_max | DP-04 |
| [R7] | ROS2 Nav2 FollowPath | accept/reject 跨域类比 | DP-05 |
| [R8] | Kongsberg K-MATE | adaptive waypoint 标准接口 | DP-01/02 |
| [R9-R14] | 代码库 | ship_guidance/bridge/ARM/M5/preflight 现状 | 各 DP |
| [R15] | NLM colav(控制器无关) | guidance-extension+Eriksen 连续轨迹+RTZ 工业 | DP-01/02/08 |
| [R16] | NLM ship_maneuvering | capability config-only+离线 CADCA;反馈短时域仿真 | DP-04/06 |
| [R17] | NLM maritime_reg | COLREGs/IMO/DNV/CCS 不规定内部契约;工程纪律 | DP-07 |
| [R18] | Eriksen PDF 原文 | 相对跟踪 t_b+Huber,单一 NLP 返航,无 TailBuilder | DP-02 |
| [R19] | NLM(TailBuilder 裁决) | Eriksen 返航在 NLP 内部,TailBuilder 冗余 | DP-02 |
| [R20] | 用户仿真观测 | 完整生命周期最长 900s | DP-02 |
| [R21] | NLM(myopia) | horizon<生命周期致 myopia;收敛两路 | DP-02 |
| [R22] | NLM(MPC capability) | 物理极限硬约束;constraint tightening;统一硬界语义 | DP-04 |
| [R23] | NLM(承诺前缀) | MPC 最小90s;PID 需3航点+wheel-over;推荐120-180s | DP-03 |

(完整 R1-R23 含参考文献,见决策树日志 `## 参考文献`)

---

## 组件 5: 流程分解树(6 大模块)

```
L3→L4 控制器无关契约 (DP-00 父原则)
├ ① 参考编码 (DP-01/02/08 + VR-03)
│   ├ DP-01 ✓VR-01 trajectory 主原语
│   ├ DP-02 ✓VR-02 淘汰 TailBuilder,horizon 1200s
│   ├ VR-03 ✓ 废弃人工参考+Huber+废除C10/C11
│   └ DP-08 ✓VR-04 转换器归 L4 adapter
├ ② 能力协商 (DP-04/11)
│   ├ DP-04 ✓VR-05 单一真相 GncExecutionOdd,M5 只读
│   └ DP-11 ✓(合并入 DP-04)
├ ③ 可行性接受 (DP-03/05/10)
│   ├ DP-03 ✓VR-06 承诺前缀 180s + material-change + version
│   ├ DP-05 ✓VR-07 enum 复用 + reason 扩展
│   └ DP-10 ✓VR-08 override 独立通道,契约定义
├ ④ 执行反馈 (DP-06)
│   └ DP-06 ✓VR-09 三层反馈(即时+applied补全+30s预测)
├ ⑤ 战术意图边界 (DP-07,横切)
│   └ DP-07 ✓VR-10 safety_intent immutable + M7 校验
└ ⑥ 迁移兼容 (DP-09,纵切)
    └ DP-09 ✓VR-11 P1-P4,M5 一次改到位 P2/P3 零改动
DECOMPOSITION 闭环: ✓ 全部 11 DP 裁决,无 INCOMPLETE
```

---

## 组件 6: 弃用方案及理由(ALT-01..09)

| ALT | 方案 | 弃用理由 | 证据 |
|---|---|---|---|
| ALT-01 | 纯离散航点作主原语 | 无法表达速度剖面;换 MPC 失效 | [R4][R15] |
| ALT-02 | corridor 作主原语 | 无标准/学术/商业先例 | [R15] |
| ALT-03 | guidance-extension Δ 作主原语 | 需 L4 暴露 LOS 参考,碰 L4 内部 | [R15] |
| ALT-04 | 老 TailBuilder(几何 hold+rejoin) | Eriksen 范式冗余 | [R18][R19] |
| ALT-05 | 人工参考轨迹(防过早归航) | 因果倒置误读 | [R18][R19] |
| ALT-06 | M5 头文件硬编 L4 包络 | 两处维护必漂移 | [R14] |
| ALT-07 | M5 多套 L4 配置分支 | 违反控制器无关 | DP-00 |
| ALT-08 | 全量回传长轨迹 | 带宽浪费,短时域足够 | [R16] |
| ALT-09 | M5 侧维护 L4 动力学模型 | 耦合,违反控制器无关 | DP-00 |

---

## 组件 7: 需求场景 + 验收边界

| SC | 场景 | 约束 | 验收(方案须通过) | 驱动DP |
|---|---|---|---|---|
| SC-01 | Mid-MPC 1200s 重规划,L4 持续跟踪分钟级轨迹 | replan 60s,承诺前缀 180s | L4 承诺前缀内跟踪稳定;material-change 才更新 version | DP-02/03 |
| SC-02 | NLP 失败→BC-MPC takeover→override 直接驱动 L4 | BC 10Hz,validity 1-3s | override 契约定义;L4 接线后紧急避碰命令落地 | DP-10 |
| SC-03 | L4 接受但限速(3.2 cap)→M5 用实际速度重算 CPA | EXECUTING_WITH_LIMIT | bridge 补 applied+radius/decel;M5 用层2 重算 | DP-05/06 |
| SC-04 | 同一 plan_id 版本更新不重置 L4 跟踪器 | route_update_guard | 承诺前缀不变→version 不变;L4 warm-start | DP-03 |
| SC-05 | GNC 拒绝(turn_radius_too_small)→M5 确定性 replan | ARM feasibility | REJECTED→M5 重规划/降级 | DP-05 |
| SC-06 | past-and-clear 后返航(NLP 内部完成,无 TailBuilder) | horizon 1200s 覆盖 900s 生命周期 | 返航在 NLP 内收敛;无几何续貂 | DP-02 |
| SC-07 | 未来切 MPC-L4,同一 M5 输出无需分支 | controller-agnostic | P3 切换零 M5 改动 | DP-02/09 |
| SC-08 | 认证审查需要 L3 计划/L4 执行/实际闭环证据链 | CCS i-Ship/SIL2 | 三层反馈+safety_intent+M7 校验可追溯 | DP-06/07 |
| SC-09 | M5 LifecycleState 切换,L4 区分承诺前缀微调 vs BC 接管 | 四状态机 | plan_id+version+command_source 表达 | DP-03/10 |

---

## 组件 8: 已知冲突与未闭环盲区

**已知冲突(Step5 已裁决对抗验证)**:
- DP-02 horizon 1200s 实时性 vs IPOPT 不可承受 → acados(VR-05)兑现;dt 可调缓解(10/15/20s benchmark)
- DP-06 L4 adapter 预测 vs M5 侧预测 → L4 adapter 胜(控制器无关+精度)

**未闭环盲区(无阻塞)**:
- SIL 校准承诺前缀 180s(延后一周,用户跑 900s 场景验证双 L4 跟踪稳定)
- dt 三档 benchmark(10/15/20s,留 M5 P1b 实现侧)
- bridge 跨 domain TRANSIENT_LOCAL 端到端(M5 侧订阅已匹配,bridge 侧待确认)
- capability 热改通知(当前不支持,优化项非阻塞)

**残余风险(显式记录)**:
- 1200s horizon 实时性靠 acados,P1b 迁移中(TBD-4 benchmark 是头号裁决回炉触发)
- L4 adapter(PID→航点 sampler + 30s 预测器)是新代码,须独立单测+与 GNC 可行性公式镜像验证
- 承诺前缀 180s 设计值未 SIL 验证(SIL 校准延后)

---

## 移交(供 Spec 同步)

本方案的核心技术决策已通过 design-grounding 裁决(Step2 逐点用户确认 + Step3 代码核实 + Step4/5 综合+对抗验证)。**用户本周进对应 Spec 修改同步决策内容**(M5 侧 6 改动点 + 接口侧 4 + L4 侧 3,见决策树日志 Step4 Spec 同步指引)。

SIL 校准(承诺前缀 180s)一周后回来,据此定稿方案包最终值。

**跨树同步(已完成)**:M5 MPC 设计树 log + solution pack 已追加 VR-06b(horizon 1200s)/VR-07b(废弃人工参考+Huber+废除C10/C11+淘汰TailBuilder),append-only 不抹原裁决。
