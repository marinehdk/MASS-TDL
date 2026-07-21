# 新对话核心提示词:M5 MPC 7 层修复任务(L0-L5+LX)继续

> **使用说明**:复制下方 ``` 代码块内的全部内容,粘贴到新对话第一条消息。提示词自包含,新对话不需要额外上下文。

```
你是 MASS-L3 M5 Mid-MPC 修复任务的 TDL Lead 主 agent。继续上一会话的 7 层架构(L0-L5+LX)逐层修复工作。

## 工作目录(权威)
`/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`(worktree)
分支:`codex/m5-design-grounding`
HEAD:`ccd9acd1f`

## 必读(按顺序,建立完整 context)
1. `docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md` —— **7 层架构主文档**,§12 实施总表(进度追踪)+ §10 GATE 表 + §12.3 bug 待确认表。这是权威索引。
2. `docs/superpowers/design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md` —— 决策树状态(§0 注册表 DP/BL/EV/VR/ALT/TS + Step2 + Step5 完整记录)
3. `docs/superpowers/handoff/2026-07-20-m5-acatos-cpa-hard-floor-handoff.md` —— 上次会话 handoff
4. `handoff/workspace_log.md`(末尾 [2026-07-20] 条目)—— 详细改动 + pitfalls
5. AGENTS.md(项目规则 + subagent routing + COLREGs 全链路调试规则 + worktree 纪律)

## 已完成(✅)
- **L0 上游输入层**(commit `6a0c12f3b`):6 输入 guard + InputDegradation bitmask 追溯 + 16 unit tests。GATE 6/6 通过。
- **L1 Step2 grilling**:两份独立评审(ZCode agent_1436144e + Codex 同步跑)对照 14 项议题,暴露 9 盲区(BL-07..15)+ 7 场景(SC-07..13)。
- **Step5 DESIGN-IT-TWICE nh 抉择**(BL-09 闭环):方案 B(nh=20+nsh=0+J_colreg cost barrier 表达 soft 2500)采纳;方案 A(nh=36 双 row)弃用(无生产先例 + double-expression 权重协调 known-hard)。VR-01 final。
- **L1a-spec-freeze 批次 1**(commit `fe251260b`):DP-01 CPA hard floor true-hard 化。
  - 新增 kGIdxCpaHard=154 slot(26+128=154 追加)+ kAcadosNpGlobal 154→155
  - CPA residual 从 cpa_safe² 改 cpa_hard²(both .cpp MX + .py SX 同构)
  - **删 idxsh/Zl/zl/Zu/zu**(nsh=0 天然排除 idxsh,true hard)
  - kAcadosNt(16,layout) 与 kAcadosNsh(0,slack) 解耦
  - L4 telemetry 补救:soft_aspiration_d_min_m + violation_m(因 nsh=0 失去 slack telemetry)
  - 两份独立审查通过(code review PASS_WITH_MINOR_FINDINGS,M1/M2 已修;GNC review PASS_WITH_FINDINGS,C2 docstring 已补)

## ⚠️ 关键阻塞:批次 1 源码在 codegen re-run 前不改变 runtime 行为
stale `c_generated_code/acados_solver_m5_mid_mpc_acados.h`(NSH=16/NP=210)与本批次源码(期望 NSH=0/NP=211)**完全不兼容**。必须先在容器内跑 codegen 重生成,才能让:
1. true-hard CPA floor 生效(nsh=0)
2. FB-2 telemetry 生效(d_min/violation_m populate)
3. acatos-ON build 不出现 NP mismatch 内存 overflow

## 立即要做的下一步(优先级排序)

### 1. 容器内 codegen re-run + colcon test(阻塞批次 1 验收)
```bash
# 进 sil_nodes 构建容器(casadi + acatos C 库在 /usr/local)
# 找构建容器:docker ps | grep sil-nodes 或查 sil_nodes.Dockerfile 构建流程
# 在容器内:
cd /home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding/src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend
python3 gen_mid_mpc_acados.py  # 重生成 c_generated_code(NSH=0/NP=211)

# colcon build(触发 codegen + 编译)
cd /home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding
source /opt/ros/humble/setup.bash
colcon build --packages-select m5_tactical_planner

# 跑 unit tests
colcon test --packages-select m5_tactical_planner --event-handlers console_direct+
# 关键测试:test_l1a_cpa_hard_floor(T-B2 + T-B6)、test_mid_mpc_acados_formulation(np_global=155)
```
**验证 T-B5 dimension hash**:generated header 应该 `NSH=0/NS=0/NP_GLOBAL 更新`,与源码 static_assert 一致。

### 2. 三 case SIL 回归(SC-01/02/03)验证 ACADOS_FAILURE → Converged
```bash
# 用根因诊断的 runs/m5_solver_diag/ 三 case fixture:
# - target2500_exact(SC-01)
# - rule14_ho_5000_ab_canonical(SC-02)
# - rule14_ho_live_dispatch_749728000002(SC-03,live heading box 23.2°..53.2°,x₀≈0°)
# 期望:从 ACADOS_FAILURE 转 Converged(REFERENCE_FEASIBLE 对齐)
```

### 3. 补 T4 friend-test(FB-2 d_min/violation_m unit test,延后项)
通过 `debug_constraints_satisfied_after_solve` friend hook,构造已知 trajectory + target fixture,assert d_min 和 violation_m 的精确值。

### 4. T-B1/T-B3 ample-time 实测(方案 B 核心风险)
- T-B1(SC-08 距离扫点 1851.9/1852/2000/2500/2500.1m):验证 J_colreg barrier 是否驱动 solver 主动保持 d≥2500
- T-B3(Rule 8/16 ample-time 时机):验证 solver 解出的轨迹是 early/substantial 避让
- 若实测 solver 在 d≈2000 停留(勉强合规),调 ζ(steepness,formulation.cpp:88 kZeta=5e-3)或 w_colreg
- **条件性升级条款**:若 T-B1/T-B3 证明 barrier 不足且调参无效,方案 B 升级到方案 A(恢复 nsh>0 + dual row),此时 slack telemetry 成为必需

### 5. L1a-spec-freeze 批次 2(批次 1 GATE 通过后)
- **DP-02 box live 落地**:wrapper 每 solve 每 stage 重发 lbx/ubx(消费 live heading/speed);stage0 保持 x0 equality 不被 box 覆盖
- **DP-02 ROT 来源修正**:ROT 来自 GNC ODD(`mid_mpc_node.cpp:902-916`)而非 M4
- **DP-02 terminal contract**(BL-10):显式定义 NBXN=0 是"terminal 不在安全 claim"还是"纳入 NBXN"
- **DP-02 heading/ROT schedule 分离**:heading soften,ROT 全 stage hard
- **DP-08 grid physical-time map 原则**(BL-13):所有 schedule 先按物理秒定义再映射各自 backend grid,禁裸 k parity(IPOPT/acados off-by-one)

### 6. L1b(依赖 Q4 + Step5 + L1a 全 GATE)
- DP-08 k_head 公式 + ample-time 下界 `t_latest_safe`(BL-12,挪 Step3/Step5 调研)
- DP-07 CPA suffix-hard schedule(DP-01×DP-08 耦合解耦)
- DP-03 min-alt reachability b'(VR-03,保守因子 + oracle cross-check)
- DP-04 prefix CPA NO_SAFE_PLAN+M7(VR-04,路径 A+D1)
- Q4 σ conditional(IPOPT,BL-15/BUG-L1-01 扩大)
- direction row reachability schedule(BL-06)

### 7. C2/C3/C4 后续
- C2(L3):F-05 EXACT Hessian + R=0 + no-reg 数值
- C3(L4):F-01 status fail-closed(raw 0..7 映射)+ 4.1 h_fn rebuild
- C4(L5):5.3 BC→L4 链闭合 + 5.4 FinalDegrade→MRM + 5.4.b Last-Safe-Maneuver

### 8. LX 横向诊断(贯穿)
- X3 prefix pact_pre activation trace 补全
- X4 Failure Classifier 自动化
- BL-11 continuous/swept CPA(SC-07)调研

## 关键 pitfalls(必读,避免重蹈覆辙)
1. **worktree 纪律**:启动 subagent 前必须 `git status` 确认 clean 或显式声明 dirty 范围。上一会话因脏 worktree 导致批次 1 与遗留 gate-2 v3.1 dispatch redesign 混合,已剥离 9 个非批次1文件。
2. **构建环境分离**:本机 host 没有 acatos/casadi Python 模块,只在 sil_nodes **构建容器**(不是运行容器)里有。host 只能做符号层验证(gen.py 语法 + slot 算术 + grep);codegen+colcon 必须进容器。
3. **subagent 沿用遗留依赖风险**:subagent 在脏 worktree 工作时会沿用遗留 include(如 diagnostic_capture.hpp),剥离时必须检查新增 include。
4. **NLM retrieval 不可信**(VR-09):Codex 验证 maritime_regulations/colav_algorithms NLM 查询未返回 payload。涉及 COLREGs ample-time / safe-distance 证据改用 IMO/MAIB 一手源。
5. **mempalace CLI 被 MCP daemon 锁**:mine 写入失败时,退化为本地文档(docs/superpowers/handoff/)+ workspace_log。search 只读可用。

## subagent routing 规则(AGENTS.md)
- **实施任务**派 `tdl-mechanical-implementer` 或 `tdl-m5-planner-engineer`(workspace-write,scoped,isolated worktree)。本会话教训:启动前 `git status` 确认 clean。
- **独立审查**:M5→L4 executability 用 `tdl_gnc_contract_reviewer`;代码质量用 `tdl_code_reviewer`;安全/MRM/ToR 用 `tdl_m7_safety_reviewer`;DDS/plugin/adapter 用 `tdl_cyber_reviewer`;SIL/scenario 用 `tdl_sil_vv_engineer`。
- **并行评审**:可以同时给 ZCode subagent + Codex(用户同步跑)发同源同标准的评审任务,两份回来做分歧对照(本会话验证有效)。
- **primary agent 是唯一 TDL Lead**:stage classification、routing、最终裁决不委托。

## 关键文件路径(绝对路径)
- 7 层架构主文档:`docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md`
- 决策树日志:`docs/superpowers/design-logs/2026-07-20-m5-acados-c1-semantic-ocp-design-log.md`
- 根因报告:`docs/superpowers/review/2026-07-20-m5-acados-root-cause-diagnosis.md`
- acatos formulation:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_formulation.cpp`
- acatos codegen:`src/l3_tdl_kernel/m5_tactical_planner/test/external/acados_backend/gen_mid_mpc_acados.py`
- acatos solver wrapper:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`
- IPOPT row registry:`src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`
- IPOPT solver:`src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`
- 三 case fixture:`runs/m5_solver_diag/4fd37fd7e9fc435656e2154d92b859920a0eb646/`

## 第一步行动
请先读必读 1-2(7 层架构主文档 + 决策树日志),确认进度状态,然后按"立即要做的下一步"第 1 项(容器内 codegen re-run + colcon test)开始。如果容器环境不可用,先报告阻塞,再决定是否跳到批次 2 源码实施(可并行)或派 subagent 调研 BL-11/BL-12 等。

严格遵守 AGENTS.md 的:逐层修复顺序、bug 记录机制(§12.3)、COLREGs 全链路调试规则、subagent routing、worktree 纪律。
```
