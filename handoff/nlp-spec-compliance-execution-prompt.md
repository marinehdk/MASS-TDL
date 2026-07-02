# 新对话实施提示词 — M5 NLP Spec-Compliance（subagent-driven + Codex review）

> **用途**：把"复制以下内容到新对话"section 全文粘贴到新 ZCode/Claude Code 对话的首条消息，即可开始实施。新对话用 subagent-driven-development 并行执行 + Codex 严格对照设计 review。

---

## 复制以下内容到新对话

---

## 任务：subagent-driven 并行实施 M5 NLP Spec-Compliance（10 Slices + Codex review）

我需要你用 **superpowers:subagent-driven-development** skill，并行实施一个 M5 Mid-MPC NLP 的完整升级。每个 Slice 派 fresh subagent 实施，实施后交 **codex-rescue 做严格对照 spec 的 code review**，review 通过我才确认进下一批。

这是 MASS（自主船）L3 战术决策层的避让轨迹生成器升级，目标是把 NLP 从"带部分 CPA/heading rows 的候选生成器"升级为 spec §9.3 完整的 normal 航线 NLP，产出 COLREGs 合规 + 时序连续 + GNC 可执行的稳定轨迹。

**spec 和 plan 已经过两轮 Codex 对抗评审 + 轻评审定稿，不要重新设计，按 plan 执行。**

### 环境

- 仓库：`/Users/marine/Code/MASS-L3-Tactical Layer`（git repo）
- 工作目录（worktree）：`/Users/marine/Code/MASS-L3-Tactical Layer/.worktrees/colregs-12probe-debug`
- branch：`codex/colregs-12probe-debug`
- 当前 HEAD：`cd6df0a9`（spec v3 + plan + 本提示词已 commit）
- 容器：`codex-gnc-validation-sil-nodes-1`（bind mount worktree src→/opt/ws/src，已运行）

**所有 git/build/test 操作都在 worktree 目录下执行，不要在主 checkout 操作。**

### 必读文件（开工前完整读完）

1. **Plan（执行手册）**：`.worktrees/colregs-12probe-debug/docs/superpowers/plans/2026-07-02-m5-nlp-spec-compliance.md`
   - 10 个 Task（Slice P0→V1），每个有精确文件路径 + TDD 步骤 + 代码骨架 + build/test 命令 + commit message。
2. **Spec（设计依据，review 对照标准）**：`.worktrees/colregs-12probe-debug/docs/superpowers/specs/2026-07-02-m5-nlp-spec-compliance-design.md`
   - v3 定稿，15 章节。Codex review 严格对照此 spec。重点章节：§3 NLP 完整定义、§4 route-frame、§5 terminal+TailBuilder、§6 continuity、§7 Rule13/direction、§8 前置 bug。

### 执行架构：subagent-driven + Codex review 两阶段

每个 Slice 走**两阶段 gate**：

**阶段 1 — subagent 实施（fresh subagent per Slice）**
- 用 superpowers:subagent-driven-development skill 派 fresh subagent
- subagent 按 plan 的 TDD 步骤实施：写失败测试 → 验证失败 → 最小实现 → 验证通过 → commit
- subagent 完成后返回：改了哪些文件、测试结果（PASS/FAIL + 数字）、commit hash

**阶段 2 — Codex 严格对照 spec review（codex-rescue agent）**
- subagent 完成后，派 codex-rescue agent 做 code review
- review 标准：**严格对照 spec v3**（不是泛泛 code review）。逐项检查：
  - 实现是否真正满足 spec 对应章节的要求（不只看测试 PASS，看实现逻辑是否闭合）
  - 是否引入 spec 明确禁止的东西（nonsmooth max/abs、调权重压 probe、mock/skip/forced-pass、vessel-specific 分支）
  - 是否有 spec 要求但实现遗漏的（cost 项缺失、约束缺失、降级项未声明）
  - cost/constraint 是否与 spec §3.2/§3.3 公式一致
  - 坐标系契约（§3.7）、row registry 契约（§3.8）、continuity 冻结对象（§6.1 H_commit 非 H_publish）是否正确
- Codex review 输出：PASS / 需修订（列具体问题 + 严重度 + spec 章节证据）
- **若 Codex review 需修订**：反馈给 subagent 修订（同一 subagent 或新派），修订后重 review，直到 Codex PASS
- **Codex PASS 后**：向我报告（subagent 产出 + Codex review 结论），等我确认进下一批

### 并行调度（依赖图）

```
Batch 1: P0（独立前置，单独一个 subagent）
         ↓
Batch 2: R1 ‖ N1（并行两个 subagent，都依赖 P0，互不依赖）
         ↓
Batch 3: T1 ‖ M1（并行；T1 依赖 R1，M1 独立依赖 P0）
         ↓
Batch 4: W1（依赖 T1 + M1）‖ C1（依赖 N1 + M1）  ← 可并行
         ↓
Batch 5: D1 ‖ O1（并行，都依赖 R1 + N1）
         ↓
Batch 6: V1（runtime 验证，依赖全部；这个由主 agent 跑，不派 subagent）
```

**每个 Batch 内的 Slice 并行派 subagent**。Batch 间串行（等上一批全部 Codex review PASS + 我确认后才进下一批）。

**并行风险注意**：同 Batch 的 subagent 可能改同一文件（如 `mid_mpc_node.cpp` 被 R1/M1/W1/C1 都改）。处理：
- 同 Batch 内尽量选不冲突的文件集（plan 的 File Structure 已尽量分离）
- 若冲突不可避免，串行化该 Batch 内冲突的 Slice（先 A commit，B rebase 后改）
- subagent 完成后检查 git diff 是否有意外文件被改

### Build / Test 命令（subagent 用）

```bash
# build + 单测（容器内）
docker exec codex-gnc-validation-sil-nodes-1 bash -lc 'source /opt/ros/humble/setup.bash; cd /opt/ws && colcon build --packages-select m5_tactical_planner --cmake-args -DBUILD_TESTING=ON && source install/setup.bash && build/m5_tactical_planner/<test_name>'

# rebuild + restart（symlink-install，restart 才加载新代码）
docker restart codex-gnc-validation-sil-nodes-1

# probe（host，从 worktree；V1 阶段用）
rtk python3 scripts/run_6_scenarios.py --profile gnc --restart-between-runs --sim-rate 5 --trace-report-dir runs/<tag> --summary-out runs/<tag>-summary.json --scenario colreg-rule14-ho
```

### Codex review 提示词模板（每个 Slice review 时用）

派 codex-rescue agent 时，用以下结构（替换 `<Slice>` 和 `<spec 章节>`）：

```
你是严格的 code reviewer。对照 spec v3 审查 Slice <X> 的实现。

## 审查对象
commit <hash>（Slice <X> 实施）
改了文件：<list>

## 审查标准（严格对照 spec，不只看测试 PASS）
spec 文件：docs/superpowers/specs/2026-07-02-m5-nlp-spec-compliance-design.md
重点对照章节：<§X.X>

逐项检查：
1. 实现是否真正满足 spec <§X.X> 的要求（实现逻辑闭合，非表面测试绿）
2. 是否引入 spec 禁止的：nonsmooth max/abs（§3.2/§5.4）、调权重压 probe、mock/skip/forced-pass、vessel-specific 分支
3. 是否有 spec 要求但遗漏的：cost 项/约束/降级声明
4. cost/constraint 公式是否与 spec §3.2/§3.3 一致
5. 坐标系（§3.7 WGS84）、row registry（§3.8）、continuity 冻结对象（§6.1 H_commit）是否正确
6. 是否有 plan 注明的"执行时细节"未处理（NED normal 符号、kParamDim static_assert）

输出：PASS / 需修订（列具体问题 + 严重度 Critical/High/Medium + spec 章节证据 + 修改建议）
```

### 关键约束（CLAUDE.md + spec，subagent + Codex 都遵守）

1. **不调 w_colreg/w_dist/w_vel/k_asym 权重压 probe 绿**（J_colreg redesign spec 固化）。新 cost 权重 [TBD-HAZID]。
2. **不 tune 阈值/场景几何/scorer gate** 让单 probe 变绿。无 mock/skip/forced-pass/vessel-specific 分支/scenario-id 条件。
3. **COLREGs 全链路 debug**：避让缺陷走全链路（L2→M2→M6→M4→M5→L4→M7→M8），先从 trace 证据分类失败 stage，再改。
4. **commit 前确认 HEAD**（worktree，branch `codex/colregs-12probe-debug`）。每个 Slice 一个 commit，message 按 plan 给的。
5. **不 push 到 GitHub/GitLab**（local gate 未过，A4000 未验证，按 CLAUDE.md promotion rule）。
6. 容器 `codex-gnc-validation-sil-nodes-1` 是 bind mount，rebuild 后须 `docker restart` 才加载新代码。
7. **并行 subagent 改同一文件时**：先检查 plan File Structure 是否分离；若冲突，Batch 内串行化冲突 Slice。

### 验证目标（V1 最终验收，spec §10.3）

rule14-ho（主验收，GNC profile, sim-rate 5, restart-between-runs）：
- steering_reversals < 50（当前 baseline 1660）
- int_abs_xte < 300000（当前 1,587,980）
- route_return PASS（当前 FAIL）
- CPA ≥ cpa_hard_m (1852) 全程
- 无 180° port/starboard 翻转

**降级声明（诚实）**：不保证 overall_pass=True（spec §3.5 降级项 risk covariance/ship-domain/no-crossing-ahead 未进 NLP）。验收目标：continuity + route-return + terminal tail-extension 三项主目标显著改善。

### 已知背景（不用重新调研，已 pinned）

- **根因（handoff pinned）**：NLP 每 cycle 贪婪重解 90s 时域，psi[0] 自由决策变量只受 heading box + intra-horizon ROT 约束，warm-start 续接上一 cycle 解 → 累积过转 → 极限环（steering_reversals 1660、int_abs_xte 1.59M、port/starboard 180° 翻转）。
- **committed-route Slice A-K 已实现**（publish 门控、manager、TailBuilder 类、preflight），但 **on_solve_cycle_ 的 NLP solve 逻辑没动**（仍贪婪重解），TailBuilder/manager 未接入 normal path。
- **本 plan 修核心**：NLP 内部加 route-frame/terminal/continuity 约束 + TailBuilder active-phase 接线 + manager prefix 数据源。
- handoff 详见 `.worktrees/colregs-12probe-debug/handoff/workspace_log.md` 最后两条（2026-07-02 + cont.）。

### 开工

1. 先读 plan 全文 + spec 全文。
2. 从 **Batch 1: Task P0**（前置 bug：zone 积分方向 + risk weight 死代码）开始：
   - 派 fresh subagent 实施 P0（按 plan TDD 步骤）
   - P0 完成后派 codex-rescue review（对照 spec §8）
   - Codex PASS 后向我报告，我确认进 Batch 2
3. Batch 2 起按并行调度（R1 ‖ N1 两个 subagent 同时派）。

如果 plan 某处不清楚或与代码现状冲突，停下来问我，不要自行臆断。如果 Codex review 发现 spec 级问题（非实现 bug），也停下来问我，可能需要回 spec 修订。

---

## （以上为复制内容，以下为本文件元信息，不复制）

### 这个提示词覆盖的上下文

- 环境（worktree/branch/HEAD/容器）
- 必读文件（plan + spec 路径）
- 执行架构（subagent-driven + Codex review 两阶段 gate）
- 并行调度（6 Batch，依赖图，同 Batch 冲突处理）
- Codex review 提示词模板
- build/test/probe 命令
- 关键约束（权重/阈值/全链路/commit/push/并行冲突）
- 验证目标 + 降级声明
- 已知背景（根因 + committed-route 现状）
- 开工指令

### 新对话 agent 需要的 skills

- `superpowers:subagent-driven-development`（fresh subagent per Slice + review）
- `codex:codex-rescue`（严格对照 spec code review）
- `superpowers:test-driven-development`（每 Slice TDD）
- `superpowers:verification-before-completion`（验证后 commit）
