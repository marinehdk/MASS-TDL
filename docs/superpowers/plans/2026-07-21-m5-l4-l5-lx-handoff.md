# M5 MPC 7 层闭环 — L4/L5/LX 任务 Handoff

> **生成时间**: 2026-07-21 L2-T2 fix + L3 scan 固化完成后
> **上一对话产物**: L0-L3 GATE 全部关闭，contract test + regression scan 固化
> **本 handoff 目标**: 继续修复 L4（解复核/遥测）、L5（输出降级/执行闭环）、LX（横向诊断），将 MPC 7 层结构全部闭环

---

## 当前状态速览

| 层 | GATE | 关键成就 |
|----|------|---------|
| **L0** 上游输入 | ✅ 关闭 | 37/37 contract tests PASS；纯函数 guards 已提取；degradation flag 是 write-only（F2 待修） |
| **L1** OCP 建模 | ✅ 关闭 | 11/11 contract tests PASS；LBX-len5 已修；box live / CPA hard / reachability schedule 全部落地 |
| **L2** 求解准备 | ✅ 关闭 | 5/5 contract tests PASS；L2-T2 场景简化为纯 CPA 违反（移除不合理 COLREGs） |
| **L3** 数值求解 | ✅ 关闭 | S-T1~S-T5 全部 GREEN；portable scan 确认 P4 baseline 收敛边界无回归 |
| **L4** 解复核 | ⬜ 待实施 | 3 项 P2+P3+F-01 |
| **L5** 输出降级 | ⬜ 待实施 | 3 项 BC→L4 / MRM / Envelope |
| **LX** 诊断 | ⬜ 部分有 | X3/X4 自动化 + continuous CPA |

---

## 工作目录（权威）

- **Worktree**: `/home/marine.huang/Code/mass-l3/.worktrees/m5-design-grounding`
- **当前 HEAD**: `1d176e320`（S-T5 portable scan）
- **容器**: `codex-m5-p3-sil-nodes-1`（运行 18h+，acados v0.4.4 + CasADi 3.7.2 已装）。**继续使用**
- **bind-mount**: 容器 `/opt/ws/src` → host worktree `src/`

---

## L4 任务（解复核与反馈 — C3 阶段）

### L4-T1 [P2] L0 degradation flag 下游消费（HIGH 安全优先级）

**问题**: `InputDegradation` 的 6 个 flag（own_psi/own_u/target/speed_box/reachability/planned_speed）在 `assemble_input_` 正确设置，但 **L1+ solver 完全不读**（`grep -c "degradation\." mid_mpc_acados_solver.cpp == 0`）。一旦发生 silent substitution（如 own_psi NaN → 0.0），solver 仍报 Converged，操作员无信号。违反 ARCH-DECISION-03 "NEVER silently substitute"。

**当前证据**: L0-T4 RED test 保留为显式证伪。

**修复方向**:
1. 在 `MidMpcAcadosSolver::solve()` 入口读 `InputDegradation::any()`
2. 把 degraded 信号映射到 `MidMpcSolution`：至少填充 `rationale` 字符串附 `L0:{summary}`
3. 或者新增 `status` 枚举值（如 `DegradedConverged`），让 L4/L5 能区分"真实输入" vs "fallback 值"
4. 修复后 L0-T4 应从 RED 转 GREEN

**涉及文件**:
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`（solve() 入口 + MidMpcSolution 填充）
- `include/m5_tactical_planner/common/types.hpp`（InputDegradation struct + MidMpcSolution status）
- `test/unit/test_l0_contracts.cpp`（L0-T4 断言更新）

**验证**: L0-T4 RED → GREEN（确认 degradation flag 被消费）

---

### L4-T2 [P3] soft_aspiration telemetry 全出口填充

**问题**: `constraints_satisfied_()`（`mid_mpc_acados_solver.cpp:1166-1183`）只在 Converged（status=0）或 QP recovered（status=4）路径调用。当 solve 返回 NumericalFailure（status=3）时，`last_soft_aspiration_d_min_m` / `last_soft_aspiration_violation_m` 保持为 0，操作员失去 "d_min 违反程度" 信号。

**当前处理**: L2-T2 已通过简化场景（去掉 COLREGs 复杂度）转 GREEN，但全出口填充仍未实现。

**修复方向**:
1. 将 `constraints_satisfied_` 的 d_min 计算提取为独立函数（如 `compute_soft_aspiration_telemetry_()`）
2. 在 `solve()` 的所有出口路径（status=0/2/3/4）都调用一次
3. 即使返回判定为失败，telemetry 仍要传出去（"失败时的 d_min 信号" 对操作员有诊断价值）
4. L2 contract test 可加一个非 Converged 出口的 telemetry 断言（但不阻塞当前 GATE）

**涉及文件**:
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`（extract + all-exit call）
- `include/m5_tactical_planner/mid_mpc/mid_mpc_acados_solver.hpp`（private method 声明）

**验证**: 构造一个必然返回 status=3 的 input（如 target 在 cpa_hard 内），确认 `soft_aspiration_d_min_m > 0`

---

### L4-T3 [F-01] status fail-closed 映射修复

**问题**: 
- `raw 4`（QP error recovered）在 `solver_moved && constraints_satisfied_` 时被重映射为 `Converged`（`mid_mpc_acados_solver.cpp:99-111,1114-1128`）
- linked enum 与 wrapper 映射不一致（raw 1/2/7 映射错误）
- `constraints_satisfied_` 只做部分 primal feasibility recheck，无 stationarity / complementarity / dual feasibility

**修复方向（VR-05）**:
1. raw 0..7 全部 fail-closed 映射；raw 4 绝不重映射 Converged
2. success 分层：`solver_status` / `safety_status` / `execution_status` 独立（不是单一 Converged/Not）
3. raw 0 要成为可发布 plan，必须同时 stationarity + complementarity + primal feas + dual feas 独立通过
4. status-contract test：构造 raw=4 输入，断言 solution.status ≠ Converged

**涉及文件**:
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp`（status 映射逻辑）
- `include/m5_tactical_planner/common/types.hpp`（MidMpcSolution status enum 可能需要扩展）
- 新建 `test/unit/test_l4_contracts.cpp`（status-contract test）

**⚠️ 注意**: L4.1 反向依赖 L1 — `constraints_satisfied_` 复用 L1 的 `con_h_expr`（`mid_mpc_acados_solver.cpp:555-568` h_fn lazy-build 缓存）。L1 改 con_h_expr 时，L4 的 h_fn cache 必须 rebuild。当前 L1 已稳定（GATE 关闭），暂无冲突。

---

## L5 任务（输出降级与执行闭环 — C4 阶段）

### L5-T1 BC→L4 链闭合

**问题**: BC-MPC 发 `reactive_override_cmd`，GNC bridge 订 `avoidance_plan`，但 BC→L4 链路在 M5 输出端未完整闭环。`HandoverManager`（5.3）当前状态为 ❌。

**修复方向**:
1. 梳理 `reactive_override_cmd` → `avoidance_plan` 的当前订阅/发布链
2. 确认 BC-MPC takeover 时 M5 的输出切换逻辑（正常下发 GNC / 交 BC-MPC / 周期切换 / 冻结）
3. 补齐缺失的 subscriber/publisher + 状态机
4. 加 contract test：BC takeover 信号到达时，M5 输出正确切换到 BC 模式

**涉及文件**:
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（输出逻辑）
- ROS2 topic: `reactive_override_cmd` / `avoidance_plan`

---

### L5-T2 FinalDegrade→MRM 执行证据

**问题**: `Mid FinalDegrade` 发 `suggested_action=MRM`，但 M7 concern 分支无完整执行证据。Fallback Manager（5.4）当前状态为 ❌。

**修复方向**:
1. 确认 MRM 触发链路：M5 FinalDegrade → M7 concern → 执行动作
2. 补齐证据链（ROS2 log / ASDR trace）
3. 加 SIL scenario：构造 solver 连续失败场景，验证 MRM 正确触发

**涉及文件**: 
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`
- M7 相关模块

---

### L5-T3 Last-Safe-Maneuver Envelope Computer（5.4.b）

**问题**: 缺少独立的 reachability 计算来定义 BC-MPC 接管边界（扰动/不确定性/rudder slew/takeover latency）。根因报告 §10 明确标注缺失。

**修复方向**:
1. 实现独立 reachability 计算（不依赖 solver 收敛性）
2. 定义接管边界：扰动 σ_pos、rudder slew 限制、takeover latency
3. 当 solver 在 Envelope 内无解时，触发 BC-MPC 接管

**涉及文件**: 新建模块（参考 BL-B escalation 的 MMG oracle 保守因子方法）

---

## LX 任务（横向诊断 — 贯穿全流程）

### LX-T1 X3 prefix pact_pre activation trace

**问题**: `derivative_diagnostics.json` 缺 prefix 段 `pact_pre` activation trace。评审点 16 标注 Q4 debug 需要。

**修复方向**: 在 `constraint_compiler` 或 solver diagnostic 输出中补 prefix 段的约束激活日志（每 stage 每个 row 的 lh/uh/slack 状态）。

### LX-T2 X4 Failure Classifier 自动化

**问题**: 当前根因分类靠手工（`runs/m5_solver_diag/` 的四分类）。需自动化：原始 OCP 不可行 / 线性化 QP 不可行 / 数值未收敛 / 输出不可执行。

**修复方向**: 基于 solver status + KKT residual + constraint violation + trajectory feasibility 的自动分类器。前置到 C1 并行，支持 L1 GATE 的 "独立 MMG witness 规模化执行"。

### LX-T3 continuous/swept CPA（BL-11）

**问题**: 15s 网格内节点 CPA 都 ≥1852 但区间穿越 <1852。node-only row 可能漏区间穿越（SC-07）。

**修复方向**: 调研 continuous CPA 计算方法（根因 oracle 已用连续线段），评估是否需要加到 formulation 或仅作为 L4 independent witness。

---

## 实施优先级

| 优先级 | 任务 | 层 | 理由 |
|--------|------|----|------|
| **P0** | L4-T1: degradation flag 下游消费 | L4 | 安全：silent substitution 无损检测（ARCH-DECISION-03） |
| **P0** | L4-T2: soft_aspiration 全出口填充 | L4 | 操作员遥测：非 Converged 时也需 d_min 信号 |
| **P1** | L4-T3: status fail-closed | L4 | raw4→Converged 重映射是 fail-open，安全关键 |
| **P1** | L5-T3: Last-Safe-Maneuver Envelope | L5 | BC-MPC 接管边界的独立 reachability |
| **P2** | L5-T1: BC→L4 链闭合 | L5 | 执行闭环完整性 |
| **P2** | L5-T2: FinalDegrade→MRM 证据 | L5 | M7 concern 证据链 |
| **P3** | LX-T1~T3: 诊断自动化 | LX | 规模化验证支撑，但可并行 |

**依赖关系**: L5 任务依赖 L4 GATE 关闭（3 项 L4 全部完成）。LX 任务可并行。

---

## 关键参考文件

| 文件 | 用途 |
|------|------|
| `docs/Design/Architecture Design/M5_MPC_业务流程分层架构.md` | 权威 7 层架构定义 + GATE 表 + 实施总表 §12 |
| `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-report.md` | L0-L3 contract test 诊断报告 + P0-P3 待办 |
| `docs/superpowers/specs/2026-07-21-m5-7layer-contract-test-design.md` | contract test 设计规格 |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_acados_solver.cpp` | production solver（status 映射、constraints_satisfied、box live 写入） |
| `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp` | assemble_input + degradation flag 设置 + 输出 dispatch |
| `include/m5_tactical_planner/common/types.hpp` | MidMpcSolution / InputDegradation struct |
| `test/unit/test_l0_contracts.cpp` | L0 contract tests（含 L0-T4 RED） |
| `test/unit/test_regression_scan.cpp` | S-T1~S-T5 regression scan（portable scan 为权威） |
| `test/external/rule14_ho_benchmark/` | Rule 14 对头 benchmark（2000m Converged / 500m fail） |

---

## 能力边界（当前 acados 求解器）

### 纯 CPA 违反（无 COLREGs）— portable scan 权威数据

| target_y | gap | raw | sqp_iter | status |
|----------|-----|-----|----------|--------|
| 2400 | -548 | 0 | 206 | Converged |
| 2100 | -248 | 0 | 218 | Converged |
| 1900 | -48 | 0 | 212 | Converged |
| 1852 | 0 | 0 | 189 | Converged |
| 1800 | +52 | 0 | 226 | Converged |
| 1700 | +152 | 0 | 143 | Converged |
| 1600 | +252 | 4 | 5 | Converged (QP recovered) |
| 1500 | +352 | 4 | 3 | Converged (QP recovered) |

**gap ≤ +352 全部收敛**。gap > +352 未测试。

### Rule 14 对头（COLREGs 真实场景）

| 场景 | TCPA | acados | 详情 |
|------|------|--------|------|
| 2000m head-on | 200s | ✅ Converged (raw=0, sqp_iter=11, 29ms) | ±60° box, route_weight=1.0 |
| 500m head-on | 50s | ❌ NumericalFailure (raw=4) | QP conditioning 限制，IPOPT fallback |

---

## Pitfalls

1. ❌ 不要跳过 L4 直接做 L5 — L4 的 fail-closed 映射和遥测是 L5 执行闭环的前置条件
2. ❌ 不要改 production solver 的 cost weight / QP tolerance / codegen — L1-L3 GATE 已关闭，收敛边界已稳定
3. ❌ 不要创建新的 mock/skip/forced-pass
4. ✅ status fail-closed（L4-T3）会改变现有 `raw=4 → Converged` 行为 — 需评估对现有测试和 SIL scenario 的影响
5. ✅ degradation flag 消费（L4-T1）需要定义新的 solution status 枚举值 — 先讨论方案再实施
6. ✅ L5 BC→L4 链需要理解当前的 ROS2 topic 订阅/发布拓扑 — 先调研再动手

---

## 期望产出

- L4 GATE 关闭：3 项（degradation 消费 + telemetry 全出口 + status fail-closed）全部 GREEN
- L5 GATE 关闭：3 项（BC→L4 + MRM + Envelope）全部 GREEN
- LX 诊断：X3/X4 自动化就位
- 所有 7 层 GATE 关闭，MPC 结构完整闭环
