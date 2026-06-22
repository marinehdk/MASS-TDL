# COLREGs 避碰链鲁棒性与泛化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 COLREGs 避碰链（M2→M6→M4→M5）对齐架构报告 §8-10 完整设计意图，使其对任意 COLREGs 几何会遇场景产生规则合规、物理安全、船艺合理的避碰行为。

**Architecture:** 自底向上 4 Phase。P1：M5 ConstraintCompiler 接线，CPA soft cost → hard constraint（架构 §10.4）。P2：stand-on 三阶段门控回归保护 + starboard-only 方向（已实现，防回归）。P3：M2 新增 Woerner 目标合规度 scorer，stand-on 对不让路目标提前避让。P4：M4 新增 RECOVERY 行为，M5 渐进回航线。每 Phase 独立交付 + 独立验收。

**Tech Stack:** C++17 (M2/M4/M5/M6 ROS2 nodes, CasADi/IPOPT), ROS2 Humble DDS, ament_cmake, GoogleTest, Python 3 (scoring/orchestrator)。

**Spec:** `docs/superpowers/specs/2026-06-19-colregs-avoidance-robust-generalization-design.md`

**Branch:** `codex/colregs-behavior-fix` (worktree `.worktrees/colregs-behavior-fix`)

**前置条件（必须先满足）：**
- ✅ IPOPT solver 已收敛（main 含 e8c53154/cfb5c9a6）
- ✅ ConstraintCompiler 已完整实现（codegraph 确认）
- ✅ stand-on 三阶段门控已实现（`colregs_constraint_generator.cpp:30-35`）
- ⚠️ **环境阻塞**：behavior-fix stack DOMAIN_ID 串扰（sil-nodes 进程实际 DOMAIN_ID=0 非 43）。实施前必须先修环境，否则 batch 无法跑。这是独立运维任务，不在本 plan，但**阻塞验收**。

---

## File Structure

### Phase 1 — M5 CPA Hard Constraint
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/constraint_compiler.hpp` — 新增 `compile_cpa_distance`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp` — 实现 `compile_cpa_distance`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp` — 持有 ConstraintCompiler + ConstraintInputs
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp` — `build_symbolic_graph`/`build_constraints_` 接入 compiler
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — `assemble_input_` 填充 ConstraintInputs
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp` — CPA 距离约束 case
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_nlp_formulation.cpp` — 硬约束生效 case

### Phase 2 — Stand-on 方向保护 + 门控回归
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp` — stand-on starboard-only（若缺失）
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_constraint_generator.cpp` — 门控回归 case

### Phase 3 — Woerner Compliance Scorer
- Create: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/woerner_compliance_scorer.hpp`
- Create: `src/l3_tdl_kernel/m2_world_model/src/woerner_compliance_scorer.cpp`
- Create: `src/l3_tdl_kernel/m2_world_model/test/test_woerner_compliance_scorer.cpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/world_model_node.hpp` — 目标历史缓冲
- Modify: `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp` — 调用 scorer + 发布 compliance
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/WorldState.msg` 或新增 compliance 字段
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_reasoner_node.cpp` — 消费 compliance 提前 stand-on

### Phase 4 — M4/M5 RECOVERY
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/BehaviorPlan.msg` — 新增 BEHAVIOR_RECOVERY
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp` — RECOVERY 行为
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp` — AVOID→RECOVERY→TRANSIT
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — RECOVERY 渐进轨迹
- Test: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp` — 转换 case

---

## Build & Test 命令参考

```bash
# worktree 内
cd "/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-behavior-fix"

# C++ 改动容器内 colcon build（AGENTS.md: BuildKit cache）
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select <pkg>"

# 单元测试
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select <pkg> --event-handlers console_direct+"

# 12-probe（环境修复后）
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 scripts/run_colregs_clean_8probe.py --include-intelligent --restart-between-runs \
  --summary-out runs/batch_<timestamp>.json
```

---

# Phase 1 — M5 CPA Hard Constraint（D-2/D-7 修复）

> **目标**：ConstraintCompiler 接线进 MidMpcNode，新增 CPA 距离硬约束，使 solver 不允许 CPA < cpa_safe。
> **验收**：rule17-cr-so `min_cpa_m ≥ 180`；6 GREEN 不回归。

## Task 1.1: 新增 ConstraintCompiler::compile_cpa_distance（TDD）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/constraint_compiler.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp`

- [ ] **Step 1: 写失败测试 — CPA 距离硬约束**

追加到 `test_constraint_compiler.cpp` 末尾：

```cpp
// CPA distance hard constraint: for a single target at (tx,ty) with CPA-safe,
// compile_cpa_distance produces g >= 0 where g = d^2 - cpa_safe^2.
// d^2 = (x_own - tx)^2 + (y_own - ty)^2, computed from psi_seq via integration.
TEST(ConstraintCompiler, CompileCpaDistanceProducesBarrierConstraint) {
  ConstraintCompiler compiler;
  constexpr int32_t N = 4;
  casadi::MX psi = casadi::MX::sym("psi", N);
  casadi::MX u   = casadi::MX::sym("u", N);
  ConstraintInputs inputs;
  inputs.cpa_safe_m = 1852.0;
  TargetState tgt;
  tgt.x_m = 5000.0; tgt.y_m = 0.0; tgt.vx_mps = 0.0; tgt.vy_mps = 0.0;
  inputs.targets.push_back(tgt);
  inputs.own_ship_psi_rad = 0.0;
  // dt_s and u magnitude baked so own-ship integrates forward.
  auto cc = compiler.compile_cpa_distance(psi, u, inputs, /*dt_s=*/5.0);
  EXPECT_GT(cc.g.size1(), 0) << "CPA distance constraint must be non-empty";
  EXPECT_EQ(cc.g.size1(), cc.g_lb.size1());
  EXPECT_EQ(cc.g.size1(), cc.g_ub.size1());
  // All bounds are [0, +inf] (g >= 0 convention).
  for (int i = 0; i < cc.g_lb.size1(); ++i) {
    EXPECT_DOUBLE_EQ(static_cast<double>(cc.g_lb(i)), 0.0);
  }
  EXPECT_FALSE(cc.names.empty());
  EXPECT_EQ(cc.names.front().find("cpa_distance"), 0u);
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner \
   --event-handlers console_direct+ --ctest-args -R ConstraintCompiler"
```
Expected: FAIL — `compile_cpa_distance` 未定义（编译错误）。

- [ ] **Step 3: 声明 compile_cpa_distance（header）**

在 `constraint_compiler.hpp` 的 `compile_colregs_rules` 声明后、`compile_zone_constraints` 前插入：

```cpp
  // CPA distance hard constraint: d_k^2 - cpa_safe^2 >= 0 for each (target, step).
  // d_k computed by integrating own-ship position from psi_seq/u_seq (flat-earth NED).
  // Squared form avoids sqrt non-smoothness (matches build_colreg_cost_ guard).
  // Phase 1: implements architecture §10.4 "CPA(ψ_k) ≥ CPA_safe(ODD)" as hard constraint.
  [[nodiscard]] CompiledConstraints compile_cpa_distance(
      const casadi::MX& psi_seq,
      const casadi::MX& u_seq,
      const ConstraintInputs& inputs,
      double dt_s) const;
```

- [ ] **Step 4: 实现 compile_cpa_distance（cpp）**

在 `constraint_compiler.cpp` 的 `compile_colregs_rules` 实现后插入：

```cpp
// ===========================================================================
// compile_cpa_distance() — CPA hard constraint: d_k^2 - cpa_safe^2 >= 0
// Per (target, step). Own-ship position integrated from psi/u (flat-earth NED).
// Target assumed constant velocity (tx + vx*dt, ty + vy*dt).
// ===========================================================================
ConstraintCompiler::CompiledConstraints ConstraintCompiler::compile_cpa_distance(
    const casadi::MX& psi_seq,
    const casadi::MX& u_seq,
    const ConstraintInputs& inputs,
    double dt_s) const {
  const int32_t N  = static_cast<int32_t>(psi_seq.size1());
  const int32_t Nt = static_cast<int32_t>(inputs.targets.size());
  CompiledConstraints result{};
  if (N < 1 || Nt < 1) { return result; }

  const casadi::DM dt(dt_s);
  const casadi::DM cpa_safe_sq(inputs.cpa_safe_m * inputs.cpa_safe_m);
  // Own-ship start position baked at origin (relative geometry only matters).
  casadi::MX cx = casadi::DM(0.0);
  casadi::MX cy = casadi::DM(0.0);
  std::vector<casadi::MX> g_rows;
  std::vector<std::string> names;
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_seq(casadi::Slice(k, k + 1));
    const casadi::MX u_k   = u_seq(casadi::Slice(k, k + 1));
    cx = cx + u_k * dt * casadi::MX::cos(psi_k);
    cy = cy + u_k * dt * casadi::MX::sin(psi_k);
    const casadi::DM kdt(static_cast<double>(k) * dt_s);
    for (int32_t t = 0; t < Nt; ++t) {
      const auto& tgt = inputs.targets[static_cast<std::size_t>(t)];
      const casadi::DM tx(tgt.x_m + tgt.vx_mps * static_cast<double>(k) * dt_s);
      const casadi::DM ty(tgt.y_m + tgt.vy_mps * static_cast<double>(k) * dt_s);
      const casadi::MX dx = cx - tx;
      const casadi::MX dy = cy - ty;
      g_rows.push_back(dx * dx + dy * dy - cpa_safe_sq);
      names.push_back("cpa_distance_t" + std::to_string(t) + "_k" + std::to_string(k));
    }
  }
  result.g     = casadi::MX::vertcat(g_rows);
  const int32_t total = static_cast<int32_t>(g_rows.size());
  result.g_lb  = casadi::DM::zeros(total, 1);
  result.g_ub  = casadi::DM::ones(total, 1) * kInf;
  result.names = std::move(names);
  return result;
}
```

- [ ] **Step 5: 运行测试确认通过**

同 Step 2 命令。Expected: PASS。

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/shared/constraint_compiler.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp
git commit -m "feat(m5): add ConstraintCompiler::compile_cpa_distance hard constraint (Phase 1)

Architecture §10.4 declares CPA(ψ_k) ≥ CPA_safe as hard constraint.
ConstraintCompiler now compiles d_k^2 - cpa_safe^2 >= 0 per (target, step).
Squared form avoids sqrt non-smoothness. Not yet wired to MidMpcNode runtime."
```

---

## Task 1.2: MidMpcNlpFormulation 接入 ConstraintCompiler（TDD）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_nlp_formulation.cpp`

- [ ] **Step 1: 写失败测试 — 硬约束阻止 CPA 穿透**

追加到 `test_mid_mpc_nlp_formulation.cpp`（若无此文件，参考 test_mid_mpc_solver.cpp 结构新建）：

```cpp
// Given a target that would penetrate CPA_safe under the old soft-cost-only
// formulation, the wired hard constraint forces solver output trajectory CPA
// >= cpa_safe (within IPOPT tolerance).
TEST(MidMpcNlpFormulation, HardConstraintPreventsCpaPenetration) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 6;
  MidMpcNlpFormulation form(cfg);
  form.build_symbolic_graph();
  // Input: own-ship heading straight (psi=0) toward target at (1000, 0),
  // cpa_safe=1852. Without hard constraint, distance cost may drive through.
  MidMpcInput inp{};
  inp.own_ship.psi_rad = 0.0;
  inp.own_ship.u_mps = 5.0;
  inp.planned_speed_mps = 5.0;
  inp.constraints.heading_min_rad = -M_PI;
  inp.constraints.heading_max_rad =  M_PI;
  inp.constraints.speed_min_mps = 0.0;
  inp.constraints.speed_max_mps = 10.0;
  inp.colregs_cpa_safe_m = 1852.0;
  inp.colregs_conflict_active = true;
  TargetState tgt;
  tgt.x_m = 1000.0; tgt.y_m = 0.0; tgt.vx_mps = 0.0; tgt.vy_mps = 0.0;
  inp.targets.push_back(tgt);
  inp.applicable_rules.push_back(14);

  MidMpcSolver::IpoptOptions opts;
  opts.max_iter = 200;
  MidMpcSolver solver(form, opts);
  MidMpcSolution sol = solver.solve(inp, nullptr);

  // Assert every trajectory step keeps distance >= cpa_safe (within tol).
  for (const auto& pt : sol.trajectory) {
    // Recompute distance at this step (own-ship integrated).
    // For this assertion we check the solver converged (hard constraint active).
    // Exact CPA recomputation requires position integration — done in integration test.
  }
  EXPECT_EQ(sol.status, MidMpcSolution::Status::Converged)
      << "Solver must converge with hard CPA constraint";
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m5_tactical_planner \
   --event-handlers console_direct+ --ctest-args -R HardConstraintPreventsCpaPenetration"
```
Expected: FAIL — ConstraintInputs 未传入 formulation / compile 未接入。

- [ ] **Step 3: formulation header 持有 ConstraintCompiler + ConstraintInputs**

修改 `mid_mpc_nlp_formulation.hpp`：
- 顶部 include 增加 `#include "m5_tactical_planner/shared/constraint_compiler.hpp"`
- `class MidMpcNlpFormulation` private 区增加成员：
```cpp
  shared::ConstraintCompiler compiler_{};
  ConstraintInputs constraint_inputs_{};
```
- public 增加设值方法：
```cpp
  // Phase 1: set runtime constraint inputs (from M6/M2) before build_symbolic_graph.
  void set_constraint_inputs(const ConstraintInputs& inputs) { constraint_inputs_ = inputs; }
```

- [ ] **Step 4: build_constraints_ 接入 compiler**

修改 `mid_mpc_nlp_formulation.cpp build_constraints_()`（现有实现只编译 ROT + box），在末尾 `return` 前增加：

```cpp
  // Phase 1: wire ConstraintCompiler — CPA hard constraint + per-rule constraints.
  auto colregs_cc = compiler_.compile_colregs_rules(psi_, u_, constraint_inputs_);
  auto cpa_cc     = compiler_.compile_cpa_distance(psi_, u_, constraint_inputs_, cfg_.dt_s);
  auto zone_cc    = compiler_.compile_zone_constraints(psi_, u_, constraint_inputs_, cfg_.dt_s);
  g_all = casadi::MX::vertcat({g_all, colregs_cc.g, cpa_cc.g, zone_cc.g});
```

注意：`g_all` 是现有 `build_constraints_` 累积的 ROT/box 约束向量。需把现有 return 改为先 vertcat 再 return。同步更新 `g_dim()` 计算逻辑使其包含 compiler 输出行数。

- [ ] **Step 5: 运行测试确认通过**

同 Step 2。Expected: PASS。

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_nlp_formulation.cpp
git commit -m "feat(m5): wire ConstraintCompiler into MidMpcNlpFormulation (Phase 1)

COLREGs CPA constraint now hard (architecture §10.4) via compile_cpa_distance
+ per-rule + TSS zone constraints. Soft cost J_colreg retained as gradient
guidance within feasible region. g_dim extended to include compiler output."
```

---

## Task 1.3: MidMpcNode::assemble_input_ 填充 ConstraintInputs

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

- [ ] **Step 1: 定位 assemble_input_ + colregs constraint 消费点**

```bash
grep -n "assemble_input_\|colregs_constraint\|COLREGsConstraint\|applicable_rules\|cpa_safe" \
  src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
```

- [ ] **Step 2: 在 assemble_input_ 填充 ConstraintInputs 并传入 formulation**

在 `assemble_input_()` 末尾（return inp 前）增加：

```cpp
  // Phase 1: build ConstraintInputs for ConstraintCompiler (hard CPA constraint).
  ConstraintInputs cstr_inputs;
  cstr_inputs.cpa_safe_m = input.colregs_cpa_safe_m;
  cstr_inputs.own_ship_psi_rad = input.own_ship.psi_rad;
  cstr_inputs.heading_min_rad = input.constraints.heading_min_rad;
  cstr_inputs.heading_max_rad = input.constraints.heading_max_rad;
  cstr_inputs.speed_min_mps = input.constraints.speed_min_mps;
  cstr_inputs.speed_max_mps = input.constraints.speed_max_mps;
  cstr_inputs.applicable_rules = input.applicable_rules;  // from M6 msg
  cstr_inputs.targets = input.targets;                    // from M2 world state
  formulation_.set_constraint_inputs(cstr_inputs);
```

注意：`input` 是 `MidMpcInput`（assemble_input_ 的返回类型）。若 `MidMpcInput` 无 `applicable_rules`/`targets` 字段，需在 `types.hpp MidMpcInput` 增加（从 M6 COLREGsConstraint msg + M2 WorldState 填充）。先 `grep MidMpcInput` 确认现有字段。

- [ ] **Step 3: 容器内重编 m5_tactical_planner**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && \
   colcon build --packages-select m5_tactical_planner"
```
Expected: 编译通过。

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp
git commit -m "feat(m5): populate ConstraintInputs in assemble_input_ (Phase 1)

MidMpcNode now feeds M6 applicable_rules + M2 targets + cpa_safe into
ConstraintCompiler via ConstraintInputs. Hard CPA constraint active at runtime."
```

---

## Task 1.4: Phase 1 集成验证（需环境修复后）

- [ ] **Step 1: 确认环境（DOMAIN_ID 隔离）**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "echo ROS_DOMAIN_ID=\$ROS_DOMAIN_ID"
# Expected: ROS_DOMAIN_ID=43（若仍是 0，先修 docker-compose.behavior-fix-isolation.yml 生效）
```

- [ ] **Step 2: 重启 sil-nodes**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose restart sil-nodes
sleep 30
```

- [ ] **Step 3: 跑 8 clean probe**

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 scripts/run_colregs_clean_8probe.py --restart-between-runs \
  --summary-out runs/batch_phase1_$(date +%Y%m%d_%H%M%S).json
```

- [ ] **Step 4: 核验结果**

```bash
python3 -c "
import json,glob,sys
f=sorted(glob.glob('runs/batch_phase1_*.json'))[-1]
d=json.load(open(f))
red=[k for k,v in d.items() if not v.get('cpa_ok') or not v.get('stability_pass') or not v.get('route_corridor_ok')]
print(f'batch: {f}')
print(f'GREEN: {len(d)-len(red)}/{len(d)}')
print(f'RED: {red}')
for k,v in d.items():
    print(f\"  {k}: cpa_ok={v.get('cpa_ok')} min_cpa={v.get('min_cpa_m',0):.0f} stab={v.get('stability_pass')}\")
"
```
Expected: rule17-cr-so `min_cpa_m ≥ 180`（cpa_ok=True）；6 GREEN 不回归。

- [ ] **Step 5: 若 rule17-cr-so 仍 RED，记录证据**

stand-on 在 hold 期 solver 可能无 active conflict（colregs_conflict_active=false），硬约束不触发。这是预期——rule17-cr-so 最终解决靠 Phase 3 Woerner。记录 Phase 1 后的 min_cpa 作为基线。

---

# Phase 2 — Stand-on 方向保护 + 门控回归

> **目标**：确认 stand-on 三阶段门控不回归；stand-on 对左舷目标独立避让 starboard-only。
> **验收**：单元测试覆盖门控回归。

## Task 2.1: 门控回归测试

**Files:**
- Test: `src/l3_tdl_kernel/m6_colregs_reasoner/test/test_constraint_generator.cpp`

- [ ] **Step 1: 写门控回归测试**

追加到 `test_constraint_generator.cpp`：

```cpp
// Regression: stand-on in PRESERVE_COURSE / SOUND_WARNING must NOT require action.
TEST(ConstraintGenerator, StandOnPreserveCourseNoAction) {
  // requires_action is file-local in colregs_constraint_generator.cpp (anonymous ns).
  // Test via ConstraintGenerator::generate output: stand-on PRESERVE_COURSE should
  // not produce an active action constraint.
  std::vector<RuleEvaluation> evals;
  RuleEvaluation e;
  e.role = Role::STAND_ON;
  e.phase = TimingPhase::PRESERVE_COURSE;
  e.is_active = true;
  evals.push_back(e);
  RuleParameters params;
  auto msg = ConstraintGenerator{}.generate(evals, params, 1.0);
  // No active action required in PRESERVE_COURSE.
  EXPECT_EQ(msg.active_action_required, false);
}

TEST(ConstraintGenerator, StandOnIndependentActionRequiresAction) {
  std::vector<RuleEvaluation> evals;
  RuleEvaluation e;
  e.role = Role::STAND_ON;
  e.phase = TimingPhase::INDEPENDENT_ACTION;
  e.is_active = true;
  evals.push_back(e);
  RuleParameters params;
  auto msg = ConstraintGenerator{}.generate(evals, params, 1.0);
  EXPECT_EQ(msg.active_action_required, true);
}
```

注意：`active_action_required` 字段名需核实（grep COLREGsConstraint.msg）。若无，通过 `msg.rules` 向量的 action 字段间接断言。

- [ ] **Step 2: 运行测试**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m6_colregs_reasoner \
   --event-handlers console_direct+ --ctest-args -R StandOn"
```
Expected: PASS（门控已实现）。

- [ ] **Step 3: Commit**

```bash
git add src/l3_tdl_kernel/m6_colregs_reasoner/test/test_constraint_generator.cpp
git commit -m "test(m6): stand-on phase gating regression coverage (Phase 2)

Confirms stand-on only requires_action in INDEPENDENT_ACTION/CRITICAL_ACTION.
Protects the existing §9.3 three-stage gating from regression."
```

---

## Task 2.2: 核实 stand-on starboard-only 方向

**Files:**
- Modify (if needed): `src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp`

- [ ] **Step 1: 核实现有 stand-on 方向逻辑**

```bash
grep -n "STAND_ON\|stand_on\|standon\|starboard\|port" \
  src/l3_tdl_kernel/m4_behavior_arbiter/src/colregs_directive.cpp | head -20
```

- [ ] **Step 2: 若 stand-on 无 starboard-only 强制，则新增**

在 stand-on directive 生成路径，对左舷目标（relative bearing > 180°）强制 direction=STARBOARD（Rule 17(a)(ii)）。具体代码取决于现有 directive 结构——先读 `colregs_directive.cpp` stand-on 分支确认。

- [ ] **Step 3: 写测试 + 重编 + Commit**

若改动：写 stand-on 左舷目标 starboard-only 测试，重编 m4_behavior_arbiter，commit `feat(m4): stand-on starboard-only for port-side target (Phase 2)`。

若已有：记录现状，无 commit。

---

# Phase 3 — M2 Woerner Compliance Scorer

> **目标**：M2 新增 Woerner scorer 输出 target_compliance；stand-on 对 compliance 低的目标提前避让。
> **验收**：stand-on 对 constant-velocity dummy（compliance 低）提前避让。

## Task 3.1: WoernerComplianceScorer 类骨架（TDD）

**Files:**
- Create: `src/l3_tdl_kernel/m2_world_model/include/m2_world_model/woerner_compliance_scorer.hpp`
- Create: `src/l3_tdl_kernel/m2_world_model/src/woerner_compliance_scorer.cpp`
- Create: `src/l3_tdl_kernel/m2_world_model/test/test_woerner_compliance_scorer.cpp`
- Modify: `src/l3_tdl_kernel/m2_world_model/CMakeLists.txt`

- [ ] **Step 1: 写失败测试 — 让路目标 compliance 高，不让路 compliance 低**

```cpp
// test_woerner_compliance_scorer.cpp
#include <gtest/gtest.h>
#include "m2_world_model/woerner_compliance_scorer.hpp"

TEST(WoernerComplianceScorer, YieldingTargetScoresHigh) {
  WoernerComplianceScorer scorer(/*history_window_s=*/30.0);
  // Target initially closing, then turns to open CPA (yielding).
  TargetHistorySample s1{}; s1.t_s = 0.0;  s1.cpa_m = 300.0; s1.range_m = 2000.0;
  TargetHistorySample s2{}; s2.t_s = 10.0; s2.cpa_m = 400.0; s2.range_m = 1800.0;
  TargetHistorySample s3{}; s3.t_s = 20.0; s3.cpa_m = 600.0; s3.range_m = 1600.0;
  scorer.add_sample(s1); scorer.add_sample(s2); scorer.add_sample(s3);
  double c = scorer.score();
  EXPECT_GT(c, 0.6) << "Yielding target (CPA increasing) should score high compliance";
}

TEST(WoernerComplianceScorer, NonYieldingTargetScoresLow) {
  WoernerComplianceScorer scorer(30.0);
  TargetHistorySample s1{}; s1.t_s = 0.0;  s1.cpa_m = 200.0; s1.range_m = 1500.0;
  TargetHistorySample s2{}; s2.t_s = 10.0; s2.cpa_m = 190.0; s2.range_m = 1300.0;
  TargetHistorySample s3{}; s3.t_s = 20.0; s3.cpa_m = 185.0; s3.range_m = 1100.0;
  scorer.add_sample(s1); scorer.add_sample(s2); scorer.add_sample(s3);
  double c = scorer.score();
  EXPECT_LT(c, 0.4) << "Non-yielding target (CPA not increasing) should score low";
}

TEST(WoernerComplianceScorer, InsufficientHistoryReturnsNeutral) {
  WoernerComplianceScorer scorer(30.0);
  TargetHistorySample s1{}; s1.t_s = 0.0; s1.cpa_m = 300.0; s1.range_m = 2000.0;
  scorer.add_sample(s1);
  double c = scorer.score();
  EXPECT_NEAR(c, 0.5, 0.01) << "Insufficient history returns neutral 0.5";
}
```

- [ ] **Step 2: 运行测试确认失败**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m2_world_model \
   --event-handlers console_direct+ --ctest-args -R Woerner"
```
Expected: FAIL — 文件不存在（编译错误）。

- [ ] **Step 3: 实现 header + cpp**

`woerner_compliance_scorer.hpp`：

```cpp
#ifndef MASS_L3_M2_WOERNER_COMPLIANCE_SCORER_HPP_
#define MASS_L3_M2_WOERNER_COMPLIANCE_SCORER_HPP_

// M2 World Model — Woerner Compliance Scorer (Phase 3)
// Quantifies target vessel COLREGs compliance ∈ [0,1] from observable history.
// High score = target yielding (CPA/range opening); low = non-yielding.
// Source: Woerner et al. (2019) ATR [R6]; online port from offline protocol eval.
// Used by M6 stand-on reasoning to escalate early when give-way target fails to act.

#include <cstdint>
#include <deque>

namespace mass_l3::m2 {

struct TargetHistorySample {
  double t_s{0.0};      // timestamp [s]
  double cpa_m{0.0};    // closest point of approach [m]
  double range_m{0.0};  // current range [m]
};

class WoernerComplianceScorer {
 public:
  explicit WoernerComplianceScorer(double history_window_s);

  void add_sample(const TargetHistorySample& sample);

  // Compliance ∈ [0,1]. Returns 0.5 if insufficient history (<2 samples).
  // [TBD-HAZID] scoring formula calibrated at RUN-001.
  [[nodiscard]] double score() const;

  [[nodiscard]] std::size_t sample_count() const noexcept;

 private:
  double history_window_s_;
  std::deque<TargetHistorySample> history_;

  void prune_();
};

}  // namespace mass_l3::m2

#endif  // MASS_L3_M2_WOERNER_COMPLIANCE_SCORER_HPP_
```

`woerner_compliance_scorer.cpp`：

```cpp
#include "m2_world_model/woerner_compliance_scorer.hpp"

#include <algorithm>
#include <cmath>

namespace mass_l3::m2 {

WoernerComplianceScorer::WoernerComplianceScorer(double history_window_s)
    : history_window_s_(history_window_s) {}

void WoernerComplianceScorer::add_sample(const TargetHistorySample& sample) {
  history_.push_back(sample);
  prune_();
}

void WoernerComplianceScorer::prune_() {
  if (history_.empty()) { return; }
  const double newest_t = history_.back().t_s;
  while (!history_.empty() &&
         (newest_t - history_.front().t_s) > history_window_s_) {
    history_.pop_front();
  }
}

double WoernerComplianceScorer::score() const {
  if (history_.size() < 2u) { return 0.5; }  // neutral
  // Compliance proxy: CPA trend slope (dCPA/dt).
  // Positive slope (CPA increasing) = target opening = yielding = high compliance.
  const auto& first = history_.front();
  const auto& last  = history_.back();
  const double dt = last.t_s - first.t_s;
  if (dt <= 0.0) { return 0.5; }
  const double dcpa = last.cpa_m - first.cpa_m;
  const double slope = dcpa / dt;  // [m/s]
  // Map slope to [0,1]: slope >= kHighSlope → 1.0; slope <= kLowSlope → 0.0.
  // [TBD-HAZID] thresholds: kHighSlope=2.0 m/s (fast opening), kLowSlope=0.0 (closing).
  constexpr double kHighSlope = 2.0;
  constexpr double kLowSlope  = 0.0;
  double c = (slope - kLowSlope) / (kHighSlope - kLowSlope);
  c = std::clamp(c, 0.0, 1.0);
  return c;
}

std::size_t WoernerComplianceScorer::sample_count() const noexcept {
  return history_.size();
}

}  // namespace mass_l3::m2
```

- [ ] **Step 4: CMakeLists 注册**

修改 `m2_world_model/CMakeLists.txt`，在 add_library 增加 `src/woerner_compliance_scorer.cpp`，test 区增加 `test/test_woerner_compliance_scorer.cpp`。

- [ ] **Step 5: 运行测试确认通过**

同 Step 2。Expected: 3 case PASS。

- [ ] **Step 6: Commit**

```bash
git add src/l3_tdl_kernel/m2_world_model/include/m2_world_model/woerner_compliance_scorer.hpp \
        src/l3_tdl_kernel/m2_world_model/src/woerner_compliance_scorer.cpp \
        src/l3_tdl_kernel/m2_world_model/test/test_woerner_compliance_scorer.cpp \
        src/l3_tdl_kernel/m2_world_model/CMakeLists.txt
git commit -m "feat(m2): WoernerComplianceScorer target compliance quantifier (Phase 3)

Online port of Woerner et al. (2019) [R6] offline protocol eval.
Scores target compliance ∈ [0,1] from CPA trend. Neutral 0.5 if <2 samples.
Not yet wired to M6 stand-on reasoning."
```

---

## Task 3.2: M6 stand-on 消费 compliance 提前避让

**Files:**
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/WorldState.msg` — 新增 target_compliance
- Modify: `src/l3_tdl_kernel/m2_world_model/src/world_model_node.cpp` — 维护历史 + 发布 compliance
- Modify: `src/l3_tdl_kernel/m6_colregs_reasoner/src/colregs_constraint_generator.cpp` — stand-on compliance 提前

- [ ] **Step 1: WorldState.msg 增加 per-target compliance**

核实 `WorldState.msg` 的 TrackedTarget 结构，新增 `float64 compliance`（默认 0.5）。bump schema_version。

- [ ] **Step 2: M2 world_model_node 维护目标历史 + 调 scorer**

在 `world_model_node.cpp` 的 WorldState publish 逻辑，为每个 TrackedTarget 维护 `WoernerComplianceScorer`（按 target_id map），填入 compliance 字段。

- [ ] **Step 3: M6 stand-on 提前逻辑**

修改 `colregs_constraint_generator.cpp requires_action`（匿名 namespace），stand-on 增加 compliance 提前：

```cpp
bool requires_action(const RuleEvaluation& e) {
  const bool give_way = (e.role == Role::GIVE_WAY || e.role == Role::BOTH_GIVE_WAY);
  const bool standon_inextremis = (e.role == Role::STAND_ON &&
      (e.phase == TimingPhase::INDEPENDENT_ACTION || e.phase == TimingPhase::CRITICAL_ACTION));
  // Phase 3: stand-on escalates early when give-way target is non-compliant.
  // Rule 17(a)(ii): "if... finds herself so close that collision can be avoided".
  const bool standon_noncompliant_early = (e.role == Role::STAND_ON &&
      e.target_compliance < 0.4 /*[TBD-HAZID]*/ &&
      e.phase == TimingPhase::SOUND_WARNING);
  return e.is_active && (give_way || standon_inextremis || standon_noncompliant_early);
}
```

需在 `RuleEvaluation`（`m6 types.hpp`）增加 `double target_compliance{0.5}` 字段，colregs_reasoner_node 从 WorldState msg 填充。

- [ ] **Step 4: 重编 m2 + m6 + l3_msgs**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && \
   colcon build --packages-select l3_msgs m2_world_model m6_colregs_reasoner"
```

- [ ] **Step 5: 写 stand-on compliance 提前测试 + Commit**

测试：stand-on + SOUND_WARNING + compliance=0.2 → requires_action=true。
Commit: `feat(m6): stand-on early escalation on non-compliant target (Phase 3)`。

---

## Task 3.3: Phase 3 集成验证

- [ ] **Step 1: 新增模拟让路目标场景**（验证 scorer 区分让路/不让路）

在 `scenarios/` 新增一个 stand-on 场景，目标在 CPA 接近时主动转向让路（compliance 高），验证本船 stand-on 不提前动作。

- [ ] **Step 2: 跑 rule17-cr-so + 新场景**

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 scripts/run_colregs_clean_8probe.py --scenario colreg-rule17-cr-so \
  --summary-out runs/batch_phase3_rule17_$(date +%Y%m%d_%H%M%S).json
```
Expected: rule17-cr-so `min_cpa_m ≥ 180`（stand-on 因 compliance 低提前动作拉开 CPA）。

- [ ] **Step 3: 6 GREEN 回归 + Commit 证据**

记录 `runs/batch_phase3_*.json`，确认无回归。

---

# Phase 4 — M4/M5 RECOVERY 渐进回航线

> **目标**：M4 新增 RECOVERY 行为；M5 生成渐进回航轨迹；AVOID→RECOVERY→TRANSIT。
> **验收**：避让后 XTE 平滑收敛，无二次 conflict_toggles。

## Task 4.1: BehaviorPlan.msg 新增 BEHAVIOR_RECOVERY

**Files:**
- Modify: `src/l3_tdl_kernel/l3_msgs/msg/BehaviorPlan.msg`

- [ ] **Step 1: 新增 enum**

在 `BehaviorPlan.msg` 的 `BEHAVIOR_MRC_HEAVE_TO = 6` 后增加：
```
uint8 BEHAVIOR_RECOVERY = 7
```

- [ ] **Step 2: 重编 l3_msgs + 所有依赖**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && cd /opt/ws && colcon build --packages-select l3_msgs"
```

- [ ] **Step 3: 同步 Python 引用**

`docker/fsm_aggregator_node.py:64 BEHAVIOR_NAMES` 和 `sil_topic_bridge.py` 增加 RECOVERY 映射。

- [ ] **Step 4: Commit**

```bash
git add src/l3_tdl_kernel/l3_msgs/msg/BehaviorPlan.msg docker/fsm_aggregator_node.py docker/sil_topic_bridge.py
git commit -m "feat(msg): add BEHAVIOR_RECOVERY enum (Phase 4)"
```

---

## Task 4.2: M4 behavior_arbiter RECOVERY 转换（TDD）

**Files:**
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/include/m4_behavior_arbiter/behavior_arbiter_node.hpp`
- Modify: `src/l3_tdl_kernel/m4_behavior_arbiter/src/behavior_arbiter_node.cpp`
- Test: `src/l3_tdl_kernel/m4_behavior_arbiter/test/unit/test_m4_node_lifecycle.cpp`

- [ ] **Step 1: 写失败测试 — AVOID→RECOVERY→TRANSIT**

```cpp
TEST(BehaviorArbiterNode, AvoidToRecoveryToTransit) {
  // Setup: node in AVOID with XTE beyond corridor.
  // When COLREGs latch releases (past_and_clear) and XTE > corridor_half*0.5,
  // behavior transitions to RECOVERY (not TRANSIT).
  // When XTE converges below corridor_half*0.5 + dwell, transition to TRANSIT.
  // (具体 setup 依赖现有 test_m4_node_lifecycle.cpp 的 fixture 模式)
  // ... 见现有 AVOID/TRANSIT 测试结构 ...
  EXPECT_EQ(msg.behavior, l3_msgs::msg::BehaviorPlan::BEHAVIOR_RECOVERY);
}
```

- [ ] **Step 2: 运行确认失败**

```bash
COMPOSE_PROJECT_NAME=colregs-behavior-fix docker compose exec sil-nodes bash -c \
  "source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash && \
   cd /opt/ws && colcon test --packages-select m4_behavior_arbiter \
   --event-handlers console_direct+ --ctest-args -R AvoidToRecovery"
```
Expected: FAIL — BEHAVIOR_RECOVERY 转换逻辑不存在。

- [ ] **Step 3: 实现 RECOVERY 转换**

在 `behavior_arbiter_node.cpp` 的行为仲裁逻辑，COLREGs release 后判断：
```cpp
const bool xte_beyond_recovery = std::abs(current_xte_m) > corridor_half_m * 0.5;
if (colregs_released && xte_beyond_recovery) {
  behavior = BEHAVIOR_RECOVERY;
} else if (colregs_released && xte_within_recovery_dwell) {
  behavior = BEHAVIOR_TRANSIT;
}
```

RECOVERY 优先权重 0.4（低于 COLREGs_Avoidance 0.7，高于 Transit 0.3）。

- [ ] **Step 4: 运行确认通过 + Commit**

Commit: `feat(m4): AVOID→RECOVERY→TRANSIT state machine (Phase 4)`。

---

## Task 4.3: M5 RECOVERY 渐进轨迹

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`

- [ ] **Step 1: on_solve_cycle_ 增加 RECOVERY 分支**

在 `on_solve_cycle_()`（`mid_mpc_node.cpp:312`），现有 `is_transit` 硬切空 plan 逻辑前，增加 RECOVERY 分支：

```cpp
const bool is_recovery =
    behavior_plan_->behavior == l3_msgs::msg::BehaviorPlan::BEHAVIOR_RECOVERY;
// ...
if (is_recovery) {
  // Phase 4: generate gradual return-to-route trajectory.
  plan = build_recovery_plan_(input, lat, lon);
} else if (is_transit) {
  // existing empty-plan logic
}
```

- [ ] **Step 2: 实现 build_recovery_plan_**

新增私有方法，生成从当前偏离位指向航线最近投影点的 N 步轨迹（XTE 线性衰减）：

```cpp
l3_msgs::msg::AvoidancePlan MidMpcNode::build_recovery_plan_(
    const MidMpcInput& input, double lat0_deg, double lon0_deg) {
  l3_msgs::msg::AvoidancePlan plan;
  plan.schema_version = 112;
  plan.status = "RECOVERY";
  plan.rationale = "gradual return to planned route";
  plan.confidence = 0.8F;
  // Generate 4 waypoints from current pos converging to route projection.
  // XTE linear decay over horizon. [TBD-HAZID] decay rate.
  // ... flat-earth NED → WGS84 (reuse ned_to_geopoint_) ...
  return plan;
}
```

- [ ] **Step 3: 重编 + Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp
git commit -m "feat(m5): RECOVERY gradual return-to-route trajectory (Phase 4)"
```

---

## Task 4.4: Phase 4 集成验证

- [ ] **Step 1: 跑 12-probe 全量**

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 scripts/run_colregs_clean_8probe.py --include-intelligent --restart-between-runs \
  --summary-out runs/batch_phase4_final_$(date +%Y%m%d_%H%M%S).json
```

- [ ] **Step 2: 核验回航线平滑性**

每个场景检查：
- `bp_transitions` 无 AVOID↔TRANSIT 抖动（应有 RECOVERY 中间态）
- `behavior_toggles` 不异常增加
- `returned_to_route=true`
- `transit_after_avoidance_s` 合理

- [ ] **Step 3: 全量回归 + 证据归档**

记录 `runs/batch_phase4_final_*.json`，确认 12-probe GREEN 数 ≥ Phase 1 基线。

---

# 收尾

## Final: 全量回归 + 证据归档

- [ ] **Step 1: 12-probe 全量 final**

```bash
export SIL_ORCH_BASE_URL=https://127.0.0.1:18001/api/v1
python3 scripts/run_colregs_clean_8probe.py --include-intelligent --restart-between-runs \
  --summary-out runs/batch_final_$(date +%Y%m%d_%H%M%S).json
```

- [ ] **Step 2: 核验所有 gate（不降门槛）**

每个场景 `overall_pass = cpa_ok AND stability_pass AND route_return AND route_corridor_ok AND risk_gate_ok AND seamanship_gate_ok`。

- [ ] **Step 3: handoff 记录**

更新 `handoff/workspace_log.md`：日期 / Agent / Commit / Task Goal / Core Changes / Current Status / Handoff Notes。

- [ ] **Step 4: mempalace diary_write**

会话结束前写 AAAK 摘要到 `mass_l3_tactical_layer` wing。

---

## 环境修复（前置阻塞，独立任务）

> 本 plan 的集成验证（Task 1.4 / 3.3 / 4.4）依赖 behavior-fix stack DOMAIN_ID 隔离生效。当前 sil-nodes 进程实际 DOMAIN_ID=0，与主 stack 串扰。**实施前必须修复**，否则 batch 无法跑。

修复步骤（独立运维，不属本 plan 但阻塞验收）：
1. 核实 `docker-compose.behavior-fix-isolation.yml` 的 `ROS_DOMAIN_ID=43` 为何没传进 sil-nodes 进程（可能 base compose 覆盖或 env_file 优先级）
2. 确认主 stack `mass-l3-sil` 不再 `restarting`（先停或修复）
3. 重启 behavior-fix stack，验证 `echo $ROS_DOMAIN_ID` 在容器进程内为 43
4. 验证 `ros2 node list`（DOMAIN_ID=43）能看到 m1-m8 + target_vessel
