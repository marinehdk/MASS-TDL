# 新对话提示词 — M5 7 层回归基线 Contract Test 实施(Phase A 开始)

> **用法**: 把下面 `<handoff-prompt>...</handoff-prompt>` 标签内的全部内容(不含标签本身)复制到新对话的首条消息。

---

<handoff-prompt>

## M5 7 层回归基线 Contract Test 实施 — 新对话提示词

### 任务背景(必读)

**上一对话已完成 Phase 1 调查**,产出 v4 设计稿(已获用户批准核心方向)。**本对话的任务是从 Phase A 开始实施**(创建纯函数 + 重构 + debug 接口),逐 Phase 完成 L0/L1/L2 contract test + regression scan + 诊断报告。

**为什么这是必要的**:之前 L0~L3 每层标 "✓ GATE closed" 用的都是新增 cherry-picked 测试,**从未做过真正的回归基线**。L3 scan 发现 gap=+52 从 P4 的收敛退化为 raw=2(ACADOS MAX_ITER)。用户要求:**先每层补 contract test,颗粒度 L0/L1/L2 三层,通过后再补并行 scan;每次 scan 时间控制在 120s 内**。

### 工作目录(权威)

```
/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding
```

- **分支**: `codex/m5-design-grounding`(当前处于 detached HEAD)
- **HEAD**: `fb84701b1`(L3 GATE closed 的错误声称,待 Phase G 纠正)
- **worktree 干净**: 只有 2 个 untracked 文件(spec 文档 + bisect 脚本),无未提交代码改动
- **容器**: `codex-m5-p3-sil-nodes-1`(已运行 12h+,acados 已装,codegen MERIT_BACKTRACKING)。**继续使用,不要重启**。
- **bind-mount 关系**: 容器 `/opt/ws/src` → host worktree `src/`;host 的 git checkout 自动反映到容器。

### Phase 1 已验证的关键事实(不要再质疑)

1. **P4 baseline 可信**: commit `2c031bc49` 上独立 capsule scan(8 点)与 P5 §2 表**精确吻合**(gap -548~+252 全 raw=0,+352 raw=4)。这是真基线。

2. **L3 HEAD 退化真实**: 用 verified-correct codegen(容器内 fresh `gen_mid_mpc_acados.py` 产出 NSH=0/NP=211/NH=20)重跑 scan,退化**仍然存在**:
   - gap -548~+152 全部 raw=2 sqp=400(退化)
   - gap +252/+352 raw=4 sqp=2~4(收敛,post-L2 改善)
   - **退化形态 = 收敛带反转**,不是单调边界右移 → 层间耦合迹象

3. **raw=2 = ACADOS MAX_ITER,不是 QP infeasible**(F7,高优先级):
   - wrapper line 116-118 把 raw=2 映射成 `Status::Infeasible`,**这是误导性命名**
   - 实际语义是 "SQP 跑满 400 iter 未收敛"
   - **归因时必须看 raw status,不看 mapped status**

### 7 个代码考古 finding(三只并行 Explore agent 的结论)

| ID | 发现 | 严重度 | 锚点 |
|---|---|---|---|
| F1 | `c_generated_code/` git-ignored;内容取决于"谁上次跑 codegen"。**Test 方法论风险,不是 committed-stale**。 | 中 | `git ls-files c_generated_code/` 返回空 |
| F2 | **L0 degradation flag write-only**:`InputDegradation` 由 L0 设置,但 L1/L4/LX 从不读。文档注释(`types.hpp:281-288`)说"downstream 可区分",代码没实现。 | 高 | grep 全 solver 0 命中 |
| F3 | L0-B 没有 `box_reach_side == pref_dir_side` assertion。"fallback" 实际是 `direction_disabled` 静默 no-op。 | 中 | `mid_mpc_node.cpp:753-779` |
| F4 | L1b schedule 是 **bound-based 不是 idxsh-based**:三阶段 commit→soften→hard 修改 per-stage `lh/uh` 边界,**从不碰 idxsh**。 | 中 | `mid_mpc_acados_solver.cpp:177-231` |
| F5 | NP 命名歧义 + 注释 stale:`mid_mpc_acados_solver.cpp:1148` 说 `np_global=106`,实际 155。 | 低 | 多处 |
| F6 | `HeadOn5000m_GiveWayStarboard_Converges` 测试名字撒谎(函数体不断言收敛,预期 status=3)。 | 中 | `test_mid_mpc_acados_solver.cpp:1051` |
| F7 | raw=2 = MAX_ITER 不是 Infeasible(见上)。 | 高 | `mid_mpc_acados_solver.cpp:116-118` |

### v4 设计稿(权威,必读)

**文件**: `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md`

这份设计稿包含:
- 每个 test 的精确规格(名字/输入/断言/期望/file:line 锚点)
- L0(12) + L1(11) + L2(5) + scan(4) 共 ~32 个新 test
- G+H scan 加速方案(共享 capsule + max_iter override,~85s < 120s)
- 改动清单(文件路径 + 动作)

### 用户已确认的决策(不要再问)

- ✅ **颗粒度 = L0/L1/L2 三层**(不细跟每次 commit)
- ✅ **先补 contract test,通过后再补并行 scan**
- ✅ **scan 预算 = 120s**,用 G+H 方案(~85s)
- ✅ **L0-T4 写成 RED test**(显式证伪 "L0 GATE closed":degradation flag write-only)
- ✅ **接受 2 处 solver debug 接口**(`set_max_iter_diagnostic` + `debug_get_stage_bounds`)
- ✅ **L0 用抽纯函数**(不用 friend hook — MidMpcNode 构造太重)
- ✅ **L1 codegen 在 test SetUp 里 re-codegen**(最安全)
- ✅ **逐 Phase 实施,每 Phase 完成后给用户看结果再继续**

### 本对话任务:从 Phase A 开始

#### Phase A(预计 30-45min)— 创建基础设施

**A.1**: 创建 `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/l0_guards.hpp` + `src/l3_tdl_kernel/m5_tactical_planner/src/common/l0_guards.cpp`,包含 9 个纯验证函数(跟随现有 `validate_speed_box` 模式):

| 函数 | 签名 | 行为 | 源行(mid_mpc_node.cpp) |
|---|---|---|---|
| `validate_own_heading` | `(double heading_deg, InputDegradation&) -> double psi_rad` | NaN/Inf → 0.0 + flag;else normalize_signed | `:527-536` |
| `validate_own_speed` | `(double u_water, double sog_kn, InputDegradation&) -> double u_mps` | u_water>0.1&finite → u_water;else sog finite&≥0 → sog;else 0.0+flag | `:538-551` |
| `validate_target_latlon` | `(double lat, double lon) -> bool valid` | both finite → true;else false | `:560-564` |
| `validate_target_sog` | `(double sog_kn, InputDegradation&) -> double sog_mps` | finite&≥0 → sog_mps;else 0.0+flag | `:573-581` |
| `validate_box_reach` | `(double box_reach_deg, InputDegradation&) -> double` | finite&≥0 → box_reach;else 0.0+flag | `:614-625` |
| `validate_rot_step` | `(double rot_step_deg, InputDegradation&) -> double` | finite&>0 → rot_step;else 0.0+flag | `:628-637` |
| `validate_min_alt` | `(double min_alt_rad, InputDegradation&) -> double` | finite&≥0 → min_alt;else 0.0+flag | `:638-647` |
| `bump_cpa_safe_for_conflict` | `(bool conflict_active) -> double cpa_safe_m` | conflict → 2500.0;else 1852.0 | `:787-791` |
| `check_box_reach_pref_dir_consistency` | `(double box_reach, bool conflict_active, ColregsPreferredDirection, InputDegradation&) -> void` | box_reach>0 & conflict & pref_dir∉{Stbd,Port} → flag | `:769-779` |

**A.2**: 微重构 `mid_mpc_node.cpp::assemble_input_`(line 510+):把内联验证代码替换为调用 `l0_guards.hpp` 的纯函数。**行为必须完全不变**(只搬位置,不改逻辑)。warn 日志保留在 `assemble_input_` 内(纯函数不 log)。

**A.3**: 在 `mid_mpc_acados_solver.hpp/cpp` 加 2 个 test-only debug 接口:
- `void set_max_iter_diagnostic(int max_iter)` — 仅 scan 测试用,设 `diag_max_iter_` 成员;`solve()` 开头 `if (diag_max_iter_ > 0) ocp_nlp_solver_opts_set(opts, "max_iter", &diag_max_iter_);`
- `std::vector<StageBounds> debug_get_stage_bounds() const` — 返回 per-stage lbx/ubx 值,供 L2-T1/L2-T2 检查 heading 延迟 schedule

**A.4**: 在 `CMakeLists.txt` 注册 `l0_guards` 库 + 4 个新 test binary(L0/L1/L2/scan)。

**Phase A 验证标准**:
- 容器内 `colcon build --packages-select m5_tactical_planner --symlink-install --cmake-args -DM5_USE_ACADOS=ON -DBUILD_TESTING=ON` 成功
- 现有 test suite 全绿(474+ tests,**行为不变**)
- 给用户看 diff + test 结果,等确认后才进 Phase B

#### Phase B(预计 45min)— L0 contract tests(~12 tests)

新建 `test/unit/test_l0_contracts.cpp`,实现 v4 设计稿 §2.2 的 12 个 test(直接调 l0_guards 纯函数):
- T1/T1b: validate_own_heading(NaN + valid)
- T2/T2b: validate_own_speed(NaN + valid water)
- T3/T3b: validate_target_latlon(NaN + valid)
- T4: **RED test** — DegradedFlagsPropagate_ToSolverBehavior_RED(证伪 "L0 GATE closed")
- T5/T5b: check_box_reach_pref_dir_consistency(box_reach>0+Hold + box_reach=0)
- T6: kCpaSafeFallback_m == 1852.0(锁默认值)
- T7/T7b: bump_cpa_safe_for_conflict(2500 + 1852)

#### Phase C(预计 60min)— L1 codegen fixture + L1a tests(T1-T6)

新建 `test/unit/test_l1_contracts.cpp`。L1 codegen 在 gtest Environment SetUp 里 subprocess 调 `gen_mid_mpc_acados.py`,parse stdout 断言 signature(nsh=0/np_global=155/np_per_stage=56/nh=20)。

L1a tests:
- T1: Codegen_ProducesStep5Signature
- T2: SolverNpMacroMatchesCodegenHeader(static_assert)
- T3: Codegen_ConstrHFunReadsCpaHardSlot154NotCpaSafeSlot10
- T4: BoxLive_LiveBoundsWrittenStages1ToN(friend counter)
- T5: BoxLive_DefaultBoundsSkipsModelSet
- T6: Stage0EqualityPin_MatchesCodegenSignature

#### Phase D(预计 60min)— L1b tests(T7-T11)

- T7: ReachabilitySchedule_KHeadEarliest_BoxPathFormula
- T8: ReachabilitySchedule_KMinaltUsesBprimeRotStep(kSurrogateFudgeFactor=2.0)
- T9: CpaSchedule_ThreePhaseBoundBasedNotIdxsh
- T10: PrefixCpaWitness_ViolationOverridesToNumericalFailure
- T11: ReachabilitySchedule_KHeadEarliestExceedsLatest_WarnsButDoesNotFail

#### Phase E(预计 30min)— L2 contract tests(5 tests)

新建 `test/unit/test_l2_contracts.cpp`:
- T1: BoxLive_HeadingDelayedToKHeadEarliest(用 debug_get_stage_bounds)
- T2: DMinTelemetry_FoldedBeforeIsRelaxedGuard(复用 FB-2b)
- T3: WarmStartShiftInit_SecondCycleUsesPrevSolution(复用)
- T4: OcpLayout_StaticAssertsHoldAtCompileTime
- T5: WrapperNpCommentNotStale(注释漂移检测)

#### Phase F(预计 30min)— Regression scan(G+H,4 tests)

新建 `test/unit/test_regression_scan.cpp`:
- `SharedSolverEnv` gtest Environment:1 个 solver 实例 + `set_max_iter_diagnostic(100)`
- S-T1: RegressionScan_EightPoints_SharedCapsule(diagnostic,只 log)
- S-T2: RegressionScan_Gap52_MustConverge_BaselineGate(raw==0 硬断言,**会 RED**)
- S-T3: RegressionScan_Gap252_MustConverge(raw==0||raw==4)
- S-T4: RegressionScan_Gap352_StatusDocumented(只 log)

**时间预算验证**: 1 warm-up(~25s) + 8 solves(6×10s fail + 2×0.3s) ≈ 86s < 120s ✓

#### Phase G(预计 60min)— 诊断报告 + 文档纠正

1. 写 `docs/superpowers/review/2026-07-21-m5-7layer-regression-baseline.md`,记录:
   - P4 baseline vs L3 HEAD scan 对比表
   - 7 个 finding(F1-F7)的代码证据
   - 每个 contract test 的 RED/GREEN 结果
   - 退化源归因(基于 contract test 结果)
   - 修复建议(回滚/部分回滚/参数调整/重新设计)— **让用户决定**

2. 纠正架构文档 false claims:
   - `docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md` §6(L3 GATE closed → 改为"未关闭,退化待定位")
   - §9 排障表(F-05 状态更新)
   - §10 GATE 表(L3 ✅ → L3 ⬠ 阻塞)
   - §12 实施总表(F-05 ✓ → ⬠,记录 7 个 finding)

### 关键参考文档(读这些就够)

1. **v4 设计稿(本对话权威)**: `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md`
2. **7 层主架构**: `docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md`
3. **P5 收敛边界报告**: `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md`(§2 边界表 = P4 baseline)
4. **workspace_log**: `handoff/workspace_log.md`(末尾含本对话完整 handoff entry + Phase 1 调查细节)

### 关键代码文件(Phase A 会动这些)

- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` — `assemble_input_` 在 line 510
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_node.hpp` — MidMpcNode class
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp` — wrapper
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp` — 加 debug 接口
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/common/types.hpp` — `InputDegradation` struct + 现有 `validate_speed_box`/`validate_earliest_min_alt_k`
- `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py` — codegen
- `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_l0_input_guards.cpp` — 现有 L0 test 模式参考
- `src/l3_tdl_kernel/m5_tactical_planner/CMakeLists.txt` — 注册新 test

### 标准运行流程

**Codegen + shared_lib + build**(每次切 commit 后必做):
```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && cd /opt/ws &&
  python3 src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py &&
  cd src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/c_generated_code &&
  make clean_shared_lib 2>/dev/null; make shared_lib &&
  cd /opt/ws && colcon build --packages-select m5_tactical_planner --symlink-install \
    --cmake-args -DM5_USE_ACADOS=ON -DBUILD_TESTING=ON 2>&1 | tail -5'
```

**跑单个 test binary**:
```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws && ./build/m5_tactical_planner/<test_binary_name> 2>&1 | tail -20'
```

**跑全量 test suite**(Phase A 验证用):
```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | tail -30'
```

### Pitfalls(必读,避免重蹈覆辙)

1. **P5 §2 表可信**: 上一对话已独立复现,不要再质疑。
2. **`c_generated_code/` git-ignored**: 每次切 commit 后**必须重跑 codegen + make shared_lib + colcon build**,否则会用旧 codegen 测新代码。
3. **raw status 语义**: 看 raw,不看 mapped。raw=0=Converged, raw=1=Timeout(max_iter), raw=2=MAX_ITER(被误导映射为 Infeasible), raw=3=NumericalFailure, raw=4=QP error recovered。
4. **MidMpcNode 构造太重**: 加载 yaml + 建 IPOPT/acados formulation + ROS2 subscriptions。**单元测试不要构造它,用纯函数**(这是 v3→v4 修订的原因)。
5. **BUG-BUILD-01**: `mid_mpc_node.cpp:1411` 的 `#ifdef M5_USE_ACADOS` 块引用未实现方法(`last_nlp_backend` 等)。这是 pre-existing 问题,L1a batch2 commit `a283fd1b0` 已用 `#if 0` 注释掉。HEAD `fb84701b1` 上已修复,但切到更早 commit(L0 `6a0c12f3b` 等)时需要手动应用相同 bypass。
6. **scan 慢的原因**: 每个 TEST_F fresh capsule = cold warm-up(~25s,占 60%)+ fail case 跑满 400 iter(~35s,占 35%)。G+H 方案同时解决两个瓶颈(共享 capsule + max_iter=100)。
7. **L0 degradation flag 是 dead code**: 文档说下游消费,实际无人读。L0-T4 RED test 会显式暴露这个。
8. **之前的误判记录**(避免再犯):
   - F1 一开始被列为 critical("stale artifact"),实地验证后降级为"codegen-state 依赖风险"
   - scan 加速一开始推荐 D 方案(~120s),用户挑战后找到 G+H(~85s)
   - L0 friend hook 一开始计划用,Phase A 前发现 MidMpcNode 构造太重,改为抽纯函数

### 验收标准(全部 Phase 完成后)

- [ ] Phase A: l0_guards.hpp/cpp 创建 + assemble_input_ 微重构 + solver debug 接口
- [ ] Phase A: 现有 test suite 全绿(行为不变)
- [ ] Phase B: L0 contract tests(~12)实现,记录 RED/GREEN
- [ ] Phase C: L1a tests(T1-T6)实现
- [ ] Phase D: L1b tests(T7-T11)实现
- [ ] Phase E: L2 contract tests(5)实现
- [ ] Phase F: Regression scan(G+H,4 tests)实现,时间 < 120s
- [ ] Phase G: 诊断报告写入 `docs/superpowers/review/2026-07-21-m5-7layer-regression-baseline.md`
- [ ] Phase G: 架构文档 §6/§9/§10/§12 纠正 false claims
- [ ] 每个 Phase 完成后给用户看 diff + test 结果,等确认才进下一个 Phase

### 禁止的捷径(AGENTS.md + 用户明确要求)

- ❌ 用 mock/skip/forced-pass 掩盖退化
- ❌ 用"方法论不公平"等借口解释退化
- ❌ 在未完成诊断前就修改 codegen default 或 solver production 路径
- ❌ 改 production `solve()` 路径行为(诊断阶段;max_iter override 只走 diagnostic path)
- ❌ 写 vessel/scenario-specific 分支
- ❌ 在未完成诊断前就更新 GATE 状态文档
- ❌ 跨 Phase 跳跃(每 Phase 完成后必须等用户确认)
- ❌ 一次性实施全部 Phase(用户已明确要求逐 Phase 确认)

### 第一步建议

1. 读 v4 设计稿 `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md`(全文,~250 行)
2. 读架构文档 `docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md` §3/§4/§5(L0/L1/L2 模块契约)
3. 读现有 L0 test `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_l0_input_guards.cpp`(模式参考)
4. 读 `mid_mpc_node.cpp:510-900`(assemble_input_ 完整实现,确认纯函数提取边界)
5. 启动 Phase A.1:创建 `l0_guards.hpp/cpp` 的骨架(9 个函数签名 + 文档注释),给用户看骨架后再填实现

**记住**: 用户要求"实事求是,使用 systematic-debugging,保证 7 层 MPC 的每一层都合理,避免让问题漏到后面导致耦合难以分析"。每一步都要有代码证据,不要凭直觉。

</handoff-prompt>
