# INDEX — COLREGs 避碰 C1/C7 合规修复运行时验证（2026-06-18）

**状态**：Draft（待用户审批后进入实施）
**作者**：ZCode（GLM-5.2）
**日期**：2026-06-18
**范围**：C1（Rule 15 crossing release）+ C7（Rule 13 overtaking release）热修复的 **COLREGs 合规核对 + 运行时验证 + 整合卫生**
**前置文档**：
- `docs/superpowers/specs/2026-06-17-colregs-avoidance-fsm-design.md`（全 FSM 重写 spec，**本日不做**）
- `docs/superpowers/plans/2026-06-17-colregs-avoidance-fsm.md`（全 FSM 重写 plan，**本日不做**）
- `docs/Design/Review/2026-06-17/COLREGs_Avoidance_Decision_Logic_Report.md`（偏离报告 D-1~D-8）
- `COLREG-Consolidated-2018.pdf`（附件，权威规则文本）

---

## 1. 本日工作定位（一句话）

**不推进全 FSM 重写**。本日聚焦：**C1/C7 热修复（已 commit `ea6b06e6`）的 COLREGs 合规性核对 + 文档修正 + 运行时验证 + 主/辅 stack 分离**。

全 FSM 重写（spec/plan 2026-06-17）是另一条独立路线，待 C1/C7 热修复验证绿后再决策是否启动，**本日不动**。

---

## 2. 三件套导航

| 文件 | 角色 | 内容 |
|---|---|---|
| **本文档（INDEX）** | 入口 | 范围、状态、决策记录、合规核对结论摘要、与既有 spec/plan 的边界 |
| `docs/superpowers/specs/2026-06-18-colregs-c1c7-compliance.md` | Spec | COLREGs 合规核对（三红线）+ 验证条件定义 + 完成标准 |
| `docs/superpowers/plans/2026-06-18-colregs-c1c7-verification.md` | Plan | 7 Phase 实施计划（citation 修正→脏改动决策→stack 偏移→C1/C7 验证→8-probe 回归→整合→收尾）|

---

## 3. 关键状态快照（开工前）

### 3.1 分支/worktree

| 位置 | HEAD | 内容 | 状态 |
|---|---|---|---|
| 主 checkout `/Users/marine/Code/MASS-L3-Tactical Layer` | `codex/colregs-phase-gate-diag` @ `68b38095` | phase-gate 集成（C1-C7 phase semantics）+ gate bug 修复 | **9 文件 dirty**（内容 = worktree `ea6b06e6` 的 C1/C7 fix 副本）+ 2 未追踪 scenario dir |
| worktree `.worktrees/colregs-behavior-fix` | `ea6b06e6` | C1/C7 严格 release fix（已 commit）+ L4 Kd（`d37dde0d`）+ C2 gate（`c849f06c`）| C1/C7 source of truth |
| worktree `.worktrees/codex-colregs-avoidance-report` | `f8dbe8fd` | report/spec/plan 全 FSM 路线文档 | 静态文档，不动 |

### 3.2 运行时环境

| Stack | 状态 | 约束 |
|---|---|---|
| 主 stack `mass-l3-sil` | 7 容器全 Up | **绝不碰**（用户演示用，另一对话已重建）|
| behavior-fix stack `colregs-behavior-fix` | 全 Exited | 需起，但当前 host network + domain 0 与主 stack 冲突 |

---

## 4. 调研结论摘要（COLREGs 合规三红线）

详细推理见 Spec §3，结论：

### 🔴 红线 1 — C1 citation 错误（几何对，引用错）

`release_policy.hpp:11-13` 注释 + commit `ea6b06e6` message 写 "112.5° abaft beam from Rule 3(g)"。

**NLM 验证（🟢 high, maritime_regulations）**：Rule 3(g) 定义 "**vessel restricted in her ability to manoeuvre**"，与 abaft beam **无关**。

**正确来源**：Rule 13(b) "more than **22.5 degrees abaft her beam**" + Rule 21(c) sternlight 135° arc → 112.5° from bow。

→ **改 citation（注释 + commit doc），不动几何值 112.5°**

### 🟡 红线 2 — C7 aspect<90° 判据（工程近似，需文档说明）

C7 用 `aspect < 90°`（own-ship 在 target 前方半球）作 overtake release 条件。

**NLM 验证（🟢 high）**：
- forward hemisphere **alone 不足**（Steamship Mutual: "always safer to cross astern"）
- 但配合 `cpa_projection_past_and_safe` + `range >= cpa_safe` + `!range_closing` 三条件，符合 MASS 算法对 "finally past and clear" 的工程近似
- COLREGs 原文是定性"ordinary practice of seamen"，无单一数学阈值

→ **不改几何**，但**补充文档说明**：C7 是 "forward hemisphere + CPA/range/closing 多因素工程近似"，列出依据 + 局限，标注 [ref-工程近似] 而非 [A级-字面]

### 🔴 红线 3 — 主 checkout 脏改动重复（整合卫生）

主 checkout 9 文件 dirty = worktree `ea6b06e6` 已 commit 内容的副本。两处同源副本，整合易冲突。

→ **Plan Phase 2 决策**：discard 主 checkout dirty（worktree 是 source of truth）OR 主 checkout 单独 commit。倾向前者。

---

## 5. 与既有 spec/plan 的边界（避免路线混淆）

| 既有文档 | 本日关系 |
|---|---|
| `2026-06-17-colregs-avoidance-fsm-design.md`（全 FSM spec）| **不执行**。本日只做 C1/C7 热修验证 |
| `2026-06-17-colregs-avoidance-fsm.md`（全 FSM plan）| **不执行**。D-1~D-8 完整闭合延后 |
| `2026-06-17-colregs-phase-semantics-gate.md`（phase gate spec）| **已完成上线**（主 checkout commits），本日验证它暴露的 C1/C7 行为 |
| `COLREGs_Avoidance_Decision_Logic_Report.md`（偏离报告）| D-1~D-8 全范围偏离仍在，本日只闭合 phase gate 暴露的 C1/C7 子集 |

**路线决策（本日）**：C1/C7 热修先行 + 验证 → 8-probe 回归绿 → 再议是否启动全 FSM 重写。两线**不并行**。

---

## 6. 决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| 本日范围 | 仅 C1/C7 热修验证，不推进全 FSM | 两线并行返工风险高；热修已 commit 待验证，先闭合 |
| C1 几何值 112.5° | 保留 | NLM 确认 Rule 13(b)/21(c) 双重支持，值正确 |
| C1 citation | 改 Rule 3(g)→Rule 13(b)+Rule 21(c) | NLM 确认 Rule 3(g) 定义无关 |
| C7 aspect<90° 几何 | 保留，补文档说明 | NLM 确认 forward hemisphere alone 不足，但多条件工程近似合规 |
| 主 checkout dirty | Plan Phase 2 决策（倾向 discard）| worktree 是 source of truth |
| 主 stack | 绝不碰 | 用户演示保留 |
| behavior-fix stack | 独立 network + domain 43 偏移 | 避开主 stack host network + domain 0 |

---

## 7. 验证条件（Plan 完成定义）

| 项 | 标准 |
|---|---|
| 文档修正 | C1 citation 全部改对（注释 + commit doc + spec），几何不动 |
| 脏改动决策 | 主 checkout 状态明确（discard 或 commit），无残留歧义 |
| stack 隔离 | behavior-fix stack 起，主 stack 不受影响 |
| C1 运行时 | rule15-cs 过去 rel_brg≥112.5° 才 release，几何 KPI PASS |
| C7 运行时 | rule13-ot aspect<90° + CPA/range/closing 条件 release，几何 KPI PASS |
| 8-probe 回归 | colregs-clean-8probe skill 全绿（restart-between-runs）|
| 整合 | worktree `ea6b06e6` 整合路径明确（是否本日合入主 checkout 延后决策）|

---

## 8. 开放问题（需用户 sign-off）

1. **C7 是否要加 112.5° abaft beam 的 backstop？** NLM 指出 forward hemisphere alone 不足。当前靠 CPA/range/closing 兜底。可选：加 `abs(rel_bearing) >= 112.5°` 作为额外几何硬门。**本 Plan 默认不加**（多条件已合规），但 Spec §3.2 列为可选项。
2. **脏改动 discard vs commit**：Plan Phase 2 默认 discard，若你想主 checkout 也留 commit 则改。
3. **8-probe 跑在哪**：本 Plan 默认 local OrbStack（behavior-fix stack），A4000 验证延后（需 sync + A4000 gate，另立任务）。

---

**INDEX 结束。开始前请审批 Spec + Plan。**
