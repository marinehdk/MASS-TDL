# P1b-0: acados staging 扩展验证 spike — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 P1a subset(已验证)基础上,逐步加 4 个真实复杂度点(prefix equality / J_colreg 完整 / 全局 σ slack / bound schedule),每个验证 staging 可扩,最后合并验证共存。给 P1b-1 全量迁移前置信心门。

**Architecture:** 全部在 `test/external/acados_staging/` 独立目录,基于 P1a subset 增量扩展。沿用 P1a F1-F5 配置(warm-start seed / uh=1e10 / EXACT hessian / MERIT_BACKTRACKING / status 4 容忍)+ Dockerfile 约束(acados_template --no-deps / ACADOS_SOURCE_DIR=/usr/local)。不碰生产 NLP,IPOPT 保留。

**Tech Stack:** acados 0.4.4(P1a 已装), acados_template 0.4.4, CasADi SX(symbolic,与 P1a subset 一致), HPIPM, CMake, colcon

**Spec:** `docs/superpowers/specs/2026-07-16-m5-p1b0-acados-staging-spike-design.md`
**P1a 起点(模板):** `test/external/acados_m5_subset/{gen_m5_subset.py, subset_runner.cpp, run_subset.sh}`

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,P1a 后 HEAD a2db064b1)
- **沿用 P1a F1-F5**:warm-start seed(forward-propagated,非零初值,否则首 QP 病态);单边 h 上界用 1e10(非 np.inf,t_renderer 拒 Infinity);EXACT hessian(非线性 CPA 需要);MERIT_BACKTRACKING globalization(CPA-active 起点必需);status 4(QP error during refinement)是鲁棒性/容差项,**容忍**,以约束满足判 PASS。
- **不碰生产 NLP**:mid_mpc_nlp_formulation/solver.cpp 不动;全部在 test/external/acados_staging/ 独立目录。M5_USE_CASADI=ON 保留。
- **测试标志**:用 `BUILD_TESTING`(非 M5_BUILD_TESTS)+ 包名 `m5_tactical_planner`(非 l3_tdl_kernel)—— P0/P1a 已修正。
- **容器内执行**:source scripts/a4000-env.sh;独立 COMPOSE_PROJECT_NAME=codex-acados-staging,不碰 mass-l3-sil demo stack。
- 每 task 一个 commit;过一个 task 再加下一个(spec 要求顺序 T1→T5)。
- acados C API 模式(从 P1a subset_runner.cpp 读出):capsule create → get config/dims/in/out → set lbx/ubx/x0 → seed x/u via ocp_nlp_out_set → solve → ocp_nlp_out_get per stage → free。

---

## File Structure

| 文件 | 责任 | 新增 |
|---|---|---|
| `test/external/acados_staging/common.py` | 共享:从 P1a subset 提取 dynamics+CPA+box+EXACT+MERIT_BACKTRACKING+warm-start 基类 | 是 |
| `test/external/acados_staging/T1_prefix/gen_prefix.py` + `runner_prefix.cpp` + `run_prefix.sh` | T1 prefix equality staging 验证 | 是 |
| `test/external/acados_staging/T2_colreg/gen_colreg.py` + `runner_colreg.cpp` + `run_colreg.sh` | T2 J_colreg per-stage EXTERNAL 数值等价 | 是 |
| `test/external/acados_staging/T3_slack/gen_slack.py` + `runner_slack.cpp` + `run_slack.sh` | T3 全局 σ slack 三映射对比 | 是 |
| `test/external/acados_staging/T4_bounds/gen_bounds.py` + `runner_bounds.cpp` + `run_bounds.sh` | T4 bound schedule per-stage lb/ub | 是 |
| `test/external/acados_staging/T5_merged/gen_merged.py` + `runner_merged.cpp` + `run_merged.sh` | T5 4 点合并共存 | 是 |
| `test/external/acados_staging/run_all.sh` | T1→T5 顺序跑(过一个加下一个) | 是 |
| 各目录 `.gitignore` | 忽略 c_generated_code/ + __pycache__(P1a 模式) | 是 |

---

## Task 0: 共享基础 + run_all 框架

**Files:**
- Create: `test/external/acados_staging/common.py`
- Create: `test/external/acados_staging/run_all.sh`
- Create: `test/external/acados_staging/.gitignore`

**Interfaces:**
- Produces: `common.py` 导出 `build_base_ocp(N, DT, target_x, target_y, cpa_hard, psi_lb, psi_ub)` 返回配好 dynamics+CPA+box+EXACT+MERIT_BACKTRACKING 的 `AcadosOcp`(无 stage cost,各 task 自加);`forward_seed(x0, dpsi_seq)` warm-start helper

- [ ] **Step 1: 写 common.py — 从 P1a gen_m5_subset.py 提取共享部分**

读 `test/external/acados_m5_subset/gen_m5_subset.py`(P1a 模板),提取 dynamics(`px[k+1]=px[k]+u[k]·dt·cos(psi[k])` 等)+ CPA nonlinear h + 航向 box bounds + solver options(EXACT hessian / DISCRETE integrator / SQP / MERIT_BACKTRACKING / FULL_CONDENSING_HPIPM)到一个 `build_base_ocp(...)` 函数。**不设 stage cost**(各 task 自加)。uh=1e10(F2)。导出 `forward_seed(x0, dpsi_seq, N, DT)` 按 P1a subset_runner.cpp:64-78 模式生成 warm-start seed。

- [ ] **Step 2: 写 run_all.sh — T1→T5 顺序执行框架**

```bash
#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
for t in T1_prefix T2_colreg T3_slack T4_bounds T5_merged; do
  echo "=== running $t ==="
  bash "$t/run_$(echo $t | cut -d_ -f2 | tr '[:upper:]' '[:lower:]').sh" || { echo "FAIL at $t — stop"; exit 1; }
  echo "=== $t PASS ==="
done
echo "ALL PASS: staging scalable, P1b-1 全量 spec 可写"
```

- [ ] **Step 3: 写 .gitignore(各子目录也复制一份)**

```
c_generated_code/
__pycache__/
*.json
```

- [ ] **Step 4: Commit**

```bash
git add test/external/acados_staging/common.py test/external/acados_staging/run_all.sh test/external/acados_staging/.gitignore
git commit -m "feat(m5): acados staging spike common base + run_all (P1b-0 T0)

Extract dynamics+CPA+box+EXACT+MERIT_BACKTRACKING+warm-start from P1a
gen_m5_subset.py into common.build_base_ocp(). T1-T5 incrementally add one
complexity point each. Follows P1a F1-F5 configuration."
```

---

## Task 1: prefix equality staging 验证

**Files:**
- Create: `test/external/acados_staging/T1_prefix/gen_prefix.py`
- Create: `test/external/acados_staging/T1_prefix/runner_prefix.cpp`
- Create: `test/external/acados_staging/T1_prefix/run_prefix.sh`
- Create: `test/external/acados_staging/T1_prefix/.gitignore`

**Interfaces:**
- Consumes: `common.build_base_ocp` + `forward_seed`
- Produces: prefix equality staging 可行结论 + 推荐策略(equality h vs bounds 切换)

**验证点**: k<K 时 psi[k]=prefix_psi[k] 等式;k≥K 自由。每 cycle K 变(本 task 用 K=3 验证)。

- [ ] **Step 1: 写 gen_prefix.py — 策略 (a) equality h-constraint**

基于 `common.build_base_ocp(N=10, DT=5, ...)` 加:
- stage cost `J = Σ_k (psi[k]-psi_ref)²`(航向参考,psi_ref=0.3 终态)
- **prefix equality via h-constraint**:`h_prefix[k] = psi[k] - prefix_psi[k]`,k<K 时 lh=uh=0(等式),k≥K 时 lh=-1e10/uh=1e10(双禁用,F2 有界)。prefix_psi=[0.1,0.2,0.3] for k=0,1,2
- 参数 prefix_psi 作 per-stage p(set via ocp_nlp_params_set in runner)
- 沿用 warm-start seed

```python
# 伪框架 — 实现时填 acados API
from common import build_base_ocp, forward_seed
import casadi as ca, numpy as np
ocp = build_base_ocp(N=10, DT=5.0, target_x=200., target_y=0., cpa_hard=100., psi_lb=-1.2, psi_ub=1.2)
K = 3
prefix_psi_sym = [ca.SX.sym(f"ppsi_{k}") for k in range(ocp.solver_options.N_horizon)]
# stage cost
for k in range(N):
    resid = ocp.model.x[2, k] - psi_ref   # psi - ref (按 acados per-stage 表达)
    ...
# h_prefix: 每 stage 1 行
ocp.model.con_h_expr = ...  # vertcat over stages 或 per-stage h
UH_INF = 1e10
lh = np.array([0.0 if k < K else -UH_INF for k in range(N)])
uh = np.array([0.0 if k < K else UH_INF for k in range(N)])
ocp.constraints.lh = lh; ocp.constraints.uh = uh
```
(具体 acados per-stage h 表达按 acados 0.4.4 examples;con_h_expr 是 per-stage 的,每 stage 一行 psi[k]-prefix_psi[k]。)

- [ ] **Step 2: 写 runner_prefix.cpp — 沿用 P1a subset_runner.cpp API 模式**

clone `subset_runner.cpp` 结构(capsule → config/dims/in/out → set x0 → seed → solve → get per stage → free),改:
- 设 prefix_psi per-stage 参数(k<3 = [0.1,0.2,0.3],k≥3 = 0)
- 断言:
  1. 求解收敛或 status 4(容忍)
  2. k<3: `|psi[k] - prefix_psi[k]| < 1e-4`
  3. k≥3: psi 在 box 内 + CPA 满足
- 打印 "PREFIX PASS: strategy (a) equality-h works" 或失败原因

- [ ] **Step 3: 写 run_prefix.sh — code-gen + 编译 + 跑**

clone `run_subset.sh`,改 solver name + paths。`g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 ...`(ABI,P1a)。

- [ ] **Step 4: 容器内跑 T1,验证策略 (a) equality-h**

Run: `source scripts/a4000-env.sh && COMPOSE_PROJECT_NAME=codex-acados-staging docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_staging/T1_prefix && bash run_prefix.sh"`
Expected: "PREFIX PASS: strategy (a) equality-h works"(k<3 psi==prefix,k≥3 自由避让)。

- [ ] **Step 5: 若策略 (a) 失败,试策略 (b) bounds 切换**

(a) 失败时改 (b):k<K 用 lbx=ubx=prefix_psi[k](等式 bounds),k≥K 用 PSI_LB/UB。重跑验证。两策略都试,记录哪个自然。

- [ ] **Step 6: Commit**

```bash
git add test/external/acados_staging/T1_prefix/
git commit -m "feat(m5): acados staging T1 prefix equality (P1b-0)

Verify prefix equality (k<K psi==prefix_psi, k>=K free) maps to acados
staging. Strategy (a) equality h-constraint with per-stage lh/uh (K=3).
F2 bounded uh=1e10. Records which strategy (a/b) is more natural for P1b-1."
```

---

## Task 2: J_colreg 完整 per-stage EXTERNAL 数值等价

**Files:**
- Create: `test/external/acados_staging/T2_colreg/gen_colreg.py`
- Create: `test/external/acados_staging/T2_colreg/runner_colreg.cpp`
- Create: `test/external/acados_staging/T2_colreg/run_colreg.sh`
- Create: `test/external/acados_staging/T2_colreg/.gitignore`

**Interfaces:**
- Consumes: `common.build_base_ocp` + `forward_seed`
- Produces: J_colreg lumped→staged 数值等价结论(归一化复现方式)

**验证点**: J_colreg 完整形式映射 per-stage EXTERNAL cost,数值等价(staged sum vs 手算 lumped < 1e-6)。

**完整 J_colreg 表达式**(formulation.cpp:344-393):
```
J_colreg = (1/max(1,Nt·N)) · Σ_t Σ_k tw_t · disc_k · exp(-ζ·(d_tk - cpa_safe))
disc_k = exp(-k·dt/T_d),  T_d=100
d_tk = sqrt(dx²+dy²+1),   guard=1
dx = x_own[k] - (tx + tc·ts·k·dt)
dy = y_own[k] - (ty + ts·k·dt)
tw_t = range-ramp (0..1 numeric per target)
```

- [ ] **Step 1: 写 gen_colreg.py — J_colreg per-stage EXTERNAL**

基于 `common.build_base_ocp`,**移除原 CPA h-constraint**(T2 只验 cost,CPA 作 cost 不作 hard h),改用 EXTERNAL cost:
- `cost_type = EXTERNAL`(非 NONLINEAR_LS,因 exp barrier 非二次残差)
- 每 stage k 的 `cost_expr_ext_cost = (1/max(1,Nt·N)) · Σ_t tw_t · disc_k · exp(-ζ·(d_tk - cpa_safe))`
- disc_k 是 stage-k 数值常数(precompute);tw_t/d 依赖 stage-k own pos(从 dynamics)+ target 参数(per-stage p)
- 2 目标(Nt=2),不同 tw/range
- ζ=5e-3, T_d=100, cpa_safe=100(从 Config 默认)

```python
# 伪框架
ocp.cost.cost_type = "EXTERNAL"
for k in range(N):
    stage_cost = 0.0
    disc_k = float(np.exp(-k*DT/T_D))
    for t in range(Nt):
        # tx,ty,tc,ts,tw 作 per-stage p (符号)
        dx = x_own[k] - (tx_t + tc_t*ts_t*k*DT)
        dy = y_own[k] - (ty_t + ts_t*k*DT)
        d_tk = ca.sqrt(dx*dx + dy*dy + 1.0)
        stage_cost += tw_t * disc_k * ca.exp(-ZETA*(d_tk - cpa_safe))
    stage_cost /= max(1, Nt*N)
    # acados EXTERNAL per-stage cost (按 0.4.4 API: model.cost_y_expr_ext_cost 或 per-stage external)
```
(具体 acados EXTERNAL cost API 按 0.4.4 examples;EXTERNAL cost 用 `ocp.model.cost_expr_ext_cost` per stage。)

- [ ] **Step 2: 写 runner_colreg.cpp — 数值等价断言**

clone subset_runner API 模式。solve 后:
1. 取每 stage x(own pos via dynamics 积分)+ u
2. **手算 lumped J_colreg**(同输入,用上面公式手算 sum over t,k)
3. 取 acados 报告的 cost(ocp_nlp_eval_cost 或 out 里 cost)
4. 断言 `|staged_cost - hand_lumped| < 1e-6`
5. 打印 "COLREG PASS: staged J_colreg == lumped (diff=Xe-7)"

- [ ] **Step 3: 写 run_colreg.sh + 容器内跑**

clone run_subset 模式。
Run: `... run --rm sil-nodes bash -c "cd .../T2_colreg && bash run_colreg.sh"`
Expected: "COLREG PASS: staged J_colreg == lumped (diff<1e-6)"。

- [ ] **Step 4: 若数值不等,查归一化/系数**

若 diff > 1e-6:查 `1/max(1,Nt·N)` 是否正确;disc_k 是否每 stage 对;tw_t range-ramp 数值是否对;d_tk guard=1 是否对。修正重跑。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T2_colreg/
git commit -m "feat(m5): acados staging T2 J_colreg per-stage EXTERNAL (P1b-0)

Verify J_colreg full form (exp barrier + TCPA discount + range-ramp + /max
normalization) maps to per-stage EXTERNAL cost with numeric equivalence
(staged sum vs hand-computed lumped < 1e-6). Expression from formulation.cpp:344-393."
```

---

## Task 3: 全局 σ slack 映射验证(三选一)

**Files:**
- Create: `test/external/acados_staging/T3_slack/gen_slack.py`
- Create: `test/external/acados_staging/T3_slack/runner_slack.cpp`
- Create: `test/external/acados_staging/T3_slack/run_slack.sh`
- Create: `test/external/acados_staging/T3_slack/.gitignore`

**Interfaces:**
- Consumes: `common.build_base_ocp` + `forward_seed`
- Produces: 全局 σ slack 推荐映射(a/b/c)+ exact-penalty 验证

**验证点**: 单标量 σ(所有 CPA 行共享)映射三选一,验证 exact-penalty(feasible 时 σ=0,infeasible 时 σ>0 松弛)。

- [ ] **Step 1: 写 gen_slack.py — 三映射各一 solver(或一 solver 三配置)**

测试场景:CPA 不可达(目标极近,迫使 σ>0)。映射候选:
- **(a) σ 作每 stage 参数绑一个 scalar control**:加一个全局 slack "control"(dim 1,跨 stage 共享)→ hacky,验证是否可行
- **(b) per-stage slacks ξ_k + w_slack 重校准**:每 stage 一个 ξ_k,CPA h 软化 + ξ_k slack(Zl/Zu 权重),验证 exact-penalty(ξ_k=0 feasible)
- **(c) σ 外层循环**:不在 acados 内,跳过(失实时性,spec 标不推荐)

```python
# 映射 (b) 伪框架(per-stage slack,acados 原生支持)
# CPA h: g_cpa[k] = d² - cpa_hard² + ξ_k,  ξ_k >= 0 slack
# slack penalty: Zl (quadratic) + zl (linear) — TBD-6 混合 L1/L2 在 P3,本 task 先验纯 L2 exact-penalty
ocp.constraints.lh = np.zeros(N)  # g+ξ >= 0
ocp.constraints.uh = np.full(N, 1e10)
# slack: idxsh = range(N) (which h rows are softened)
ocp.constraints.idxsh = np.arange(N)
ocp.cost.Zl = np.array([w_slack])  # per-slack quadratic weight
ocp.cost.zl = np.array([rho])      # per-slack linear weight (exact-penalty)
```

- [ ] **Step 2: 写 runner_slack.cpp — exact-penalty 断言**

两场景:
- **场景 1(CPA 可达)**:目标远,CPA 本就可行 → 断言 σ/ξ_k ≈ 0(exact-penalty:feasible 时 slack=0)
- **场景 2(CPA 不可达)**:目标极近,CPA 不可行 → 断言 σ/ξ_k > 0 + CPA h 按 slack 松弛(g_cpa+ξ ≥ 0)

打印每映射(a/b)的 exact-penalty 表现 + 推荐映射。

- [ ] **Step 3: 写 run_slack.sh + 容器内跑**

Run: `... bash run_slack.sh`
Expected: 两场景 exact-penalty 都满足;记录推荐映射(b 最可能,acados 原生 idxsh/Zl/zl)。

- [ ] **Step 4: 若三映射全失 exact-penalty,记录阻塞**

若 (a)(b) 都不能保 exact-penalty(feasible 时 slack 不归零)→ 记录,P1b-1 须重新设计 slack 策略(可能须先做 P3 TBD-6 per-target ξ 混合 L1/L2)。spec 失败处置。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T3_slack/
git commit -m "feat(m5): acados staging T3 global sigma slack mapping (P1b-0)

Verify single scalar sigma (shared across all CPA rows) maps to acados with
exact-penalty (sigma=0 when CPA feasible, sigma>0 when infeasible relaxes).
Three candidates: (a) sigma as stage-param scalar control, (b) per-stage
slack xi_k via idxsh/Zl/zl, (c) outer loop (skipped). Records recommended
mapping for P1b-1. Note: per-target per-step xi upgrade is P3 TBD-6."
```

---

## Task 4: bound schedule per-stage lb/ub 验证

**Files:**
- Create: `test/external/acados_staging/T4_bounds/gen_bounds.py`
- Create: `test/external/acados_staging/T4_bounds/runner_bounds.cpp`
- Create: `test/external/acados_staging/T4_bounds/run_bounds.sh`
- Create: `test/external/acados_staging/T4_bounds/.gitignore`

**Interfaces:**
- Consumes: `common.build_base_ocp` + `forward_seed`
- Produces: bound schedule per-stage 映射结论 + OR-composition 复现

**验证点**: 每 cycle 软化早期行的 bound schedule 映射 per-stage lb/ub arrays。

- [ ] **Step 1: 写 gen_bounds.py — CPA h 的 per-stage 可变 lb/ub**

基于 `common.build_base_ocp`(CPA 作 h-constraint),设置 CPA h 的 per-stage lb/uh:
- cpa_hard_from_k=3:k<3 → lh=-1e10(软化,允许 g_cpa<0,靠 J_colreg 代价拉回),k≥3 → lh=0(hard g_cpa≥0)
- 同时 K=3 prefix equality(T1 策略)叠加 → OR-composition: prefix-soften(k<K) ∪ cpa-suffix-hard(k≥cpa_hard_from_k)

```python
cpa_hard_from_k = 3
UH_INF = 1e10
lh = np.array([-UH_INF if k < cpa_hard_from_k else 0.0 for k in range(N)])
uh = np.full(N, UH_INF)
ocp.constraints.lh = lh
ocp.constraints.uh = uh
```

- [ ] **Step 2: 写 runner_bounds.cpp — schedule 复现断言**

solve 后:
1. 取每 stage CPA g_cpa 值
2. 断言 k<3: g_cpa 可 <0(软化生效,但 J_colreg 代价应拉回不致严重违反)
3. 断言 k≥3: g_cpa ≥ -tol(hard 生效)
4. 验证 OR-composition(prefix k<3 + cpa suffix k≥3)正确叠加
5. 打印 "BOUNDS PASS: per-stage schedule reproduces prefix-soften ∪ suffix-hard"

- [ ] **Step 3: 写 run_bounds.sh + 容器内跑**

Run: `... bash run_bounds.sh`
Expected: "BOUNDS PASS: per-stage schedule reproduces prefix-soften ∪ suffix-hard"。

- [ ] **Step 4: Commit**

```bash
git add test/external/acados_staging/T4_bounds/
git commit -m "feat(m5): acados staging T4 bound schedule per-stage lb/ub (P1b-0)

Verify per-cycle bound schedule (soften early rows: cpa_hard_from_k, prefix
K) maps to per-stage h-constraint lh/uh arrays. OR-composition of prefix-
soften (k<K) and cpa-suffix-hard (k>=cpa_hard_from_k) reproduced."
```

---

## Task 5: 4 点合并验证共存

**Files:**
- Create: `test/external/acados_staging/T5_merged/gen_merged.py`
- Create: `test/external/acados_staging/T5_merged/runner_merged.cpp`
- Create: `test/external/acados_staging/T5_merged/run_merged.sh`
- Create: `test/external/acados_staging/T5_merged/.gitignore`

**Interfaces:**
- Consumes: T1 推荐策略 + T2 J_colreg per-stage + T3 推荐 slack 映射 + T4 bound schedule
- Produces: 4 点共存可行结论 → P1b-1 全量 spec 可写

**验证点**: 4 复杂度点(prefix + J_colreg + σ slack + bound schedule)共存一个 acados OCP,跑通。

- [ ] **Step 1: 写 gen_merged.py — 合并 4 点**

基于 common,加(用 T1-T4 推荐配置):
- T1 prefix equality(T1 推荐策略,如 equality-h,K=3)
- T2 J_colreg per-stage EXTERNAL(2 目标,完整归一化)
- T3 σ slack(T3 推荐映射,如 per-stage idxsh/Zl/zl)
- T4 bound schedule(cpa_hard_from_k=3 + prefix K=3 OR-composition)
- N=10, DT=5, 2 目标,完整场景

- [ ] **Step 2: 写 runner_merged.cpp — 4 点共存断言**

solve 后:
1. 求解收敛或 status 4(容忍)
2. prefix:k<3 psi==prefix_psi
3. J_colreg:数值合理(避让代价生效)
4. σ slack:exact-penalty(CPA 可行 σ≈0 / 不可行 σ>0)
5. bound schedule:k<3 CPA 软化 / k≥3 hard
6. 解合理(避让方向正确)

打印 "MERGED PASS: 4 complexity points coexist, staging scalable → P1b-1 全量 spec 可写"。

- [ ] **Step 3: 写 run_merged.sh + 容器内跑**

Run: `... bash run_merged.sh`
Expected: "MERGED PASS: 4 complexity points coexist, staging scalable"。

- [ ] **Step 4: 若合并失败,定位交互问题**

若 infeasibility/numerical fail:逐点回退(先去 T4,再去 T3...)定位哪个组合触发。记录交互问题(如 σ slack + bound schedule 同时软化致 infeasible)。spec 失败处置。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T5_merged/
git commit -m "feat(m5): acados staging T5 merged 4-point coexistence (P1b-0)

Verify all 4 complexity points (prefix equality + J_colreg per-stage +
sigma slack + bound schedule) coexist in one acados OCP and solve.
Gate for P1b-1 full migration spec."
```

---

## Task 6: run_all + 验收门 + handoff

**Files:**
- (无新文件;跑 run_all + 验收 + handoff)

- [ ] **Step 1: 跑 run_all(T1→T5 顺序)**

Run: `... bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_staging && bash run_all.sh"`
Expected: "ALL PASS: staging scalable, P1b-1 全量 spec 可写"(5 task 顺序过)。

- [ ] **Step 2: 回归 — P1a smoke/subset + IPOPT 路径无破坏**

Run: `... bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_smoke && bash run_smoke.sh && cd ../acados_m5_subset && bash run_subset.sh"`
Expected: P1a smoke + subset 仍 PASS。
Run: `... run --rm sil-nodes bash -c "cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && colcon test --packages-select m5_tactical_planner --event-handlers console_direct+ 2>&1 | grep -E 'PASSED|FAILED' | tail -10"`
Expected: 与 P1a 基线相同(24/29,5 pre-existing fail,0 新回归)。

- [ ] **Step 3: 验收门核对(spec 7 条)**

- [ ] T1 prefix equality staging 可扩 + 推荐策略
- [ ] T2 J_colreg per-stage EXTERNAL 数值等价(< 1e-6)
- [ ] T3 全局 σ slack 推荐映射 + exact-penalty 验证
- [ ] T4 bound schedule per-stage 映射 + OR-composition
- [ ] T5 4 点合并共存求解收敛
- [ ] P1a smoke/subset + IPOPT 无回归
- [ ] staging 可扩结论:4 点全过 → 可进 P1b-1

- [ ] **Step 4: 若 spike 失败(某 task staging 不可扩),写失败报告**

按 spec 失败处置:记录阻塞点(哪个 task / 哪个映射失败 / 原因),更新 handoff,标"回炉评估 P1b-1 策略"。

- [ ] **Step 5: 更新 handoff/workspace_log.md**

追加 P1b-0 结果(通过/失败 + 4 task 推荐配置 + 是否进 P1b-1)。

- [ ] **Step 6: Commit handoff**

```bash
git add handoff/workspace_log.md
git commit -m "docs(handoff): record P1b-0 staging spike outcome"
```

---

## Self-Review(plan 作者自检)

**1. Spec 覆盖**:
- ✅ common.py + run_all → Task 0
- ✅ T1 prefix equality → Task 1(含策略 a/b)
- ✅ T2 J_colreg per-stage EXTERNAL → Task 2(含数值等价)
- ✅ T3 全局 σ slack → Task 3(三映射 a/b/c)
- ✅ T4 bound schedule → Task 4(含 OR-composition)
- ✅ T5 合并 → Task 5
- ✅ 7 通过判据 → Task 6 Step 3
- ✅ 失败处置 → Task 6 Step 4 + 各 task 失败分支
- ✅ F1-F5 沿用 → Global Constraints + common.py

**2. Placeholder 扫描**:
- acados EXTERNAL cost / per-stage h / idxsh/Zl/zl 的具体 API 标"按 acados 0.4.4 examples"(因 acados Python API 细节未在本 plan 逐一列);但给了表达式(stage_cost 公式 / lh-uh 数组 / idxsh 设置)+ 断言点 + 参照 P1a subset_runner.cpp 的 C API 模式。执行者参考 acados 0.4.4 官方 examples + P1a 已验证代码填充。spike 性质(API 探索)决定,非 plan 缺陷。
- 无 TBD/TODO/FIXME(除引用的 TBD-4/5/6/7)。

**3. 类型一致**: N=10/DT=5/cpa_hard=100/PSI_LB=-1.2/PSI_UB=1.2/K=3 全 task 一致(对齐 P1a subset + spec);uh=1e10 一致(F2)。

**4. 风险**: T2 数值等价(归一化 bug,Step 4 排查)、T3 σ slack 三映射全失(Task 4 失败处置)、T5 合并交互(Task 5 Step 4 定位)。每 task 失败有处置,不强行绕过。
