# M5 Mid-MPC / acados 根因诊断接续提示词

请对 M5 Mid-MPC 当前“带目标场景 acados 无法稳定求解”问题执行完整、证据驱动的系统诊断；目标是先让 production acados 在正常、物理可行的中远距离 COLREGs 场景稳定运行，再用可达性证据定义 Mid-MPC→BC-MPC 紧急接管边界。

## 工作位置

- Worktree：`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`
- Branch：`codex/m5-design-grounding`
- Baseline HEAD：`c46e01045e1ad443cdadb835e62d822fc6b738a7`
- 当前 worktree 已有大量未提交修改。必须先记录 `git status --short` 和完整 diff 范围；禁止 reset、checkout覆盖、stash、清理或改写现有修改。
- 这是显式 handoff：新对话接管该 worktree 前先确认没有其他活跃线程仍在编辑。

## 必读文件

按顺序读取：

1. `AGENTS.md`
2. `docs/superpowers/plans/2026-07-20-m5-acados-mid-mpc-root-cause-diagnostic-plan.md`
3. `docs/superpowers/review/2026-07-19-mpc-p0-p7-comprehensive-review.md`
4. `docs/superpowers/specs/2026-07-16-m5-mpc-colav-solution-pack.md`
5. `docs/superpowers/design-logs/2026-07-16-m5-mpc-colav-design-log.md`
6. `docs/superpowers/specs/2026-07-17-m5-mpc-p0-p7-roadmap.md`
7. `docs/superpowers/specs/2026-07-18-m5-mpc-p0-p7-implementation-report.md`
8. `docs/superpowers/specs/2026-07-19-ho-red-execution-chain-diagnosis.md`
9. `docs/superpowers/specs/2026-07-19-m5-acatos-ho-dynamic-convergence-probe.md`
10. `src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py`
11. `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_formulation.cpp`
12. `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`
13. `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`
14. `src/l3_tdl_kernel/m5_tactical_planner/test/unit/test_mid_mpc_acados_solver.cpp`

使用 `codegraph_explore`/CodeGraph CLI 先定位 symbol 和调用链，再读具体源码。当前 worktree 的 CodeGraph index 必须指向本 worktree且up-to-date。

## 已确认事实

1. 当前 codegen与production维度已一致：target stride=8、`np_global=154`、`np_stage=56`、generated total NP=210。旧容器曾有146/210 stale-install mismatch，但不是当前源码唯一问题。
2. 当前配置是 `FULL_CONDENSING_HPIPM + EXACT Hessian + SQP + MERIT_BACKTRACKING + NO_REGULARIZE + tol=1e-9`。
3. 真实2500m target实验返回raw acados status=4（QP failure）；完整 `test_mid_mpc_acados_solver` 600s timeout。
4. `AmpleTime_FarTargetMustConverge` 实际使用no-target straight-line input，不能证明target场景收敛。
5. `PerTargetBreakdown_OneTargetSlackPositive` 非收敛时仍 `SUCCEED()`，不能作acceptance。
6. Rule13/14/15 solver fixture主要使用IPOPT N=8/dt=5，不是production acados N=80/dt=15。
7. 当前wrapper raw status映射疑似与官方acados不一致：必须从实际链接版本header/runtime验证，不允许继续硬编码猜测。
8. HO的可能机制：直线seed在horizon内穿过target，使 stage CPA constraint的control-space Jacobian退化；P7 collision cost在碰撞中心可能带负Hessian；EXACT+无正则HPIPM可能第一QP失败。此结论目前仍是假设，必须用QP dump、derivative和eigenvalue证据证伪/确认。
9. “一个相对坐标分量为0就Jacobian奇异”是错误说法；需要检查二者同时接近0，以及经动力学condense后的control-space Jacobian。
10. `np_stage`参数数量增加不会直接扩大QP决策变量维度；不能把56本身当作conditioning根因，除非有derivative/QP证据。

## 用户明确要求

- 正常、物理可行、中远距离Rule14 HO必须是Mid-MPC验收场景；不得将所有aligned HO排除到BC-MPC。
- BC-MPC只承担紧急/短裕量避碰；边界由独立可达性和last-safe-maneuver证据定义，不由单一CPA/TCPA/alignment阈值定义。
- 不调CPA floor，不改scenario geometry，不加scenario-id/vessel-specific分支，不用mock/skip/forced PASS。
- 任何FAIL如实保留并定位；不为让测试变绿而降低标准。
- 初始阶段只允许诊断、test harness、可观测性和离线variant；在根因证据形成前，不得修改production behavior/formulation/config。

## 首轮任务：只完成Phase 0–2

### Phase 0：可信诊断

1. 冻结一个真实Rule14 HO `MidMpcInput`和一个2500m target input，保存完整JSON及hash。
2. 验证实际acados raw status enum；新增/执行status mapping test。
3. 验证codegen/runtime dims和artifact hash。
4. 取得：raw NLP/QP status、qpscaling status、SQP/QP iter、alpha、NLP/QP residual、first failed stage/target、seed/solution constraint residual、slack、trajectory delta。
5. 打开acados QP diagnostics/iterate storage；必要时生成debug-only solver，不修改production默认配置。

### Phase 1：数学与导数

1. 对CPA constraint执行value/Jacobian/Hessian finite-difference parity。
2. 计算每stage state-space Jacobian和condensed control-space Jacobian。
3. 对collision cost/P7 sigma-point cost计算exact Hessian最小特征值和负特征值数。
4. 完成MX/SX二阶parity。
5. 输出KKT/scaling数量级审计。

### Phase 2：冻结输入单变量消融

至少执行：

- straight seed vs COLREG-starboard dynamics-feasible seed；
- baseline vs Gershgorin-LM regularization；
- full vs partial condensing；
- HPIPM BALANCE vs ROBUST；
- square-root vs classical Riccati；
- zero slack vs residual-based slack initialization；
- m² constraint vs dimensionless diagnostic variant；
- P7 sigma/cost ON vs deterministic diagnostic variant；
- SQP vs SQP_WITH_FEASIBLE_QP + funnel（若当前acados版本支持）。

一次只改一个变量；每项cold至少5次、warm连续20次。禁止一次把多个“可能修复”叠在一起后宣称根因。

## 必须建立的分类

每个case必须由独立reference oracle和acados结果分为：

```text
REFERENCE_FEASIBLE + ACADOS_SUCCESS
REFERENCE_FEASIBLE + ACADOS_FAILURE
REFERENCE_INFEASIBLE + ACADOS_REJECT
REFERENCE_INFEASIBLE + ACADOS_SUCCESS
```

独立reference可使用动力学control-lattice/reachability或多初值高精度离线求解；不得与acados共享同一失败假设。

## 完成判据

首轮诊断完成必须同时满足：

1. raw status语义可信；
2. 可指出第一个失败QP/stage/target；
3. 有residual和Hessian/Jacobian证据；
4. 至少一个假设被单变量实验确认，至少一个被推翻；
5. 不把物理不可行误写成solver failure；
6. 不把solver failure误写成BC territory；
7. 形成候选配置排序，但不修改production默认配置。

## 报告格式

输出一份诊断报告，建议路径：

`docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md`

结构：

```markdown
# M5 acados 根因诊断报告
## 1. Baseline与工作树快照
## 2. Raw status与codegen/runtime一致性
## 3. 冻结输入与reference feasibility
## 4. Constraint/Cost导数与Hessian证据
## 5. QP/KKT失败点
## 6. 单变量消融矩阵
## 7. 已确认根因 / 已推翻假设 / OPEN
## 8. 候选solver配置排序
## 9. Formulation/seed/scaling建议
## 10. Mid能力边界与BC边界尚缺证据
## 11. 下一决策门
```

每个finding包含：severity、file:line、输入case ID、raw status、直接证据、根因置信度、修复候选、证伪条件。

## 决策门

完成Phase 0–2后停止，向用户提交：

- 根因诊断报告；
- 所有命令和evidence paths；
- 最小production修复候选（不超过3个）；
- 每个候选的风险、预期收益、验证矩阵；
- 推荐决策ID：`ACADOS-FIX-01`。

没有用户明确回复“同意 ACADOS-FIX-01”前，不实施production solver configuration、formulation、dispatch或Mid/BC边界变更。

## 角色与纪律

按仓库`AGENTS.md`：

- Primary agent负责阶段分类、证据综合和最终判断。
- 诊断先走systematic debugging和SIL first-divergence。
- M5 production write owner必须唯一。
- M5→L4 executability需要独立`tdl_gnc_contract_reviewer`。
- safety/handover需要`tdl_m7_safety_reviewer`。
- 非机械production diff需要`tdl_code_reviewer`。
- SIL reviewer只读，不修改production behavior。

开始前先输出mandatory task header：Affected modules/files/topics/ODD/COLREGs/M5-M7/tests/SIL/evidence。持续工作到Phase 0–2证据闭环；遇到unknown标OPEN，不猜测。

