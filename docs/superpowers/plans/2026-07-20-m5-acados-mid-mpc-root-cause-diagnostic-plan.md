# M5 Mid-MPC / acados 求解鲁棒性根因诊断计划

> 日期：2026-07-20  
> 工作树：`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`  
> 分支：`codex/m5-design-grounding`  
> 基线 HEAD：`c46e01045e1ad443cdadb835e62d822fc6b738a7`  
> 状态：PRE_SPEC_DISCOVERY / NOT_READY  
> 范围：诊断与验证计划；不构成 production formulation/config 变更批准

## 1. 决策与目标

正常、物理可行、处于中远距离的 Rule 14 head-on 必须属于 Mid-MPC 验收范围。不能因为当前 acados 解不了 aligned HO，就把全部 HO 路由给 BC-MPC。BC-MPC 边界应由剩余安全机动裕量和独立可达性证据决定，不由 `CPA<阈值`、`TCPA<阈值`、`aligned=true` 单点决定。

本计划回答：

1. 当前失败是 NaN、SQP max-iteration、minimum-step、QP failure、真实不可行，还是 wrapper 误分类？
2. HO 失败来自 constraint/control Jacobian 退化、Hessian 非正定、尺度、slack、初值、condensing，还是组合？
3. 哪些场景物理可行但 acados 失败？
4. Mid-MPC 与 BC-MPC 的真实能力/接管边界在哪里？
5. 哪套 formulation/configuration 能使 Mid-MPC 在正常 Rule13/14/15/17 场景稳定收敛？

完成标准：得到可复现输入、可证伪根因、能力图谱、Mid/BC 边界、候选求解配置和规则族回归证据。不得以单个 HO probe 变绿代替内部求解闭环。

## 2. 正常 Mid-MPC 场景定义

建议使用安全机动裕量：

\[
T_{margin}=t_{first\ safety\ violation}-t_{maneuver\ required}-t_{solve}-t_{L4}
\]

- `t_first_safety_violation`：nominal 轨迹首次进入安全域时间。
- `t_maneuver_required`：在当前速度、ROT、加速度和横向走廊内达到安全轨迹所需最短时间。
- `t_solve`：Mid 求解和发布预算。
- `t_L4`：L4 接收、验证、开始执行的延迟。

| 分类 | 定义 | 正确结果 |
|---|---|---|
| Mid-Normal | 独立可达性 oracle 判定可行，安全裕量充分 | Mid raw status=0，输出可执行计划 |
| Mid-Boundary | 可行但安全裕量接近保证边界 | Mid 仍可解；可准备 BC，不得出现 command gap |
| BC-Emergency | Mid 长时域计划来不及执行，但 BC 仍可行 | 明确、及时 handover |
| NO_SAFE_PLAN | Mid/BC 在 ODD 约束内均不可行 | 显式报告 M7，禁止伪造 PASS |

独立 reference oracle 至少包含动力学可达性/control-lattice 或多初值高精度离线求解；不能用 acados 自身失败反推“物理不可行”。

## 3. Phase 0：建立可信诊断基础

### T0.1 raw status 语义锁定

当前 wrapper 的 raw status 映射必须与实际链接 acados 版本的 header 对齐。每次输出同时保留：

- `raw_acados_status`
- `raw_qp_status`
- `internal_status`
- `qpscaling_status`

禁止只打印转换后 status。单测逐个锁定 raw enum → internal enum。

### T0.2 codegen/runtime 一致性

solver 启动必须断言：

```text
N=80
NX=5
NU=2
NP_GLOBAL=154
NP_STAGE=56
NP_TOTAL=210
NH=20
NSH=16
```

同时校验 formulation hash、codegen hash、generated library build identity。任何不一致应 fail-fast 为 `NotInitialized`。

### T0.3 每次 solve 的必需诊断

```text
case_id
raw_status / raw_qp_status / qpscaling_status
sqp_iter / qp_iter[] / alpha[]
res_stat / res_eq / res_ineq / res_comp
cost / solve_time
seed_type / cold_or_warm
min_h_seed / min_h_solution
min_state_constraint_jacobian_norm
min_condensed_control_jacobian_norm
hessian_min_eigenvalue
hessian_negative_eigenvalue_count
estimated_condition_number
max_slack_required_seed / max_slack_solution
trajectory_delta
active_constraint_rows
first_failed_stage / first_failed_target
```

必须检查控制空间梯度，而非只检查状态空间梯度：

\[
\frac{\partial h_k}{\partial u_{0:k}}
=
\frac{\partial h_k}{\partial x_k}
\frac{\partial x_k}{\partial u_{0:k}}
\]

### Phase 0 Gate

- raw status 无歧义；
- 所有失败有 QP residual；
- 可定位首个失败 stage/target；
- codegen/runtime mismatch 被 fail-fast；
- 不再根据错误 wrapper status 做因果判断。

## 4. Phase 1：数学、导数、尺度验证

### T1.1 CPA constraint 导数

对 `h=dx²+dy²-R²` 测：安全圆外、边界、圆内、近中心、中心、非对称点。比较 symbolic 与 finite-difference 的 value/Jacobian/Hessian。

必须证明：

- `dx=0,dy!=0` 不是奇异；
- 仅 `dx≈dy≈0` 时 state-space gradient 接近零；
- condensed control Jacobian 是否同时退化。

### T1.2 P7 collision cost Hessian

测试：

- `sigma=0/30/60/100 m`
- `intent_confidence=1/0.5/0`
- `d/R=0/0.1/0.5/1/2`

输出 Hessian 最小特征值、负特征值数量、cost/gradient finiteness。确认 exact Hessian 在碰撞中心是否非正定。

### T1.3 MX/SX 二阶 parity

同一输入逐 stage 比较：

- cost value/gradient/Hessian；
- constraint value/Jacobian/Hessian；
- drifted target position；
- sigma/intent/transition 参数。

### T1.4 scaling audit

记录 `x/y`、`psi`、`u`、CPA residual、slack、collision cost、KKT blocks 的数量级。若尺度跨度导致不合理 condition number，应直接 RED。

### Phase 1 Gate

- derivative parity 通过；
- 退化点和负曲率得到直接证据；
- scaling 问题量化；
- 尚未通过时，不进入 solver configuration 结论。

## 5. Phase 2：冻结输入根因消融

### Anchor cases

| ID | 场景 |
|---|---|
| A0 | no-target production-normal route |
| A1 | Rule14 远距 HO，DCPA=0，TCPA>horizon |
| A2 | Rule14 中距 HO，DCPA=0，TCPA≈0.75 horizon |
| A3 | Rule14 临界 HO，DCPA=0，TCPA≈0.25 horizon |
| A4 | Rule14 offset，DCPA=0.25R |
| A5 | Rule14 offset，DCPA=0.75R |
| A6 | Rule15 crossing，DCPA=0 |
| A7 | Rule15 crossing，DCPA=0.5R |
| A8 | Rule13 overtaking give-way |
| A9 | Rule17 stand-on，target 正常让路 |
| A10 | Rule17 emergency 17(b) |
| A11 | 独立 oracle 判定物理不可行 |

每个 case 保存完整 `MidMpcInput`，所有 variant 使用同一输入。

### 可证伪假设

| 假设 | 单变量实验 | 区分信号 |
|---|---|---|
| H1 直线 seed 退化 | straight vs COLREG-starboard feasible seed | 仅 seed 改变即恢复 |
| H2 exact Hessian 非正定 | baseline vs Gershgorin-LM | 负特征值被正则化后恢复 |
| H3 full condensing 病态 | full vs partial condensing | condensed condition/residual 改善 |
| H4 HPIPM factorization 要求 | BALANCE/ROBUST；square-root/classical Riccati | raw QP status 改变 |
| H5 slack 初始化失败 | zero slack vs residual-based feasible slack | first QP feasibility 恢复 |
| H6 constraint 尺度错误 | m² residual vs dimensionless residual | condition/slack/QP 恢复 |
| H7 P7 cost 导致负曲率 | P7 sigma/cost ON vs deterministic | Hessian/status 随 P7 切换 |
| H8 transition/warm-start 污染 | transition OFF/ON；correct shift/straight | 连续周期差异 |
| H9 first QP infeasible | SQP vs SQP_WITH_FEASIBLE_QP | feasibility direction 恢复 |
| H10 真实不可行 | independent reachability oracle | 正确分类为 BC/NO_SAFE_PLAN |

消融纪律：一次只改一个因素；不改 CPA floor；不改场景几何；不删除 target；非收敛不得 `SUCCEED()`；每个 variant cold 5 次、warm 连续 20 次。

### 当前测试需收紧

1. `PerTargetBreakdown_OneTargetSlackPositive` 非收敛仍 `SUCCEED()`，不能作 acceptance。
2. `AmpleTime_FarTargetMustConverge` 实际无 target。
3. Rule13/14/15 fixture 使用 IPOPT N=8/dt=5，不是 production acados N=80/dt=15。

## 6. Phase 3：solver configuration 筛选

Baseline：

```text
FULL_CONDENSING_HPIPM
EXACT
SQP
MERIT_BACKTRACKING
NO_REGULARIZE
tol=1e-9
```

第一轮只筛：

| Config | 变化 |
|---|---|
| C0 | baseline |
| C1 | `GERSHGORIN_LEVENBERG_MARQUARDT` |
| C2 | C1 + `hpipm_mode=ROBUST` |
| C3 | C2 + classical Riccati |
| C4 | partial condensing，`cond_N=10/20` |
| C5 | `SQP_WITH_FEASIBLE_QP` + funnel globalization |

不要直接切 `GAUSS_NEWTON`：当前 EXTERNAL cost 未重构为适合 GN 的 residual form。`SQP_RTI`也不作为首轮候选，因为当前失败发生在第一 QP。

### Config Gate

- A0–A10 中全部 reference-feasible cases raw status=0；
- A11 明确拒绝；
- 无 NaN；
- 不接受 status4 wrapper remap 作为正常收敛；
- cold/warm 确定性一致；
- target solve P99 <15 s；
- 仅最多两组候选进入 capability atlas。

## 7. Phase 4：Mid-MPC capability atlas

主要无量纲轴：

| 轴 | 取值 |
|---|---|
| `DCPA/Rsafe` | 0,0.1,0.25,0.5,0.75,1,1.25,2 |
| `TCPA/Horizon` | 0.05,0.1,0.25,0.5,0.75,1,1.25 |
| Rule14 bearing | -5,-2,0,+2,+5 deg |
| Rule15 bearing | 67.5,90,112.5 deg |
| speed ratio | 0.5,1,1.5 |
| own speed/ROT | ODD min/nominal/max |
| `sigma_pos` | 0/nominal/max |
| intent confidence | 1/0.5/0 |
| route XTE | center/half-corridor/edge |

先做 200–300 个 deterministic pairwise cases；对失败边界做局部 full-factor sweep。

每个 case 四分类：

```text
REFERENCE_FEASIBLE + ACADOS_SUCCESS
REFERENCE_FEASIBLE + ACADOS_FAILURE
REFERENCE_INFEASIBLE + ACADOS_REJECT
REFERENCE_INFEASIBLE + ACADOS_SUCCESS
```

确定性 Mid-Normal corpus 要求 100%：raw status0、residual pass、plan 非空、CPA/pass-side/Rule semantics 正确、独立 L4 executability pass、P99<15s、20周期无 backend oscillation。

## 8. Phase 5：连续 replan / warm-start

Rule13/14/15 anchor 各执行 20–40 周期：

- target 按真实 COG/SOG 更新；
- own ship 按可执行轨迹推进；
- replan=60s；dt=15s；物理 shift 应验证 `60/15=4 stages`；
- 注入 identity 稳定更新、confidence 下降、M6 phase 转换；
- past-and-clear 后 route return。

检查：动力学可行 seed、shift 索引、residual不累积、solver/backend不振荡、180s committed prefix、M6 source/run/stamp freshness、scenario reset 清理、ASDR可重建。

## 9. Phase 6：Mid/BC handover 边界

每个规则族逐步降低安全时间裕量：Mid comfortably feasible → marginal → execution margin insufficient → Mid infeasible/BC feasible → both infeasible。

验收：

- Mid-Normal 不得因 `aligned` 直接送 BC；
- Mid-Boundary 可准备 BC，但 Mid 仍输出有效计划；
- BC-Emergency 在 last-safe-maneuver 前接管；
- 无空 plan/零命令窗口；
- BC trajectory 通过独立 L4 检查；
- 均不可行时显式 `NO_SAFE_PLAN`→M7；
- Rule14 exact HO 必须覆盖 Mid-Normal、Boundary、BC-Emergency 三个区间。

## 10. Phase 7：COLREGs / SIL 闭环

固定顺序：

1. scenario truth + independent CPA/TCPA + run lineage；
2. G-ART GREEN；
3. M2/M6/M4/M5/L4/M7 per-module oracles；
4. Rule14 cohort；
5. Rule15 cohort；
6. Rule13 cohort；
7. Rule17 cohort；
8. Clean12。

先用 transit+avoidance short gate 判断 Mid 核心避碰；再跑 full lifecycle 判断 release/route-return。禁止混淆两者。

## 11. Evidence contract

```text
runs/m5_solver_diag/<commit>/<config>/<case_id>/
├── input.json
├── solver_config.json
├── codegen_manifest.json
├── seed_trajectory.json
├── iterate_000.json
├── qp_statistics.json
├── derivative_diagnostics.json
├── solution.json
├── trajectory.csv
├── gnc_executability.json
└── verdict.json
```

汇总：`capability_atlas.json`、`capability_heatmap.png`、`root_cause_ablation.json`、`mid_bc_boundary.json`、`regression_summary.json`。

## 12. 执行顺序与 readiness

| 阶段 | 停止条件 |
|---|---|
| Phase 0 | status/diagnostics可信 |
| Phase 1 | 导数、Hessian、尺度闭环 |
| Phase 2 | 首因能被单变量证伪 |
| Phase 3 | 留下最多2个候选配置 |
| Phase 4 | Mid能力图谱形成 |
| Phase 5 | 连续replan稳定 |
| Phase 6 | Mid/BC边界可证明 |
| Phase 7 | 规则族和Clean12闭环 |

任何 `raw NaN`、codegen mismatch、artifact lineage inconsistency、reference-feasible solver failure、reference-infeasible false success、L4不可执行，都阻塞后续通过声明。

当前 readiness：`NOT_READY`。最小下一工作是 Phase 0–2：取得冻结 HO 输入的完整 QP dump和单变量消融结果。

