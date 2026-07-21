# M5 L3 HEAD SQP 收敛退化排查 — 新对话提示词

## 任务背景(必读)

**上一对话已完成 LBX-len5 production bug 修复 + 7 层 contract test 基础设施**,但 Regression scan 暴露了一个更深的 **L3 HEAD SQP 收敛退化**问题。本对话的任务是**用 git bisect 定位转折 commit,然后系统性排查根因**。

**为什么这是必要的**:scan 数据(S-T2/S-T3 RED)显示 L3 HEAD 在 gap ≤ +152 的所有点上跑满 400 SQP iter 都不收敛(raw=2 MAX_ITER),而 P4 baseline(commit 2c031bc49)在 ~110-150 iter 就收敛。这是真实的 production 退化,**不是测试设计问题**(已用 systematic-debugging Phase 1 验证过)。退化与 LBX-len5 无关(LBX-len5 修复后 scan 结果完全没变,因为 scan 用 default heading box ±π,box live 写入路径整个跳过)。

## 工作目录(权威)

`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`
- 分支: `codex/m5-design-grounding`(当前 detached HEAD 在 LBX-len5 修复后状态)
- 上一个 task 改动**未 commit**(全是 working tree 改动 + 新 untracked 文件)
- 容器: `codex-m5-p3-sil-nodes-1`(运行 13h+,acados 已装)。**继续使用,不要重启**。
- bind-mount: 容器 `/opt/ws/src` → host worktree `src/`;host git checkout 自动反映到容器。
- **前置**: 本对话开始前**先 commit 上一个 task 的改动**(LBX-len5 fix + contract test + scan),让 bisect 有干净起点。

## Phase 1 已验证的关键事实(不要再质疑)

1. **LBX-len5 bug 已修**(`mid_mpc_acados_solver.cpp:1562-1565`,长度 5→3 紧凑数组)。L1-T4/L2-T1 contract test 已转 GREEN。这个 fix 是对的,**不要回滚**。
2. **退化与 LBX-len5 无关**: scan 场景用 default heading box ±π,`hdg_differs=false`,box live 写入块整个跳过。修 LBX-len5 前后 scan 数据完全一致。
3. **退化形态**: gap ≤ +152 全 raw=2(MAX_ITER,sqp_iter=400,traj_delta~162000 m,solver 在动但没收敛到最优);gap ≥ +252 全 raw=3/4(QP error at iter 1,solver 没动)。
4. **P4 baseline (2c031bc49) 数据**:

| target_y | gap | P4 sqp_iter | P4 status | HEAD sqp_iter | HEAD status |
|---|---|---|---|---|---|
| 2400 | -548 | 109 | 0 ✅ | 400 (MAX_ITER) | 2 ❌ |
| 2100 | -248 | 135 | 0 ✅ | 400 (MAX_ITER) | 2 ❌ |
| 1900 | -48 | 152 | 0 ✅ | 400 (MAX_ITER) | 2 ❌ |
| 1800 | +52 | 129 | 0 ✅ | 400 (MAX_ITER) | 2 ❌ (S-T2 RED) |
| 1700 | +152 | 112 | 0 ✅ | 400 (MAX_ITER) | 2 ❌ |
| 1600 | +252 | 12 | 0 ✅ | 1 (QP fail) | 3 ❌ (S-T3 RED) |
| 1500 | +352 | 5 | 3 ❌ | 1 (QP fail) | 3(持平,P4 也 fail)|
| 1200 | +652 | 1 | 3 ❌ | 1 (QP fail) | 3(持平)|

5. **既有 production test 也退化**: `test_mid_mpc_acados_solver.cpp:207 PerTargetBreakdown_OneTargetSlackPositive`(target_y=1800 同场景)跑出 status=2 MAX_ITER,只是断言写成 `if (Converged) {...} else { SUCCEED(); }` 才 PASS。
6. **spec §0 提示词预测过这个退化**: "L3 HEAD 退化真实: 用 verified-correct codegen(NSH=0/NP=211)重跑,退化仍在(gap -548~+152 全 raw=2 sqp=400,+252/+352 raw=4)"。本对话的工作是**定位根因 + 修复**,不是重新验证退化存在。

## 关键参考文档

1. 上轮诊断报告: `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-report.md`(§H.2 完整退化分析)
2. P4 baseline 数据原始来源: `docs/superpowers/specs/2026-07-18-m5-p5-acados-convergence-design.md`(§2 表)
3. scan 测试: `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_regression_scan.cpp`(S-T2/S-T3 RED 是退化证据)
4. solver 主文件: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`
5. formulation: `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_formulation.cpp`
6. codegen: `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py`

## 关键 commit

- `2c031bc49` — **P4 baseline**(scan 全 raw=0,可信)
- `fb84701b1` — **L3 HEAD**(scan 全 raw=2/3,退化)
- 中间 commits 都是怀疑对象。重点查 P5/Step5 方案 B 相关 commits(nsh=0 + J_colreg barrier)。

## 推荐调查方向(优先级排序)

### 方向 1: git bisect(首选,最高优先级)

用 S-T2 gap=+52 作为判据(raw=0 = good, raw=2/3 = bad)。建议流程:

```bash
# 1. 先 commit 当前 working tree(LBX-len5 fix + 测试),让 bisect 有干净起点
# 2. 在 worktree 里启动 bisect
git bisect start
git bisect bad fb84701b1   # L3 HEAD(退化)
git bisect good 2c031bc49  # P4 baseline(正常)

# 3. 每个 bisect step 需要:
#    a. 重跑 codegen(NSH=0/NP=211 signature 可能变)
#    b. rebuild m5_tactical_planner
#    c. 跑最小判据测试(用 S-T2 的 make_target_scenario(1800.0) 单测)
#    d. 报 raw=0(good) 或 raw!=0(bad)

# 4. 写一个 bisect run 脚本自动化(参考 scripts/diag-bisect-scan.sh,但要改)
```

**判据脚本建议**(独立可执行,不依赖 gtest):
```cpp
// probe_bisect.cpp:直接调 solver 跑 target_y=1800,打印 raw status
// raw=0 → good, raw!=0 → bad
// 编译:同 /tmp/probe_p4.cpp 的 link flags
```

### 方向 2: 怀疑 Step5 方案 B(nsh=0 + J_colreg barrier)

P4 baseline 的 codegen signature 不一定是 NSH=0/NP=211。查 P4 commit `2c031bc49` 的 c_generated_code 状态:
- 如果 P4 是 NSH=16(有 slack),HEAD 是 NSH=0(无 slack),那么 Step5 方案 B **移除了 slack 这个"安全阀"**,cost landscape 变得更陡 → SQP 难收敛。
- 验证方法: checkout P4,跑 codegen,看 nsh 值;再 checkout HEAD,对比。
- 这与 spec §0 说的 "用 verified-correct codegen(NSH=0/NP=211)重跑,退化仍在" **矛盾** — 那句话意味着 NSH=0 不是退化根因。需要重新核实。

### 方向 3: cost weight 变化

P5 报告 §3 提到 14-arm 消融全部失败。查 P4 → HEAD 之间 cost weight 是否变过:
- `gen_mid_mpc_acados.py` 的 `W_COLREG / W_DIST / W_ROUTE / W_VEL`
- `mid_mpc_acados_formulation.cpp::Config` 默认值
- 重点:J_colreg 的 exp barrier 系数 ζ(zeta)是否调过

### 方向 4: QP tolerance / solver options

查 `gen_mid_mpc_acados.py` 的 `nlp_solver_tol_*`、`qp_solver_*` 在 P4 → HEAD 之间是否变过。

### 方向 5: seed strategy

查 `mid_mpc_acados_solver.cpp` 的 F1 forward-propagated seed 在 P4 → HEAD 之间是否变过。seed 决定 SQP 起步点,坏 seed 会让 SQP 在 cost landscape 上"迷路"。

## 标准运行流程

### 重跑 codegen + build(P4 baseline 或 HEAD 都要先做这步)

```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && cd /opt/ws &&
  python3 src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py > /tmp/gen.log 2>&1
  echo "=== codegen rc=$? ==="
  tail -3 /tmp/gen.log
  cd src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/c_generated_code &&
  make clean_shared_lib 2>/dev/null; make shared_lib 2>&1 | tail -2 &&
  cd /opt/ws && colcon build --packages-select m5_tactical_planner --symlink-install \
    --cmake-args -DM5_USE_ACADOS=ON -DBUILD_TESTING=ON 2>&1 | tail -3'
```

### 跑 scan 看状态

```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws &&
  ./build/m5_tactical_planner/test_regression_scan --gtest_output="xml:/tmp/scan.xml" 2>&1 | tail -10'
```

### 跑单点判据(bisect 用)

```bash
docker exec codex-m5-p3-sil-nodes-1 bash -c '
  source /opt/ros/humble/setup.bash && source /opt/ws/install/setup.bash &&
  cd /opt/ws &&
  ./build/m5_tactical_planner/test_regression_scan --gtest_filter="*Gap52*" 2>&1 | tail -5'
```

## Pitfalls(必读)

1. **c_generated_code/ git-ignored**: 每次切 commit 必须重跑 codegen + make shared_lib + colcon build。否则 stale .so 会污染结果。
2. **shared capsule 污染**: scan 里 8 点 sweep 会污染 shared solver 的 capsule state。bisect 判据用**单独的 solver instance**(gtest fixture 模式),不要用 shared scan env。
3. **acatos cold-start effect**: 第一个 solve 总是失败(status=2)。所有判据都要让 solver 先 warm-up(调一次 throwaway solve)再跑真测试。
4. **codegen signature 依赖 commit**: P4 的 NP 可能不是 211(可能是 142 老 layout)。bisect 时每个 commit 都要重跑 codegen,然后看实际 signature。
5. **LBX-len5 fix 不要回滚**: 那是真 bug 修复(L1-T4 GREEN)。bisect 时如果回到 LBX-len5 fix 之前的 commit,要把那个 fix cherry-pick 过去(否则 psi bound 不落地,会污染收敛性测试)。
6. **MAX_ITER vs QP fail 语义**:
   - raw=0 = Converged
   - raw=2 = MAX_ITER(SQP 跑满 budget 没收敛,traj_delta 大)
   - raw=3 = acatos 内部错误
   - raw=4 = QP error recovered(solver 移动了但 re-check 失败,被映射成 NumericalFailure)
   - **归因看 raw 不看 mapped status**(wrapper line 116-118 把 raw=2 误导映射成 Infeasible,但实际是 MAX_ITER)
7. **scan 时间预算**: 单点 ~30-60s(sqp=400 慢);8 点 sweep ~170s。CMakeLists 里 scan TIMEOUT=600s,够用。
8. **不要碰 production solver 行为除非根因明确**: 上一对话严格遵循 spec §7 "诊断阶段不动 production",本对话用户已授权**超出诊断阶段**,可以改 solver — 但仍要 systematic-debugging Phase 1-4 流程,**每个改动都先用 failing test 验证假设**。

## 禁止的捷径

- ❌ mock/skip/forced-pass 掩盖退化
- ❌ 放宽 scan 断言换假 PASS(上轮已犯过,用户已纠正)
- ❌ 不重跑 codegen 就切 commit
- ❌ 在 shared capsule 上跑判据(会污染)
- ❌ 同时改多个变量(每次只改一个,跑测试,再下一步)
- ❌ 凭直觉改 cost weight / QP tol 不先做 bisect 定位转折 commit

## 第一步建议

1. **commit 上轮改动**(让 bisect 有干净起点)
2. **写 bisect 判据脚本**(独立 C++ probe 或 gtest fixture,跑 target_y=1800 gap=+52,打印 raw status)
3. **在 P4 (2c031bc49) 上验证判据**:重跑 codegen + build + 判据,确认 raw=0(如果 P4 也 fail,说明判据本身有问题)
4. **在 HEAD (fb84701b1) 上验证判据**:同样跑,确认 raw!=0
5. **启动 git bisect**,跑 12-15 步定位转折 commit
6. **拿到转折 commit 后**,`git show <commit>` 看改动,form hypothesis
7. **systematic-debugging Phase 1-4** 验证 hypothesis + 写 failing test + 修 + 验证

## 用户原话(上一对话结尾)

> "同意,给出完整提示词,我在新对话中继续排查求解器退化原因"

**期望产出**:
- 转折 commit 定位
- 根因分析(哪个改动让 SQP 收敛行为退化)
- production fix(经过 systematic-debugging Phase 1-4 验证)
- S-T2 / S-T3 从 RED 转 GREEN
- L3 GATE 真正可以重关(基于修复后的 scan 数据)

**实事求是**: 每一步都要有代码证据,不要凭直觉。如果 bisect 定位到的 commit 是 Step5 方案 B 的核心(无法简单 revert),需要上升到用户讨论是否回退整个 Step5 方案 B。
