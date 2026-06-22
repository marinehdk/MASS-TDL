# COLREGs 避碰链鲁棒性与泛化设计 Spec

| 属性 | 值 |
|---|---|
| D 编号 | （非 D-task，FSM 重写专项）|
| 标题 | COLREGs 避碰链鲁棒性与泛化设计：M5 CPA hard constraint + M6 stand-on 三阶段 + M2/M6 Woerner scorer + M4/M5 RECOVERY |
| Affects | M2, M4, M5, M6 |
| Spec 日期 | 2026-06-19 |
| 架构权威 | `docs/Design/Architecture Design/MASS_ADAS_L3_TDL_架构设计报告.md` §8 (M4) / §9 (M6) / §10 (M5) |
| 实施分支 | `codex/colregs-behavior-fix` (worktree `.worktrees/colregs-behavior-fix`) |
| 前序证据 | `runs/batch_standoff_20260619_130320.json` (6G/1R), `COLREGs_Avoidance_Decision_Logic_Report.md` (D-2/D-7 偏离) |

---

## 1. 背景与动机

### 1.1 handoff 前提核实（已推翻）

接 sess_f73c6014 的工作。原 handoff 核心断言"M5 NLP solver 是 D3.1 stub 永不收敛，5RED 共性根因在 M5/M4 二值硬切"。核实（🟢 A 级，git + 代码 + batch 三方印证）发现该前提**已被推翻**：

| handoff 断言 | 实测真相 | 证据 |
|---|---|---|
| "5RED，3G/5R" | **6G/1R** | `batch_standoff_20260619_130320.json` |
| "M5 stub 永不收敛，solver_failed 恒 true" | **IPOPT 真实接入并大量收敛** | git main 含 `e8c53154`/`cfb5c9a6` convergence fix；rule14-ho solver VALID=533/EMPTY=202；`mid_mpc_nlp_formulation.cpp:270` `casadi::nlpsol("mid_mpc_solver","ipopt",...)`；`Solved_To_Acceptable_Level`→`Converged` 映射 |
| "ConstraintCompiler 未接线" | ✅ **仍正确** | codegraph `callers ConstraintCompiler` 仅 test 命中，main `mid_mpc_node.cpp` 零引用 |
| "回航线靠 conflict=false 硬切空 plan" | ✅ **仍正确** | `mid_mpc_node.cpp:362-373` TRANSIT→空 plan |

### 1.2 真实差距（来自决策报告 D-2/D-3/D-7 + 0619 batch）

唯一 RED `colreg-rule17-cr-so`（stand-on crossing）：`min_cpa_m=168.85` < `cpa_floor_m=180`。根因不在"M5 stub"，而在三个设计↔实现偏离：

- **D-2/D-7**：COLREGs `CPA(ψ_k) ≥ CPA_safe` 架构 §10.4 声明为 hard constraint，代码实现为 soft cost barrier（`build_colreg_cost_` `exp(-ζ(d-cpa_safe))`）。ConstraintCompiler 已完整实现（rule14/15/16/17 + TSS polygon）但未接线运行时。soft barrier 可被代价权衡穿透。
- **D-3（已部分过时）**：决策报告称 `requires_action()` 不查 phase。**核实推翻**：`colregs_constraint_generator.cpp:30-35` 已实现 stand-on 三阶段门控（stand-on 仅 INDEPENDENT_ACTION/CRITICAL_ACTION 才 requires_action）。rule17-cr-so RED 真实根因是 stand-on in-extremis 避让时机硬性依赖 t_act，未考虑"目标不让路"信号——详见 Phase 2/3。
- **回航线渐进性**：架构 §8.3 行为字典无 RECOVERY 行为，M4 AVOID↔TRANSIT 二值切换，M5 TRANSIT 态出空 plan（`mid_mpc_node.cpp:369`），无渐进回航能力。

### 1.3 为什么 12-probe 不是验收契约

12-probe 场景是验证手段，不是优化目标。为 12-probe 特例调阈值（如降 CPA floor、改几何迎合 PASS）违反 AGENTS.md "不得降门槛/硬编码" 红线。本 spec 以**架构报告 §8-10 完整设计意图**为准，使本船行为对任意 COLREGs 几何会遇场景合规。12-probe 用于回归验证设计是否泛化。

---

## 2. 目标

把 COLREGs 避碰链（M2→M6→M4→M5）对齐架构报告 §8-10 完整设计意图，使其**对任意符合 COLREGs 几何的会遇场景**产生规则合规、物理安全、船艺合理的避碰行为。

### 2.1 可验证完成条件

| Phase | 完成条件（可验证）|
|---|---|
| P1 | ConstraintCompiler 接线进 MidMpcNode；COLREGs 硬约束在 active-set 生效；现有 6 GREEN 场景不回归，rule17-cr-so CPA ≥ cpa_floor（180m）|
| P2 | stand-on 三阶段门控回归保护（已实现，防回归）；stand-on 对左舷目标独立避让方向 starboard-only |
| P3 | Woerner scorer 输出 `target_compliance ∈ [0,1]`；stand-on 对 compliance < 阈值的目标提前避让 |
| P4 | M4 行为字典含 RECOVERY；避让解除后 AVOID→RECOVERY→TRANSIT；M5 生成渐进回航轨迹；XTE 平滑收敛无二次触发 |

### 2.2 非目标（明确排除）

- ❌ 不换 Mid-MPC solver（IPOPT 已收敛；D9.2 收敛契约独立）
- ❌ 不接线 BC-MPC 紧急层（D-8 话题命名空间修复是独立运维）
- ❌ 不实现 DBN 意图推断（P3 Woerner scorer 是轻量替代；DBN 跨 D-task）
- ❌ 不修环境问题（DOMAIN_ID 串扰、主 stack restarting 独立运维）
- ❌ 不为 12-probe 调阈值（AGENTS.md 红线）

---

## 3. 架构对齐与来源

| 设计点 | 架构来源 | 法规/文献来源 |
|---|---|---|
| CPA hard constraint `CPA(ψ_k) ≥ CPA_safe` | 架构 §10.4 `s.t.` 块 | COLREG Rule 8 "safe passing distance"（无数值，工程参数）|
| Rule 17 三阶段 T_standOn/T_act | 架构 §9.3 表 + §9.2 层5 | Wang et al. (2021) JMSE 9(6):584 [R17]；Rule 17(a)(ii) 字面 |
| Rule 8 大幅动作 ≥30° | 架构 §9.3 表 | *The Roseline*/*The Oden* 判例；Rule 8(b) |
| Woerner 目标合规度评分 | （架构无，新增）| Woerner et al. (2019) ATR [R6]；架构参考列表 [6] 已列 |
| M4 RECOVERY 行为 | 架构 §8.3 行为字典（补充）| Cockcroft 四阶段 stage 6 "恢复航线" |
| ConstraintCompiler 硬约束 | 架构 §10.4 + §10.7 TSS polygon | COLREG Rule 10 TSS；Rule 14/15/16/17 |

**置信度**：
- CPA hard constraint / 三阶段 / ConstraintCompiler：🟢（架构原文 + 代码已实现 + 判例法 A 级）
- Woerner scorer：🟡（架构参考列表已列 [6]，但无实现先例；scorer 指标定义需校准）
- M4 RECOVERY 阈值：🟡（Cockcroft stage 6 是工程框架，具体 XTE 收敛阈值标 [TBD-HAZID]）

---

## 4. Phase 1 — M5 CPA Hard Constraint（D-2/D-7 修复）

### 4.1 现状

`mid_mpc_nlp_formulation.cpp:129-178 build_colreg_cost_()` 对每 (target, step) 施加 soft barrier `exp(-ζ(d-cpa_safe))`。`mid_mpc_nlp_formulation.cpp:126` 注释明写 "Phase E1: COLREGs rules handled as soft cost; hard constraints deferred to Phase E2"。

`ConstraintCompiler`（`shared/constraint_compiler.hpp/.cpp`）已完整实现：
- `compile_rule14/15/16/17`：per-rule 硬约束（starboard 偏置、大幅动作、stand-on 保向）
- `compile_zone_constraints` + `decompose_polygon` + `point_inside_convex`：TSS polygon（Rule 10）
- `compile_heading_bounds/speed_bounds/rot_limit`：行为 box 约束
- 单元测试 `test_constraint_compiler.cpp` 13 个 case 覆盖

**但运行时零接线**：`mid_mpc_node.cpp` 无 `ConstraintCompiler` 引用，`MidMpcNlpFormulation::build_constraints_()` 只编译 ROT 限制 + heading/speed box，不含 COLREGs 硬约束。

### 4.2 目标设计

把 `ConstraintCompiler::compile()` 输出接入 `MidMpcNlpFormulation::build_symbolic_graph()` 的约束向量 `g_`，使 COLREGs 硬约束进入 IPOPT active-set。

**约束语义**（架构 §10.4）：
```
s.t. CPA(ψ_k) ≥ CPA_safe(ODD)    # COLREGs 安全距离，hard
     speed_k ≤ speed_limit(ODD)   # ODD 速度，box bound（已有）
     |Δψ_k| ≤ ROT_max × Δt        # 转艏率（已有）
```

**soft cost 保留**：`J_colreg`（`build_colreg_cost_`）保留作渐变引导，让 solver 在 hard constraint 可行域内选最优避让方向。这是架构 §10.4 完整意图——`s.t.` 块硬约束 + `J_colreg` 软偏好并存。

### 4.3 接线点

| 文件 | 改动 |
|---|---|
| `mid_mpc_nlp_formulation.hpp` | `MidMpcNlpFormulation` 持有 `ConstraintCompiler` 实例 + `ConstraintInputs` 构造参数 |
| `mid_mpc_nlp_formulation.cpp build_symbolic_graph()` | 调用 `compiler_.compile(psi_, u_, inputs, dt, rot_max)`，stack 进 `g_`；同步更新 `g_dim()` |
| `mid_mpc_node.cpp assemble_input_()` | 从 M6 COLREGs constraint msg + M2 world state 填充 `ConstraintInputs`（applicable_rules / targets / zones）|
| `mid_mpc_solver.cpp solve()` | `lbg/ubg` 维度跟随 `g_dim()` 扩展（已用 `g_dim_()` 动态计算，无需改）|

### 4.4 关键设计决策

**CPA 约束形式**：ConstraintCompiler 现有 `compile_colregs_rules` 是 per-rule 航向硬约束（rule14 starboard、rule16 大幅动作），**不是直接的 `CPA(ψ_k) ≥ CPA_safe` 距离约束**。距离约束需新增：对每 (target, step)，`d_k ≥ cpa_safe` 作为非线性约束。

- 方案：在 ConstraintCompiler 新增 `compile_cpa_distance(psi_seq, u_seq, targets, cpa_safe)`，输出每 (target, step) 的 `d_k² - cpa_safe² ≥ 0`（平方形式避免 sqrt 非光滑，与 `build_colreg_cost_` 的 smooth-sqrt guard 一致）。
- 标 `[TBD-HAZID]`：CPA 硬约束可能在多目标密集场景导致 infeasible。需在 `MidMpcSolver` 检测 `Infeasible` 时降级（FM-2 M7 MRM 路径已存在，`mid_mpc_solver.cpp:147`）。

### 4.5 验收

- 单元：`test_mid_mpc_nlp_formulation.cpp` 新增 case——给定 target 穿透 CPA_safe 的初态，solver 输出轨迹 CPA ≥ cpa_safe（硬约束生效）
- 集成：现有 6 GREEN 场景不回归；rule17-cr-so `min_cpa_m ≥ 180`（cpa_floor）
- 证据：`runs/batch_<timestamp>.json` solver_stats 仍 VALID 占多数（硬约束不破坏收敛）

---

## 5. Phase 2 — M6 Stand-on in-extremis 避让增强（重新定位）

### 5.1 现状核实（决策报告 D-3 已部分过时）

决策报告 D-3 称"`requires_action()` 不查 phase，Rule 17 时机门控缺失"。**核实推翻**（🟢 代码直接证据）：

`colregs_constraint_generator.cpp:30-35 requires_action()` **已实现 stand-on 三阶段门控**：
```cpp
const bool standon_inextremis = (e.role == Role::STAND_ON &&
    (e.phase == TimingPhase::INDEPENDENT_ACTION || e.phase == TimingPhase::CRITICAL_ACTION));
return e.is_active && (give_way || standon_inextremis);
```

stand-on 只在 `INDEPENDENT_ACTION`(tcpa ≤ t_act) 或 `CRITICAL_ACTION`(tcpa ≤ t_emergency) 阶段才 `requires_action`。这正是架构 §9.3 三阶段门控。`TimingPhase` enum（`m6 types.hpp:20-21`）：`PRESERVE_COURSE=0 / SOUND_WARNING=1 / INDEPENDENT_ACTION=2 / CRITICAL_ACTION=3`。

**门控本身已对**。rule17-cr-so RED 的真实根因不在"门控缺失"，而在 **stand-on 进入独立避让（t=340.8s）后，避让幅度/方向不足以拉开 CPA**（min_cpa=168.85，差 floor 仅 11.15m）。

### 5.2 rule17-cr-so 真实根因分析

场景（`colreg-rule17-cr-so.yaml`）：
- 目标从本船**左舷**（relative bearing 315°）来，course 090° 10kn，**constant velocity 永不让路**
- 本船 STAND_ON，`expected_outcome.profile = standon_in_extremis_4L`，`cpa_min_m_ge = 180.0`
- basis："stand-on late-action probe; in-extremis action uses 4.0L emergency floor because own ship must hold course before intervening"

这是**故意设计的 stand-on 极限测试**。0619 batch 指标：
- `engagement_window_s=[340.8, 680.5]`（t=340.8s 进入 AVOID，对应 tcpa 降到 t_act）
- `steer_mag=74.1° starboard`（独立避让转了 74°，符合 Rule 17 不向左转）
- `min_cpa_m=168.85` < floor 180，**差 11.15m**

根因：stand-on 在 t_act 才动作（按 Rule 17 正确），但此时距离已近，即使转 74° 也只差 11m。**问题是独立避让触发时机是硬性的 t_act，未考虑"目标明显不让路"的信号**——这正是 Phase 3 Woerner scorer 要解决的（compliance 低时提前动作）。

### 5.3 Phase 2 目标（重新定位）

Phase 2 不再"加三阶段门控"（已有），而是**核实 + 增强 stand-on 独立避让方向正确性**：

**核实项**：Rule 17(a)(ii) 规定 stand-on 独立避让"shall avoid altering course to port for a vessel on her own port side"。rule17-cr-so 目标在左舷，本船转向 starboard——**方向正确**。需核实 M4 stand-on directive 是否对左舷目标强制 starboard-only（防止误左转）。

**增强项**：stand-on 独立避让的 `min_alteration_deg`。架构 §9.3 ODD-A stand-on 无独立 min_alteration（give-way 是 30°）。Rule 17 独立避让应是"能避免碰撞的最小动作"。当前 stand-on 转了 74°（够大），但仍差 11m——说明问题不在幅度而在**时机**（Phase 3 解决）。

### 5.4 Phase 2 实际改动（最小）

| 文件 | 改动 |
|---|---|
| `colregs_constraint_generator.cpp` | 无（门控已对）|
| `colregs_directive.cpp` 或 M4 stand-on 路径 | 核实并强制 stand-on 独立避让 starboard-only（防左舷目标误左转），若无则新增 |
| `test_constraint_generator.cpp` | 新增回归 case 确认 stand-on PRESERVE_COURSE 阶段 requires_action=false（防门控回归）|

### 5.5 Phase 2 验收（调整）

- 单元：stand-on PRESERVE_COURSE/SOUND_WARNING 阶段 `requires_action=false`（回归保护）
- 单元：stand-on 对左舷目标独立避让方向 = starboard（若 M4 需改）
- 集成：rule17-cr-so 若仅靠 Phase 2 不一定转 GREEN（差 11m），**Phase 3 Woerner + Phase 1 hard constraint 才是解决主因**。Phase 2 是方向正确性保护。

### 5.6 Phase 依赖关系（重要）

rule17-cr-so 的解决是 **Phase 1 + Phase 3 协同**：
- Phase 1 CPA hard constraint：物理兜底，solver 不允许 CPA < cpa_safe（但 stand-on 在 hold 期 solver 可能无 active conflict，硬约束不触发）
- Phase 3 Woerner scorer：stand-on 识别目标 compliance 低 → 提前进入独立避让 → 拉开 CPA

Phase 2 是辅助（方向保护），不单独解决 rule17-cr-so。

---

## 6. Phase 3 — M2/M6 Woerner 目标合规度 Scorer

### 6.1 现状

所有目标船是 constant course/speed dummy（M2 只接收 TrackedTargetArray，不做目标行为建模）。stand-on 场景中，give-way 目标永不主动让路，stand-on 即使按 Rule 17 独立避让，若触发太晚 CPA 仍可能穿透。

### 6.2 目标设计

新增 `WoernerComplianceScorer`，基于目标历史轨迹（CPA/TCPA/range/heading 变化率）量化目标合规度 `target_compliance ∈ [0,1]`：

- `compliance ≈ 1.0`：目标在让路（CPA 增大、heading 朝本船右舷让开）
- `compliance ≈ 0.0`：目标不让路（CPA 不增、heading 不变/朝本船）

输出供 M6 stand-on 推理消费：当 `target_compliance < threshold` 且 stand-on 已过 T_act，提前进入独立避让（不等 T_emergency）。

### 6.3 Woerner 指标（来源：Woerner et al. 2019 ATR [R6]）

Woerner 量化协议评估 6 维度：Rule 5/6/7（lookout/safe speed/risk）、Rule 8（action）、Rule 13-17（specific）。本 scorer 取其中**目标可观测**的子集：
- 目标是否采取行动（heading/range 变化率）
- 目标行动是否符合其角色（give-way 是否右转让开）

**置信度 🟡**：Woerner scorer 原用于离线评估本船合规度，这里**移植为在线目标合规度推断**。指标定义需校准，标 `[TBD-HAZID]`。

### 6.4 接线点

| 文件 | 改动 |
|---|---|
| `m2_world_model` 新增 `woerner_compliance_scorer.{hpp,cpp}` | 输入 TrackedTarget 历史（需 M2 维护目标历史缓冲），输出 compliance |
| `m2_world_model` WorldState msg | 新增 `target_compliance` 字段（或单独 topic）|
| `m6_colregs_reasoner` stand-on 推理 | 读 compliance；`compliance < threshold AND phase >= SOUND_WARNING` → 提前 INDEPENDENT_ACTION |
| `idl/` WorldState 或 COLREGs msg | 新增 compliance 字段 + schema_version bump |

### 6.5 关键设计决策

**历史窗口长度**：compliance 需目标历史轨迹。M2 当前是否维护目标历史？需核实。若无，需在 M2 增加目标历史环形缓冲（如最近 30s/60 采样）。标 `[TBD-HAZID]` 窗口长度。

**降级**：历史不足（目标刚进入）时 `compliance = 0.5`（中性，不提前也不推迟 stand-on 动作）。

### 6.6 验收

- 单元：`test_woerner_compliance_scorer.cpp`——给定让路/不让路/异常目标轨迹，compliance 分别高/低/低
- 集成：stand-on 对 constant-velocity dummy（compliance 低）提前避让；对模拟让路目标（compliance 高）延后避让
- 注意：当前目标全是 dummy（compliance 恒低），需新增 1-2 个"模拟让路"目标场景验证

---

## 7. Phase 4 — M4/M5 RECOVERY 渐进回航线

### 7.1 现状

M4 行为字典（架构 §8.3）无 RECOVERY。`behavior_arbiter_node.cpp` 在 COLREGs conflict release 后直接 TRANSIT。`mid_mpc_node.cpp:362-373` TRANSIT 态出**空 plan**（waypoints 为空），bridge 释放 avoidance 回归 L2 PlannedRoute——硬切，无渐进过渡。

### 7.2 目标设计

架构 §8.3 行为字典补充 RECOVERY 行为：

| 行为名称 | 适用 ODD | 触发条件 | 优先权重 |
|---|---|---|---|
| `Recovery` | ODD-A, B | COLREGs conflict released AND XTE > corridor_half | 0.4 |

状态机：`AVOID → RECOVERY → TRANSIT`
- `AVOID → RECOVERY`：COLREGs latch release（past_and_clear）且当前 XTE 偏离航线
- `RECOVERY → TRANSIT`：XTE 收敛到 corridor_half 内 + release_dwell（架构 §9.3 CPA 恢复确认 60s 思路）

M5 在 RECOVERY 态生成**渐进回航轨迹**：从当前偏离位指向航线上最近投影点，平滑收敛（而非空 plan 硬切）。

### 7.3 接线点

| 文件 | 改动 |
|---|---|
| `behavior_arbiter_node.hpp/cpp` | 新增 RECOVERY 行为 + AVOID→RECOVERY→TRANSIT 转换 |
| `behavior_arbiter_node` behavior enum | 新增 `BEHAVIOR_RECOVERY` |
| `mid_mpc_node.cpp on_solve_cycle_()` | RECOVERY 态生成回航 AvoidancePlan（非空，指向航线投影点）；移除 TRANSIT 硬切空 plan 逻辑 |
| `mid_mpc_waypoint_generator` 或新增 | RECOVERY 轨迹生成：当前位 → 航线最近点，平滑插值 |
| BehaviorPlan msg | `BEHAVIOR_RECOVERY` enum + schema_version bump |

### 7.4 关键设计决策

**RECOVERY 优先级 0.4**：低于 COLREGs_Avoidance(0.7)，高于 Transit(0.3)。若回航途中又遇新冲突，COLREGs_Avoidance 立即压倒 RECOVERY（AVOID 优先）。这符合 IvP 多目标仲裁（架构 §8.2）。

**回航轨迹平滑**：避免阶跃。用 Mid-MPC horizon（90s）生成从当前偏离位到航线投影点的 N 步轨迹，每步 XTE 线性或指数衰减。标 `[TBD-HAZID]` 衰减率。

**RECOVERY 不应回退到 AVOID 抖动**：若回航途中 COLREGs 反复触发（XTE 收敛中遇新目标），需 release_dwell 防抖（架构 §9.3 CPA 恢复确认 60s）。

### 7.5 验收

- 单元：`test_behavior_arbiter_node.cpp` 新增 AVOID→RECOVERY→TRANSIT 转换 case
- 集成：避让后 XTE 平滑收敛，无 bp_transitions 抖动，无二次 conflict_toggles
- 证据：`transit_after_avoidance_s` 合理，`returned_to_route=true`，`behavior_toggles` 不增加

---

## 8. 数据流与接口契约

### 8.1 Phase 间数据流

```
M2 WorldState (+P3 target_compliance)
   ↓
M6 COLREGs Reasoner
   ├─ PhaseClassifier (+P2 stand-on 三阶段门控)
   ├─ Woerner compliance 消费 (+P3)
   └─ COLREGsConstraint msg (applicable_rules, targets, zones)
       ↓
M4 Behavior Arbiter
   ├─ 行为字典 (+P4 RECOVERY)
   └─ BehaviorPlan (behavior enum +P4 RECOVERY)
       ↓
M5 Mid-MPC
   ├─ ConstraintCompiler 接线 (+P1 hard constraint)
   ├─ RECOVERY 渐进轨迹 (+P4)
   └─ AvoidancePlan
       ↓
L4 Guidance
```

### 8.2 接口契约变更

| msg | 变更 | Phase |
|---|---|---|
| WorldState | +`target_compliance` 字段（per-target）| P3 |
| COLREGsConstraint | 无变更（applicable_rules/targets 已有）| P1 消费 |
| BehaviorPlan | +`BEHAVIOR_RECOVERY` enum | P4 |
| AvoidancePlan | 无变更（waypoints + status）| P4 填充 |

**schema_version bump**：WorldState / BehaviorPlan 改动需 bump schema_version + 前后端同步。ROS2 DDS 字段必须保留 stamp/schema_version/confidence/rationale（AGENTS.md）。

---

## 9. 错误处理与降级

### 9.1 P1 CPA 硬约束 infeasible

多目标密集场景，CPA 硬约束可能 infeasible（无解同时满足所有目标 CPA ≥ cpa_safe）。
- 检测：`MidMpcSolver` 返回 `Infeasible`（`mid_mpc_solver.cpp:147` 已有路径）
- 降级：fallback 到几何圆弧（`build_geometric_fallback_plan_`，已有）；M7 MRM-02 升级（FM-2，已有）
- 不在本次新增降级路径，复用现有

### 9.2 P3 compliance 不可用

目标历史不足（刚进入）→ `compliance = 0.5` 中性，stand-on 按 P2 三阶段正常门控（不提前不推迟）。

### 9.3 P4 RECOVERY 中遇新冲突

RECOVERY 优先级 0.4 < COLREGs_Avoidance 0.7，新冲突立即压倒 RECOVERY 切回 AVOID。release_dwell 防抖避免抖动。

---

## 10. 测试策略

### 10.1 单元测试（每 Phase）

| Phase | 测试文件 | 新增 case |
|---|---|---|
| P1 | test_constraint_compiler / test_mid_mpc_nlp_formulation | CPA 硬约束生效；infeasible 降级 |
| P2 | test_phase_classifier / test_constraint_generator | stand-on 三阶段门控 |
| P3 | test_woerner_compliance_scorer（新）| 让路/不让路/异常/历史不足 |
| P4 | test_behavior_arbiter_node | AVOID→RECOVERY→TRANSIT 转换 |

### 10.2 集成测试

- 现有 6 GREEN 场景不回归（每 Phase 后跑）
- rule17-cr-so CPA ≥ 180m（P1 验收）
- 新增模拟让路目标场景（P3 验收 stand-on 对让路目标延后动作）

### 10.3 验证工作流（AGENTS.md）

- 本地：worktree `colregs-behavior-fix`，stack `colregs-behavior-fix`（DOMAIN_ID=43 port 18001）
- 12-probe：`run_colregs_clean_8probe.py --include-intelligent --restart-between-runs`
- gate：不降门槛，基于 COLREGs 原文 + 架构设计
- 不碰主 stack mass-l3-sil（演示用）

### 10.4 环境阻塞备忘

当前 behavior-fix stack 有 DOMAIN_ID 串扰问题（sil-nodes 进程实际 DOMAIN_ID=0 非 43，与主 stack 共享 DDS）。**这是独立运维问题，不在本 spec 范围**。实施前需先修环境（确保 DOMAIN_ID=43 隔离生效），否则 batch 无法跑。

---

## 11. [TBD-HAZID] 汇总

以下值标 `[TBD-HAZID]`，HAZID RUN-001（2026-08-19）校准前作为初始值，**不为 12-probe 调整**：

| 项 | 初始值 | 来源 |
|---|---|---|
| CPA 硬约束 infeasible 容忍度 | 无（直接降级）| 架构 §10.4 |
| stand-on 三阶段 T_standOn/T_act/T_emergency | 8/4/1 min | 架构 §9.3 ODD-A |
| Woerner compliance threshold | 0.4 | Woerner 2019 + 工程判断 |
| Woerner 历史窗口 | 30s | 工程判断 |
| RECOVERY XTE 收敛阈值 | corridor_half × 0.5 | 工程判断 |
| RECOVERY 轨迹衰减率 | 线性 | 工程判断 |
| RECOVERY release_dwell | 60s | 架构 §9.3 CPA 恢复确认 |

---

## 12. 置信度声明

- 🟢 High：P1（ConstraintCompiler 已实现 + 架构 §10.4 原文 + D-7 证据）、P2（PhaseClassifier 已实现 + 架构 §9.3）、P4（架构 §8.3 行为字典可扩展 + Cockcroft stage 6）
- 🟡 Medium：P3 Woerner scorer（架构参考列表已列 [6]，但在线移植是新设计，指标需校准）
- 🟢 High：handoff 前提推翻（git + 代码 + batch 三方印证）

---

**Spec 结束。** 下一步：writing-plans skill 产出实施 plan。
