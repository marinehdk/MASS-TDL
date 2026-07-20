# GNC 独立评审:IPOPT 路径 OCP 语义合理性

> **评审 agent**:tdl-gnc-contract-reviewer(agent_03ab040d)
> **评审日期**:2026-07-20
> **评审对象**:M5 Mid-MPC **IPOPT 路径**的 OCP 语义实现(作为 acados 对齐参照系的前提验证)
> **工作区**:HEAD `4fd37fd7e9fc435656e2154d92b859920a0eb646`,分支 `codex/m5-design-grounding`
> **workspace writes**:none(read-only)
> **状态**:DONE_WITH_CONCERNS → PASS_WITH_FINDINGS

## 评审目的

用户批准"acados 与 IPOPT 语义一致"方向,但质疑 IPOPT 路径本身是否合理。本评审独立验证 IPOPT 路径的 OCP formulation,确认它是否适合作为 acados 对齐的权威参照系。如果 IPOPT 路径本身有缺陷,把 acados 对齐到它就是归因漂移。

## 总评:IPOPT 路径作为 acados 对齐参照系 — 部分适合

**结论**:IPOPT 路径在结构层面适合作为参照系,但在 **3 个语义点**需要双方共同修正后再主张"语义对齐"。

---

## ✅ 适合作为参照系的部分(7 项,acados 应严格镜像)

### Q1 — CPA hard floor 字段拆分(`cpa_hard_m` vs `cpa_safe_m`)
**判定:结构合理,但 σ slack 削弱了 "hard" 的语义强度 — 需明确陈述**

证据:
- `constraint_compiler.cpp:247-303` `compile_cpa_distance` 实现 `g_row = dx² + dy² - cpa_hard_m² + sigma`,使用 `inputs.cpa_hard_m`(而非 `cpa_safe_m`)。
- `mid_mpc_nlp_formulation.cpp:579-580` 求解器将 `sigma_`(MX 符号)传递给编译器。
- `mid_mpc_node.cpp:656-664` 节点将 `cpa_safe_m` 设为 2500(冲突期间),`cpa_hard_m` 设为 1852(未提升)。
- `mid_mpc_nlp_formulation.hpp:121-122` `cpa_slack_enabled=true`,`w_slack=1e8`。
- `mid_mpc_solver.cpp:259-268` σ 符号受限于 `lbx=0, ubx=+inf`。
- `row_registry.hpp:318-328` `apply_cpa_suffix_hard_` 仅切换边界;表达式中的 σ 保持不变。

**风险**:即使对于"硬"CPA 行,`g = d²-cpa_hard²+σ ≥ 0` 也可以通过激活 σ 而不是几何上满足。设计意图(规范 v2.3 §2.1/§2.3)是"精确惩罚以保持可行性",这是有意软化,但安全声明"CPA hard floor"必须表述为"在大权重 w_slack=1e8 下 σ 趋近于零时保持硬约束",**并非真正的不可行硬约束**。

### Q2 — Reachability schedule `k_minalt_rot = ceil(min_alt / rot_step) - 1`
**判定:公式合理(off-by-one 正确),但 box-infeasible 全软化的路径有 fail-open 风险需明确标注**

证据(`mid_mpc_solver.cpp:462-502`):
- `:473` `rot_step = input.rot_max_rad_s * dt_s`,使用 `cruise_max_yaw_rate_deg_s`(GNC execution ODD),物理合理。
- `:475-477` `k_minalt_rot = ceil(min_alt/rot_step) - 1`,`clamp(0, n_horizon)`。

**off-by-one 验证 — 正确**:ROT 行结构是 (row 0,1) = own_psi→psi[0] hi/lo + (row 2..2N-1) inter-step。stage k 累计偏转上界 = `(k+1)·rot_step`,要让 `|psi[k]-own_psi| ≥ min_alt`,需 `k ≥ min_alt/rot_step - 1`。单测 `test_mid_mpc_solver.cpp:1280-1291` 验证。

**box-infeasible fail-open 风险**(`:491-496`):当 `box_reach_rad < min_alt - epsilon`,设 `minalt_box_infeasible=true` + `minalt_hard_from_k = n_horizon`(全软化)。这是**有意 fail-open**:让 NLP 尝试但 σ 兜底,由 `last_minalt_box_infeasible_`(`:304`)触发 BC-MPC dispatch(`mid_mpc_node.cpp:897-908`)。安全性**依赖 BC-MPC dispatch chain 闭环**。

### Q3 — heading box reachability
**判定:公式实现正确,但 box-reach 公式不对称(只 check upper 不 check lower),且 prefix 段不做 reachability soften**

证据(`mid_mpc_solver.cpp:479-496`):
- `box_reach_deg` 与 `min_alt` 比较,不可达则全软化。
- 这是 spec v2.2 §4.6 bimodal 实现。

**box-reach 不对称风险(CRITICAL-ISH)**:`heading_box_reachable_from_psi0_deg` 语义是"从 own_psi 沿 ROT 单向能到达 box 边界"(单边)。公式只 check `box_reach_rad < min_alt` 不区分方向。M4 publish 时已按 pref_dir 侧给(隐性合约),但 M5 代码层无 sanity check。**acados 镜像时必须同时 check box-reach 方向 vs pref_dir**。

### Q4 — prefix 不可控段处理
**判定:不合理 — 与 VR-04 / ALT-03 裁决冲突(IPOPT 真正缺陷)**

证据:
- `mid_mpc_solver.cpp:290-296` `K_eff > 0` 自动设 `rb_eff.colreg_prefix_softened = true`,k<K 段 CPA/direction/min_alt 全 relax。
- prefix 段 own 轨迹**几何冻结**,NLP 只能选 suffix。

**committed prefix 本身违反 hard floor 时**:IPOPT **没有**显式 NO_SAFE_PLAN 路径,只有 `Status::{Converged,Timeout,Infeasible,NumericalFailure,NotInitialized}`。prefix 段 CPA 被 relax,σ 全局标量仍加到这些 row → 返回 Converged + σ>0。**这正是 ALT-03 所禁止的形态**。

**修复建议**:prefix 段 CPA 应 relax 到 [-inf,+inf] **不带 σ**(expression 层 conditional),或 solve() 入口独立几何 check 违反即返回 Infeasible。

### Q5 — runtime heading/speed/ROT box 落地
**判定:合理,但有静态 box 限制和 frame/unit 注意事项**

证据:
- heading box:静态全 horizon,`mid_mpc_solver.cpp:253-258`,来自 M4 BehaviorPlan,rad/signed,**单位一致**。
- speed box:静态全 horizon,`:256-257`,kn→m/s via `kMsPerKn`,**单位一致**。
- ROT:动态参数 `kIdxRotMax`,每 cycle 从 GNC ODD 设定。
- decel:全 N 行 `g_speed_rate[k] ≥ 0`,仅限制减速幅度(加速无约束,已知建模简化)。

### Q6 — L4 executability
**判定:基本满足,但 prefix-CPA fail-open(Q4)是最大 executability 风险**

- 连续性、曲率/ROT、速度、航点有效性、L4 生命周期接管 — 均满足。
- **Executability 缺口**:prefix-CPA fail-open(Q4)、静态 heading/speed box(Q5 future-gap)、accel 无约束(Q5 已知)、σ 共享标量(Q1 多船)。

### Q7 — direction row k=0 soften(§4.4)
**判定:合理,acados 必须镜像**

证据(`mid_mpc_solver.cpp:504-554`):
- `g_dir[0] = pref_dir · l[0]`,l[0] 是 own 当前 XTE,NLP initial condition 不可移动。
- wrong-side 几何 `g_dir[0] < 0` 是不可移动硬违反。
- `direction_hard_from_k = clamp(k_dir, 0, n_horizon)`。
- spec §4.4 授权:"若有非 0 残差且活跃 → 下个会话软化(同 min_alt reachable schedule 模式)"。

**acados 必须镜像**,否则 wrong-side 几何直接 infeasible。

---

## ❌ 不适合直接镜像、需两路径同时修正的部分

### [Q4 严重] prefix 段 CPA fail-open
`apply_colreg_prefix_soften_` relax prefix CPA row,但 σ 全局标量仍加到这些 row → prefix 几何违反 hard floor 被 σ 吸收 → Converged+σ>0。**与 VR-04/ALT-03 裁决冲突**。这是 acados 对齐**之前**应先在 IPOPT 修复的项。

### [Q1/Q6] σ 共享标量在多船场景不可分
`[TBD-MULTI-SHIP]` 已标注,不是当前 bug,但 acados 镜像时不应声称"per-target semantic parity"。

### [Q3 隐患] box-reach 方向 vs pref_dir 一致性
隐性 M4 合约,建议两路径同时加 sanity assert。

### [Q5 future-gap] 静态 heading/speed box
未来 M4 publish per-stage box 时两路径需同时升级。

---

## 推荐顺序(评审建议)

1. **优先**(IPOPT 路径独立修复 Q4):σ expression 不加到 k<K prefix CPA row + 加独立 prefix witness → 让 prefix 段违反 hard floor 显式返回 Infeasible。
2. 然后 acados 对齐 Q1/Q2/Q3/Q7 的 schedule + σ 精确惩罚。
3. acados status fail-open(F-01)独立于 IPOPT,并行修。

---

## 相关文件路径(绝对路径)

- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp`

## 剩余不确定性(交 TDL Lead 决策)

- Q4 prefix-CPA fail-open 修复方向(σ 不加 prefix row / 入口独立 witness / 两者)涉及架构裁决。
- BL-01/BL-02/BL-04/BL-06 未闭环需排程。
- Q4 fail-open 无回归测试拦截,建议补充。
