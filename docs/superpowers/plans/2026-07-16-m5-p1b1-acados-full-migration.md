# P1b-1: 生产 NLP acatos 全量迁移 + Nomoto physics 升级 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把生产 M5 NLP 从 IPOPT(恒速运动学)全量迁移到 acatos(Nomoto 变速 dynamics),用更合适的预测模型提升避碰航线求解精度/效率。这是 DP-05(VR-05)实测落地 + VR-02 Nomoto 升级。

**Architecture:** 方案 A 三阶段。P1b-1a(test/external staging 验证 2 新点 + VDM 辨识 + 合并)→ P1b-1b(生产 MidMpcAcadosSolver,compile-time `M5_USE_ACADOS` flag,默认 OFF,IPOPT 不动)→ P1b-1c(Rule14 HO benchmark 轨迹级行为等价)。沿用 P1b-0 已验证 4 点配置 + F1-F5。

**Tech Stack:** acatos 0.4.4(P1a 已装), acados_template 0.4.4, CasADi SX, HPIPM, CasADi MX(生产 formulation 用 MX), CMake, colcon, VesselDynamicsModel(4-DOF MMG 辨识用)

**Spec:** `docs/superpowers/specs/2026-07-16-m5-p1b1-acados-full-migration-design.md`
**P1b-0 起点(已验证模板):** `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_staging/{common.py, T1_prefix/, T2_colreg/, T3_slack/, T4_bounds/, T5_merged/}`(commit 02ce2bec0)

## Global Constraints

- 工作目录: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(分支 codex/m5-design-grounding,P1b-1 spec 后 HEAD 4aff16587)
- **沿用 P1b-0 F1-F5**:warm-start seed(forward-propagated 非零,否则首 QP 病态);单边 h 上界 1e10(非 np.inf,t_renderer 拒 Infinity);EXACT hessian(非线性 CPA 需要);MERIT_BACKTRACKING globalization(CPA-active 起点必需);status 4(QP error during refinement)容忍,以约束满足判 PASS。
- **沿用 P1b-0 staging 发现**:acatos `lh/uh`/`lbx/ubx`/`idxsh`/`Zl/zl` 全 stage-uniform → per-stage 切换用参数激活因子(`pact*...`);per-stage p 用生成的 `<name>_acados_update_params(capsule, stage, vals, NP)`(非 `ocp_nlp_in_set "p"`);EXTERNAL cost 必须 `cost_scaling=ones(N+1)`(否则 ×DT 偏)。
- **P1b-1a(T6-T9)不碰生产**:全部在 `test/external/acados_staging/` 独立目录。生产 NLP(`mid_mpc_nlp_formulation.cpp`/`mid_mpc_solver.cpp`)只读。
- **P1b-1b 生产 backend**:`M5_USE_ACADOS` 默认 OFF;IPOPT 路径不动,新增 `mid_mpc_acados_*.{hpp,cpp}`;`mid_mpc_solver.cpp` 加 dispatch 分支;`CMakeLists.txt` 加 option。
- **Dockerfile 约束**(P1a 已修,勿破坏):acados_template 必须 `pip install --no-deps`;ACADOS_SOURCE_DIR=/usr/local(安装树)。
- **测试标志**:`BUILD_TESTING`(非 M5_BUILD_TESTS)+ 包名 `m5_tactical_planner`。
- **容器内执行**:`source scripts/a4000-env.sh`;`COMPOSE_PROJECT_NAME=codex-acados-staging`(P1b-1a)或 `codex-acados-backend`(P1b-1b/c);不碰 mass-l3-sil demo stack。
- **失败即停纪律**:某 task/scenario 不可达即停,记录阻塞点,不 mock / 不 forced-pass / 不为过测试调阈值。按阻塞性质分类回炉。
- 每 task 一个 commit;P1b-1a 顺序执行 T8(辨识,产 Nomoto 参数)→ T6(用 T8 参数)→ T7 → T9(合并);过一个加下一个。

---

## File Structure

| 文件 | 责任 | 阶段 |
|---|---|---|
| `test/external/acados_staging/common.py` | 扩 `build_base_ocp_nomoto(N, DT, T, K, ...)`(双通道 Nomoto)+ 保留 `build_base_ocp` | P1b-1a |
| `test/external/acados_staging/T6_nomoto/{gen_nomoto.py, runner_nomoto.cpp, run_nomoto.sh}` | T6 Nomoto dynamics staging | P1b-1a |
| `test/external/acados_staging/T7_xi/{gen_xi.py, runner_xi.cpp, run_xi.sh}` | T7 per-target ξ staging | P1b-1a |
| `test/external/acados_staging/T8_ident/{ident_nomoto.py, verify_nomoto.py, run_ident.sh}` | T8 VDM zigzag 辨识 + 验证 | P1b-1a |
| `test/external/acados_staging/T9_merge6/{gen_merge6.py, runner_merge6.cpp, run_merge6.sh}` | T9 6 点合并 | P1b-1a |
| `test/external/acados_staging/run_all_p1b1.sh` | T8→T6→T7→T9 顺序跑 | P1b-1a |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp` + `src/mid_mpc/mid_mpc_acados_formulation.cpp` | acatos OCP 符号图(Nomoto+6 cost+全约束+142 参数) | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp` + `src/mid_mpc/mid_mpc_acados_solver.cpp` | acatos code-gen+求解+参数 pack+输出重构 | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp` + `src/mid_mpc/mid_mpc_solver.cpp` | dispatch 分支(M5_USE_ACADOS) | P1b-1b |
| `CMakeLists.txt` | option(M5_USE_ACADOS)+ 条件编译+code-gen 步骤 | P1b-1b |
| `test/unit/test_mid_mpc_acados_*.cpp` | acatos backend 单元测试 | P1b-1b |
| `test/external/rule14_ho_benchmark/{run_benchmark.sh, compare.py}` | Rule14 HO benchmark 两 backend 对比 | P1b-1c |

---

## Task 8: Nomoto 参数 VDM zigzag 辨识(先做,产 T6/T9 参数)

> **顺序说明:** T8 先于 T6 执行 —— T8 产出 Nomoto T, K,T6 用这些参数验证 dynamics staging。plan 编号保留 spec 的 T6/T7/T8/T9,执行序 T8→T6→T7→T9。

**Files:**
- Create: `test/external/acados_staging/T8_ident/ident_nomoto.py`
- Create: `test/external/acados_staging/T8_ident/verify_nomoto.py`
- Create: `test/external/acados_staging/T8_ident/run_ident.sh`
- Create: `test/external/acados_staging/T8_ident/.gitignore`

**Interfaces:**
- Consumes: `VesselDynamicsModel`(`src/shared/vessel_dynamics_model.cpp`,45m FCB 4-DOF MMG,`step(State{s.x,s.y,s.psi,s.u,s.v,s.r}, Input{rudder_rad, rpm_rps}, dt_s) -> State`)
- Produces: Nomoto T [s], K [1/s] 数值(打印 + 写 `nomoto_params.json`),供 T6/T9 用

**Nomoto 模型回顾**([R22]):`T·ṙ + r = K·δ`,其中 r=yaw rate, δ=rudder。离散化 `r[k+1] = r[k] + dt·(K·δ[k] - r[k])/T`。辨识:对 VDM 施加 zigzag 舵角序列,记录 (δ[k], r[k]) 时间序列,最小二乘拟合 `T·ṙ ≈ K·δ - r` 即 `ṙ = (K/T)·δ - (1/T)·r`,回归 `ṙ = a·δ + b·r` → `K=a·T=a/(-b)`,`T=-1/b`。

- [ ] **Step 1: 写 ident_nomoto.py — VDM zigzag 仿真 + 最小二乘辨识**

读 `src/shared/vessel_dynamics_model.cpp` 的 VDM(45m FCB)。脚本逻辑:
1. 用 `fcb_capability_fixture.yaml`(`test/fixtures/`)构造 `CapabilityManifest` → `VesselDynamicsModel`。
2. 跑 **10/10 zigzag**:初始直航(u=18kn≈9.26 m/s, psi=0, r=0),舵角规则:ψ 偏离 ≥+10° → δ=-10°(转回),ψ 偏离 ≤-10° → δ=+10°,记录每步 t, ψ, r, δ, u。dt=0.5s,跑 300s(覆盖多个振荡周期)。
3. 跑 **20/20 zigzag**:同样但 ±20° 阈值,跑 400s。
4. 从两序列取 (δ[k], r[k], ṙ[k]≈(r[k+1]-r[k])/dt),最小二乘拟合 `ṙ = a·δ + b·r`(用 numpy `np.linalg.lstsq` 堆叠两序列)。
5. 计算 `T = -1/b`,`K = a·T = a/(-b)`。存无量纲 `T_prime = T·U/L`,`K_prime = K·L/U`(U=巡航速度,L=45m)。
6. 打印 + 写 `nomoto_params.json`: `{T_s, K_inv_s(K 作 1/s), T_prime, K_prime, fit_residual, n_samples}`。

```python
# ident_nomoto.py 关键结构
import json, numpy as np
# (用 subprocess 调 C++ VDM 或直接用 numpy 重实现 VDM 离散方程;
#  推荐:写一个 ident_runner.cpp 调 VesselDynamicsModel.step 输出 csv,ident_nomoto.py 读 csv 辨识。
#  若纯 python 重实现 VDM 风险高(线性化系数),优先 C++ runner。)
# 决策:写 ident_runner.cpp(调 VDM,输出 zigzag csv),ident_nomoto.py 读 csv 做最小二乘。
```
**实现决策(plan 锁定)**:VDM 是 C++ 类,辨识分两件 —— (a) `ident_runner.cpp`:用 `VesselDynamicsModel.step()` 跑 zigzag,输出 csv(t,psi,r,delta,u);(b) `ident_nomoto.py`:读 csv,最小二乘拟合 T,K。这样辨识用的是真实 VDM 代码,不是重实现。

- [ ] **Step 2: 写 ident_runner.cpp — VDM zigzag 仿真器**

clone P1a subset_runner.cpp 的编译/链接结构(但链接 VDM + capability_manifest)。构造 `VesselDynamicsModel`(从 fixture),跑 10/10 + 20/20 zigzag,输出 csv 到 stdout。FcbCapability fixture 路径 `test/fixtures/fcb_capability_fixture.yaml`。

- [ ] **Step 3: 写 verify_nomoto.py — 辨识质量验证**

读 `nomoto_params.json` 的 T,K。用拟合的 Nomoto 离散方程重新模拟 10/10 zigzag,比 VDM 真值 csv:ψ 序列 max|Δ|(回代误差)。打印 + 检查 IMO MSC.137(76) 一超调指标(10/10 ≤10-20°,20/20 ≤25°)。断言回代误差 < 2°(0.035 rad,因 VDM 是简化 MMG,Nomoto 是其降阶拟合,2° 是合理容差)。打印 `IDENT PASS: T=Xs K=Y/s fit_res=Z (ψ reprojection err W deg, IMO overshoot V deg)`。

- [ ] **Step 4: 写 run_ident.sh + 容器内跑**

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
echo "=== [1/3] VDM zigzag simulation (ident_runner) ==="
# 编译 ident_runner.cpp (链 VDM + manifest),跑,输出 zigzag csv
g++ -std=c++17 -D_GLIBCXX_USE_CXX11_ABI=1 -Wall -Werror ident_runner.cpp \
    -I../../../include -I/usr/local/include \
    -L../../../build -lm5_shared_or_vdm \
    ... -o ident_runner && ./ident_runner > zigzag.csv
echo "=== [2/3] Nomoto T,K identification ==="
python3 ident_nomoto.py zigzag.csv > nomoto_params.json
cat nomoto_params.json
echo "=== [3/3] Verification (reprojection + IMO) ==="
python3 verify_nomoto.py nomoto_params.json zigzag.csv
```
Run: `source scripts/a4000-env.sh && COMPOSE_PROJECT_NAME=codex-acados-staging docker compose -f docker-compose.yml -f docker-compose.a4000.yml run --rm sil-nodes bash -c "cd /opt/ws/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_staging/T8_ident && bash run_ident.sh"`
Expected: `IDENT PASS: T=<2-10>s K=<0.1-0.6>/s ...`。

- [ ] **Step 5: 若辨识失败(回代误差大/IMO 不达标),排查**

若 ψ 回代误差 > 2° 或 T,K 落在 [R22] 合理范围(T≈2-10s, K≈0.1-0.6/s)外:查 VDM 是否正确初始化(45m FCB fixture)、zigzag 阈值逻辑、最小二乘堆叠。若 Nomoto 一阶模型本身不足以拟合 VDM 的 4-DOF(横向 v 耦合),记录:"一阶 Nomoto 对简化 MMG 拟合不足,需 Nomoto 二阶或接受更大容差"。这是真实发现(spec 失败处置),不强行调容差。

- [ ] **Step 6: Commit**

```bash
git add test/external/acados_staging/T8_ident/
git commit -m "feat(m5): acados staging T8 Nomoto param VDM zigzag identification (P1b-1)

Identify Nomoto T,K for 45m FCB by running 10/10+20/20 zigzag on the
4-DOF MMG VesselDynamicsModel, least-squares fit Tṙ+r=Kδ. Outputs T,K +
dimensionless T',K' + reprojection error + IMO MSC.137(76) overshoot.
Precision: simplified-MMG fit, sea-trial calibration TBD-5."
```

---

## Task 6: Nomoto 变速 dynamics staging

**Files:**
- Create: `test/external/acados_staging/common_nomoto.py`(或扩 `common.py` 加 `build_base_ocp_nomoto`)
- Create: `test/external/acados_staging/T6_nomoto/gen_nomoto.py`
- Create: `test/external/acados_staging/T6_nomoto/runner_nomoto.cpp`
- Create: `test/external/acados_staging/T6_nomoto/run_nomoto.sh`
- Create: `test/external/acados_staging/T6_nomoto/.gitignore`

**Interfaces:**
- Consumes: `build_base_ocp_nomoto(N, DT, T_nomoto, K_nomoto, ...)`(本 task 扩);T8 的 `nomoto_params.json` 的 T,K 值
- Produces: Nomoto dynamics staging 可行结论 + `build_base_ocp_nomoto` 供 T9 用

**验证点**: Nomoto 离散 dynamics 映射 acatos `disc_dyn_expr`,双通道 u=[δ,n] 控制下 SQP 收敛。

**Nomoto 离散方程**:
```
r[k+1] = r[k] + dt·(K·δ[k] - r[k])/T      # Nomoto 一阶 yaw
ψ[k+1] = ψ[k] + dt·r[k]                    # heading from yaw rate
px[k+1] = px[k] + u[k]·dt·cos(ψ[k])        # kinematic pos (surge u from thrust)
py[k+1] = py[k] + u[k]·dt·sin(ψ[k])
u[k+1]  = u[k] + dt·(thrust(n[k]) - drag(u[k]))   # surge dynamics (simplified)
```
state x=[px,py,ψ,r,u](5) 或 x=[px,py,ψ,r](4)+ u 作 control? **设计决策(锁定)**:x=[px,py,ψ,r](4),控制 u=[δ,n](2 舵角+转速)。surge 速度从 n(转速)经简化推力模型得 u —— 但为降阶,本 task 先用 **u surge 作 stage 参数**(恒定=u0 from input),只验 Nomoto 偏航 dynamics(ψ,r)+ 位置积分 + δ 控制。变速 surge 优化推 T9/P1b-1b(本 task 验偏航通道可扩即可)。

- [ ] **Step 1: 扩 common.py 加 build_base_ocp_nomoto**

`build_base_ocp_nomoto(N=10, DT=5.0, T_nomoto=5.0, K_nomoto=0.3, target_x=200., target_y=0., cpa_hard=100., psi_lb=-1.2, psi_ub=1.2, rot_max=0.2)`:
- state x = vertcat(px, py, psi, r)(nx=4,加 ROT r)
- control u_ctrl = vertcat(delta)(nu=1,舵角;surge 恒定作 param,本 task 先单舵角控制)
- disc_dyn_expr:`r_new = r + DT*(K_nomoto*delta - r)/T_nomoto`,`psi_new = psi + DT*r`,`px_new = px + u_surge*DT*cos(psi)`,`py_new = py + u_surge*DT*sin(psi)`(u_surge 作 param,默认 5.0)
- CPA h + heading box + EXACT + MERIT_BACKTRACKING + warm-start opts(同 build_base_ocp)
- 加 ROT box:`|r| <= rot_max`(lbx/ubx on r, index 3)
- 不设 cost

- [ ] **Step 2: 写 gen_nomoto.py — Nomoto dynamics OCP**

基于 `build_base_ocp_nomoto`(用 T8 的 T,K 值,硬编码或读 nomoto_params.json)。加 stage cost NONLINEAR_LS(ψ - ψ_ref + δ penalty)。生成 C,`SOLVER_NAME="m5_staging_nomoto"`。

- [ ] **Step 3: 写 runner_nomoto.cpp — dynamics 数值正确性断言**

clone subset_runner API 模式。set T,K params(若作 param)+ per-stage。warm-start seed(δ 阶跃序列 forward-propagate by Nomoto)。solve。断言:
1. **dynamics 数值正确**:对每 stage k,acatos 报告的 r[k],ψ[k] vs **手算 Nomoto forward 模拟**(同 δ 序列 + 同 T,K)一致 < 1e-9。
2. 求解收敛或 status 4(F5)。
3. ROT `|r[k]| <= rot_max + tol`、舵角 box、CPA 满足。
打印 `NOMOTO PASS: dynamics forward-match (max err Xe-Y), status=Z`。

- [ ] **Step 4: 写 run_nomoto.sh + 容器内跑**

clone run_subset 模式,改 solver name。Run: `... T6_nomoto && bash run_nomoto.sh`。Expected: `NOMOTO PASS`。

- [ ] **Step 5: 若 dynamics 不匹配(手算 vs acatos 差 > 1e-9),排查**

查:Nomoto 离散方程是否对(acatos DISCRETE integrator 用 disc_dyn_expr 直接,无 RK);T,K 值是否传对;手算 forward 是否用同方程。若 acatos DISCRETE 对非线性 dynamics 有隐式处理差异,记录。

- [ ] **Step 6: Commit**

```bash
git add test/external/acados_staging/common.py test/external/acados_staging/T6_nomoto/
git commit -m "feat(m5): acados staging T6 Nomoto variable-speed dynamics (P1b-1)

Verify Nomoto first-order yaw dynamics Tṙ+r=Kδ maps to acatos disc_dyn_expr
with state x=[px,py,ψ,r], control u=[δ]. Dynamics forward-match (acatos vs
hand-computed < 1e-9). ROT box enforced. T,K from T8 VDM identification.
Variable surge optimization deferred to P1b-1b."
```

---

## Task 7: per-target per-step ξ 高维 slack staging

**Files:**
- Create: `test/external/acados_staging/T7_xi/gen_xi.py`
- Create: `test/external/acados_staging/T7_xi/runner_xi.cpp`
- Create: `test/external/acados_staging/T7_xi/run_xi.sh`
- Create: `test/external/acados_staging/T7_xi/.gitignore`

**Interfaces:**
- Consumes: `build_base_ocp`(P1b-0 单通道;T7 只验 slack 维度升级,与 dynamics 无关,用单通道 base 即可)
- Produces: per-target ξ staging 可行结论 + 混合 L1/L2 配置供 T9/P1b-1b 用

**验证点**: 从单标量 σ(P1b-0 idxsh=[0],1 slack/stage)升级到 per-target ξ(每 target 每 stage 一个 slack)。混合 L1/L2 penalty `ρ·ξ+½w·ξ²`(TBD-6 VR-TBD6 选项 B)。

- [ ] **Step 1: 写 gen_xi.py — 多行 CPA h + per-target idxsh + 混合 L1/L2**

基于 `build_base_ocp`,但 **CPA h 扩成 per-target 多行**:`con_h_expr = vertcat(g_cpa_t0, g_cpa_t1)`(Nt=2 target,每 target 一行,nh=2)。`idxsh = [0, 1]`(软化两行)。`Zl = [w_quad, w_quad]`(二次,如 1e2),`zl = [rho_lin, rho_lin]`(线性 exact-penalty,如 1e3,ρ>‖λ*‖∞ 保精确性)。target 参数 per-stage(同 T2 的 txdrift/tydrift/tw 模式)。生成 C,`SOLVER_NAME="m5_staging_xi"`。

- [ ] **Step 2: 写 runner_xi.cpp — per-target ξ 独立精确性断言**

两场景(同 T3 思路但 per-target):
- **场景 1(两 target 都 CPA 可达)**:两 target 远,断言 max over (target,stage) ξ_{t,k} < tol(都 ≈0,exact-penalty)。
- **场景 2(target A 可达,target B 不可达)**:target A 远,target B 极近(future-violation,T3 教训),断言:ξ_A ≈0(target A 独立精确性),ξ_B > tol(target B 松弛),g_cpa_B + ξ_B ≥ -tol。
读 per-stage sl:acatos `ocp_nlp_out_get(cfg,dims,out,stage,"sl",sl_vec)`,sl_vec 长度 = len(idxsh) = 2(每 target 一个)。
打印 `XI PASS: per-target exact-penalty (A feasible ξ≈0, B infeasible ξ>0)`。

- [ ] **Step 3: 写 run_xi.sh + 容器内跑**

Run: `... T7_xi && bash run_xi.sh`。Expected: `XI PASS`。

- [ ] **Step 4: 若 per-target ξ 不独立(target A 可达但 ξ_A 不归零),排查**

查:idxsh 是否对([0,1] 两行);Zl/zl 是否 per-slack 正确(长度 2);混合 L1/L2 的 ρ 是否够大(exact-penalty 需 ρ>‖λ*‖∞,两 target 各自 λ* 不同,ρ 取 max)。若 acatos 对多行 idxsh 的 slack 有耦合,记录。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T7_xi/
git commit -m "feat(m5): acados staging T7 per-target xi high-dim slack (P1b-1)

Upgrade single-scalar sigma (P1b-0 idxsh=[0]) to per-target per-step xi
(idxsh=[0,1], per-target slack). Mixed L1/L2 penalty rho*xi + 0.5*w*xi^2
(TBD-6 VR-TBD6 option B). Per-target exact-penalty verified (A feasible
xi~0 independent of B infeasible xi>0)."
```

---

## Task 9: 6 点合并共存

**Files:**
- Create: `test/external/acados_staging/T9_merge6/gen_merge6.py`
- Create: `test/external/acados_staging/T9_merge6/runner_merge6.cpp`
- Create: `test/external/acados_staging/T9_merge6/run_merge6.sh`
- Create: `test/external/acados_staging/T9_merge6/.gitignore`
- Create: `test/external/acados_staging/run_all_p1b1.sh`

**Interfaces:**
- Consumes: `build_base_ocp_nomoto`(T6)+ T8 T,K + T7 per-target idxsh + P1b-0 prefix/bound-schedule/J_colreg 配置
- Produces: 6 点共存可行结论 → P1b-1b 生产 backend 可写

**验证点**: Nomoto dynamics(T6)+ prefix equality(T1)+ J_colreg EXTERNAL(T2)+ per-target ξ(T7)+ bound schedule(T4)全共存一个 acatos OCP。

- [ ] **Step 1: 写 gen_merge6.py — 合并 6 点**

基于 `build_base_ocp_nomoto`(T6),叠加:
- T1 prefix equality(第二 h-row `pact_pre*(ψ-ppsi_pre)`,per-stage activation)
- T2 J_colreg EXTERNAL per-stage cost(cost_scaling=ones)
- T7 per-target ξ(CPA 多行 + idxsh=[0,1] + 混合 L1/L2)
- T4 bound schedule(CPA row activation `cpa_act*g_cpa`)
合并 `con_h_expr = vertcat(cpa_act*vertcat(g_cpaA,g_cpaB), pact_pre*(ψ-ppsi_pre))`(nh=3:2 CPA + 1 prefix)。N=10, 2 target, K_prefix=3, cpa_hard_from_k=3, Nomoto T/K from T8。生成 C。

- [ ] **Step 2: 写 runner_merge6.cpp — 6 点共存断言**

per-stage params(Nomoto T/K 全局 + prefix/cpa_act/J_colreg/target per-stage)+ warm-start。solve。断言全部 6 点:
1. Nomoto dynamics forward-match(同 T6)
2. prefix k<3 equality(T1)
3. J_colreg EXTERNAL cost 合理(T2)
4. per-target ξ exact-penalty(T7)
5. bound schedule cpa k<3 soft / k≥3 hard(T4)
6. 求解收敛或 status 4(F5,traj_delta>1e-6)
打印 `MERGE6 PASS: 6 points coexist (Nomoto+prefix+J_colreg+xi+bounds), staging scalable -> P1b-1b 生产 backend 可写`。

- [ ] **Step 3: 写 run_merge6.sh + run_all_p1b1.sh + 容器内跑**

run_all_p1b1.sh 顺序 T8→T6→T7→T9(过一加下一)。Run: `... acados_staging && bash run_all_p1b1.sh`。Expected: `ALL PASS P1b-1a: 6 points staging scalable`。

- [ ] **Step 4: 若合并失败,定位交互**

逐点回退(去 T4 → T7 → T2 → T1 → T6)定位哪个组合触发。常见交互:per-target ξ + bound schedule 在 CPA 行(T5 已验单 σ+activation 正交,per-target 同理应正交);Nomoto dynamics + CPA(变速下 CPA disc 进入时机变)。记录交互。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T9_merge6/ test/external/acados_staging/run_all_p1b1.sh
git commit -m "feat(m5): acados staging T9 6-point merged coexistence (P1b-1a)

Verify all 6 points (Nomoto dynamics + prefix + J_colreg + per-target xi +
bound schedule) coexist in one acatos OCP. Gate for P1b-1b production backend."
```

---

## Task 10: P1b-1a 验收门 + handoff

**Files:**
- (无新文件;跑 run_all_p1b1 + 验收 + handoff)

- [ ] **Step 1: 跑 run_all_p1b1(T8→T6→T7→T9)**
- [ ] **Step 2: 回归 P1b-0 + P1a smoke/subset 无破坏**
- [ ] **Step 3: 验收门核对**:T6 Nomoto 可扩 / T7 per-target ξ 可扩 / T8 辨识完成 / T9 6 点合并 / P1b-0+P1a 无回归。
- [ ] **Step 4: 若 P1b-1a 失败,写失败报告**(阻塞点 + 回炉建议)。
- [ ] **Step 5: 更新 handoff/workspace_log.md**(P1b-1a 结果 + 6 点推荐配置 + 是否进 P1b-1b)。
- [ ] **Step 6: Commit handoff**

---

> **P1b-1b 生产 backend + P1b-1c benchmark 的 task 在 P1b-1a 通过后细化。** P1b-1a 是 staging 信心门;过了才写生产 backend(改动生产代码,风险高,需 P1b-1a 的 6 点配置全部锁定)。下方给出 P1b-1b/c 的 task 骨架(spec 级,执行前 P1b-1a 过了再补全 step)。

---

## Task 11 (P1b-1b 骨架): 生产 MidMpcAcadosFormulation

**Files:** `include/mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}`
- 构建 acatos OCP 符号图:Nomoto dynamics(T6)+ 6 cost(colreg/dist/route/vel/asym/terminal)+ 全约束(CPA/direction/min_alt/ROT/terminal + bound schedule)+ 142 参数 per-stage 分区。复用 P1b-0 staging 发现(参数激活 / cost_scaling=ones / update_params API)。
- 验收:code-gen 成功 + 符号图维度匹配(nx=4, nu, nh, np)。

## Task 12 (P1b-1b 骨架): 生产 MidMpcAcadosSolver

**Files:** `include/mid_mpc/mid_mpc_acados_solver.{hpp,cpp}` + `CMakeLists.txt` + `mid_mpc_solver.cpp` dispatch
- code-gen + 求解 + 参数 pack(142 → 全局/per-stage)+ 输出重构(ψ/u 序列 + cost + status,与 IPOPT 契约一致)。`option(M5_USE_ACADOS "..." OFF)`。
- 验收:标准场景 solve 收敛 + 输出契约字段匹配 IPOPT + 142 参数正确 pack。

## Task 13 (P1b-1c 骨架): Rule14 HO benchmark

**Files:** `test/external/rule14_ho_benchmark/{run_benchmark.sh, compare.py}`
- 两次 build(M5_USE_ACADOS OFF/ON),同 Rule14 HO MidMpcInput,轨迹级行为等价 6 条判据(避让决策/CPA-feasible/轨迹形状<0.1rad/IMO 回转/实时性/cost 报告)。
- 验收:6 条全过 + IPOPT 无回归。

## Task 14: P1b-1 验收门 + promotable

- staging 门(P1b-1a)+ backend 门(P1b-1b)+ benchmark 门(P1b-1c)+ 回归门全过 → merge l3-tdl + push origin/l3-tdl + handoff。

---

## Self-Review

**1. Spec 覆盖**:
- ✅ Nomoto dynamics(VR-02)→ Task 6(+T8 辨识)
- ✅ per-target ξ 混合 L1/L2(VR-03/TBD-6)→ Task 7
- ✅ VDM zigzag 辨识 → Task 8
- ✅ 6 点合并 → Task 9
- ✅ 生产 backend + M5_USE_ACADOS flag → Task 11/12
- ✅ Rule14 HO benchmark(轨迹级行为等价)→ Task 13
- ✅ 失败处置 → 各 task 失败分支 + Task 10/14
- ✅ F1-F5 + staging 发现沿用 → Global Constraints
- ✅ 诚实标注(Nomoto physics 升级 / VDM 辨识精度 / benchmark 非 bit-close)→ spec + 各 task

**2. Placeholder 扫描**: P1b-1b/c(Task 11-14)标"骨架,执行前补全 step" —— 这是**有意为之的 staging 门设计**(spec 明确:P1b-1a 过了才写生产 backend,因 P1b-1a 的 6 点配置是 P1b-1b 的输入)。非 plan 缺陷,是风险控制。P1b-1a 的 Task 6-10 全部有完整 step + 代码骨架 + 断言 + 容器命令。

**3. 类型一致**: Nomoto T/K 全 task 一致(从 T8 产出);state x=[px,py,ψ,r](4)一致;per-target ξ idxsh=[0,1](2 target)一致;混合 L1/L2 ρ·ξ+½w·ξ² 一致。

**4. 风险**: T8 辨识(Nomoto 一阶对简化 MMG 拟合可能不足)、T6 dynamics(DISCRETE integrator 非线性处理)、T7 高维 ξ(ρ 调参)、T9 合并交互。每 task 失败有处置,不强行绕过。
