# COLREGs-MPC Dispatch Gate Threshold Literature Brief
**Date:** 2026-07-19  
**Scope:** 独立调研，为 M5 acados dispatch gate v3 设计提供文献证据  
**Domain limit:** maritime COLREGs only（排除 AV / UAV / 通用 MPC）

## 证据口径与结论边界

- `[PROJECT_FACT]`：当前 worktree 的两份权威 memo、架构文档或现有测试数据。只证明当前项目实现/样本，不外推为海事领域常数。
- `[DOMAIN_EVIDENCE]`：海事 COLREGs、船舶避碰、ship MPC 同行评审原始文献。由文献数值复算的项标 `复算`。
- `[NLM_SOURCE]`：本轮无可用结果。`nlm-research --depth deep/fast` 两次均因 `ConnectError: All connection attempts failed` 中止；未导入来源。以下文献证据均直接核对 publisher、DOI 或作者公开 PDF。
- 置信度按项目规则标注。历史原始论文即使数值提取明确，也因来源年龄和平台特异性最多记为 🟡 Medium；单源、引用对象不清或“未找到先例”记为 🔴 Low / ⚫ Unknown。
- 核心结论：文献支持 encounter-aware、horizon-aware、maneuverability-aware 的混合分层；不支持把单个瞬时几何量或 solver telemetry 当跨船型、跨 ODD 的 universal dispatch threshold。

## §1 Ample-time TCPA / range 阈值表（Q1）

| 文献 | 证据类 | ODD / 平台 | TCPA 下限 | TCPA 上限 | CPA / range 阈值 | 角色 / 阶段 | 出处 | 置信度 |
|---|---|---|---:|---:|---|---|---|---|
| Wang et al. 2021 | DOMAIN_EVIDENCE | open sea；stand-on；crossing | `>0` | `20 min` | 无 DCPA cutoff；当前 range `D ≤ 6 NM` | potential-risk monitoring / stand-on 四阶段入口 | §4, Table 1 | 🟡 |
| Wang et al. 2021 | DOMAIN_EVIDENCE | open sea；stand-on；overtaking | `>0` | `30 min` | 无 DCPA cutoff；当前 range `D ≤ 3 NM` | potential-risk monitoring / stand-on 四阶段入口 | §4, Table 1 | 🟡 |
| Wang et al. 2021 crossing simulation | DOMAIN_EVIDENCE（复算） | open sea；crossing | 未定义 | 风险监测门 `1200 s` | 场景初始 TCPA `1852 s`；文中未给通用 CPA 门 | FTCR 时剩余 TCPA `1200 s`；FTCS `444 s`；FTID `208 s` | §5.1, Figs. 8–10；剩余 TCPA = 初始 TCPA − elapsed time | 🟡 |
| Wang et al. 2021 overtaking simulation | DOMAIN_EVIDENCE（复算） | open sea；overtaking | 未定义 | 风险监测门 `1800 s` | 初始 DCPA `122 m`，不是通用阈值 | 初始 TCPA `2000 s`；FTCS 时剩余约 `602 s`；FTID 时约 `229 s` | §5.2, Figs. 12–14 | 🟡 |
| Eriksen et al. 2020 | DOMAIN_EVIDENCE | Telemetron ASV；open-water/full-scale hybrid COLAV | `0 s` | `270 s` entry | dCPA entry `<900 m` | SO / OT / GW / HO encounter-state entry | §4.2.2；Table 2, PDF p.12 | 🟡 |
| Eriksen et al. 2020 | DOMAIN_EVIDENCE | 同上 | `−20 s` | `290 s` exit | dCPA exit threshold `2000 m` | encounter-state hysteresis / monitoring release | §4.2.2；Table 2, PDF p.12 | 🟡 |
| Eriksen et al. 2020 | DOMAIN_EVIDENCE | 同上 | `tCPA > 0` | 未给 | `t_crit` entry `20 s`、exit `25 s`；moving ellipse `600 × 225 m` | emergency / short-term BC-MPC authority | §4.2.2, §5；Table 2, PDF p.12 | 🟡 |
| Thyri & Breivik 2022 | DOMAIN_EVIDENCE | deliberate NMPC；open water + restricted urban water | event-based，非 raw TCPA | `T_critical=140 s` | `d_critical=50 m` | GW / OT / HO target priority：critical entry 临近时纳入 | §4.7；Table 2, printed p.69 | 🟡 |
| Thyri & Breivik 2022 | DOMAIN_EVIDENCE | 同上；stand-on | event-based，非 raw TCPA | `20 s` | `d_critical=50 m` | stand-on target critical priority | §4.7；Table 2, printed p.69 | 🟡 |
| Thyri & Breivik 2022 | DOMAIN_EVIDENCE | 同上 | `T_ample_time=120 s` before earliest critical entry | 未给 | free-space cap `40 m`；static margin `8 m`；非 CPA 门 | ACRW maneuver start；`T_maneuver=40 s`；after-pass padding `40 s` | §4.7–4.8；Table 2, printed p.69 | 🟡 |
| Imazu & Koyama 1984 / Imazu benchmark line | DOMAIN_EVIDENCE | open-sea benchmark geometry；1–4 targets | 未给 universal threshold | 未给 | 未给 universal CPA/range threshold | encounter geometry / risk/action benchmark，不是 MPC layer gate | J. Japan Inst. Navigation 70/71；后续 Imazu-22/42 使用 | 🔴 |
| Sawada et al. 2021 的 Imazu 实例化 | DOMAIN_EVIDENCE | open-sea training benchmark | `>0` | initial `30 min` | safe-passage training target `0.3 NM` | scenario initialization，不是 ample-time safety standard | §4；Imazu appendix | 🟡 |
| Imazu 2014 OZT | DOMAIN_EVIDENCE | open sea collision-course geometry | 未给 | 未给 | OZT 预测受目标船阻碍的航向区；非固定 CPA 门 | geometric conflict representation | Navigation 188, pp.78–81 | 🟡 |
| Veitch et al. 2024 | DOMAIN_EVIDENCE | ROC supervisory control；非 MPC | `20 s` available critical time | `60 s` available critical time | 无 CPA/range | human takeover experiment；不是 planner dispatch | experiment design；Fig. 14(d), PDF pp.12–13 | 🟡 |
| MASS-L3 architecture ODD-A | PROJECT_FACT | open water | `12 min` design floor | 未定义 | CPA `1.0 NM` | project ODD monitoring boundary | architecture §3.3 | — |
| MASS-L3 architecture ODD-B | PROJECT_FACT | restricted / VTS | `4 min` design floor | 未定义 | CPA `0.3 NM` | project ODD monitoring boundary | architecture §3.3 | — |
| MASS-L3 architecture ODD-D | PROJECT_FACT | restricted visibility | base threshold `×1.5` | 未定义 | base threshold `×1.5` | project degraded-visibility margin | architecture §3.3 | — |
| MASS-L3 `T_standOn / T_act` | PROJECT_FACT | ODD-A / B / D | `T_act = 4 / 3 / 5 min` | `T_standOn = 8 / 6 / 10 min` | — | stand-on / act | architecture §9.3 | — |

### Q1 判读

1. `[DOMAIN_EVIDENCE]` Wang 给出的是 encounter-specific、captain-configured potential-risk window：crossing 20 min、overtaking 30 min。它不支持“海事文献 ample-time 上限统一为 20 min”。🟡
2. `[PROJECT_FACT]` 架构 §9.3 的 `8/4 min`（ODD-A）、`6/3 min`（ODD-B）、`10/5 min`（ODD-D）是项目工程值。Wang 原文没有 `T_standOn` / `T_act` 这组符号或三组数值；当前 `[R17]` 书目还把 DOI `10.3390/jmse9060584` 错配给另一篇 dynamic-window 题名/作者。该引用必须勘误，不能作为阈值来源链。
3. `[DOMAIN_EVIDENCE]` Eriksen 的直接门是 `dCPA + tCPA` 状态机：`900 m` 与 `0–270 s` entry；不是 `T_horizon` 单变量门，也不是 5–20 min 区间。🟡
4. `[DOMAIN_EVIDENCE]` Thyri 的 120 s 是“最早 critical-entry 之前的 maneuver lead time”；140 s 是 target-priority critical time；600 s 是 control horizon。三者语义不能互换。🟡
5. `[DOMAIN_EVIDENCE]` Veitch 的 20/60 s 是人工接管实验自变量。30 min monitoring 条件下，60 s 组仍出现 50% collision；因此论文既不证明 universal `TMR ≥ 60 s`，也不证明 60 s 足够。🟡
6. `[DOMAIN_EVIDENCE]` Imazu 经典工作支撑 encounter 几何覆盖与 benchmark，不提供可迁移的 TCPA/CPA gate 数字。请求中的“1987 / 2014”不是同一类阈值来源：1987 指 benchmark line，2014 OZT 指几何风险区。🔴
7. `[NLM_SOURCE]` / `[DOMAIN_EVIDENCE]` 均未定位到名为 “AoU” 的可访问同行评审来源。若 AoU 指项目 assurance/operational-use calibration，它只能作为 `[PROJECT_FACT]`，不能与 Veitch 合并写成独立文献共识。⚫

## §2 Hybrid COLAV 边界标定方法（Q2）

### 2.1 Eriksen 系：周期运行 + 风险状态/权限切换

`[DOMAIN_EVIDENCE]` Eriksen et al. 2020 的作者是 Eriksen、Bitar、Breivik、Lekkas；不含 Johansen。请求所指“§3.3 mid-MPC activated when”与论文结构不符：§3 是 high-level planner；mid-level encounter activation 的直接位置是 §4.2.2 和 Table 2。

- Mid-level NLP 每 `60 s` 周期运行；预测 `N_p=36`、`h=10 s`，即 `T_h=360 s`。
- BC-MPC 每 `5 s` 周期运行；不是 mid 求解失败后才启动的冷备进程。正常时跟踪 mid trajectory；emergency、迟检或他船不履责时获得近场避碰权限。
- SO / OT / GW / HO 风险状态由 `dCPA < 900 m` 且 `0 ≤ tCPA ≤ 270 s` 进入；退出阈值放宽到 `dCPA=2000 m` 与 `−20 ≤ tCPA ≤ 290 s`，形成 hysteresis。
- Emergency 用 time-to-critical `20/25 s`，不是 solver iteration/status。

失败处理原文（§6.1，20 words）：

> “If the mid-level algorithm fails in finding a feasible solution, the algorithm will re-use the solution from the last iteration.”

`[DOMAIN_EVIDENCE]` 该句只定义单次失败后的 mid-plan reuse。完整 architecture safety story 还依赖 5 s BC-MPC。旧轨迹在新目标、状态漂移、ODD 变化后可能失效；不能把“复用旧解”单独写成充分 fail-safe。🟡

### 2.2 Thyri：horizon / critical-entry / critical-exit 事件门

请求所指“Thyri 2022 §3.2 mid-MPC activated when”也不符：§3.2 是 target-ship domain；目标何时进入 NMPC priority list 的直接位置是 §4.7。

对 GW / OT / HO，优先纳入条件为：

\[
t_{exit,crit} < T_{horizon} - T_{after\_pass\_padding}
\quad\lor\quad
t_{enter,crit} < T_{critical}.
\]

对 stand-on：

\[
t_{enter,crit} < T_{crit,stand-on}.
\]

数值：`T_horizon=150×4=600 s`、`T_after_pass_padding=40 s`、`T_critical=140 s`、`T_crit,stand-on=20 s`。ACRW maneuver start 另用 `min(t_enter,crit)-T_ample_time`，其中 `T_ample_time=120 s`。

作者对 120 s 固定值的限制原文（§4.8，14 words）：

> “a more qualified estimate of these parameters should be made for each vessel-to-vessel encounter”

`[DOMAIN_EVIDENCE]` 即：合理边界应随船型、速度、maneuvering capacity、encounter geometry 变化；论文没有把 120 s 宣称为 universal threshold。🟡

### 2.3 Horizon、replan、接管对照

| 方法 | Mid / reactive horizon | Replan | Boundary calibration | Mid failure response |
|---|---:|---:|---|---|
| Eriksen & Breivik 2017 mid-level MPC | `24×10=240 s` | 未作为 layer gate 给出 | moving-obstacle geometry + COLREG cost；steady `U-r` feasible set | 未给独立状态机 |
| Eriksen et al. 2020 hybrid | Mid `36×10=360 s` | Mid `60 s`；BC `5 s` | `dCPA + tCPA` encounter state；`t_crit` emergency | reuse previous mid solution + continuously running BC-MPC |
| Eriksen et al. 2019 BC-MPC | published parameter vector `T=[5,20,30] s` | `5 s` | finite dynamically feasible candidate set + emergency risk | 总能选 candidate，但可能次优；不是硬安全证明 |
| Thyri & Breivik 2022 | `150×4=600 s` | 未报告固定周期 | critical entry/exit vs horizon + 40 s past-pass padding | shifted warm start；无独立 reactive fail-safe 实现 |
| MASS-L3 P5 | `80×15=1200 s` | `60 s` | 当前待标定 G1/G2 + backend health | `[PROJECT_FACT]` current-cycle acados failure 不重试 IPOPT；后续 BC/MRM 链处理 |

`[DOMAIN_EVIDENCE]` 结论：Eriksen/Thyri 没有用“固定 TCPA 或固定 CPA 或 solver status 三选一”标定唯一边界。使用组合条件：encounter risk、预测 critical-entry/exit、horizon coverage、动态可行候选、独立 reactive layer。🟡

`[DOMAIN_EVIDENCE]` 定向检索未匹配到 Bøhn 或 Hexel 的 maritime COLREGs-MPC solver-dispatch 论文。Bøhn 的可检索 horizon/update-interval 工作属于通用控制 benchmark，应按任务域限制排除；“Hexel”可能是 Hexeberg（AIS trajectory prediction），不能并入 solver 阈值证据。🔴

## §3 SQP 在 COLREGs barrier 上的失败处理（Q3）

### 3.1 IPOPT、SQP 与 failure handling

| 文献 | Solver / formulation | 非凸/不收敛处理 | 能否支撑当前 gate |
|---|---|---|---|
| Eriksen & Breivik 2017 | CasADi + IPOPT；nonconvex kinematic NLP | warm start；steady SOG/ROT feasible set；未报告 nonconvergence state machine | 只支撑 maritime IPOPT precedent；不支撑为何“必须不用 SQP” |
| Eriksen et al. 2020 | CasADi + IPOPT；moving ellipses + COLREG costs + slack | five-stage `K_ξ` homotopy；失败复用旧 mid plan；5 s BC-MPC backup | 支撑 continuation + hybrid fallback；不支撑 acados/SQP 阈值 |
| Thyri & Breivik 2022 | 论文未命名具体 NLP solver | shifted previous solution 与 reference/initial mix，缓解 local minima 和迭代间振荡 | 不得写成 IPOPT 或 SQP 证据 |
| Johansen et al. 2016 | finite behavior enumeration + predictive ship simulation | 刻意避免 gradient numerical optimization | 支撑“动态可行候选层可规避 NLP convergence risk” |
| Eriksen & Breivik 2019 | branching-course sample MPC | finite dynamically feasible candidates，无 gradient NLP | hybrid reactive backup 先例；无 SQP convergence data |
| Sun et al. 2021 | maritime multi-ship MPC + SQP | 报告平均 CPU 约 7 s；无 iteration/residual/failure-rate/DCPA sweep | 证明“有人在 ship MPC 用 SQP”；不证明 barrier failure 已解决 |
| Gonzalez-Garcia et al. 2022 | acados NMPC | LiDAR/static buoy avoidance；非 ship-to-ship COLREGs | 邻域反例；按 domain limit 不能迁移 |

`[DOMAIN_EVIDENCE]` Eriksen 文献只报告“使用 IPOPT”，没有 IPOPT-vs-SQP 对照，也没有写 SQP 在 COLREGs 上的专门限制。不能把作者的 solver choice 解释成已证明 IPOPT 更适合。🟡

`[DOMAIN_EVIDENCE]` 指定作者群和定向 primary-source 检索没有找到 acados SQP + ship-to-ship COLREGs 论文。存在 acados maritime obstacle NMPC，但对象为静态浮标、无 COLREGs encounter；不得用来校准本 gate。⚫

### 3.2 `K_ξ` homotopy 不是 exact-penalty ratio

Eriksen 2020 顺序求解五个 NLP：

\[
K_\xi = [0.1,\ 1,\ 10,\ 100,\ \infty].
\]

前一阶段解 warm-start 下一阶段。小 penalty 允许轨迹穿过 moving-obstacle potential 的局部拓扑障碍；逐级增大后强制 `ξ=0`。论文没有给 multiplier `λ̂`、没有 `K_ξ/λ̂` 门，也没有说明 0.1 的统计标定方法。它是 continuation schedule，不是 current acados 单次 SQP exact-penalty calibration。`[DOMAIN_EVIDENCE]` 🟡

Kerrigan/Maciejowski 的 `ρ > ||λ*||` 是通用 MPC exact-penalty 理论背景，非 maritime、非 COLREGs、非非凸 ship OCP、非 acados dispatch proof。`[DOMAIN_EVIDENCE]` 在本 brief 中只能作为“无领域验证的外部理论”，不能给 G3 贴 maritime precedent。🔴

`[PROJECT_FACT]` P5 的 `ρ=1000`、估计 `λ≈3700`、把 `ρ` 提到 `10^6` 仍未激活 `ξ`，以及 `ξ≈10^-19`，只说明当前 formulation/seed/globalization 下的实测现象；它不等同 Eriksen homotopy，也不能由 Kerrigan 条件单独解释。

## §4 Gate 候选判据的文献支持（Q4）

| 候选 | 海事 COLREGs precedent | 反例 / 限制 | 判定 |
|---|---|---|---|
| **G1 horizon-projected CPA gap** `max(0,cpa_safe−min_k CPA_pred(k))` | Johansen 2016 用 finite-horizon vessel simulation 做 predictive hazard；Thyri §4.7 用 predicted critical-entry/exit 与 horizon/past-pass padding；Zhang et al. 2025 按 horizon 内 predicted ship-domain zone intrusion 选择风险函数 | 未找到同名 scalar gap 直接切 solver layer；Eriksen §4.2 假定 horizon 内 encounter classification 不变；linear CPA、trajectory CPA、domain intrusion 不是同一量 | **概念支持，数值无先例**；🟡 |
| **G2 required deflection / ROT reach** `Δreq/(½u·rot_max·T_h²)` | Eriksen 2017 用 coupled steady `U-r` feasible set；Johansen 2016 用含 yaw/rudder/propulsion dynamics 的 3-DOF simulator；Thyri 用逐节点 velocity/acceleration constraints；均要求 maneuverability-aware prediction | 未找到该 closed-form ratio；steady ROT envelope 不覆盖 transient L4 reachability、rate limits、current、actuator lag；Tsolakis 2022 显示强制角加速度下界会产生高度非凸甚至不连通 search space | **原则支持，ratio/阈值无先例**；🟡 |
| **G3 exact-penalty ratio** `ρ/λ̂` | 无 maritime COLREGs precedent | Kerrigan 是通用 MPC；Eriksen `K_ξ` 是 homotopy，不是 multiplier-calibrated exact penalty | **无领域支持**；🔴 |
| **G4 TCPA vs horizon** `tcpa > T_h` | Thyri 直接比较 `t_enter/t_exit` 与 `T_horizon`，并保留 `40 s` after-pass padding；Eriksen 的 `tCPA entry≤270 s` 小于 `360 s` horizon | Eriksen 不是 raw `tcpa>T_h` dispatch rule；同时要求 dCPA；Thyri 对低 closing-speed target 允许由 critical-entry 分支纳入，避免只看 exit/horizon 漏检 | **event-based horizon gate 有直接支持；raw TCPA-only 过粗**；🟡 |
| **G5 post-solve trend** SQP iterations + slack inertia | Eriksen 2017、Thyri 2022 报 runtime/warm-start spikes，证明 telemetry 有诊断价值 | 未找到用 iteration slope、KKT trend、slack inertia 作 safety dispatch；Eriksen 失败后按 feasible/no-feasible 切换，不按趋势阈值 | **可作 PROJECT telemetry；无领域 gate precedent**；🔴 |

### Q4 直接结论

- `[DOMAIN_EVIDENCE]` G1 最接近文献，但应从“单个 minimum CPA gap”升级为 `critical-entry + minimum domain/CPA + critical-exit + after-pass padding` 事件链。🟡
- `[DOMAIN_EVIDENCE]` G2 的正确先例是完整动态可达性，不是瞬时 ROT bound。简式 ratio 可做 cheap rejector，不能作 L4 executability proof。🟡
- `[DOMAIN_EVIDENCE]` G3、G5 没有 maritime COLREGs threshold precedent。🔴
- `[DOMAIN_EVIDENCE]` “G4 是 Eriksen 直接用的”需修正：Eriksen 用 `dCPA+tCPA` state entry，且 `270 s < 360 s horizon`；真正写出 horizon-relative target inclusion 的是 Thyri §4.7。🟡

## §5 ho 场景几何特殊性（Q5）

### 5.1 为什么 pure head-on 会暴露 gradient NLP 弱点

`[DOMAIN_EVIDENCE + 数学推断]` Rule 14 给出确定的 starboard 合规分支；但在未施加强方向约束的连续避障 OCP 中，dead-ahead / DCPA=0 产生左右镜像绕行拓扑。直线 seed 穿过 circular/elliptic keep-out set；在零横向偏移处，一阶横向距离梯度为零，SQP 一阶 QP 难发现二阶侧向逃逸方向。该几何会放大 nonconvex barrier、initial seed、linearization、merit globalization 缺陷。🟡

`[DOMAIN_EVIDENCE]` 但“DCPA=0 是所有 MPC 最难场景”不成立：

- Tsolakis et al. 2022 直接把 Rule 8 的角加速度下界描述为 highly non-convex / disconnected search space，并以 affine tangent constraints 形成 convex polytope；未给 head-on solver convergence sweep。
- Eriksen 2020 的 slack homotopy用于避免 moving obstacles 的 local minima；高/中层非凸算法可能不返回解。该文用 IPOPT，不是 SQP；无 DCPA=0 iteration/KKT 对照。
- Eriksen & Breivik 2019 的 BC-MPC 以 finite dynamically feasible branches 解 pure reciprocal head-on；三次 full-scale 最小距离 `197.8 / 100.8 / 132.5 m`。一例 direction 不合规、两例合规。说明 DCPA=0 对 sample-based reactive MPC 并非先天不可解。
- Thyri 报告的 reactive stagnation 典型几何接近直角交叉，不是 head-on；这是“head-on universal hardest”的反例。
- Sun et al. 2021 用 SQP 做 maritime MPC，但未测 pure DCPA=0 convergence boundary。

### 5.2 初始 range / TCPA 对照

| 来源 | 方法 | Initial range | Initial TCPA | Initial DCPA | 解释 |
|---|---|---:|---:|---:|---|
| MASS-L3 ho | PROJECT_FACT | `10000 m = 5.40 NM` | `1620 s = 27 min` | `0` | 当前 calibration scenario |
| Zhang et al. 2025 close-quarter IQMPC | DOMAIN_EVIDENCE（复算） | `179.6 m` | `31.0 s` | `0` | reciprocal straight-line；未报告 solver/convergence |
| Zhang et al. 2025 immediate-danger IQMPC | DOMAIN_EVIDENCE（复算） | `183.8 m` | `31.7 s` | `0` | reciprocal straight-line；未报告 solver/convergence |
| Sawada et al. 2021 Imazu training setup | DOMAIN_EVIDENCE | OS path ±6 NM；TS 按 collision-at-origin 布置 | `30 min` | collision-course / nominal `0` | benchmark initialization，不是 MPC threshold |
| Eriksen et al. 2020 layer entry | DOMAIN_EVIDENCE | 未给通用 initial range | `≤270 s` | `<900 m` | state-machine activation，不是 scenario initial geometry |

`[DOMAIN_EVIDENCE]` 文献的 head-on initialization 从约 31 s close-quarter 到 30 min benchmark 均有；不存在单一“通常 range/TCPA”。当前 5.4 NM / 27 min 落在 long-lead benchmark 时间尺度内，却远早于 Eriksen `270 s` encounter-state entry，也超过 Thyri `600 s` horizon。它不能仅因 DCPA=0 被宣称“先天不可解”。🟡

`[DOMAIN_EVIDENCE]` 未找到论文明确测量 acados/SQP 在 Rule 14、dead-ahead、DCPA=0 下随 gap 变化的 convergence boundary。⚫

## §6 Gate 阈值建议（Q6，带不确定性）

### 6.1 G1：不要把 252 m 写成 COLREGs threshold

定义：

\[
g_{CPA}=\max(0,\ cpa_{safe}-\min_k CPA_{pred}(k)).
\]

`[PROJECT_FACT]` P5 只得到当前 `N=80, dt=15 s, T_h=1200 s`、当前 seed/barrier/acados-HPIPM、单一 dead-ahead scan 的 bracket：`g=252 m` 收敛，`g=352 m` 失败。边界在 `(252,352) m`；252 m 不是已识别 boundary，更不是海事规范值。

建议分两层：

1. **当前 provisional dispatch guard**：`g_CPA ≤ 200 m` 才进入 acados；`200–250 m` 作为 characterization band；`>250 m` 进入 hybrid fallback。`200 m` 相对唯一已知通过点 252 m 留 `20.6%` guard。`[PROJECT_FACT + 工程推断]` 🔴 Low：只来自单场景项目证据，文献不背书该数字。
2. **v3 目标判据**：不要单用 `g_CPA`。至少组合：
   - predicted critical entry 落入需行动窗口；
   - horizon 内 minimum CPA/domain margin；
   - predicted critical exit 落入 `T_h−T_after_pass_padding`；
   - encounter role / Rule 14 starboard topology；
   - dynamic reachability 和 ODD uncertainty margin。
   `[DOMAIN_EVIDENCE]` Thyri 的 event-based horizon condition、Eriksen 的 dCPA+tCPA hysteresis、Johansen 的 predictive vessel simulation共同支持此结构。🟡

若实现只能接受一个 hard number，则用 **200 m provisional**，名称明确为 `PROJECT_EMPIRICAL_ACADOS_GUARD`；禁止叫 `COLREGs ample-time threshold`。提升到 250/252 m 前，至少完成 dead-ahead、±小 lateral offset、不同 TCPA/relative speed、single/multi-target、cold/warm seed 的重复 sweep。

### 6.2 G2：1.0 是几何不可达边界，不是安全放行阈值

定义：

\[
R_{reach}=\frac{\Delta_{req}}
{\tfrac12 u\,\dot\psi_{max}\,T_h^2}.
\]

- `R_reach > 1`：在 constant-speed、small-angle、instantaneous ROT availability 假设下不可达。**1.0 可作 analytic hard reject boundary**。
- `R_reach ≤ 1`：只说明简化模型未证明不可达；不证明 actuator lag、ROT acceleration、current、route corridor、COLREG topology、L4 tracking 下可执行。
- **当前 provisional dispatch**：`R_reach ≤ 0.8` 才放行；`0.8–1.0` characterization / conservative fallback；`>1.0` reject。`0.8` 是 20% project engineering margin，无 maritime literature calibration。`[PROJECT_FACT + 工程推断]` 🔴 Low。
- `[DOMAIN_EVIDENCE]` Eriksen/Johansen/Thyri 支持 maneuverability-aware prediction；没有人用此 ratio 或 1.0/0.8 数值作 solver dispatch。Tsolakis 对强制角加速度下界产生 nonconvex/disconnected set 的结果，反对把 G2 进一步写成“ROT 必须接近上限”的优化约束。🟡

`T_h` 不能无条件取完整 1200 s。应先定义有效 maneuver window：

\[
T_{eff}=\min\left(T_h,\ \max(0,t_{enter,crit}-T_{reserve})\right),
\]

再以经本船 transient maneuver model 验证的可达 lateral displacement 替换理想化分母。否则 `T_h²` 会在远期目标上夸大可达位移，把实际来不及的 encounter 误判为可行。`[DOMAIN_EVIDENCE]` Thyri 以 `t_enter,crit` 放置 maneuver window，而非无条件使用全 horizon。🟡

G2 只能做 pre-solve cheap rejector。最终 executability 仍需动态 rollout：包含 yaw-rate/rudder lag、速度变化、环境扰动、L4 command tracking，检查 Rule 8 readily-apparent action、CPA floor、past-and-clear、无 crossing-ahead。

### 6.3 推荐 gate 语义

```text
encounter relevance
  = critical-entry / CPA-domain / role / ODD
        ↓
pre-solve feasibility screen
  = G1 event-chain + G2 dynamic reachability margin
        ↓
mid solver
        ↓
post-solve validity
  = solver success + hard constraints + committed-prefix executability
        ↓ failure
previous valid mid plan (only if still valid) + BC-MPC / M7 / MRM
```

`[DOMAIN_EVIDENCE]` 文献最强支持不是“瞬时 CPA gate”，而是 **time/range encounter state + horizon event coverage + maneuverability + independent reactive fallback**。若必须在 G1/G2 二选一：G1 更接近现有 COLREGs planning literature；G2 适合作为必要但不充分的 feasibility screen。🟡

`[PROJECT_FACT + DOMAIN_EVIDENCE]` 当前 ho 初始 `TCPA=1620 s` 大于 `T_h=1200 s`。此时 eventual DCPA=0 不应自动解释成“当前 NLP 必须处理 420 s horizon 之外的 CPA 事件”，也不应直接派给 reactive layer；更接近 Eriksen/Thyri 的语义是 monitoring + 60 s replan，直到 critical-entry/CPA/past-pass event 能被 horizon 完整表达。若业务要求从 t=0 提前改变航向，则必须延长 horizon、加入 terminal/event constraint，或明确 committed-prefix 跨周期契约；不能靠放宽 G1 gap 数字掩盖 horizon truncation。

## §7 引用

1. Wang, B.; He, Y.; Hu, W.; Mou, J.; Li, L.; Zhang, K.; Huang, L. (2021). “A Decision-Making Method for Autonomous Collision Avoidance for the Stand-On Vessel Based on Motion Process and COLREGs.” *Journal of Marine Science and Engineering*, 9(6), 584. [DOI 10.3390/jmse9060584](https://doi.org/10.3390/jmse9060584).
2. Eriksen, B.-O. H.; Bitar, G.; Breivik, M.; Lekkas, A. M. (2020). “Hybrid Collision Avoidance for ASVs Compliant With COLREGs Rules 8 and 13–17.” *Frontiers in Robotics and AI*, 7, 11. [DOI 10.3389/frobt.2020.00011](https://doi.org/10.3389/frobt.2020.00011); [author preprint arXiv:1907.00198](https://arxiv.org/abs/1907.00198).
3. Eriksen, B.-O. H.; Breivik, M. (2017). “MPC-Based Mid-Level Collision Avoidance for ASVs Using Nonlinear Programming.” *IEEE CCTA 2017*, pp. 766–772. [DOI 10.1109/CCTA.2017.8062554](https://doi.org/10.1109/CCTA.2017.8062554).
4. Eriksen, B.-O. H.; Breivik, M.; Wilthil, E. F.; Flåten, A. L.; Brekke, E. F. (2019). “The Branching-Course Model Predictive Control Algorithm for Maritime Collision Avoidance.” *Journal of Field Robotics*, 36(7), 1222–1249. [DOI 10.1002/rob.21900](https://doi.org/10.1002/rob.21900); [arXiv:1907.00039](https://arxiv.org/abs/1907.00039).
5. Thyri, E. H.; Breivik, M. (2022). “Collision Avoidance for ASVs Through Trajectory Planning: MPC with COLREGs-Compliant Nonlinear Constraints.” *Modeling, Identification and Control*, 43(2), **55–77**. [DOI 10.4173/mic.2022.2.2](https://doi.org/10.4173/mic.2022.2.2); [publisher PDF](https://www.mic-journal.no/PDF/2022/MIC-2022-2-2.pdf).（请求给出的 15–32 页不符。）
6. Johansen, T. A.; Perez, T.; Cristofaro, A. (2016). “Ship Collision Avoidance and COLREGS Compliance Using Simulation-Based Control Behavior Selection With Predictive Hazard Assessment.” *IEEE Transactions on Intelligent Transportation Systems*, 17(12), 3407–3422. [DOI 10.1109/TITS.2016.2551780](https://doi.org/10.1109/TITS.2016.2551780).
7. Imazu, H.; Koyama, T. (1984). “The Determination of Collision Avoidance Action.” *Journal of the Japan Institute of Navigation*, 70, 31–37. [DOI 10.9749/jin.70.31](https://doi.org/10.9749/jin.70.31).
8. Imazu, H.; Koyama, T. (1984). “The Optimization of the Criterion for Collision Avoidance Action.” *Journal of the Japan Institute of Navigation*, 71, 123–130. [DOI 10.9749/jin.71.123](https://doi.org/10.9749/jin.71.123).
9. Imazu, H. (1987). “Research on Collision Avoidance Manoeuvre System through Imazu Problem.” *Proceedings of the Japan Society of Naval Architects and Ocean Engineers*, 17, 191–194. **本条书目信息来自后续同行评审论文的 reference list；原文未取得。**
10. Imazu, H. (2014). OZT / obstacle-zone-by-target work. *Navigation*, 188, 78–81. [DOI 10.18949/jinnavi.188.0_78](https://doi.org/10.18949/jinnavi.188.0_78).
11. Sawada, R. et al. (2021). “Automatic Ship Collision Avoidance Using Deep Reinforcement Learning with LSTM in Continuous Action Spaces.” *Journal of Marine Science and Technology*. [DOI 10.1007/s00773-020-00755-0](https://doi.org/10.1007/s00773-020-00755-0).
12. Veitch, E. et al. (2024). “Human Factor Influences on Supervisory Control of Remotely Operated and Autonomous Vessels.” *Ocean Engineering*, 299, 117257. [DOI 10.1016/j.oceaneng.2024.117257](https://doi.org/10.1016/j.oceaneng.2024.117257); [repository PDF](https://core.ac.uk/download/pdf/619653173.pdf).
13. Sun, X. et al. (2021). “Multi-Ship Control and Collision Avoidance Using MPC and RBF-Based Trajectory Predictions.” *Sensors*, 21(21), 6959. [DOI 10.3390/s21216959](https://doi.org/10.3390/s21216959).
14. Tsolakis, A.; Benders, D.; de Groot, O.; Negenborn, R. R.; Reppa, V.; Ferranti, L. (2022). “COLREGs-Aware Trajectory Optimization for Autonomous Surface Vessels.” *IFAC-PapersOnLine*, 55(31), 269–274. [DOI 10.1016/j.ifacol.2022.10.441](https://doi.org/10.1016/j.ifacol.2022.10.441).
15. Zhang, H.; Cao, Y.; Shan, Q.; Sun, Y. (2025). “Collision Avoidance for Maritime Autonomous Surface Ships Based on Model Predictive Control Using Intention Data and Quaternion Ship Domain.” *Journal of Marine Science and Engineering*, 13(1), 124. [DOI 10.3390/jmse13010124](https://doi.org/10.3390/jmse13010124).
16. Gonzalez-Garcia, A.; Collado-Gonzalez, I.; Cuan-Urquizo, R.; Sotelo, C.; Sotelo, D.; Castañeda, H. (2022). “Path-Following and LiDAR-Based Obstacle Avoidance via NMPC for an Autonomous Surface Vehicle.” *Ocean Engineering*, 266, 112900. [DOI 10.1016/j.oceaneng.2022.112900](https://doi.org/10.1016/j.oceaneng.2022.112900). **Non-COLREGs boundary case only.**
17. Kerrigan, E. C.; Maciejowski, J. M. (2000). “Soft Constraints and Exact Penalty Functions in Model Predictive Control.” UKACC International Conference Control 2000. [Author publication page](https://www.imperial.ac.uk/people/e.kerrigan/publications.html). **Non-maritime theoretical background only；仅用于回答 G3 是否存在领域迁移，未用于阈值标定。**

## Open questions / evidence gaps

1. Imazu 1987 primary thesis/full benchmark definition未取得；22/42 case lineage、请求所指精确版本仍需馆藏级核对。
2. “Bøhn / Hexel”未匹配到 maritime COLREGs-MPC solver papers；需准确题名、DOI，或确认是否误指 Bø / Hexeberg。
3. 无 acados SQP + ship-to-ship COLREGs 原始论文；无 pure head-on DCPA=0 convergence sweep；G1/G2 数字只能通过项目 factorial SIL 标定。
4. G1 的 `CPA_pred(k)`、M2 linear CPA、BC-MPC trajectory minimax CPA、M7 checker CPA 语义尚未统一；统一前不能共享一个“1852 m 对齐”论证。
5. G2 简式未建模 yaw acceleration、rudder lag、current、speed change、L4 exact execution；需要本船 maneuvering envelope 与 L4 tracking evidence。
6. NLM research service 本轮不可用；未形成 `[NLM_SOURCE]` 交叉验证或 durable source import。
