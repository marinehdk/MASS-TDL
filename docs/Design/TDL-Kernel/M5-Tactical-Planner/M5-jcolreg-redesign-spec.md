# M5 · Mid-MPC `J_colreg` Redesign · Spec

| 属性 | 值 |
|---|---|
| 模块 | M5 Tactical Planner · Mid-MPC NLP |
| 触发 | 避碰回归 systematic-debugging（2026-06-08）→ `Restoration_Failed` 根因定位 |
| 类型 | 缺陷修复 + 成本函数重设计（COLREGs soft-cost） |
| 关联 | 实装于 [D3.2](../../Phase%203/D3.2-m5-tactical-planner/)；本 spec 取代 D3.2 的 `build_colreg_cost_` 公式部分 |
| 上游证据 | [gap doc](../../../Doc%20From%20Claude/2026-06-08-avoidance-design-vs-implementation-gap.md) §6 |
| 接地来源 | NLM `colav_algorithms` 笔记本 2 次查询（2026-06-08，均 confidence: **high** 🟢，见 §7） |
| 状态 | **✅ 已实现**（2026-06-08）：Restoration_Failed 50→0，单测 9/9，e2e GREEN。Plan: [2026-06-08-m5-jcolreg-redesign.md](../../../superpowers/plans/2026-06-08-m5-jcolreg-redesign.md) |

---

## 1. 问题陈述

Mid-MPC NLP 在头对头（`colreg-rule14-ho`）遭遇中**间歇 IPOPT 求解失败**（`Restoration_Failed` / `Maximum_Iterations_Exceeded`，~50 次/75s 遭遇），失败周期回退 DEGRADED 几何 fallback，导致避碰非确定、轨迹质量下降。

历史误判（已被本轮实证推翻）：
- ❌「never converges」——实测多数周期收敛（`mid_mpc_node.cpp:248` 注释过时）。
- ❌ infeasible cold-start 是成因——x0 clamp 修复尝试实测无效（gap doc §6 修复尝试 #1）。

---

## 2. 根因（Phase 1+2，A4000 live `[M5DIAG]` 实证）

通过在 `MidMpcSolver::solve()` 注入**逐约束组残差分解**（start point + 返回迭代），定位三层叠加根因，并用消去法量化各自贡献：

| 配置 | 失败数/75s | 结论 |
|---|---|---|
| baseline（box 作为 general constraint `g`）| **50** | — |
| box → IPOPT `lbx`/`ubx` 变量界 | **22** | box-as-general-constraint 是真实贡献项 ✅ |
| + ROT `abs()` → 两条平滑线性约束 | **23** | ROT kink 可忽略（正确但非承重） |
| + `w_colreg=0`（停用 J_colreg）| **0** | **`J_colreg` 的 `fmax` 项是主导剩余成因** |

**机理：**
1. **box 编码错误（已修）**：heading/speed 限幅原作为 `g≥0` 一般不等式（`psi`/`u` 无 `lbx`/`ubx`，是自由变量）。`J_dist` 把 ψ 拉向航线 bearing，而 bearing 落在 M4 避碰窗口**之外** → 最优解恒被钉在 box 边界上。`limited-memory` Hessian + `mu_strategy=adaptive` 下，**一般不等式 active 边界**是 restoration-fragile（变量界 active 才是 IPOPT 稳健工况）。实证：box 宽 0–103° 均失败（非「窗口太窄」问题）。
2. **ROT 非光滑（已修，中性）**：`g_rot = rot_step − |Δψ|` 的 kink 在 Δψ=0，恰是近恒向轨迹的工作点，约束 Jacobian 每迭代翻号；改为 `rot_step−Δψ≥0 ∧ rot_step+Δψ≥0` 两条线性约束（等价、平滑）。实测失败数不变，保留作 formulation hygiene。
3. **`J_colreg` 非光滑 + 陡峭（待修，本 spec）**：`Σ tw·fmax(0, cpa²−d²)`。`fmax` 在 d=cpa 处非光滑；`w_colreg=1000` 使其陡峭主导。同时该项**语义无效**（实测 `cost_colreg≡0`，避碰实际全靠 M4 box 硬约束驱动）且**无右转偏好**（对称正遇下解出 −180° 逃逸，致 `HeadOnGiveWayRightTurn` 单测失败）。

---

## 3. 设计（接地于 §7 文献）

> 文献最佳实践 🟢：分层两步 —— 上游行为层**选离散 COLREGs 态**（正遇→右转），下游 NMPC 以**软成本**执行；硬约束在多船/紧迫场景易致 infeasible，不推荐。本系统中 M4/M6 应负责「选哪一侧」，M5 软成本负责「如何平滑执行」。

### 3.1 `fmax` → 指数障壁（平滑）

替换每 (target, step) 罚项：

```
旧:  tw · fmax(0, cpa_safe² − d²)
新:  tw · exp(−ζ · (d − cpa_safe))         // d = sqrt(dx²+dy²+ε)
```

- 平滑、**无奇点**（优于 reciprocal/polynomial barrier）；安全区 ≈0，仅逼近安全边界时陡升。
- `ζ` = [TBD-HAZID] 敏感度常数：`ζ>0`，调到「exp 罚在 `d→d_safe` 时强压过其余目标项、过早把 solver 推离边界」。具体标量依单位尺度（m vs 归一化船长）而定，HAZID RUN-001 校准（🟢 文献：ζ 按 RPN 加权调陡度，无通用标量）。
- 与上一会话 `_dbg_formula_grid.py` 的 `exp_flip` 原型一致。

### 3.2 静态权重 → 动态 range/TCPA 标度

弃 `w_colreg=1000` 静态主导（🟢 文献：避碰峰值权重约为航线跟踪的 **3×**，非 1000×）。**range + TCPA 双标度都上**（空间维 + 时间维，文献明确推荐二者并用）：
- **Range 斜坡**（IvP `pwt_outer_dist`/`pwt_inner_dist` 映射）：罚权在 `outer` 外为 0，线性升至 `inner` 内 100%。空间滤波——远处接触不触发无谓偏航。
- **TCPA 时间折扣**：`exp(−t_k/T_d)` —— 时间维滤波，多目标时临近危险压过远处。
- 现 `tw = min(1/(cpa·tcpa), cap)` **替换为 range 斜坡权**（语义更清晰、文献对齐）。

⚙️ **平滑性关键**：range 斜坡只依赖初始几何 `rng0=‖tgt₀−own₀‖`（参数,非决策变量）→ 在 `pack_parameters` 用 `std::clamp` **数值算成 per-target 标量**入 `tw` slot,**不进符号图**（避免 clamp 的 kink）。TCPA 折扣 `exp(−k·dt/T_d)` 是**常数向量**,作 DM 系数乘入符号和。**唯一进符号图的非常数项 = 指数障壁**(d 的平滑函数)。整个 NLP 保持平滑。

**数值默认（[TBD-HAZID]，HAZID RUN-001 校准；下列为文献经验值 🟢）：**

| 参数 | 默认 | 来源/说明 |
|---|---|---|
| `μ_max`（避碰峰值权重）| **≈ 3 × `w_dist`**（即 w_dist=10 → ~30，**非 1000**）| 文献:CA 峰值权重 3× 任务权重 |
| `pwt_inner_dist` | **1852 m（1 nm）** = `cpa_safe`/SDA，内界 100% 优先 | 大船 SDA |
| `pwt_outer_dist` | **11112 m（6 nm）** 开阔水域,风险评估起点 | 6→1 nm 线性升 |
| `T_d`（TCPA 折扣时间常数）| **100 s** | 预测 COLAV 标准值 |

### 3.3 Rule-14 右转不对称（消除 −180° 逃逸）

**已定方案：控制量不对称罚**（input-cost asymmetry），**平滑实现**。

⚠️ 文献的「port_mult 1.8 vs stbd_mult 1.5」原文是分段权重（在 ψ=ψ_ref 处硬切换 = **非光滑 kink**），直接搬会重新引入我们刚消除的非光滑性。故改为**等效平滑的单侧 softplus port 罚**：

```
J_asym = give_way · k_asym · Σ_k  τ·log(1 + exp((ψ_ref − ψ_k)/τ))
```

- ψ_k 在 port 侧（ψ_k < ψ_ref）→ 项 ≈ (ψ_ref−ψ_k)，罚 port；ψ_k 在 stbd 侧 → 项 ≈ 0，不罚。C∞ 平滑。
- `k_asym` 标定到等效 port:stbd 罚比 ≈ 1.8:1.5（🟢 文献比值），`τ` = 平滑尺度 [rad]。均 [TBD-HAZID]。
- **门控 `give_way`**：M6 `applicable_rules` 含 14（正遇）或 15（交叉让路）→ give_way=1，否则 0。在 `pack_parameters` 算成标量,经**新增 param slot `kIdxGiveWay`**（kParamDim 93→94）入符号图;`give_way=0` 时 J_asym≡0（对称,stand-on/无遭遇）。
- ψ_ref = 航线 bearing（`kIdxRouteBearing`）。
- **转率不对称（Δψ mult 1.2/0.9）本轮 DEFER**（YAGNI）：航向偏移不对称已足够消除对称逃逸 + 满足 DoD「正遇右转」；转率精修留后续。

**选型理由（🟢 文献排名 + 本系统架构）：** 文献按 IPOPT 鲁棒性排序为 ① port 侧「罚墙」势场（最稳，cost 单独选侧时最佳）② 控制量不对称罚（高常用、计算廉、平滑，但纯 cost 时可被强空间收益压过）③ 不对称安全裕度(认证最清晰但硬约束最不稳,须转 soft+slack)。本系统是**分层架构**——选侧由 M4/M6 负责（避碰窗口已作 `lbx`/`ubx` 钉住右舷），M5 不对称仅作**二级软偏置**防对称退化；故选 ② 控制量罚（与分层最契合、最简、认证中等），无需 ① 的逐目标空间场复杂度。
**升级路径：** 若 M4 窗口过宽/permissive 致 ② 被压过，再升 ① 罚墙。

### 3.4 保持 soft + 分层

- J_colreg 全程 soft；M4 heading box 继续作 `lbx`/`ubx`（已修），承担「选侧 + 物理限幅」。
- ⚠️ **跨模块发现（M4，仅记录不改）**：M4 在避碰段输出 **1–2° 宽** heading 窗口（实证 box `[67,69]` 等），过窄会饿死 M5 优化空间。文献建议行为层给「侧选择 + 合理宽窗」而非 2° 钉点。**属 M4 模块，CLAUDE.md §7 指出不改**，建议另开 M4 gap 条目。

---

## 4. 已完成（本轮，待提交）

| 改动 | 文件 | 状态 |
|---|---|---|
| box → `lbx`/`ubx` | `mid_mpc_solver.cpp`（arg 加 lbx/ubx）+ `mid_mpc_nlp_formulation.cpp`（`build_constraints_` 去 box 行，`g_dim`）| ✅ 实测 50→22，单测过 |
| ROT `abs()` → 两线性约束 | `mid_mpc_nlp_formulation.cpp` `build_constraints_`，`g_dim=2(N-1)` | ✅ 等价平滑，单测过 |
| `GDim` 单测更新 + 边界钉点回归单测 | `test_mid_mpc_nlp_formulation.cpp` / `test_mid_mpc_solver.cpp` | ✅ 过（除 `HeadOnGiveWayRightTurn`，见 §5）|
| `[M5DIAG]` 残差诊断 | `mid_mpc_solver.cpp` | ⚠️ **临时**，merge 前移除 |

---

## 5. `HeadOnGiveWayRightTurn` 单测处置

现断言「对称正遇下 M5 单独解出右转 >30°」。实证该断言**测错层职责**：M5 对称 `J_colreg` 无右转偏好，旧实现 >30° 是 solver 路径巧合；真实右转来自 M4/M6 选侧。

- **方案（随 §3.3 落地）**：改测「给定右转窗口/M6 give-way 态时，M5 在窗口内右转」——测 M5 真实契约（窗口内执行），右转**决策**归 M4/M6 层测试。
- 若选 §3.3 备选方案，断言相应调整。

---

## 6. 决策（已定 — NLM `colav_algorithms` 🟢 high + 主 Agent 工程判断）

| # | 决策点 | 结论 | 依据 |
|---|---|---|---|
| 1 | 不对称编码 | **控制量不对称罚**（input-cost） | 文献排名 ②，但①罚墙的额外鲁棒性主要用于「cost 单独选侧」；本系统分层(M4/M6 选侧)下 ② 最契合+最简+认证中等（§3.3）|
| 2 | 动态权重 | **range 斜坡 + TCPA 折扣 都上** | 文献明确:空间维+时间维互补,二者并用 🟢 |
| 3 | ROT 平滑 | **保留** | 等价+平滑+已测;回退只为减 diff 无收益。主 Agent 判断 |
| 4 | 提交粒度 | **分两次**:① box+ROT(50→22,语义中性里程碑) ② J_colreg(行为变更,COLREGs/认证) | 可 bisect;中性修复先安全落地。主 Agent 判断 |
| 5 | `ζ/μ/pwt_*/T_d/mult` 数值 | 用 §3.2/§3.3 文献默认 + 全标 **[TBD-HAZID]** | HAZID RUN-001 校准 |

**关键修正**：`μ`（避碰峰值权重）从误设的 **1000** 改为 **≈3×`w_dist`(~30)** —— 静态 1000 是文献明确反模式(过陡、错误主导),也是剩余 22 失败的成因之一。

---

## 7. 接地来源（NLM `colav_algorithms`，2026-06-08，confidence: high 🟢）

> 完整查询与引用见会话记录；以下为被引用的核心结论（plan 阶段细化为 `[Rx]`）：
- 非光滑 `max(0,·)`/reciprocal/polynomial barrier 致 solver 失败 → 推荐**指数障壁** `μ·e^(−ζs)`（无奇点）。
- COLREGs 宜作**软成本**置于优先级层级（survival CPA>0 底线 / COLREGs 中高软罚 / route 低软罚）；**硬约束不推荐**。
- 行为选择与轨迹优化**分层**：上游选离散 COLREGs 态并**固定**，下游 NMPC 仅优化「如何执行」。
- Rule-14 右转：**不对称罚**（port mult > stbd mult）/ port 侧罚墙 / 不对称裕度。

**查询 2（不对称排名 + 数值默认，confidence: high 🟢）：**
- 不对称编码鲁棒性排名（IPOPT/NMPC）：① port 侧罚墙势场（最稳）② 控制量不对称罚（高常用、廉、平滑）③ 不对称安全裕度（认证最清晰但硬约束最不稳，须转 soft+slack）。
- 动态权重：range 斜坡（空间维）+ TCPA 折扣（时间维）**二者并用**。
- 数值经验值：`μ_max ≈ 3× w_dist`；`port/stbd = 1.8/1.5`、`Δport/Δstbd = 1.2/0.9`；`T_d = 100 s`；`pwt_inner = 1 nm(1852 m)`、`pwt_outer = 6 nm(11112 m)`；`ζ>0` 调到 `d→d_safe` 时强主导（单位尺度相关，无通用标量）。

---

## 8. 验证 / DoD

- [ ] 集成（A4000 `colreg-rule14-ho` @rate5，75s）：IPOPT 失败数 **22 → 0**（或注明残余 + 原因）。
- [ ] `cost_colreg` 在遭遇中 **> 0**（J_colreg 真实参与，非显示假象）。
- [ ] 对称正遇：Mid-MPC 主轨迹**右转**（非 port / 非 −180° 逃逸）。
- [ ] 单测：formulation + solver 全过（含改写后的 head-on 右转测试 + 边界钉点回归测试）。
- [ ] 避碰链端到端：右转避让 → 过 CPA → 回航线（不绕圈、不 U-turn），对齐既有 `_retest_spinfix.py` 绿判据。
- [ ] `[M5DIAG]` 临时诊断已移除。
- [ ] 三端同步（local main = GitHub origin = GitLab `l3-tdl`）+ A4000 部署对齐。

---

## 修订

| 日期 | 变更 |
|---|---|
| 2026-06-08 | 初版（avoidance-regression debug 产出；box/ROT 已修，J_colreg 重设计待评审）|
