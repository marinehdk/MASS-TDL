# M5 acados 根因诊断报告

> 状态：Phase 0–2 诊断闭环。production solver configuration、formulation、dispatch、Mid→BC 边界均未改。硬 CPA 避碰子问题已可四分类；完整 Mid→L4 GNC executability 仍为 OPEN。建议门：`ACADOS-FIX-01`。

## 1. Baseline与工作树快照

- 交接 baseline：`c46e01045e1ad443cdadb835e62d822fc6b738a7`；执行期只读 HEAD：`4fd37fd7e9fc435656e2154d92b859920a0eb646`。
- 写入前 tracked dirty：12 files，`+1382/-146`；diff SHA-256 `a158aabacfde094c96a4c6f9a0635c398c0ec16a78e30e2ac49bdc450a901f48`。
- 未 reset、checkout 覆盖、stash、clean。用户原修改全部保留。
- 本轮生产路径只加默认关闭 input capture；`M5_ACADOS_DIAG_CAPTURE_DIR` unset 时无写入、无 solver 状态改变。capture 遇 non-finite double时拒绝该次记录并返回错误，solver继续且行为不变；文件名带内容 hash，避免同 boundary/stamp 覆盖。
- 证据根：`runs/m5_solver_diag/4fd37fd7e9fc435656e2154d92b859920a0eb646/`；复现命令账本：`analysis/commands.md`。
- Live Rule14 输入来自真实 dispatcher solve boundary；该次在线 dispatcher 最终使用 IPOPT，不是在线 acados。本文 acados 结论来自该输入的 fresh offline production-config replay。严格 SIL G-ART manifest 仍 pending/`valid_data=false`，不把该 capture 冒充完整 strict scenario pass。

## 2. Raw status与codegen/runtime一致性

链接 acados：v0.4.4，commit `5c98c317416a9bb335a99d1bf7933a04712ea72b`。实际 enum：0 SUCCESS、1 NAN_DETECTED、2 MAXITER、3 MINSTEP、4 QP_FAILURE、5 READY、6 UNBOUNDED、7 TIMEOUT。当前 wrapper 对 1/2/7 映射错误；status-contract test 故意保持 RED。HPIPM raw QP status 3=`NAN_SOL`。

当前 artifact 一致：N=80、nx=5、nu=2、nh=20、nsh=16、flat `np=210`、generated `np_global=0`；应用逻辑切片 154+56=210。当前 `.so` CasADi sparsity、generated C、JSON、header 均为 210。旧容器 146/210 是已排除 stale-install 历史，不是当前源码根因。56 是参数数，不是 QP decision dimension。

selected QP backend=`FULL_CONDENSING_HPIPM`。该链接版本未向此 backend 暴露 qpscaling status，报告为 `UNAVAILABLE_IN_LINKED_VERSION`，不借用 OSQP 字段猜值。

证据：`production/status_contract/runtime_contract.json`；`fresh_production_config/*/codegen_manifest.json`。

## 3. 冻结输入与reference feasibility

| case | 来源与 SHA-256 | raw acados | 独立 reference 作用域判定 |
|---|---|---:|---|
| `target2500_exact` | prior-session command 重建，非 live；`693a6639...c162862` | 4 | `REFERENCE_FEASIBLE + ACADOS_FAILURE` |
| `rule14_ho_5000_ab_canonical` | committed A/B fixture；`e54b317b...c1c2c` | 4 | `REFERENCE_FEASIBLE + ACADOS_FAILURE` |
| `rule14_ho_live_dispatch_749728000002` | real dispatcher capture `002c6fb9...daf3d`；replay `a98e81f8...3d2c9` | 4 | `REFERENCE_FEASIBLE + ACADOS_FAILURE` |

Reference oracle 独立于 acados/CasADi/Mid surrogate/objective/Hessian/seed：SIL `ship_dynamics.MMGModel`、RK4、rudder slew 2°/s、dt 0.2→0.01 refinement、连续线段 CPA。

| case | dt=0.01 CPA / hard margin | 30°达到 | max ROT | speed range |
|---|---:|---:|---:|---:|
| target2500 | 2093.868 m / +241.868 m | 31.92 s | 1.315°/s | 4.592–5.144 m/s |
| Rule14 benchmark | 2091.950 m / +239.950 m | 49.95 s | 0.782°/s | 2.752–3.001 m/s |
| live Rule14 | 2143.792 m / +291.792 m | 40.79 s | 0.983°/s | 3.458–3.774 m/s |

Live witness 为 port-to-port，遵守 heading upper；2500 m soft boundary未达到，但 hard 1852 m 满足，soft 部分按 slack contract处理。由此只闭合“intended hard-CPA collision reachability”四分类。route return、disturbance/uncertainty、exact L4 execution仍未闭合，因此完整系统判定是 `REFERENCE_UNKNOWN + ACADOS_FAILURE`，`gnc_executability=OPEN`，不是物理不可行。

证据：`fresh_production_config/*/reference_oracle.json`、`reference_witness.json`、`reference_trajectory_dt001.csv`、`gnc_executability.json`、`verdict.json`。

## 4. Constraint/Cost导数与Hessian证据

检查了

\[
h_k=(x_k-x^t_k)^2+(y_k-y^t_k)^2-R_{safe}^2,
\qquad
\frac{\partial h_k}{\partial u_{0:k}}=
\frac{\partial h_k}{\partial x_k}\frac{\partial x_k}{\partial u_{0:k}}.
\]

| case | FD Jacobian error | FD Hessian error | MX/SX 二阶 | condensed H min eig / negative count | collision det/P7 min eig |
|---|---:|---:|---|---:|---:|
| target2500 | 1.86e-7 | 5.92e-6 | PASS | -2.662e11 / 23 | -4.847 / -1.482 |
| Rule14 benchmark | 3.23e-7 | 4.07e-5 | PASS | -2.011e11 / 6 | -0.626 / -0.571 |
| live Rule14 | 3.58e-7 | 1.08e-4 | PASS | -2.933e13 / 18 | -80.462 / -118.237 |

Baseline 80 stages `R=0`。EXACT external-cost Hessian不定、zero-R、NO_REGULARIZE 为已确认数值脆弱前提。不是 sole root：derived LM、PROJECT、GAUSS_NEWTON、collision-cost-off 单独使对应 Hessian PSD/正定后，三 case 仍 raw4/QP3。

证据：`fresh_production_config/*/derivative_diagnostics.json`；`analysis/qp_integrity/*.json`。

## 5. QP/KKT失败点

- 三个 fresh replay：raw NLP 4、QP history `[0,3]`、SQP iterations=1、line-search alpha `[0,0]`。live NLP residual `[2141.192744, 0, 6248682.693, 0]`。
- raw4发生后，三个 `solution.json` 都保留 seed：`trajectory_delta_inf=0`、`max_slack=0`。首个 seed/solution residual：target2500 stage9 target0 CPA `-99,279 m²`；benchmark stage1 min-alt `-0.261799 rad`；live stage1 min-alt `-0.523599 rad`。这些是seed/returned-solution residual，不冒充HPIPM internal failing stage。
- QP dump finite；box/soft shapes、indices、positive slack penalties一致。`get_last_qp` 不导出 general-row `d_mask`，因此对 disabled upper bound不作 order/inversion 判定。只保留可直接证明的结构事实。
- Rule14 stage1 min-alt row：positive lower、condensed control Jacobian norm=0；显式 Euler 下 `u0`只能改变 `r1`，不能改变 `psi1`。该 stage linearization不可控。移除 stage1 row仍 raw4，故它是 contract defect，不是共享 sole root。
- target2500 prefix scan：N1–2 raw2；N3–11 raw0；N12 raw4/QP3。N12 同时加入 path stage11并改变 terminal stage12，故定位为 confounded failure-inducing horizon，不是 HPIPM internal stage。相同 N12 删除 target0后变 raw2/MAXITER：target0 对 QP-NAN failure mode confirmed。
- live Rule14 prefix scan：N1 raw2；N2 raw4/QP3。相同 N2 删除 target仍 raw4：非 target rule/min-alt path已足以触发该短 horizon failure；不能宣称 target0 是 live 首因。
- magnitude ratio：target约3.19e14、benchmark约3.67e20、live约3.16e20。LM/PROJECT仍可能保留或放大极端尺度。qpscaling status不可观测。

证据：`fresh_production_config/*/qp_statistics.json`、`last_qp.json`；`analysis/prefix_localization/*/prefix_localization.json`；`analysis/qp_integrity/*.json`。

## 6. 单变量消融矩阵

14 个 required arm × target/benchmark/live；每格 5 fresh-capsule cold + 同 capsule 20 warm，全部计数完整。每个 supported arm三 case全部 raw4；最终证据根 866 个 JSON 文件通过解析。

| arm | 结论 |
|---|---|
| baseline | 稳定复现 raw4/QP3 |
| starboard dynamics-feasible seed | straight seed非 sole root |
| partial condensing | full condensing非 sole root |
| HPIPM ROBUST | BALANCE非 sole root |
| cond Riccati only / QP Riccati only | 任一 square-root choice非 sole root |
| residual slack initialization | zero slack非 sole root |
| dimensionless CPA | m² row非 sole root |
| deterministic collision | P7 sigma非 sole root |
| SQP_WITH_FEASIBLE_QP + funnel | globalization非 sole root |
| reachability-scheduled stage1 | stage1 min-alt非共享 sole root |
| derived fixed LM / PROJECT | Hessian indefiniteness单独修正不足 |
| GAUSS_NEWTON | EXACT choice非 sole root |

额外 `collision_cost_off`、`cpa_rows_relaxed` 也均 raw4。Named `GERSHGORIN_* regularize_method` 在 v0.4.4 不存在；实际支持的 fixed `levenberg_marquardt` 已单变量执行。`classical_riccati` 同时改两个变量，只作补充，不归因。预注册 `LM × dimensionless` interaction未完成，保持 OPEN，不影响单变量结论。

No-target control：5 cold均 raw2/MAXITER（400 SQP，median约69.7 s）；primer后20 warm均 raw0。目标项把失败模式变成首QP NAN，但无目标 cold-start 本身也不满足production latency/stability。

证据：`analysis/ablation_matrix.json`、`analysis/ablation_matrix.csv`；`phase2/<arm>/<case>/`。

## 7. 已确认根因 / 已推翻假设 / OPEN

### Finding F-01 — Critical：status/KKT acceptance fail-open

- severity：Critical
- file:line：`mid_mpc_acados_solver.cpp:93-110,1095-1120`
- input case ID：all；raw status：observed 4，mapping test 1/2/7 RED
- 直接证据：linked enum与wrapper不一致；raw4可仅凭trajectory moved+constraint recheck改写Converged，无stationarity/complementarity/KKT gate
- 根因置信度：High
- 修复候选：0..7 fail-closed映射；raw4禁止重映射成功
- 证伪条件：部署library enum不同，或证明现有recheck覆盖完整KKT/optimality

### Finding F-02 — Critical：acados未表达intended hard CPA floor

- severity：Critical
- file:line：`mid_mpc_node.cpp:650-664`；`mid_mpc_acados_formulation.cpp:605-611`；`gen_mid_mpc_acados.py:315-323,493-510`；`shared/constraint_compiler.cpp:242-267`
- input case ID：live Rule14；raw status：4
- 直接证据：capture含soft 2500/hard 1852；graph只打包 `G_CPA_SAFE`，CPA rows虽经`idxsh`软化但threshold仍为2500；intended hard 1852未被打包/执行。IPOPT reference compiler则使用`cpa_hard`
- 根因置信度：High
- 修复候选：独立表达hard floor；soft cost/soft constraint与hard safety语义分离
- 证伪条件：实际deployed graph存在独立hard slot并被runtime使用

### Finding F-03 — Critical：runtime bounds与earliest-stage contract未落实到graph

- severity：Critical
- file:line：`mid_mpc_acados_formulation.cpp:605-611`；`gen_mid_mpc_acados.py:178-183,477-486,308-335`
- input case ID：live Rule14；raw status：4
- 直接证据：live heading/speed/ROT被pack但generated state boxes仍静态 ±π、±0.2094、0..15；`earliest_min_alt_k`只在输入/capture存在，既未pack也无graph consumer，min-alt从stage1启用
- 根因置信度：High（contract parity defect）；其对QP3贡献为Medium
- 修复候选：明确runtime bound update；以独立reachability schedule启用min-alt
- 证伪条件：runtime trace证明每stage box已按capture值set，且graph按earliest stage禁用前置row

### Finding F-04 — High：stage1 min-alt不可控

- severity：High
- file:line：`gen_mid_mpc_acados.py:300-335`
- input case ID：benchmark/live Rule14；raw status：4/QP3
- 直接证据：stage1 row positive lower、condensed control gradient=0；MMG 30°最早40.79/49.95 s，不是15 s stage1
- 根因置信度：High（defect）；非 sole root
- 修复候选：reachability-derived temporal schedule
- 证伪条件：同一离散动力学/执行器约束证明`psi1`由`u0`可达所需角度

### Finding F-05 — High：indefinite EXACT + zero-R + no regularization

- severity：High
- file:line：`gen_mid_mpc_acados.py:350-414,450-465`
- input case ID：三 case；raw status：4/QP3
- 直接证据：负特征值、condensed H强不定、80 stages R=0
- 根因置信度：Medium；数值贡献 confirmed，sole root disproved
- 修复候选：独立正定control curvature，再审计regularization/scaling
- 证伪条件：同一dump在zero-R/no-reg下稳定解，或实际H为PD

### 已推翻

- 单个相对坐标为0即Jacobian奇异；`np_stage=56`扩大QP变量；P7 sigma、straight seed、FULL condensing、BALANCE、任一Riccati、zero slack、m² units、EXACT、stage1 min-alt任一为 sole root。

### OPEN

- PSD/finite后HPIPM仍NAN_SOL的最小相互作用：scale、soft/slack、condensing之间尚未隔离。
- benchmark full-N首失败target/stage未做prefix scan；FULL condensing本身不提供internal stage。
- qpscaling runtime status不可用。
- 完整 Mid→L4 execution、route return、扰动/不确定性。

## 8. 候选solver配置排序

无单变量solver option恢复成功；不宣称存在可直接上线配置。后续实验排序：

1. 最小正定control curvature；先独立于regularization。
2. 物理等价scaling重构，保存unscaled residual与slack反变换。
3. 在QP已可解后再比较partial/full、HPIPM mode、Riccati组合。

## 9. 约束缩放/正则化/seed/scaling建议

- 不调CPA floor、不改scenario geometry。
- 先修soft/hard CPA、runtime boxes、min-alt temporal semantics；否则numerical tuning求解错误可行域。
- control curvature、regularization、scaling、seed继续一次一个变量。seed可保留starboard候选，但seed alone已被推翻。
- 所有scaled row保留物理单位反变换、dual/slack等价、unscaled KKT/residual。

## 10. Mid能力边界与BC边界尚缺证据

4800–5000 m正常Rule14 hard-CPA子问题已有独立可达 witness，且acados失败；所以它是Mid solver failure，不是BC territory。不得把aligned HO整体排除。

BC边界仍缺 last-safe-maneuver envelope：扰动/不确定性、rudder/engine slew、L4 exact execution、route return、M7/MRM takeover latency。只能由独立reachability随时间收缩定义，不能用单一CPA/TCPA/alignment阈值。本轮不改边界。

独立 M7 reviewer 还确认 BC→L4/M7 执行链未闭合：BC只发布 `/l3/m5/reactive_override_cmd`（`bc_mpc_node.cpp:58-59`），GNC bridge只订阅 `/l3/m5/avoidance_plan`（`gnc_bridge_node.cpp:13-20,60-61`）；M7 的 override callback只更新M3 watchdog（`safety_supervisor_node.cpp:473-479`）。Mid FinalDegrade虽发布 `suggested_action="MRM"`（`mid_mpc_node.cpp:2092-2103`），M7 safety-concern分支当前只显式处理 `M3_route_stale_watchdog`（`safety_supervisor_node.cpp:507-516`）。因此现状不能证明BC命令到达L4，也不能证明FinalDegrade触发MRM；在这条执行链及takeover latency得到端到端证据前，更不能把Mid solver failure划为BC territory。

## 11. 下一决策门

推荐：`ACADOS-FIX-01`。候选不超过三项，执行顺序按安全阻断优先：

| 顺序 | 候选 | 风险 | 预期收益 | 验证矩阵 |
|---:|---|---|---|---|
| 1 | fail-closed status/KKT acceptance | availability下降、fallback增加 | 禁止错误成功/错误故障分类 | raw0..7；raw4 adversarial；dispatch/fallback/M7 |
| 2 | semantic parity：hard/safe CPA、runtime bounds、reachability schedule | codegen参数/可行域变化 | 求解正确且物理可达问题 | 三case 5/20；dimension/hash；MMG；KKT/slack |
| 3 | positive control curvature + 后续单变量regularization/scaling | 轨迹偏置、掩盖尺度bug | 改善HPIPM数值前提 | eigen/KKT/scaling；三case；GNC/L4回归 |

命令和证据路径见 `analysis/commands.md`。未收到用户明确“同意 ACADOS-FIX-01”前，不实施以上production修复，不改dispatch或Mid/BC边界。
