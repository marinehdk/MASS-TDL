# M5 Mid-MPC NLP 约束重构 Spec (v2.1)

- Date: 2026-07-04 (v2.1)
- Worktree: `/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- Branch: `fix/m5-nlp-heartbeat-shadow-upstream`
- Supersedes（约束章节）: `2026-06-30-m5-committed-route-design-v2.md` §5.5 / §7.1 / §9.3 / §13（仅约束分类与 hard/soft split，其余 v2 章节保持权威）
- Review basis: Codex task-mr53qtfa 决定性分析（horizon vs 避让生命周期）+ Fix A-F 修复链实测（CPA 穿透 0.2-3.4m + NLP 100% Infeasible）+ 用户 4 决策点拍板（Codex 完整方案 / ROT 4.7°/s / CPA hard 仅 release/suffix / 先 spec 后代码）

## Revision History

| 版本 | 日期 | 变更摘要 |
| --- | --- | --- |
| v2 | 2026-06-30 | 评审后修订：stand-on 角色矩阵 + M6 语义独占 + s_clear 复用 + keep-last ≤45s + 非凸 SOTIF + 架构同步清单 |
| **v2.1** | **2026-07-04** | **NLP 约束分类重构**（round-1 + round-2 Codex adversarial + NLM 🟢 修订）：(1) min_alt reachable schedule `k* = ceil(min_alt/rot_step)-1`（C1 off-by-one）= **k*=1** for 4.7°/s；(2) CPA floor **suffix-hard schedule**（k≥k_cpa 后 hard，前 J_colreg barrier soft）+ tail-gate release hard（B5 + NLM 🟢）+ B5-round2 k_cpa 唯一公式 + RF 条件化；(3) terminal full soften + tail-gate hard（B7 + NLM 🟢）+ B7-round2 two-sided softplus upper-band（无 abs kink）+ 复用 terminal_tau + helper 用 trajectory_terminal_lateral_offset_m（非不存在的 cross_track_m）；(4) direction 保持 hard + implementation-only diag probe（C3）；(5) ROT 4.7°/s（IMO MSC.137(76) 二次推导 🟡 B 级，B2 修订，**NLP rot_max 来自 GNC cruise 非 fcb_45m rot_max_curve**，B3/B4-round2）；(6) ROT 出处统一 4.7（cruise + emergency YAML，GNC `max(cruise,emergency)` clamp）；(7) 不声称「NLP 可行域凸化」（B1：l 经 cos/sin 积分仍非凸）；(8) CPA 名义术语（B6-round2 拍板 cpa_release 复用 cpa_hard_m）；(9) RowBoundConfig `*_override_valid` bool 区分 explicit vs derived（B8-round2）；(10) ReduceSpeed/stand-on/non-lateral disable precedence（B9-round2）；(11) CPA row order k·n_targets+t（C2-round2）；(12) probe 面扩展 Rule13/15/ReduceSpeed/stand-on（C5-round2）|

## 0. Scope & How To Read This Spec

本 spec 是 M5 Mid-MPC NLP **约束分类与 hard/soft split** 的权威设计，supersede v2 的约束相关章节（§5.5 / §7.1 / §9.3 / §13），其余 v2 章节（航线承诺、GNC 移交、COLREGs 恢复生命周期、M6 语义独占、SOTIF/policing）保持权威，本 spec 不重复。

**阅读顺序建议**：
- 实现者：§3 约束分类矩阵 → §4 约束公式 → §5 ROT 参数 → §6 spec 调和 → §7 测试与验证。
- 认证/架构审查：§2 缺陷证据链 → §6 spec 调和 → §8 非凸性论证调整。
- 上游模块（M6/M2）维护者：仅 §1 范围（本 spec 不扩 M6/M2 契约）。

**自洽性约定**（继承 v2 §0，新增约束分类相关术语）：
- `H_pred` = Mid-MPC 预测时域 = **90 s**（N=18, dt=5 s）。≠ 完整航线长度，≠ 完整 COLREG 避让生命周期。
- `rot_max` = NLP 单 step 最大航向变化率，packed at cycle origin = **4.7°/s**（design baseline，IMO MSC.137(76) 二次推导 🟡，[TBD-HAZID-2026-08-19] 校准候选）。**NLP rot_max 来源**：GNC `cruise_max_yaw_rate_deg_s`（mid_mpc_node.cpp:728，Fix F Stage 1-2），**非** `config/vessels/fcb_45m.yaml rot_max_curve`（后者仅 HAZID 物理模型，不参与 k* 计算）。
- `min_alt` = Rule 8/16 最小航向偏转（M6 packed）= 典型 **30°**（0.524 rad）。
- `k*` = min_alt reachable deadline step = `ceil(min_alt / rot_step) - 1`（含 off-by-one，§4.2）= ROT 4.7°/s, dt=5s, min_alt=30° 下 **k*=1**（5s 后 hard）。
- `tail-gate` = NLP 收敛后的 publish 前 acceptance gate（`tail_gate_turns_are_feasible` + `tail_gate_cpa_release_clear`，types.hpp:764-786），不是 NLP 内约束。
- 约束分类六类：**physical**（ROT/speed/decel）/ **prefix-eq** / **min_alt** / **CPA floor** / **direction** / **terminal**。
- CPA 名义术语（B6 修订，round-2 拍板）：
  - `cpa_hard_m`（unbumped）= NLP suffix-hard floor radius + **tail-gate release hard check radius**（round-2 B6 拍板：tail-gate 复用 `cpa_hard_m`，不新增 `cpa_release_m` 字段，避免 spec/代码/test 多处扩张）
  - `cpa_safe_m`（bumped, `=2500` during active conflict）= J_colreg soft barrier radius（cost-only，**不**用于任何 hard check）

## 1. Purpose

把 M5 NLP 从「单次 solve 完成全 COLREGs 避让」的过约束设计，改为「单次 solve 完成战术时域内的可达机动 + 全生命周期靠 rolling + tail-gate」。对齐 v2 §9.12 line 392-408（rolling append policy）与架构 §10.4 line 899-923（Mid-MPC 短时域战术层）。

**v2.1 新增目标**：
- **NLP 可行域可解性恢复**：移除结构性 INFEAS（min_alt all-k hard + CPA hard from k=0），改用物理可达的 hard/soft schedule。**注**（B1 修订）：不声称「NLP 可行域凸化」——`l[k]` 经 `cos/sin(psi)` 积分（formulation.cpp:236-239）使可行域本质上非凸；v2.1 只移除 CPA hard 这一类非凸硬约束，整体硬可行域非凸性不变（见 §8）。
- **物理可达性**：每类约束的 hard 时机由物理 deadline 决定（min_alt reachable schedule + CPA suffix-hard schedule），可审计（CCS/i-Ship）。
- **tail-gate 契约正式化**：types.hpp:770 的 CPA release skip 逻辑从「Bug C 修复」升级为 spec-mandated。
- **ROT 物理基线**：从 [TBD-HAZID] preliminary 12°/s 改为 IMO MSC.137(76) 二次推导 4.7°/s（🟡 derived engineering estimate，非 MSC.137 直接规定值；仍标 HAZID 校准候选）。

**本 spec 不做**（与 v2 §21 Non-Goals 一致，不重复）：
- 不扩 horizon（N=18, dt=5 锁定，v2 §12.0）。
- 不改 v2 §3.1 语义权威分工（M6 独占 side/role/past-clear）。
- 不调 w_colreg/w_dist/w_vel/k_asym 权重（v2 §9.3 锁定）。
- 不改 lifecycle 八态（v2 §0/§10）。
- 不 tune scenario geometry / forced-pass / vessel-specific 分支（AGENTS.md「COLREGs 全链路」硬规则）。
- 不在本会话实施代码（用户决策：先 spec 评审，代码留下个会话）。

## 2. 缺陷证据链（v2.1 触发理由）

### 2.1 rule14-ho probe 实测（Fix E + F 后，runs/fix_f*_rule14ho/）

| run | CPA min | SOLVER_CONVERGED | INFEAS |
| --- | --- | --- | --- |
| Fix E 基线 | 1.1m | 19 | 282 |
| Stage1-2（M5 ROT 1.2°/s） | 0.2m | 0 | 402 |
| +Stage3 M4 clamp | 3.4m | 0 | 394 |
| +0.3° epsilon margin | 0.9m | 0 | 388 |

Fix E（NLP ROT own_psi→psi[0] hard）+ Fix F（M4 box clamp + M5 ROT 对齐 GNC cruise 1.2°/s）正确实施 + 全单测 PASS，但 NLP 仍 100% Infeasible + CPA 穿透 0.2-3.4m。

### 2.2 三重根因（Codex task-mr53qtfa 决定性结论）

**根因 1 — min_alt hard all-k from k=0**：
```
g_minalt[k] = pref_dir·(psi[k] - own_psi) - min_alt ≥ 0   ∀ k ∈ [0,N)
```
源码：`src/.../mid_mpc/mid_mpc_nlp_formulation.cpp:523` built for ALL k。
K=0 时（first commit，无 prefix）`apply_colreg_prefix_soften_` 循环 `for k < cfg.K=0` 不执行 → 全 horizon hard。
Fix E + F 后 ROT=1.2°/s × 5s = 6°/step，但 min_alt=30° → k=0 即要求 30° 偏转，ROT 只允 6° → **结构性 INFEAS，与 ROT 值无关**。

**根因 2 — CPA floor hard all-k from k=0**：
```
g_rows[t·N + k] = dx² + dy² - cpa_safe² ≥ 0   per (target, step)
```
源码：`src/.../shared/constraint_compiler.cpp:331-356` per (target, step) hard。
低 ROT + 早期 step，本船未及开 CPA → 早期 k hard floor 不可达。
**注意**：types.hpp:770 的 tail-gate 在 target closing 时 skip CPA floor，但**这只在外层 gate 生效，NLP 内仍是全 hard**——两层不一致。

**根因 3 — terminal lateral hard at k=N-1**：
源码：`src/.../mid_mpc_nlp_formulation.cpp:540-543` 3 hard rows（side/lo/hi）at k=N-1。
90s horizon 不够完成避让 + 回归，terminal lateral 强 hard 会 INFEAS。

### 2.3 spec 内部矛盾（v2 §9.3 line 119/289 vs types.hpp:770）

- v2 line 119/123/289：「CPA hard 不可移除，非凸不可去」（架构 §10.4 line 919）
- types.hpp:770-785：「tail-gate target closing 时 skip CPA floor」（实现层已软化，但只在外层 gate）

实现层已在 release 阶段 skip，spec 仍写「不可移除」——spec 与实现不一致，违反 AGENTS.md「design change 完整性」规则。

## 3. 约束分类与 hard/soft 矩阵（核心）

| # | 约束类 | 当前（v2 + Fix A-F） | v2.1 改为 | 物理依据 |
| --- | --- | --- | --- | --- |
| 1 | ROT (own→psi[0] + inter-step) | hard all | **hard all** ✅ 保持 | 物理执行极限（Fix E） |
| 2 | speed bounds / speed_rate (decel) | hard all | **hard all** ✅ 保持 | 物理执行极限（Fix D-2） |
| 3 | prefix-equality (psi/u) | hard k<K | **hard k<K** ✅ 保持 | committed prefix 不可变（v2 §6.2） |
| 4 | **min_alt** | hard all k | **reachable schedule**（§4.2） | ROT 物理最早可达 |
| 5 | **CPA floor** | hard all k (NLP) | **suffix-hard schedule（k ≥ k_cpa 后 hard，前 soft）+ J_colreg barrier 驱动**（§4.3） | ROT 物理展开时间 + NLM 🟢 recursive feasibility 保证 |
| 6 | direction (wrong-side) | hard all k | **保持 hard all + diag probe**（§4.4） | k=0 时 l[0]=0 不冲突，待 probe 验证 |
| 7 | **terminal lateral** | hard 3 rows at k=N-1 | **NLP full soften + J_terminal upper-band cost（新增）+ tail-gate hard**（§4.5） | 90s 不够完成避让回归 + Doer-Checker 架构对齐 SOTIF/SIL2 |

**不变量**（v2 + 架构 §10.4 + AGENTS.md）：
- prefix-equality / ROT / speed / decel 全 horizon hard（物理极限，不改）。
- M6 owns side/role/past-clear；M2 owns CPA/TCPA 数值；M5 只做非语义几何可行性检查（v2 §3.1 不变）。
- L4/GNC owns ROT 执行，M5 在执行包络内规划（架构 §L4 不变）。
- 无 mock/skip/forced-pass/vessel-specific 分支（AGENTS.md 硬规则）。

## 4. 约束公式

### 4.1 ROT/speed/decel/prefix-eq（保持，仅引用）

不变，详见 v2 §6.2 + mid_mpc_nlp_formulation.cpp:413-484（Fix D-2 speed_rate + Fix E own→psi[0]）。全 horizon hard。

### 4.2 min_alt — Reachable Schedule

**row-time 语义**（C1 修订）：NLP ROT rows 允许 `|psi[0]-own_psi| ≤ rot_step`（formulation.cpp:425-434）+ inter-step rows `|psi[k+1]-psi[k]| ≤ rot_step`。因此 row k 的累计偏转上界 = `(k+1)·rot_step`（own→psi[0] 占 1 步 + k 个 inter-step）。最早满足 `min_alt` 的 row index：
```
k* = ceil(min_alt / rot_step) - 1
```
其中 `rot_step = rot_max · dt`。spec 用此公式（含 `-1`）以匹配 graph 数学。

**当前**（mid_mpc_nlp_formulation.cpp:520-523）：
```cpp
for k ∈ [0,N):
  g_minalt[k] = pref_dir · (psi[k] - own_psi) - min_alt
// RowRegistry: 全 horizon bounds [0, +inf]
```

**v2.1 改为**：
```cpp
const int32_t k_star = static_cast<int32_t>(
    std::ceil(cfg.min_alt_rad / (cfg.rot_max_rad_s * cfg.dt_s))) - 1;
const int32_t k_star_clamped = std::max(0, std::min(k_star, N));
for k ∈ [0,N):
  g_minalt[k] = pref_dir · (psi[k] - own_psi) - min_alt   // 表达式不变
// RowRegistry: bounds 按 k 分
//   k < k_star_clamped:  [-inf, +inf]   soft（J_asym cost 驱动偏转）
//   k ≥ k_star_clamped:  [0, +inf]      hard
```

**数值**（ROT=4.7°/s, dt=5s, min_alt=30°=0.524rad）：
- rot_step = 0.0820 × 5 = 0.410 rad = 23.5°
- ceil(0.524 / 0.410) = ceil(1.278) = 2
- k* = 2 - 1 = **1**
- k_star_clamped = 1

即 k=0 soft（J_asym 驱动），k≥1 hard（必须达 30°）。物理含义：rot_step 限下 earliest k=1 时 cumulative delta 可达 2·rot_step = 47° ≥ 30°，故 k=1 起 hard。**注**：单测须断言 hard 起始 row（k=0 soft, k=1 hard）。

**RowBoundConfig 扩展**（include/.../mid_mpc/row_registry.hpp）：
```cpp
struct RowBoundConfig {
  // ... existing ...
  int32_t minalt_hard_from_k{0};  // v2.1: reachable schedule deadline
};
```
RowRegistry 新增 `apply_minalt_reachable_schedule_()`：
```cpp
void apply_minalt_reachable_schedule_(const RowBoundConfig& cfg, BoundArray& b) const {
  for (int32_t k = 0; k < N_; ++k) {
    const bool hard = (k >= cfg.minalt_hard_from_k);
    const double lb = hard ? 0.0 : -kInf;
    const double ub = hard ? kInf : kInf;
    const std::size_t rm = static_cast<std::size_t>(min_alt_row(k));
    b.lbg[rm] = lb; b.ubg[rm] = ub;
  }
}
```

**退化语义**：`minalt_hard_from_k=0` → 全 hard（与 v2 等价，用于回归测试）。`minalt_hard_from_k=N` → 全 soft。

**B9 修订（round-2 ReduceSpeed/stand-on/non-lateral disable precedence）**：
reachable schedule 仅在 lateral COLREG active（give-way + pref_dir≠0 + non-HOLD/ReduceSpeed）时生效。precedence（高 → 低）：
1. `direction_disabled=true`（stand-on / HOLD / ReduceSpeed / pref_dir=0，源码 formulation.cpp:510-513 注释）→ **所有 direction + min_alt rows 全 disable [-inf,+inf]**，reachable schedule 不适用。
2. `direction_disabled=false`（lateral give-way）→ reachable schedule 按 `minalt_hard_from_k` 生效。

源码 `pack_parameters()` 对 ReduceSpeed 打包 `preferred_direction=0`，使 `g_minalt = 0·(psi-own) - min_alt = -min_alt`，必须靠 `direction_disabled=true` 禁用。§4.5 role matrix 的「non-lateral give-way (ReduceSpeed) terminal_disabled」同源。

### 4.3 CPA floor — Suffix-Hard Schedule（B5 + NLM 修订）

**设计依据**（B5 Codex + NLM colav_algorithms 🟢 High）：
- 原 v2.1 draft「NLP 全 soft barrier」被 test_mid_mpc_route_cost.cpp:230-333 实测推翻：`w_colreg·J_colreg > w_route·J_route + w_dist·J_dist` FALSE，soft barrier 驱动力 < route pullback，CPA 可能不开。
- NLM 调研三方案对比：
  - slack-variable：保 IPOPT 可行性，但「does not ensure safety nor RF」+ matrix banded structure 破坏
  - pure soft barrier：与 B5 一致，弱于 route pullback
  - **suffix-hard（receding constraint）**：「guarantees recursive feasibility (RF) and strict safety bounds」+ 「allows maneuver time to develop」+ 「Lemma 1: safety constraints never violated once trajectory enters safe set」
- NLM sources: [Kerrigan 2000 CUED soft constraints](http://www-control.eng.cam.ac.uk/Homepage/papers/cued_control_53.pdf)、[acados running constraint](https://discourse.acados.org/t/running-constraint-on-a-specific-node-of-the-mpc-horizon/2016)、[Johansen ICRA'18 MPC COLREGs](https://torarnj.folk.ntnu.no/icla18.pdf)。

**当前**（constraint_compiler.cpp:309-357）：
```cpp
for k ∈ [0,N), for t ∈ [0,Nt):
  g_rows[t·N+k] = dx² + dy² - cpa_hard²     // cpa_hard_m, unbumped (B6)
// bounds [0, +inf]  全 hard from k=0
```

**v2.1 改为 suffix-hard schedule**：
- 表达式不变（CPA floor 表达式保留在 g_rows，供 active-set 日志）
- bounds 按 k 分（receding constraint）：
```
k_cpa = clamp(max(k_minalt, k_tcpa_margin), 0, N)
  其中:
    k_minalt = ceil(min_alt / rot_step) - 1   (同 §4.2，min_alt 达到后本船才有偏转开 CPA)
    k_tcpa_margin = ceil(min(tcpa_s_primary, t_cap) / dt) - margin_steps
      tcpa_s_primary = primary target 的 TCPA（M2 TargetState.tcpa_s, 取 min over active targets）
      t_cap = N · dt = 90s（horizon cap）
      margin_steps = 1（保守，留 1 step buffer）
  k < k_cpa:  bounds [-inf, +inf]   soft（J_colreg exp barrier 驱动 opening）
  k ≥ k_cpa:  bounds [0, +inf]      hard（CPA floor 强制）
```
- **target closing phase 特殊处理**（B5-round2 safety 论证条件化）：
  当 primary target `closing_speed_mps > 0`（active-approach，CPA 在下降），`k_cpa` 后 hard floor 仍可能物理不可达（本船偏转速度 < target closing 速度）。此时：
  - NLP 内：`k ≥ k_cpa` 仍 hard，但 solver 若 INFEAS，走 IPOPT infeasible exit → fallback（不强行解）
  - tail-gate（types.hpp:764-786）：target closing 时 skip CPA floor check（maneuver 本身即 CPA-opening action）；target opening 时 hard check `terminal_cpa - 3σ ≥ cpa_hard_m`（B6 拍板：复用 `cpa_hard_m`，不新增 `cpa_release_m`）
  - **RF 声明条件化**（B5-round2）：NLM 🟢 的「recursive feasibility 保证」仅在「suffix-hard 可达」前提下成立——即本船 ROT 能力 + target 几何允许 k_cpa 后 CPA floor 可达。若 target closing 速度使 k_cpa 后仍不可达，NLP 报 INFEAS（这是正确行为，触发 fallback/DegradedHold），不声称无条件 RF。

- **C2-round2 row order 修正**：CPA row 索引源码是 `cpa_row(t,k) = cpa_start + k·n_targets + t`（row_registry.hpp:137-138，outer loop k / inner loop t）。spec 与源码一致，**不**写 `t·N+k`。`apply_cpa_suffix_hard_` 循环：`for k: for t: cpa_row(t,k)`。

**正式化**：types.hpp:770 的注释从「Bug C 修复」升级为「v2.1 §4.3 spec-mandated」：
```cpp
// v2.1 §4.3: CPA floor is a RELEASE concern (is the target finally clear?),
// not an active-avoidance concern. NLP-internal CPA rows are suffix-hard:
// soft (J_colreg barrier) for k < k_cpa while maneuver develops, hard for
// k >= k_cpa. tail-gate enforces hard CPA check only when target is opening
// (release/recovery), using cpa_release_m (not cpa_safe_m). Spec reference:
// docs/superpowers/specs/2026-07-04-m5-nlp-constraint-restructure-design-v2.1.md §4.3
```

**RowBoundConfig 扩展**（B8-round2 修订：加 `*_override_valid` bool 区分 explicit 0 vs default）：
```cpp
struct RowBoundConfig {
  // ... existing K/colreg_prefix_softened/terminal_disabled/direction_disabled ...
  // v2.1 §4.2/§4.3: reachable schedule deadlines
  int32_t minalt_hard_from_k{0};   // spec default 0 = legacy hard (regression baseline)
  bool    minalt_override_valid{false};  // true = explicit caller override; false = solver derive
  int32_t cpa_hard_from_k{0};      // spec default 0 = legacy hard (regression baseline)
  bool    cpa_override_valid{false};
};
```
**B8-round2 优先级**（可实现）：
- caller 传 `minalt_override_valid=true` → 用 caller 值（even if 0 = legacy hard）
- caller 传 `minalt_override_valid=false`（default）→ solver auto-derive 从 `input.rot_max_rad_s` + `input.colregs_min_alteration_rad` + `input.targets[].tcpa_s`（§4.3 k_cpa 公式）
- spec default（无 caller override + derive 失败）→ `minalt_hard_from_k=0` + `cpa_hard_from_k=0`（v2 等价，最保守）

**RowRegistry 新增**（C2-round2 row order：outer k / inner t）：
```cpp
void apply_cpa_suffix_hard_(const RowBoundConfig& cfg, BoundArray& b) const {
  // cpa_row(t,k) = cpa_start + k*n_targets_ + t  (row_registry.hpp:137-138)
  for (int32_t k = 0; k < N_; ++k) {
    const bool hard = (k >= cfg.cpa_hard_from_k);
    const double lb = hard ? 0.0 : -kInf;
    const double ub = kInf;  // CPA floor is one-sided lower bound
    for (int32_t t = 0; t < n_targets_; ++t) {
      const std::size_t r = static_cast<std::size_t>(cpa_row(t, k));
      b.lbg[r] = lb; b.ubg[r] = ub;
    }
  }
}
```

### 4.4 direction — 保持 hard + diag probe 条款

**当前**（mid_mpc_nlp_formulation.cpp:518-522）：全 horizon hard。

**v2.1 临时保留**：保持全 horizon hard。

**理由**：
- k=0 时 l[0] = own cross-track = (pos[0] - route_origin) · n_hat。rule14-ho 起始 own 在 route line → l[0] = 0 → pref_dir · 0 = 0 ≥ 0 满足。
- direction 语义即「不可穿 wrong side」，应全程强制。
- rule14-ho 当前 INFEAS 主因是 min_alt（4.2）非 direction。

**diag probe 条款**（C3 修订：implementation-only，merge 前移除）：
实现期加临时 NLP g(x*) row 残差 dump，跑一次 rule14-ho probe 收集 direction rows 各 k 残差。**这是 implementation-only diagnostic hook，非 spec-mandated 永久行为**——验证完成后（确认 direction 保持 hard 或决定软化）须从 production 代码移除，不得留作 diag-only pass path（AGENTS.md「无 mock/skip/forced-pass」规则）。
- 若 direction rows（`g_direction` 各 k 值）全 0 或不活跃 → 确认保持 hard，移除 hook。
- 若有非 0 残差且活跃 → 下个会话软化（同 min_alt reachable schedule 模式），移除 hook。

### 4.5 terminal lateral — Full soften + J_terminal upper-band cost + tail-gate hard（B7 + NLM 修订）

**设计依据**（B7 Codex + NLM colav_algorithms 🟢 High）：
- 原 v2.1 draft「NLP 全 soft + tail-gate hard」设计本身正确（NLM Option 2 最优，对齐 Doer-Checker SOTIF/SIL2）。
- 但 B7 暴露两个 implementation gap：
  1. `build_terminal_cost_`（formulation.cpp:244-260）只 penalize wrong-side（lower），注释明写 upper bound 由 §5.5 hard rows 处理。全 soft 后 upper-band 无 NLP pressure。
  2. `tail_gate_terminal_lateral_feasible` helper 无调用点（types.hpp:924 现 `accept_tail_gate` 5 check 无 lateral band）。
- NLM 排除其他选项：Option 1（split same-side hard）「仍把 safety-critical hard 约束放在不可审计非凸 NLP，SIL2 auditability 失败」；Option 3（control-invariant set）「nonlinear non-convex dynamic obstacles 下 computational intractable」；Option 4（no terminal）「90s 太短 short-sighted，违 COLREGs」。

**当前**（mid_mpc_nlp_formulation.cpp:536-543）：3 hard rows at k=N-1（side/lo/hi）。

**v2.1 改为（保设计，补 gap）**：
- **NLP 内**：3 rows bounds 改 `[-inf,+inf]`（全 soft，对所有 role——C4 matrix 见下）。
- **J_terminal 扩展**（B7-round2 修订：two-sided smoothsoftplus，无 abs kink）：
  原 round-1 draft 写 `softplus((|l| - l_max)/...)` 在 l=0 不可导（abs kink），违反 v2 §5.4「max/abs kink 删除」原则。改为 two-sided smoothsoftplus（同 lower-band 模式）：
```cpp
// 现有（保）：J_terminal_lower = give_way · τ_t · softplus(-preferred_direction · l[N-1] / l_scale / τ_t)
// 新增（B7-round2）：upper-band two-sided smoothsoftplus（无 abs）
//   J_terminal_upper = τ_t · [softplus((l[N-1] - l_max)/l_scale/τ_t)
//                         + softplus((-l[N-1] - l_max)/l_scale/τ_t)]
//   两项分别在 l > +l_max 和 l < -l_max 时激活，smooth，C∞ 可导
// 复用现有 terminal_tau（cfg_.terminal_tau, mid_mpc_nlp_formulation.hpp:125）+ l_scale（kIdxLateralScale）+ l_max_feasible_m（cfg_.terminal_l_max_feasible_m）
//   → 不新增 cfg 字段（B7-round2 参数定义）
J_terminal = give_way_role · (J_terminal_lower + J_terminal_upper)
// give_way_role gate 同现有（mid_mpc_nlp_formulation.cpp:256-260, role-based）
```
- **tail-gate 接线**（B7-round2 gap 2 + C2 + C4-round2 role guard）：
  `accept_tail_gate`（types.hpp:924）新增第 6 项 check，**使用现有 `trajectory_terminal_lateral_offset_m(point, route_brg)` 函数**（types.hpp:687，**不**用不存在的 `cross_track_m` 字段——round-2 暴露 TrajectoryPoint 无此字段）：
```cpp
inline bool tail_gate_terminal_lateral_feasible(
    const MidMpcSolution& solution,
    double route_brg_rad,           // 现有 trajectory_terminal_lateral_offset_m 签名
    double pref_dir,                // COLREGs side（M6）
    bool   lateral_colreg_active,   // C4-round2: stand-on/ReduceSpeed/non-lateral → skip
    double l_min_feasible_m,
    double l_max_feasible_m) {
  if (!lateral_colreg_active) return true;  // C4-round2 role guard: non-lateral 不检查
  if (solution.trajectory.empty()) return true;
  const double lN = trajectory_terminal_lateral_offset_m(
      solution.trajectory.back(), route_brg_rad);  // 现有函数
  // COLREGs same-side（lower bound，语义不可去）+ lateral band（upper bound）
  if (pref_dir * lN < l_min_feasible_m) return false;  // wrong side or insufficient
  if (lN < -l_max_feasible_m || lN > l_max_feasible_m) return false;  // out of band
  return true;
}
// 接线点：types.hpp accept_tail_gate() 加第 6 项调用
//   lateral_colreg_active = (primary_role == give-way lateral) && (pref_dir != 0) && !HOLD && !ReduceSpeed
//   reject reason = "terminal_lateral_out_of_band"
```

**C4 stand-on/give-way/legacy matrix**（明确化）：

| role | terminal NLP rows | J_terminal cost（B7-round2） | tail-gate lateral check（C4-round2）|
| --- | --- | --- | --- |
| stand-on | disabled（`terminal_disabled=true`）| 不 gate（give_way_role=false，lower + upper 都 off）| skip（`lateral_colreg_active=false`）|
| give-way lateral（Rule 13/14/15/16）| soft（`terminal_nlp_soft=true`）| lower + upper（give_way_role gate）| **hard check**（`lateral_colreg_active=true`）|
| give-way non-lateral（ReduceSpeed）| disabled（`terminal_disabled=true`）| 不 gate（pref_dir=0）| skip（`lateral_colreg_active=false`）|
| legacy/degrade（`terminal_nlp_soft=false`）| hard（v2 等价）| lower only（upper 不激活，因 hard rows 已强制）| hard check（v2 等价）|

**RowBoundConfig 扩展**：
```cpp
struct RowBoundConfig {
  // ... existing + minalt_hard_from_k + cpa_hard_from_k ...
  bool terminal_nlp_soft{true};  // v2.1: NLP-internal terminal → all soft (give-way default); legacy=false
};
```

**`apply_terminal_disable_` 位置修正**（I2）：在 `row_registry.hpp:249`（非 types.hpp，原 draft 误标）。v2.1 复用此函数实现「全 soft」（bounds → [-inf,+inf]），调用条件扩为 `terminal_disabled || terminal_nlp_soft`。

**退化语义**：`terminal_nlp_soft=false` → hard（v2 等价，回归测试）。

## 5. ROT 参数定标

### 5.1 选值：4.7°/s（design baseline，derived engineering estimate）

**来源**（🟡 Medium，B 级 derived engineering estimate，**非** IMO MSC.137 直接规定值）：

**B2 修订（Codex adversarial 暴露）**：原 draft 称「IMO MSC.137(76) 推导 🟢 A 级」过强。MSC.137(76) 实际只规定操纵性试验**标准**（turning circle ≤ 5L、10°/10° zig-zag overshoot、20°/20° zig-zag overshoot、stopping），**不直接给 ROT 公式或数值**。且 MSC.137 主要适用 L≥100m 船舶（45m FCB 在适用边界外）。spec 的 4.7°/s 是基于 MSC.137 试验框架的**二次推导**。

推导透明化：
- IMO MSC.137(76) 试验框架：35° rudder → measure tactical diameter + heading change time
- 假设：tactical diameter ≤ 5L = 225m（L=45m），v=8kn（4.11 m/s）
- ROT ≈ v / R，R = tactical_diameter / 2 = 112.5m
- ROT ≈ 4.11 / 112.5 = 0.0365 rad/s = **2.09°/s**
- 若用 10°/10° zig-zag 标准（更小 R）：ROT 升至 ~4.7°/s
- 综合（含 turning phase + second gyro）：design baseline 4.7°/s，**置信度 🟡**（推导 + 相似船实测验证，非规范直接值）

**HAZID 校准前不应声称 4.7°/s 是 IMO 规定值**。HAZID 后应替换为实船 zig-zag 试验数据或 MMG 水动力模型推导值。

**对比矩阵**：

| 源 | ROT | 评价 | 置信度 |
| --- | --- | --- | --- |
| M5 vessel_model（preliminary） | 12°/s | [TBD-HAZID] 偏乐观 2.5× | 🔴 待验证 |
| GNC cruise（comfort） | 1.2°/s | 10× 保守 | 🟡 经验值 |
| GNC emergency | 2.0°/s | 仍低于 IMO 推导 | 🟡 经验值 |
| **IMO MSC.137(76) 二次推导** | **4.7°/s** | **本 spec 选值** | 🟡 B 级 derived estimate |
| 36.4m RoRo 实测（NLM ship_maneuvering） | 1.5-5.5°/s | 支持范围 | 🟡 C 级实船 |
| handoff 综合判断 | 3-5°/s | 4.7 在范围内 | 🟡 |

### 5.2 改动文件（B3 + B4 修订：实际路径 + emergency 4.7 统一）

**B4 修订（Codex 暴露配置路径全错）**：
- `config/fcb_vessel_capability.yaml` **不存在**；实际是 `config/vessels/fcb_45m.yaml`
- 字段不是 `rot_max_at_18kn: 12.0`；实际是 `rot_max_curve:` 列表（[0kn,8°/s] / [10kn,6°/s] / [22kn,3°/s]）
- GNC ship_config 路径是 `third_party/gnc_ws/src/platform/ship_bringup/config/`（非 `ship_guidance/config/`）
- 还有 fast10 overlay（`ship_config_fast10.yaml`）

**B3 修订（emergency 4.7 统一）**：
- GNC `active_route_manager_node.cpp:121-123` 强制 `emergency_max_yaw_rate = max(cruise, emergency_YAML)`
- 原 draft 写「emergency 保持 2.0」错——cruise 改 4.7 后 published emergency 自动 = max(4.7, 2.0) = 4.7
- 用户决策：所有 ROT 出处统一为 4.7°/s（cruise + emergency YAML 都改）

| 文件 | 当前 | 改为 | 备注 |
| --- | --- | --- | --- |
| `config/vessels/fcb_45m.yaml:7-10` | `rot_max_curve: [[0,8],[10,6],[22,3]]` ⚫ HAZID-unverified | `rot_max_curve: [[0,4.7],[10,4.7],[22,4.7]]` 或保持曲线 shape 但 baseline 校准至 4.7 平均 | 注释加「design baseline = MSC.137(76) 二次推导 🟡，HAZID 校准候选」+ MMG D1.3a 验证 TBD |
| `third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml:638` | `max_yaw_rate_deg_s: 1.2` (cruise) | `max_yaw_rate_deg_s: 4.7` | 与 M5 vessel_model 对齐 |
| `ship_config.yaml:639` | `emergency_max_yaw_rate_deg_s: 2.0` | `emergency_max_yaw_rate_deg_s: 4.7` | B3：与 cruise 统一（GNC clamp `max(cruise,emergency)` 实际生效值 = 4.7）|
| `ship_config_fast10.yaml:560` | `max_yaw_rate_deg_s: 1.2` | `max_yaw_rate_deg_s: 4.7` | fast10 overlay 同步 |
| `ship_config_fast10.yaml:561` | `emergency_max_yaw_rate_deg_s: 2.0` | `emergency_max_yaw_rate_deg_s: 4.7` | fast10 overlay 同步 |

**fcb_45m.yaml rot_max_curve 处理说明**：当前是 speed-dependent 曲线（低速高 ROT、高速低 ROT）。spec baseline 4.7°/s 是 speed-averaged 估计。实施时可选：(a) flatten 为常数 4.7；(b) 保持曲线 shape 但 scale 至 4.7 平均；(c) 等 MMG D1.3a 水动力验证（[TBD-D1.3a]）。推荐 (b) 保 shape + scale，但本 spec 不强制——实施会话决定。

### 5.3 HAZID 状态

仍标 `[TBD-HAZID-2026-08-19]`，但 spec 注明「design baseline = IMO MSC.137 推导 4.7°/s，HAZID 后可调」。reachable schedule 公式自适应 ROT 值——HAZID 调值只需改 5.2 表中三处 YAML，k* 自动重算，不需重构代码。

### 5.4 不影响 v2 horizon policy

v2 §12.0 锁 N=18, dt=5s（H_pred=90s）。v2.1 不改 horizon。ROT 4.7°/s × 90s = 423° 累计偏转能力，远超 min_alt=30° 需求（k*=1 后剩余 N-k*=17 step 可用）。

**B3/B4-round2 ROT 来源明确**：NLP `rot_max` 来自 GNC `cruise_max_yaw_rate_deg_s`（mid_mpc_node.cpp:728，Fix F Stage 1-2）。`config/vessels/fcb_45m.yaml rot_max_curve` 是 HAZID 物理模型（MMG D1.3a 验证候选），**不参与 NLP k* 计算**。故：
- §5.2 改 GNC ship_config `max_yaw_rate_deg_s`（cruise）+ `emergency_max_yaw_rate_deg_s` = 4.7 → NLP rot_max = 4.7
- `fcb_45m.yaml rot_max_curve` 是物理模型 reference，spec 不强制 flatten/scale（留 HAZID + D1.3a）
- §4.2 k* = 1（基于 NLP rot_max = 4.7°/s，GNC cruise source）

## 6. spec 调和与文档更新

### 6.1 v2 章节 supersede 映射

| v2 章节 | v2 内容 | v2.1 改为 | 实施方式 |
| --- | --- | --- | --- |
| §5.5 terminal | 「3 hard rows at k=N-1」 | 「NLP 内 full soften + J_terminal upper-band cost（B7 新增）+ tail-gate hard」 | 本 spec §4.5 supersede |
| §7.1 min_alt | 未明说 hard all-k（隐含 hard all） | 「reachable schedule `k ≥ k*=ceil(min_alt/rot_step)-1` 后 hard，前 soft」（C1 off-by-one 修正）| 本 spec §4.2 supersede |
| §7.1 direction | 全 hard | 保持 hard + implementation-only diag probe 条款（merge 前移除） | 本 spec §4.4 supersede |
| §9.3 line 289 CPA hard | 「CPA hard 不可移除，非凸不可去」 | 「CPA floor 在 NLP 内 suffix-hard（k ≥ k_cpa 后 hard，前 soft），tail-gate release hard check」 | 本 spec §4.3 supersede |
| §13.1-§13.3 非凸论证 | NLP 内 CPA hard 致非凸 | NLP 移除 CPA hard all-k 这一类非凸硬约束（移到 suffix-hard + tail-gate），**整体硬可行域仍非凸**（B1：l 经 cos/sin 积分）| 本 spec §8 重写（C5：仅 §13.1-13.3，**§13.4 SOTIF/policing 不变**）|

### 6.2 架构报告同步

| 架构章节 | 当前 | v2.1 后 | 改动 |
| --- | --- | --- | --- |
| §10.4 line 919 | CPA hard（行 919） | 引用 v2.1 §4.3：NLP 内 soft + tail-gate hard | 措辞修订（per AGENTS.md「edit only relevant chapter」） |
| §10.4 line 899-923 | Mid-MPC 短时域战术层 | 保持，引用 v2.1 强化 | 仅加 spec 指针 |

### 6.3 实施时文档改动清单

- [ ] v2 spec revision history 加 v2.1 entry（指针，不重写 v2 主体）
- [ ] v2 spec §5.5 / §7.1 / §9.3 / §13 加「v2.1 supersede」标记 + 指针
- [ ] 架构报告 §10.4 措辞修订（仅 CPA hard 行）
- [ ] `TDL-Kernel/M5-*/M5-progress.md` 加 v2.1 设计条目
- [ ] `handoff/workspace_log.md` append-only entry（本会话产出）
- [ ] types.hpp:770 注释从「Bug C」改「v2.1 §4.3 spec-mandated」（实施 commit）

### 6.4 不改动的文档

- v2 spec 主体（只加指针 + revision history）
- M6/M4/M2 章节（本 spec 不扩上游契约）
- handoff/workspace_log.md（append-only，AGENTS.md 规则）

## 7. 测试与验证

### 7.1 单测矩阵（spec §15 新增，留下个会话执行；B5/B7/C1 修订后）

| 测试 | 范围 | 断言 |
| --- | --- | --- |
| `test_minalt_reachable_schedule_bounds` | ROT=4.7, min_alt=30°, k*=1 边界（C1）| k<1 (k=0) bounds [-inf,+inf]，k≥1 bounds [0,+inf] |
| `test_minalt_k0_no_longer_infeasible` | rule14-ho fixture, ROT=4.7, min_alt=30° | k=0 不 INFEAS（regression for Fix E） |
| `test_minalt_degrade_to_hard` | `minalt_hard_from_k=0` | 全 hard（与 v2 等价，回归） |
| `test_cpa_suffix_hard_schedule` | k_cpa=2 边界 | k<2 bounds [-inf,+inf]，k≥2 bounds [0,+inf]（B5 NLM suffix-hard）|
| `test_cpa_suffix_hard_k0_no_longer_infeasible` | rule14-ho fixture, ROT=4.7, low early-step CPA | k=0 不 INFEAS（CPA suffix-soft while maneuver develops）|
| `test_cpa_tail_gate_release_hard` | target opening + terminal CPA < cpa_release_m | tail-gate reject |
| `test_cpa_tail_gate_closing_skip` | target closing + terminal CPA < floor | tail-gate accept（maneuver 是 opening action） |
| `test_cpa_degrade_to_hard` | `cpa_hard_from_k=0` | 全 hard from k=0（v2 等价，回归）|
| `test_terminal_soft_nlp_no_upper_band_infeas` | terminal lateral out-of-band | NLP 不 INFEAS（full soft）|
| `test_terminal_j_cost_upper_band_pressure` | l > l_max | J_terminal upper-band smooth cost 非零驱动 reduce（B7 gap 1 修复）|
| `test_terminal_tail_gate_lateral_wired` | lateral out-of-band | tail-gate reject reason = `terminal_lateral_out_of_band`（B7 gap 2 + C2 接线）|
| `test_terminal_role_matrix` | stand-on / give-way / non-lateral / legacy（C4）| bounds + cost + tail-gate 按 §4.5 matrix 正确 |
| `test_rot_4_7_reachable_k` | rot=4.7, min_alt=30° | k* = 1（C1 off-by-one）|
| `test_direction_diag_probe_hook_impl_only` | diag dump hook 存在 + production 默认 off（C3）| g_dir 各 k 残差可输出，但需 explicit flag |

### 7.2 probe 验证（rule14-ho, GNC profile, sim-rate 5）

通过条件（C5-round2 扩展 probe 面）：
- rule14-ho（主 probe）：NLP SOLVER_CONVERGED 占比 > 30%（当前 0%）+ CPA min ≥ 180m（当前 0.2-3.4m）
- direction diag dump 各 k 残差记录（决定下个会话 direction 是否软化）
- **新增 targeted probes**（C5-round2，覆盖 spec 改动面）：
  - Rule13 overtake give-way（suffix-hard + min_alt reachable）
  - Rule15 crossing give-way（suffix-hard + terminal role matrix）
  - ReduceSpeed non-lateral（direction_disabled precedence B9）
  - stand-on（terminal_disabled + tail-gate skip C4）

**不做的验证**（per AGENTS.md「诚实」+「COLREGs 全链路」规则）：
- 不声称 COLREGs 全链路 GREEN（只验证 rule14-ho）。
- 不调 w_colreg/w_dist/w_vel/k_asym 权重（v2 §9.3 + 用户硬规则）。
- 不 tune scenario 几何让 probe 绿。
- direction 软化待 probe 数据。
- 不在代码实施前跑 probe（本会话只产 spec）。

### 7.3 退化兼容性测试（C3-round2 修订：flag 名修正 + schedule boundary matrix）

flag 字段（与 §4.2/§4.3/§4.5 一致）：
- `minalt_hard_from_k`（int32_t，spec default 0 = legacy hard）+ `minalt_override_valid`（bool）
- `cpa_hard_from_k`（int32_t，spec default 0 = legacy hard）+ `cpa_override_valid`（bool）
- `terminal_nlp_soft`（bool，spec default true = v2.1 new）

**C3-round2 schedule boundary matrix**（非 2³ bool，覆盖 int 边界）：

| 测试 | minalt_hard_from_k | cpa_hard_from_k | terminal_nlp_soft | 断言 |
| --- | --- | --- | --- | --- |
| `test_degrade_all_to_v2` | 0 (override) | 0 (override) | false | 全 v2 等价（min_alt + CPA + terminal 全 hard）|
| `test_minalt_boundary_k0_k1_kN` | 0 / 1 / N | (derive) | true | k=0,1,N 边界 bounds 正确切换 |
| `test_cpa_boundary_k0_k1_kN` | (derive) | 0 / 1 / N | true | k=0,1,N 边界 bounds 正确切换 |
| `test_override_valid_false_uses_derive` | (ignored) | (ignored) | true | override_valid=false → solver auto-derive |
| `test_override_valid_true_uses_explicit` | 5 (override) | 3 (override) | false | 用 explicit 值（even if 非 derived）|
| `test_terminal_soft_hard_toggle` | (derive) | (derive) | true / false | soft/hard bounds 切换正确 |

## 8. 非凸性论证调整（v2 §13.1-§13.3 supersede；C5：§13.4 SOTIF 不变）

### 8.1 v2 §13 原论证（部分失效）

v2 §13 line 289：「CPA 硬约束 `dx²+dy² ≥ cpa²`（constraint_compiler.cpp:313）使可行域非凸（'圆外部'）。这是架构 §10.4 行 919 规定的硬约束，非凸不可去。」

v2.1 后：CPA hard 从 NLP 内的「all-k from k=0」改为「suffix-hard（k ≥ k_cpa）」，移除了早期 step 的非凸硬约束。但 **整体硬可行域仍非凸**（B1 修订）。

### 8.2 v2.1 非凸性来源（B1 重写——不声称凸化）

**B1 修订（Codex 暴露）**：原 draft 称「NLP 可行域凸化」错。`l[k]`（cross-track）经 `cos/sin(psi)` 积分（formulation.cpp:236-239），`direction` 约束 = `pref_dir · l[k] ≥ 0` 中 l[k] 是 psi 的非线性函数 → direction hard 约束本身贡献非凸性。架构 §10.4 line 910 也明确「位置由 ψ/u 经 cos/sin 积分（非线性耦合）」。

NLP 非凸性来源（v2.1 后）：
1. **可行域非凸源**：
   - `direction` hard（`pref_dir · l[k] ≥ 0`，l 是 cos/sin 积分）— 非凸
   - `min_alt` hard for k ≥ k*（`pref_dir · (psi[k]-own_psi) ≥ min_alt`，线性 in psi）— 凸
   - `CPA floor` hard for k ≥ k_cpa（`dx²+dy² ≥ cpa_hard²`，圆外部）— 非凸
   - ROT/speed/decel/prefix-eq — 凸（线性）
   - `terminal` NLP 全 soft（移出可行域）— 不贡献
2. **cost 非凸源**（不变）：J_colreg exp barrier + sqrt(d²) + J_asym softplus + J_terminal softplus + B7 新增 upper-band softplus。

**结论（B1 修订）**：v2.1 后 NLP **可行域仍非凸**（direction + suffix-hard CPA），但相比 v2 移除了「CPA hard from k=0」这一类早期 step 的非凸硬约束，降低 INFEAS 风险。非凸性仍由 policing-function（M7 SOTIF + tail-gate）兜底（§13.4 不变）。**不声称「凸化」**。

### 8.3 SOTIF / policing 不变（C5：§13.4 保持权威）

v2 §13.4 SOTIF/policing-function 论证不变（M7 独立 checker + X-axis veto + tail-gate defense-in-depth）。CPA floor 的 hard check 从「NLP 内 all-k」改为「NLP 内 suffix-hard + tail-gate release hard」，仍是确定性 policing function，不弱化安全边界。supersede 范围仅 §13.1-§13.3 非凸论证，**§13.4 SOTIF 不变**。

## 9. Open Items（下个会话）

- [ ] 代码实施 §4.2-4.5（min_alt reachable / CPA soft / terminal soft / direction probe）
- [ ] ROT 参数 YAML 改动（§5.2）
- [ ] 单测矩阵实施（§7.1）
- [ ] rule14-ho probe 验证（§7.2）
- [ ] direction diag probe 数据收集 + 软化决策（§4.4）
- [ ] 文档改动清单（§6.3）
- [ ] GNC rebuild（ship_config cruise 4.7 + 需 `--cmake-clean-cache` + touch cpp 强制重编，per handoff）
- [ ] M5/M4 colcon build + 全单测
- [ ] Codex 评审（per AGENTS.md「兼听则明」，代码实施完成后）

## 10. Source Coordinates

- NLP 约束实现：`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:410-577`（build_constraints_）
- min_alt 当前：同上 :520-523
- CPA floor 当前：`src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp:309-357`
- terminal 当前：`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp:536-543`
- RowRegistry：`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp:180-259`
- RowBoundConfig：同上 :180-187（待扩字段）
- tail-gate CPA：`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp:764-786`
- tail-gate ROT：同上 :788+
- ROT 当前源：`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:728`（Fix F Stage 1-2 改读 GNC cruise）
- GNC cruise YAML（B4 修订实际路径）：`third_party/gnc_ws/src/platform/ship_bringup/config/ship_config.yaml:638`（非 `ship_guidance/config/`）
- GNC fast10 overlay：`third_party/gnc_ws/src/platform/ship_bringup/config/ship_config_fast10.yaml:560`
- GNC emergency clamp：`third_party/gnc_ws/src/gnc/ship_guidance/src/active_route_manager_node.cpp:121-123`（`emergency = max(cruise, emergency_YAML)`）
- M5 vessel_model（B4 修订实际路径）：`config/vessels/fcb_45m.yaml:7-10`（`rot_max_curve`，非 `rot_max_at_18kn`）
- v2 spec：`docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md`
- 架构报告：`docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §10.4 line 899-923
- Codex 决定性分析：task-mr53qtfa（输出在 `/private/var/folders/v8/fdl1682s09xgrbmlk51sjtxc0000gp/T/codex-companion/colregs-12probe-debug-25f90e3311160839/jobs/task-mr53qtfa-ba0dgz.log`）
- handoff 末条：`handoff/workspace_log.md`（`[2026-07-03] ZCode / f4ad6bab..55554ab8`）
- rule14-ho probe 证据：`runs/fix_e_diagf_rule14ho/`、`runs/fix_f*_rule14ho/`、`runs/fix_f2_diag/`

## 11. Decision Record（v2.1 — 4 用户决策 + 5 Codex/NLM 修订追溯）

| # | 议题 | 决策 | 依据 | spec 落地 |
| --- | --- | --- | --- | --- |
| 1 | 约束重构范围 | **Codex 完整方案**（5 类约束全改） | 用户拍板；Codex task-mr53qtfa verdict (d) | §3 矩阵、§4 公式 |
| 2 | ROT 定标 | **4.7°/s**（IMO MSC.137(76) 二次推导 🟡 B 级，**非** A 级直接规定值） | 用户拍板；B2 修订降级；NLM ship_maneuvering 🟡 范围支持 | §5 全节 |
| 3 | spec v2 line 119/289 vs types.hpp:770 矛盾 | **spec 改写：CPA suffix-hard（k≥k_cpa）+ tail-gate release hard** | 用户原选「CPA hard 仅 release/suffix gate」；Codex B5 + NLM 🟢 进一步收敛到 suffix-hard | §4.3、§6.1、§8 |
| 4 | 本会话目标 | **先 spec + 设计，代码留下个会话** | 用户拍板；brainstorming skill 流程 | 本 spec 全文 + §9 Open Items |
| 5 | direction 约束 | **保持 hard + implementation-only diag probe**（merge 前移除） | 用户选「待 probe 数据决定」；C3 修订状态 | §4.4、§7.2 |
| 6 | 实施变体 | **Approach A**（物理语义分阶段，公式驱动） | 用户拍板；每约束独立物理公式可审计 | §3、§4 |
| 7 | CPA floor 设计（B5 修订） | **Suffix-Hard Schedule**（k≥k_cpa 后 hard，前 J_colreg barrier soft） | Codex B5（test 实测 soft barrier 弱于 route pullback）+ NLM colav_algorithms 🟢（RF 保证 + COLREGs safety）| §4.3 重写 |
| 8 | terminal lateral 设计（B7 修订） | **保 full soften + tail-gate hard**（NLM Option 2），补 J_terminal upper-band cost + tail-gate 接线 | Codex B7（暴露 2 implementation gap）+ NLM 🟢（Doer-Checker SOTIF 最优）| §4.5 重写 |
| 9 | GNC emergency ROT（B3 修订） | **所有 ROT 出处统一 4.7°/s**（cruise + emergency YAML 都改） | 用户拍板；GNC `emergency = max(cruise, emergency_YAML)` clamp 实际生效值 = 4.7 | §5.2 |

## 12. References

- [R1] IMO MSC.137(76) Resolution「Standards for ship manoeuvring」（2002）—— 操纵性试验标准框架（turning circle / zig-zag / stopping），**不直接给 ROT 数值**，4.7°/s 是二次推导（🟡 B 级 derived estimate）。L≥100m 适用边界，45m FCB 在边界外。
- [R2] Codex task-mr53qtfa「M5 NLP horizon vs COLREG avoidance lifecycle」（2026-07-03）—— 约束重构决定性分析（首轮）。
- [R3] Codex task-mr5nips5「M5 NLP spec v2.1 adversarial review」（2026-07-04）—— 8 Blocker + 5 Concern + 2 Info 源码-backed 评审。
- [R4] NLM colav_algorithms notebook「CPA floor design for short-horizon MPC」ask 🟢 High（2026-07-04）—— Suffix-Hard Schedule 优于 slack / pure soft（RF 保证 + COLREGs safety）。
- [R5] NLM colav_algorithms notebook「terminal lateral constraint design」ask 🟢 High（2026-07-04）—— Option 2（full soften + tail-gate hard）最优，对齐 Doer-Checker SOTIF/SIL2。
- [R6] NLM colav_algorithms notebook sources: [Kerrigan 2000 CUED soft constraints](http://www-control.eng.cam.ac.uk/Homepage/papers/cued_control_53.pdf)、[acados running constraint](https://discourse.acados.org/t/running-constraint-on-a-specific-node-of-the-mpc-horizon/2016)、[Johansen ICRA'18 MPC COLREGs](https://torarnj.folk.ntnu.no/icra18.pdf)、[embotech FORCES soft constraints](https://forces.embotech.com/documentation/examples/high_level_soft_constraints/index.html)、[CasADi slack users group](https://groups.google.com/g/casadi-users/c/yYSZeaK1-z8)。
- [R7] `docs/superpowers/specs/2026-06-30-m5-committed-route-design-v2.md`（v2 spec）—— 本 spec 的 supersede 基础。
- [R8] `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §10.4 line 899-923—— Mid-MPC 短时域战术层架构依据。
- [R9] handoff `workspace_log.md` entries `35818e27..55554ab8`（2026-07-03）—— Fix A-F 修复链实测证据。
- [R10] AGENTS.md「COLREGs 全链路 debug」「design and documentation rules」「local-first deployment gate」—— 工程纪律约束。
- [R11] NLM ship_maneuvering notebook（36.4m RoRo ferry ROT 1.5-5.5°/s）—— 相似船舶实测支持（🟡 C 级）。
