# 新对话实施提示词 — M5 NLP Spec-Compliance 实现计划

> **用途**：把这段全文复制到新 ZCode/Claude Code 对话的首条消息，即可开始实施，无需额外上下文。新对话从 P0 开始，逐 Task 执行 plan。

---

## 复制以下内容到新对话

---

## 任务：执行 M5 NLP Spec-Compliance 实现计划（10 Slices）

我需要你按照已就绪的 spec + plan，逐 Task 实施一个 M5 Mid-MPC NLP 的完整升级。这是 MASS（自主船）L3 战术决策层的避让轨迹生成器升级，目标是把 NLP 从"带部分 CPA/heading rows 的候选生成器"升级为 spec §9.3 完整的 normal 航线 NLP，产出 COLREGs 合规 + 时序连续 + GNC 可执行的稳定轨迹。

**spec 和 plan 已经过两轮 Codex 对抗评审 + 轻评审定稿，不要重新设计，直接按 plan 执行。**

### 环境

- 仓库：`/Users/marine/Code/MASS-L3-Tactical Layer`（git repo）
- 工作目录（worktree）：`/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- branch：`codex/colregs-12probe-debug`
- 当前 HEAD：`9bde5abe`（spec v3 + plan 已 commit）
- 容器：`codex-gnc-validation-sil-nodes-1`（bind mount worktree src→/opt/ws/src，已运行）

**所有 git/build/test 操作都在 worktree 目录下执行，不要在主 checkout 操作。**

### 必读文件（按顺序，实施前完整读完）

1. **Plan（执行依据）**：`.worktrees/colregs-12probe-debug/docs/superpowers/plans/2026-07-02-m5-nlp-spec-compliance.md`
   - 这是你的执行手册。10 个 Task（Slice P0→V1），每个 Task 有精确文件路径 + TDD 步骤 + 代码骨架 + build/test 命令 + commit message。
2. **Spec（设计依据，有疑问时查）**：`.worktrees/colregs-12probe-debug/docs/superpowers/specs/2026-07-02-m5-nlp-spec-compliance-design.md`
   - v3 定稿。15 章节。重点：§3 NLP 完整定义、§4 route-frame、§5 terminal+TailBuilder、§6 continuity、§7 Rule13/direction。

### 执行方式

使用 **superpowers:executing-plans** skill（inline 批量执行 + checkpoint）。按 plan 的 Task 顺序逐个实施：

1. 每个 Task 严格按 TDD：先写失败测试 → 验证失败 → 最小实现 → 验证通过 → commit。
2. 每个 Task 完成后停下来，向我报告：改了哪些文件、测试结果（PASS/FAIL + 数字）、commit hash。等我确认后再进下一个 Task。
3. 依赖顺序：**P0 → (R1 ‖ N1 可并行) → T1 → M1 → W1 → C1 → D1 → O1 → V1**。
4. 遇到 plan 里的"执行时细节"（如 NED normal 符号约定、kParamDim static_assert 实际值、fixture 细化），按现有代码模式（参考 `test/unit/test_midmpc_tail_gate.cpp` 的 fixture 写法）和编译器反馈决定，不要回头改 spec/plan，在 commit message 或报告里注明即可。

### Build / Test 命令（全局参考）

```bash
# build + 单测（容器内）
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && build/m5_tactical_planner/<test_name>'

# rebuild + restart（symlink-install，restart 才加载新代码）
docker restart codex-gnc-validation-sil-nodes-1

# probe（host，从 worktree；V1 阶段用）
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/<tag> --summary-out runs/<tag>-summary.json --scenario colreg-rule14-ho
```

### 关键约束（CLAUDE.md + spec，实施期间绝对遵守）

1. **不调 w_colreg/w_dist/w_vel/k_asym 权重压 probe 绿**（J_colreg redesign spec 固化）。新 cost（J_route/J_terminal）权重用 [TBD-HAZID] 标注，HAZID RUN-001 校准。
2. **不 tune 阈值/场景几何/scorer gate** 让单 probe 变绿。无 mock/skip/forced-pass/vessel-specific 分支/scenario-id 条件。
3. **COLREGs 全链路 debug**：避让缺陷走全链路（L2→M2→M6→M4→M5→L4→M7→M8），先从 trace 证据分类失败 stage 契约，再改。
4. **commit 前确认 HEAD**（worktree，branch `codex/colregs-12probe-debug`）。每个 Task 一个 commit，commit message 按 plan 给的。
5. **不 push 到 GitHub/GitLab**（local gate 未过，A4000 未验证，按 CLAUDE.md promotion rule）。
6. 容器 `codex-gnc-validation-sil-nodes-1` 是 bind mount，rebuild 后须 `docker restart` 才加载新代码。

### 验证目标（V1 最终验收，spec §10.3）

rule14-ho（主验收，GNC profile, sim-rate 5, restart-between-runs）：
- steering_reversals < 50（当前 baseline 1660）
- int_abs_xte < 300000（当前 1,587,980）
- route_return PASS（当前 FAIL）
- CPA ≥ cpa_hard_m (1852) 全程
- 无 180° port/starboard 翻转

**降级声明（诚实）**：不保证 overall_pass=True（spec §3.5 降级项 risk covariance/ship-domain/no-crossing-ahead 未进 NLP）。验收目标：continuity + route-return + terminal tail-extension 三项主目标显著改善，CPA/risk 合规靠现有 hard floor + tail-gate + fallback 兜底。

### 已知背景（不用重新调研，已 pinned）

- **根因（handoff pinned）**：NLP 每 cycle 贪婪重解 90s 时域，psi[0] 自由决策变量只受 heading box + intra-horizon ROT 约束，warm-start 续接上一 cycle 解 → 累积过转 → 极限环（steering_reversals 1660、int_abs_xte 1.59M、port/starboard 180° 翻转）。
- **committed-route Slice A-K 已实现**（publish 门控、manager、TailBuilder 类、preflight），但 **on_solve_cycle_ 的 NLP solve 逻辑没动**（仍贪婪重解），TailBuilder/manager 未接入 normal path。
- **本 plan 修核心**：NLP 内部加 route-frame/terminal/continuity 约束 + TailBuilder active-phase 接线 + manager prefix 数据源。
- handoff 详见 `.worktrees/colregs-12probe-debug/handoff/workspace_log.md` 最后两条（2026-07-02 + cont.）。

### 开工

先读 plan 全文，然后从 **Task P0**（前置 bug：zone 积分方向 + risk weight 死代码）开始。P0 是独立小修，TDD 验证通过 commit 后报告，我确认后进 R1/N1。

如果 plan 某处不清楚或与代码现状冲突，停下来问我，不要自行臆断。

---

## （以上为复制内容，以下为本文件的元信息，不需要复制）

### 这个提示词覆盖的上下文

- 环境（worktree/branch/HEAD/容器）
- 必读文件（plan + spec 路径）
- 执行方式（executing-plans, TDD, checkpoint）
- build/test/probe 命令
- 关键约束（权重/阈值/全链路/commit/push 禁忌）
- 验证目标 + 降级声明
- 已知背景（根因 pinned + committed-route 现状）
- 开工指令

### 新对话 agent 需要的 skills

- `superpowers:executing-plans`（批量执行 + checkpoint）
- `superpowers:test-driven-development`（每 Task TDD）
- `superpowers:verification-before-completion`（每 Task 验证后 commit）
