# P1b-0 Spec: acados staging 扩展验证 spike

> **产出**: brainstorming,2026-07-16
> **方案包**: `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
> **决策树日志**: `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
> **关联裁决**: DP-05(VR-05 NLP→acados)+ TBD-4(实测门)
> **前置**: P1a 通过(commit a2db064b1);P0 完成(manifest 已修正)
> **范围**: M5 MPC 重构 P1b 的第 0 阶段(staging 可行性扩展 spike)。P1b-1(全量等价迁移)在 spike 通过后开。

---

## 目的

P1a 证明了 acados 工具链 + M5 最小子集(dynamics+CPA+box)映射可行。但现有生产 NLP 远比子集复杂(4 个结构决策点),直接全量迁移(P1b-1)风险高。本 spike 在 P1a subset 基础上**逐步加 4 个真实复杂度点**,每个验证 staging 可扩,过了一个再加下一个,最后合并验证 4 点共存。给 P1b-1 全量 spec 前置信心门。

## 背景(来自方案包 + 探索证据)

P1a spike(subset)已验证:discrete dynamics + 单目标 CPA(nonlinear h)+ 航向 box(bounds)+ stage cost + EXACT hessian + MERIT_BACKTRACKING + warm-start。关键发现 F1-F5(warm-start 必需 / 单边 h 需有界上界 1e10 / EXACT hessian / MERIT_BACKTRACKING / status 4 鲁棒性)+ Dockerfile 约束(acados_template --no-deps / ACADOS_SOURCE_DIR=/usr/local)。

探索(migration reference)暴露现有生产 NLP 的 4 个结构决策点(均源于"现有 NLP 是 flat lumped,acados 是 staged"):
1. **prefix equality**(每 cycle K 变):k<K 时 psi[k]=prefix_psi[k] 等式,k≥K 双禁用。映射:bounds 切换 or equality h-constraint。
2. **J_colreg 完整**:per-(target,step) exp barrier `tw·exp(-k·dt/T_d)·exp(-ζ·(d-cpa_safe))` + TCPA discount + range-ramp tw + `/max(1,Nt·N)` 归一化。映射:per-stage EXTERNAL cost。验证 lumped→staged 数值等价。
3. **全局 σ slack**(所有 CPA 行共享单标量)。映射三选一:σ 作 stage 参数 / per-stage slack 重校准 / 外层循环。验证哪个保 exact-penalty 语义。
4. **bound schedule**(每 cycle 软化早期行:minalt_hard_from_k / cpa_hard_from_k / direction_hard_from_k / terminal_nlp_soft)。映射:per-stage lb/ub arrays 随 cycle 变。

**P1a spike 代码位置**(本 spike 的起点/模板):`src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_m5_subset/{gen_m5_subset.py, subset_runner.cpp}`。

## 用户裁决(brainstorming 澄清)

- **P1b 分阶段**: P1b-0(staging 扩展 spike)→ P1b-1(全量等价迁移)→ P1b-2(增强)[用户 2026-07-16]
- **位置状态**: 先扩 spike 验证 staging(非直接全量)[用户 2026-07-16]
- **staging spike 验证点**: 全部 4 个(prefix equality / J_colreg 完整 / 全局 σ slack / bound schedule)[用户 2026-07-16]
- **spike 组织**: 4 点各一测试 + 合并 [用户 2026-07-16]

## 设计

### 改动范围

P1b-0 spike **不碰生产 NLP 代码**,全部在 `test/external/acados_staging/` 独立目录,基于 P1a subset 增量扩展。IPOPT 保留。

| 文件 | 改动 | 类型 |
|---|---|---|
| `test/external/acados_staging/common.py` | 共享(staging subset 基类 + dynamics + CPA + box,P1a subset 提取) | 新增 |
| `test/external/acados_staging/T1_prefix/gen_prefix.py` + `runner_prefix.cpp` | T1 prefix equality staging 验证 | 新增 |
| `test/external/acados_staging/T2_colreg/gen_colreg.py` + `runner_colreg.cpp` | T2 J_colreg 完整 per-stage EXTERNAL 验证 | 新增 |
| `test/external/acados_staging/T3_slack/gen_slack.py` + `runner_slack.cpp` | T3 全局 σ slack 映射验证(三选一) | 新增 |
| `test/external/acados_staging/T4_bounds/gen_bounds.py` + `runner_bounds.cpp` | T4 bound schedule per-stage lb/ub 验证 | 新增 |
| `test/external/acados_staging/T5_merged/gen_merged.py` + `runner_merged.cpp` | T5 4 点合并验证共存 | 新增 |
| `test/external/acados_staging/run_all.sh` | 5 个 task 顺序跑(T1→T5,过一个加下一个) | 新增 |

**明确排除**(推 P1b-1):
- 生产 mid_mpc_solver 切换 acados
- 全量 142 参数 per-stage 分区
- J_route/J_dist/J_vel/J_asym/J_terminal 全 cost 项(本 spike 只验 J_colreg 作代表)
- Rule14/15 硬编码行(推 P5 移除)
- zone 约束(非平滑,单独评估)

### 共享基础(common.py)

从 P1a subset 提取共享部分(dynamics + 单目标 CPA + 航向 box + EXACT hessian + MERIT_BACKTRACKING + warm-start),作为 4 个 task 的起点。每个 task 在此基础上增量加一个复杂度点。沿用 P1a F1-F5 配置(warm-start seed / uh=1e10 / EXACT / MERIT_BACKTRACKING / status 4 容忍)。

### Task 1: prefix equality staging 验证

**验证点**: k<K 时 psi[k]=prefix_psi[k] 等式约束;k≥K 双禁用。每 cycle K 变。

**staging 策略候选**:
- (a) equality h-constraint:`h_prefix[k] = psi[k] - prefix_psi[k]`,k<K 时 lh=uh=0(等式),k≥K 时 lh=-1e10/uh=1e10(双禁用)
- (b) bounds 切换:k<K 时 lbx=ubx=prefix_psi[k](等式 bounds),k≥K 时 lbx=PSI_LB/ubx=PSI_UB

**测试场景**: K=3(N=10),prefix_psi[0..2]=[0.1, 0.2, 0.3](非零 prefix),suffix 自由。验证:求解收敛 + 前 3 步 psi==prefix_psi + 后 7 步自由避让。

**runner 断言**:
1. 求解收敛(status 0 或容忍 status 4)
2. k<3: `|psi[k] - prefix_psi[k]| < tol`
3. k≥3: psi 在 box 内 + CPA 满足
4. 两种 staging 策略(a/b)都能跑通,记录哪个更自然

**输出**: prefix equality staging 可行结论 + 推荐策略(a 或 b)。

### Task 2: J_colreg 完整 per-stage EXTERNAL 验证

**验证点**: J_colreg 完整形式从 lumped 映射到 per-stage EXTERNAL cost,数值等价。

**完整 J_colreg 表达式**(从 formulation.cpp:344-393):
```
J_colreg = (1/max(1, Nt·N)) · Σ_t Σ_k tw_t · disc_k · exp(-ζ·(d_tk - cpa_safe))
disc_k = exp(-k·dt/T_d)           # TCPA discount, T_d=t_discount_s=100
d_tk = sqrt(dx²+dy²+1)            # smoothed distance, guard=1
dx = x_own[k] - (tx + tc·ts·k·dt) # own pos - target dead-reckon
dy = y_own[k] - (ty + ts·k·dt)
tw_t = range-ramp weight (0..1, numeric per target)
```

**staging 映射**: 每 stage k 的 EXTERNAL cost = `(1/max(1,Nt·N)) · Σ_t tw_t · disc_k · exp(-ζ·(d_tk - cpa_safe))`,其中 disc_k 是 stage-k 常数,tw_t/d 等依赖 stage-k 的 own pos(从 dynamics 积分)+ target 参数(per-stage p)。

**测试场景**: 2 目标(Nt=2),不同 tw/range,验证 per-stage sum == lumped sum(数值等价,tol 1e-6)。

**runner 断言**:
1. 求解收敛
2. **数值等价**:per-stage EXTERNAL cost sum vs 手算 lumped J_colreg(同输入),`|staged - lumped| < 1e-6`
3. CPA 约束满足(避让有效)
4. 归一化 `/max(1,Nt·N)` 正确复现

**输出**: J_colreg lumped→staged 数值等价结论 + 归一化复现方式。

### Task 3: 全局 σ slack 映射验证(三选一)

**验证点**: 现有单标量 σ(所有 CPA 行共享)映射到 acados,三选一哪个保 exact-penalty 语义。

**三个候选映射**:
- (a) σ 作每 stage 参数绑一个 scalar control:hacky,但保全局共享
- (b) per-stage slacks ξ_k + 重校准 w_slack:改变惩罚标度,需验证 exact-penalty
- (c) σ 放 acados 外层循环:不推荐(失 MPC 实时性)

**测试场景**: 单目标,CPA 不可达(迫使 σ>0),验证 σ>0 时 CPA 约束松弛 + J_slack=w_slack·σ² 惩罚。

**runner 断言**:
1. 求解收敛
2. σ>0 时:CPA 约束按 σ 松弛(g_cpa + σ ≥ 0)
3. σ=0 时(CPA 可达):exact-penalty(feasible 时 σ=0)
4. 三映射对比:哪个保 exact-penalty 语义 + 实现最自然

**输出**: 全局 σ slack 推荐映射(a/b/c)+ exact-penalty 验证。**注:P1b-0 只验单标量 σ(等价迁移);per-target per-step ξ 升级是 P3 TBD-6 的工作。**

### Task 4: bound schedule per-stage lb/ub 验证

**验证点**: 每 cycle 软化早期行的 bound schedule 映射到 per-stage lb/ub arrays。

**bound schedule**(从 RowBoundConfig + derive_row_bound_config):
- minalt_hard_from_k:k<此值 → minalt 软化 [-1e10,1e10],k≥此值 → hard [0,1e10]
- cpa_hard_from_k:同上(CPA)
- direction_hard_from_k:同上(direction)
- terminal_nlp_soft:默认 true(terminal 行软化,靠 J_terminal)
- prefix K:k<K equality,k≥K 自由

**staging 映射**: per-stage nonlinear h-constraint 的 lb/ub arrays,每 stage k 按 schedule 设值。如 CPA h:k<cpa_hard_from_k → lh=-1e10(软化),k≥ → lh=0(hard)。

**测试场景**: cpa_hard_from_k=3(K=3 prefix + cpa suffix-hard from k=3),验证前 3 步 CPA 软化、后 7 步 hard。

**runner 断言**:
1. 求解收敛
2. k<3: CPA 软化(可违反,g_cpa 允许 <0,但 J_colreg 代价拉回)
3. k≥3: CPA hard(g_cpa ≥ 0)
4. per-stage lb/ub arrays 正确按 schedule 设置

**输出**: bound schedule per-stage 映射结论 + OR-composition(prefix-soften ∪ suffix-hard)复现方式。

### Task 5: 4 点合并验证共存

**验证点**: 4 个复杂度点(prefix + J_colreg + σ slack + bound schedule)共存于一个 acados OCP,跑通。

**测试场景**: 完整 subset + 全部 4 点(N=10, 2 目标, K=3 prefix, cpa_hard_from_k=3, σ slack enabled, J_colreg 完整归一化)。

**runner 断言**:
1. 求解收敛(status 0 或容忍 status 4)
2. prefix equality(k<3)+ CPA(k≥3 hard)+ σ slack + J_colreg 归一化 全部正确
3. 解合理(避让方向正确,σ 合理松弛)

**输出**: 4 点共存可行结论 → 可进 P1b-1 全量 spec。

### 数据流

```
P1a subset(common.py 提取)
  → T1 +prefix equality staging → 过
  → T2 +J_colreg per-stage EXTERNAL → 过
  → T3 +全局 σ slack 映射 → 过
  → T4 +bound schedule per-stage → 过
  → T5 合并 4 点共存 → 过
  → staging 可扩结论 → P1b-1 全量 spec
```

### 错误处理

- 沿用 P1a F1-F5 配置(warm-start seed / uh=1e10 / EXACT / MERIT_BACKTRACKING / status 4 容忍)。
- 每 task 失败即停 + 记录阻塞点(staging 不可扩的具体点)。
- 数值等价(T2)失败 → 记录 lumped vs staged 差异,可能是归一化/系数复现 bug,查证。
- slack 映射(T3)三选一全失败 → 记录,P1b-1 须重新设计 slack 策略(可能须先做 P3 TBD-6)。

## 测试

### 每 task 验证(见上各 task runner 断言)
- T1: prefix equality staging(a/b 策略对比)
- T2: J_colreg 数值等价(per-stage vs lumped, tol 1e-6)
- T3: σ slack exact-penalty(三映射对比)
- T4: bound schedule per-stage(schedule 复现)
- T5: 4 点合并共存

### 回归(不破坏现有)
- P1a smoke + subset 仍绿(acados 工具链未动)
- IPOPT 路径全绿(M5_USE_CASADI=ON,与 P1a 相同 24/29)

### 通过判据(P1b-0 进 P1b-1 的门)
- [ ] T1 prefix equality staging 可扩 + 推荐策略
- [ ] T2 J_colreg per-stage EXTERNAL 数值等价(lumped vs staged < 1e-6)
- [ ] T3 全局 σ slack 推荐映射 + exact-penalty 验证
- [ ] T4 bound schedule per-stage 映射 + OR-composition 复现
- [ ] T5 4 点合并共存求解收敛 + 全约束正确
- [ ] P1a smoke/subset + IPOPT 路径无回归
- [ ] **staging 可扩结论**: 4 点全过 → 可进 P1b-1 全量 spec

### 失败处置
- 某 task staging 不可扩 → 记录阻塞点,**回炉评估**:是否调整 P1b-1 策略(如 T3 σ slack 全失败 → P1b-1 须先解 P3 TBD-6 per-target ξ,或外层 σ 循环)。
- 不强行绕过 —— staging 阻塞是 P1b-1 规划关键输入。

## 风险

- **中**(P1a 工具链已验证,本 spike 是增量扩展已知映射模式)
- 主要风险:T2 数值等价(归一化/系数复现 bug)、T3 σ slack 三映射都失 exact-penalty(须重新设计)、T5 合并时 4 点交互(infeasibility/numerical)
- spike 价值:前置暴露这些,P1b-1 全量 spec 才有信心

## 出 P1b-0 范围(后续)
- **P1b-1**(spike 通过后): 全量等价迁移(142 参数 per-stage 分区 + 全 cost 项 J_route/dist/vel/asym/terminal + 全约束类 + 生产 mid_mpc_solver feature flag 切换 + Rule14 HO benchmark 等价性)
- **P1b-2**: 增强(per-target ξ TBD-6 / x=[ψ,r,u] P2 / 360s P4 / COLREGs 几何 P5)
- **回炉**(spike 失败): 带 staging 不可扩证据重评 DP-05

## 关联
- 方案包组件 3(DP-05)+ 组件 8(TBD-4):本 P1b-0 是 P1b-1 全量迁移的前置 staging 信心门
- P1a spec/plan + F1-F5:本 spike 沿用 P1a 配置
- 探索 migration reference(Q1-Q7):本 spike 4 task 对应 4 个结构决策点
