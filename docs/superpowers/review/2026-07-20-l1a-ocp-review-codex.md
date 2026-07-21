# L1a OCP 建模独立评审 · Codex

> **评审 agent**: Codex(用户在 Codex CLI 同步跑的平行评审,使用与 ZCode 评审相同的提示词)
> **评审范围**: L1a(DP-01 / DP-02 / DP-08)
> **状态**: DONE_WITH_CONCERNS
> **Preflight readiness**: NOT_READY
> **workspace writes**: none
> **完成时间**: 2026-07-20 17:00
> **原文**: 用户粘贴,Codex CLI 产出。本文件为原文落盘,用于两份评审分歧对照与 Step5 DESIGN-IT-TWICE 证据。

---

## 核心裁决

三项方向可组合,但当前草案仍缺完整 OCP contract。DP-01 `nh=36` 非最简必要方案;DP-02 "每 stage" 忽略 stage0/terminal 维度;DP-08 把物理可达时间等同 ample-time deadline。直接实施,仍可能出现静默安全空窗、假 infeasible、跨 backend off-by-one。

## BL-A 数学事实独立复核

| 结论 | 独立复核 |
|---|---|
| `idxsh` row 是否结构上 soft | **成立。** acados v0.4.4 exact commit 中,`model_set("idxsh")`修改 soft-row index;约束残差明确对这些 index 减 lower/upper slack。[R23: bgh model_set](https://github.com/acados/acados/blob/5c98c317416a9bb335a99d1bf7933a04712ea72b/acados/ocp_nlp/ocp_nlp_constraints_bgh.c#L526-L538)、[R24: bgh residual](https://github.com/acados/acados/blob/5c98c317416a9bb335a99d1bf7933a04712ea72b/acados/ocp_nlp/ocp_nlp_constraints_bgh.c#L1411-L1418)。 |
| 增大 `Zl/zl` 能否变 true hard | **不能。** `Z/z`只进入 slack cost、gradient;任意有限权重仍允许非零 slack。[R25: external cost](https://github.com/acados/acados/blob/5c98c317416a9bb335a99d1bf7933a04712ea72b/acados/ocp_nlp/ocp_nlp_cost_external.c#L762-L772)。 |
| runtime 更新 `idxsh` 是否有效 | **有效,但 `nsh` 数量固定。** 每次 NLP solve 都重新 initialize constraints;更新后的 index 会重新建立内部 reverse map。[R26: common initialize](https://github.com/acados/acados/blob/5c98c317416a9bb335a99d1bf7933a04712ea72b/acados/ocp_nlp/ocp_nlp_common.c#L2645-L2670)。 |
| 反例 | 未发现有限罚权反例。可实现 true hard 的标准路径:row 不进 `idxsh`;或增加相同 residual 的独立 hard duplicate。 |
| 适用边界 | "永远 soft"应读作:**该 stage、该次 solve 中,只要 row 属于 `idxsh`,它就可被 slack 放宽**。runtime 下一次可改变 index membership。 |
| 置信 | 🟢:上游 exact source、local generator、generated C 三方一致。 |

## 新增边界场景

| ID | 场景 |
|---|---|
| SC-07 | 所有网格节点 CPA ≥1852,但两个节点之间 swept CPA <1852 |
| SC-08 | 距离扫点:1851.9、1852、2000、2500、2500.1m |
| SC-09 | 仅 terminal `x_N` 出现 CPA/heading/speed/ROT 违反 |
| SC-10 | own speed 已在新 M4 speed box 外,受 decel limit 无法一步进入 |
| SC-11 | heading wrap:359°→[5°,35°]及1°→[325°,355°] |
| SC-12 | 低 ROT/rudder slew,进入 heading box 所需时间超过 horizon |
| SC-13 | `prefix_active_k > k_head`,冻结 prefix 仍在 heading box 外 |

## DP-01:CPA hard slot

### 裁决

| 要求 | 独立评审 |
|---|---|
| 1. 语义正确性 | **部分正确。** `cpa_hard_m` 安全底线、`cpa_safe_m` 软目标分离正确;证据见 [R1] `types.hpp:124-132`、[R3] `formulation.cpp:333-340`。hard row 必须完全排除 `idxsh`。但草案仅按 `prefix_active_k` harden,不等价现有 IPOPT `cpa_hard_from_k` reachability schedule;可能在可控 suffix 尚未物理脱离 1852 前制造 HARD-infeasible。另,当前 generated solver `NH0=0/NHN=0`,hard CPA 实际只覆盖 stage `1..N-1`,不能称全 horizon hard floor。 |
| 2. 与草拟 VR | **部分同意。** 同意 hard/soft 独立、hard row 永不进 `idxsh`。不同意直接冻结 `nh=36`。现有 `J_colreg` 已用 `cpa_safe` 提供 soft aspiration:`mid_mpc_acados_formulation.cpp:361-375`。双 row 会重复表达 2500 目标、增加同梯度 row、继续使用量纲很大的 `m²` slack。 |
| 推荐替代 | **首选 `nh=20,nsh=0`**:`[0,1] prefix eq`;`[2..17] CPA hard(cpa_hard_m, no idxsh)`;`[18] direction`;`[19] min_alt`;2500 仅留在 `J_colreg`。若业务明确需要"soft constraint violation telemetry/lexicographic buffer",才保留 `nh=36,nsh=16`,并先裁决重复 cost、缩放、权重。 |
| hard activation | 不应简单写成 `k>=prefix_active_k`。需要物理时间 contract:首次 hard stage 至少考虑 `max(prefix exit, CPA reachability deadline)`。本评审不裁决 Q4 witness,但 DP-01 必须声明所消费的 schedule。 |
| COLREGs ample-time | hard distance 保证和"及时行动"不同。Rule 8 要求 positive、ample-time、最终 safe distance;Rule 16 要求 early/substantial。仅证明未来某节点 1852 hard,并不能证明 ample-time。[IMO COLREGs](https://www.imo.org/en/about/conventions/pages/colreg.aspx)、[MAIB 5/2026 §1.14](https://assets.publishing.service.gov.uk/media/69a59f41bc86a487b52c7180/2026-5-Polesie-Verity-ReportAndAnnexes.pdf)。🟡:两份 A 级来源;具体 deadline 仍依场景。 |
| 7. 跨路径一致性 | **不对等,属于 bug/未决设计。** IPOPT 使用 `cpa_hard_m`,但每个 CPA expression 仍加全局 `σ`:`constraint_compiler.cpp:242-266,287-292`,所以不是 true hard。IPOPT 另有 `cpa_hard_from_k`:`mid_mpc_solver.cpp:556-620`。acados 若采用 true hard,将比当前 IPOPT 严格;应两路径共同修,不应把 IPOPT 现状当 oracle。 |

### 失效边界与"最简版"追问

| 场景 | 失效方式 |
|---|---|
| SC-01/SC-02 | 只改 `nh`,漏改 MX/SX graph、generated header/.so、row offsets、h_fn cache 或 wrapper constants:错 row 被 harden,甚至维度失配。 |
| SC-08 | `1852<d<2500` 应 hard-feasible、soft-cost active。若 soft row penalty 过大,2500 实际变"近似硬",重现 reference-feasible→solver failure。 |
| SC-07 | 节点 residual 均≥0,但 15s 区间内穿越 1852。node-only row 静默宣称安全;根因 oracle 已使用连续线段 CPA:root-cause report `:32-40`。 |
| SC-09 | 当前 generated header 明确 `NHN=0`;terminal-only penetration 不检查:`c_generated_code/acados_solver_m5_mid_mpc_acados.h:44-70`。 |
| 0/1/16 targets | empty target slot 需同时 relax soft/hard rows;row offset、`idxsh`长度、`n_targets` 变化可能污染 direction/min-alt。 |
| `cpa_safe<cpa_hard` | hard/soft 层级反转。必须 fail-closed 验证 `0<cpa_hard<=cpa_safe`。 |
| 机制C:只加 hard row,不改 `idxsh` | 静默退化 ALT-02。 |
| 机制C:只提高 `Zl` | 仍可 slack;只是更贵。 |
| 机制C:仅 path stage hard | terminal 与节点间安全空窗继续存在。 |

### 盲区与验证需求

| 优先级 | 盲区 / 必须测试 |
|---|---|
| 高 | `nh20/nsh0 + J_colreg` vs `nh36/nsh16 + soft row + J_colreg` 业务需求裁决。当前日志 `VR-01 nh20` 与架构总表 `DP-01 nh36` 互相矛盾。 |
| 高 | SC-07 swept-segment CPA:需连续区间约束、保守 tightening 或证明 15s 网格不会漏穿。 |
| 高 | terminal `x_N` 是否纳入 CPA hard;若纳入,需 `con_h_expr_e/NHN`。 |
| 高 | physical-time mapping:IPOPT 在 `constraint_compiler.cpp:274-289` 先积分 own position,却以 target `k·dt` 取位置,疑似一阶段错位。先闭环再做 parity。 |
| 中 | dual rows 具有相同 state gradient,仅半径常数不同;对首 QP 退化/conditioning 影响。 |
| 必测 | generated contract:MX/SX row 同构;header/JSON/.so `nh/nsh` 一致;hard row 与 `idxsh` 集合不相交。 |
| 必测 | adversarial slack:令 `Z/z=0`,hard-active 节点 `d<1852` 仍不得接受;soft row 可违反。 |
| 必测 | 参数隔离:只变 `cpa_safe`,hard residual 不变;只变 `cpa_hard`,soft cost 不变。 |
| 必测 | SC-01/02/03、SC-07/08/09、0/1/16 targets;输出 node residual、continuous CPA、slack、active-set trace。 |

## DP-02:runtime heading/speed/ROT boxes 每 stage

### 裁决

| 要求 | 独立评审 |
|---|---|
| 1. 语义正确性 | **方向正确,stage contract 不完整。** live envelope 必须写入全部可控 predicted states。当前 wrapper `:975-1009` 只写 `lh/uh` 和 stage0 x0 equality;stage1..N-1 仍用 codegen 静态 box。 |
| 2. 与草拟 VR | **部分同意。** 同意每 solve 重发 runtime bounds。不同意把 stage0/path/terminal 视为相同维度;不同意"ROT 来自 M4 BehaviorPlan"。heading/speed 来自 M4:`mid_mpc_node.cpp:598-680`;ROT 来自 effective GNC ODD:`:902-916`。 |
| 正确 stage 语义 | stage0:保持 measured x0 equality,不拿行为 box 重定义当前事实。stage1..N-1:`r/u` runtime hard;`psi` 按 DP-08 schedule。terminal N:必须明确是否约束;当前 `NBXN=0`。 |
| per-stage 差异来源 | 当前上游只有每-cycle scalar boxes,没有 per-stage arrays。stage 变化应由 M5 bound policy 派生,不应表述成"M4 直接给每 stage box"。 |
| 7. 跨路径一致性 | **部分对等。** IPOPT 每 solve 把 live heading/speed 写满 `psi/u[0..N-1]`:`mid_mpc_solver.cpp:240-258`;ROT 通过 own→ψ0 和 inter-step rows:`mid_mpc_nlp_formulation.cpp:444-465`。acados 直接约束 yaw-rate state `r`。业务 envelope 可对等,数学状态/网格不相同。 |

### 失效边界与"最简版"追问

| 场景 | 失效方式 |
|---|---|
| SC-03 | x0≈0°、box=23.2°..53.2°。若 stage0 行为 box 覆盖 x0 equality,假 infeasible;若仍只写 stage0,则 future box 完全未落地。 |
| SC-09 | `x_{N-1}` 合规,最后 dynamics step 使 `x_N` 的 ψ/r/u 越界。当前 generated `NBXN=0`,runtime setter 对 terminal 无有效 payload。 |
| SC-10 | own speed 高于新 `speed_max`,decel 受限;stage1 立即 hard speed box 可能物理不可达。heading 有 schedule,speed 无 schedule,缺陷只是转移。 |
| 非零真实 yaw rate | acados 把 `r0=0` 固定:`mid_mpc_acados_solver.cpp:992-997`。首步 heading、ROT reach time、box feasibility 均可能错误。 |
| box 跨 cycle 骤变 | 只更新部分 stage 会残留 codegen default 或上周期 bound;solver success 无法证明 live contract 生效。 |
| 机制C:只 set stage0 | 正是现状;stage1..N-1 仍静态。 |
| 机制C:循环 `k=0..N` 传相同 3 值 | stage0 `NBX0=5`、terminal `NBXN=0`;可能误覆盖或静默 no-op。 |
| 机制C:只 set `1..N-1` | terminal 漏约束。 |
| 机制C:heading schedule、speed 立即 hard | SC-10 继续假 infeasible。 |

### 盲区与验证需求

| 优先级 | 盲区 / 必须测试 |
|---|---|
| 高 | "all stage" 正式定义:stage0、path `1..N-1`、terminal N 分别何种 bounds。 |
| 高 | terminal `idxbx_e/lbx_e/ubx_e` 是否需要。若不需要,必须明确 terminal 不属于安全 claim。 |
| 高 | speed box reachability/failure policy;不能只解决 heading。 |
| 高 | `r0=0` 是假设还是合法 contract;若无测量 yaw rate,需估计或不确定性边界。 |
| 中 | heading unwrapped range 可能跨±π;runtime box 与 cost/dynamics 必须使用同一 branch。 |
| 必测 | 给每 stage 不同 fingerprint,set 后立即 readback;连续两 cycle 换 box,证明无 stale。 |
| 必测 | stage0 x0 在行为 box 外仍保持 equality;不因当前事实造 infeasible。 |
| 必测 | SC-09 terminal ψ/r/u 各单独越界必须按正式 contract 处理。 |
| 必测 | SC-10 speed 高于 max、低于 min、边界相等;验证 reach schedule 或显式 infeasible。 |
| 必测 | SC-11 wrap、`r=±rot_max`、非零 r0、M4 fallback 全圆窗口。 |

## DP-08:heading box 时间语义

### 裁决

| 要求 | 独立评审 |
|---|---|
| 1. 语义正确性 | **概念正确,算法未冻结。** 当前状态在 box 外时,早期 heading bound 应"disabled/relaxed",物理可达后才 hard。这里不是 slack softening。 |
| 2. 与草拟 VR | **部分同意。** 同意 reachable suffix;不同意"与 min-alt 同源"已足够实施。min-alt 目标是相对 own heading 达到指定改向;heading-entry 目标是沿允许方向进入 nearest box boundary。可共享 vessel reachability model,不能直接共享角度公式。设计日志 BL-03 仍未闭环。 |
| ample-time 关键差异 | physical reachability 给出"最早/保守可达时刻";COLREGs ample-time 还需要"最晚安全时刻"。正确 contract 需两个量:`t_reach_upper` 与 `t_latest_safe`。必须满足 `t_reach_upper <= t_hard <= t_latest_safe`。否则应显式 infeasible/degraded。仅用 reachability 不能证明 early/substantial action。 |
| `k_head` 定义 | 应先定义物理秒数,再映射到 backend grid;输入至少含 ψ、r、必要时 rudder state/slew、speed、方向、wrapped box boundary、模型误差裕度。 |
| 不可达 policy | `k_head>N` 不能等同全 horizon relax success。必须输出明确不可达分类;否则行为 envelope 静默消失。 |
| 7. 跨路径一致性 | **当前不对等,属于两路径共同 bug。** IPOPT `psi[0]` 是首个可控 predicted heading;acados stage0 是 measured x0,stage1 才是首个 future state。复制同一整数 `k_head` 会系统性 off-by-one。 |

### 失效边界与"最简版"追问

| 场景 | 失效方式 |
|---|---|
| SC-03 | explicit Euler、`r0=0` 时 `psi1=psi0`;只 relax k=0 仍使 stage1 不可控。root-cause report `:67-70,134-142`。 |
| SC-11 | raw 359°→5° 差值变 −354°,算出错误方向/巨大 `k_head`。必须用同一 unwrapped branch。 |
| SC-12 | reach time>horizon。`k_head=N` 且 `NBXN=0` 等于全 horizon 无 heading box,静默退化。 |
| SC-13 | prefix 冻结到 `K>k_head`,却在 prefix 内部 harden,NLP 无法修改,必然 infeasible。 |
| box 骤缩/方向改变 | 旧 warm prefix 在旧 box 内、新 box 外;若 schedule 不重算,错误 harden。 |
| `t_reach<t_latest_safe` 但联合不可行 | 单独 heading 可达,不代表同时满足 CPA hard、speed、ROT。组合 OCP 仍可能无解。 |
| 机制C:只 soften k=0 | SC-03 stage1 继续失败。 |
| 机制C:固定 relax 1/2 stages | 不同速度、初始 r、rudder slew、box 角距下错误。 |
| 机制C:复用 `ceil(min_alt/(rot_max·dt))-1` | 已被 MMG witness 证伪:名义 ROT 与真实约差 5 倍;设计日志 `:177-185`。 |
| 机制C:backend 复制同一 k | IPOPT/acados 首次 hard 物理时刻相差一 stage。 |

### 盲区与验证需求

| 优先级 | 盲区 / 必须测试 |
|---|---|
| 高 | `k_head` 正式公式及独立 witness:ψ/r/δ/u、rudder slew、direction、nearest boundary、wrap、误差裕度。 |
| 高 | `t_latest_safe` 来源:CPA/TCPA/risk、ODD、traffic/visibility。MAIB 资料表明具体时间高度依场景;不能把 Rule 8 文字直接编码成单一固定 stage。 |
| 高 | backend physical-time map;禁止裸 k parity。 |
| 高 | `k_head>N` 显式 outcome。 |
| 高 | prefix 组合;虽然 Q4 裁决不在本次范围,但 L1a 不能宣称完全独立。 |
| 中 | heading box 究竟是 hard allowed envelope 还是行为 preference。若只是偏好,suffix hard 本身错误。 |
| 必测 | SC-03:`k_head-1` 无 hard bound,`k_head` 起全部 hard;MMG/独立 dynamics 证明首次 hard 可达。 |
| 必测 | `k_head={0,1,2,N-1,N,N+1}`。 |
| 必测 | SC-11 Starboard/Port 双向 wrap。 |
| 必测 | SC-12 horizon 不可达必须显式 reject/degrade,不得成功。 |
| 必测 | SC-13 `prefix_K={0,<k_head,=k_head,>k_head}`。 |
| 必测 | 初始 `r={负,0,正}`、多速度、ROT、rudder slew;IPOPT/acatos 按秒对齐首次 hard time。 |

## 总体评审结论

| 项目 | 结论 |
|---|---|
| L1a 整体语义 | **意图自洽,规格未自洽。** hard CPA、live boxes、reachable heading 三方向正确;当前不足以进入无条件实施。 |
| 最大内部冲突 | DP-02 若"全 stage 同一 heading box",直接违反 DP-08 early relaxed;必须由单一 `StageBoundPolicy` 生成每 stage bounds。 |
| CPA × heading 耦合 | heading 尚不可达时若 CPA 立即 hard,重造原始 infeasible;若 CPA 过度延后,又形成安全空窗。需要联合 witness,不是三个独立单测相加。 |
| terminal 共同缺口 | 当前 generated `NHN=0/NBXN=0`。DP-01、DP-02、DP-08 在最后 dynamics step 同时失去约束。 |
| grid 共同缺口 | IPOPT k=0 与 acados stage0 不是同一物理时刻。所有 schedule 必须先按秒定义,再分别映射。 |
| 推荐 DP-01 | 首选 `nh=20,nsh=0 + J_colreg(cpa_safe)`;`nh=36` 仅在明确需要独立 soft-row 语义后采用。 |
| readiness | **NOT_READY**。最小关闭项:①soft-row 业务裁决;②terminal contract;③continuous CPA;④`k_head` 与 latest-safe deadline;⑤backend physical-time map;⑥speed reachability。 |
| NLM 状态 | `maritime_regulations` 与 `colav_algorithms` 只读查询均未返回答案 payload;未把 NLM retrieval 当证据。相关结论改用 IMO/MAIB 当前一手来源,并保留 Step3 调研项。 |
| 评审状态 | **DONE_WITH_CONCERNS**。完成只读评审;未运行求解/构建;未产生 artifact;未修改文件。 |
