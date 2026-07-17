# P3: per-target ξ 行为验证 + ρ 校准 + 测试缺口填补 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 闭环 P3 三个缺口:① ρ(zl=1e3)exact-penalty SIL 实测验证;② per-target ξ 独立性 + 精确性单测;③ ξ 可观测性增强(per-target breakdown)。formulation 不改(ξ+L1/L2 已 P1b-1b 落地)。

**Architecture:** 只加测试 + solver/node 可观测性字段 + SIL 实测脚本。formulation/gen script 不改(除非 SIL 实测证明 zl 不够 → 条件性调 RHO_LIN)。

**Tech Stack:** C++17, acados 0.44, ROS2 ament_cmake, colcon, gtest, colregs-probe skill / run_colregs_*.py(SIL)

**Spec:** `docs/superpowers/specs/2026-07-18-m5-p3-slack-validation-design.md`
**生产现状(只读参考):**
- `src/mid_mpc/mid_mpc_acados_solver.cpp` L586-589(ξ 提取 per-stage "sl" length 16)+ L931/1000(cpa_slack max)+ L1205(node JSON)
- `include/m5_tactical_planner/common/types.hpp` L295 `MidMpcSolution` + L315 `cpa_slack`(标量)
- `test/unit/test_mid_mpc_acados_solver.cpp` L60-130 `AcadosSolverTest` fixture + `straight_line()` builder
- `test/external/acados_backend/gen_mid_mpc_acados.py` L154-162(Zl=1e2/zl=1e3)
- `scenarios/IMAZU标准测试/imazu-{06,10,15,17,18,21}-ms.yaml`(多船 SIL 场景)

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,HEAD c88165fb8)
- **formulation 不改**:ξ 结构(per-target per-step)+ 混合 L1/L2(Zl=1e2/zl=1e3)已 P1b-1b 落地,P3 不重写。
- **条件性调 zl**: 仅当 SIL 实测证明 zl=1e3 不满足 exact-penalty(feasible ξ>tol)时,改 gen script RHO_LIN(调大如 1e4/1e5)+ 重测。调 zl 是数值改动,须 SIL 证据支撑,不凭感觉。
- **测试标志**: `BUILD_TESTING` + 包名 `m5_tactical_planner`。
- **容器内执行**: `source scripts/a4000-env.sh`;`COMPOSE_PROJECT_NAME=codex-m5-p3`,不碰 mass-l3-sil demo stack。
- **M5_USE_ACADOS**: ξ 行为只在 =ON 时激活;单测 + SIL 都须 ON。回归门 OFF 也跑(IPOPT 路径不受影响)。
- **诚实纪律**: ξ 独立性/精确性单测用构造的 MidMpcInput(可控 CPA 可达/不可达),非 tautology;SIL 实测真跑 imazu-*-ms,非声称。
- 每 task 一 commit;TDD(先写失败测试,再实现)。

---

## File Structure

| 文件 | 责任 | P3 改动 |
|---|---|---|
| `include/m5_tactical_planner/common/types.hpp` | MidMpcSolution 加 `cpa_slack_per_target`(per-target ξ max breakdown) | 改 |
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | 提取 per-target ξ breakdown(除现有标量 max)+ 填 MidMpcSolution | 改 |
| `src/mid_mpc/mid_mpc_node.cpp` | ASDR JSON 加 per-target ξ 字段(认证可见) | 改 |
| `test/unit/test_mid_mpc_acados_solver.cpp` | 加 multi-target MidMpcInput builder + ξ 独立性/精确性/penalty 单测 | 改 |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | **条件性**:若 SIL 实测 zl 不够,调 RHO_LIN | 改(条件性,Task 5) |
| SIL 实测脚本(用 colregs-probe skill) | 跑 imazu-*-ms + 提取 ξ 行为报告 | 新增(用现有工具) |

---

## Task 1: MidMpcSolution 加 per-target ξ breakdown 字段

**Files:**
- Modify: `include/m5_tactical_planner/common/types.hpp`(L295 MidMpcSolution,加 cpa_slack_per_target)

**Interfaces:**
- Produces: `MidMpcSolution::cpa_slack_per_target`(数组/vector,per-target ξ max over stage)

- [ ] **Step 1: 读 MidMpcSolution L295-320 确认 cpa_slack 字段位置**

Run: `sed -n '295,320p' src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp`
Expected: 看到 `double cpa_slack{0.0};` at L315。

- [ ] **Step 2: 加 cpa_slack_per_target 字段**

在 `cpa_slack` 后加:
```cpp
  // P3: per-target ξ max breakdown (max over stage, per target slot).
  // Length = max_targets (16); slot t is the max |ξ_{t,k}| over stages k.
  // Empty target slots (t >= n_targets) are 0.0. For observability/认证
  // (CCS i-Ship ξ 行为可追溯) + SIL ρ-exact-penalty analysis.
  std::array<double, 16> cpa_slack_per_target{};  // zero-init
```
(用 std::array<16> 固定长度匹配 kAcadosNsh=16;或 std::vector<double> 若偏好动态。查 MidMpcSolution 其他字段风格定。)

- [ ] **Step 3: 编译确认(字段加好,未用)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=ON 2>&1 | tail -3`
Expected: 编译通过(新字段零初始化,不影响现有)。

- [ ] **Step 4: Commit**

```bash
git add include/m5_tactical_planner/common/types.hpp
git commit -m "feat(m5): MidMpcSolution.cpa_slack_per_target field (P3 T1)

Per-target ξ max breakdown (max over stage, per target slot) for
observability + SIL ρ-exact-penalty analysis + CCS certification trace.
Zero-init; existing cpa_slack scalar max unchanged."
```

---

## Task 2: solver 提取 per-target ξ breakdown

**Files:**
- Modify: `src/mid_mpc/mid_mpc_acados_solver.cpp`(L586-589 ξ 提取处 + L925-1000 cpa_slack 聚合处)

**Interfaces:**
- Consumes: Task 1 cpa_slack_per_target 字段
- Produces: solver 填 per-target ξ breakdown 到 MidMpcSolution

- [ ] **Step 1: 读 ξ 提取 + 聚合代码(L580-600, L925-1000)**

Run: `sed -n '580,600p;925,1005p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`
Expected: 看到 L586-589 per-stage "sl" 读取 + L931 cpa_slack_max 聚合。

- [ ] **Step 2: 加 per-target ξ breakdown 聚合**

在 cpa_slack_max 聚合循环(L931 附近,遍历 stage 读 sl_vec),同时聚合 per-target:
```cpp
// 既有:double cpa_slack_max = 0.0;
// P3: per-target breakdown
std::array<double, 16> cpa_slack_per_target{};  // 已在 Task 1 加到 sol
// 在遍历 stage k 读 sl_vec[kAcadosNs] 的循环内:
for (int t = 0; t < kAcadosNsh /*16*/ && t < n_targets; ++t) {
  double xi_t = std::fabs(sl_vec[t]);
  if (xi_t > cpa_slack_per_target[t]) cpa_slack_per_target[t] = xi_t;
}
// solve 结束后:sol.cpa_slack_per_target = cpa_slack_per_target;
```
(具体循环结构按 L925-1000 现有代码定;sl_vec 是 per-stage length-16,每 stage 遍历 t 聚合 max。)

- [ ] **Step 3: 写单测验证 per-target breakdown 提取正确**

扩 `test_mid_mpc_acados_solver.cpp`:构造 1-target infeasible 场景(目标极近 → ξ>0),solve 后断言 `sol.cpa_slack_per_target[0] > 0`(目标 0 的 ξ max)+ 其他 slot ≈ 0(空 target)。

- [ ] **Step 4: 跑测试确认通过**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_solver 2>&1 | tail -5`
Expected: PASSED(per-target breakdown 提取正确)。

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp \
        src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_acados_solver.cpp
git commit -m "feat(m5): solver extracts per-target ξ breakdown (P3 T2)

Aggregate per-target ξ max over stages into MidMpcSolution.cpa_slack_per_target.
Unit test: 1-target infeasible → ξ[0]>0, empty slots≈0."
```

---

## Task 3: node ASDR JSON publish per-target ξ

**Files:**
- Modify: `src/mid_mpc/mid_mpc_node.cpp`(L1195-1220 ASDR JSON 处)

**Interfaces:**
- Consumes: Task 2 sol.cpa_slack_per_target
- Produces: ASDR JSON 加 per-target ξ 字段(M8/认证可见)

- [ ] **Step 1: 读 ASDR JSON 组装(L1195-1220)**

Run: `sed -n '1195,1225p' src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
Expected: 看到 `"cpa_slack":` + slack_buf(JSON 字段)。

- [ ] **Step 2: 加 per-target ξ JSON 字段**

在 `"cpa_slack":` 后加 per-target breakdown(数组形式):
```cpp
// P3: per-target ξ breakdown for observability/认证
std::string per_target_buf = "[";
for (size_t t = 0; t < sol.cpa_slack_per_target.size(); ++t) {
  if (t) per_target_buf += ",";
  char b[32];
  std::snprintf(b, sizeof(b), "%.3f", sol.cpa_slack_per_target[t]);
  per_target_buf += b;
}
per_target_buf += "]";
// JSON 加:  + ",\"cpa_slack_per_target\":" + per_target_buf
```
(在现有 json 字符串拼接加 `+ ",\"cpa_slack_per_target\":" + per_target_buf`。)

- [ ] **Step 3: 验证 ASDR JSON 含新字段(SIL 或单测)**

跑一次 solve(任意场景),检查 ASDR JSON 输出含 `cpa_slack_per_target` 数组字段。
Run: 用 colregs-probe 或单次 scenario run,grep ASDR 输出。
Expected: JSON 含 `"cpa_slack_per_target":[...]`。

- [ ] **Step 4: 回归 — 现有 cpa_slack 标量不破坏**

确认 `cpa_slack` 标量字段仍在 JSON(只加新字段,不改现有)。

- [ ] **Step 5: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp
git commit -m "feat(m5): ASDR JSON publishes per-target ξ breakdown (P3 T3)

Add cpa_slack_per_target array to ASDR diagnostic JSON (after existing
cpa_slack scalar). For M8 observability + CCS i-Ship certification trace
+ SIL ρ-exact-penalty analysis. Backward compatible (new field only)."
```

---

## Task 4: ξ 独立性 + 精确性 + penalty 单测

**Files:**
- Modify: `test/unit/test_mid_mpc_acados_solver.cpp`(加 multi-target builder + 3 case)

**Interfaces:**
- Consumes: Task 1-3 per-target ξ 可观测性

- [ ] **Step 1: 加 multi-target MidMpcInput builder**

参考 `straight_line()`(L105-130)模式,加:
```cpp
static MidMpcInput two_targets_independent() {
  MidMpcInput inp = straight_line();  // base
  // 目标 A:CPA 不可达(极近,迫使 ξ_A>0)
  // 目标 B:CPA 可行(远,应 ξ_B≈0)
  // 填 inp.targets[0] / inp.targets[1](查 MidMpcInput.targets 字段结构)
  // A 近:tx,ty 使 d < cpa_safe;B 远:d >> cpa_safe
  ...
  return inp;
}
```
(查 MidMpcInput.targets 字段结构 — TrackedTarget 或类似,填 x_m/y_m/sog/cog。)

- [ ] **Step 2: 写 ξ 独立性测试(masking 消除,SC-02)**

```cpp
TEST_F(AcadosSolverTest, XiIndependent_NoMasking) {
  // 2-target:A 不可达(ξ_A>0), B 可行(ξ_B≈0)
  // per-target ξ 应独立:B 不被 A 的松弛拖累
  auto inp = two_targets_independent();
  auto sol = solver_->solve(inp, /*warm=*/nullptr);
  ASSERT_EQ(sol.status, SolveStatus::Converged);  // 或容忍 status 4
  // ξ_A > 0(A 不可达,松弛)
  EXPECT_GT(sol.cpa_slack_per_target[0], 1e-3) << "target A (infeasible) must relax ξ_A";
  // ξ_B ≈ 0(B 可行,不被 A 拖累 —— masking 消除)
  EXPECT_LT(sol.cpa_slack_per_target[1], 1e-3) << "target B (feasible) ξ_B must be ~0 (no masking)";
}
```

- [ ] **Step 3: 写 ξ 精确性测试(feasible ξ≈0 / infeasible ξ>0)**

```cpp
TEST_F(AcadosSolverTest, XiExactPenalty_FeasibleZero) {
  // 1-target feasible(远)→ ξ≈0(exact-penalty)
  auto inp = straight_line();
  // 加 1 远目标(d >> cpa_safe)
  ...
  auto sol = solver_->solve(inp, nullptr);
  EXPECT_LT(sol.cpa_slack, 1e-3) << "feasible CPA → ξ≈0 (exact-penalty)";
}

TEST_F(AcadosSolverTest, XiExactPenalty_InfeasiblePositive) {
  // 1-target infeasible(近)→ ξ>0(松弛)
  ...
  EXPECT_GT(sol.cpa_slack, 1e-3) << "infeasible CPA → ξ>0 (relax)";
}
```

- [ ] **Step 4: 写混合 L1/L2 penalty 数值测试**

```cpp
TEST_F(AcadosSolverTest, SlackPenalty_MixedL1L2Value) {
  // 构造已知 ξ,验证 J_slack = ρ·ξ + ½w·ξ²(ρ=1e3, w=1e2 from gen)
  // 用 sol.cost_total 分量或独立 oracle 计算
  // (若 cost 分量不可拆,用 oracle:J_slack(ξ) = 1e3*ξ + 0.5*1e2*ξ²)
  ...
}
```

- [ ] **Step 5: 跑所有新测试**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_solver 2>&1 | tail -10`
Expected: 新 case 全绿(独立性/精确性/penalty)。

- [ ] **Step 6: 若 ξ 独立性/精确性失败,记录(SIL 实测前置输入)**

若 ξ_B 不 ≈0(masking 未消除)或 feasible ξ 不归零(exact-penalty 不满足)→ **记录,不强行过**。这是 Task 5 SIL 实测的关键输入,可能需调 zl。

- [ ] **Step 7: Commit**

```bash
git add src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_acados_solver.cpp
git commit -m "test(m5): per-target ξ independence + exact-penalty + penalty value (P3 T4)

- XiIndependent_NoMasking: 2-target, A infeasible ξ_A>0, B feasible ξ_B≈0
  (masking/free-riding eliminated, SC-02)
- XiExactPenalty_FeasibleZero / InfeasiblePositive
- SlackPenalty_MixedL1L2Value: ρ·ξ+½w·ξ² oracle
Multi-target MidMpcInput builder (two_targets_independent)."
```

---

## Task 5: ρ exact-penalty SIL 实测(决定 zl 校准)

**Files:**
- SIL 实测脚本(用 colregs-probe skill / run_colregs_*.py)+ 实测报告(无生产代码改动,除非调 zl)

**关键决策点**: 据 SIL 结果决定 zl 校准(固定/调大/同伦)。**条件性**:若 zl=1e3 够则不改 gen script;若不够则调 RHO_LIN。

- [ ] **Step 1: 跑 SIL 多船场景,M5_USE_ACADOS=ON**

用 colregs-probe skill 或 run_colregs_clean_8probe.py,跑 imazu-06-ms 或 imazu-10-ms(多船)。
Run(示例): `source scripts/a4000-env.sh && COMPOSE_PROJECT_NAME=codex-m5-p3 ... colregs-probe imazu-06-ms`(具体按 colregs-probe skill 工作流)
M5_USE_ACADOS=ON(镜像 build 时 -DM5_USE_ACADOS=ON)。

- [ ] **Step 2: 提取 per-target ξ 行为(用 Task 3 ASDR JSON)**

从 ASDR JSON 提取 `cpa_slack_per_target` 数组,分析:
- CPA feasible(目标远/无冲突)时 ξ ≈ 0?
- CPA active(避让中)时 ξ 合理(仅不可达时 >0)?
- 多船:各目标 ξ 独立(一目标松弛不拖累其他)?

- [ ] **Step 3: 判定 ρ exact-penalty 是否满足**

- **PASS**: feasible 时 ξ < tol(1e-3)→ zl=1e3 满足 exact-penalty,固定值够。记录,不调 zl。
- **FAIL/PARTIAL**: feasible 时 ξ > tol → zl 不够大。进 Step 4。

- [ ] **Step 4: (条件性)若 zl 不够,调 RHO_LIN 重测**

改 `gen_mid_mpc_acados.py` L158 `RHO_LIN = 1.0e3` → 1e4(或 1e5);重 build;重跑 SIL imazu-*-ms;看 feasible ξ 是否归零。
- 若调大后 ξ 归零 → 记录新 zl 值 + 据此定。
- 若调大仍不归零 → 评估 Eriksen 同伦 K_ξ 序列(回用户裁决,可能开专项)。

- [ ] **Step 5: 写 ρ 校准决策报告**

记录:SIL 场景 / ξ 行为数据 / 判定(PASS/FAIL)/ zl 校准决策(固定 1e3 / 调大至 X / 引入同伦)。写入 handoff。

- [ ] **Step 6: (若调 zl)Commit gen script 改动**

```bash
git add test/external/acados_backend/gen_mid_mpc_acados.py
git commit -m "tune(m5): RHO_LIN zl 1e3->X per SIL exact-penalty evidence (P3 T5)

SIL imazu-*-ms showed feasible ξ>tol with zl=1e3 (exact-penalty not met).
Raised to X per Kerrigan ρ>‖λ*‖∞ condition. Re-tested: feasible ξ≈0."
```
(若未调 zl,此 step 跳过,只 commit 报告。)

---

## Task 6: 回归 + 验收门 + codex 评审 + handoff

**Files:**
- (验收 + 评审 + handoff)

- [ ] **Step 1: 回归门 — IPOPT 路径无回归**

Run: `COMPOSE_PROJECT_NAME=codex-m5-p3 docker compose ... run --rm sil-nodes bash -c "cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=OFF && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | grep -E 'PASSED|FAILED' | tail"`
Expected: IPOPT 路径全绿(P3 只加可观测性 + 测试,IPOPT formulation 不动)。

- [ ] **Step 2: 回归门 — acados 路径全绿**

Run: 同上但 `-DM5_USE_ACADOS=ON`。
Expected: acados 现有测试 + P3 新测试(ξ 独立性/精确性/penalty/per-target breakdown)全绿。

- [ ] **Step 3: 验收门核对(spec 7 条)**

- [ ] ρ SIL 实测完成:feasible ξ 行为判定(PASS zl 够 / FAIL 调大)+ 报告(Task 5)
- [ ] ξ 独立性单测:多船一目标松弛不拖累其他(masking 消除,SC-02)(Task 4)
- [ ] ξ 精确性单测:feasible ξ≈0 / infeasible ξ>0(Task 4)
- [ ] 混合 L1/L2 penalty 数值单测:ρ·ξ+½w·ξ² 正确(Task 4)
- [ ] ξ 可观测性:per-target ξ breakdown publish + 认证可见(Task 1-3)
- [ ] IPOPT 路径无回归 + acados 现有测试全绿(Step 1-2)
- [ ] ρ 校准决策记录(固定/调大/同伦,据 SIL 结果)(Task 5)

- [ ] **Step 4: codex 对抗评审(强制,用户要求)**

调用 codex(或 tdl-code-reviewer agent)对照 P3 spec + plan 严格评审:
- spec/plan 符合性(7 验收门 + Task 1-6)
- 诚实性(ξ 独立性/精确性真验非 tautology;SIL 实测真跑非声称;per-target breakdown 真提取)
- 范围合规(formulation/gen script 未改除非 SIL 证据;dynamics/state/route cost/horizon/COLREGs 全未动)
- zl 校准若有,证据支撑(SIL 数据,非凭感觉)
- 发现分类:Critical(必修重审)/Important(评估)/Minor(记录)
- 0 Critical 才算 P3 完成

- [ ] **Step 5: 更新 handoff/workspace_log.md**

追加 P3 完成 + ρ 校准决策 + codex 评审结论。

- [ ] **Step 6: Commit handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record P3 slack validation completion + ρ decision"
```

---

## Self-Review(plan 作者自检)

**1. Spec 覆盖**:
- ✅ ρ SIL 实测 → Task 5(条件性调 zl)
- ✅ ξ 独立性 + 精确性单测 → Task 4
- ✅ 混合 L1/L2 penalty 数值 → Task 4 Step 4
- ✅ ξ 可观测性 → Task 1(字段)+ Task 2(solver)+ Task 3(node JSON)
- ✅ 7 验收门 → Task 6 Step 3
- ✅ codex 强制评审 → Task 6 Step 4

**2. Placeholder 扫描**:
- Task 4 multi-target builder 标"查 MidMpcInput.targets 字段结构"(因未读全 TrackedTarget 字段);执行者参考 node.cpp assemble_input_ 的 target pack 模式。给了 straight_line() 模板。
- Task 5 SIL 实测具体命令标"按 colregs-probe skill 工作流"(因 colregs-probe 是 skill,执行者调 skill);给了场景名 imazu-06-ms/10-ms。
- 无 TBD/TODO/FIXME(除引用的 TBD-5/6/7)。

**3. 类型一致**: cpa_slack_per_target(std::array<double,16>)全 plan 一致;Zl=1e2/zl=1e3 一致(gen L157-158);kAcadosNsh=16 一致。

**4. 风险**: ξ 独立性/精确性单测若失败(Task 4 Step 6)→ 记录为 SIL 实测输入,可能调 zl(Task 5);SIL 多船场景 ξ 提取链路(ASDR JSON);multi-target builder 构造。Task 6 codex 评审是最终门。
