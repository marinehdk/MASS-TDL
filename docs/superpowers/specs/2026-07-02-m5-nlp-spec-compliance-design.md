# M5 NLP Spec-Compliance Design — 完整 NLP + TailBuilder + Continuity

- Date: 2026-07-02
- Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- Branch: `codex/colregs-12probe-debug`
- Status: Draft（待用户评审 + Codex 对抗评审）
- Supersedes (针对 NLP 内部): `2026-06-30-m5-committed-route-design-v2.md` 的 §9.3/§9.5 NLP 内部条款由本 spec 细化为可执行实现依据；committed-route spec 的 publish/manager/§9.7 TailBuilder 外部契约保留不变
- 关联: `M5-jcolreg-redesign-spec.md`（J_colreg/J_asym 来历，本 spec 不改其公式）

## Revision History

| 版本 | 日期 | 变更 |
| --- | --- | --- |
| v1 | 2026-07-02 | 初版：基于 Codex NLP 完备性评审 + nlm 🟢 continuity 调研，定义完整 NLP（route-frame + terminal + Rule13 + continuity）+ TailBuilder 接线 + 前置 bug 修复 |

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

1. **spec §9.3 七项全覆盖**（NLP 内部）：CPA/risk、COLREG direction+min_alt、Rule13-17、heading/speed、ROT/yaw、route-return、terminal tail-extension。
2. **continuity**：NLP 续接 committed prefix（suffix-only optimization），消除贪婪重解导致的极限环。
3. **TailBuilder 接线**：normal path 从 NLP terminal state 经 TailBuilder 生成 hold+rejoin+nominal suffix 完整航线。
4. **时序连续 + GNC 可执行**：发布几何时序连续（prefix 冻结 + heartbeat 对齐），过 GNC preflight。
5. **不改 J_colreg/J_asym 公式**（J_colreg redesign spec 已固化，本 spec 只加 cost 项不改动既有）。
6. **不调权重/阈值压 probe**（CLAUDE.md 禁）；新 cost 项的权重带文献/工程依据 + [TBD-HAZID] 标注。

## 3. NLP 完整定义

### 3.1 决策变量（不变）

`x = [psi(0..N-1); u(0..N-1)]`，`N=18, dt=5s, H_pred=90s`。
位置 `pos[k] = (x_m[k], y_m[k])` 由 psi/u 积分（NED: psi=0→north, x=north, y=east）：
```
x_m[k] = x0 + Σ_{j=0}^{k-1} u[j]·dt·cos(psi[j])
y_m[k] = y0 + Σ_{j=0}^{k-1} u[j]·dt·sin(psi[j])
```
**不引入 `l/s` 为决策变量**（Approach 1；Approach 2 备选升级，见 §11）。

### 3.2 Cost 全集（J_colreg/J_asym 不变 + 新增 3 项）

```
J = w_colreg·J_colreg + w_dist·J_dist + w_vel·J_vel + J_asym
  + w_route·J_route          (新增, §4.3)
  + w_terminal·J_terminal    (新增, §5.3)
```

| 项 | 公式 | 权重 | 作用 | 状态 |
| --- | --- | --- | --- | --- |
| J_colreg | `avg Σ tw·disc·exp(-ζ(d-cpa_safe))` | w_colreg | CPA soft barrier | 不变（J_colreg spec）|
| J_dist | `Σ(psi[k]-route_bearing)²` | w_dist | heading 拉回 | 不变（保留，与 J_route 互补）|
| J_vel | `Σ(u[k]-planned_speed)²` | w_vel | speed 跟踪 | 不变 |
| J_asym | `give_way·k_asym·Σ softplus(bearing-psi[k])` | k_asym | Rule14/15 右转 | 不变（J_colreg spec）|
| **J_route** | `Σ l[k]²` + `λ·l[N-1]²`（terminal 加强）| w_route [TBD-HAZID] | route-frame cross-track 回归 | 新增 §4.3 |
| **J_terminal** | 见 §5.3 | w_terminal [TBD-HAZID] | terminal state 可延伸性 | 新增 §5.3 |

**权重依据**：w_colreg/w_dist/w_vel/k_asym 已在 J_colreg spec §6 决策固化（nlm 🟢）。w_route/w_terminal 用文献经验值（route 跟踪权重 ≈ w_dist 量级；terminal 引导权重 < J_colreg 避免压制避让）+ [TBD-HAZID] 标注，HAZID RUN-001 校准。**禁止用权重压 probe 绿**（CLAUDE.md）。

### 3.3 约束全集（g ≥ 0 不等式 + prefix equality）

| 约束类 | 形式 | 数量 | 类型 | 状态 |
| --- | --- | --- | --- | --- |
| heading box | `lbx/ubx` per-var | N | 变量界 | 不变 |
| speed box | `lbx/ubx` per-var | N | 变量界 | 不变 |
| ROT | `rot_step±dpsi ≥ 0` | 2(N-1) | 平滑线性 g | 不变 |
| CPA hard floor | `dx²+dy²-cpa_hard² ≥ 0` per(target,step) | Nt·N | 硬 g | 不变（RC-C）|
| Rule14/15/16/17 | heading rows（constraint_compiler）| 规则依赖 | 硬 g | 不变 |
| **Rule13** | overtake side（§7.2）| 规则依赖 | 硬 g | 新增 §7 |
| **COLREG direction** | `preferred_direction·l[k] ≥ 0`（§7.1）| N（give-way）| 硬 g | 新增 §7.1 |
| **min alteration** | `direction·(psi[k]-own_psi) ≥ min_alt`（§7.1）| N（give-way）| 硬 g | 新增 §7.1 |
| **terminal side** | `direction·l[N-1] ≥ l_min_feasible`（§5.4）| 1 | 硬 g | 新增 §5.4 |
| **terminal lateral bound** | `l[N-1]+l_max≥0 ∧ l_max-l[N-1]≥0`（§5.4，两线性替代 abs）| 2 | 硬 g | 新增 §5.4 |
| **prefix equality** | `psi[k]-prefix_psi[k] = 0`（§6）| N（动态 K active）| 等式 g（lbg=ubg=0）| 新增 §6 |
| **prefix u equality** | `u[k]-prefix_u[k] = 0`（§6）| N | 等式 g | 新增 §6 |
| zone | polygon containment | zone·N | 硬 g | **修 bug §8** |

### 3.4 Parameter 扩展（kParamDim 94 → 增 route-frame + prefix）

新增 parameter slot（kParamDim 扩展，编译时固定上限）：
- `kIdxRouteFrameOriginX`, `kIdxRouteFrameOriginY`：route-frame 原点 NED（x=north, y=east）
- `kIdxRouteFrameNormalX`, `kIdxRouteFrameNormalY`：当前 active leg 法向单位向量分量（pre-computed = bearing+90°，避免符号图内 trig）
- `kIdxRouteFrameActiveLegBearing`：当前 active leg bearing（rad，用于 §7.1 min_alt 方向参照）
- `kIdxPrefixPsi[N]`, `kIdxPrefixU[N]`：committed prefix 的 psi/u 值（N=18 固定）
- `kIdxPrefixActiveK`：当前 active prefix 长度 K（标量，control §6.2 lbg/ubg 激活）
- `kIdxPreferredDir`：M6 preferred_direction（-1.0/0.0/+1.0）
- `kIdxMinAlterationRad`：M6 minimum alteration
- `kIdxRole`：M6 primary_role enum

**kParamDim 重算**：94（现）+ 8 head slots + 2·18 prefix = 138。更新 `static_assert`。route-frame polyline 多段不进 NLP parameter（首版只用 active leg 法向，§4.2 单 leg 近似），node 层在 assemble_input_ 算出 active leg 后只 pack 该 leg 的法向 + bearing。

**符号图一次建好**（build_symbolic_graph 仍单次调用），所有新约束在图内用 parameter 表达，每 cycle pack_parameters 填新值。**不每 cycle rebuild graph**（spec §12.2）。

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

### 4.3 J_route（新增 cost）

```
J_route = Σ_{k=0}^{N-1} l[k]²  +  λ_terminal · l[N-1]²
```
- 第一项：全时域 cross-track 罚（拉回 nominal 航线）
- 第二项：terminal cross-track 加强（`λ_terminal > 1`，引导 terminal state 接近 nominal，利于 rejoin）
- `w_route` 权重使 J_route 与 J_dist 同量级（route 跟踪是 mid-priority，低于 COLREG 避让，高于纯 heading 拉回）

**与 J_dist 的关系**：J_dist 拉回 heading bearing（方向），J_route 拉回 cross-track（位置）。两者互补——J_dist 保证朝向对，J_route 保证横向位置回归。J_dist 保留不变（不破坏 J_colreg spec 已验证的权重平衡）。

## 5. Terminal Tail-Extension 约束 + TailBuilder 接线

### 5.1 设计原理

spec §9.5 tail-gate 当前是 publish 前的 acceptance 检查（reactive reject）。本 spec 把 tail-extension 要求**前移到 NLP 内部**（proactive guide）：NLP 的 terminal state 受约束/cost 引导，使其天然落在 TailBuilder 可安全延伸的构型。这样 IPOPT 有梯度知道"如何靠近可延伸 terminal state"，而非盲目优化 90s 机动后被动等 tail-gate reject。

nlm 🟢 High（technique 3）：immutable prefix + **deterministic Terminal Set** 是 receding-horizon NMPC recursive feasibility 的标准方法（Maciejowski; Bemporad & Morari）。TailBuilder 几何 tail 即 Terminal Set。

### 5.2 TailBuilder 接线（normal path）

**当前**：`mid_mpc_waypoint_generator.cpp` 只转 NLP waypoints + 直接 append L2 suffix，不调 TailBuilder。

**改后**：normal path（node.cpp `on_solve_cycle_` 的 `wp_gen_.generate` 或等效处）流程：
```
NLP solution (psi/u trajectory, terminal state pN/psiN/uN)
  ↓
[1] NLP waypoints (MID_MPC_OPTIMIZED, 密集采样)          ← 现有 wp_gen
  ↓
[2] TailBuilder.build(TailInputs{                         ← 新增接线
       pN, psiN, uN,                                      ← NLP terminal
       protected_side = M6 preferred_direction,
       m6_past_clear, m6_encounter_state,                 ← Slice G msg
       route_frame, targets, cpa_*, gnc_odd
     })
  ↓ if TailResult.hold_then_rejoin has value:
[3] append MID_MPC_TERMINAL_HOLD + REJOIN_TO_L2 waypoints
  ↓
[4] append L2_NOMINAL_SUFFIX (rejoin station 下游)        ← 现有 append_l2_nominal_suffix
  ↓
[5] GNC preflight complete candidate                     ← Slice F
  ↓
[6] CommittedAvoidanceRoute.try_revise + publish         ← Slice D + A
```

**TailBuilder reject 处理**：若 `TailResult.reject_reason` 非空（terminal state 不可延伸），候选标 `nlp_tail_gate_failed`，走 fallback/DegradedHold（现有 on_solve_cycle_ 逻辑 :524-536）。但 §5.3/§5.4 的 NLP 内 terminal 约束应使这种情况大幅减少。

### 5.3 J_terminal（新增 cost）

引导 terminal state 朝 TailBuilder 可延伸构型（同侧 + lateral offset 在可 rejoin 范围）：
```
J_terminal = softplus_penalty(l[N-1] 在错误侧)           // 鼓励 terminal 在 M6 side
           + (max(0, |l[N-1]| - l_rejoin_max))²          // 惩罚 lateral 太大不可 rejoin
```
- `l_rejoin_max` = GNC-feasible 最大 lateral offset（GncExecutionOdd.max_lateral_offset_m）
- 第一项：softplus 平滑（与 J_asym 同族），gate by give_way
- `w_terminal` 弱于 w_colreg（避让优先），强于 w_dist（terminal 引导需有效）

### 5.4 Terminal 硬约束（给-way 角色）

给-way 角色下，terminal state 须满足（硬约束 g 行）：
```
g_term_side:    preferred_direction · l[N-1] ≥ l_min_feasible       // 同侧, l_min_feasible > 0
g_term_lo:      l[N-1] + l_max_feasible ≥ 0                          // lateral 下界 (平滑线性, 替代 |·|)
g_term_hi:      l_max_feasible - l[N-1] ≥ 0                          // lateral 上界
```
- `l_min_feasible` = COLREG apparent action 最小偏置（防 tail 过小无效）
- `l_max_feasible` = GNC + TailBuilder rejoin 几何上界
- **不用 `|l[N-1]|`**：abs 在 0 处非光滑（kink），改为两条线性约束 `l+l_max≥0 ∧ l_max-l≥0`（等价 box `|l|≤l_max`，平滑），对齐 J_colreg spec §2 ROT abs→两线性的同一处理原则

**stand-on 角色（§4 committed-route spec）**：无 terminal 约束，完整航线 = L2 nominal（Rule 17 keep heading）。

### 5.5 与 tail-gate（acceptance）的关系

NLP 内 terminal 约束（§5.3/§5.4）是 **proactive 引导**；acceptance 层 tail-gate（types.hpp `accept_tail_gate`）是 **reactive 验证**，保留不变。两者不冲突：NLP 约束使解更可能过 tail-gate，tail-gate 仍是发布前最后防线（defense in depth）。

## 6. Continuity 机制（A1 Prefix-Equality）

### 6.1 设计原理（nlm 🟢 High technique 3）

每 cycle NLP **续接 committed prefix** 而非贪婪重解。committed prefix（已发布 + GNC guard 内不可变）化为等式约束钉死，NLP 只在 suffix 子时域自由优化。

极限环链切断点：
```
旧: 每 cycle fresh 重解 → psi[0] 自由 → warm-start 续接 → 累积 42° → 翻转
新: prefix equality 钉死 psi[0..K-1] → suffix 优化不能动 prefix → 发布几何连续
```

### 6.2 Prefix-Equality 实现（不重建图）

符号图预分配 N 行 `g_prefix_psi[k] = psi[k] - prefix_psi[k]`（k=0..N-1），N 行 `g_prefix_u[k] = u[k] - prefix_u[k]`。`prefix_psi/prefix_u` 是 parameter（pack 时填）。

每 cycle 通过 **lbg/ubg 动态激活**：
- k < K（active prefix）: `lbg = ubg = 0`（等式约束，g=0 强制 psi[k]=prefix_psi[k]）
- k ≥ K（suffix 自由）: `lbg = -inf, ubg = +inf`（约束失效，psi[k] 自由）

CasADi/IPOPT 支持 per-row 不同 bound，符号图不重建。`K` 每 cycle 变化只改 lbg/ubg 数组。

### 6.3 动态 K 策略

`K = max(1, 已执行步数)`，随本船推进 K 增长（spec §9.12 "prefix 累积"语义）：
- 首次 commit: K=0（全 suffix 自由，NLP 出完整首版轨迹）
- 后续 cycle: K = ceil(已执行时间 / dt_s)（已执行段升格为 prefix）
- K 上限: K_max = N - K_suffix_min，`K_suffix_min` 保留足够 suffix 优化空间（建议 6 步 = 30s）

### 6.4 CPA 冲突处理（prefix 段软化）

**问题**：prefix 段几何钉死后，目标船移动使 prefix 段可能违反新 target 位置的 CPA hard floor → NLP 不可行。

**处理**：prefix 段（k < K）的 CPA hard floor 行 lbg 设 -inf（软化），只 suffix 段（k ≥ K）保留 `dx²+dy²-cpa_hard² ≥ 0`。

**安全论证**：
- prefix 几何是上一 cycle 已发布 + GNC 正在执行的 committed route，非 NLP 可修
- CPA 穿透的真正安全网是 **M7 SOTIF + X-axis veto**（§13.4 policing）+ **Keep-Last-Route §9.12 风险门控**（已实现：current_cpa < cpa_hard → DegradedHold）
- NLP 内 suffix 段 CPA hard floor 保证**未来**轨迹安全
- 不在 NLP 内强行保证 prefix 段 CPA（那需 slack 变量增加复杂度，且 Keep-Last 已兜底）

### 6.5 Warm-Start 调整

`pack_warm_start_`（solver.cpp:41）当前用裸上一解。改为：
- prefix 段（k<K）: x0 = prefix_psi/prefix_u（被 equality 钉死，warm-start 自然对齐）
- suffix 段（k≥K）: x0 = 上一解对应 step（rolling continuity，但 prefix equality 已防累积漂移）

cold-start（首次无 prefix）: 不变（pack_cold_start_ 用 own_psi/own_u）。

## 7. Rule13 + COLREG Direction/Min-Alt 内化

### 7.1 COLREG Direction + Min Alteration 进 NLP（给-way）

当前 M6 `preferred_direction`（-1 port / +1 stbd）/`colregs_min_alteration_rad` 只用于 fallback/tail gate。内化为 NLP 硬约束：
```
g_dir:    preferred_direction · l[k] ≥ 0          per k (give-way)   // 同侧
g_minalt: preferred_direction · (psi[k]-own_psi) ≥ min_alt   per k   // 最小转向
```
- `l[k]` = route-frame cross-track（§4.2）；preferred_direction·l[k]≥0 保证 lateral 在 M6 要求侧
- `own_psi` = kIdxOwnPsi（当前 reserved，本 spec 启用）
- `min_alt` = M6 minimum alteration（已算，node.cpp:364-385）

**平滑性**：preferred_direction 是 parameter（±1），l[k] 是 pos 线性函数 → g_dir 线性。g_minalt 线性。无新非光滑源。

**与 J_asym 关系**：J_asym 是 give-way 右转的 soft 偏好（防对称退化）；g_dir/g_minalt 是 hard side 约束（M6 权威）。J_asym 保留（二级偏置），不冲突。

### 7.2 Rule13（Overtaking）实现

当前 constraint_compiler.cpp:253-267 是空 sentinel。实现：
- Rule13 give-way（追越船）: side 由 M6 `preferred_direction` 定（不默认 starboard，spec §4 矩阵）
- 约束形式: 同 §7.1 的 g_dir + g_minalt（Rule13 复用 direction 约束，不额外加 heading-row）
- 关键: Rule13 的 `preferred_direction` 来自 M6（overtake 几何选 side），M5 不自算

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
- **heartbeat 到期时发布的几何**: 因 prefix 钉死，新发布几何的 prefix 段 = 上次发布的对应段（连续）；suffix 段是 NLP 新优化（可能变），但在 GNC guard 距离之外（可接受更新）。
- **route_hash 稳定性**: prefix 不变 → hash 高频匹配 → heartbeat 刷新 valid_until 不增 revision（committed-route spec §6）。

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

### 10.1 Unit-level
- NLP §9.3 七项全覆盖（每项有对应 cost/constraint + 单测）
- Zone 积分方向修复（单测：zone 点与 CPA 点几何一致）
- Risk weight 死代码移除（单测：pack 不依赖 cpa_m/tcpa_s 字段改动）
- J_route 使 NLP 朝 nominal cross-track 收敛（单测：无 target 时 NLP 轨迹 lateral→0）
- terminal 硬约束使 terminal state 在 M6 side + feasible lateral 范围（单测）
- TailBuilder 接线：normal path 输出含 MID_MPC_OPTIMIZED + TERMINAL_HOLD + REJOIN + NOMINAL 四段（单测）
- prefix-equality：K=2 时 psi[0..1] 被钉死，NLP 只优化 psi[2..N-1]（单测）
- CPA 冲突：prefix 段 CPA 软化（lbg=-inf），suffix 段保留 hard floor（单测）
- Rule13：overtake side 由 M6 preferred_direction 定（单测）
- COLREG direction：give-way 时 lateral 在 M6 side（单测）
- continuity：连续 cycle NLP 解的 psi[0] delta 不累积（单测：2 cycle 后 psi0_delta < 阈值）

### 10.2 Integration-level
- TailBuilder 段标签在 ASDR trace 可见（MID_MPC_TERMINAL_HOLD / REJOIN_TO_L2）
- CommittedAvoidanceRoute revision 稳态稳定（prefix 不变 → 低 revision 增长）
- route_hash 在 heartbeat 期稳定
- GNC 消费完整候选（四段）执行无 path reset

### 10.3 Scenario-level（rule14-ho, GNC profile, sim-rate 5, restart-between-runs）
- steering_reversals ≈ 0（vs 当前 1660）
- int_abs_xte < 300000（L6 seamanship green，vs 当前 1.59M）
- 无 180° port/starboard 翻转
- CPA ≥ cpa_hard_m 全程
- route_return PASS
- overall_pass=True（safety+colregs+stability+route_return+corridor+risk+seamanship）

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
- 不在本 spec 改 publish/manager/M6 msg/M7 policing（committed-route spec v2 保留）
- 不加 slack 变量到 CPA hard floor（Keep-Last §9.12 兜底，§6.4）
- 不实现 Monte Carlo / 故障注入 V&V（committed-route spec §21 Non-Goal）

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
- **J_colreg redesign spec §3/§6/§7**: J_colreg/J_asym 公式 + 权重决策 + nlm 接地（不改）
- **committed-route spec v2 §9.3/§9.5/§9.7/§9.12/§13**: NLP 须含 + tail-gate + TailBuilder + rolling policy + 非凸论证（外部契约保留）
- **handoff workspace_log 2026-07-02 (cont.)**: continuity 根因 pinned（psi[0] 自由 + warm-start 累积 → 极限环）

## 15. Implementation Slices（概要，详细 plan 由 writing-plans 产出）

- **Slice P0**: 前置 bug（zone 积分 + risk weight 死代码），独立 commit
- **Slice R1**: route-frame polyline 投影 + J_route + l[k] 符号计算（§4）
- **Slice T1**: terminal 约束 + J_terminal（§5.3/§5.4）
- **Slice W1**: TailBuilder 接线 normal path（§5.2）
- **Slice C1**: continuity prefix-equality + 动态 K + CPA 软化 + warm-start 调整（§6）
- **Slice D1**: COLREG direction + min_alt 内化（§7.1）
- **Slice O1**: Rule13 实现（§7.2）
- **Slice V1**: runtime 验证（rule14-ho + rule15-cs + rule13-ot，GNC profile sim-rate 5）
