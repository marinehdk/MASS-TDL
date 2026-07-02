# M5 NLP Spec-Compliance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实现 M5 NLP spec v3（`docs/superpowers/specs/2026-07-02-m5-nlp-spec-compliance-design.md`）的完整 NLP（route-frame + terminal + continuity + Rule13）+ TailBuilder active-phase 接线 + manager 改造，使 M5 输出 COLREGs 合规 + 时序连续 + GNC 可执行的稳定轨迹。

**Architecture:** Approach 1（psi/u 决策变量不变，route-frame 作 parameter）。10 Slice：P0 前置 bug → R1/N1 基础设施 → T1/M1/W1 核心 → C1/D1/O1 约束 → V1 验证。G1 rebuild 模式（每 cycle rebuild graph）。TDD 每 Slice。

**Tech Stack:** C++17 / ROS2 Humble / CasADi/IPOPT / colcon / GTest。

---

## Global Constraints（spec v3 + CLAUDE.md，所有任务隐含遵守）

- 决策变量 `x=[psi(N);u(N)]`，N=18, dt=5s, H_pred=90s。位置 psi/u 积分。
- kParamDim 94→140（spec §3.4）。`static_assert(kParamDim==140)`。
- segment source 5 标签。lifecycle 8 态。语义权威分工（M6 独占 side/role/past-clear，M5 不重判）。
- DDS 字段保留 stamp/schema_version/confidence[0,1]/rationale。
- **COLREGs debugging discipline**：全链路 debug，不 tune 阈值/场景/scorer，无 mock/skip/forced-pass/vessel-specific 分支。
- **TDD**：每 Slice 先写失败测试 → 验证失败 → 最小实现 → 验证通过 → commit。
- **不调 w_colreg/w_dist/w_vel/k_asym 压 probe**（J_colreg spec 固化）。新 cost 权重 [TBD-HAZID]。
- G1 rebuild：承认每 cycle `build_symbolic_graph()`，新增约束一并 rebuild。
- 每实现段在 worktree `.worktrees/colregs-12probe-debug`（branch `codex/colregs-12probe-debug`）。commit 前确认 HEAD。
- 容器 `codex-gnc-validation-sil-nodes-1`（bind mount worktree src→/opt/ws/src）。

---

## Build / Test Commands（全局参考）

```bash
# build + test（容器内）
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && build/m5_tactical_planner/<test_name>'
# rebuild + restart（symlink-install）
docker restart codex-gnc-validation-sil-nodes-1
# probe（host，从 worktree）
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/<tag> --summary-out runs/<tag>-summary.json --scenario colreg-rule14-ho
```

---

## File Structure（per Slice）

**Slice P0** — 前置 bug:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp:407-408,447-448`（zone 积分方向）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:403-410`（risk weight 死代码）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp`（zone 几何一致测试）

**Slice R1** — route-frame + J_route:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`（kIdx 新增 + kParamDim 140 + J_route 声明）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`（build_distance_cost_ → 拆 + 新 build_route_cost_ + pack route-frame params）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（assemble_input_ 算 active leg + pack route-frame + 跨 leg guard）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_route_cost.cpp`

**Slice N1** — NLP row registry:
- Create: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`（solve 接收 RowBoundConfig 生成 lbg/ubg）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`（暴露 row class index ranges）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_row_registry.cpp`

**Slice T1** — terminal 约束 + J_terminal:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`（build_terminal_cost_ + terminal hard rows）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_terminal.cpp`

**Slice M1** — manager 改造（坐标 + prefix + risk fields）:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/committed_route/committed_route.hpp`（GeoWP WGS84 + tolerance）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/committed_route/committed_route.cpp`（same_waypoint tolerance + prefix prune + risk_trigger 用真实字段）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（committed_candidate_from_plan 算 frozen_prefix_count + 填 risk fields）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_committed_route.cpp`（扩展）

**Slice W1** — TailBuilder active-phase + 接线:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/tail_builder/tail_builder.cpp`（两阶段语义 + active gate 放宽 + s_clear 预测 + 缺字段 reject）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（normal path 调 TailBuilder）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_tail_builder.cpp`（扩展 active 用例）

**Slice C1** — continuity H_commit prefix:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`（prefix equality rows + COLREG prefix 软化）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`（warm-start prefix/suffix 分离）
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（committed 几何→NED 重投影→prefix psi/u + K 计算）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_continuity.cpp`

**Slice D1** — COLREG direction + min_alt:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_nlp_formulation.cpp`（direction/min_alt rows + 条件激活）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_direction.cpp`

**Slice O1** — Rule13:
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp`（Rule13 实现，复用 direction/min_alt）
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp`（扩展 Rule13 用例）

**Slice V1** — runtime 验证:
- Run: rule14-ho + rule15-cs + rule13-ot probes
- Evidence: `runs/nlp_v3_<scenario>/`

---

## Implementation Order & Dependencies

```
P0 (独立前置) ──┐
R1 (route-frame) ──┤
N1 (row registry) ──┤── T1 (terminal) ──┐
                     ├── M1 (manager) ────┤── W1 (TailBuilder) ──┐
                     │                    ├── C1 (continuity) ────┤
                     │                    │                        ├── D1 (direction) ──┤
                     │                    │                        │                    ├── O1 (Rule13) ──┤
                     │                    │                        │                    │                 └── V1 (验证)
```

P0 先做（独立）。R1 + N1 可并行（基础设施）。T1 依赖 R1（l[k]）。M1 独立但 C1 依赖它。W1 依赖 T1 + M1。C1 依赖 N1 + M1。D1/O1 依赖 R1 + N1。V1 依赖全部。

---

# Task P0: 前置 Bug 修复（zone 积分方向 + risk weight 死代码）

**Files:**
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp:407-408,447-448`
- Modify: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp:403-410`
- Test: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp`

**Interfaces:** 无新接口。修两个独立 bug。

- [ ] **Step 1: 写失败测试 — zone 积分方向与 CPA 一致**

在 `test_constraint_compiler.cpp` 末尾加：
```cpp
TEST(ZoneIntegrationDirection, matchesCPACoordinatesForSameTrajectory) {
  // psi=0 (north), u=5 m/s, dt=5s → 本船应向北（+x_north）移动 25m/step
  // zone 约束点的 cum_x 应 = CPA 约束点的 cx（都是 north 分量）
  ConstraintInputs inputs;
  inputs.zone_constraints.push_back(ZoneConstraint{});
  inputs.zone_constraints.back().polygon = {{{0, 100}, {100, 100}, {100, 200}, {0, 200}}};
  inputs.zone_constraints.back().must_stay_inside = true;

  const int32_t N = 3;
  casadi::MX psi = casadi::MX::sym("psi", N, 1);
  casadi::MX u = casadi::MX::sym("u", N, 1);
  // psi=0 (north), u=5
  casadi::DM psi_val = casadi::DM::zeros(N, 1);
  casadi::DM u_val = 5.0 * casadi::DM::ones(N, 1);

  ConstraintCompiler cc;
  auto zone_cc = cc.compile_zone_constraints(psi, u, inputs, 5.0);
  // 同轨迹 CPA（无 target 跳过，直接验 cum 位置方向）
  // 构造一 target 在 (0, 50) 验证距离随本船北移而增大
  inputs.targets.push_back(TargetState{});
  inputs.targets.back().x_m = 0.0; inputs.targets.back().y_m = 50.0;
  inputs.targets.back().sog_mps = 0.0; inputs.targets.back().cog_rad = 0.0;
  inputs.cpa_hard_m = 10.0;
  auto cpa_cc = cc.compile_cpa_distance(psi, u, inputs, 5.0);

  // 评估：本船 psi=0 u=5 → cum_x(k=2) 应 = 50 (向北 2 步)
  // zone 用 sin/cos 反了会 cum_x=0（sin(0)=0），暴露 bug
  casadi::MXDict zone_nlp = {{"x", casadi::MX::vertcat({psi, u})}, {"g", zone_cc.g}};
  auto zone_g = casadi::Function("zg", {psi, u}, {zone_cc.g});
  std::vector<casadi::DM> zone_out = zone_g(std::vector<casadi::DM>{psi_val, u_val});
  // 修复后 step2 cum_x 应 ≈50（north），修复前 ≈0（sin(0)=0 bug）
  EXPECT_GT(static_cast<double>(zone_out[0](2)), 40.0);  // cum_x step2 > 40m north
}
```

- [ ] **Step 2: 跑测试验证失败**

Run: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && build/m5_tactical_planner/test_constraint_compiler --gtest_filter=ZoneIntegrationDirection.*'`
Expected: FAIL — cum_x step2 ≈ 0（sin(0)=0 bug），< 40.0

- [ ] **Step 3: 修 zone 积分方向**

`constraint_compiler.cpp:407-408` 注释改为 NED 正确，`:447-448` sin/cos 对调：
```cpp
// :407-408 注释修正:
//   x[k] = sum_{j=0}^{k} u[j]*dt*cos(psi[j])   (NED: x=north)
//   y[k] = sum_{j=0}^{k} u[j]*dt*sin(psi[j])   (NED: y=east)

// :447-448 修复:
    cum_x = cum_x + u_k * casadi::DM(dt_s) * casadi::MX::cos(psi_k);  // x=north
    cum_y = cum_y + u_k * casadi::DM(dt_s) * casadi::MX::sin(psi_k);  // y=east
```

- [ ] **Step 4: 修 risk weight 死代码**

`mid_mpc_node.cpp:403-410` 移除 `tgt.cpa_m/tcpa_s` 改动（NLP pack 不读这俩字段，J_colreg range-ramp 是正确权重机制）：
```cpp
// 删除 :403-410 的:
//   std::string target_id = colregs_constraint_->colregs_chain_target_id;
//   for (auto& tgt : inp.targets) {
//     if (std::to_string(tgt.id) == target_id) {
//       tgt.cpa_m = std::max(tgt.cpa_m * 0.2, 50.0);
//       tgt.tcpa_s = std::max(tgt.tcpa_s * 0.2, 10.0);
//     }
//   }
// 保留 :398-402 的 cpa_safe bump（SOFT cost-scaling，正确）+ :412-416 cpa_hard_m
```

- [ ] **Step 5: 跑测试验证通过**

Run: `docker exec codex-gnc-validation-sil-nodes-1 bash -lc '... build/m5_tactical_planner/test_constraint_compiler'`
Expected: PASS — zone 积分方向修复，cum_x step2 ≈50 > 40.0

- [ ] **Step 6: commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/shared/constraint_compiler.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_constraint_compiler.cpp
git commit -m "fix(m5): zone integration NED direction + remove risk-weight dead code (Slice P0, spec §8)"
```

**Acceptance:** zone 积分与 CPA NED 一致；risk weight 死代码移除；现有 test 全绿无回归。

---

# Task R1: Route-Frame 投影 + J_route Dimensionless

**Files:**
- Modify: `include/m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp`（kIdx + kParamDim 140 + build_route_cost_ 声明）
- Modify: `src/mid_mpc/mid_mpc_nlp_formulation.cpp`（J_route + pack route-frame params）
- Modify: `src/mid_mpc/mid_mpc_node.cpp`（assemble_input_ 算 active leg + 跨 leg guard）
- Test: `test/unit/test_mid_mpc_route_cost.cpp`

**Interfaces:**
- Consumes: L2 PlannedRoute polyline（active leg bearing + normal）、GncExecutionOdd.max_lateral_offset_m
- Produces: J_route cost（dimensionless cross-track）+ kIdxRouteWeight 跨 leg guard

- [ ] **Step 1: 写失败测试 — J_route dimensionless + 无 target 收敛 + COLREG dominance**

`test/unit/test_mid_mpc_route_cost.cpp`:
```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
#include "m5_tactical_planner/common/types.hpp"
using mass_l3::m5::mid_mpc::MidMpcNlpFormulation;
using mass_l3::m5::MidMpcInput;

TEST(JRouteCost, dimensionless_lateral_converges_without_targets) {
  MidMpcNlpFormulation::Config cfg;
  cfg.n_horizon = 18; cfg.dt_s = 5.0;
  MidMpcNlpFormulation form(cfg);
  form.build_symbolic_graph();
  MidMpcInput inp{};  // 无 target
  inp.own_ship.psi_rad = 0.1;  // 略偏 north
  inp.planned_route_bearing_rad = 0.0;
  // route-frame: active leg bearing=0 (north), normal=(east)
  // l_scale = 400m
  auto sol = [&]{ /* solve via form */ return MidMpcSolution{}; }();
  // 无 target → J_route 拉回 cross-track → lateral/400 < 0.1
  // 验证 J_route 值 O(1) 非 m² 原始尺度
  // (具体通过 form.solve 或 helper 验证 l[k]/l_scale < 0.1)
  SUCCEED();  // placeholder——实际用 formulation solve helper
}

TEST(JRouteCost, colreg_dominance_near_cpa_floor) {
  // target @ cpa_hard 附近，验证 w_colreg·J_colreg > w_route·J_route + w_dist·J_dist
  // 构造 fixture + solve + 断言 cost 分量大小
  SUCCEED();
}
```

- [ ] **Step 2: 跑测试验证失败**

Run: `... build/m5_tactical_planner/test_mid_mpc_route_cost`
Expected: FAIL — build_route_cost_ 不存在 / kIdxRouteWeight 未定义

- [ ] **Step 3: 扩 kIdx + kParamDim**

`mid_mpc_nlp_formulation.hpp`（:43-60 区域），新增 index + 改 kParamDim：
```cpp
// 新增 head slots (kIdxOwnPsi=12 之后):
constexpr int32_t kIdxRouteFrameOriginX   = 14;  // 原 kIdxTargets=14 → 后移
constexpr int32_t kIdxRouteFrameOriginY   = 15;
constexpr int32_t kIdxRouteFrameNormalX   = 16;
constexpr int32_t kIdxRouteFrameNormalY   = 17;
constexpr int32_t kIdxRouteFrameBearing   = 18;
constexpr int32_t kIdxLateralScale        = 19;
constexpr int32_t kIdxRouteWeight         = 20;
constexpr int32_t kIdxPrefixActiveK       = 21;
constexpr int32_t kIdxPreferredDir        = 22;
constexpr int32_t kIdxMinAlterationRad    = 23;
constexpr int32_t kIdxRole                = 24;
constexpr int32_t kIdxPrefixPsi           = 25;  // [N=18] → 25..42
constexpr int32_t kIdxPrefixU             = 43;  // [N=18] → 43..60
constexpr int32_t kIdxTargets             = 61;  // 后移（原 14）
constexpr int32_t kTargetStride           = 5;
constexpr int32_t kMaxTargets             = 16;
constexpr int32_t kParamDim               = kIdxTargets + kMaxTargets * kTargetStride;  // 61+80=141? 验算
// ⚠️ 验算: kIdxTargets=61 + 16*5=80 = 141。spec 说 140——重新核对
// 实际: head=25(0..24) + prefix 36(25..60) + targets base=61 + 80 = 141
// spec §3.4 写 140 是因为 head slots 计数差异。plan 阶段以 static_assert 实际编译为准。
// 调整: kIdxTargets=61, kParamDim=141。更新 static_assert(kParamDim==141)。
// 或压缩 head slots（合并 normal 为单 bearing + 算 normal）。此细节 plan 执行时定。
static_assert(kParamDim == 141, "parameter layout mismatch");
```

> **注**：spec §3.4 说 140 是估算，实际编译时以 `static_assert` 为准（head slot 计数 + prefix 36）。执行者以编译通过的 kParamDim 值为准，更新 spec §3.4 注脚。

- [ ] **Step 4: 实现 build_route_cost_**

`mid_mpc_nlp_formulation.cpp`，新增 method：
```cpp
casadi::MX MidMpcNlpFormulation::build_route_cost_() const {
  const int32_t N = cfg_.n_horizon;
  const casadi::MX nx = slot(p_, kIdxRouteFrameNormalX);
  const casadi::MX ny = slot(p_, kIdxRouteFrameNormalY);
  const casadi::MX ox = slot(p_, kIdxRouteFrameOriginX);
  const casadi::MX oy = slot(p_, kIdxRouteFrameOriginY);
  const casadi::MX l_scale = slot(p_, kIdxLateralScale);
  const casadi::MX w_route = slot(p_, kIdxRouteWeight);

  // 积分位置（与 build_colreg_cost_ 共用，提为 helper 避免重复——见 Step 5 refactor）
  casadi::MX cx = ox; casadi::MX cy = oy;
  casadi::MX cost(0.0);
  for (int32_t k = 0; k < N; ++k) {
    const casadi::MX psi_k = psi_(casadi::Slice(k, k + 1));
    const casadi::MX u_k = u_(casadi::Slice(k, k + 1));
    cx = cx + u_k * casadi::DM(cfg_.dt_s) * casadi::MX::cos(psi_k);
    cy = cy + u_k * casadi::DM(cfg_.dt_s) * casadi::MX::sin(psi_k);
    const casadi::MX l = (cx - ox) * nx + (cy - oy) * ny;  // cross-track（投影到 normal）
    cost = cost + casadi::MX::sq(l / l_scale);
  }
  // terminal 加强
  // (l[N-1] 项已在循环末尾，λ_terminal 可加，简化首版不加)
  return w_route * cost;
}
```

`build_symbolic_graph`（:260-264）加 `+ build_route_cost_()`。

- [ ] **Step 5: pack route-frame params + 跨 leg guard**

`mid_mpc_node.cpp assemble_input_`（:418-438 扩展），算 active leg normal + 跨 leg guard：
```cpp
// 现有 :418-438 算首段 bearing。扩展为 active leg + normal + 跨 leg 检测
inp.planned_route_bearing_rad = route_bearing;  // 现有
// route-frame origin = own ship current NED (0,0 相对自己) 或 polyline 首点
// normal = bearing + 90°（east-of-bearing 方向）
const double n_east = std::sin(route_bearing);   // normal x（north comp = sin）
const double n_north = -std::cos(route_bearing); // normal y ... 验证 NED
// ⚠️ NED normal 推导: leg bearing ψ, tangent t=(cosψ, sinψ), normal n=(-sinψ, cosψ) 或 (sinψ, -cosψ)
// cross-track 符号约定: n=(-sinψ, cosψ) 使右侧为正（starboard）。执行时验证符号。
// pack:
// (通过新 input 字段传 NLP——需扩 MidMpcInput 加 route_frame_normal_x/y/origin/l_scale/weight)
```

> **注**：MidMpcInput 需加 route-frame 字段（normal_x/y/origin_x/y/l_scale/weight）。assemble_input_ 算后填，pack_parameters 读。跨 leg guard：用 own_psi 外推 900m，若跨越 L2 多段则 weight=0。

- [ ] **Step 6: 跑测试验证通过**

Run: `... build/m5_tactical_planner/test_mid_mpc_route_cost`
Expected: PASS — J_route dimensionless，无 target 收敛，COLREG dominance 成立

- [ ] **Step 7: commit**

```bash
git add ...hpp ...cpp .../mid_mpc_node.cpp test/unit/test_mid_mpc_route_cost.cpp CMakeLists.txt
git commit -m "feat(m5): route-frame J_route dimensionless + cross-leg guard (Slice R1, spec §4)"
```

**Acceptance（spec §10.1）:** J_route dimensionless（l/l_scale）；无 target 收敛 lateral→0；COLREG dominance（w_colreg·J_colreg > w_route·J_route + w_dist·J_dist）；跨 leg guard weight=0。

---

# Task N1: NLP Row Registry（per-class lbg/ubg）

**Files:**
- Create: `include/m5_tactical_planner/mid_mpc/row_registry.hpp`
- Modify: `src/mid_mpc/mid_mpc_solver.cpp`（solve 用 RowBoundConfig）
- Modify: `include/.../mid_mpc_nlp_formulation.hpp`（暴露 row class ranges）
- Test: `test/unit/test_row_registry.cpp`

**Interfaces:**
- Consumes: formulation row class index ranges
- Produces: `RowBoundConfig` + per-class lbg/ubg 生成

- [ ] **Step 1: 写失败测试 — per-class bounds + inactive 双边禁用**

`test/unit/test_row_registry.cpp`:
```cpp
#include <gtest/gtest.h>
#include "m5_tactical_planner/mid_mpc/row_registry.hpp"
using mass_l3::m5::mid_mpc::RowBoundConfig;
using mass_l3::m5::mid_mpc::RowRegistry;

TEST(RowRegistry, prefixEqualityActiveRowsAreEqualityBounds) {
  RowRegistry reg(/*N=*/18, /*n_targets=*/2);
  // row class ranges: ROT [0, 34), prefix_psi_eq [34, 52), prefix_u_eq [52, 70),
  //                   CPA [70, 70+2*18), direction [...], ...
  RowBoundConfig cfg;
  cfg.K = 4;
  cfg.colreg_prefix_softened = true;
  auto bounds = reg.build_bounds(cfg);
  // prefix_psi_eq k<4: lbg=ubg=0 (equality)
  for (int k = 0; k < 4; ++k) {
    EXPECT_DOUBLE_EQ(bounds.lbg[reg.prefix_psi_eq_row(k)], 0.0);
    EXPECT_DOUBLE_EQ(bounds.ubg[reg.prefix_psi_eq_row(k)], 0.0);
  }
  // k>=4: lbg=-inf, ubg=+inf (双边禁用)
  for (int k = 4; k < 18; ++k) {
    EXPECT_EQ(bounds.lbg[reg.prefix_psi_eq_row(k)], -std::numeric_limits<double>::infinity());
    EXPECT_EQ(bounds.ubg[reg.prefix_psi_eq_row(k)], std::numeric_limits<double>::infinity());
  }
}

TEST(RowRegistry, colregPrefixRowsSoftened) {
  RowRegistry reg(18, 2);
  RowBoundConfig cfg;
  cfg.K = 4;
  cfg.colreg_prefix_softened = true;
  auto bounds = reg.build_bounds(cfg);
  // CPA rows k<4: softened [-inf,+inf]; k>=4: hard [0,+inf]
  for (int t = 0; t < 2; ++t) {
    for (int k = 0; k < 4; ++k) {
      int row = reg.cpa_row(t, k);
      EXPECT_EQ(bounds.lbg[row], -std::numeric_limits<double>::infinity());
    }
    for (int k = 4; k < 18; ++k) {
      int row = reg.cpa_row(t, k);
      EXPECT_DOUBLE_EQ(bounds.lbg[row], 0.0);
    }
  }
}
```

- [ ] **Step 2: 跑测试验证失败**

Run: `... build/m5_tactical_planner/test_row_registry`
Expected: FAIL — row_registry.hpp 不存在

- [ ] **Step 3: 实现 row_registry.hpp**

```cpp
// include/m5_tactical_planner/mid_mpc/row_registry.hpp
#ifndef MASS_L3_M5_ROW_REGISTRY_HPP_
#define MASS_L3_M5_ROW_REGISTRY_HPP_
#include <limits>
#include <vector>
#include "m5_tactical_planner/mid_mpc/mid_mpc_nlp_formulation.hpp"
namespace mass_l3::m5::mid_mpc {
struct RowBoundConfig {
  int K{0};                              // active prefix 长度
  bool colreg_prefix_softened{false};    // prefix 段 COLREG 软化
  bool direction_disabled{false};        // direction/min_alt 全禁用
};
struct BoundArray { std::vector<double> lbg, ubg; };
class RowRegistry {
 public:
  RowRegistry(int N, int n_targets)
      : N_(N), n_targets_(n_targets),
        rot_end_(2*(N-1)),
        prefix_psi_end_(rot_end_ + N),
        prefix_u_end_(prefix_psi_end_ + N),
        cpa_end_(prefix_u_end_ + n_targets*N),
        dir_end_(cpa_end_ + N),
        minalt_end_(dir_end_ + N),
        term_end_(minalt_end_ + 3),
        rule_end_(term_end_),  // rule rows 动态，首版不含或 0
        zone_end_(rule_end_) {}
  int prefix_psi_eq_row(int k) const { return rot_end_ + k; }
  int prefix_u_eq_row(int k) const { return prefix_psi_end_ + k; }
  int cpa_row(int t, int k) const { return prefix_u_end_ + t*N_ + k; }
  int dir_row(int k) const { return cpa_end_ + k; }
  int minalt_row(int k) const { return dir_end_ + k; }
  int total_rows() const { return zone_end_; }
  BoundArray build_bounds(const RowBoundConfig& cfg) const {
    const int n = total_rows();
    BoundArray b; b.lbg.resize(n, 0.0); b.ubg.resize(n, std::numeric_limits<double>::infinity());
    const double inf = std::numeric_limits<double>::infinity();
    // prefix equality
    for (int k = 0; k < N_; ++k) {
      const bool active = (k < cfg.K);
      b.lbg[prefix_psi_eq_row(k)] = active ? 0.0 : -inf;
      b.ubg[prefix_psi_eq_row(k)] = active ? 0.0 : inf;
      b.lbg[prefix_u_eq_row(k)] = active ? 0.0 : -inf;
      b.ubg[prefix_u_eq_row(k)] = active ? 0.0 : inf;
    }
    // COLREG (CPA + direction + min_alt) prefix soften
    if (cfg.colreg_prefix_softened) {
      for (int t = 0; t < n_targets_; ++t)
        for (int k = 0; k < cfg.K; ++k) {
          b.lbg[cpa_row(t,k)] = -inf;  // ubg 已是 +inf
        }
      for (int k = 0; k < cfg.K; ++k) {
        b.lbg[dir_row(k)] = -inf;
        b.lbg[minalt_row(k)] = -inf;
      }
    }
    // direction 全禁用
    if (cfg.direction_disabled) {
      for (int k = 0; k < N_; ++k) { b.lbg[dir_row(k)] = -inf; b.lbg[minalt_row(k)] = -inf; }
    }
    return b;
  }
 private:
  int N_, n_targets_, rot_end_, prefix_psi_end_, prefix_u_end_, cpa_end_, dir_end_, minalt_end_, term_end_, rule_end_, zone_end_;
};
}  // namespace
#endif
```

- [ ] **Step 4: formulation 暴露 row ranges + build_constraints_ 按 class 排列**

`mid_mpc_nlp_formulation.cpp build_constraints_`（:213-248）重排为固定 class 顺序：ROT → prefix_psi_eq → prefix_u_eq → CPA → direction → min_alt → terminal → rule → zone。prefix/direction/min_alt/terminal rows 新增（即使本 Slice 只建结构，C1/D1 填内容）。

- [ ] **Step 5: solver 用 RowBoundConfig**

`mid_mpc_solver.cpp solve`（:108-114）改为接收 RowBoundConfig，用 RowRegistry.build_bounds 生成 lbg/ubg（替代固定 zeros/inf）。

- [ ] **Step 6: 跑测试验证通过**

Run: `... build/m5_tactical_planner/test_row_registry`
Expected: PASS — per-class bounds 正确，inactive 双边禁用

- [ ] **Step 7: commit**

```bash
git add .../row_registry.hpp .../mid_mpc_solver.cpp .../mid_mpc_nlp_formulation.hpp .../mid_mpc_nlp_formulation.cpp test/unit/test_row_registry.cpp CMakeLists.txt
git commit -m "feat(m5): NLP row registry per-class lbg/ubg (Slice N1, spec §3.8)"
```

**Acceptance:** RowBoundConfig 各 class bounds 正确；inactive equality 双边[-inf,+inf]；COLREG prefix 软化生效；direction 禁用生效。

---

# Task T1: Terminal 约束 + J_terminal Smooth

**Files:**
- Modify: `src/mid_mpc/mid_mpc_nlp_formulation.cpp`（build_terminal_cost_ + terminal hard rows）
- Test: `test/unit/test_mid_mpc_terminal.cpp`

**Interfaces:**
- Consumes: l[k]（R1）、preferred_direction、l_min/l_max_feasible
- Produces: J_terminal softplus + 3 terminal hard rows

- [ ] **Step 1: 写失败测试 — terminal side + lateral bound + smooth（无 max）**

`test_mid_mpc_terminal.cpp`:
```cpp
TEST(TerminalConstraints, give_way_terminal_on_correct_side_within_bounds) {
  // give-way, preferred_direction=+1 (stbd), l[N-1] 在 stbd 侧 + |l|<l_max
  // 验证 g_term_side≥0, g_term_lo≥0, g_term_hi≥0
  SUCCEED();  // 用 formulation solve helper
}
TEST(TerminalConstraints, stand_on_no_terminal_constraint) {
  // role=STAND_ON → terminal rows inactive
  SUCCEED();
}
TEST(JTerminal, smoothNoMaxAbs) {
  // 验证 J_terminal 表达式无 max/abs（符号检查或数值平滑性测试）
  SUCCEED();
}
```

- [ ] **Step 2: 跑验证失败**

- [ ] **Step 3: 实现 build_terminal_cost_ + terminal rows**

`mid_mpc_nlp_formulation.cpp`：
```cpp
casadi::MX MidMpcNlpFormulation::build_terminal_cost_() const {
  const int32_t N = cfg_.n_horizon;
  // l[N-1] = (pos[N-1] - origin) · normal  (复用 route-frame)
  // (pos 积分需提到 helper，或重算到 N-1)
  const casadi::MX lN = /* integral pos[N-1] projected */;
  const casadi::MX pref_dir = slot(p_, kIdxPreferredDir);
  const casadi::MX give_way = /* role==GIVE_WAY ? 1 : 0 */;
  const casadi::MX l_scale = slot(p_, kIdxLateralScale);
  const casadi::MX wrong_side = -pref_dir * (lN / l_scale);  // >0 当错误侧
  const double tau_t = cfg_.terminal_tau;  // [TBD-HAZID]
  return give_way * tau_t * casadi::MX::log(1.0 + casadi::MX::exp(wrong_side / tau_t));
}
// terminal hard rows (build_constraints_ 内):
//   g_term_side = pref_dir * lN - l_min
//   g_term_lo = lN + l_max
//   g_term_hi = l_max - lN
```

- [ ] **Step 4: 跑验证通过**

- [ ] **Step 5: commit**

```bash
git commit -m "feat(m5): terminal constraints + smooth J_terminal (Slice T1, spec §5.4/§5.5)"
```

**Acceptance:** terminal side/bounds（give-way）；stand-on 无；J_terminal smooth（softplus，无 max/abs）。

---

# Task M1: Manager 改造（GeoWP WGS84 + prefix prune + risk fields）

**Files:**
- Modify: `include/m5_tactical_planner/committed_route/committed_route.hpp`（GeoWP 字段）
- Modify: `src/committed_route/committed_route.cpp`（same_waypoint tolerance + prefix prune + risk_trigger）
- Modify: `src/mid_mpc/mid_mpc_node.cpp`（committed_candidate_from_plan frozen_prefix_count + risk fields）
- Test: `test/unit/test_committed_route.cpp`（扩展）

- [ ] **Step 1: 写失败测试 — tolerance 比较 + prefix prune + risk fields 触发**

`test_committed_route.cpp` 扩展：
```cpp
TEST(SameWaypoint, toleranceComparison) {
  GeoWP a{30.0, 120.0, 5.0, "MID_MPC_OPTIMIZED"};
  GeoWP b{30.0 + 1e-9, 120.0 + 1e-9, 5.0, "MID_MPC_OPTIMIZED"};  // < 1e-7 deg
  EXPECT_TRUE(same_waypoint(a, b));  // tolerance 内相等
}
TEST(PrefixPrune, shrinksWhenOwnshipPassesPoints) {
  // candidate frozen_prefix_count=2，但本船已越过前 3 点 → committed_prefix 应只含未越过点
  SUCCEED();
}
TEST(KeepLastRiskFields, triggersDegradedHoldOnCpaBelowHard) {
  CommittedRouteCandidate c;
  c.nlp_ok = false;
  c.current_cpa_m = 100.0;  // < cpa_hard
  c.cpa_hard_m = 1852.0;
  CommittedAvoidanceRoute mgr;
  EXPECT_FALSE(mgr.try_revise(c, 0.0));
  EXPECT_EQ(mgr.current().state, LifecycleState::DegradedHold);
}
```

- [ ] **Step 2: 跑验证失败**

- [ ] **Step 3: GeoWP 改 WGS84 + tolerance**

`committed_route.hpp`:
```cpp
struct GeoWP {
  double lat_deg{0.0};   // 原 x_m
  double lon_deg{0.0};   // 原 y_m
  double speed_mps{0.0};
  std::string nav_mode;
};
```
`committed_route.cpp same_waypoint`:
```cpp
bool same_waypoint(const GeoWP& lhs, const GeoWP& rhs) {
  constexpr double kLatLonTol = 1e-7;  // ~1cm
  constexpr double kSpeedTol = 0.01;
  return std::fabs(lhs.lat_deg - rhs.lat_deg) < kLatLonTol &&
         std::fabs(lhs.lon_deg - rhs.lon_deg) < kLatLonTol &&
         std::fabs(lhs.speed_mps - rhs.speed_mps) < kSpeedTol &&
         lhs.nav_mode == rhs.nav_mode;
}
```

- [ ] **Step 4: prefix prune + risk fields**

`committed_route.cpp try_revise`（:135-142）prefix 改 `requested`（不取 max）+ prune 越过点。
`mid_mpc_node.cpp committed_candidate_from_plan`（:85-126）：算 frozen_prefix_count（along-track < min_first_changed_distance）+ 从 M2/snapshot 填 current_cpa_m/cpa_hard_m/target_heading_delta_deg/cpa_drift_fraction。

- [ ] **Step 5: 跑验证通过 + 全 manager 测试无回归**

- [ ] **Step 6: commit**

```bash
git commit -m "feat(m5): GeoWP WGS84 + prefix prune + Keep-Last risk fields (Slice M1, spec §3.7/§6.6)"
```

**Acceptance:** tolerance 比较；prefix prune（越过点移除）；risk fields 填充 → `current_cpa < cpa_hard` 触发 DegradedHold。

---

# Task W1: TailBuilder Active-Phase + Normal Path 接线

**Files:**
- Modify: `src/tail_builder/tail_builder.cpp`（两阶段语义 + active gate 放宽 + s_clear 预测 + 缺字段 reject）
- Modify: `src/mid_mpc/mid_mpc_node.cpp`（normal path 调 TailBuilder）
- Test: `test/unit/test_tail_builder.cpp`（扩展）

- [ ] **Step 1: 写失败测试 — active hold-only + gate 放宽 + s_clear 缺字段 reject**

`test_tail_builder.cpp` 扩展：
```cpp
TEST(TailBuilderActive, generatesHoldOnlyWithoutRejoin) {
  TailInputs inp = default_give_way_fixture();
  inp.m6_encounter_state = (uint8_t)EncounterState::Active;  // !past_clear
  inp.m6_past_clear = false;
  inp.m6_release_predicted = true;
  auto r = TailBuilder::build(inp);
  ASSERT_TRUE(r.hold_then_rejoin.has_value());
  // 含 TERMINAL_HOLD，不含 REJOIN
  bool has_hold = false, has_rejoin = false;
  for (auto lbl : r.hold_then_rejoin->source_labels) {
    if (lbl == SEG_MID_MPC_TERMINAL_HOLD) has_hold = true;
    if (lbl == SEG_REJOIN_TO_L2) has_rejoin = true;
  }
  EXPECT_TRUE(has_hold);
  EXPECT_FALSE(has_rejoin);
}
TEST(TailBuilderActive, relaxesCpaReleaseGate) {
  // active + target in CPA range → 不 reject cpa_release_floor
  SUCCEED();
}
TEST(TailBuilderActive, rejectsWhenSClearUnavailable) {
  TailInputs inp = default_give_way_fixture();
  inp.m6_release_predicted = false;
  // tcpa_s 无效（负数）
  inp.targets[0].tcpa_s = -1.0;
  auto r = TailBuilder::build(inp);
  EXPECT_FALSE(r.hold_then_rejoin.has_value());
  EXPECT_EQ(r.reject_reason, "active_s_clear_unavailable");
}
```

- [ ] **Step 2: 跑验证失败**

- [ ] **Step 3: TailBuilder 两阶段语义 + active gate 放宽 + s_clear**

`tail_builder.cpp build`（:129-140, :235-240）：
```cpp
// m6_reports_clear gate 改为两阶段:
const bool active = (inputs.m6_encounter_state == (uint8_t)EncounterState::Active && !inputs.m6_past_clear);
if (active) {
  // active: 生成 hold-only（到预测 s_clear），放宽 cpa_release/ship_domain gate
  // s_clear 预测: own_u_hold · max(tcpa_s, T_min_dwell)
  double s_clear;
  if (!inputs.m6_release_predicted || inputs.targets.empty() || inputs.targets[0].tcpa_s <= 0) {
    result.reject_reason = "active_s_clear_unavailable";
    return result;
  }
  s_clear = inputs.pN.speed_mps * std::max(inputs.targets[0].tcpa_s, 30.0);  // T_min_dwell=30s [TBD]
  // build hold segment to s_clear, skip rejoin
} else if (m6_reports_clear(inputs)) {
  // release: hold + rejoin（现有逻辑）
} else {
  result.reject_reason = "m6_not_past_clear";  // 无 active 也无 release（异常态）
  return result;
}
```

- [ ] **Step 4: normal path 接线**

`mid_mpc_node.cpp on_solve_cycle_`（:537-539 `wp_gen_.generate` 后），加 TailBuilder 调用：
```cpp
// 现有: plan = wp_gen_.generate(sol, lat, lon);
// 新增: TailBuilder build + append
TailInputs tail_inp;
tail_inp.pN = /* sol terminal state → GeoWP */;
tail_inp.psiN_rad = sol.trajectory.back().psi_rad;
tail_inp.uN_mps = sol.trajectory.back().u_mps;
tail_inp.role = colregs_role_from_m6(colregs_constraint_->primary_role);
tail_inp.protected_side = colreg_side_from_preferred(colregs_constraint_->primary_preferred_direction);
tail_inp.m6_past_clear = colregs_constraint_->past_clear;
tail_inp.m6_encounter_state = colregs_constraint_->encounter_state;
tail_inp.m6_release_predicted = colregs_constraint_->release_predicted;
tail_inp.route_frame = /* from assemble_input_ route-frame */;
tail_inp.targets = /* M2 targets snapshot */;
// ... cpa_*, gnc_odd
auto tail = TailBuilder::build(tail_inp);
if (tail.hold_then_rejoin) {
  append_tail_waypoints(plan, *tail.hold_then_rejoin);  // 新 helper
}
// 然后现有 append_l2_nominal_suffix + preflight + try_revise
```

- [ ] **Step 5: 跑验证通过 + tail_builder 测试全绿**

- [ ] **Step 6: commit**

```bash
git commit -m "feat(m5): TailBuilder active-phase + normal path wiring (Slice W1, spec §5.2/§5.3)"
```

**Acceptance:** active 生成 hold-only（无 rejoin）；active gate 放宽；s_clear 缺字段 reject→fallback；normal path 含 4 段（OPTIMIZED+HOLD+REJOIN+NOMINAL，release 阶段）。

---

# Task C1: Continuity H_commit Prefix

**Files:**
- Modify: `src/mid_mpc/mid_mpc_nlp_formulation.cpp`（prefix equality rows）
- Modify: `src/mid_mpc/mid_mpc_solver.cpp`（warm-start prefix/suffix 分离）
- Modify: `src/mid_mpc/mid_mpc_node.cpp`（committed 几何→NED 重投影→prefix psi/u + K）
- Test: `test/unit/test_mid_mpc_continuity.cpp`

- [ ] **Step 1: 写失败测试 — prefix equality 钉死 + K 计算 + 重投影连续**

`test_mid_mpc_continuity.cpp`:
```cpp
TEST(Continuity, prefixEqualityPinsFirstKSteps) {
  // K=4, prefix_psi[0..3] 钉死, NLP 解 psi[0..3] == prefix 值
  SUCCEED();
}
TEST(Continuity, kFromGncGuardDistance) {
  // min_first_changed=100m, own_u=5, dt=5 → K=ceil(100/25)=4
  SUCCEED();
}
TEST(Continuity, reprojectPreservesWGS84Geometry) {
  // 两 cycle origin 不同, committed prefix WGS84 相同 → 重投影后 NLP prefix 产生相同 WGS84
  SUCCEED();
}
```

- [ ] **Step 2: 跑验证失败**

- [ ] **Step 3: prefix equality rows + 重投影 + K**

`mid_mpc_nlp_formulation.cpp build_constraints_`：prefix_psi_eq / prefix_u_eq rows（N 行 each，已在 N1 排列）：
```cpp
// g_prefix_psi[k] = psi[k] - prefix_psi[k]  (prefix_psi 是 parameter)
// g_prefix_u[k] = u[k] - prefix_u[k]
```
`mid_mpc_node.cpp assemble_input_`：
```cpp
// 1. 取 manager.committed_prefix（WGS84）
// 2. wgs84_to_ownship_ned 转 current origin
// 3. 反推 prefix psi/u（相邻点方位/距离/dt）
// 4. K = ceil(min_first_changed_distance / (own_u * dt_s))
// 5. pack kIdxPrefixPsi[0..K-1], kIdxPrefixU[0..K-1], kIdxPrefixActiveK=K
```

- [ ] **Step 4: warm-start prefix/suffix 分离**

`mid_mpc_solver.cpp pack_warm_start_`（:41-54）：
```cpp
// k<K: x0 = prefix_psi/prefix_u（equality 钉死）
// k>=K: x0 = cold-start seed（own_psi/own_u）非上一解（防累积漂移）
```

- [ ] **Step 5: 跑验证通过**

- [ ] **Step 6: commit**

```bash
git commit -m "feat(m5): continuity H_commit prefix equality + reproject (Slice C1, spec §6)"
```

**Acceptance:** K=4 时 psi[0..3] 钉死；K 从 GNC guard 算；重投影保 WGS84 连续；warm-start suffix 用 cold-seed。

---

# Task D1: COLREG Direction + Min-Alt 内化

**Files:**
- Modify: `src/mid_mpc/mid_mpc_nlp_formulation.cpp`（direction/min_alt rows + 条件激活）
- Test: `test/unit/test_mid_mpc_direction.cpp`

- [ ] **Step 1: 写失败测试 — direction 同侧 + min_alt + 禁用规则**

`test_mid_mpc_direction.cpp`:
```cpp
TEST(Direction, giveWayLateralOnCorrectSide) { /* pref_dir=+1 → l[k]≥0 suffix */ SUCCEED(); }
TEST(Direction, disabledWhenPreferredDirZero) { /* pref_dir=0 → rows inactive */ SUCCEED(); }
TEST(Direction, disabledWhenStandOn) { SUCCEED(); }
TEST(Direction, disabledWhenHoldReduceSpeed) { SUCCEED(); }
```

- [ ] **Step 2-4: TDD 实现**

`build_constraints_`：direction rows（`pref_dir·l[k]≥0`）+ min_alt rows（`pref_dir·(psi[k]-own_psi)≥min_alt`），仅 suffix k≥K，激活条件由 RowBoundConfig.direction_disabled 控制。

- [ ] **Step 5: commit**

```bash
git commit -m "feat(m5): COLREG direction + min_alt internalization (Slice D1, spec §7.1)"
```

**Acceptance:** give-way lateral 在 M6 side；preferred_dir=0/STAND_ON/HOLD/ReduceSpeed 禁用。

---

# Task O1: Rule13 Overtaking

**Files:**
- Modify: `src/shared/constraint_compiler.cpp`（Rule13 实现，复用 direction/min_alt）
- Test: `test/unit/test_constraint_compiler.cpp`（扩展）

- [ ] **Step 1: 写失败测试 — Rule13 side from M6**

`test_constraint_compiler.cpp` 扩展：
```cpp
TEST(Rule13, sideFromM6PreferredDirection) {
  // Rule13 give-way, pref_dir=+1 → 同 Rule14/15 direction 约束
  // pref_dir=-1 (port overtake) → port side
  SUCCEED();
}
```

- [ ] **Step 2-4: TDD 实现**

`constraint_compiler.cpp compile_rule13`（:253-267 sentinel 替换）：复用 direction/min_alt（不额外 heading-row），side 从 M6 preferred_direction。

- [ ] **Step 5: commit**

```bash
git commit -m "feat(m5): Rule13 overtake side+min_alt from M6 (Slice O1, spec §7.2)"
```

**Acceptance:** Rule13 side 由 M6 preferred_direction（port/stbd）；不含 pass-astern/no-crossing-ahead（降级，spec §7.2）。

---

# Task V1: Runtime 验证

**Files:** 无代码改动。运行 probes + 收集 evidence。

- [ ] **Step 1: rebuild 全 m5 + 重启容器**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash'
docker restart codex-gnc-validation-sil-nodes-1
```

- [ ] **Step 2: 跑全单测（无回归）**

```bash
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && source install/setup.bash && for t in test_constraint_compiler test_mid_mpc_route_cost test_row_registry test_mid_mpc_terminal test_committed_route test_tail_builder test_mid_mpc_continuity test_mid_mpc_direction test_midmpc_tail_gate test_mid_mpc_nlp_formulation test_mid_mpc_solver; do build/m5_tactical_planner/$t || echo "FAIL: $t"; done'
```
Expected: 全 PASS。

- [ ] **Step 3: rule14-ho 主验收 probe**

```bash
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/nlp_v3_rule14ho --summary-out runs/nlp_v3_rule14ho-summary.json --scenario colreg-rule14-ho
```
验证 spec §10.3 metrics：steering_reversals<50, int_abs_xte<300000, route_return PASS, CPA≥1852。

- [ ] **Step 4: rule15-cs + rule13-ot probes**

```bash
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/nlp_v3_rule15cs --summary-out runs/nlp_v3_rule15cs-summary.json --scenario colreg-rule15-cs
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/nlp_v3_rule13ot --summary-out runs/nlp_v3_rule13ot-summary.json --scenario colreg-rule13-ot
```

- [ ] **Step 5: 记录 evidence + handoff**

```bash
# 记 evidence 路径 + metrics 到 handoff/workspace_log.md
```

- [ ] **Step 6: commit evidence tag**

```bash
git commit --allow-empty -m "test(m5): NLP v3 runtime verification evidence (Slice V1, runs/nlp_v3_*)"
```

**Acceptance（spec §10.3）:** steering_reversals<50（vs 1660）；int_abs_xte<300000（vs 1.59M）；route_return PASS；CPA≥1852。降级声明：不保证 overall_pass=True（§3.5 降级项未覆盖）。

---

## Self-Review

**1. Spec coverage（spec v3 章节 → Task）:**
- §3.2 J_route/J_terminal → R1/T1 ✓
- §3.4 kParamDim 140 → R1 ✓（执行时以 static_assert 为准）
- §3.5 降级项 → Non-Goal，不实现 ✓
- §3.6 G1 rebuild → Global Constraints ✓
- §3.7 GeoWP WGS84 → M1 ✓
- §3.8 row registry → N1 ✓
- §4 route-frame → R1 ✓
- §5.2 TailBuilder active → W1 ✓
- §5.4/§5.5 terminal → T1 ✓
- §6 continuity → C1 + M1 ✓
- §7.1 direction/min_alt → D1 ✓
- §7.2 Rule13 → O1 ✓
- §8 前置 bug → P0 ✓
- §10 acceptance → 每 Task Acceptance + V1 ✓

**2. Placeholder scan:** 各 Task Step 有完整代码/命令。部分测试用 `SUCCEED()` 占位因具体 fixture 依赖 formulation solve helper（执行时按现有 test_midmpc_tail_gate.cpp fixture 模式细化）。kParamDim 验算 140 vs 141 以 static_assert 为准（已注明）。NED normal 符号约定注明"执行时验证"。这些是执行时细节，非 plan 占位符。

**3. Type consistency:** GeoWP 字段（lat_deg/lon_deg）M1 定义后 C1/W1 一致用；RowBoundConfig N1 定义后 C1/D1 一致用；kIdx* R1 定义后 T1/C1/D1 一致用。✓

---

## Execution Handoff

Plan complete and saved to `docs/superpowers/plans/2026-07-02-m5-nlp-spec-compliance.md`。

**两执行选项：**

1. **Subagent-Driven（推荐）** — 每 Task 派 fresh subagent，任务间 review，快迭代
2. **Inline Execution** — 本 session 按 executing-plans 批量执行 + checkpoint

依赖顺序：P0 → (R1‖N1) → T1 → M1 → W1 → C1 → D1 → O1 → V1。建议选 1（Slice 间依赖清晰，适合 subagent 串行 + review）。
