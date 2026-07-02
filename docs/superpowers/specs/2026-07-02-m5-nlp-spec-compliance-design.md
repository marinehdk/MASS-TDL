# M5 NLP Spec-Compliance Design — 完整 NLP + TailBuilder + Continuity

- Date: 2026-07-02
- Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- Branch: `codex/colregs-12probe-debug`
- Status: Draft v2（Codex 对抗评审后修订，待二次评审）
- Supersedes: `2026-06-30-m5-committed-route-design-v2.md` §9.3/§9.5 NLP 内部条款 + §9.7 TailBuilder active-phase 语义 + §9.12 H_commit prefix 契约（本 spec 修订这三处的实现依据；committed-route spec v2 的 publish heartbeat / M6 msg / M7 policing 外部契约保留不变）
- 关联: `M5-jcolreg-redesign-spec.md`（J_colreg/J_asym 来历，本 spec 不改其公式）

## Revision History

| 版本 | 日期 | 变更 |
| --- | --- | --- |
| v1 | 2026-07-02 | 初版：基于 Codex NLP 完备性评审 + nlm 🟢 continuity 调研，定义完整 NLP（route-frame + terminal + Rule13 + continuity）+ TailBuilder 接线 + 前置 bug 修复 |
| v2 | 2026-07-02 | Codex 对抗评审后修订 6 项 Critical/High：(1) continuity 冻结对象改为 H_commit/GNC guard + committed 几何（非 H_publish/psi-u）；(2) TailBuilder active-phase terminal hold 到预测 s_clear（非 release 后）；(3) J_route/J_terminal dimensionless 化 + 去 nonsmooth max + COLREG dominance test；(4) one-time graph 声明改为"承认继续 rebuild 或全参数化二选一"；(5) §9.3 措辞降级为 partial coverage（risk/covariance/ship-domain 明确标注后续）；(6) M7 兜底改为前置依赖声明（未就绪前 prefix CPA 软化不可用，用 Keep-Last + fallback 兜底）。另修 kParamDim 计数、direction/min_alt 在 preferred_direction=0/HOLD/ReduceSpeed 时禁用规则。 |

## 0. Scope

本 spec 是 **M5 Mid-MPC NLP 内部** 的完整 spec-compliance 设计，使其从"带部分 CPA/heading rows 的 psi/u 候选生成器"升级为 **spec §9.3 完整的 normal 航线 NLP**，并能产出 COLREGs 合规 + 时序连续 + GNC 可执行的稳定轨迹。

**本 spec 覆盖**：
- NLP 决策变量、cost、约束、parameter 的完整定义（§3）
- route-frame 引入与 route-return pressure（§4）
- terminal tail-extension 约束 + TailBuilder 接线（§5）
- continuity 机制（A1 prefix-equality，§6）
- Rule13 实现 + COLREG direction/min_alt 内化（§7）
- CPA 冲突处理（prefix 段软化，§6.4）
- 前置 bug 修复（§8）

**本 spec 不覆盖**（保留在 committed-route spec v2）：
- publish 策略 / heartbeat / GNC preflight / DegradedHold（§6/§9.10/§9.12 不变）
- M6 msg 扩字段契约（§14 不变）
- M7 policing（§13 不变）
- 非凸论证（§13 不变）

**设计基线**（Approach 1，已与用户确认）：决策变量保持 `psi/u`，route-frame 作 parameter 引入 cost/约束；Approach 2（`l` 升为决策变量）列为备选升级路径。

## 1. Problem Statement

### 1.1 当前 NLP 不完备（Codex 评审证据）

当前 M5 Mid-MPC NLP 对照 spec §9.3 "NLP 须含" 7 项，判定如下（证据见 Codex 评审报告）：

| §9.3 要求 | 判定 | 主要 gap |
| --- | --- | --- |
| 每目标 CPA/risk clearance 全时域 | ⚠️ | CPA hard rows 有；risk/covariance/ship-domain 不进 NLP |
| COLREG direction + min alteration | ⚠️ | M6 direction/min_alt 只用于 fallback/tail gate，不进 NLP |
| Rule 13/14/15/16/17 role-specific | ⚠️ | 14/15/16/17 有；**Rule13 空 sentinel** |
| heading/speed bounds | ✅ | lbx/ubx 生效 |
| ROT/yaw 约束 | ⚠️ | ROT 有；GNC yaw/lateral-accel/decel 不进 NLP |
| **route-return pressure** | 🔴 | 只有 J_dist 拉回单一 bearing，无 route-frame cross-track |
| **terminal tail-extension** | 🔴 | NLP 无 terminal rows/cost；normal path 无 TailBuilder |

### 1.2 continuity 结构性缺陷（已 pinned）

NLP 每 cycle 贪婪重解 90s 时域，`psi[0]` 自由决策变量只受 heading box + intra-horizon ROT 约束，warm-start 续接上一 cycle 解 → 累积过转 → 极限环（steering_reversals 1660、int_abs_xte 1.59M、port/starboard 180° 翻转）。handoff 2026-07-02 (cont.) pinned。

### 1.3 TailBuilder + CommittedAvoidanceRoute 纸面落地

Slice C/D 实现了独立类 + 单测，但**未接入 on_solve_cycle_ 主流程**：
- TailBuilder：`mid_mpc_waypoint_generator.cpp` 完全不调它；normal path 只转 NLP waypoints + 直接 append L2 suffix
- CommittedAvoidanceRoute：只在 publish 路径（node.cpp:927）做 gate，未影响 NLP solve

### 1.4 前置 bug（独立于 redesign，须先修）

- **Zone 积分方向反**（constraint_compiler.cpp:447-448）：`cum_x += u·dt·sin(psi)` 应为 `cos(psi)`（NED x=north），与 CPA 积分（:305-306）不一致
- **Risk weight 死代码**（mid_mpc_node.cpp:403-410）：改 `tgt.cpa_m/tcpa_s` 但 NLP pack 不读这两字段（J_colreg redesign 后遗留）

## 2. Design Goal

使 M5 Mid-MPC NLP 满足以下可验证条件：

1. **spec §9.3 主要项覆盖**（NLP 内部）：CPA hard clearance、COLREG direction+min_alt、Rule13-17、heading/speed、ROT、route-return、terminal tail-extension。**明确降级项**：risk covariance/ship-domain/BCT/no-crossing-ahead 在本 spec 标注为后续工作（§3.5），不声称 §9.3 full coverage。
2. **continuity**：NLP 续接 committed route prefix（按 H_commit/GNC guard 几何冻结，非 H_publish/psi-u），消除贪婪重解导致的极限环。
3. **TailBuilder active-phase 接线**：normal path 从 NLP terminal state 经 TailBuilder 生成 hold（到预测 s_clear）+ rejoin + nominal suffix 完整航线；active 阶段也生成（非 release 后才生成）。
4. **时序连续 + GNC 可执行**：发布几何时序连续（committed 几何 prefix 冻结 + heartbeat 对齐），过 GNC preflight。
5. **不改 J_colreg/J_asym 公式**（J_colreg redesign spec 已固化）；新增 cost 项 dimensionless 归一化 + COLREG dominance test 保证不压制避让。
6. **不调权重/阈值压 probe**（CLAUDE.md 禁）；新 cost 项权重带文献/工程依据 + [TBD-HAZID] 标注。
7. **M7 兜底诚实声明**：prefix CPA 软化的安全论证依赖 Keep-Last §9.12 + fallback（已实现），**不**依赖 M7 policing（Slice K 未完成）；M7 就绪后升级安全论证。

## 3. NLP 完整定义

### 3.1 决策变量（不变）

`x = [psi(0..N-1); u(0..N-1)]`，`N=18, dt=5s, H_pred=90s`。
位置 `pos[k] = (x_m[k], y_m[k])` 由 psi/u 积分（NED: psi=0→north, x=north, y=east）：
```
x_m[k] = x0 + Σ_{j=0}^{k-1} u[j]·dt·cos(psi[j])
y_m[k] = y0 + Σ_{j=0}^{k-1} u[j]·dt·sin(psi[j])
```
**不引入 `l/s` 为决策变量**（Approach 1；Approach 2 备选升级，见 §11）。

### 3.2 Cost 全集（J_colreg/J_asym 不变 + 新增 dimensionless 2 项）

```
J = w_colreg·J_colreg + w_dist·J_dist + w_vel·J_vel + J_asym
  + w_route·J_route          (新增 dimensionless, §4.3)
  + w_terminal·J_terminal    (新增 smooth-only, §5.3)
```

| 项 | 公式 | 权重 | 作用 | 状态 |
| --- | --- | --- | --- | --- |
| J_colreg | `avg Σ tw·disc·exp(-ζ(d-cpa_safe))` | w_colreg | CPA soft barrier | 不变（J_colreg spec）|
| J_dist | `Σ(psi[k]-route_bearing)²` | w_dist | heading 拉回 | 不变（保留，与 J_route 互补）|
| J_vel | `Σ(u[k]-planned_speed)²` | w_vel | speed 跟踪 | 不变 |
| J_asym | `give_way·k_asym·Σ softplus(bearing-psi[k])` | k_asym | Rule14/15 右转 | 不变（J_colreg spec）|
| **J_route** | `Σ (l[k]/l_scale)²` + `λ·(l[N-1]/l_scale)²`（dimensionless）| w_route [TBD-HAZID] | route-frame cross-track 回归 | 新增 §4.3 |
| **J_terminal** | smooth softplus only（见 §5.3，**无 max/abs**）| w_terminal [TBD-HAZID] | terminal state 可延伸性 | 新增 §5.3 |

**Dimensionless 归一化（Codex 评审 Critical 修复）**：`l_scale` = GNC feasible lateral limit（`GncExecutionOdd.max_lateral_offset_m`，现 400m）。`l[k]/l_scale ∈ [-1,1]` 使 J_route 无量纲且 O(1)，与已平均化的 J_colreg（`avg` 后 O(1)）同量级。**禁止用 m² 原始尺度**（100m XTE 时 Σl²≈180k 会压制 w_colreg≈30 的避让 cost）。

**COLREG dominance 契约（必须验证，非自证）**：
- 单测：构造 target@cpa_hard 附近 fixture，验证 `w_colreg·J_colreg > w_route·J_route + w_dist·J_dist`（避让 cost 主导）
- 集成测：rule14-ho 探针 CPA≥cpa_hard 全程（route cost 不把船拉进 target）
- 若 dominance 不成立，降 w_route 而非升 w_colreg（不破坏 J_colreg spec 权重平衡）

**权重依据**：w_colreg/w_dist/w_vel/k_asym 已在 J_colreg spec §6 固化（nlm 🟢）。w_route/w_terminal 用文献经验值（route 跟踪权重 ≈ w_dist 量级；terminal 引导 < J_colreg）+ [TBD-HAZID]，HAZID RUN-001 校准。**禁止用权重压 probe 绿**（CLAUDE.md）。

### 3.3 约束全集（g ≥ 0 不等式 + prefix equality）

| 约束类 | 形式 | 数量 | 类型 | 状态 |
| --- | --- | --- | --- | --- |
| heading box | `lbx/ubx` per-var | N | 变量界 | 不变 |
| speed box | `lbx/ubx` per-var | N | 变量界 | 不变 |
| ROT | `rot_step±dpsi ≥ 0` | 2(N-1) | 平滑线性 g | 不变 |
| CPA hard floor | `dx²+dy²-cpa_hard² ≥ 0` per(target,step) | Nt·N | 硬 g（**suffix 段 active；prefix 段软化 §6.4**）| 不变（RC-C）|
| Rule14/15/16/17 | heading rows（constraint_compiler）| 规则依赖 | 硬 g | 不变 |
| **Rule13** | overtake side（§7.2）| 规则依赖 | 硬 g | 新增 §7 |
| **COLREG direction** | `preferred_direction·l[k] ≥ 0`（§7.1，**仅 suffix 段 k≥K；preferred_direction≠0 且 give-way 时 active**）| N | 硬 g（条件激活）| 新增 §7.1 |
| **min alteration** | `direction·(psi[k]-own_psi) ≥ min_alt`（§7.1，**同 direction 激活条件**）| N | 硬 g（条件激活）| 新增 §7.1 |
| **terminal side** | `direction·l[N-1] ≥ l_min_feasible`（§5.4）| 1 | 硬 g | 新增 §5.4 |
| **terminal lateral bound** | `l[N-1]+l_max≥0 ∧ l_max-l[N-1]≥0`（§5.4，两线性替代 abs）| 2 | 硬 g | 新增 §5.4 |
| **prefix equality** | `psi[k]-prefix_psi[k] = 0`（§6，**committed 几何映射，非裸 psi-u**）| N | 等式 g（lbg=ubg=0，动态 K）| 新增 §6 |
| **prefix u equality** | `u[k]-prefix_u[k] = 0`（§6）| N | 等式 g | 新增 §6 |
| zone | polygon containment | zone·N | 硬 g | **修 bug §8** |

**direction/min_alt 激活规则（Codex 评审修复）**：当 `preferred_direction==0`（M6 无方向偏好）、`primary_role==STAND_ON`、或 M4 behavior∈{HOLD, REDUCE_SPEED}（非机动行为）时，direction/min_alt 行**禁用**（lbg=-inf, ubg=+inf）。否则 `min_alt>0 && direction==0` 直接 infeasible。

**COLREG hard rows 分段激活（Codex 评审 Critical 修复）**：所有 COLREG 相关 hard rows（CPA/direction/min_alt/terminal）在 prefix 段（k<K）软化（lbg=-inf），只 suffix 段（k≥K）保留 hard floor。理由：prefix 几何钉死后 target 移动使 prefix 段违反新 CPA/direction，NLP 不可修。安全靠 Keep-Last §9.12 + fallback（§6.4）。

### 3.4 Parameter 扩展（kParamDim 94 → 139）

新增 parameter slot（kParamDim 扩展，编译时固定）：
- `kIdxRouteFrameOriginX`, `kIdxRouteFrameOriginY`：route-frame 原点 NED（2 slot）
- `kIdxRouteFrameNormalX`, `kIdxRouteFrameNormalY`：当前 active leg 法向单位向量分量（2 slot，pre-computed = bearing+90°）
- `kIdxRouteFrameActiveLegBearing`：当前 active leg bearing（1 slot）
- `kIdxLateralScale`：l_scale 归一化尺度（1 slot，= GncExecutionOdd.max_lateral_offset_m）
- `kIdxPrefixPsi[N]`, `kIdxPrefixU[N]`：committed prefix 的 psi/u 值（2·18=36 slot）
- `kIdxPrefixActiveK`：当前 active prefix 长度 K（1 slot）
- `kIdxPreferredDir`：M6 preferred_direction（1 slot）
- `kIdxMinAlterationRad`：M6 minimum alteration（1 slot）
- `kIdxRole`：M6 primary_role enum（1 slot）

**kParamDim 重算**：94（现）+ 9 head slots + 36 prefix = **139**。更新 `static_assert(kParamDim==139)`。

### 3.5 明确降级项（Codex 评审要求诚实声明）

本 spec **不覆盖**以下 §9.3 子项（标注后续工作，不声称 full compliance）：
- **risk covariance**：M2 `cpa_covariance_m2`（TrackedTarget.msg:19-20）存在，但 NLP param stride 只读 x/y/cog/sog/tw。tail-gate（acceptance）用 3σ 膨胀，NLP 内未用。**后续**：NLP param stride 加 covariance slot，CPA hard floor 用 cpa-3σ。
- **ship-domain / BCT**：kernel 无实现（committed-route spec §2.3 已记录缺口）。**后续**：进 M2 geometry 新建，NLP 消费。
- **no-crossing-ahead**：tail-gate（acceptance）检查，NLP 内无约束。**后续**：NLP 加 terminal/全时域 no-crossing-ahead 几何约束（依赖 ship-domain）。
- **GNC yaw/lateral-accel/decel**：tail-gate/preflight 检查，NLP 内无显式约束行。**后续**：评估是否需进 NLP（可能 tail-gate reactive 足够）。

这些降级项**不阻塞本 spec 的 continuity + route-return + terminal tail-extension 主目标**，但意味着 NLP 仍非 §9.3 完整实现。验收标准（§10）据此调整。

### 3.6 Graph Rebuild 策略（Codex 评审 Critical 修复——二选一，删虚假声明）

当前 `on_solve_cycle_` 每 cycle 调 `build_symbolic_graph()`（node.cpp:475），target/rules/zones numeric-baked（formulation.cpp:204-212 注释）。v1 spec 声称"不每 cycle rebuild"与现状矛盾。

**二选一策略**（plan 阶段决策，本 spec 只声明选型约束）：
- **选项 G1（保守，推荐首版）**：承认继续每 cycle rebuild。新增 cost/constraint 一并 rebuild。不改变现有 rebuild 模式，风险最低。代价：2s CPU cap 压力随约束数增长（需验证）。
- **选项 G2（激进，性能优化）**：全约束参数化——target/rules/zones 从 numeric-baked 改为 fixed-max-rows + dynamic lbg/ubg 激活。一次建图。代价：formulation 大改，所有 compile_* 方法重写，风险高。

**推荐 G1**：continuity（§6）的 prefix-equality 本就通过 lbg/ubg 动态激活（不依赖 G2），且 J_colreg spec §6 决策 4 已接受 rebuild 模式。G2 留作性能优化独立 D-task（若 CPU cap 成瓶颈）。

## 4. Route-Frame 与 Route-Return Pressure

### 4.1 Route-Frame 构造

从 L2 完整 `PlannedRoute` polyline 构造 route-frame（node 层 assemble_input_ 预算，pack 进 parameter）：
- 投影原点 `(rx0, ry0)` = polyline 首点（NED）
- 当前 active leg index `leg0`（本船所在段）
- 每段 bearing `bearing[leg]`（polyline 段方位）
- 段长 `length[leg]`

当前 `assemble_input_`（node.cpp:418-438）只算首段 bearing + 单点 XTE。**扩展为完整 polyline 投影**：扫所有段找最近 leg + 沿 leg 的 station `s0` + 法向 cross-track `l0`。

### 4.2 Cross-Track l[k] 符号计算

NLP 内 `l[k]`（本船在 step k 的 route-frame cross-track）由积分位置 `pos[k]` 投影到 route-frame 算：
```
l[k] = (pos[k] - route_origin) · n_hat[leg(k)]
```
`n_hat[leg]` = leg 法向单位向量（parameter，bearing[leg]+90°）。`leg(k)` 由 `s[k]`（沿轨距离）查表得，但为保持平滑性，**首版用当前 active leg 的法向**（不随 k 切换 leg，避免 leg 边界的非光滑切换）。

**平滑性论证**：n_hat 是 parameter（常量），l[k] 是 pos[k] 的线性函数（pos[k] 是 psi/u 积分 = 非线性但平滑）。无新非光滑源。首版单 leg 近似在单次 90s 避让机动内足够（避让段不跨越多个 L2 leg；若跨越，route-frame 切换在 pack 时用当前 leg）。

### 4.3 J_route（新增 cost，dimensionless）

```
J_route = Σ_{k=0}^{N-1} (l[k]/l_scale)²  +  λ_terminal · (l[N-1]/l_scale)²
```
- `l_scale` = GNC feasible lateral limit（`GncExecutionOdd.max_lateral_offset_m`=400m），pack 进 `kIdxLateralScale`
- 第一项：全时域 dimensionless cross-track 罚
- 第二项：terminal cross-track 加强（`λ_terminal > 1`）
- **dimensionless**：`l/l_scale ∈ [-1,1]`，J_route 项 O(1)，与 J_colreg（avg 后 O(1)）同量级

**与 J_dist 的关系**：J_dist 拉回 heading bearing（方向），J_route 拉回 cross-track（位置）。两者互补。J_dist 保留不变。

**单 leg 近似风险缓解（Codex 评审 High 修复）**：90s horizon at 10m/s = 900m，若跨越 L2 route corner，single-leg 法向投影失真。缓解：assemble_input_ 算 active leg 时，**若 NLP 预测轨迹（用 own_psi 直线外推 900m）跨越多个 L2 leg，则 pack 时标记 route-frame 不可靠 → J_route 权重临时降为 0（只保留 J_dist）**，避免拉错方向。这是一个 runtime guard，不重建图。

## 5. Terminal Tail-Extension 约束 + TailBuilder Active-Phase 接线

### 5.1 设计原理

spec §9.5 tail-gate 当前是 publish 前的 acceptance 检查（reactive reject）。本 spec 把 tail-extension 要求**前移到 NLP 内部**（proactive guide）+ **TailBuilder active-phase 生成**（Codex 评审 Critical 修复）。

nlm 🟢 High（technique 3）：immutable prefix + **deterministic Terminal Set**（Maciejowski; Bemporad & Morari）。TailBuilder 几何 tail 即 Terminal Set。

### 5.2 TailBuilder Active-Phase 语义（Codex 评审 Critical 修复）

**当前 bug**：TailBuilder 只在 `m6_past_clear || encounter_state∈{RELEASE,CLEAR}` 后才 build（tail_builder.cpp:129-134,339-342 `m6_reports_clear` gate）。active encounter 期间 reject `m6_not_past_clear` → normal route 无法生成 tail → 卡死。

**spec §9.7 真实要求**：TailBuilder 在 **active 阶段就生成** hold 段（从 NLP terminal state 延伸到**预测的** s_clear），不是 release 后才开始。`past_clear` 只决定 rejoin 起点，不决定是否生成 tail。

**修复**：TailBuilder 的 `m6_reports_clear` gate 改为**两阶段语义**：
- **active 阶段**（`encounter_state==ACTIVE`，`!past_clear`）：生成 MID_MPC_TERMINAL_HOLD 段（constant-offset hold 到**预测 s_clear**），**不生成 REJOIN_TO_L2**（rejoin 待 release）。`s_clear` 预测：用 M2 target 速度外推 + `release_predicted` 信号（M6 Slice G msg）。
- **release 阶段**（`encounter_state==RELEASE` 或 `past_clear`）：在已生成 hold 基础上 append REJOIN_TO_L2 段（曲率受限回归 nominal）。

这使 normal route 在整个 encounter 生命周期（active→release→return）都有完整 tail，不被 `past_clear` 卡死。

**`release_predicted` 消费（Slice G msg 字段）**：active 阶段预测 s_clear 需 M6 `release_predicted`（commit-route spec §14.1）。Slice G 须就绪。若未就绪，TailBuilder active 阶段用保守 s_clear（hold 到 horizon 尽头），release 阶段正常。

### 5.3 TailBuilder 接线（normal path）

**当前**：`mid_mpc_waypoint_generator.cpp` 只转 NLP waypoints + 直接 append L2 suffix，不调 TailBuilder。

**改后**：normal path（node.cpp `on_solve_cycle_`）流程：
```
NLP solution (psi/u trajectory, terminal state pN/psiN/uN)
  ↓
[1] NLP waypoints (MID_MPC_OPTIMIZED, 密集采样)          ← 现有 wp_gen
  ↓
[2] TailBuilder.build(TailInputs{                         ← 新增接线
       pN, psiN, uN,                                      ← NLP terminal
       protected_side = M6 preferred_direction,
       m6_past_clear, m6_encounter_state, m6_release_predicted,  ← Slice G msg
       route_frame, targets, cpa_*, gnc_odd
     })   ← active 阶段生成 hold-only; release 阶段 hold+rejoin (§5.2)
  ↓ if TailResult.hold_then_rejoin has value:
[3] append MID_MPC_TERMINAL_HOLD [+ REJOIN_TO_L2 if release] waypoints
  ↓
[4] append L2_NOMINAL_SUFFIX (rejoin station 下游，或 hold 末端 if active)  ← 现有
  ↓
[5] GNC preflight complete candidate                     ← Slice F
  ↓
[6] CommittedAvoidanceRoute.try_revise + publish         ← Slice D + A（§6.6 改 prefix 数据源）
```

**TailBuilder reject 处理**：若 `TailResult.reject_reason` 非空（terminal state 不可延伸），候选标 `nlp_tail_gate_failed`，走 fallback/DegradedHold。§5.4/§5.5 的 NLP 内 terminal 约束应使这种情况减少。

### 5.4 J_terminal（新增 cost，smooth-only，无 max/abs）

Codex 评审 Critical 修复：删除 v1 的 `(max(0, |l|-lmax))²`（max 非光滑，J_colreg redesign 消除的 Restoration_Failed 源）。terminal lateral bound 已由 §5.5 硬约束覆盖，cost 只需 smooth side 引导：

```
J_terminal = give_way · τ_t · softplus((l_wrong_side)/τ_t)
```
- `l_wrong_side = -preferred_direction · (l[N-1]/l_scale)`（>0 当 terminal 在错误侧）
- softplus 平滑（C∞），与 J_asym 同族
- gate by `give_way`（stand-on 无 terminal cost）
- lateral 上界靠 §5.5 硬约束（非 cost），不重复

### 5.5 Terminal 硬约束（给-way 角色，仅 suffix 末步 k=N-1）

给-way 角色下，terminal state 须满足（硬约束 g 行）：
```
g_term_side:    preferred_direction · l[N-1] ≥ l_min_feasible       // 同侧, l_min_feasible > 0
g_term_lo:      l[N-1] + l_max_feasible ≥ 0                          // lateral 下界 (两线性替代 abs)
g_term_hi:      l_max_feasible - l[N-1] ≥ 0                          // lateral 上界
```
- `l_min_feasible` = COLREG apparent action 最小偏置（防 tail 过小无效）
- `l_max_feasible` = min(GncExecutionOdd.max_lateral_offset_m, TailBuilder rejoin 几何上界)
- **不用 `|l[N-1]|`**：abs 非光滑，改为两线性约束（对齐 J_colreg spec §2 原则）
- **激活条件**：同 §3.3 direction/min_alt（give-way + preferred_direction≠0 + 非 HOLD/ReduceSpeed）

**stand-on 角色**：无 terminal 约束，完整航线 = L2 nominal（Rule 17 keep heading）。

### 5.6 与 tail-gate（acceptance）的关系

NLP 内 terminal 约束（§5.4/§5.5）是 **proactive 引导**；acceptance 层 tail-gate（types.hpp `accept_tail_gate`）是 **reactive 验证**，保留不变。defense in depth。

## 6. Continuity 机制（H_commit Committed-Geometry Prefix）

### 6.1 设计原理（nlm 🟢 High technique 3 + Codex 评审 Critical 重写）

每 cycle NLP **续接 committed route prefix** 而非贪婪重解。prefix = committed-route spec §0 的 **H_commit**（GNC guard 距离内 + 已越过航点），**不是** H_publish（60s heartbeat）。

**v1 错误（Codex 评审 Critical 修复）**：
- v1 把 prefix 冻结到 H_publish=60s（K≥12），且 K_max=12 → suffix 只剩 30s，避让空间不足
- v1 冻结 psi/u（决策变量），但 NLP 每 cycle 从 ownship origin 重积分，psi/u 相同起点不同 → WGS84 几何不同，发布几何不连续
- v1 声称"不改 manager"，但 manager 当前 `frozen_prefix_count=0` 固定（node.cpp:111），无 prefix 数据源

**v2 修正**：
- K 由 **GNC guard 距离**（`GncExecutionOdd.min_first_changed_distance_m`=100m）决定，不由 heartbeat。100m at 5m/s = 20s = 4 步 → K≈4（远小于 12，suffix 留 70s = 14 步，避让空间充足）
- 冻结对象是 **committed route 的 WGS84 几何/station**（manager 持有），映射成 NLP 约束时按 ownship current origin 重投影
- 需改 manager：`frozen_prefix_count` 从发布几何的 GNC-guard 内点数算（§6.6）

### 6.2 Prefix-Equality 实现（committed 几何映射）

**冻结对象**：manager 的 `committed_prefix`（WGS84 几何，GNC guard 内点）。

**映射到 NLP**：每 cycle assemble_input_ 时：
1. 取 manager `committed_prefix`（WGS84 lat/lon 列表）
2. 转换到 **当前 cycle ownship NED 原点**（每 cycle origin 变，重投影）
3. 对 prefix 内每个航点，反推其 psi/u（从相邻点距离/方位 + dt 反解）
4. pack 进 `kIdxPrefixPsi/KIdxPrefixU`（前 K 个 NLP step 对应值）
5. lbg/ubg 激活前 K 行 equality（`psi[k]=prefix_psi[k]`, `u[k]=prefix_u[k]`）

**为何不直接冻结 psi/u**：psi/u 是 ownship-relative 控制量，origin 变则几何变。冻结 WGS84 几何 + 每 cycle 重投影到 current origin，才能保证发布几何真的连续。

### 6.3 动态 K 策略（H_commit = GNC guard）

```
K = ceil(GncExecutionOdd.min_first_changed_distance_m / (own_u · dt_s))
```
- 100m at 5m/s, dt=5s → K = ceil(100/25) = 4
- K 随 own_u 变化（低速 K 增大，但 dt_s 步内距离小）
- K_max = N - K_suffix_min，K_suffix_min = 8（40s suffix，充足避让空间）→ K_max = 10
- 首次 commit: K=0（全 suffix 自由）

**与 H_publish 的关系**：K≈4 步 = 20s << H_publish=60s。heartbeat 内本船执行掉约 12 步 prefix，但只有前 4 步在 GNC guard 内（不可变），后 8 步可被新 revision 替换（GNC 接受，因 first-changed-distance 足够远）。这符合 spec §9.12 rolling：append/replace 本船前方足够远的未来几何。

### 6.4 CPA 冲突处理（prefix 段软化，安全网诚实声明）

**问题**：prefix 段几何钉死后，目标船移动使 prefix 段违反新 CPA hard floor → NLP 不可行。

**处理**：prefix 段（k<K）的所有 COLREG hard rows（CPA/direction/min_alt）lbg 设 -inf（软化），只 suffix 段（k≥K）保留 hard floor。

**安全网（Codex 评审 Critical 修复——诚实声明，不引用未就绪的 M7）**：
- v1 错误引用 M7 policing 作安全网，但 M7 runtime coverage 🔴（Slice K 未完成）
- **v2 安全论证**：
  - prefix 几何是 GNC 正在执行的 committed route（已过 GNC preflight + 发布前 tail-gate）
  - CPA 穿透时，**Keep-Last §9.12 风险门控**（已实现：current_cpa < cpa_hard → DegradedHold）触发，hold 最后 valid route
  - 若 DegradedHold 不够（CPA 持续恶化），**geometric fallback**（现有 on_solve_cycle_ :524-536）接管——fallback 从 own_psi 重算，不依赖 prefix
  - **M7 就绪后（Slice K 完成）升级**：NLP-internal-fail → M7 veto → MRM 链补强，但本 spec 不依赖它

### 6.5 Warm-Start 调整

`pack_warm_start_`（solver.cpp:41）当前用裸上一解。改为：
- prefix 段（k<K）: x0 = prefix_psi/prefix_u（§6.2 重投影值，被 equality 钉死）
- suffix 段（k≥K）: x0 = cold-start seed（own_psi/own_u）而非上一解

**为何 suffix 不用上一解**：prefix equality 已保证 prefix 连续；suffix 用 cold-start（own_psi）避免 warm-start 续接累积漂移（v1 根因）。suffix 的稳定性靠 prefix 锚定 + J_route/J_dist 拉回，不靠 warm-start 续接。

cold-start（首次 K=0）: 不变（pack_cold_start_ 用 own_psi/own_u）。

### 6.6 Manager Prefix 数据源（Codex 评审 Critical 修复——承认改 manager）

**当前**：`committed_candidate_from_plan`（node.cpp:85-126）固定 `frozen_prefix_count=0U`。manager `try_revise` 虽接收 `frozen_prefix_count` 但永远是 0，`committed_prefix` 始终空。

**修复**：`committed_candidate_from_plan` 算 `frozen_prefix_count`：
1. 取 plan 的 WGS84 waypoints + 本船 current position
2. 算每点到本船的 along-track 距离
3. `frozen_prefix_count` = 距离 < `GncExecutionOdd.min_first_changed_distance_m` 的点数（GNC guard 内）
4. 这些点进 candidate.geometry 作为 prefix

manager `try_revise` 的 `preserves_committed_prefix` 检查（committed_route.cpp:226-238）已存在，会真正生效（之前 prefix 空所以 trivially pass）。需验证 `same_waypoint` 的 NED vs WGS84 一致性（GeoWP 当前是 x_m/y_m NED，但发布几何是 lat/lon——§6.2 重投影需一致坐标系）。

## 7. Rule13 + COLREG Direction/Min-Alt 内化

### 7.1 COLREG Direction + Min Alteration 进 NLP（给-way）

当前 M6 `preferred_direction`（-1 port / +1 stbd）/`colregs_min_alteration_rad` 只用于 fallback/tail gate。内化为 NLP 硬约束（**仅 suffix 段 k≥K**，§3.3 分段激活）：
```
g_dir:    preferred_direction · l[k] ≥ 0          per k∈[K,N) (give-way)   // 同侧
g_minalt: preferred_direction · (psi[k]-own_psi) ≥ min_alt   per k∈[K,N)   // 最小转向
```
- `l[k]` = route-frame cross-track（§4.2）；preferred_direction·l[k]≥0 保证 lateral 在 M6 要求侧
- `own_psi` = kIdxOwnPsi（当前 reserved，本 spec 启用）
- `min_alt` = M6 minimum alteration（已算，node.cpp:364-385）

**激活/禁用规则（§3.3）**：`preferred_direction==0` / `STAND_ON` / HOLD/ReduceSpeed 时禁用（lbg=-inf）。

**平滑性**：preferred_direction 是 parameter（±1），l[k] 线性 → g_dir 线性。g_minalt 线性。无新非光滑源。

**与 J_asym 关系**：J_asym 是 give-way 右转 soft 偏好；g_dir/g_minalt 是 hard side 约束。J_asym 保留，不冲突。

### 7.2 Rule13（Overtaking）实现

当前 constraint_compiler.cpp:253-267 是空 sentinel。实现：
- Rule13 give-way（追越船）: side 由 M6 `preferred_direction` 定（不默认 starboard）
- 约束形式: 复用 §7.1 的 g_dir + g_minalt（Rule13 不额外加 heading-row）
- **降级声明（Codex 评审）**：Rule13 的 pass-astern/no-crossing-ahead/side-release 语义本 spec 不覆盖（依赖 ship-domain，§3.5 后续）。本 spec 只实现 side + min_alt，overtake 完整语义留后续。

## 8. 前置 Bug 修复

### 8.1 Zone 积分方向（constraint_compiler.cpp:447-448）

```cpp
// 当前（错）:
cum_x = cum_x + u_k * dt * sin(psi_k);   // x=north 但用了 sin
cum_y = cum_y + u_k * dt * cos(psi_k);   // y=east  但用了 cos

// 修复（对齐 CPA :305-306 与 NED 约定）:
cum_x = cum_x + u_k * dt * cos(psi_k);   // x=north
cum_y = cum_y + u_k * dt * sin(psi_k);   // y=east
```
同时修正注释（:408-409）。单测：构造已知 psi/u 轨迹，验证 zone 约束点与 CPA 约束点几何一致。

### 8.2 Risk Weight 死代码（mid_mpc_node.cpp:403-410）

移除 `tgt.cpa_m = ... ; tgt.tcpa_s = ...` 改动（NLP pack 不读这两字段）。J_colreg 的 range-ramp 权重（formulation.cpp:336-344）已是正确的动态权重机制（J_colreg spec §3.2）。移除死代码 + 更新注释。

## 9. 时序连续性 + GNC 可执行性

### 9.1 时序连续（prefix + heartbeat 对齐）

- **prefix 冻结时长 ≥ heartbeat 间隔**: committed prefix 覆盖 ≥ H_publish（60s 开阔）的执行段。K·dt ≥ 60s → K ≥ 12 步。结合 §6.3 K_max，K 范围 [1, N-K_suffix_min]=[1,12]。
- **heartbeat 到期时发布的几何**: prefix 段（GNC guard 内）= 上次发布的对应段（连续，§6.2 重投影保证）；suffix 段是 NLP 新解（可能变），但在 GNC guard 之外（GNC 可接受更新）。
- **route_hash 稳定性（Codex 评审修复——删虚假声明）**：manager hash 全几何（committed_route.cpp:200-209），suffix 每变 hash 就变。**prefix 不变 ≠ hash 不变 ≠ heartbeat 不增 revision**。真实行为：heartbeat 刷新 valid_until 不增 revision **仅当 suffix 几何也未变**；suffix 变则 route_changed=true 立即发布 + revision+1。continuity 的价值是**几何时序连续**（prefix 段），不是 hash 稳定。

### 9.2 GNC 可执行性（preflight 闭环）

完整候选（NLP + TailBuilder hold/rejoin + nominal suffix）发布前过 Slice F GNC preflight（已实现）。本 spec 新增的 route-frame/terminal 约束使候选更可能过 preflight（几何更规整）。preflight 失败仍走 fallback/DegradedHold（不变）。

### 9.3 完整发布候选时序

```
cycle T:
  [1] assemble_input_ (含 route-frame polyline 投影, §4.1)
  [2] NLP solve (prefix equality + route/terminal/direction 约束, §3-7)
  [3] NLP waypoints (MID_MPC_OPTIMIZED, 密集)
  [4] TailBuilder (terminal hold + rejoin, §5.2)
  [5] L2 nominal suffix append
  [6] GNC preflight (Slice F)
  [7] CommittedAvoidanceRoute.try_revise (prefix 冻结 + suffix 替换, Slice D)
  [8] publish_avoidance_plan_ (route_hash 去重 + 60s heartbeat, Slice A)
```

## 10. Acceptance Criteria

### 10.1 Unit-level（每条带 exact command）
- Zone 积分方向修复：`colcon test --packages-select m5_tactical_planner --ctest-labels "ZoneIntegration"`，验证 zone 约束点与 CPA 约束点 NED 几何一致（同 psi/u 输入）
- Risk weight 死代码移除：grep 确认 node.cpp 无 `tgt.cpa_m=.../tgt.tcpa_s=...` 改动；单测 pack 后 target cpa_m/tcpa_s == M2 原值
- J_route dimensionless：无 target fixture NLP solve 后 `l[k]/l_scale < 0.1 ∀k`（lateral 收敛）；J_route 值 O(1)
- **COLREG dominance（Codex 评审 Critical）**：target@cpa_hard fixture，验证 `w_colreg·J_colreg > w_route·J_route + w_dist·J_dist`（单测断言 cost 分量大小关系）
- terminal 硬约束：give-way fixture，terminal `direction·l[N-1] ≥ l_min`；lateral 在 [−l_max, +l_max]
- TailBuilder 接线：normal path 输出含 MID_MPC_OPTIMIZED + TERMINAL_HOLD（active 阶段）+ REJOIN（release 阶段）+ NOMINAL 段（单测 mock M6 state）
- **TailBuilder active-phase（Codex 评审 Critical）**：mock `encounter_state==ACTIVE && !past_clear`，TailBuilder 返回 hold-only（无 rejoin），不 reject `m6_not_past_clear`
- prefix-equality：K=4 fixture，psi[0..3] 被 equality 钉死（解 == prefix 值），psi[4..N-1] 自由
- **prefix 几何映射（Codex 评审 Critical）**：两 cycle origin 不同但 committed prefix WGS84 相同 → NLP prefix psi/u 重投影后产生相同 WGS84 几何（单测）
- CPA/direction prefix 软化：k<K 行 lbg=-inf（单测检查 lbg 数组）
- Rule13：overtake side = M6 preferred_direction（单测 port/stbd 两案例）
- direction/min_alt 禁用：preferred_direction=0 / STAND_ON / HOLD 时 lbg=-inf（单测）
- **manager frozen_prefix_count（Codex 评审 Critical）**：plan waypoints 中 GNC guard 内点数 == candidate.frozen_prefix_count（单测）

### 10.2 Integration-level
- TailBuilder 段标签在 ASDR trace 可见（grep `segment_source` in trace jsonl）
- CommittedAvoidanceRoute prefix 真正保留（两 cycle 后 manager.committed_prefix 非空且 preserves_committed_prefix 返回 true）
- GNC 消费完整候选执行无 path reset（`docker logs codex-gnc-validation-gnc-nodes-1 | grep "切换航点"` 无异常 reset）

### 10.3 Scenario-level（exact command + metric source）
```bash
# rule14-ho（主验收）
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs \
  --sim-rate 5 --trace-report-dir runs/nlp_v2_rule14ho \
  --summary-out runs/nlp_v2_rule14ho-summary.json --scenario colreg-rule14-ho
```
| metric | pass 阈值 | 来源 | 当前（v1 baseline） |
| --- | --- | --- | --- |
| steering_reversals | < 50 | trace summary `steering_reversals` | 1660 |
| int_abs_xte | < 300000 | L6 seamanship scorer | 1,587,980 |
| 180° port/stbd 翻转 | 0 | trace `heading` 序列人工/脚本检 | 存在 |
| CPA min | ≥ cpa_hard_m (1852) | trace `cpa_min` | 812（v1）|
| route_return | PASS | L5 scorer | FAIL |
| overall_pass | True | summary `overall_pass` | False |

**阈值来源**：steering_reversals<50 + int_abs_xte<300000 来自 L6 seamanship scorer green 判据（非本 spec 自定）。CPA≥1852 来自 `odd_aware_thresholds.yaml`。**禁止为让 probe 变绿而调这些阈值**（CLAUDE.md）。

**降级声明**：本 spec 不保证 overall_pass=True（因 §3.5 降级项 risk/covariance/ship-domain/no-crossing-ahead 未进 NLP）。验收目标：steering_reversals + int_abs_xte + route_return 三项显著改善（continuity + route-return 主目标达成），CPA/risk 合规靠现有 CPA hard floor + tail-gate + fallback 兜底。

## 11. 备选升级路径（Approach 2，暂不实现）

若 Approach 1 的 route-frame 投影非线性导致 IPOPT 不稳（验收未过），升级为 Approach 2：
- 决策变量扩展为 `x=[psi;u;l]`（l = cross-track 显式决策变量）
- route-return/terminal/direction 约束直接作用 l（线性）
- 代价：决策维度 2N→3N（36→54），CPU cap 2s 压力上升，需验证
- 一致性约束：`l[k] = project(integral_pos[k], route_frame)`（linking constraint）

**升级触发条件**：Approach 1 验收 10.3 未过 + 根因定位为 route-frame 投影非线性致 IPOPT 频繁 Infeasible/Restoration_Failed。

## 12. Non-Goals

- 不改 J_colreg/J_asym 公式（J_colreg spec 固化）
- 不调既有权重（w_colreg/w_dist/w_vel/k_asym）压 probe 绿
- 不扩 NLP horizon 到 600s/10min（spec §12.2）
- 不引入第二个优化器（分层方案，Approach 3 已否决）
- 不改 publish heartbeat / M6 msg schema / M7 policing（committed-route spec v2 保留；**例外**：改 manager 的 frozen_prefix_count 数据源 §6.6 + TailBuilder active-phase 语义 §5.2，这两处是本 spec 必须改的）
- 不加 slack 变量到 CPA hard floor（Keep-Last §9.12 + fallback 兜底，§6.4）
- 不实现 Monte Carlo / 故障注入 V&V（committed-route spec §21 Non-Goal）
- **不声称 §9.3 full compliance**（§3.5 降级项：risk covariance / ship-domain / BCT / no-crossing-ahead / GNC yaw-decel 留后续）
- 不实现 Rule13 完整语义（pass-astern/no-crossing-ahead/side-release 依赖 ship-domain，§7.2 降级）
- 不依赖 M7 policing 作安全网（Slice K 未完成，§6.4 诚实声明）

## 13. Source Coordinates

- NLP formulation: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`
- NLP header (kIdx): `include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp:41-60`
- Constraint compiler: `src/.../shared/constraint_compiler.cpp`（zone bug :447-448, Rule13 sentinel :253-267）
- Solver (warm-start): `src/.../mid_mpc/mid_mpc_solver.cpp:41-71`
- Node (on_solve_cycle_, assemble_input_, risk weight 死代码): `src/.../mid_mpc/mid_mpc_node.cpp`
- TailBuilder: `src/.../tail_builder/tail_builder.cpp` + `include/.../tail_builder/tail_builder.hpp`
- Waypoint generator (未接 TailBuilder): `src/.../mid_mpc/mid_mpc_waypoint_generator.cpp`
- CommittedAvoidanceRoute: `src/.../committed_route/committed_route.cpp`
- tail-gate (acceptance): `include/.../common/types.hpp:840-897` (`accept_tail_gate`)
- J_colreg spec: `docs/Design/TDL-Kernel/M5-Tactical-Planner/M5-jcolreg-redesign-spec.md`
- committed-route spec v2: `docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md`

## 14. 接地来源

- **nlm colav_algorithms 🟢 High**: receding-horizon NMPC continuity 四技术对比，technique 3（immutable prefix + Terminal Set）最鲁棒；homotopy class switch 需多 prefix 点防御；Maciejowski / Bemporad & Morari Terminal Set 理论
- **Codex NLP 完备性评审（2026-07-02）**: §9.3 七项逐项判定 + 疑点 A/B 确认 + zone bug + risk weight 死代码 + TailBuilder 未接线
- **Codex 对抗评审（2026-07-02）**: 6 项 Critical/High 修订——H_commit vs H_publish 混淆、TailBuilder active 卡死、J_route 尺度压制、J_terminal nonsmooth max、one-time graph 假声明、M7 兜底未就绪 + §9.3 虚假 coverage + kParamDim 计数 + direction 禁用规则
- **J_colreg redesign spec §3/§6/§7**: J_colreg/J_asym 公式 + 权重决策 + nlm 接地（不改）
- **committed-route spec v2 §0/§9.3/§9.5/§9.7/§9.12/§13**: H_commit 定义 + NLP 须含 + tail-gate + TailBuilder active/hold 到 s_clear + rolling policy + 非凸论证
- **handoff workspace_log 2026-07-02 (cont.)**: continuity 根因 pinned（psi[0] 自由 + warm-start 累积 → 极限环）
- **代码实证**: H_commit `GncExecutionOdd.min_first_changed_distance_m=100`（tail_builder.hpp:33）；TailBuilder `m6_reports_clear` gate（tail_builder.cpp:129-134）；manager `frozen_prefix_count=0`（node.cpp:111）；build_symbolic_graph 每 cycle 调（node.cpp:475）

## 15. Implementation Slices（概要，详细 plan 由 writing-plans 产出）

- **Slice P0**: 前置 bug（zone 积分 + risk weight 死代码），独立 commit
- **Slice R1**: route-frame active-leg 投影 + J_route dimensionless + l[k] 符号计算 + 单 leg 跨越 guard（§4）
- **Slice T1**: terminal 约束 + J_terminal smooth-only（§5.4/§5.5）
- **Slice W1**: TailBuilder active-phase 语义改造（§5.2 两阶段 hold/rejoin）+ normal path 接线（§5.3）
- **Slice C1**: continuity H_commit prefix（§6.2 几何映射 + §6.3 GNC guard K + §6.4 CPA 软化 + §6.5 warm-start）
- **Slice M1**: manager frozen_prefix_count 数据源（§6.6，改 committed_candidate_from_plan + 坐标系一致性）
- **Slice D1**: COLREG direction + min_alt 内化 + 分段激活（§7.1）
- **Slice O1**: Rule13 side+min_alt（§7.2，降级：不含 pass-astern/no-crossing-ahead）
- **Slice V1**: runtime 验证（rule14-ho 主验收 + rule15-cs + rule13-ot，GNC profile sim-rate 5）

**依赖**：P0 独立先做；R1→T1→W1（TailBuilder 需 terminal state + route-frame）；C1 依赖 M1（manager prefix 数据源）；D1/O1 依赖 R1（l[k]）+ C1（分段激活）；V1 依赖全部。
