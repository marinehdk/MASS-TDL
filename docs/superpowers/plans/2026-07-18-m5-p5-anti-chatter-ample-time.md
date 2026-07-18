# P5: 反 chattering(warm-start shift-init + 转移代价)+ ample-time 验收门 + IPOPT compiler 清理 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 落地 TBD-7 反 chattering 前 2 层(warm-start shift-init + 转移代价混合范数;符号翻转推后)+ ample-time 验收门/ODD 边界(基于 P4 收敛性核实)+ IPOPT compiler 硬编码 Rule14/15/16/17 清理。

**Architecture:** warm-start 用现成被忽略的 `warm_start` 参数(solver.cpp L690 `(void)warm_start`)。转移代价加到 acados cost(MX + gen SX parity)。ample-time 门基于实测证据(目标>2000m 收敛)。IPOPT compiler 清理度数偏移改 M6 几何。

**Tech Stack:** C++17, CasADi MX/SX, acados 0.4.4, ROS2 ament_cmake, colcon, gtest

**Spec:** `docs/superpowers/specs/2026-07-18-m5-p5-anti-chatter-ample-time-design.md`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,HEAD 2c031bc49)
- **M6 几何/Huber 已落地**: acados 路径已用 M6 几何(direction/min_alt,P1b-1b)+ Huber(P2)。P5 不重做。
- **warm_start 参数已存在**: solver.cpp L688-690 `solve(input, warm_start)` 的 `warm_start` 当前被 `(void)` 忽略。P5 实际使用它(shift-init)。这是精确挂载点。
- **ample-time 证据(实测)**: 目标当前 >2000m 收敛(5028m→237iter,2121m→130iter);<2000m 失败(1802m/1581m→status=3)。边界是当前距离 ~2000m。
- **MX/SX parity 真验**: 转移代价改后 gen SX 同步,parity assertion 跑通。
- **测试标志**: `BUILD_TESTING` + 包名 `m5_tactical_planner`。
- **容器内执行**: `source scripts/a4000-env.sh`;`COMPOSE_PROJECT_NAME=codex-m5-p5`,不碰 mass-l3-sil。
- **M5_USE_ACADOS 默认 ON**(P4 已切): acados 是主路径;IPOPT 是 fallback(=OFF 显式编译)。
- 诚实纪律: warm-start shift-init 真用(非 stub);转移代价数值真验(oracle);ample-time 门真跑(目标 5000m);IPOPT 清理后回归真跑;反 chattering 效果多周期验证。
- 每 task 一 commit;TDD。

---

## File Structure

| 文件 | 责任 | P5 改动 |
|---|---|---|
| `src/mid_mpc/mid_mpc_acados_solver.cpp` | warm-start shift-init(L690 挂载点)+ cache last_converged_solution_ | 改 |
| `src/mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}` | 转移代价 J_transition(L2 航向 + L1 速度)+ 上周期解参数 | 改 |
| `test/external/acados_backend/gen_mid_mpc_acados.py` | 转移代价 SX parity + 上周期解 per-stage 参数 | 改 |
| `src/shared/constraint_compiler.cpp` + `include/shared/constraint_compiler.hpp` | 清理 compile_rule14/15/16/17 度数偏移 | 改 |
| `test/unit/test_mid_mpc_acados_solver.cpp` | ample-time 门 + warm-start 单测 + J_transition 数值 + RhoCal 修正 | 改 |
| `test/unit/test_constraint_compiler.cpp` | Rule14/15/16/17 清理后回归 | 改 |
| docs(ODD 边界 + 收敛边界) | ample-time ODD + ~2000m 边界记录 | 改 |

---

## Task 1: warm-start shift-init(反 chattering 首要)

**Files:**
- `src/mid_mpc/mid_mpc_acados_solver.cpp`(L688-690 solve 入口 + L715-743 F1 seed block 1a)

**Interfaces:**
- Consumes: `solve(input, warm_start)` 的 `warm_start` 参数(现被 `(void)` 忽略)
- Produces: warm-start shift-init(有上周期收敛解 → shift 一步作初值;无 → F1 seed)

- [ ] **Step 1: 写失败测试(连续两周期,第二周期用 shift-init)**

扩 `test_mid_mpc_acados_solver.cpp`:
```cpp
TEST_F(AcadosSolverTest, WarmStartShiftInit_SecondCycleUsesPrevSolution) {
  // 第一周期:cold(F1 seed)
  auto inp = straight_line();
  // 加 ample-time 目标(目标 5000m 外,确保收敛)
  TargetState t; t.x_m = -1500.0; t.y_m = 4800.0; t.sog_mps = 0.0; ...
  inp.targets.push_back(t);
  const auto sol1 = solver_->solve(inp, nullptr);  // cold
  ASSERT_EQ(static_cast<int>(sol1.status), 0);  // 第一周期必收敛(ample-time)

  // 第二周期:warm-start shift-init(传入 sol1)
  // 同 input(或微调 own position 模拟前进)
  auto inp2 = inp;
  inp2.own_ship.x_m = 50.0;  // own 前进 50m(模拟 1 周期)
  const auto sol2 = solver_->solve(inp2, &sol1);  // warm-start
  EXPECT_EQ(static_cast<int>(sol2.status), 0);  // 第二周期收敛
  // 验证 shift-init 生效:第二周期 sqp_iter 应 < 第一周期(warm-start 比 cold 快)
  // 或:验证轨迹连续(sol2 psi 序列 vs sol1 shift 的差小)
  EXPECT_LE(solver_->last_sqp_iter(), solver_first_cycle_iter);  // warm-start 更快
}
```

- [ ] **Step 2: 跑确认失败(warm_start 被忽略,sol2 用 cold F1 seed,不比 sol1 快)**

- [ ] **Step 3: 实现 shift-init**

solver.cpp:
- 加成员 `MidMpcSolution last_converged_solution_; bool has_last_converged_ = false;`
- solve() L690 改:不再 `(void)warm_start`;判断:
  ```cpp
  const bool use_shift_init = (warm_start != nullptr
      && static_cast<int>(warm_start->status) == 0  // 上周期收敛
      && !sig_changed_  // 约束结构未变(须加 sig 检测或外部传入)
      && warm_start->psi_sequence.size() == ...);  // 维度匹配
  ```
- 若 use_shift_init:seed_traj 用 warm_start 的 x/u shift 一步(stage k 用 warm_start stage k+1),替代 F1 seed block 1a 的 forward-propagate。
- 若否:回退 F1 seed(现逻辑 block 1a)。
- solve() 结束:若 sol.status==0 → cache `last_converged_solution_ = sol; has_last_converged_ = true;`。

- [ ] **Step 4: 跑测试确认通过(shift-init 生效,sol2 比 sol1 快或轨迹连续)**

- [ ] **Step 5: 验证 cold-capsule 不冲突(首周期 warm-up 仍用 F1 seed)**

首周期无 warm_start → F1 seed + cold-capsule warm-up(现逻辑不变)。

- [ ] **Step 6: Commit**

```bash
git commit -am "feat(m5): warm-start shift-init for anti-chattering (P5 T1, TBD-7)"
```

---

## Task 2: 转移代价混合范数(L2 航向 + L1 速度)

**Files:**
- `include/mid_mpc/mid_mpc_acados_formulation.hpp` + `src/mid_mpc/mid_mpc_acados_formulation.cpp`
- `test/external/acados_backend/gen_mid_mpc_acados.py`(SX parity)
- `src/mid_mpc/mid_mpc_acados_solver.cpp`(pack 上周期解参数)

**Interfaces:**
- Consumes: Task 1 last_converged_solution_(ψ/u 序列作 per-stage 参数)
- Produces: J_transition = w_trans·[K_Δχ·Σ(ψ-ψ_prev)² + K_ΔU·Σ|u-u_prev|]

- [ ] **Step 1: 写失败测试(J_transition 数值 oracle)**

```cpp
TEST_F(AcadosSolverTest, TransitionCost_MixedL1L2Value) {
  // 构造已知 ψ_prev/u_prev,验证 J_transition 数值
  // J = w_trans * (K_dchi * sum((psi-psi_prev)^2) + K_du * sum(|u-u_prev|))
  // 用 oracle(MX 表达或手算)验证 cost_total 含正确 J_transition 分量
  ...
}
```

- [ ] **Step 2: 加 build_transition_cost_ 到 formulation**

formulation.cpp 加:
```cpp
casadi::MX MidMpcAcadosFormulation::build_transition_cost_() const {
  // per-stage:ψ_prev, u_prev 作 per-stage 参数(gslot per-stage)
  const casadi::MX psi = x_(2);       // 当前 ψ
  const casadi::MX u_surge = x_(4);   // 当前 u
  const casadi::MX psi_prev = pslot_(kPIdxPsiPrev);  // per-stage 上周期 ψ
  const casadi::MX u_prev = pslot_(kPIdxUPrev);      // per-stage 上周期 u
  // Eriksen tran_χ (L2) + tran_U (L1)
  const casadi::MX tran_chi = k_dchi * (psi - psi_prev) * (psi - psi_prev);   // L2 航向
  const casadi::MX tran_u = k_du * casadi::MX::fabs(u_surge - u_prev);         // L1 速度
  return w_trans * (tran_chi + tran_u);
}
```
hpp:Config 加 `w_trans{1.0}, k_dchi{2.5}, k_du{0.3}`(Eriksen 默认);per-stage 参数 kPIdxPsiPrev/kPIdxUPrev 加到 np_per_stage。

- [ ] **Step 3: solver pack 上周期解参数**

solver.cpp pack_parameters 或 step 1b 附近:若有 last_converged_solution_,把 ψ/u 序列 shift 一步写入 per-stage ψ_prev/u_prev;若无(首/失败)→ ψ_prev=ψ_curr, u_prev=u_curr(J_transition=0)。

- [ ] **Step 4: gen script SX parity**

gen_mid_mpc_acados.py 加 transition cost SX 表达 + per-stage ψ_prev/u_prev 参数;MX/SX parity 验证。

- [ ] **Step 5: build_symbolic_graph 加 J_transition 到总 cost**

J_total += J_transition(与其他 cost 项并列)。

- [ ] **Step 6: 跑测试 + parity**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_formulation 2>&1 | tail -5`
Expected: J_transition 数值对 + MX/SX parity 过。

- [ ] **Step 7: Commit**

```bash
git commit -am "feat(m5): transition cost mixed L1/L2 (P5 T2, TBD-7 Eriksen tran_chi/tran_U)"
```

---

## Task 3: ample-time 验收门 + ODD 边界 + RhoCal 修正

**Files:**
- `test/unit/test_mid_mpc_acados_solver.cpp`(ample-time 门 + RhoCal 修正)
- docs(ODD 边界 + 收敛边界)

**关键**: 基于 P4 期间实测证据(目标当前 >2000m 收敛)。

- [ ] **Step 1: 加 ample-time 验收门测试**

```cpp
TEST_F(AcadosSolverTest, AmpleTime_FarTargetMustConverge) {
  // 真实 ample-time:目标当前 5000m 外,CPA~1500m(< cpa_safe)
  MidMpcInput inp = straight_line();
  TargetState t;
  t.x_m = -1500.0;   // 横向偏移 → CPA≈1500m
  t.y_m = 4800.0;    // 纵向 → 当前距离 5030m
  t.sog_mps = 0.0;   // 静止
  inp.targets.push_back(t);
  const auto sol = solver_->solve(inp, nullptr);
  EXPECT_EQ(static_cast<int>(sol.status), 0)
      << "ample-time scenario (target 5030m away) MUST converge — Mid-MPC operational contract";
}

TEST_F(AcadosSolverTest, AmpleTime_ConvergenceBoundaryCurrentDistance) {
  // 扫描当前距离,确认边界 ~2000m(诊断,记录边界)
  for (const double ty : {4800.0, 3000.0, 2000.0, 1500.0, 1000.0}) {
    // ... 记录 status,无硬断言(边界本身是发现)
  }
}
```

- [ ] **Step 2: 修正 RhoCalibration_RealisticMultiShip 为 ample-time 场景**

现 L656 `a.x_m = -1500.0`(目标当前 1500m,BC-MPC 区)→ 改为目标当前 >2000m(ample-time):
```cpp
a.x_m = -1500.0;   // CPA 横向偏移 1500m(不变)
a.y_m = 4800.0;    // 改:纵向 4800m → 当前距离 5030m(ample-time,非 1500m stress-test)
```
目标 A 改为 ample-time 距离(当前 5030m),保持 CPA~1500m 测试语义。

- [ ] **Step 3: 跑测试确认 ample-time 门过 + RhoCal 修正后收敛**

Run: `colcon test --packages-select m5_tactical_planner --pytest-name test_mid_mpc_acados_solver --gtest_filter="*AmpleTime*:*RhoCalibration*" 2>&1 | tail`
Expected: ample-time 门 status=0;RhoCal 修正后收敛。

- [ ] **Step 4: ODD 边界 + 收敛边界记录**

文档(handoff 或 architecture report):Mid-MPC ODD = 目标当前 >2000m(ample-time 职责);<2000m 交 BC-MPC(P6)。收敛边界 ~2000m 作已知限制。

- [ ] **Step 5: Commit**

```bash
git commit -am "test(m5): ample-time acceptance gate + ODD boundary + RhoCal fix (P5 T3)"
```

---

## Task 4: IPOPT compiler 硬编码 Rule14/15/16/17 清理

**Files:**
- `src/shared/constraint_compiler.cpp`(L166-286 compile_rule14/15/16/17 + compile_colregs_rules dispatch)
- `include/m5_tactical_planner/shared/constraint_compiler.hpp`
- `test/unit/test_constraint_compiler.cpp`

**关键**: acados 路径已用 M6 几何(direction/min_alt);IPOPT compiler 仍有硬编码度数。清理使两路径一致。

- [ ] **Step 1: 读 compile_colregs_rules dispatch(L245-286)+ formulation 调用**

确认 IPOPT formulation 如何调 compile_colregs_rules + 硬编码行如何叠加到 g。

- [ ] **Step 2: 写失败测试(清理后无硬编码度数行)**

```cpp
TEST(ConstraintCompiler, Rule14_NoHardcodedDegreeOffset) {
  // 清理后:compile_rule14 不产生 psi >= psi_0+5° 行
  // 应返回空(M6 几何在 formulation direction/min_alt 行已处理)
  // 或:返回 M6 几何驱动行(用 preferred_direction/min_alteration)
  ...
}
```

- [ ] **Step 3: 清理 compile_rule14/15/16/17**

两条路(选其一,据 IPOPT formulation 现状):
- (a) 删 compile_rule14/15/16/17 度数偏移;compile_colregs_rules dispatch case 14/15/16/17 返回空(M6 几何在 formulation direction/min_alt 行已处理,compiler 不再加度数)。
- (b) 改 compile_rule14/15/16/17 为 M6 几何驱动(用 preferred_direction/min_alteration,与 acados 一致)。

推荐 (a)(最简:M6 几何已在 formulation 层,compiler 不重复)。但须确认 IPOPT formulation 是否有 direction/min_alt 行(若有 → (a);若无 → (b) 或移植 acados 的)。

- [ ] **Step 4: 改 test_constraint_compiler**

删/改 Rule14/15/16/17 度数偏移测试;加 M6 几何(若选 b)或空返回(若选 a)测试。

- [ ] **Step 5: IPOPT 回归(M5_USE_ACADOS=OFF)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON -DM5_USE_ACADOS=OFF && colcon test --packages-select m5_tactical_planner --pytest-name test_constraint_compiler --pytest-name test_mid_mpc_solver 2>&1 | tail`
Expected: IPOPT 路径清理后回归过(无硬编码度数,M6 几何正确)。

- [ ] **Step 6: Commit**

```bash
git commit -am "refactor(m5): remove hardcoded Rule14/15/16/17 offsets in IPOPT compiler (P5 T4, VR-04)"
```

---

## Task 5: 回归 + 反 chattering 多周期验证 + 验收门 + codex 评审

- [ ] **Step 1: acados 全测试(默认 ON 路径)**

Run: `colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner 2>&1 | grep -E "PASSED|FAILED" | tail`
Expected: 全绿(含 P5 新增:warm-start/transition/ample-time)。

- [ ] **Step 2: IPOPT 回归(M5_USE_ACADOS=OFF)**

Run: 同上 `-DM5_USE_ACADOS=OFF`。
Expected: IPOPT fallback 路径(Rule14/15 清理后)回归过。

- [ ] **Step 3: 反 chattering 多周期 SIL 验证(若可行)**

跑连续多周期 SIL 场景(目标 ample-time 距离),验证 port/starboard 不 flip(轨迹连续)。若 SIL 全栈不可用(SSL cert 等),单测级验证连续周期轨迹连续性(替代)。

- [ ] **Step 4: 验收门核对(spec 8 条)**

- [ ] warm-start shift-init 实现(Task 1)
- [ ] 转移代价 J_transition(L2+L1)+ 数值单测(Task 2)
- [ ] ample-time 验收门:目标 5000m 外收敛(Task 3)
- [ ] RhoCalibration 修正为 ample-time(Task 3)
- [ ] IPOPT compiler 硬编码清理 + 回归(Task 4)
- [ ] ODD 边界 + 收敛边界记录(Task 3)
- [ ] 反 chattering 效果:多周期不 flip(Task 5 Step 3)
- [ ] acados + IPOPT 回归全绿(Step 1-2)

- [ ] **Step 5: codex 对抗评审(强制,用户要求)**

调用 codex 对照 P5 spec + plan 严格评审:
- spec/plan 符合性(8 验收门 + Task 1-5)
- 诚实性(warm-start 真用非 stub/转移代价数值真验/ample-time 门真跑目标 5000m/IPOPT 清理后回归真跑/反 chatter 多周期真验)
- 范围合规(M6 几何/Huber 未重做/符号翻转未做/R4 未做/FSM P6 未动)
- ample-time 证据一致性(与 P4 收敛性核实一致)
- 发现分类:Critical(必修重审)/Important(评估)/Minor(记录)
- 0 Critical 才算 P5 完成

- [ ] **Step 6: handoff 更新 + Commit**

```bash
git commit -am "docs(handoff): record P5 anti-chatter + ample-time completion"
```

---

## Self-Review

**1. Spec 覆盖**: T1 warm-start / T2 转移代价 / T3 ample-time 门+ODD / T4 IPOPT 清理 / T5 验收+评审。8 验收门全覆盖。

**2. Placeholder**: Task 1 sig 检测标"须加 sig 检测或外部传入"(约束结构变检测,执行者参考 node.cpp 的 sig 逻辑 L848-862);Task 2 per-stage 参数偏移标"加到 np_per_stage"(执行者按现 kAcadosPerStageTbXOff 模式加);Task 4 (a)/(b) 选一(据 IPOPT formulation 现状,执行者读后定)。无 TBD/TODO/FIXME。

**3. 类型一致**: w_trans/k_dchi/k_du(Eriksen 默认 1.0/2.5/0.3)全 plan 一致;ample-time 5000m/2000m 边界一致(与 P4 核实一致)。

**4. 风险**: shift-init 与 cold-capsule 配合(Task 1 Step 5);转移代价权重(Task 2);ample-time 门须基于实测(Task 3 已有证据);IPOPT 清理后行为变(Task 4 回归);反 chatter 多周期验证(Task 5 Step 3 SIL 或单测替代)。Task 5 codex 评审是最终门。
