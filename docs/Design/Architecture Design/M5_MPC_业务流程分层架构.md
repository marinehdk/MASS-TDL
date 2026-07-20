# M5 Mid-MPC 业务流程分层架构

> **状态**:架构骨架(2026-07-20),后续对话逐层填充。每层有 GATE,GATE 通过打 ✓。
> **关联**:根因报告 `docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md`;决策日志 `docs/superpowers/design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md`。
> **HEAD**:`4fd37fd7e9fc435656e2154d92b859920a0eb646`(分支 `codex/m5-design-grounding`)。
> **证据溯源**:NLM colav_algorithms(高置信)[R12];GNC 独立评审(agent_03ab040d,PASS_WITH_FINDINGS)[R13]。

---

## 0. 文档目的与使用方式

本文档是 M5 Mid-MPC 的**业务流程导航图**,目标是:

1. 让任何一次 MPC 缺陷都能**快速归位到具体责任层 + 模块**(配合 §8 排障定位表)。
2. 支撑**逐层实施 + 阶段门管控**:每层"讨论→设计→实施→测试→GATE→打钩→下一层",层间不跳。
3. 作为后续对话的**持久参照**:读 §9 GATE 打钩状态即知进度,不依赖对话上下文。

**使用方式**:遇到 MPC 问题,先查 §8 排障表定位层;层内查 §3-§7 的模块契约定位子模块;修复后更新 §9 对应 GATE 状态。

---

## 1. 三段法快速归位(先用这个)

任何 MPC 问题,先问三句:

| 段 | 覆盖层 | 核心问题 | 典型现象 |
|---|---|---|---|
| **一·问题定义段** | L0 + L1 + L2 | **题是不是写错了?** | OCP 不可行、stage0 卡死、hard/soft 混乱、时域太短 |
| **二·数值求解段** | L3 | **题数值上能不能稳定求出来?** | QP failure、Hessian 病态、迭代震荡、condensing 病态 |
| **三·工程输出段** | L4 + L5 | **求出来的东西能不能安全落地?** | 数值收敛但业务不可接受、GNC 跟不了、fallback 不稳定 |

**80% 的 MPC 问题能被这三句快速归位。** 归位后再进对应层的细查。

---

## 2. 总体分层架构

```
┌─────────────────────────────────────────────────────────────────┐
│ LX 横向诊断与可观测性层(贯穿全流程,一等公民)                  │
│   X1 Problem Snapshot · X2 Iteration Logger · X3 Activation Trace │
│   X4 Failure Classifier · X5 Visualization                       │
└─────────────────────────────────────────────────────────────────┘
        ▲            ▲            ▲            ▲            ▲
        │            │            │            │            │
┌───────┴────┐ ┌─────┴────┐ ┌─────┴────┐ ┌─────┴────┐ ┌─────┴────┐
│ L0 上游输入 │ │ L1 OCP   │→│ L2 求解  │→│ L3 数值   │→│ L4 解复核 │→│ L5 输出  │
│ 业务上下文 │ │ 建模     │ │ 准备     │ │ 求解     │ │          │ │ 降级    │
│ 入口       │ │ 问题装配 │ │ 桥梁     │ │ 引擎     │ │ 质量门   │ │ 执行闭环│
└────────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘ └─────────┘
   题写错?  ←      题写错?      题写错?    题难求? →   解不能用? →  解不能用?
   (M2/M4/M6/L2)                                       (独立校验)    (L4 fallback)
```

**层间依赖(单向,不可跳)**:L0 → L1 → L2 → L3 → L4 → L5。LX 贯穿全部。

---

## 3. L0 上游场景与任务输入层

> **职责**:把外部世界(M2/M4/M6/L2)+ 业务意图变成 M5 可用的标准化输入。不是求解器。

### 模块契约

| 模块 | 输入来源 | 输出 | 当前状态 |
|---|---|---|---|
| 0.1 World Model Adapter | M2 NavFilter + targets | own_ship 状态(px,py,psi,r,u)+ targets 位置/速度/航向 | ✓ 正常 |
| 0.2 Behavior Intent Adapter | M4 BehaviorPlan | 行为意图(避碰/保持/回归)、是否变速、pref_dir | ✓ 正常 |
| 0.3 COLREGs Semantic Adapter | M6 COLREGsConstraint | Rule 13/14/15/16/17、give-way/stand-on 角色、规则窗口 | ✓ 正常 |
| 0.4 Route/Mission Adapter | L2 航线 | 名义航线、rejoin 目标、corridor 边界、参考切线 | ✓ 正常 |
| 0.5 Vessel Capability Adapter | GNC ODD | max ROT、speed min/max、decel、GNC 可跟踪边界 | ✓ 正常(但 heading_box_reachable 是隐性合约,见 L0 风险) |

### L0 已知风险(来自 GNC 评审 [R13] Q3)

- **box-reach 方向 vs pref_dir 一致性是隐性 M4 合约**:`heading_box_reachable_from_psi0_deg` 由 M4 publish,语义"从 own_psi 沿 ROT 单向能到达 box 边界"。M4 合约保证方向一致,但 M5 代码层无 sanity assert(`box_reach_side == pref_dir_side`)。若 M4 future 行为变化,M5 静默接受错误符号。
- **修复建议**:L0.2/L0.5 加 sanity assert,失败则降级到 ROT-only(v2.1 公式)。

### L0 故障现象

- TS 预测不对(0.1)
- 规则判错(0.3)
- 航线参考错误(0.4)
- 船舶能力过紧/过松(0.5)
- 坐标系/单位混乱(全模块)

---

## 4. L1 OCP 建模与问题装配层 ⚠️ C1 修复主战场

> **职责**:把业务语义变成数学优化问题。回答"我们到底让求解器解一个什么题?"
> **当前缺陷集中区**:F-02(CPA hard/soft)、F-04(min-alt 不可控)、DP-01/03/07/08。

### 模块契约

| 模块 | 职责 | 当前状态 | 关联 finding/DP |
|---|---|---|---|
| 1.1 Decision Variable | x=[px,py,psi,r,u], u=[delta,n], slack | ✓ nx=5,nu=2,nsh=16 正确 | — |
| 1.2 Horizon & Grid | T=1200s, N=80, Δt=15s, 均匀网格 | ✓(C2 才看 ERK/RK4) | Step 8 |
| 1.3 Dynamics Model | 显式 Euler,5 状态 2 控制 | ✓ 正确(C1 不改) | — |
| 1.4 Objective Builder | J_colreg + J_dist + J_route + J_vel + J_asym + J_transition + J_terminal | ✓ 正确(soft aspiration 在 cost) | — |
| **1.5 Constraint Builder** | 硬/软约束:状态边界、控制边界、CPA、direction、min-alt、prefix | **❌ F-02 CPA 2500 当硬;F-04 min-alt 每 stage 激活** | **F-02, F-04, DP-01, DP-07** |
| **1.6 Rule-to-Constraint Mapper** | Rule14 右转→stage 激活、substantial action、past-and-clear | **❌ acados 无 reachability schedule(IPOPT 已有)** | **F-04, DP-03, DP-08** |
| 1.7 OCP Assembler | 打包成完整 OCP(x/u/dynamics/cost/constraints/params) | ✓ 结构正确(内容待 L1.5/1.6 修) | — |

### L1 关键缺陷详解(逐 row)

#### 1.5.a CPA hard/soft 混淆(F-02,DP-01)

| 层 | 职责 | 当前 acados | 应该是 | 证据 |
|---|---|---|---|---|
| Hard floor(约束 row) | 安全底线:距离 ≥ 1852m,绝对不许穿过 | ❌ 用 cpa_safe=2500 当硬 row threshold,且 idxsh slack 可软化 | hard 1852,不可 slack(或仅 prefix 段可控,见 DP-04) | `formulation.cpp:333,340` 只用 cpa_safe;`gen:315,323` [R3] |
| Soft aspiration(cost) | 舒适/习惯:鼓励保持 2500m | ✓ `J_colreg` 的 `exp(−ζ(d−cpa))` 已是 soft | 保持,但确认读 cpa_safe(2500)而非 cpa_hard(1852) | `formulation.cpp:374` [R3] |

**修复方向(VR-01,BL-A 确认路径 A)**:双独立 slot —— `kGIdxCpaHard`(row threshold=1852)+ `kGIdxCpaSafe`(cost=2500)。nh 从 20 调整为容纳 hard+soft 双 CPA row(BL-A 待定 nh=20 单 hard+soft cost vs nh=36 双 row,两者兼容路径 A,需 TDL Lead 确认)。

**⚠️ idxsh 耦合闭环(BL-A 风险 2,必须在 L1 GATE 验证)**:BL-A 从 acados v0.4.4 源码 `ocp_nlp_constraints_bgh.c:1411-1417` 证实:**一旦 row 进入 idxsh,acados 在残差里减 slack,该 row 结构上永远 soft**。增大 Zl 只能让 slack 代价变大,**永远不能强制 slack==0**。所以 hard 1852 row **必须排除出 idxsh**(runtime 每 cycle 每 stage 重发 idxsh 时排除),否则 VR-01 静默退化为已弃用的 ALT-02。当前 `gen:510 idxsh = arange(NT)+2` codegen 静态索引所有 CPA row,必须改为 runtime 动态排除 hard row。

#### 1.5.b min-alt 不可控(F-04,DP-03,VR-03 修正为 b')

| 问题 | 证据 |
|---|---|
| min-alt row 每 stage 激活(含 stage1),但显式 Euler 下 `ψ₁=ψ₀+Δt·r₀`,live `r₀=0` → `u₀` 只能改 `r₁` 不能改 `ψ₁` → stage1 row 不可控 | `gen:331-335` [R9] |
| MMG witness:30° 最早 40.79s(live)→ stage3,49.95s(benchmark)→ stage4 | [R11][R22] |
| **BL-B 新发现 Q8-reach**:IPOPT ROT-reach 公式 `ceil(min_alt/rot_step)−1` 是 **surrogate-derived**(rot_max=4.7°/s GNC ODD 名义),MMG oracle 实测 max ROT 仅 0.983°/s(live)/0.782°/s(benchmark),**差 5x**。IPOPT 给 k=1,物理实际 k=3。根因:rudder slew 2°/s 限制下转饱和需 11.5s。**IPOPT reachability 本身就是错的,不是正确参照系** | `mid_mpc_solver.cpp:462-502` [R4][R20] |

**修复方向(VR-03 修正为选项 b')**:**不是镜像 IPOPT**(IPOPT 公式是 surrogate-derived 缺陷)。采用 BL-B 推荐的 b':
1. **在线路径保留 max(ROT,box) 结构**,但 rot_step 除以保守因子 `kSurrogateFudgeFactor`(由离线 MMG oracle 校准:live 4.78x、benchmark 6.01x、target2500 3.57x,速度相关)。
2. **离线 MMG witness 保留为回归 oracle**,CI 强制每个 COLREG scenario 通过 `witness_reach_time/dt_NLP ≤ minalt_hard_from_k` cross-check。在线 k < oracle k → CI RED。F-01 教训推广。
3. **"独立"边界**:独立于 acados solver surrogate(保守因子+oracle cross-check),独立于 M4 hint(advisory),不需独立于 M4 box_reach(几何信息)。
4. **BL-B escalation**(未闭环):保守因子速度分段(单标量 vs 速度表,需 NLM ship_maneuvering);node fallback 1.2°/s vs live ODD 4.7°/s ROT 源不一致。

#### 1.5.c prefix 不可控段(Q4 决策,基于 BL-A/B 已明确)

| 问题 | 证据 |
|---|---|
| committed prefix(k<prefix_active_k)几何冻结,CPA 由 committed prefix 负责 | 现有 `build_stage_row_bounds:178` prefix 段 CPA relax |
| **GNC 评审 Q4(高严重度)**:IPOPT `apply_colreg_prefix_soften_` relax prefix CPA row,但 σ 全局标量**仍加到这些 row** → prefix 几何违反 hard floor 被 σ 吸收,返回 Converged+σ>0。**这正是 VR-04/ALT-03 明令拒绝的 fail-open 形态** | `row_registry.hpp:256-270` [R14];GNC 评审 [R13] Q4 |
| **BL-A 确认 acados Q4 形态不同**:acados 的 fail-open 不是 σ 全局标量,而是 `gen:510 idxsh` codegen 静态索引所有 CPA row(含 prefix 段),每 stage 每 target 独立 slack 天然吸收 prefix 违反。**不需要 σ 全局标量**。 | `gen:510` [R18][R19] |

**修复方向(基于 BL-A,方向 C 防御纵深,拆 IPOPT/acados 子项)**:
- **acados 侧(路径 A)**:gen nh 含 hard 1852 + soft 2500 双 CPA row;wrapper runtime 每 cycle 每 stage 重发 idxsh,**hard row 永远不在 idxsh**,soft row 仅 suffix 段(k≥prefix_active_k)进 idxsh。BL-A 从 acados v0.4.4 源码验证 bgh 模块支持 runtime 每 stage 更新 idxsh。
- **IPOPT 侧(σ conditional)**:σ expression 不加到 k<K prefix CPA row(expression 层 conditional),prefix 段 CPA 真 hard。
- **D1(两路径共享,第 2 层防御)**:committed prefix CPA 由独立 MMG/L0 几何 witness(prefix 冻结几何,NLP 无法改变),违反 hard floor → NO_SAFE_PLAN + M7(VR-04)。
- **prefix_active_k 动态性**:BL-A 确认 runtime 变化(0→12→K_max,范围 0..N-8),codegen 静态分区失败 → runtime 每 cycle 每 stage 重发 idxsh 是必须的。wrapper 已每 cycle 每 stage 计算 prefix_K,加 idxsh 重发仅 O((N+1)·NT) 整数写入。

#### 1.6 Rule-to-Constraint Mapper(reachability schedule,BL-B 修正)

| 约束 row | IPOPT 状态 | acados 状态 | 需做(基于 BL-A/B) |
|---|---|---|---|
| min-alt | ❌ **surrogate-derived 缺陷**(Q8-reach,rot_max×dt 乐观差 5x) | ❌ 无 | **两路径共修 VR-03 b'**:保守因子 + oracle cross-check(非镜像 IPOPT) |
| direction | ✓ `direction_hard_from_k`(k=0 wrong-side soften,§4.4) | ❌ 无 | **acados 镜像**(GNC Q7:不镜像则 wrong-side 几何直接 infeasible) |
| CPA | ✓ `cpa_hard_from_k`(k_initial_relax=2 floor + geometric-reach) | ❌ 无 | acados 镜像 + 路径 A idxsh 排除 hard row |
| heading box | ⚠️ 静态全 horizon(GNC Q5 future-gap) | ❌ stage≥1 静态 | **两路径共修**:wrapper 每 stage 写 live box + 可达 suffix schedule(VR-06) |

### L1 故障现象

> **特征:不是 ACADOS 算不好,而是题目本身写错了。**

- 原始 OCP 就不可行(1.5)
- stage0 被最小改向硬约束卡死(1.5/1.6)
- hard/soft CPA 映射错误(1.5.a)
- 约束激活窗口与动力学可达性冲突(1.6)
- 时域太短导致不可能完成避碰(1.2)

---

## 5. L2 初始轨迹与求解准备层

> **职责**:连接"业务定义"(L1)和"数值求解"(L3),给求解器合理起点。

### 模块契约

| 模块 | 职责 | 当前状态 | 关联 finding/DP |
|---|---|---|---|
| 2.1 Seed Generator | straight seed / shift warm start / rule-guided seed | ✓(straight seed 已被消融证明非 sole root) | Step 5 |
| 2.2 Initial Guess Conditioner | seed 平滑/插值/状态一致化 | ✓ | — |
| **2.3 Parameter Loader** | 目标船预测、安全距离、规则参数、phase activation mask 写入每 stage | **❌ F-03 runtime box 未落地** | **F-03, DP-02** |
| 2.4 Bound/Slack Configurator | x0、状态/控制上下界、非线性约束边界、slack 权重 | **❌ stage≥1 静态 box;idxsh slack 策略待定(BL-01)** | **DP-02, BL-01** |
| 2.5 Solver Backend Selector | IPOPT/acados、Hessian、SQP、condensing | ✓(C2 才调) | Step 6/7 |

### L2 关键缺陷:F-03 runtime box 未落地(DP-02)

| idx | 变量 | 当前 codegen box | live 输入应有 | wrapper 实际 |
|---|---|---|---|---|
| 2 psi | 静态 ±π | heading_min/max_rad(如 23.2°..53.2°) | ❌ 只 stage0,stage≥1 静态 |
| 3 r | 静态 ±ROT_MAX | rot_max_rad_s | ❌ 只 stage0 |
| 4 u | 静态 [0,15] | speed_min/max_mps | ❌ 只 stage0 |

**证据**:`mid_mpc_acados_solver.cpp:984-1009` wrapper 只 set stage0 lbx/ubx(=x0 固定) [R8]。
**修复方向(VR-02)**:wrapper 每 cycle 每 stage 写 live heading/speed/ROT box + reachability schedule(与 L1.6 heading box schedule 同源)。

### L2 故障现象

- seed 正对目标船导致线性化退化(2.1)
- warm start shift 错位(2.1)
- 初值不满足动力学(2.2)
- 参数没正确写进每个 stage(2.3,即 F-03)
- slack 忘记开或权重极端(2.4)
- backend 配置不适合当前问题(2.5)

---

## 6. L3 数值求解迭代层 ⚠️ C2 修复主战场

> **职责**:在 seed 附近不断寻找更好轨迹,直到收敛。图 1 核心求解引擎层。
> **当前缺陷**:F-05(EXACT Hessian + R=0 + no-reg 数值脆弱)。但**必须在 L1 OCP 正确后才能调**(否则在错误 OCP 上调数值 = 归因漂移)。

### 模块契约(对应图 1 步骤 4-7)

| 模块 | 对应图1 | 职责 | 当前状态 | 关联 Step |
|---|---|---|---|---|
| 3.1 Linearization | 步骤4 | 动力学/约束线性化、目标二次近似、构造局部 QP | ✓(内部黑箱) | — |
| 3.2 QP Builder | — | Hessian + gradient + linearized constraints + bounds | ✓ | Step 6 |
| 3.3 QP Solver | 步骤5 | HPIPM 求解 → Δz | **❌ 首QP NAN_SOL(raw4/QP3)** | Step 7 |
| 3.4 Globalization | 步骤6 | line search / merit backtracking / funnel → α | ✓(MERIT_BACKTRACKING) | — |
| 3.5 Iterate Updater | 步骤7 | z^(i+1) = z^(i) + α·Δz | ✓ | — |
| 3.6 SQP Loop Controller | 步骤8-9 | iteration count / stopping / failure / timeout | ✓(max_iter 400, tol 1e-9) | Step 9 |

### L3 关键缺陷:F-05 数值脆弱(非 sole root)

| 证据 | 结论 |
|---|---|
| condensed H min eig:−2.6e11 / −2.0e11 / −2.9e13,负特征值 23/6/18 | EXACT Hessian 强不定 |
| 80 stages R=0, NO_REGULARIZE | 无控制曲率正则化 |
| 14-arm 单变量消融全部 raw4(Step 5-9 变体) | **任一单项不是 sole root**(在错误 OCP 上证伪) |

**重要**:**L3 修复必须等 L1 OCP 正确后**。当前消融在错误 OCP 上完成,只能证伪"某项非 sole root",不能决定正确 OCP 上的最佳数值配置。C1 后必须在正确 OCP 上重跑最小数值矩阵。

### L3 故障现象

> **特征:问题能否解 ≠ 数值是否好解。L3 是"好不好解"的问题。**

- QP failure(3.3,即当前 raw4)
- Hessian 不定(3.2)
- line search 接受不了步长(3.4)
- SQP 震荡(3.6)
- 迭代卡在初始点附近(3.5)
- full condensing 长时域病态(3.2)
- tolerance 太严(3.6)

---

## 7. L4 解复核与 L5 输出降级层 ⚠️ C3+C4 修复区

> **L4 职责**:求解器说"算完了",不代表业务上"可用"。独立校验真收敛/真可行/真合规/真可下发。
> **L5 职责**:安全下发可执行结果,必要时走 fallback。
> **当前缺陷**:F-01(status fail-closed)、DP-04(NO_SAFE_PLAN)、DP-05。

### L4 模块契约

| 模块 | 职责 | 当前状态 | 关联 finding/DP |
|---|---|---|---|
| **4.1 Numerical Convergence Checker** | KKT、stationarity、primal/dual residual、solver status | **❌ F-01 当前 fail-open:raw4 可被重映射 Converged** | **F-01, DP-05** |

> **⚠️ L4.1 反向依赖 L1(评审点 2)**:`constraints_satisfied_` 复用 L1 的 `con_h_expr`(`mid_mpc_acados_solver.cpp:555-568` h_fn lazy-build 缓存 formulation graph)。**L4→L1 反向读取**:L1 改 con_h_expr(C1 加 hard slot / 路径 A 改 nh)时,L4.1 的 h_fn cache 必须 rebuild。C1 与 C3 并行时,con_h_expr 是共享点,C1 merge 后 C3 必须重跑 h_fn rebuild 测试。
| 4.2 Feasibility Checker | 动力学一致、box、安全约束、soft 违背量 | ⚠️ `constraints_satisfied_` 只做部分 primal recheck,无 stationarity/complementarity | DP-05 |
| 4.3 COLREGs Consistency Checker | Rule14 右转、substantial action、past-and-clear、不合理回切 | ⚠️ 未独立实现 | C4 |
| 4.4 Trackability Checker | 曲率、航向变化率、舵率、速度指令可执行性 | ✓ `tail_gate_turns_are_feasible` 存在 | C4 |
| 4.5 Solution Quality Evaluator | 避碰是否过激、轨迹平滑、过度减速、回归延迟、周期差异 | ⚠️ 部分 | C4 |

### L4 关键缺陷:F-01 status fail-closed(DP-05)

| 问题 | 证据 |
|---|---|
| raw 1→Timeout / 2→Infeasible / 3→NumericalFailure,但 raw 4 在 `solver_moved && constraints_satisfied_` 时被重映射 Converged | `mid_mpc_acados_solver.cpp:99-111,1114-1128` [R7] |
| `constraints_satisfied_` 只重建 h-rows 做部分 primal feasibility,无 stationarity、无 complementarity、无 dual feasibility | `mid_mpc_acados_solver.cpp:520-628` |
| linked enum 与 wrapper 映射不一致(1/2/7 映射错误) | [R7] |

**修复方向(VR-05,后置 C3,独立于 L1)**:
1. raw 0..7 fail-closed 映射;raw 4 绝不重映射 Converged
2. status-contract test(当前 RED)
3. success 分层:solver status / safety status / execution status 独立
4. raw0 要成为可发布 plan,必须同时 stationarity + complementarity + primal feas + dual feas 独立通过

### L5 模块契约

| 模块 | 职责 | 当前状态 | 关联 finding/DP |
|---|---|---|---|
| 5.1 Route Extractor | committed avoidance route、轨迹点、局部承诺段 | ✓ | — |
| 5.2 Output Formatter | L4 可消费格式(waypoint/trajectory/heading-speed/confidence) | ✓ | — |
| **5.3 Handover Manager** | 正常下发 GNC / 交 BC-MPC / 周期切换 / 冻结 | **❌ BC→L4 链未闭合(BC 发 reactive_override_cmd,GNC bridge 订 avoidance_plan)** | **C4** |
| **5.4 Fallback Manager** | 无解时:保持上一承诺 / 保守航线 / 降级 / 触发 M7 | **❌ Mid FinalDegrade 发 suggested_action=MRM,但 M7 concern 分支无完整执行证据** | **DP-04, C4** |
| **5.4.b Last-Safe-Maneuver Envelope Computer**(评审点 17 新增) | 独立 reachability,定义 BC-MPC 接管边界(扰动/不确定性/rudder slew/takeover latency) | **❌ 缺失**(根因报告 §10 明确) | **C4** |
| 5.5 Execution Feedback Bridge | GNC 跟踪误差、执行偏差、下周期 warm start | ✓ | — |

### L4/L5 故障现象

- 数值收敛但业务不可接受(4.5)
- 理论安全但 GNC 跟不了(4.4)
- rule 一致性差(4.3)
- 回归条件不合理(4.3)
- 轨迹抖动大(4.5)
- 输出格式不对(5.2)
- 周期切换不平滑(5.3)
- fallback 不稳定(5.4)
- BC-MPC 接管条件不清(5.3/5.4)

---

## 8. LX 横向诊断与可观测性层(一等公民,贯穿全流程)

> **职责**:把每层内部状态变成"可观测、可归因、可复现"。
> **现实实例**:你的根因报告 `runs/m5_solver_diag/` 就是 LX 的实例,没有它无法定位 raw4/QP3。

### 模块契约

| 模块 | 职责 | 当前实例 | 备注 |
|---|---|---|---|
| X1 Problem Snapshot Recorder | 每轮上游输入/OCP 参数/seed/solver config 快照 | `runs/m5_solver_diag/.../input.json` `solver_config.json` `seed_trajectory.json` | ✓ 已有 |
| X2 Iteration Logger | 每 SQP:iter/cost/KKT/violation/step/QP status | `qp_statistics.json` `iterate_000.json` `last_qp.json` | ✓ 已有 |
| X3 Constraint Activation Trace | 哪些约束哪些 stage 激活、hard/soft 生效、slack 量、最紧约束 | `derivative_diagnostics.json` 部分 | ⚠️ 待完善(**评审点 16**:缺 prefix 段 pact_pre activation trace,Q4 debug 需要) |
| X4 Failure Classifier | 自动判断:原始 OCP 不可行 / 线性化 QP 不可行 / 数值未收敛 / 输出不可执行 | 根因报告手工分类 | ⚠️ **评审风险 3b**:待自动化,**前置到 C1 并行**(L1 GATE "独立 MMG witness 对每个新 hard row 满足"需要 X4 规模化执行) |
| X5 Visualization/Debug Export | 轨迹图、约束激活图、迭代曲线、CPA-stage 图、KKT 趋势图 | A/B benchmark SVG | ⚠️ 待完善 |

### LX 证据标准(每个 case 必须保存)

```
runs/m5_solver_diag/<commit>/<config>/<case_id>/
├── input.json · solver_config.json · codegen_manifest.json
├── seed_trajectory.json · iterate_000.json
├── qp_statistics.json · derivative_diagnostics.json
├── solution.json · trajectory.csv
├── gnc_executability.json · verdict.json
```

四分类必须保持:`REFERENCE_FEASIBLE+ACADOS_SUCCESS` / `REFERENCE_FEASIBLE+ACADOS_FAILURE` / `REFERENCE_INFEASIBLE+ACADOS_REJECT` / `REFERENCE_INFEASIBLE+ACADOS_SUCCESS`。**不得共享 acados 失败假设作为 reference oracle。**

---

## 9. 排障定位表(评审/debug 直接使用)

| 现象 | 段 | 优先怀疑层 | 优先检查模块 | 关联 finding |
|---|---|---|---|---|
| 一开始就无解 | 一 | L1 | 1.6 Rule-to-Constraint Mapper、1.5 Constraint Builder | F-04 |
| HO 场景 stage0 卡死 | 一 | L1 | 1.6 规则激活时机、最小改向约束 | F-04, DP-03 |
| hard/soft CPA 混乱 | 一 | L1 | 1.5.a CPA Constraint Builder | **F-02, DP-01** |
| prefix 段违反被静默吞 | 一 | L1+L4 | 1.5.c σ 表达式 + 4.2 prefix witness | **Q4(待决策)** |
| runtime box 未生效 | 一 | L2 | 2.3 Parameter Loader、2.4 Bound Configurator | **F-03, DP-02** |
| 直线 seed 下 QP 总失败 | 一/二 | L2/L3 | 2.1 Seed Generator、3.1 Linearization、3.3 QP Solver | F-05(非 sole) |
| ACADOS 迭代不收敛 | 二 | L3 | 3.2 Hessian、condensing、3.4 globalization、3.6 tol | F-05(非 sole) |
| 首QP 就 NAN_SOL | 二 | L3 | 3.3 QP Solver(但根因常在 L1 OCP 错误) | **当前 raw4** |
| raw4 被改写成 Converged | 三 | L4 | 4.1 Convergence Checker | **F-01, DP-05** |
| 轨迹数值收敛但很难看 | 三 | L4 | 4.5 Quality Evaluator、L1.4 Objective | C4 |
| 轨迹可行但 GNC 跟不了 | 三 | L4/L5 | 4.4 Trackability、5.2 Output Formatter | C4 |
| 经常 fallback | 三 | L5 | 5.4 Fallback Manager、X4 Failure Classifier | C4 |
| 同场景时好时坏 | 跨 | LX | X1 Snapshot、X2 Iteration Logger、seed/参数一致性 | — |

---

## 10. GATE 阶段门管控(逐层实施进度)

> **规则**:每层"讨论→设计→实施→测试→GATE→打钩→下一层"。GATE 通过打 ✓,未通过打 ✗ 并标阻塞项。下个对话读此表即知进度。

| 层 | GATE 定义 | 状态 | 阻塞项 |
|---|---|---|---|
| **L0 上游输入** | L0-A 所有来源字段 isfinite/range guard + input_degraded 追溯;L0-B box-reach 标量 guard(方向 assert 待 M4 合约确认后);L0-C kCpaSafeFallback 从 yaml 读;三 case 输入 provenance 闭合 | ✓ 完成(commit 6a0c12f3b;T-L0-5/6/7 SIL 集成测试留 C4) | 无 |
| **L1a OCP 建模(规格冻结,Step2 重新定义 VR-07)** | **hard-never-in-idxsh 原则**已落 codegen+wrapper+adversarial 测试;**`kGIdxCpaHard` global slot** 新增完整 wiring(BL-08);**box live 落地**(stage0 x0 equality 不被 box 覆盖,heading/ROT/speed 每 stage);**ROT 来源修正**(GNC ODD);**terminal contract** 显式定义(BL-10);**heading/ROT schedule 分离**延至 L1b(k_head 公式依赖);**grid physical-time map 原则**(BL-13);**prefix 段 hard row relax 安全边界显式标注依赖 L1b D1**(L1a 测试限定 prefix_active_k=0) | ✓ **L1a-spec-freeze 完成(commit 07c36e43a+;批次1+批次2;902 tests 0 failures)** | heading/ROT schedule 分离延至 L1b |
| **Step5 DESIGN-IT-TWICE(nh 抉择,BL-09)** | DP-01 nh=36 双 row(ALT-08)vs nh=20+nsh=0+J_colreg(ALT-09)对比裁决;两份评审(ZCode [R23] 推 nh=36 / Codex [R24] 推 nh=20)分歧证据综合;含 m² slack 量级 / 回归面 / slack telemetry / ample-time 语义四维 | ✓ **完成(采纳方案 B,VR-01 final,2026-07-20 18:00)** | 无 — 进入 L1a-spec-freeze 实施 |
| **L1b OCP 建模(依赖 Q4 + L1a 留的公式回填)** | k_head 公式 + ample-time 下界 `t_latest_safe`(BL-12,✓);CPA suffix-hard schedule(DP-07,✓);min-alt reachability b'(VR-03,✓);prefix CPA NO_SAFE_PLAN+M7(VR-04,✓);direction row reachability schedule(✓);**Q4 σ conditional**(IPOPT,✓);**VR-03 b' 在线 k ≥ oracle k cross-check CI** | ✓ **L1 全部完成(5 commits a16c14397..44862cc17)** | 无 — L1 GATE 可关闭 |
| **L2 求解准备** | runtime heading/speed/ROT box 每 stage 落地(VR-02);idxsh runtime 重发(路径 A);seed/warm-start 与 L1 OCP 一致 | ⬜ 待实施 | L1a GATE |
| **L3 数值求解** | 三 case raw0 + 独立 KKT;每个数值变量 contribution 可隔离;latency p95 ≤ budget | ⬜ 待实施 | L1+L2 GATE(在错误 OCP 上调数值 = 归因漂移) |
| **L4 解复核** | raw 0..7 fail-closed;raw4 adversarial 不变 Converged;success 分层(solver/safety/execution 独立);**h_fn rebuild 测试(con_h 变更后)** | ⬜ 待实施 | 独立于 L1,可并行(但共享 con_h_expr) |
| **L5 输出降级** | BC→L4 链闭合证据;FinalDegrade→MRM 执行证据;fallback 链完整;**Last-Safe-Maneuver Envelope(5.4.b)** | ⬜ 待实施 | L4 GATE |
| **LX 诊断** | X1-X5 模块齐全;每 case 证据标准目录完整;**X3 补 prefix pact_pre trace**;**X4 自动分类前置到 C1 并行** | ⬠ 部分有(根因报告实例) | — |

---

## 11. 逐层实施顺序(对应 C1/C2/C3/C4,BL-A/B 修正)

| 实施阶段 | 覆盖层 | C 对应 | 前置 GATE |
|---|---|---|---|
| **阶段 1a-1:L1a-spec-freeze OCP 规格(不依赖 Q4,不依赖 nh 抉择)** | L1a(原则 + slot + box live + terminal contract + grid map 原则)+ L2(box live 落地) | C1 | BL-A T2 + ZCode T-DP01-1 测试;BUG-L1-04/05/06 在此层确认 |
| **阶段 1a-2:Step5 DESIGN-IT-TWICE nh 抉择** | L1(DP-01 row 布局 nh=36 vs nh=20) | C1 | L1a-spec-freeze GATE + 两份评审分歧证据 [R23][R24] |
| **阶段 1b:C1b OCP 语义(依赖 Q4 + Step5 + L1a 留的公式回填)** | L1b(prefix CPA + min-alt reachability + k_head + CPA suffix-hard + Q4 σ conditional) | C1 | Step5 nh 抉决 + L1a-spec-freeze GATE + Q4 决策(已明确路径 A+D1) |
| **阶段 2:C2 QP 数值条件** | L3(题难求?) | C2 | L1+L2 GATE |
| **阶段 3:C3 status/KKT fail-closed** | L4(解不能用?— 接受门) | C3 | 独立,可与 C1 并行(共享 con_h_expr,h_fn rebuild) |
| **阶段 4:C4 真实 Rule14 联合验收** | L4 + L5(解不能用?— 执行闭环) | C4 | L1-L3 GATE |

**L0 和 LX 贯穿所有阶段,不单独排队。**

---

## 12. 实施总表(7 层归属映射 + bug 记录 + 进度追踪)

> **使用规则(用户 ARCH-DECISION-04)**:
> 1. **逐层修复顺序严格**:必须按 L0→L1a→L1b→L2→L3→L4→L5 的层顺序,LX 贯穿。跨层提前修 = 返工。
> 2. **bug 记录机制**:实施前在任何层发现的 bug,**必须先记录到 §12.3 bug 待确认表**,不得跨层提前修。待该 bug 所属层开始讨论时,确认后再实施。避免来回篡改。
> 3. **进度追踪**:每项修复完成后,§12.1 状态列更新(⬜待实施 → 🔄实施中 → ✓完成 → ⚠️阻塞)。下个对话读此表即知进度。
> 4. **归属唯一性**:每个修复点有且只有一个层归属 + 一个 GATE,不得跨层。

### 12.1 修复点层归属 + 进度

> **L1a 范围重新定义(Step2,2026-07-20 17:05)**:两份独立评审(ZCode agent_1436144e + Codex,证据 [R23][R24])共识"L1a 完全独立不成立",用户裁决 L1a = 规格冻结 + 不依赖 k_head 的子项(VR-07)。原 C1a 拆为三段:**L1a-spec-freeze**(本表以下 L1a 行)+ **Step5 nh 抉择**(BL-09,DESIGN-IT-TWICE)+ **L1b**(k_head/CPA suffix-hard/prefix witness)。详见设计日志 Step2。

| 修复点 | 层 | 阶段 | GATE | 依赖 | 状态 |
|---|---|---|---|---|---|
| **L0-A** 输入值 guard + input_degraded 追溯 | L0 | L0 独立 | L0 GATE | 无 | ✓ 完成(commit 6a0c12f3b) |
| **L0-B** box-reach sanity assert + msg 注释同步 | L0 | L0 独立 | L0 GATE | M4 合约已确认 | ✓ 完成(T-L0-5 SIL 集成测试留 C4) |
| **L0-C** kCpaSafeFallback yaml/param 一致性 | L0 | L0 独立 | L0 GATE | 无 | ✓ 完成(ROS param m5.cpa_hard_m;T-L0-6/7 集成测试留 C4) |
| **DP-01 原则** hard-never-in-idxsh(codegen + wrapper runtime idxsh 排除 hard row) | L1+L2 | L1a-spec-freeze | L1a GATE | BL-A T2/ZCode T-DP01-1 测试 | ✓ **完成(commit fe251260b;nsh=0 天然排除 idxsh,符号图 T-B2 adversarial 验证)** |
| **DP-01 子项** `kGIdxCpaHard` global slot 新增完整 wiring(enum + pack + codegen SX 镜像 + np_global 154→155 + 三方 hash) | L1+L2 | L1a-spec-freeze | L1a GATE | BL-08 闭环 | ✓ **完成(commit fe251260b;slot 154 追加,target block 不动;三方 hash 待 codegen re-run)** |
| **DP-01 nh 抉择** ~~nh=36 双 row(ALT-08)vs nh=20+nsh=0+J_colreg(ALT-09)~~ | L1 | ~~Step5 DESIGN-IT-TWICE~~ | ~~Step5 GATE~~ | BL-09 闭环 | ✓ **完成(Step5 采纳方案 B:nh=20+nsh=0+J_colreg,VR-01 final,2026-07-20 18:00)** |
| **DP-01 row 布局实施** 方案 B 落地(nh=20 不变,CPA row residual 改 cpa_hard_m,idxsh 删除,NSH=0/NS=0) | L1+L2 | L1a-spec-freeze(原 L1b,Step5 后提前) | L1a GATE | Step5 nh 抉择 ✓ + DP-01 原则 + kGIdxCpaHard slot | ✓ **完成(commit fe251260b;待 codegen re-run 让 runtime 生效)** |
| **DP-01 L4 telemetry 补救** constraints_satisfied_ 去 sl_vec 读,新增 d_min + soft violation_m telemetry(方案 B 失去 slack 的补救) | L4 | L1a-spec-freeze | L1a GATE | 方案 B FB-2 缓解 | ✓ **完成(commit fe251260b;C2 docstring 已补:d_min 只覆盖 suffix stages;T4 friend-test 延后 codegen 后)** |
| **DP-02 box live** wrapper 每 solve 每 stage 重发 lbx/ubx(消费 live heading/speed/ROT);**stage0 保持 x0 equality 不被 box 覆盖**;非 codegen-default 时才调用 ocp_nlp_constraints_model_set(避免扰动 cold capsule warm-up) | L2 | L1a-spec-freeze | L1a GATE | BL-10 terminal contract | ✓ **完成(commit 07c36e43a+;902 tests,0 failures)** |
| **DP-02 ROT 来源修正** ROT 来自 GNC ODD(`mid_mpc_node.cpp:902-916`)而非 M4;solver 从 `input.rot_max_rad_s`(MidMpcInput direct field)读取 live ROT | L2 | L1a-spec-freeze | L1a GATE | 无(Codex 纠正 [R24]) | ✓ **完成** |
| **DP-02 terminal contract** 显式定义: NHN=0/NBXN=0 是设计选择——terminal stage N(1200s future)不在安全 claim 范围内。hard 约束覆盖 stages 0..N-1(path)。committed prefix + early-mid horizon 提供真实安全保证;M7/MRM 覆盖 terminal gap。若未来证据表明 gap 显著,在后续阶段加 NBXN>0。 | L1+L2 | L1a-spec-freeze | L1a GATE | BL-10 闭环 | ✓ **定义完成(见 §DP-02 terminal contract)** |
| **DP-02 heading/ROT schedule 分离** heading soften,ROT 全 stage hard(adversarial T-DP02-3) | L2 | **L1b**(依赖 k_head 公式 BL-12) | L1b GATE | DP-08 k_head(L1b) | ➡ **延至 L1b** |
| **DP-08 原则** grid physical-time map:所有 schedule 先按物理秒定义再映射各自 backend grid,禁裸 k parity(IPOPT/acados off-by-one)。IPOPT psi[0] vs acados stage0 差一 stage,物理秒映射消除差异。 | L1+LX | L1a-spec-freeze | L1a GATE | BL-13 原则层闭环 | ✓ **原则定义完成(见 §DP-08 原则)** |
| **DP-08 k_head 公式** k_head + ample-time 下界 `t_latest_safe`(双量) | L1 | **L1b**(原 C1a) | L1b GATE | BL-12 Step3/Step5 | ✓ **完成(commit a16c14397;acados compute_reachability_schedule 含 k_head_earliest/k_head_latest 双量 + window cross-check)** |
| **DP-03** min-alt reachability b'(保守因子 + oracle cross-check) | L1 | C1b | L1b GATE | Q4 已确认 | ✓ **完成(commit 44862cc17;kSurrogateFudgeFactor=2.0 应用于 acados+IPOPT 双路径;oracle cross-check log)** |
| **DP-04** prefix CPA NO_SAFE_PLAN+M7(路径 A+D1) | L1+L5 | C1b | L1b GATE | Q4 已确认 | ✓ **完成(commit c1981d47c;d1_prefix_cpa_witness 独立几何检查 + solve 状态覆写)** |
| **DP-07** prefix CPA row 处理 + CPA suffix-hard schedule(DP-01×DP-08 耦合解耦) | L1 | C1b | L1b GATE | Q4 + DP-08 k_head | ✓ **完成(commit ee6cf0cb0;三阶段 commit→soften→hard, k_cpa_suffix=max(k_minalt,k_head_earliest,k_tcpa))** |
| **direction row** reachability schedule(§4.4 k=0 soften 镜像) | L1 | C1b | L1b GATE | GNC Q7 | ✓ **完成(commit a16c14397;方向 wrong-side 检测 + k_head_earliest 含 k_dir)** |
| **Q4 σ conditional** IPOPT prefix CPA σ 吸收修复 + D1 witness(两路径同源) | L1(IPOPT) | C1b | L1b GATE | BL-15 + BUG-L1-01 | ✓ **完成(commit a39c9b978;compile_cpa_distance 加 prefix_K 参数,σ 只加 k>=prefix_K 行)** |
| **F-05** EXACT Hessian + R=0 + no-reg 数值(L3 单变量矩阵) | L3 | C2 | L3 GATE | L1+L2 GATE | ⬜ 待实施 |
| **F-01** status fail-closed(raw 0..7 映射) | L4 | C3 | L4 GATE | 独立(共享 con_h_expr) | ⬜ 待实施 |
| **4.1 h_fn rebuild** 同步(con_h 变更后 cache 重建) | L4 | C3 | L4 GATE | con_h_expr 变更 | ⬜ 待实施 |
| **5.3** BC→L4 链闭合(reactive_override_cmd 订阅) | L5 | C4 | L5 GATE | L4 GATE | ⬜ 待实施 |
| **5.4** FinalDegrade→MRM 执行证据 | L5 | C4 | L5 GATE | L4 GATE | ⬜ 待实施 |
| **5.4.b** Last-Safe-Maneuver Envelope Computer | L5 | C4 | L5 GATE | L4 GATE | ⬜ 待实施 |
| **X3** prefix pact_pre activation trace 补全 | LX | 贯穿(C1b 并行) | LX GATE | Q4 debug | ⬜ 待实施 |
| **X4** Failure Classifier 自动化(前置 C1) | LX | 贯穿(C1 并行) | LX GATE | L1 GATE 规模化 | ⬜ 待实施 |
| **LX 候选** continuous/swept CPA(SC-07:node-only row 漏区间穿越) | LX | 贯穿 | LX GATE | BL-11 调研 | ⬜ 待调研(Step2 Codex 新增) |

### 12.2 评审遗漏项(纳入对应层 GATE,非独立修复点)

| 遗漏项 | 归属层 | 纳入方式 | 来源 |
|---|---|---|---|
| ROS2 4 字段合约(stamp/schema_version/confidence/rationale) | L5.2 | L5 GATE 加"输出 msg 必须含 4 字段,rationale 引用 LX 分类索引" | 评审点 15 |
| encounter lifecycle 阶段切片(L2→M2→M6→M4→M5→L4→M7→M8) | L0 | L0 文档标注各字段对应 lifecycle 哪一步 | 评审点 15 |
| 扰动/不确定性(OU σ_pos)GATE 覆盖 | L1.4 | L1 GATE 加"UT σ_pos 在 hard row 变更后回归" | 评审点 15 |
| BL-B escalation:保守因子速度分段 | L1 | L1b 实施时 ship_maneuvering 一手源调研(NLM 失效,改一手源 [R27][R28]) | BL-B + Step2 VR-09 |
| BL-B escalation:node fallback 1.2 vs ODD 4.7 ROT 源 | L0/L1 | 独立 finding,记录到 §12.3 | BL-B |
| BL-10 terminal contract(NHN=0/NBXN=0) | L1a+L1b | L1a GATE 必须显式定义 terminal x_N 是否纳入 CPA/heading/speed/ROT 约束 | Step2 Codex [R24][R26] |
| BL-11 continuous/swept CPA(SC-07) | LX | LX 调研:node-only row 是否漏 15s 区间穿越;根因 oracle 已用连续线段 | Step2 Codex [R24] |
| BL-12 k_head + t_latest_safe ample-time 双量 | L1b | L1b k_head 公式必须含 ample-time 下界 `t_latest_safe`,不能只 reachability | Step2 Codex [R24][R27] |
| BL-13 grid physical-time map(IPOPT/acados off-by-one) | L1a 原则 + L1b 公式 | L1a 立原则:所有 schedule 先按物理秒定义;L1b 落公式 | Step2 Codex [R24] |
| BL-14 r0=0 是假设还是合法 contract | L1a+L1b | L1a GATE 调研:若无测量 yaw rate,需估计或不确定性边界 | Step2 Codex [R24] |
| BL-15 IPOPT σ 全局标量非 true hard(parity 重定义) | L1b(Q4 合并) | parity 框架修正:不把 IPOPT 当 oracle,两路径共对齐语义正确 OCP 规范 | Step2 Codex [R24][R26] |

### 12.3 bug 待确认表(实施前发现,待所属层讨论确认)

> **规则**:在非当前实施层发现的 bug 记录于此,不得跨层提前修。待该层开始讨论时,在此表标记"已确认",再纳入 §12.1 实施。

| bug ID | 发现时间 | 发现层 | 所属层 | 描述 | 证据 | 状态 |
|---|---|---|---|---|---|---|
| BUG-L1-01 | 2026-07-20(GNC 评审) | L4 评审 | L1(IPOPT) | IPOPT `apply_colreg_prefix_soften_` relax prefix CPA row 但 σ 全局标量仍加到这些 row,prefix 几何违反被 σ 吸收 | `row_registry.hpp:256-270` [R14] | **Step2 扩大**:Codex 发现 IPOPT 所有 CPA expression 都加 σ(`constraint_compiler.cpp:242-266`),不止 prefix 段,整个 IPOPT 路径非 true hard(BL-15 [R26])。待 L1b Q4 σ conditional 修复 |
| BUG-L1-02 | 2026-07-20(BL-B) | L1 调研 | L1(IPOPT+acados) | ROT-reach 公式 surrogate-derived,rot_max×dt 与 MMG oracle 差 5x(IPOPT 给 k=1,物理 k=3) | `mid_mpc_solver.cpp:462-502` [R20][R22] | 待 L1b 确认(C1b VR-03 b' 修复) |
| BUG-L1-03 | 2026-07-20(主 agent) | L1 调研 | L1(acados) | `gen:510 idxsh = arange(NT)+2` codegen 静态索引所有 CPA row(含 prefix 段),hard floor 可被 slack 软化 | `gen_mid_mpc_acados.py:510` [R18] | 待 L1a-spec-freeze 确认(hard-never-in-idxsh 原则修复) |
| BUG-L1-04 | 2026-07-20(Step2 ZCode) | L1 评审 | L1(acados) | acados graph **无 `kGIdxCpaHard` global slot**;`cpa_hard_m` 从未 pack 进 global 参数向量。nh=36 双 row 方案隐含必须新增,否则编译失败或 silently 用 cpa_safe 当 hard(回退 ALT-02) | grep kGIdxCpaHard 0 命中 [R23] | 待 L1a-spec-freeze 确认(kGIdxCpaHard slot 新增,BL-08) |
| BUG-L1-05 | 2026-07-20(Step2 ZCode+Codex) | L1 评审 | L1+L2(acados) | codegen box 是**字面量绑定**(`PSI_LB/UB=±π`、`ROT_MAX=0.2094`、`U_SURGE_MIN/MAX=[0,15]`),`kGIdxHeadingMin/Max` slot 被 pack 但 **graph 无任何表达式读**;wrapper set stage0 lbx/ubx 是 x0 pinning 不是 heading box。M4 heading box 被 pack 后完全忽略 | `gen:481-486`;`formulation.cpp:605-606` [R23][R24] | 待 L1a-spec-freeze 确认(DP-02 box live 落地) |
| BUG-L1-06 | 2026-07-20(Step2 Codex) | L1 评审 | L1+L2(acados) | generated solver `NHN=0/NBXN=0`,terminal stage x_N 完全无 hard constraint;DP-01/02/08 在最后 dynamics step 同时失效,"全 horizon hard floor" 安全 claim 不完整 | `acados_solver_m5_mid_mpc_acados.h:44-70` [R26] | 待 L1a-spec-freeze 确认(DP-02 terminal contract,BL-10) |
| BUG-L1-07 | 2026-07-20(Step2 Codex) | L1 评审 | L1(DP-08) | IPOPT/acados grid off-by-one:IPOPT `psi[0]` 是首个可控 predicted heading,acados stage0 是 measured x0,stage1 才是 future state。复制同一整数 k_head → 两路径首次 hard 物理时刻差一 stage | `mid_mpc_solver.cpp:240-258` vs `mid_mpc_acados_solver.cpp` [R24] | 待 L1b 确认(grid physical-time map 原则,BL-13) |
| BUG-L1-08 | 2026-07-20(Step2 Codex) | L1 评审 | L1+LX | continuous/swept CPA 漏检:15s 网格内节点 CPA 都 ≥1852 但区间穿越 <1852;根因 oracle 已用连续线段 CPA | root-cause report `:32-40` [R24] | 待 LX 调研确认(BL-11) |
| BUG-L0-01 | 2026-07-20(M4 调研) | L0 调研 | L0 | `BehaviorPlan.msg:25` 注释漂移(未同步 v2.2 方向感知语义,还是旧标量 ≥0) | `BehaviorPlan.msg:25` [R16] | 已确认 ✓ L0-B 实施时修复(commit 6a0c12f3b) |
| BUG-L0-02 | 2026-07-20(BL-B escalation) | L1 调研 | L0/L1 | node fallback `cruise_max_yaw_rate_deg_s=1.2` vs live ODD 4.7,GNC ODD 到达前后 reachability schedule 跳变 | `mid_mpc_node.cpp:2072` vs live [R20] | 待 L0/L1 讨论确认归属 |
| BUG-评审-cpa_rows_relaxed | 2026-07-20(架构评审) | L3 评审 | L1/L3 | `cpa_rows_relaxed` arm 全 raw4(ablation_matrix.csv:13-15),暗示 F-03/F-04 可能比 F-02 更关键,未单独分析 | `ablation_matrix.csv:13-15` [R17] | 待 L1/L3 讨论确认 |
| BUG-BUILD-01 | 2026-07-20(L0 subagent) | L0 实施 | 构建/L1 | `m5_mid_mpc_node` 编译失败:`mid_mpc_node.hpp:18` include `bc_mpc_health.hpp` 在 subagent 构建环境未生成 msg header(pre-existing,HEAD 4fd37fd7e 就有)。`BcMpcHealth.msg` 源文件存在,是构建顺序/环境问题,非源码缺失 | `mid_mpc_node.hpp:18`;`l3_msgs/msg/BcMpcHealth.msg` 存在 | 待 L1a-spec-freeze 实施时在正确构建环境验证 |

---

## 附录 A:4 大责任域正交切分(辅助归因)

与 L0-L5 正交的责任域,用于跨层归因:

| 责任域 | 回答 | 覆盖模块 |
|---|---|---|
| A 业务语义域 | 当前什么规则?右转还是左转?何时规避/回归? | L0.2/0.3, L1.5/1.6, L4.3 |
| B 数学建模域 | 状态/控制是什么?时域/目标/约束怎么设? | L1 全部, L2.3/2.4 |
| C 数值求解域 | 为什么 QP 死?Hessian 病态?step 太小?配置合适? | L2.5, L3 全部, L4.1 |
| D 工程输出域 | 解真的可执行?如何下发 L4?无解怎么办? | L4.4/4.5, L5 全部 |

---

## 附录 B:证据溯源

| 引用 | 来源 |
|---|---|
| [R1] | `types.hpp:120-169` ConstraintInputs 字段定义 + Bug C deep RC-C 注释 + v2.2 §4.6 reachability 合约 |
| [R2] | `mid_mpc_solver.cpp:600-606` IPOPT 路径消费 cpa_hard_m |
| [R3] | `mid_mpc_acados_formulation.cpp:333,340,374` + `gen_mid_mpc_acados.py:315,323` acados graph 只用 cpa_safe |
| [R4] | `mid_mpc_solver.cpp:462-502` IPOPT reachability schedule |
| [R5] | `mid_mpc_node.cpp:557-564,664` M4 BehaviorPlan → MidMpcInput pack |
| [R6] | `types.hpp:163-169` v2.2 §4.6 reachability 合约字段注释 |
| [R7] | `mid_mpc_acados_solver.cpp:99-111,1114-1128` status 4 fail-open 重映射 |
| [R8] | `mid_mpc_acados_solver.cpp:984-1009` wrapper stage0 lbx/ubx + 静态 box |
| [R9] | `gen_mid_mpc_acados.py:331-335` min-alt 每 stage 激活 |
| [R10] | `runs/m5_solver_diag/4fd37fd7e.../fresh_production_config/*/verdict.json` 三 case 四分类 |
| [R11] | `docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md` §6 MMG witness |
| [R12] | NLM colav_algorithms domain(高置信):OCP formulation 离线独立设计步骤;离散化/condensing 独立可验证;SQP/QP 强耦合;COLREGs hard/soft 分层;fallback 链 |
| [R13] | GNC 独立评审(agent_03ab040d,PASS_WITH_FINDINGS):IPOPT 7 项适合镜像;Q4 prefix-CPA fail-open 需两路径共修 |
| [R14] | `row_registry.hpp:256-270` apply_colreg_prefix_soften_ + σ 全局标量 |
| [R15] | `mid_mpc_solver.cpp:462-502` box-reach bimodal + epsilon 0.005 rad;Q3 box-reach 方向隐性合约 |

---

## 变更记录

| 日期 | 变更 | 来源 |
|---|---|---|
| 2026-07-20 | 初版架构骨架(L0-L5 + LX),融合 4 层 + ChatGPT 6+LX + NLM + GNC 评审 | 决策日志 Step1.6 |
