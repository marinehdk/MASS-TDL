# M5 v3.1 上游修复方案 — 排查与移植 colregs-nlp-cpa-fix 缺失项

> **Status**: PLAN — 等待新对话执行
> **工作树**: `.worktrees/m5-design-grounding`（分支 `codex/m5-design-grounding` @ `c46e01045` + v3.1 acatos dispatch gate + T1-T15 + rot_reach_ratio fail-safe fix + sil_entrypoint.sh LD_LIBRARY_PATH fix）
> **来源 worktree**: `.worktrees/colregs-nlp-cpa-fix`（分支 `codex/colregs-nlp-cpa-fix` @ `c7a5da50d`）
> **前置证据**（必读）:
> 1. `runs/m5_v31_comparison/HO_CS_COMPARISON.md` — v3.1 在 GNC profile 下 HO/CS 实测
> 2. `docs/superpowers/specs/2026-07-19-m5-acados-dispatch-gate-v3-event-based-design.md` — v3.1 dispatch gate memo
> 3. `docs/superpowers/specs/2026-07-19-ho-red-execution-chain-diagnosis.md` — F2 ho RED 链路诊断

---

## 0. 核心认知（不要重蹈覆辙）

**Mid-MPC 60s replan 是设计意图，不是缺陷**（P4 VR-06b，memo §0）。Mid-MPC 负责中远距离 ample-time 避碰，优先 acados；BC-MPC 负责事件驱动紧急避让。**不要改 `solve_timer_` 周期**。

v3.1 评估失败的真因不在 dispatch gate（gate 工作正常），而在：
1. M5 内部 NLP 在退化输入上失败且不能复位（缺 Phase 3.10 gate）
2. M4 cold-start 没 clamp heading box → IPOPT 每周期 Infeasible（缺三层一致性修复）
3. BC-MPC reactive_override 在 GNC profile 是 dead letter（缺 F2 F-C 完整栈）

**所有缺失都来自 `colregs-nlp-cpa-fix`**。`m5-design-grounding` 的 `2884e9dbb` 显式 graft COLREGs toolchain 时排除了 M5 src/include，所以这些修复结构性缺失。

## 1. 分支拓扑

```
merge-base = 08d9b3c3
├── codex/colregs-nlp-cpa-fix   @ c7a5da50d  (+408 commits)  ← 修复源
└── codex/m5-design-grounding   @ c46e01045  (+96 commits, 60 touch M5)  ← 当前 v3.1 工作树
```

`git cherry codex/m5-design-grounding codex/colregs-nlp-cpa-fix` 显示 408 个 `+`（无等价检测）。两个分支在 M5 源码上结构性分叉。

## 2. 必移植项（按优先级）

### P0 — Phase 3.10 NLP gate（`a29962fe8`）

**症状**: CS 场景 M5 avoidance_plan publish = 0；spdlog 显示 `acatos=1 reason=all_pass` 但 cycle 没走到 publish_outputs_。

**根因**: v3.1 的 `on_solve_cycle_`（`mid_mpc_node.cpp:819-871`）无条件 `solver_.solve()`，且 `MidMpcSolver` 接口（`mid_mpc_solver.hpp:90-107`）**没有 `reset_consecutive_failures()` 方法**。colregs-nlp-cpa-fix 在 TRANSIT/RECOVERY 跳过 solve + 复位 counter。

**移植要点**:
- `mid_mpc_solver.hpp` 加 `void reset_consecutive_failures() noexcept { consecutive_failures_ = 0; }`
- `mid_mpc_node.cpp::on_solve_cycle_` 在 `is_transit || is_recovery` 时跳过 solve + 调用 `solver_.reset_consecutive_failures()`
- v3.1 的 acatos dispatch gate 在跳过路径**不能被绕过** — gate 是 cycle 内部决策，cycle 跳过则 dispatch 也跳过。这是设计正确的，因为 TRANSIT/RECOVERY 不需要避让解。

**cherry-pick 风险**: 🟢 低。`a29962fe8` 的核心改动是 cycle body 加 guard + solver 加 reset 方法，跟 v3.1 的 dispatch gate 改动正交。但它的 `scripts/run_colregs_clean_8probe.py` hunk（加 `SIL_NODE_CONTAINER`/`SIL_NODE_IMAGE_SUBSTR` env override）跟 v3.1 的 graft 冲突，需要手动 reapply（trivial）。

### P1 — 三层一致性修复（`f54b0d607`）

**症状**: HO spdlog 显示每周期 `IPOPT status=Infeasible_Problem_Detected iter=215`，`critical "Infeasible: collision unavoidable; M7 MRM expected"`。

**根因**: 三个 fix 互相依赖:
- **Fix 0**（M6）: `RuleAssessment publish` 没 gate on `conflict_detected` → M5 拿到的 colregs_constraint 语义混乱
- **Fix 1**（M4）: cold-start 时 `latest_gnc_odd_` SharedPtr 是 nullptr → heading box 没 clamp → IPOPT 在过宽的 heading 窗口上 Infeasible
- **Fix 2**（M5）: NLP heading-box 步进松弛 `RowBoundConfig::heading_hard_from_k`（`row_registry.hpp` + `mid_mpc_solver.cpp::solve()` 的 lbx/ubx loop）

**移植要点**:
- Fix 1（M4 cold-start）🟢 低风险，跟 v3.1 完全正交
- Fix 0（M6 publish gate）🟢 低风险
- **Fix 2（M5 row_registry）🔴 高风险** — v3.1 的 `mid_mpc_solver.cpp` 已被 P5/P6/P7 + acados dispatch path 大幅重写，`f54b0d607` patch 的 lbx/ubx loop 形状可能已不存在。**移植前必须先 diff 两个分支的 `mid_mpc_solver.cpp::solve()` 看 lbx/ubx loop 是否还在原位置**。

**cherry-pick 风险**: Fix 2 需要手动 port（不是 git cherry-pick），按 v3.1 的 solver 重构后的代码形状重新实现 heading-box 步进松弛语义。

### P2 — GNC bridge reactive_override 完整栈（F2 F-C dead letter）

**症状**: HO trace 显示 BC-MPC 发了 1187 次 `reactive_override_cmd`，但船 0° 转向。memo F2 F-C 已诊断，但 v3.1 没修。

**根因**: `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp` 在 v3.1 是 171 行，colregs-nlp-cpa-fix 是 334 行。v3.1 只订阅 `/l3/m5/avoidance_plan` + `/l2/planned_route` + `/l3/sim/reset_own_ship`，**完全没订阅 `/l3/m5/reactive_override_cmd`**。colregs-nlp-cpa-fix 还转发了 `/sil/lifecycle_status` 和 `/l3/m1/mrm_command` → `/safety/mrm_command`。

**移植要点**:
- 整个栈是 **additive**（新订阅 + 新 publisher + drain branch），cherry-pick 风险 🟢 低
- 但依赖 `translators.cpp` 的 `to_gnc_reactive_override` + `rebase_reactive_override_timebase`（+119 行）— 一起 port
- 配套: `812cbcaab` 系列（execution envelope + replay-state persistence）也建议一起 port，因为它们一起测试过

**cherry-pick 风险**: 🟢 低（pure additive），但要确认 v3.1 的 `ship_interfaces::msg::ReactiveOverrideCmd` IDL 跟 colregs-nlp-cpa-fix 一致。

### P3 — BehaviorPlan.msg schema 113→115 + structured avoidance_intent

**相关 commits**: `2654e6773`（M4 mirrors active_mrm_*）+ `8cdc8f3e4`（structured avoidance_intent: TURN_STBD/PORT/REDUCE_SPEED/MAINTAIN）

**移植要点**:
- msg schema 升级是 **跨模块合约改动**，必须 M4/M5/M7 同步 port
- v3.1 当前 M5 不消费这些字段，所以单独 port msg 字段是安全的（消费者在 P0/P1 的修复里）
- AGENTS.md 硬约束: ROS2 IDL 改动需要 ASDR `schema_version` bump + `rationale` 字段

**cherry-pick 风险**: 🟡 中（跨模块 schema）。

### P4 — `clear_bc_mpc_takeover` 释放路径

**症状**: v3.1 的 `mid_mpc_node.cpp:900-908` 只有 `mark_bc_mpc_takeover()`，**没有 `clear_bc_mpc_takeover()`**。一旦 BC-MPC 接管 latch 触发，永不释放。

**移植要点**: colregs-nlp-cpa-fix `mid_mpc_node.cpp:1435-1452` 的完整 takeover/release 二态机:
```cpp
const bool bcmpc_override_fresh = has_fresh_reactive_override_(cycle_now);
if (bc_mpc_should_take_over && bcmpc_override_fresh) { ... mark_bc_mpc_takeover(); }
else if (committed_route_manager_.bc_mpc_takeover_requested()) { ... clear_bc_mpc_takeover(); }
```

**cherry-pick 风险**: 🟡 中。跟 P0（Phase 3.10 gate）紧耦合，必须一起 port。

## 3. 不要移植的项（v3.1 本地设计决策）

| 项 | v3.1 状态 | 为什么不移植 |
|---|---|---|
| `solve_timer_` 60s | `9621a4e8f` 本地设计 | P4 VR-06b anti-chatter 设计意图。**不要改回 1Hz** |
| P6 keep-last abolish | `74f67e365` 本地设计 | v3.1 的 `publish_keep_last_` 改成空心跳。colregs-nlp-cpa-fix 是 keep-last + bcmpc_follow heartbeat。**冲突的设计决策，不移植** |
| `pub_health_` on `/l3/m5/bc_mpc/health`（P6 health metrics） | v3.1 本地加 | colregs-nlp-cpa-fix 没有。**v3.1 这条更好，保留** |
| acatos dispatch gate v3.1 | 本 memo 的核心 | colregs-nlp-cpa-fix 没有。**保留 v3.1** |

## 4. 执行顺序（新对话工作流）

按依赖关系 + 风险递增:

### Step 0: 复现基线（30 分钟）
- 在当前 v3.1 worktree 跑 HO + CS（`runs/m5_v31_comparison/` 已有），确认 M5 publish 计数（HO=2, CS=0）作为 BEFORE 基线
- 在 `colregs-nlp-cpa-fix` worktree 启动它的 stack（`COMPOSE_PROJECT_NAME=colregs-nlp-cpa-fix` + `gnc-profile-start.sh`），跑同样的 HO + CS，作为「修复后」对照组。**这是关键 A/B 证据** — 如果 colregs-nlp-cpa-fix 也 publish 少，说明根因不在这些 fix；如果 publish 多 + 避让成功，证明移植路径正确

### Step 1: P0 + P4 一起移植（Phase 3.10 gate + bc_mpc takeover release）
- 单独 commit，标题 `fix(m5): port Phase 3.10 NLP gate + bc_mpc takeover release from colregs-nlp-cpa-fix`
- 跑 M5 单测（`colcon test --packages-select m5_tactical_planner`）确认 882 PASS 不回归
- 跑 HO + CS 确认 cycle skip 行为正确 + CS publish 数变化

### Step 2: P1 移植（三层一致性）
- 分 3 个 commit: Fix 0（M6）→ Fix 1（M4 cold-start）→ Fix 2（M5 row_registry，**手动 port 不是 cherry-pick**）
- **Fix 2 前必须 diff 两分支 `mid_mpc_solver.cpp::solve()`**，确认 lbx/ubx loop 形状
- 每个 commit 后跑 HO 验证 IPOPT Infeasible 是否消失

### Step 3: P2 移植（GNC bridge reactive_override 完整栈）
- 单独 commit，纯 additive
- 跑 HO 验证 BC-MPC reactive_override 是否到 GNC + 船是否真的转

### Step 4: P3 移植（BehaviorPlan schema 升级）
- 跨模块，单独 commit
- bump ASDR schema_version + 加 rationale

### Step 5: A/B 对比验证
- 重跑 HO + CS 在 v3.1+fixes 上
- 对比 `colregs-nlp-cpa-fix` 的 HO + CS 结果
- 写对比报告: v3.1 acatos dispatch 路径在修复后的效率/能力提升

## 5. 关键文件（必读）

### v3.1（当前 worktree）
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_node.cpp`（cycle body + publish_outputs + bc_mpc takeover）
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/mid_mpc_solver.hpp`（solver 接口，缺 `reset_consecutive_failures()`）
- `src/l3_tdl_kernel/m5_tactical_planner/src/mid_mpc/mid_mpc_solver.cpp`（lbx/ubx loop）
- `src/l3_tdl_kernel/m5_tactical_planner/include/m5_tactical_planner/mid_mpc/row_registry.hpp`（heading_hard_from_k 缺失）
- `src/sim_workbench/gnc_bridge/src/gnc_bridge_node.cpp`（171 行，缺 reactive_override）
- `src/sim_workbench/gnc_bridge/src/translators.cpp`（缺 to_gnc_reactive_override）

### colregs-nlp-cpa-fix（来源 worktree `.worktrees/colregs-nlp-cpa-fix`）
- 同名文件，参考实现

### 共享
- `src/l3_msgs/msg/BehaviorPlan.msg`（v3.1 = 113, colregs-nlp-cpa-fix = 115）
- `src/l3_msgs/msg/ReactiveOverrideCmd.msg`（确认 IDL 一致）

## 6. 容器/stack 配置（避免重踩坑）

- v3.1 stack: `codex-gnc-validation`（已通过 `gnc-profile-start.sh` 启动；env `LD_LIBRARY_PATH` 修复已 bind-mount 持久）
- colregs-nlp-cpa-fix stack: `colregs-nlp-cpa-fix`（需要从该 worktree 跑 `gnc-profile-start.sh`，容器名 `colregs-nlp-cpa-fix-sil-nodes-1` 等）
- 跑 probe 时**必须**:
  - `NO_PROXY=127.0.0.1,localhost no_proxy=127.0.0.1,localhost`（否则 SSL EOF）
  - `--profile gnc` + 显式 `--restart-container codex-gnc-validation-sil-nodes-1` + `...-gnc-gnc-nodes-1` + `...-gnc-gnc-bridge-1`
  - **不要用 `--run-name`**（v3.1 的 `run_colregs_clean_8probe.py` 不支持，SKILL.md 跟代码不同步；用 legacy `--summary-out` + `--trace-report-dir`）

## 7. 验收门

每个 Step 完成后:
1. M5 单测 882 PASS 不回归（v3.1 T1-T15 + 846 旧测）
2. HO + CS ASDR trace 里 M5_Tactical_Planner records > 0（说明 cycle 跑到 publish_outputs_）
3. HO IPOPT `Infeasible_Problem_Detected` 次数显著下降（Step 2 后）
4. HO BC-MPC reactive_override 在 GNC bridge log 里看到 forward（Step 3 后）
5. 船实际转向（turn_starboard > 0°）

最终 A/B: v3.1+fixes vs colregs-nlp-cpa-fix 在 HO + CS 上的 KPI 对比（CPA / behavior_toggles / turn_mag / M5 publish count / acatos dispatch count），写成报告。

## 8. 不在本方案范围（明确 follow-up）

- acatos solver opts 优化（FUNNEL+adaptive LM）— F3
- acatos 验收场景集 + factorial sweep（标定 0.01/0.8/200m 真值）— F5
- 架构 [R17] 勘误 — 独立 PR
- SOTIF 性能限制登记（acatos ho 先天失败）— escalation to safety engineer
- RFC-004 Resolution #6 ASDR 加 M5/M3 mandatory event
- ASDR trace 在低频 publisher 上的 QoS 问题（独立 infra 任务）
