# M5 7 层回归基线诊断 — Contract Test 详细规格 v4

> **状态**: v4 设计稿(2026-07-21),用户已批准核心方向,待 Phase A 启动。
> **v3 → v4 关键修订**: L0 friend hook 改为抽纯函数(MidMpcNode 构造太重);L0 test 数量从 7 增到 ~12(加 happy path 变体);solver friend 接口从 3 处减到 2 处(去掉 L0AssembleInputTestAccess)。
> **方法论**: superpowers:systematic-debugging Phase 1 + Phase 3(每条 test 是一个可证伪假设)。
> **用户反馈整合**:
> - "代码测试和设计文档同步且一致" → 每个 test 精确到 名字/输入/断言/期望
> - "test SetUp 里 re-codegen" → L1 codegen 处理已明确
> - "scan 没有更高效方案?" → 已加 G+H(共享 capsule + max_iter override,~85s)
> - "不理解 stale artifact 决策" → 已澄清(F1 降级,不需要 git 决策)
> - "不理解 stale artifact" → 已澄清(F1 降级,不需要 git 决策)

---

## 0. 已验证事实(v3,codegen-verified)

| 事实 | 证据 | 置信度 |
|---|---|---|
| P4 baseline 可复现 | `2c031bc49` scan 与 P5 §2 表精确吻合 | 高 |
| L3 HEAD 退化真实 | 用 verified codegen(NSH=0/NP=211)重跑,退化仍在 | 高 |
| 退化形态 = 收敛带反转 | gap -548~+152 全 raw=2(MAX_ITER), +252/+352 raw=4(QP error recovered → converge) | 高 |
| raw=2 = MAX_ITER 不是 QP infeasible | wrapper line 116-118 把 raw=2 映射成 Infeasible,但 raw=2 在 acatos 语义是 MAX_ITER | 高(F7) |
| `c_generated_code/` git-ignored | `git ls-files` 返回空;每次 codegen 产出 fresh | 高 |
| L0 degradation flag write-only | grep 全 solver 0 命中;L1/L4/LX 不读 | 高(F2) |
| L1b schedule 是 bound-based | build_stage_row_bounds 修改 lh/uh,不碰 idxsh | 高(F4) |

---

## 1. 新增文件 & 改动清单(精确路径,v3→v4 修订)

> **v4 修订(Phase A 前发现 MidMpcNode 构造太重)**:原计划用 friend hook 访问 `assemble_input_`,但 MidMpcNode 构造函数加载 yaml + 建 IPOPT/acados formulation + 创建 ROS2 subscriptions,**单元测试构造它太重**。改为**抽出纯验证函数**(跟随现有 `validate_speed_box` / `validate_earliest_min_alt_k` 模式)。

| 文件 | 动作 | 内容 |
|---|---|---|
| `test/unit/test_l0_contracts.cpp` | **新建** | L0-T1~T7(7 tests,直接调纯函数) |
| `test/unit/test_l1_contracts.cpp` | **新建** | L1-T1~T11(11 tests) |
| `test/unit/test_l2_contracts.cpp` | **新建** | L2-T1~T5(5 tests) |
| `test/unit/test_regression_scan.cpp` | **新建** | G+H scan(共享 capsule + max_iter override) |
| `mid_mpc_acados_solver.hpp/cpp` | **小改** | 加 `set_max_iter_diagnostic(int)` public + `debug_get_stage_bounds()` debug getter |
| **`common/l0_guards.hpp/cpp`(新)** | **新建** | 纯验证函数:`validate_own_heading`/`validate_own_speed`/`validate_target_latlon`/`validate_box_reach`/`validate_rot_step`/`validate_min_alt`/`bump_cpa_safe_for_conflict` |
| `mid_mpc_node.cpp` | **微重构** | `assemble_input_` 内联验证改为调用 `l0_guards.hpp` 的纯函数(行为不变,只搬位置) |
| `CMakeLists.txt` | **小改** | 注册 4 个新 test binary + l0_guards 库 |

**v4 vs v3 差异**:
- ❌ 不加 `friend class L0AssembleInputTestAccess`(不再需要)
- ✅ 新增 `l0_guards.hpp/cpp`(纯函数,跟随 validate_speed_box 模式)
- ✅ `mid_mpc_node.cpp` assemble_input_ 内联验证代码**搬入** l0_guards(行为完全不变)
- ✅ L0 test 直接调 l0_guards 函数,不需 MidMpcNode

**所有改动遵循 AGENTS.md**:无 mock/skip/forced-pass;无 vessel/scenario 分支;纯函数对 production 与 test 都可见。

---

## 2. L0 Contract Tests 规格(v4,纯函数版本)

> 现有 `test_l0_input_guards.cpp` 测了 `InputDegradation` struct + `validate_speed_box`/`validate_earliest_min_alt_k`。本节抽出**剩余的验证逻辑**为纯函数,补全 L0 覆盖。

### 2.1 抽出的纯函数规格(放在 `common/l0_guards.hpp`)

每个函数签名 + 行为契约 + 对应的 `mid_mpc_node.cpp` 源行。

| 函数 | 签名 | 行为 | 源行 |
|---|---|---|---|
| `validate_own_heading` | `(double heading_deg, InputDegradation&) -> double psi_rad` | NaN/Inf → 0.0 + flag;else normalize_signed | `:527-536` |
| `validate_own_speed` | `(double u_water, double sog_kn, InputDegradation&) -> double u_mps` | u_water>0.1&finite → u_water;else sog finite&≥0 → sog;else 0.0+flag | `:538-551` |
| `validate_target_latlon` | `(double lat, double lon) -> bool valid` | both finite → true;else false(drop + flag 由 caller 设) | `:560-564` |
| `validate_target_sog` | `(double sog_kn, InputDegradation&) -> double sog_mps` | finite&≥0 → sog_mps;else 0.0+flag | `:573-581` |
| `validate_box_reach` | `(double box_reach_deg, InputDegradation&) -> double` | finite&≥0 → box_reach;else 0.0+flag | `:614-625` |
| `validate_rot_step` | `(double rot_step_deg, InputDegradation&) -> double` | finite&>0 → rot_step;else 0.0+flag | `:628-637` |
| `validate_min_alt` | `(double min_alt_rad, InputDegradation&) -> double` | finite&≥0 → min_alt;else 0.0+flag | `:638-647` |
| `bump_cpa_safe_for_conflict` | `(bool conflict_active) -> double cpa_safe_m` | conflict → 2500.0;else 1852.0(kCpaSafeFallback) | `:787-791` |
| `check_box_reach_pref_dir_consistency` | `(double box_reach, bool conflict_active, ColregsPreferredDirection, InputDegradation&) -> void` | box_reach>0 & conflict & pref_dir∉{Stbd,Port} → flag(warn 由 caller 发) | `:769-779` |

### 2.2 test 规格表(v4,纯函数直接调用,不需 MidMpcNode)

| ID | Test 名字 | 输入 | 断言 | 期望 | 锚点 |
|---|---|---|---|---|---|
| L0-T1 | `ValidateOwnHeading_NaN_FallsBackToZeroAndFlags` | `heading_deg = NaN` | `psi_rad == 0.0` AND `deg.own_psi_degraded == true` | PASS | `l0_guards.hpp validate_own_heading` ← `:527-536` |
| L0-T1b | `ValidateOwnHeading_ValidNormalizes` | `heading_deg = 370.0` | `psi_rad ≈ 10°`(normalized to [-π,π]) AND `deg.own_psi_degraded == false` | PASS | 同上 |
| L0-T2 | `ValidateOwnSpeed_NaNSogNoWater_FallsBackToZeroAndFlags` | `u_water = NaN, sog_kn = NaN` | `u_mps == 0.0` AND `deg.own_u_degraded == true` | PASS | `validate_own_speed` ← `:538-551` |
| L0-T2b | `ValidateOwnSpeed_ValidWater_PrefersWater` | `u_water = 3.5, sog_kn = 5.0` | `u_mps == 3.5` AND `!deg.own_u_degraded` | PASS | 同上 |
| L0-T3 | `ValidateTargetLatlon_NaN_ReturnsFalse` | `lat = NaN, lon = 0.0` | returns `false`(caller drops + flags) | PASS | `validate_target_latlon` ← `:560-564` |
| L0-T3b | `ValidateTargetLatlon_Valid_ReturnsTrue` | `lat = 30.0, lon = 122.0` | returns `true` | PASS | 同上 |
| L0-T4 | `DegradedFlagsPropagate_ToSolverBehavior_RED` | 调 solve 两次:正常 input vs 同 input 但 own_psi_degraded=true(看 L1 是否消费) | 断言两次 solve 结果不同(trajectory/cost) | **RED**(预期失败)—— 证伪"L0 GATE closed";用户已选保留为显式 RED test | `types.hpp:281-288` vs grep 0 命中 |
| L0-T5 | `CheckBoxReachPrefDirConsistency_BoxReachPositiveHoldPrefDir_FlagsDegraded` | `box_reach=30, conflict=true, pref_dir=Hold` | `deg.reachability_degraded == true` | PASS | `check_box_reach_pref_dir_consistency` ← `:769-779` |
| L0-T5b | `CheckBoxReachPrefDirConsistency_BoxReachZero_NoFlag` | `box_reach=0, conflict=true, pref_dir=Hold` | `!deg.reachability_degraded` | PASS | 同上 |
| L0-T6 | `ValidateCpaHardROSParam_NaN_FallsBackTo1852NoFlag` | 这是构造时逻辑(`:406-411`),不能纯函数测。**改为 unit test 直接断言常量**:`kCpaSafeFallback_m == 1852.0` | `kCpaSafeFallback_m == 1852.0` | PASS(锁默认值) | `mid_mpc_node.cpp:47` + `:406-411` |
| L0-T7 | `BumpCpaSafeForConflict_ActiveConflict_Returns2500Silently` | `conflict_active=true` | returns `2500.0` AND 无 flag 参数(纯函数无 side effect) | PASS | `bump_cpa_safe_for_conflict` ← `:787-791` |
| L0-T7b | `BumpCpaSafeForConflict_NoConflict_Returns1852` | `conflict_active=false` | returns `1852.0` | PASS | 同上 |

### 2.3 L0 GATE 关闭判据(v4)

- T1/T1b, T2/T2b, T3/T3b, T5/T5b, T6, T7/T7b 全绿 = L0 纯验证函数契约成立
- T4 RED = 显式证伪"L0 GATE closed"(degradation flag write-only);保留为可执行证据
- **覆盖性 gap**:assemble_input_ 的**调用顺序/组合**(各 helper 如何串起来)不被纯函数测试覆盖。这是 trade-off:不需 MidMpcNode 重构造,代价是失去端到端链路覆盖。LX 阶段的 SIL 集成测试可补这个 gap。

---

## 3. L1 Contract Tests 规格(`test_l1_contracts.cpp`)

### 3.1 codegen SetUp 设计(用户已选 "test SetUp 里 re-codegen")

**全局 fixture**(gtest Environment):
```cpp
class L1CodegenEnv : public ::testing::Environment {
  void SetUp() override {
    // 1. 调 codegen 脚本(确保 fresh NSH=0/NP=211)
    int rc = std::system("python3 <repo>/gen_mid_mpc_acados.py > /tmp/gen.log 2>&1");
    ASSERT_EQ(rc, 0) << "codegen failed";
    // 2. parse 输出断言 signature
    std::string log = read_file("/tmp/gen.log");
    EXPECT_THAT(log, HasSubstr("nsh=0"));
    EXPECT_THAT(log, HasSubstr("np_global=155"));
    EXPECT_THAT(log, HasSubstr("np_per_stage=56"));
    // 3. make shared_lib
    std::system("cd <c_generated_code> && make shared_lib > /tmp/make.log 2>&1");
  }
};
::testing::AddGlobalTestEnvironment(new L1CodegenEnv);
```

### 3.2 test 规格表

#### L1a 部分(OCP 规格 + box live)

| ID | Test 名字 | 输入 | 断言 | 期望 | 锚点 |
|---|---|---|---|---|---|
| L1-T1 | `Codegen_ProducesStep5Signature` | codegen 全局 fixture SetUp | parse codegen stdout: `nsh=0`, `np_global=155`, `np_per_stage=56`, `nh=20` | PASS | `gen_mid_mpc_acados.py:610-621, 644-647` |
| L1-T2 | `SolverNpMacroMatchesCodegenHeader` | compile-time | `static_assert(kAcadosNp == M5_MID_MPC_ACADOS_NP)` 在 test 里成立 | PASS(若不成立 = F1 codegen-state drift) | `mid_mpc_acados_solver.cpp:57` |
| L1-T3 | `Codegen_ConstrHFunReadsCpaHardSlot154NotCpaSafeSlot10` | 读生成的 `constr_h_fun.c` | grep `arg[3][154]` 命中 AND `arg[3][10]` 在 CPA 段不命中 | PASS(若反 = codegen 退回 pre-Step5) | `c_generated_code/.../constr_h_fun.c` |
| L1-T4 | `BoxLive_LiveBoundsWrittenStages1ToN` | hdg_min=0.5(非默认),通过 friend hook 调 solve | counter: `ocp_nlp_constraints_model_set` 在 stage>=1 被调用 | PASS | `mid_mpc_acados_solver.cpp:1455-1501` |
| L1-T5 | `BoxLive_DefaultBoundsSkipsModelSet` | hdg/spd/rot 全默认 | counter: `model_set` 调用次数 == 0 | PASS | `:1470-1474` |
| L1-T6 | `Stage0EqualityPin_MatchesCodegenSignature` | codegen fixture 后读 header | 根据当前 codegen signature,断言 `idxbx_0` 配置(动态,不硬编码) | PASS | 生成的 .h |

#### L1b 部分(schedule + witness)

| ID | Test 名字 | 输入 | 断言 | 期望 | 锚点 |
|---|---|---|---|---|---|
| L1-T7 | `ReachabilitySchedule_KHeadEarliest_BoxPathFormula` | rot_max=4.7°/s(dt=15s), box_reach=30° | `k_head_earliest == ceil(0.5236/(0.0820))-1 == 5`(用 deg2rad+dt 转换) | PASS(用 friend 调 compute_reachability_schedule) | `:268-442` |
| L1-T8 | `ReachabilitySchedule_KMinaltUsesBprimeRotStep` | rot_max=4.7°/s, min_alt=20° | `k_minalt` 用 `bprime_rot_step = rot_step/2.0`,不是 rot_step;断言 `k_minalt == ceil(20°/(rot_step/2.0))-1` | PASS | `:140, :290, :301-303` |
| L1-T9 | `CpaSchedule_ThreePhaseBoundBasedNotIdxsh` | prefix_K=3, k_cpa_suffix=10 | 检查 stage 0/2/9/10 的 CPA row lh 值: stage<prefix_K 是 committed; prefix_K<=stage<k_cpa_suffix 是 -inf(soft); stage>=k_cpa_suffix 是 0(hard) | PASS | `:177-231` |
| L1-T10 | `PrefixCpaWitness_ViolationOverridesToNumericalFailure` | 构造 prefix positions 使 stage 0..K-1 的 dx²+dy²<cpa_hard² | solve 返回 `status==NumericalFailure` **即使 SQP 本身收敛** | PASS | `:471-535, :1659` |
| L1-T11 | `ReachabilitySchedule_KHeadEarliestExceedsLatest_WarnsButDoesNotFail` | 构造几何使 k_head_earliest > k_head_latest | solve 仍返回任意 status(不抛异常),且 stdout 含 warn | PASS | `:429-436` |

### 3.3 L1 GATE 关闭判据

- T1~T11 全绿 = L1 行为契约成立
- T1/T3 是 codegen-state drift 的检测器;若 RED 说明 codegen 被污染(必须重跑)

---

## 4. L2 Contract Tests 规格(`test_l2_contracts.cpp`)

### 4.1 test 规格表

| ID | Test 名字 | 输入 | 断言 | 期望 | 锚点 |
|---|---|---|---|---|---|
| L2-T1 | `BoxLive_HeadingDelayedToKHeadEarliest` | k_head_earliest=5, hdg_min=0.5 | stage<5 的 hdg bound == default(±π); stage>=5 == live(0.5); ROT bound 所有 stage == live | PASS(用 debug_get_stage_bounds) | `:1483-1499` |
| L2-T2 | `DMinTelemetry_FoldedBeforeIsRelaxedGuard` | target_y=2100, cpa_safe=2500 | `sol.soft_aspiration_d_min_m > 0` AND `sol.soft_aspiration_violation_m > 0` AND `violation == max(0, cpa_safe - d_min)` | PASS(复用 FB-2b 逻辑) | `:1027-1053, :1091-1103` |
| L2-T3 | `WarmStartShiftInit_SecondCycleUsesPrevSolution` | cycle1: no target; cycle2: own_x advanced 50m, warm_start=sol1 | `sol2.status==Converged` AND `sol2.traj[0].psi ≈ sol1.traj[1].psi (d<0.5 rad)` AND `warm_sqp_iter <= cold_sqp_iter + 10` | PASS(复用 WarmStartShiftInit) | `:1194-1233, :1516-1525` |
| L2-T4 | `OcpLayout_StaticAssertsHoldAtCompileTime` | include formulation.hpp | `static_assert(NP_GLOBAL==155)`, `(NP_PER_STAGE==56)`, `(NH==20)`, `(NSH==0)` 在 test TU 里成立 | PASS(compile-time) | `formulation.hpp:172-178` |
| L2-T5 | `WrapperNpCommentNotStale` | compile-time check | 单独 grep 注释 `:1148 np_global = 106` 应已更新为 155(或删除);若仍 stale,test 标 RED | RED 或 PASS(取决于注释是否已修) | `:1148` |

### 4.2 L2 GATE 关闭判据

- T1,T2,T3,T4 全绿 = L2 行为契约成立
- T5 是注释漂移检测器(低优先级,RED 不阻塞 L2 GATE)

---

## 5. Regression Scan 规格(`test_regression_scan.cpp`,G+H 方案)

### 5.1 共享 capsule 设计

```cpp
// 全局 fixture:所有 scan 共享 1 个 solver(1 次 warm-up)
class SharedSolverEnv : public ::testing::Environment {
 public:
  static MidMpcAcadosFormulation* form_;
  static MidMpcAcadosSolver* solver_;
  void SetUp() override {
    form_ = new MidMpcAcadosFormulation;
    form_->build_symbolic_graph();
    solver_ = new MidMpcAcadosSolver(*form_);
    // H 方案:scan-only max_iter override(从 400 降到 100)
    solver_->set_max_iter_diagnostic(100);
  }
  void TearDown() override {
    delete solver_; delete form_;
  }
};
MidMpcAcadosFormulation* SharedSolverEnv::form_ = nullptr;
MidMpcAcadosSolver* SharedSolverEnv::solver_ = nullptr;
::testing::AddGlobalTestEnvironment(new SharedSolverEnv);
```

### 5.2 scan test 规格表

| ID | Test 名字 | 输入 | 断言 | 期望(baseline) | 期望(HEAD) |
|---|---|---|---|---|---|
| S-T1 | `RegressionScan_EightPoints_SharedCapsule` | 8 个 target_y 顺序 solve | 只 log raw/status/sqp,**不断言**(diagnostic) | P4: -548~+252 raw=0, +352 raw=4 | HEAD: -548~+152 raw=2, +252/+352 raw=4 |
| S-T2 | `RegressionScan_Gap52_MustConverge_BaselineGate` | target_y=1800 | `raw == 0` | P4: PASS | HEAD: **FAIL**(暴露退化) |
| S-T3 | `RegressionScan_Gap252_MustConverge` | target_y=1600 | `raw == 0 || raw == 4` (converged or QP-error-recovered) | PASS | PASS |
| S-T4 | `RegressionScan_Gap352_StatusDocumented` | target_y=1500 | 只 log(raw 0 或 4 都接受,文档锁定) | P4: raw=4 | HEAD: raw=4(改善) |

### 5.3 时间预算验证

```
SharedSolverEnv SetUp: 1 warm-up (~25s) + make_shared_lib(已在 fixture 外)
S-T1: 8 solves(MAX_ITER=100): 6×10s + 2×0.3s ≈ 61s
S-T2~T4: 3 solves ≈ 30s(若都 fail)或 5s(若都收敛)
总: 25 + 61 + 30 = ~116s ≤ 120s ✓(worst case)
typical: 25 + 61 + 5 = ~91s ✓
```

### 5.4 scan 状态判据

- **S-T1**(diagnostic scan): 只记录,不断言 —— 用于诊断报告里的数据
- **S-T2**(baseline gate): gap=+52 必须 raw=0 收敛 —— **这是回归基线的硬门**
- **S-T3**(boundary gate): gap=+252 收敛 —— 边界稳定性
- **S-T4**(improvement marker): gap=+352 状态记录,文档锁定 —— 不阻塞

---

## 6. 实施顺序(待批准后)

| Phase | 内容 | 预计时间 |
|---|---|---|
| A | 加 friend hooks + max_iter_diagnostic setter + debug_get_stage_bounds | 30 min |
| B | L0 contract tests(T1,T2,T3,T5,T6,T7;T4 待用户裁决) | 45 min |
| C | L1 codegen fixture + L1-T1~T6(L1a) | 60 min |
| D | L1-T7~T11(L1b) | 60 min |
| E | L2 contract tests | 30 min |
| F | Regression scan(G+H) | 30 min |
| G | 跑全部,记录 RED/GREEN,写诊断报告 | 60 min |
| **总** | | **~5.5h** |

---

## 7. 不做的事(明确边界)

- ❌ 不修复任何 bug(诊断阶段)
- ❌ 不回滚 commit(用户决定)
- ❌ 不改 codegen default max_iter(只加 diagnostic override)
- ❌ 不改 production solve() 路径行为
- ❌ 不写 vessel/scenario-specific 分支
- ❌ 不用 mock/skip/forced-pass

---

## 8. 待用户最终确认

1. **L0-T4 RED test**: 保留为显式证伪 vs 仅文档化 finding?
2. **friend hooks 数量**: 加 `L0AssembleInputTestAccess` + `MidMpcAcadosSolver` 加 `set_max_iter_diagnostic` + `debug_get_stage_bounds` —— 这 3 处小改可接受吗?
3. **v3 设计稿是否足够精确可以进入实施**?
