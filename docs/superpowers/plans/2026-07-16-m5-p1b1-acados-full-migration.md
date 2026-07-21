# P1b-1: 生产 NLP acatos 全量迁移 + Nomoto physics 升级 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把生产 M5 NLP 从 IPOPT(恒速运动学)全量迁移到 acatos(舵驱动航向 dynamics),用更合适的预测模型提升避碰航线求解精度/效率。这是 DP-05(VR-05)实测落地。**注(2026-07-16 Path B 修订)**:原"VR-02 一阶 Nomoto 升级"在 T8 执行中发现 VDM 无偏航阻尼而撞墙,改为 honest 双积分器 dynamics(`ṙ=c(u)·δ, ψ̇=r`)——仍达成"ψ,u 扁平 → 舵驱动航向"的 physics 升级,详见 spec §"关键设计变更(Path B)"。

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
- 每 task 一个 commit;P1b-1a 顺序执行 T8(辨识 c(u),Path B)→ T6(用 T8 c_u,双积分器)→ T7 → T9(合并);过一个加下一个。

---

## File Structure

| 文件 | 责任 | 阶段 |
|---|---|---|
| `test/external/acados_staging/common.py` | 扩 `build_base_ocp_doubleint(N, DT, c_u, u_surge, ...)`(双积分器 Path B)+ 保留 `build_base_ocp` | P1b-1a |
| `test/external/acados_staging/T6_doubleint/{gen_doubleint.py, runner_doubleint.cpp, run_doubleint.sh}` | T6 双积分器 dynamics staging(Path B) | P1b-1a |
| `test/external/acados_staging/T7_xi/{gen_xi.py, runner_xi.cpp, run_xi.sh}` | T7 per-target ξ staging | P1b-1a |
| `test/external/acados_staging/T8_ident/{ident_nomoto.py, verify_nomoto.py, run_ident.sh}` | T8 VDM zigzag 辨识 + 验证 | P1b-1a |
| `test/external/acados_staging/T9_merge6/{gen_merge6.py, runner_merge6.cpp, run_merge6.sh}` | T9 6 点合并 | P1b-1a |
| `test/external/acados_staging/run_all_p1b1.sh` | T8→T6→T7→T9 顺序跑 | P1b-1a |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_formulation.hpp` + `src/mid_mpc/mid_mpc_acados_formulation.cpp` | acatos OCP 符号图(双积分器 Path B +6 cost+全约束+142 参数) | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp` + `src/mid_mpc/mid_mpc_acados_solver.cpp` | acatos code-gen+求解+参数 pack+输出重构 | P1b-1b |
| `include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp` + `src/mid_mpc/mid_mpc_solver.cpp` | dispatch 分支(M5_USE_ACADOS) | P1b-1b |
| `CMakeLists.txt` | option(M5_USE_ACADOS)+ 条件编译+code-gen 步骤 | P1b-1b |
| `test/unit/test_mid_mpc_acados_*.cpp` | acatos backend 单元测试 | P1b-1b |
| `test/external/rule14_ho_benchmark/{run_benchmark.sh, compare.py}` | Rule14 HO benchmark 两 backend 对比 | P1b-1c |

---

## Task 8: VDM zigzag 辨识 → c(u) 增益 + 积分器 model-class 诊断(Path B 修订,2026-07-16)

> **Path B 变更(2026-07-16):** 原一阶 Nomoto `Tṙ+r=Kδ` 辨识(首试,commit `382bc86bb`)撞真实物理墙:VDM `compute_accelerations` 无 `N_r·r` 偏航阻尼 → 偏航是纯积分器(二阶),一阶 Nomoto 结构不可拟合(`b≈-9.7e-4`→`T≈1027s`,回代误差 35°,IDENT FAIL)。**用户裁决 Path B**:T8 改为 honest 辨识速度相关偏航增益 `c(u)=k_n_rudder·u²/izz_e` + 积分器 model-class 正向诊断(不造 VDM 系数)。详见 spec §"关键设计变更(Path B)"。`ident_runner.cpp`(zigzag 仿真器)首试已验证可用,**保留不动**;改 `ident_nomoto.py` + `verify_nomoto.py` 为 c(u) 估计 + 线性度门。

> **顺序说明:** T8 先于 T6 执行 —— T8 产出 `c_u`(偏航增益),T6 用它验证双积分器 dynamics staging。执行序 T8→T6→T7→T9。

**Files(改 2 个,runner 不动):**
- Keep(首试已验证): `test/external/acados_staging/T8_ident/ident_runner.cpp`(VDM zigzag 仿真器,输出 csv: `t,psi,r,delta,u`)
- Modify: `test/external/acados_staging/T8_ident/ident_nomoto.py`(从"拟合 T,K"改为"估计 c(u) + 积分器线性度诊断")
- Modify: `test/external/acados_staging/T8_ident/verify_nomoto.py`(去一阶回代 2° 门,改为 c(u) forward-match + 线性度门)
- Keep: `test/external/acados_staging/T8_ident/run_ident.sh`, `.gitignore`

**Interfaces:**
- Consumes: `ident_runner.cpp` 输出的 zigzag csv(`t_s,psi_rad,r_rad,delta_rad,u_mps`,含 `# maneuver 10/10` / `# maneuver 20/20` 分隔)
- Produces: `c_u`(在 cruise u≈9.26 m/s 下的偏航增益,rad/s² per rad 舵) + 积分器线性度指标(60s 恒舵前后半程 r 增量比) + IMO MSC.137(76) 参考,供 T6/T9 双积分器 dynamics 用

**双积分器 model-class 回顾(Path B):** VDM 偏航 `ṙ = c(u)·δ`(c(u)=k_n_rudder·u²/izz_e,无 r 阻尼项)。离散化 `r[k+1]=r[k]+dt·c(u)·δ[k]`,`ψ[k+1]=ψ[k]+dt·r[k]`。辨识:从 zigzag csv 取 (δ[k], ṙ[k]≈(r[k+1]-r[k])/dt),单变量回归 `ṙ = c_u·δ`(无 r 项,因 model-class 是积分器)。`c_u` 即斜率。

- [ ] **Step 1: 改 ident_nomoto.py — c(u) 估计 + 积分器线性度诊断**

读 zigzag csv(复用首试的解析:`#` 跳过、按 `# maneuver` 切两段)。脚本逻辑:
1. 解析两段 maneuver(10/10、20/20),堆叠 (δ[k], ṙ[k]=(r[k+1]-r[k])/dt) 样本。
2. **积分器 model-class 诊断(正向证据,首要)**:取 20/20 maneuver 中最长恒舵段(δ 恒定 ≥40 步),计算前后半程 r 增量比 `ratio = Δr_2nd_half / Δr_1st_half`。积分器下 ratio≈1(线性增长);一阶 Nomoto 下 ratio≪1(指数收敛)。**门:ratio ∈ [0.95, 1.05]**(积分器正向证据)。打印 ratio + 前后半程增量。
3. **c(u) 估计**:单变量最小二乘 `ṙ = c_u·δ`(`np.linalg.lstsq` 堆两段样本),得 `c_u`[rad/s² per rad]。同时跑旧的 `ṙ=a·δ+b·r` 二元回归作对照(预期 `b≈0`,再次确认积分器),打印 a/b/T(只作诊断,不作门)。
4. 无量纲:`c_prime = c_u·L/U`(L=45,U=cruise u≈9.26)。
5. fit_residual = RMS(ṙ_pred − ṙ_actual);r_squared(单变量)。
6. 写 `nomoto_params.json`(**JSON keys 必须精确**):`{"c_u":..., "c_prime":..., "integrator_ratio":..., "delta_half1":..., "delta_half2":..., "fit_residual":..., "r_squared":..., "n_samples":..., "u_cruise":..., "source":"VDM direct yaw gain (no fabricated N_r); double-integrator model-class; real yaw damping TBD-5 sea-trial", "diagnostic_b_coef":...(旧二元回归 b,≈0 对照)}`。**T6/T9 读 `c_u`**(不再是 T_s/K_inv_s)。保留 T_s/K_inv_s/b_coef/a_coef 作历史诊断字段也可,但 c_u 是新主键。
7. 打印人类可读 summary。

- [ ] **Step 2: 改 verify_nomoto.py — c(u) forward-match + 线性度门**

读 `nomoto_params.json` 的 `c_u` + zigzag csv。验证逻辑:
1. **c(u) forward-match**:用双积分器离散方程 `r[k+1]=r[k]+dt·c_u·δ[k]`、`ψ[k+1]=ψ[k]+dt·r[k]` 重投每个 maneuver(喂 VDM 同 δ 序列),比 VDM 真值 csv 的 r 序列与 ψ 序列:max|Δr|、max|Δψ|。**门:max|Δψ| < 5°(0.087 rad)**(双积分器是 VDM 的 honest 降阶 —— r 通道应几乎精确,ψ 累积误差小)。注意:这比旧 2° 门宽,因双积分器诚实拟合 VDM(同 model-class),不是一阶 Nomoto 强拟合;若 r 通道 match<1e-6 且 ψ<5° 即证 c_u 辨识对。打印 max|Δr|/max|Δψ|。
2. **积分器线性度门(与 Step1 Step 诊断一致)**:ratio ∈ [0.95,1.05]。
3. IMO MSC.137(76) 一超调参考(10/10/20/20,仅参考不门 —— 纯积分器 zigzag 会过冲,这是 model-class 已知行为,不是辨识质量问题)。
4. 打印 `IDENT PASS: c_u=<X> rad/s²/rad (integrator ratio=<Y>, ψ forward-match <Z> deg, r forward-match <W> rad)` on pass;else `IDENT FAIL: ...` 带原因 exit 非零。
5. **诚实守则**:若 ratio 不在 [0.95,1.05](VDM 居然不是积分器),或 r forward-match 差(说明 c(u) 非 const,有未建模速度/耦合),记录真实发现,不强行调 c_u 或门。

- [ ] **Step 3: 改 run_ident.sh 的输出 label(若需)+ 容器内跑**

run_ident.sh 结构基本不变([1]编 ident_runner → zigzag.csv;[2] ident_nomoto.py → nomoto_params.json;[3] verify_nomoto.py)。**链接配方用首试已验证的**(只链 `vessel_dynamics_model.cpp.o` + `capability_manifest.cpp.o` 从 `libm5_shared_lib.a` 用 `ar x` 提取,加 `-lyaml-cpp -lm`,不拖 rclcpp/ament 树 —— 首试 report 已记录该配方)。Expected 输出: `IDENT PASS: c_u=... (integrator ratio≈1.0, ψ forward-match <5°)`。

- [ ] **Step 4: Commit(amend 首试 commit 或新 commit)**

新 commit(不改写历史,保留首试 IDENT FAIL 作 audit trail):
```bash
git add test/external/acados_staging/T8_ident/ident_nomoto.py test/external/acados_staging/T8_ident/verify_nomoto.py test/external/acados_staging/T8_ident/run_ident.sh
git commit -m "feat(m5): acados staging T8 Path B — c(u) gain + integrator model-class (P1b-1a)

Re-scope T8 from 1st-order Nomoto (首试 382bc86bb IDENT FAIL: VDM has no
N_r*r yaw damping, b~-9.7e-4 -> T~1027s, 1st-order structurally unfittable)
to honest double-integrator: estimate yaw gain c(u)=k_n_rudder*u^2/izz_e
(direct VDM read, no fabricated coefficient) + integrator-linearity
diagnostic (60s constant-rudder half-ratio in [0.95,1.05]). User decision
Path B (GNC reviewer + numeric double-check corroborated the physics wall).
c_u feeds T6/T9 double-integrator dynamics. Real yaw damping TBD-5."
```

---

## Task 6: 双积分器 heading dynamics staging(Path B 修订,2026-07-16)

> **Path B 变更:** 原 "Nomoto 变速 dynamics" 改为 honest 双积分器(详见 spec §"关键设计变更(Path B)")。state x=[px,py,ψ,r](4),control u=[δ](1),surge 恒定作 stage param。**不是一阶 Nomoto**(无 T,K)。

**Files:**
- Modify: `test/external/acados_staging/common.py`(加 `build_base_ocp_doubleint(N, DT, c_u, u_surge, ...)`,保留 `build_base_ocp` 不动)
- Create: `test/external/acados_staging/T6_doubleint/{gen_doubleint.py, runner_doubleint.cpp, run_doubleint.sh, .gitignore}`

**Interfaces:**
- Consumes: `build_base_ocp_doubleint(N, DT, c_u, u_surge, target_x, target_y, cpa_hard, psi_lb, psi_ub, rot_max)`(本 task 加);T8 的 `nomoto_params.json` 的 `c_u` 值
- Produces: 双积分器 dynamics staging 可行结论 + `build_base_ocp_doubleint` 供 T9 用

**验证点**: 双积分器离散 dynamics 映射 acatos `disc_dyn_expr`,SQP 收敛(临界稳定极点 z=1 靠 ROT box 保 r 有界)。

**双积分器离散方程(Path B,c(u)=k_n_rudder·u²/izz_e,T8 辨识得 c_u):**
```
r[k+1]  = r[k]  + dt·c_u·δ[k]              # 偏航加速度积分(无 r 阻尼项,VDM 直读)
ψ[k+1]  = ψ[k]  + dt·r[k]                  # heading from yaw rate
px[k+1] = px[k] + u_surge·dt·cos(ψ[k])     # 运动学位置积分(surge 恒定)
py[k+1] = py[k] + u_surge·dt·sin(ψ[k])
```
state x=[px,py,ψ,r](4,加 ROT r)。control u_ctrl=[δ](1,舵角;surge 恒定作 param u_surge=T8 cruise u≈9.26)。**变速 surge 优化推 P1b-1b**(本 task 验偏航通道双积分可扩即可)。

- [ ] **Step 1: 扩 common.py 加 build_base_ocp_doubleint**

`build_base_ocp_doubleint(N=10, DT=5.0, c_u=0.01, u_surge=9.26, target_x=200., target_y=0., cpa_hard=100., psi_lb=-1.2, psi_ub=1.2, rot_max=0.2)`:
- state x = vertcat(px, py, psi, r)(nx=4,加 ROT r)
- control u_ctrl = vertcat(delta)(nu=1,舵角;surge 恒定作 param)
- disc_dyn_expr(双积分器,无 r 阻尼):`r_new = r + DT*c_u*delta`,`psi_new = psi + DT*r`,`px_new = px + u_surge*DT*cos(psi)`,`py_new = py + u_surge*DT*sin(psi)`
- CPA h + heading box + EXACT + MERIT_BACKTRACKING + warm-start opts(同 build_base_ocp)
- **ROT box(临界稳定必需)**:`|r| <= rot_max`(lbx/ubx on r, index 3)
- 不设 cost

- [ ] **Step 2: 写 gen_doubleint.py — 双积分器 dynamics OCP**

基于 `build_base_ocp_doubleint`(用 T8 的 `c_u` 值,读 nomoto_params.json 或硬编码)。加 stage cost NONLINEAR_LS(ψ - ψ_ref + δ penalty)。生成 C,`SOLVER_NAME="m5_staging_doubleint"`。

- [ ] **Step 3: 写 runner_doubleint.cpp — dynamics 数值正确性断言**

clone T5_merged runner API 模式(ocp_nlp_out_get/set,update_params)。set c_u/u_surge params(若作 param)+ per-stage。warm-start seed(δ 阶跃序列 forward-propagate by 双积分器)。solve。断言:
1. **dynamics 数值正确**:对每 stage k,acatos 报告的 r[k],ψ[k],px[k],py[k] vs **手算双积分器 forward 模拟**(同 δ 序列 + 同 c_u/u_surge)一致 < 1e-9。
2. 求解收敛或 status 4(F5,traj_delta>1e-6 solver-moved guard)。
3. ROT `|r[k]| <= rot_max + tol`、舵角 box、CPA 满足。
打印 `DOUBLEINT PASS: dynamics forward-match (max err Xe-Y), status=Z`。

- [ ] **Step 4: 写 run_doubleint.sh + 容器内跑**

clone run_merged 模式(gen→make ocp_shared_lib→g++ 链→run),改 solver name(m5_staging_doubleint)。Run: `... T6_doubleint && bash run_doubleint.sh`。Expected: `DOUBLEINT PASS`。

- [ ] **Step 5: 若 dynamics 不匹配或 SQP 不收敛,排查**

dynamics 差 > 1e-9:查双积分器离散方程是否对(acatos DISCRETE integrator 用 disc_dyn_expr 直接,无 RK);c_u 值是否传对;手算 forward 是否用同方程(注意 ψ 用 r[k] 还是 r[k+1] —— 显式 Euler 用 r[k])。SQP 不收敛(临界稳定):查 warm-start seed 是否合理、ROT box 是否够紧(rot_max 别太大让 r 发散)、cost 是否给 ψ 足够正则。若大 rot_max 下不收敛,记录临界稳定的真实数值风险(spec Path B 已知 gap)。

- [ ] **Step 6: Commit**

```bash
git add test/external/acados_staging/common.py test/external/acados_staging/T6_doubleint/
git commit -m "feat(m5): acados staging T6 double-integrator heading dynamics (P1b-1a, Path B)

Verify honest double-integrator yaw dynamics (dr/dt=c(u)*delta, no N_r*r
damping — VDM direct read, Path B) maps to acatos disc_dyn_expr with state
x=[px,py,ψ,r], control u=[δ]. Dynamics forward-match (acatos vs
hand-computed < 1e-9). ROT box enforces marginal-stability bound. c_u from
T8 VDM identification. Variable surge optimization deferred to P1b-1b."
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
- Consumes: `build_base_ocp_doubleint`(T6,Path B)+ T8 `c_u` + T7 per-target idxsh + P1b-0 prefix/bound-schedule/J_colreg 配置
- Produces: 6 点共存可行结论 → P1b-1b 生产 backend 可写

**验证点**: **双积分器 dynamics**(T6,Path B)+ prefix equality(T1)+ J_colreg EXTERNAL(T2)+ per-target ξ(T7)+ bound schedule(T4)全共存一个 acatos OCP。

- [ ] **Step 1: 写 gen_merge6.py — 合并 6 点**

基于 `build_base_ocp_doubleint`(T6,Path B),叠加:
- T1 prefix equality(第二 h-row `pact_pre*(ψ-ppsi_pre)`,per-stage activation)
- T2 J_colreg EXTERNAL per-stage cost(cost_scaling=ones)
- T7 per-target ξ(CPA 多行 + idxsh=[0,1] + 混合 L1/L2)
- T4 bound schedule(CPA row activation `cpa_act*g_cpa`)
合并 `con_h_expr = vertcat(cpa_act*vertcat(g_cpaA,g_cpaB), pact_pre*(ψ-ppsi_pre))`(nh=3:2 CPA + 1 prefix)。N=10, 2 target, K_prefix=3, cpa_hard_from_k=3, **c_u from T8**(Path B,非 Nomoto T/K)。生成 C。

- [ ] **Step 2: 写 runner_merge6.cpp — 6 点共存断言**

per-stage params(**c_u/u_surge 全局**(Path B)+ prefix/cpa_act/J_colreg/target per-stage)+ warm-start。solve。断言全部 6 点:
1. 双积分器 dynamics forward-match(同 T6)
2. prefix k<3 equality(T1)
3. J_colreg EXTERNAL cost 合理(T2)
4. per-target ξ exact-penalty(T7)
5. bound schedule cpa k<3 soft / k≥3 hard(T4)
6. 求解收敛或 status 4(F5,traj_delta>1e-6)
打印 `MERGE6 PASS: 6 points coexist (double-integrator+prefix+J_colreg+xi+bounds), staging scalable -> P1b-1b 生产 backend 可写`。

- [ ] **Step 3: 写 run_merge6.sh + run_all_p1b1.sh + 容器内跑**

run_all_p1b1.sh 顺序 T8→T6→T7→T9(过一加下一)。Run: `... acados_staging && bash run_all_p1b1.sh`。Expected: `ALL PASS P1b-1a: 6 points staging scalable`。

- [ ] **Step 4: 若合并失败,定位交互**

逐点回退(去 T4 → T7 → T2 → T1 → T6)定位哪个组合触发。常见交互:per-target ξ + bound schedule 在 CPA 行(T5 已验单 σ+activation 正交,per-target 同理应正交);**双积分器 dynamics + CPA**(变速下 CPA disc 进入时机变;且临界稳定 r 靠 ROT box 有界,与 CPA 约束交互需注意)。记录交互。

- [ ] **Step 5: Commit**

```bash
git add test/external/acados_staging/T9_merge6/ test/external/acados_staging/run_all_p1b1.sh
git commit -m "feat(m5): acados staging T9 6-point merged coexistence (P1b-1a)

Verify all 6 points (double-integrator dynamics Path B + prefix + J_colreg +
per-target xi + bound schedule) coexist in one acatos OCP. Gate for P1b-1b
production backend."
```

---

## Task 10: P1b-1a 验收门 + handoff

**Files:**
- (无新文件;跑 run_all_p1b1 + 验收 + handoff)

- [ ] **Step 1: 跑 run_all_p1b1(T8→T6→T7→T9)**
- [ ] **Step 2: 回归 P1b-0 + P1a smoke/subset 无破坏**
- [ ] **Step 3: 验收门核对**:T6 双积分器 dynamics 可扩(Path B)/ T7 per-target ξ 可扩 / T8 c(u) 辨识 + 积分器 model-class 诊断完成(Path B)/ T9 6 点合并 / P1b-0+P1a 无回归。
- [ ] **Step 4: 若 P1b-1a 失败,写失败报告**(阻塞点 + 回炉建议)。
- [ ] **Step 5: 更新 handoff/workspace_log.md**(P1b-1a 结果 + 6 点推荐配置 + 是否进 P1b-1b)。
- [ ] **Step 6: Commit handoff**

---

> **P1b-1b 生产 backend + P1b-1c benchmark 的 task 在 P1b-1a 通过后细化。** P1b-1a 是 staging 信心门;过了才写生产 backend(改动生产代码,风险高,需 P1b-1a 的 6 点配置全部锁定)。下方给出 P1b-1b/c 的 task 骨架(spec 级,执行前 P1b-1a 过了再补全 step)。

---

## Task 11 (P1b-1b 骨架): 生产 MidMpcAcadosFormulation

**Files:** `include/mid_mpc/mid_mpc_acados_formulation.{hpp,cpp}`
- 构建 acatos OCP 符号图:**双积分器 dynamics(Path B,T6)**`ṙ=c(u)·δ, ψ̇=r` + 6 cost(colreg/dist/route/vel/asym/terminal)+ 全约束(CPA/direction/min_alt/ROT/terminal + bound schedule)+ 142 参数 per-stage 分区。复用 P1b-0 staging 发现(参数激活 / cost_scaling=ones / update_params API)。
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
- ✅ **双积分器 dynamics(Path B,2026-07-16 修订替代原 Nomoto)** → Task 6(+T8 c(u) 辨识);原 Nomoto 在 T8 首试撞墙(VDM 无偏航阻尼),用户裁决 Path B,详见 spec §"关键设计变更(Path B)"
- ✅ per-target ξ 混合 L1/L2(VR-03/TBD-6)→ Task 7
- ✅ VDM zigzag 辨识 → Task 8(产 c(u) + 积分器 model-class 诊断,Path B)
- ✅ 6 点合并 → Task 9
- ✅ 生产 backend + M5_USE_ACADOS flag → Task 11/12
- ✅ Rule14 HO benchmark(轨迹级行为等价)→ Task 13
- ✅ 失败处置 → 各 task 失败分支 + Task 10/14 + **T8 首试 IDENT FAIL → Path B 已落地**(真实阻塞回炉案例)
- ✅ F1-F5 + staging 发现沿用 → Global Constraints
- ✅ 诚实标注(双积分器 honest physics / VDM 直读 c(u) 非造系数 / benchmark 非 bit-close)→ spec + 各 task

**2. Placeholder 扫描**: P1b-1b/c(Task 11-14)标"骨架,执行前补全 step" —— 这是**有意为之的 staging 门设计**(spec 明确:P1b-1a 过了才写生产 backend,因 P1b-1a 的 6 点配置是 P1b-1b 的输入)。非 plan 缺陷,是风险控制。P1b-1a 的 Task 6-10 全部有完整 step + 代码骨架 + 断言 + 容器命令。

**3. 类型一致**: **c_u(Path B)全 task 一致**(从 T8 产出,替代 Nomoto T/K);state x=[px,py,ψ,r](4)一致;per-target ξ idxsh=[0,1](2 target)一致;混合 L1/L2 ρ·ξ+½w·ξ² 一致。

**4. 风险**: T8 c(u) 辨识(VDM 可能非纯 const c(u) 有未建模速度/耦合)、**T6 双积分器临界稳定(极点 z=1,靠 ROT box 保 r 有界,SQP 大 rot_max 下收敛需 stress-test)**、T7 高维 ξ(ρ 调参)、T9 合并交互(双积分器 + CPA)。每 task 失败有处置,不强行绕过。
